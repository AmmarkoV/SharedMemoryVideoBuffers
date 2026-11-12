#!/usr/bin/env bash
# Creates a v4l2loopback camera node ahead of time. Optional: v4l2sink creates a missing device
# by itself. Existing loopback devices (and programs using them) are never touched.
#
# Usage: scripts/v4l2_setup.sh [video_nr (default 43)] [label (default "SHMVB Camera")]
set -eu
cd "$(dirname "${BASH_SOURCE[0]}")/.."
NR="${1:-43}"
LABEL="${2:-SHMVB Camera}"

[ -x ./v4l2sink ] || make v4l2sink
./v4l2sink --create-device -d "/dev/video${NR}" -l "$LABEL"
[ -e "/dev/video${NR}" ] && echo "/dev/video${NR}: $(cat "/sys/class/video4linux/video${NR}/name")"
