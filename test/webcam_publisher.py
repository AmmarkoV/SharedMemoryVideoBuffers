#!/usr/bin/env python3
"""Webcam publisher for the pyWebcamTest.sh benchmark.

Captures from a webcam with OpenCV and publishes every frame into a shared memory
stream through SharedMemoryManager. Each frame carries its capture time (Unix
nanoseconds) as its timestamp, so clients can measure latency, and a frame
counter in its first and last 4 bytes, so clients can count skipped frames and
detect torn ones (a frame whose head and tail come from different writes).

Prints one "RESULT publisher ..." line when done.
"""
import argparse
import os
import sys
import time

import cv2
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "python"))
from SharedMemoryManager import SharedMemoryManager


def percentiles_ms(samples_seconds):
    if not samples_seconds:
        return "p50=- p99=- max=-"
    ms = np.asarray(samples_seconds) * 1000.0
    return "p50=%.2f p99=%.2f max=%.2f" % (np.percentile(ms, 50), np.percentile(ms, 99), ms.max())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--library", required=True, help="path to libSharedMemoryVideoBuffers.so")
    parser.add_argument("--descriptor", default="shmvb_webcam_bench.shm")
    parser.add_argument("--stream", default="webcam")
    parser.add_argument("--device", default="0", help="camera index or device path (e.g. /dev/video0)")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fps", type=float, default=30)
    parser.add_argument("--fourcc", default="MJPG", help="capture format; raw YUYV is usually limited to low framerates at large sizes")
    parser.add_argument("--duration", type=float, default=10, help="seconds to publish")
    args = parser.parse_args()

    device = int(args.device) if args.device.isdigit() else args.device
    cap = cv2.VideoCapture(device, cv2.CAP_V4L2)
    if not cap.isOpened():
        print("webcam_publisher: couldn't open camera %s" % args.device, file=sys.stderr)
        return 1
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*args.fourcc))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)
    cap.set(cv2.CAP_PROP_FPS, args.fps)

    ok, frame = cap.read()
    if not ok:
        print("webcam_publisher: couldn't read from camera %s" % args.device, file=sys.stderr)
        return 1
    height, width = frame.shape[:2]
    channels = 1 if frame.ndim == 2 else frame.shape[2]
    print("webcam_publisher: requested %dx%d@%g %s, camera delivers %dx%dx%d@%g" %
          (args.width, args.height, args.fps, args.fourcc, width, height, channels, cap.get(cv2.CAP_PROP_FPS)),
          file=sys.stderr)

    smm = SharedMemoryManager(os.path.abspath(args.library), descriptor=args.descriptor, frameName=args.stream,
                              width=width, height=height, channels=channels)

    published = dropped = 0
    capture_times, publish_times = [], []
    counter = 0
    start = time.perf_counter()
    last_capture = start
    while time.perf_counter() - start < args.duration:
        ok, frame = cap.read()
        now = time.perf_counter()
        if not ok:
            print("webcam_publisher: camera stopped delivering frames", file=sys.stderr)
            break
        capture_times.append(now - last_capture)
        last_capture = now
        timestamp = time.time_ns()

        counter += 1
        pixels = frame.reshape(-1)  # a view: cv2 frames are contiguous
        stamp = np.frombuffer(np.uint32(counter).tobytes(), dtype=np.uint8)
        pixels[:4] = stamp
        pixels[-4:] = stamp

        publish_start = time.perf_counter()
        if smm.copy_numpy_to_shared_memory(frame, unix_timestamp=timestamp):
            published += 1
        else:
            dropped += 1
        publish_times.append(time.perf_counter() - publish_start)

    elapsed = time.perf_counter() - start
    cap.release()
    print("RESULT publisher size=%dx%dx%d frames=%d published=%d dropped=%d fps=%.1f frame_interval_ms %s publish_ms %s" %
          (width, height, channels, counter, published, dropped, published / elapsed,
           percentiles_ms(capture_times[1:]), percentiles_ms(publish_times)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
