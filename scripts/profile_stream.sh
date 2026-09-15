#!/bin/bash
# Profiles a video pipeline over shared memory: src/python/openCVStream.py publishes a video
# file and a client shows it - the C viewer, or src/python/client_downstream.py.
#
#  1. native run   : CPU use of the publisher and the client, and the publish rate readers
#                    see (scripts/stream_stats.py), without any profiler slowing them down
#  2. profiled run : the publisher under cProfile, the client under callgrind (viewer) or
#                    cProfile (python)
#
# Both runs open the publisher's and the client's windows. Results go to scripts/profile_results/.
# With a display, kcachegrind opens the viewer's profile at the end (NO_GUI=1 skips it).
# PYTHON picks the interpreter (default: src/python/venv if present, it needs OpenCV and numpy).
#
# Usage: scripts/profile_stream.sh [video=test.mp4] [seconds_per_run=15] [viewer|python]

THISDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$THISDIR"

# Job control: otherwise background jobs of a script start with SIGINT ignored, and the
# Python scripts (and cProfile) could never be stopped cleanly
set -m

cd ..
make

VIDEO="${1:-test.mp4}"
RUN_SECONDS="${2:-15}"
CLIENT="${3:-viewer}"
STREAM="profile_stream"
CONTEXT="video_frames.shm"
OUT="$THISDIR/profile_results"

if [ -z "$PYTHON" ]; then
    if [ -x src/python/venv/bin/python3 ]; then PYTHON=src/python/venv/bin/python3; else PYTHON=python3; fi
fi
if [ ! -e "$VIDEO" ]; then echo "No video file $VIDEO"; exit 1; fi
if ! "$PYTHON" -c "import cv2, numpy" 2> /dev/null; then echo "$PYTHON can't import cv2 and numpy (set PYTHON=...)"; exit 1; fi
case "$CLIENT" in
    viewer) command -v valgrind > /dev/null || { echo "valgrind is needed to profile the viewer"; exit 1; } ;;
    python) ;;
    *) echo "Unknown client $CLIENT (viewer or python)"; exit 1 ;;
esac

rm -rf "$OUT"
mkdir -p "$OUT"

CLK_TCK=$(getconf CLK_TCK)
# CPU time (user + system, every thread) a process has used so far, in clock ticks
cpu_ticks() { awk '{print $14+$15}' "/proc/$1/stat" 2> /dev/null || echo 0; }

# start_publisher <run> [python options...]
start_publisher()
{
    local run=$1
    shift
    "$PYTHON" "$@" src/python/openCVStream.py "$VIDEO" "$STREAM" > "$OUT/${run}_publisher.log" 2>&1 &
    PUBLISHER=$!
    # Wait for the stream's backing object, "/<context>.<stream>.<generation>"
    for attempt in $(seq 100); do
        if ls /dev/shm/"$CONTEXT.$STREAM".* > /dev/null 2>&1; then sleep 1; return; fi
        kill -0 "$PUBLISHER" 2> /dev/null || break
        sleep 0.1
    done
    echo "The publisher didn't create stream $STREAM, see $OUT/${run}_publisher.log"
    kill -INT "$PUBLISHER" 2> /dev/null
    exit 1
}

# start_client <run> <native|profiled>
start_client()
{
    local run=$1 mode=$2
    if [ "$CLIENT" = viewer ]; then
        if [ "$mode" = profiled ]; then
            valgrind --tool=callgrind --callgrind-out-file="$OUT/callgrind.out.viewer" ./viewer "$STREAM" > "$OUT/${run}_client.log" 2>&1 &
        else
            ./viewer "$STREAM" > "$OUT/${run}_client.log" 2>&1 &
        fi
    else
        if [ "$mode" = profiled ]; then
            "$PYTHON" -m cProfile -o "$OUT/client.prof" src/python/client_downstream.py "$STREAM" > "$OUT/${run}_client.log" 2>&1 &
        else
            "$PYTHON" src/python/client_downstream.py "$STREAM" > "$OUT/${run}_client.log" 2>&1 &
        fi
    fi
    CLIENT_PID=$!
}

stop_all()
{
    # SIGINT lets the Python scripts (and cProfile) finish cleanly; the viewer only knows SIGTERM
    if [ "$CLIENT" = viewer ]; then kill -TERM "$CLIENT_PID" 2> /dev/null; else kill -INT "$CLIENT_PID" 2> /dev/null; fi
    wait "$CLIENT_PID" 2> /dev/null
    kill -INT "$PUBLISHER" 2> /dev/null
    wait "$PUBLISHER" 2> /dev/null
}

# print_cprofile <profile> <title>: the functions with the most time spent in themselves
print_cprofile()
{
    "$PYTHON" -c "import pstats, sys; pstats.Stats(sys.argv[1], stream=sys.stdout).sort_stats('tottime').print_stats(15)" "$1" \
        | grep -v '^$' | sed -n '/ncalls/,$p' > "$OUT/$2.txt"
    echo "--- $2 (seconds spent in each function itself)"
    cat "$OUT/$2.txt"
}

echo ""
echo "=== 1/2 native run: $VIDEO -> $STREAM -> $CLIENT, ${RUN_SECONDS}s ==="
start_publisher native
start_client native native
sleep 2 # client startup (imports, window)
publisherStart=$(cpu_ticks "$PUBLISHER"); clientStart=$(cpu_ticks "$CLIENT_PID"); timeStart=$(date +%s%N)
"$PYTHON" scripts/stream_stats.py "$STREAM" "$RUN_SECONDS" "$CONTEXT" > "$OUT/native_stream_stats.txt" 2> "$OUT/native_stream_stats.log"
publisherEnd=$(cpu_ticks "$PUBLISHER"); clientEnd=$(cpu_ticks "$CLIENT_PID"); timeEnd=$(date +%s%N)
stop_all
awk -v p=$((publisherEnd-publisherStart)) -v c=$((clientEnd-clientStart)) -v t=$((timeEnd-timeStart)) -v hz="$CLK_TCK" -v client="$CLIENT" \
    'BEGIN { s = t / 1e9; printf "CPU use (100%% = one core): publisher %.0f%%, %s %.0f%%\n", p*100/hz/s, client, c*100/hz/s }' \
    | tee "$OUT/native_cpu.txt"
cat "$OUT/native_stream_stats.txt"

echo ""
echo "=== 2/2 profiled run, ${RUN_SECONDS}s ==="
start_publisher profiled -m cProfile -o "$OUT/publisher.prof"
start_client profiled profiled
sleep "$RUN_SECONDS"
stop_all

print_cprofile "$OUT/publisher.prof" publisher_cprofile
if [ "$CLIENT" = viewer ]; then
    callgrind_annotate --auto=no "$OUT/callgrind.out.viewer" 2> /dev/null | sed -n '/file:function/,$p' | head -25 > "$OUT/viewer_callgrind.txt"
    echo "--- viewer_callgrind (instructions executed in each function itself)"
    cat "$OUT/viewer_callgrind.txt"
else
    print_cprofile "$OUT/client.prof" client_cprofile
fi

echo ""
echo "Results and logs are in $OUT"
if [ "$CLIENT" = viewer ] && [ -n "$DISPLAY" ] && [ "${NO_GUI:-0}" != 1 ] && command -v kcachegrind > /dev/null; then
    kcachegrind "$OUT/callgrind.out.viewer"
fi

exit 0
