#!/usr/bin/env bash
# Webcam benchmark for the Python bindings: one OpenCV webcam publisher
# (webcam_publisher.py) and several clients (webcam_client.py) that join one
# after another and then all read the same stream at once. Each client reports
# the frames it got, frames it skipped, torn frames, capture->read latency and
# read time; the publisher reports frames published and dropped. Fails if any
# frame was torn or a process failed.
#
# Usage: test/pyWebcamTest.sh [options]
#   --device DEV      camera index or device path     (default: 0)
#   --width N         requested capture width         (default: 1280)
#   --height N        requested capture height        (default: 720)
#   --fps N           requested capture framerate     (default: 30)
#   --fourcc CODE     capture format                  (default: MJPG)
#   --clients N       number of clients               (default: 10)
#   --stagger S       seconds between client starts   (default: 0.3)
#   --duration S      seconds each client reads       (default: 10)
#   --mode copy|view  read_from_shared_memory() copies, or zero-copy read_frame() views (default: copy)
#   --work-ms N       simulated processing per frame  (default: 0)
#   --python PATH     interpreter with numpy and cv2  (default: $PYTHON or python3)
#
# The camera may not support the requested size/framerate; the publisher
# prints what it actually delivers.

set -u
cd "$(dirname "${BASH_SOURCE[0]}")"

DEVICE=0
WIDTH=1280
HEIGHT=720
FPS=30
FOURCC=MJPG
CLIENTS=10
STAGGER=0.3
DURATION=10
MODE=copy
WORK_MS=0
PYTHON="${PYTHON:-python3}"

while [ $# -gt 0 ]; do
    case "$1" in
        --device)   DEVICE=$2;   shift 2 ;;
        --width)    WIDTH=$2;    shift 2 ;;
        --height)   HEIGHT=$2;   shift 2 ;;
        --fps)      FPS=$2;      shift 2 ;;
        --fourcc)   FOURCC=$2;   shift 2 ;;
        --clients)  CLIENTS=$2;  shift 2 ;;
        --stagger)  STAGGER=$2;  shift 2 ;;
        --duration) DURATION=$2; shift 2 ;;
        --mode)     MODE=$2;     shift 2 ;;
        --work-ms)  WORK_MS=$2;  shift 2 ;;
        --python)   PYTHON=$2;   shift 2 ;;
        -h|--help)  sed -n '2,24p' "$(basename "${BASH_SOURCE[0]}")" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option $1 (see --help)"; exit 1 ;;
    esac
done

if ! "$PYTHON" -c "import numpy, cv2" 2> /dev/null; then
    echo "$PYTHON can't import numpy and cv2; pass --python with an interpreter that can"
    exit 1
fi

LIBRARY="$(pwd)/../libSharedMemoryVideoBuffers.so"
if ! make -C .. libSharedMemoryVideoBuffers.so > /dev/null; then
    echo "BUILD FAILED: libSharedMemoryVideoBuffers.so"
    exit 1
fi

SHM=shmvb_webcam_bench.shm
STREAM=webcam
LOG_DIR="$(mktemp -d)"
PIDS=()

cleanup()
{
    for pid in "${PIDS[@]}"; do kill "$pid" 2> /dev/null; done
    wait 2> /dev/null
    rm -f "/dev/shm/$SHM"* # the context and its stream's "<context>.<stream>.<generation>" objects
}
trap cleanup EXIT
trap 'exit 130' INT TERM

rm -f "/dev/shm/$SHM"*

# The publisher outlasts the last client: clients start $STAGGER apart and read
# for $DURATION each, plus time to open the camera and connect
PUBLISH_SECONDS=$(awk -v c="$CLIENTS" -v s="$STAGGER" -v d="$DURATION" 'BEGIN { print c*s + d + 5 }')

echo "Publishing $DEVICE at ${WIDTH}x${HEIGHT}@${FPS} ($FOURCC) for ${PUBLISH_SECONDS}s, $CLIENTS $MODE client(s) (work ${WORK_MS} ms), SHMVB_BUFFER_COUNT=${SHMVB_BUFFER_COUNT:-<default: 4>}"
"$PYTHON" -u webcam_publisher.py --library "$LIBRARY" --descriptor "$SHM" --stream "$STREAM" \
    --device "$DEVICE" --width "$WIDTH" --height "$HEIGHT" --fps "$FPS" --fourcc "$FOURCC" \
    --duration "$PUBLISH_SECONDS" > "$LOG_DIR/publisher.log" 2>&1 &
PUBLISHER_PID=$!
PIDS+=("$PUBLISHER_PID")

CLIENT_PIDS=()
for i in $(seq 1 "$CLIENTS"); do
    "$PYTHON" -u webcam_client.py --library "$LIBRARY" --descriptor "$SHM" --stream "$STREAM" \
        --id "$i" --mode "$MODE" --work-ms "$WORK_MS" --duration "$DURATION" > "$LOG_DIR/client_$i.log" 2>&1 &
    CLIENT_PIDS+=($!)
    PIDS+=($!)
    sleep "$STAGGER"
done

STATUS=0
for pid in "${CLIENT_PIDS[@]}"; do
    wait "$pid" || STATUS=1
done
wait "$PUBLISHER_PID" || STATUS=1

echo ""
grep -h "webcam_publisher:" "$LOG_DIR/publisher.log"
grep -h "^RESULT" "$LOG_DIR/publisher.log" || { echo "publisher failed:"; cat "$LOG_DIR/publisher.log"; STATUS=1; }
for i in $(seq 1 "$CLIENTS"); do
    grep -h "^RESULT" "$LOG_DIR/client_$i.log" || { echo "client $i failed:"; tail -5 "$LOG_DIR/client_$i.log"; STATUS=1; }
done

if grep -h "^RESULT client" "$LOG_DIR"/client_*.log | grep -qv " torn=0 "; then
    echo ""
    echo "TORN FRAMES DETECTED"
    STATUS=1
fi

echo ""
if [ "$STATUS" -eq 0 ]; then
    echo "Webcam benchmark finished without errors."
    rm -rf "$LOG_DIR"
else
    echo "Webcam benchmark FAILED. Logs kept in $LOG_DIR"
fi
exit "$STATUS"
