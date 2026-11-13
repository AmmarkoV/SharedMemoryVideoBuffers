#!/usr/bin/env bash
# ffmpeg -> shared memory -> v4l2loopback: serves a video file / URL / device as a webcam.
#
# Usage: scripts/ffmpeg_to_webcam.sh INPUT [video_nr (default 44)] [extra ffmpegsource options...]
#   e.g. scripts/ffmpeg_to_webcam.sh video.mp4 44 -L -W 1280
#        scripts/ffmpeg_to_webcam.sh rtsp://camera/stream 44
# Then: ffplay /dev/video44, or pick "SHMVB Video" in a browser / OBS / Zoom. Ctrl+C stops both.
set -eu
cd "$(dirname "${BASH_SOURCE[0]}")/.."
[ $# -ge 1 ] || { sed -n '4,6p' "$0"; exit 1; }
INPUT="$1"; shift
NR="${1:-44}"; [ $# -ge 1 ] && shift
STREAM="ffmpeg"

command -v ffmpeg >/dev/null || { echo "ffmpeg is not installed (apt install ffmpeg)"; exit 1; }
make ffmpegsource v4l2sink >/dev/null

./ffmpegsource -i "$INPUT" -s "$STREAM" "$@" &
SOURCE_PID=$!
trap 'kill $SOURCE_PID 2>/dev/null; wait $SOURCE_PID 2>/dev/null' EXIT INT TERM

# v4l2sink waits for the stream, and creates the device (via sudo) if it is missing
./v4l2sink -s "$STREAM" -d "/dev/video${NR}" -l "SHMVB Video"
