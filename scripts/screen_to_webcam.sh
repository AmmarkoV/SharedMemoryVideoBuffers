#!/usr/bin/env bash
# Screen capture -> shared memory -> v4l2loopback: serves the desktop as a webcam.
#
# Usage: scripts/screen_to_webcam.sh [video_nr (default 43)] [fps (default 30)] [geometry WxH+X+Y]
#   e.g. scripts/screen_to_webcam.sh 43 30 1280x720+0+0
# Then: ffplay /dev/video43, or pick "SHMVB Screen" in a browser / OBS / Zoom. Ctrl+C stops both.
set -eu
cd "$(dirname "${BASH_SOURCE[0]}")/.."
NR="${1:-43}"
FPS="${2:-30}"
GEOMETRY="${3:-}"
STREAM="screen"

make screencapture v4l2sink >/dev/null

ARGS=(-s "$STREAM" -r "$FPS")
[ -n "$GEOMETRY" ] && ARGS+=(-g "$GEOMETRY")
./screencapture "${ARGS[@]}" &
CAPTURE_PID=$!
trap 'kill $CAPTURE_PID 2>/dev/null; wait $CAPTURE_PID 2>/dev/null' EXIT INT TERM

# v4l2sink waits for the stream, and creates the device (via sudo) if it is missing
./v4l2sink -s "$STREAM" -d "/dev/video${NR}" -r "$FPS" -l "SHMVB Screen"
