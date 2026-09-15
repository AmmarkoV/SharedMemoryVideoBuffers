#!/usr/bin/env python3
"""Regression tests for the Python bindings in src/python/SharedMemoryManager.py.

   read_copy        - read_from_shared_memory() returns a frame later writes can't change
   read_frame       - `with smm.read_frame() as view:` keeps the view's slot from being
                      reused until the block exits, the view is read-only, reading the
                      same manager again inside the block raises, and an exception in
                      the block still releases the slot
   set_timestamp    - set_timestamp() re-stamps the latest frame and never releases a
                      writer lock held by someone else
   missing_stream   - connecting to a stream/descriptor that doesn't exist raises an
                      error that names it
   copy_validation  - copy_numpy_to_shared_memory() writes non-contiguous arrays in
                      logical order and rejects arrays of the wrong dtype or size
   restarted_stream - a reader follows a stream that its publisher re-created at a
                      new size, even when it lands in a different slot

Exit code: 0 = all checks passed, 2 = a check failed, 1 = setup error.

Usage: test_shared_memory_manager.py <libSharedMemoryVideoBuffers.so> [shm_name] [stream_name]
"""
import ctypes
import gc
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "python"))
from SharedMemoryManager import SharedMemoryManager

WIDTH, HEIGHT, CHANNELS = 64, 48, 3

failures = 0


def check(passed, name):
    global failures
    print("  %s : %s" % ("ok  " if passed else "FAIL", name), file=sys.stderr)
    if not passed:
        failures += 1


def raised(exceptionType, function, messageMustContain=""):
    try:
        function()
    except exceptionType as e:
        return messageMustContain in str(e)
    except Exception:
        return False
    return False


def solidFrame(value):
    return np.full((HEIGHT, WIDTH, CHANNELS), value, dtype=np.uint8)


