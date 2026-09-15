"""Watches a stream for a while and reports how often new frames are published to it.

Polls the latest frame's timestamp, so it measures what readers actually get: the publish
rate and the spacing between frames (jitter), without copying any pixels.

Usage: stream_stats.py <stream_name> <seconds> [descriptor]
"""
import os
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "src", "python"))
from SharedMemoryManager import SharedMemoryManager


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    stream = sys.argv[1]
    seconds = float(sys.argv[2])
    descriptor = sys.argv[3] if len(sys.argv) > 3 else "video_frames.shm"

    smm = SharedMemoryManager(os.path.join(REPO, "libSharedMemoryVideoBuffers.so"),
                              descriptor=descriptor, frameName=stream, connect=True)
    last = None
    intervals_ms = []
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        timestamp = smm.get_timestamp()
        if timestamp and timestamp != last:
            if last is not None:
                intervals_ms.append((timestamp - last) / 1e6)
            last = timestamp
        time.sleep(0.0005)

    print(f"stream {stream}: {len(intervals_ms) + (last is not None)} frames seen in {seconds:.0f} s")
    if len(intervals_ms) < 2:
        print("  not enough frames to measure")
        return
    mean = sum(intervals_ms) / len(intervals_ms)
    print(f"  publish rate  : {1000.0 / mean:.1f} fps")
    print(f"  frame interval: mean {mean:.1f} ms, median {percentile(intervals_ms, 0.5):.1f} ms, "
          f"95% {percentile(intervals_ms, 0.95):.1f} ms, max {max(intervals_ms):.1f} ms")


if __name__ == "__main__":
    main()
