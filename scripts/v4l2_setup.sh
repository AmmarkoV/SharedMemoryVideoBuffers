#!/usr/bin/env bash
# Creates a v4l2loopback camera node for v4l2sink.
# Usage: scripts/v4l2_setup.sh [video_nr (default 43)] [label (default "SHMVB Camera")]
set -eu
NR="${1:-43}"
LABEL="${2:-SHMVB Camera}"
DEV="/dev/video${NR}"

if [ -e "$DEV" ]; then
    echo "$DEV already exists: $(cat /sys/class/video4linux/video${NR}/name 2>/dev/null)"
    exit 0
fi

if lsmod | grep -q '^v4l2loopback'; then
    # Module already loaded (maybe for another device): add a node at runtime
    if command -v v4l2loopback-ctl >/dev/null; then
        sudo v4l2loopback-ctl add -n "$LABEL" -x 1 "$DEV"
    else
        echo "v4l2loopback is loaded but v4l2loopback-ctl is missing (apt install v4l2loopback-utils)."
        echo "Either install it, or reload the module with all the devices you need, e.g.:"
        echo "  sudo modprobe -r v4l2loopback && sudo modprobe v4l2loopback devices=2 video_nr=42,${NR} card_label=\"GoPro\",\"${LABEL}\" exclusive_caps=1,1"
        exit 1
    fi
else
    sudo modprobe v4l2loopback devices=1 video_nr="$NR" card_label="$LABEL" exclusive_caps=1
fi

[ -e "$DEV" ] && echo "Created $DEV ($LABEL). Run: ./v4l2sink -d $DEV -s stream1"