def main():
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 1
    libraryPath = os.path.abspath(sys.argv[1])
    shmName     = sys.argv[2] if len(sys.argv) > 2 else "shmvb_test_py.shm"
    streamName  = sys.argv[3] if len(sys.argv) > 3 else "py"

    os.environ.pop("SHMVB_BUFFER_COUNT", None)  # read_frame cases assume the default of 2 slots
    lib = ctypes.CDLL(libraryPath)
    lib.createSharedMemoryContextDescriptor.argtypes = [ctypes.c_char_p]
    if lib.createSharedMemoryContextDescriptor(shmName.encode("utf-8")) != 0:
        return 1

    writer = SharedMemoryManager(libraryPath, descriptor=shmName, frameName=streamName,
                                 width=WIDTH, height=HEIGHT, channels=CHANNELS)
    reader = SharedMemoryManager(libraryPath, descriptor=shmName, frameName=streamName, connect=True)

    # read_copy
    writer.copy_numpy_to_shared_memory(solidFrame(1))
    first = reader.read_from_shared_memory()
    for value in (2, 3, 4):
        writer.copy_numpy_to_shared_memory(solidFrame(value))
    check(first is not None and np.all(first == 1), "read_copy: a returned frame is unaffected by later writes")

    # read_frame
    writer.copy_numpy_to_shared_memory(solidFrame(20))
    with reader.read_frame() as view:
        writer.copy_numpy_to_shared_memory(solidFrame(21))  # fills the other slot
        # With 2 slots the next write would have to reuse the view's slot, so it must fail instead
        secondWriteRefused = raised(RuntimeError, lambda: writer.copy_numpy_to_shared_memory(solidFrame(22)))
        unchanged = view is not None and np.all(view == 20)
        readOnly = view is not None and raised(ValueError, lambda: view.__setitem__((0, 0, 0), 0))
        nestedReadsRefused = raised(RuntimeError, reader.read_from_shared_memory) and raised(RuntimeError, reader.get_timestamp)
    check(unchanged and secondWriteRefused, "read_frame: the view's slot isn't reused while the block is open")
    check(readOnly, "read_frame: the view is read-only")
    check(nestedReadsRefused, "read_frame: reading the same manager inside the block raises")
    writer.copy_numpy_to_shared_memory(solidFrame(23))
    latest = reader.read_from_shared_memory()
    check(latest is not None and np.all(latest == 23), "read_frame: the slot is released when the block exits")
    try:
        with reader.read_frame() as view:
            raise KeyError("raised inside the block")
    except KeyError:
        pass
    releasedAfterException = not raised(RuntimeError, lambda: [writer.copy_numpy_to_shared_memory(solidFrame(v)) for v in (24, 25, 26)])
    latest = reader.read_from_shared_memory()
    check(releasedAfterException and latest is not None and np.all(latest == 26),
          "read_frame: an exception inside the block still releases the slot")

    # set_timestamp
    writer.copy_numpy_to_shared_memory(solidFrame(5), unix_timestamp=500)
    writer.set_timestamp(777)
    check(reader.get_timestamp() == 777, "set_timestamp: latest frame carries the new timestamp")
    check(np.all(reader.read_from_shared_memory() == 5), "set_timestamp: latest frame's pixels are unchanged")
    lockByte = ctypes.c_char.from_address(writer.frame)  # VideoFrame.locked is the struct's first field
    lockByte.value = b"\x01"                              # another writer is mid-write
    failedWhileLocked = raised(RuntimeError, lambda: writer.set_timestamp(888))
    stillLocked = lockByte.value == b"\x01"
    lockByte.value = b"\x00"
    check(failedWhileLocked and stillLocked, "set_timestamp: doesn't release a writer lock held by someone else")

    # missing_stream
    check(raised(RuntimeError,
                 lambda: SharedMemoryManager(libraryPath, descriptor=shmName, frameName="no_such_stream", connect=True),
                 "no_such_stream"),
          "missing_stream: a missing stream raises an error naming it")
    check(raised(RuntimeError,
                 lambda: SharedMemoryManager(libraryPath, descriptor="no_such_descriptor.shm", frameName=streamName, connect=True),
                 "no_such_descriptor.shm"),
          "missing_stream: a missing descriptor raises an error naming it")

    # copy_validation
    pattern = (np.arange(WIDTH * HEIGHT * CHANNELS) % 251).astype(np.uint8).reshape(WIDTH, HEIGHT, CHANNELS)
    nonContiguous = pattern.transpose(1, 0, 2)  # shape (HEIGHT, WIDTH, CHANNELS), strided view
    writer.copy_numpy_to_shared_memory(nonContiguous)
    check(np.array_equal(reader.read_from_shared_memory(), nonContiguous),
          "copy_validation: a non-contiguous array is written in logical order")
    writer.copy_numpy_to_shared_memory(solidFrame(9))
    check(raised(TypeError, lambda: writer.copy_numpy_to_shared_memory(solidFrame(10).astype(np.float64))),
          "copy_validation: wrong dtype raises TypeError")
    check(raised(ValueError, lambda: writer.copy_numpy_to_shared_memory(np.full((HEIGHT, WIDTH, 1), 11, dtype=np.uint8))),
          "copy_validation: wrong size raises ValueError")
    check(np.all(reader.read_from_shared_memory() == 9), "copy_validation: rejected arrays don't change the published frame")

    # restarted_stream - the publisher goes away (destroying its stream), another
    # stream takes the freed slot, and the publisher comes back at twice the width
    del writer
    gc.collect()
    blocker = SharedMemoryManager(libraryPath, descriptor=shmName, frameName=streamName + "_blocker",
                                  width=WIDTH, height=HEIGHT, channels=CHANNELS)
    writer = SharedMemoryManager(libraryPath, descriptor=shmName, frameName=streamName,
                                 width=WIDTH * 2, height=HEIGHT, channels=CHANNELS)
    writer.copy_numpy_to_shared_memory(np.full((HEIGHT, WIDTH * 2, CHANNELS), 40, dtype=np.uint8))
    restarted = reader.read_from_shared_memory()
    check(restarted is not None and restarted.shape == (HEIGHT, WIDTH * 2, CHANNELS) and np.all(restarted == 40),
          "restarted_stream: a reader follows the re-created stream to its new slot and size")

    del reader
    del writer
    del blocker
    if os.path.exists("/dev/shm/" + shmName):
        os.remove("/dev/shm/" + shmName)

    print("test_shared_memory_manager: %d failure(s)" % failures, file=sys.stderr)
    return 2 if failures > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
