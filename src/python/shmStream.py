"""OpenCV-compatible capture source that reads a SharedMemoryVideoBuffers stream.

    cap = SharedMemoryStreamer("shm://stream1")
    while cap.isOpened():
        ret, frame = cap.read()   # frame is a BGR uint8 array the caller owns

Addresses are shm://<stream>, using the default video_frames.shm context, or
shm://<context>/<stream>.

Like a webcam, read() blocks until a frame newer than the last one returned
arrives. Frames published while the caller is busy are skipped, so read() always
gives the latest one.

This file works unchanged from the SharedMemoryVideoBuffers repository and when
copied into another project. Outside the repository it finds the library through
the SHMVB_ROOT environment variable, or a SharedMemoryVideoBuffers checkout next
to that project.
"""
import os
import sys
import time

import cv2
import numpy as np

SHM_PREFIX = "shm://"
DEFAULT_DESCRIPTOR = "video_frames.shm"
LIBRARY_NAME = "libSharedMemoryVideoBuffers.so"


def parse_shm_url(url):
    """Splits shm://<stream> or shm://<context>/<stream> into (context, stream)."""
    if not url.startswith(SHM_PREFIX):
        raise ValueError("Not a shared memory address: '%s'" % url)
    parts = url[len(SHM_PREFIX):].split("/")
    if len(parts) == 1:
        parts.insert(0, DEFAULT_DESCRIPTOR)
    if (len(parts) != 2) or (not parts[0]) or (not parts[1]):
        raise ValueError("Expected shm://<stream> or shm://<context>/<stream>, got '%s'" % url)
    return parts[0], parts[1]


def _find_shmvb_root():
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(here, "..", ".."),                                     # src/python in this repository
        os.path.join(here, "..", "..", "..", "SharedMemoryVideoBuffers"),   # <project>/<package>/streams next to a checkout
        os.path.join(here, "..", "..", "SharedMemoryVideoBuffers"),         # <project>/streams next to a checkout
    ]
    if os.environ.get("SHMVB_ROOT"):
        candidates.insert(0, os.environ["SHMVB_ROOT"])
    for root in candidates:
        if os.path.exists(os.path.join(root, LIBRARY_NAME)) and \
           os.path.exists(os.path.join(root, "src", "python", "SharedMemoryManager.py")):
            return os.path.abspath(root)
    raise RuntimeError("Could not find a built SharedMemoryVideoBuffers (%s), "
                       "build it with make and/or set SHMVB_ROOT to its directory" % LIBRARY_NAME)


def _to_bgr(view):
    # Publishers in this repository write RGB, while OpenCV consumers expect BGR.
    # cvtColor allocates, so the result stays valid after the read_frame() block.
    channels = view.shape[2]
    if channels == 3:
        return cv2.cvtColor(view, cv2.COLOR_RGB2BGR)
    if channels == 4:
        return cv2.cvtColor(view, cv2.COLOR_RGBA2BGR)
    if channels == 1:
        return cv2.cvtColor(view, cv2.COLOR_GRAY2BGR)
    # Same fallback as viewer.c for other channel counts
    return cv2.cvtColor(np.mean(view, axis=2).astype(np.uint8), cv2.COLOR_GRAY2BGR)


class SharedMemoryStreamer:

    def __init__(self, url="shm://stream1", connect_timeout=5.0, read_timeout=1.0):
        """connect_timeout is how long to wait for the stream and its first frame,
        so a consumer can start just before its publisher. read_timeout is how long
        read() waits for a new frame before returning (False, None)."""
        self.url = url
        self.descriptor, self.stream = parse_shm_url(url)
        self.read_timeout = read_timeout
        self.should_stop = False
        self.image_np = None
        self.smm = None
        self.width = 0
        self.height = 0
        self.last_timestamp = None

        root = _find_shmvb_root()
        python_dir = os.path.join(root, "src", "python")
        if python_dir not in sys.path:
            sys.path.insert(0, python_dir)
        from SharedMemoryManager import SharedMemoryManager

        give_up = time.monotonic() + connect_timeout
        while self.smm is None:
            try:
                self.smm = SharedMemoryManager(os.path.join(root, LIBRARY_NAME), descriptor=self.descriptor,
                                               frameName=self.stream, connect=True)
            except RuntimeError as err:
                if time.monotonic() >= give_up:
                    print("Could not open %s: %s" % (url, err), file=sys.stderr)
                    return
                time.sleep(0.1)

        # A webcam knows its size once opened. The first frame stays unconsumed
        # (last_timestamp is untouched), so the first read() still returns it.
        while True:
            with self.smm.read_frame() as view:
                if view is not None:
                    self.width, self.height = self.smm.width, self.smm.height
                    return
            if time.monotonic() >= give_up:
                return
            time.sleep(0.001)

    def isOpened(self):
        return self.smm is not None

    def release(self):
        self.should_stop = True
        self.smm = None

    def grab(self):
        # Shared memory only holds the latest frame, so there is nothing to skip.
        # This has to return at once: frame-drop loops call it to catch up.
        return self.smm is not None

    def retrieve(self):
        return self.read()

    def read(self):
        if self.smm is None:
            return False, None
        give_up = time.monotonic() + self.read_timeout
        while True:
            # Zero-copy: the conversion in _to_bgr is the only copy of the frame
            with self.smm.read_frame() as view:
                if (view is not None) and (self.smm.unix_timestamp != self.last_timestamp):
                    self.last_timestamp = self.smm.unix_timestamp
                    self.width, self.height = self.smm.width, self.smm.height
                    self.image_np = _to_bgr(view)
                    return True, self.image_np
            if time.monotonic() >= give_up:
                return False, None
            time.sleep(0.001)

    def get(self, prop):
        # Unknown properties return 0 like cv2.VideoCapture. That includes
        # CAP_PROP_FPS, since the rate is set by the publisher and isn't stored.
        if prop == cv2.CAP_PROP_FRAME_WIDTH:
            return float(self.width)
        if prop == cv2.CAP_PROP_FRAME_HEIGHT:
            return float(self.height)
        return 0.0

    def set(self, prop, value):
        return False  # the publisher decides size and framerate

    def visualize(self, windowname='Shared Memory Stream', width=800, height=600):
        if self.image_np is not None:
            cv2.imshow(windowname, cv2.resize(self.image_np, (width, height)))
            if cv2.waitKey(1) & 0xFF == ord('q'):
                cv2.destroyAllWindows()
                self.should_stop = True


if __name__ == '__main__':
    source = sys.argv[1] if (len(sys.argv) > 1) else "shm://stream1"
    cap = SharedMemoryStreamer(source)
    while cap.isOpened() and not cap.should_stop:
        cap.read()
        cap.visualize()
    cap.release()
