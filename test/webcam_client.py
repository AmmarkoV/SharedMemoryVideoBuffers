#!/usr/bin/env python3
"""Client for the pyWebcamTest.sh benchmark.

Reads every new frame of the stream webcam_publisher.py publishes, the way the
classifier does: poll get_timestamp() until it changes, then read the frame.

  --mode copy  read_from_shared_memory(), then "process" the copy for --work-ms
  --mode view  zero-copy read_frame(), "processing" the view for --work-ms while
               holding its slot

For every frame it checks the counter the publisher stamped into the first and
last 4 bytes: they differ in a torn frame, and gaps between frames are frames
this client skipped. In view mode the counters are checked again after the
work, which catches a slot reused while the view was held. Latency is capture
to read, and leaves out the first frame, which was captured before this client
started waiting for it.

Prints one "RESULT client=<id> ..." line when done.
"""
import argparse
import os
import sys
import time

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "python"))
from SharedMemoryManager import SharedMemoryManager


def percentiles_ms(samples_ms):
    if not samples_ms:
        return "p50=- p99=- max=-"
    ms = np.asarray(samples_ms)
    return "p50=%.2f p99=%.2f max=%.2f" % (np.percentile(ms, 50), np.percentile(ms, 99), ms.max())


def counters(frame):
    pixels = frame.reshape(-1)
    return int(pixels[:4].view(np.uint32)[0]), int(pixels[-4:].view(np.uint32)[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--library", required=True, help="path to libSharedMemoryVideoBuffers.so")
    parser.add_argument("--descriptor", default="shmvb_webcam_bench.shm")
    parser.add_argument("--stream", default="webcam")
    parser.add_argument("--id", default="0")
    parser.add_argument("--mode", choices=("copy", "view"), default="copy")
    parser.add_argument("--work-ms", type=float, default=0, help="simulated processing time per frame")
    parser.add_argument("--duration", type=float, default=10, help="seconds to read, from the first frame")
    parser.add_argument("--connect-timeout", type=float, default=15, help="seconds to wait for the stream to appear")
    args = parser.parse_args()

    smm = None
    give_up = time.time() + args.connect_timeout
    while smm is None:
        try:
            smm = SharedMemoryManager(os.path.abspath(args.library), descriptor=args.descriptor,
                                      frameName=args.stream, connect=True)
        except RuntimeError:
            if time.time() > give_up:
                print("RESULT client=%s FAILED: stream never appeared" % args.id)
                return 1
            time.sleep(0.1)

    frames = skipped = torn = failed_reads = 0
    latencies_ms, read_ms = [], []
    last_counter = None
    last_timestamp = None
    start = None
    last_frame_at = time.perf_counter()
    while (start is None) or (time.perf_counter() - start < args.duration):
        if time.perf_counter() - last_frame_at > 5:
            print("client %s: no new frame for 5 s, stopping early" % args.id, file=sys.stderr)
            break
        timestamp = smm.get_timestamp()  # None until the first frame is published
        if (timestamp is None) or (timestamp == last_timestamp):
            time.sleep(0.001)
            continue

        read_start = time.perf_counter()
        if args.mode == "copy":
            frame = smm.read_from_shared_memory()
            read_done = time.perf_counter()
            read_ns = time.time_ns()
            if frame is None:
                failed_reads += 1
                time.sleep(0.01)
                continue
            head, tail = counters(frame)
            frame_torn = head != tail
            if args.work_ms > 0:
                time.sleep(args.work_ms / 1000.0)
        else:
            with smm.read_frame() as view:
                read_done = time.perf_counter()
                read_ns = time.time_ns()
                if view is None:
                    failed_reads += 1
                    time.sleep(0.01)
                    continue
                head, tail = counters(view)
                if args.work_ms > 0:
                    time.sleep(args.work_ms / 1000.0)
                frame_torn = (head != tail) or (counters(view) != (head, tail))
        timestamp = smm.unix_timestamp
        if timestamp == last_timestamp:
            continue  # get_timestamp() saw a newer frame than the one read ended up being

        if start is None:
            start = time.perf_counter()
        last_frame_at = time.perf_counter()
        last_timestamp = timestamp
        frames += 1
        torn += frame_torn
        if (last_counter is not None) and (not frame_torn) and (head > last_counter + 1):
            skipped += head - last_counter - 1
        if not frame_torn:
            last_counter = head
        if frames > 1:
            latencies_ms.append((read_ns - timestamp) / 1e6)  # capture -> read, excluding the work
        read_ms.append((read_done - read_start) * 1000.0)

    if start is None:
        print("RESULT client=%s FAILED: no frames received" % args.id)
        return 1
    elapsed = time.perf_counter() - start
    print("RESULT client=%s mode=%s work_ms=%g frames=%d fps=%.1f skipped=%d torn=%d failed_reads=%d latency_ms %s read_ms %s" %
          (args.id, args.mode, args.work_ms, frames, frames / elapsed, skipped, torn, failed_reads,
           percentiles_ms(latencies_ms), percentiles_ms(read_ms)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
