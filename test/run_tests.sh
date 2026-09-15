#!/usr/bin/env bash
# Regression tests for the multi-buffered reader/writer protocol in
# sharedMemoryVideoBuffers.c (see MAX_LOCAL_BUFFERS / SHMVB_BUFFER_COUNT).
#
# Two writer/reader pairs, each run twice:
#   torn_frame_{writer,reader}            - does a slow reader ever see a frame
#                                            that's part old data, part new
#                                            (the writer overwrote a slot mid-read)?
#   timestamp_consistency_{writer,reader} - does a frame's reported timestamp
#                                            ever belong to a different frame
#                                            than the pixel data it's paired with?
#
# Each pair is run once with the shipped default (4 slots per stream) - that run
# MUST show zero torn frames / zero mismatches, and failing it fails this
# script. It's also run once with SHMVB_BUFFER_COUNT=1 (multi-buffering
# disabled) purely to demonstrate the tests can actually detect the problem
# they claim to detect; torn frames THERE are expected and are reported as
# informational only, not a failure.
#
# framerate_benchmark then establishes the framerate a stream of a given image size
# reaches (one writer and one reader, both as fast as they can) - reported, not
# checked against a minimum, since it depends on the machine.
#
# Usage: test/run_tests.sh [duration_seconds_per_case]

set -u
cd "$(dirname "${BASH_SOURCE[0]}")"

DURATION="${1:-3}"
CC="${CC:-gcc}"
SRC_DIR="../src/c"
LIB_SRC="$SRC_DIR/sharedMemoryVideoBuffers.c"
BIN_DIR="bin"
LOG_DIR="$(mktemp -d)"

mkdir -p "$BIN_DIR"
OVERALL_STATUS=0

build()
{
    local name=$1
    if ! "$CC" -O2 -Wall -I"$SRC_DIR" "$name.c" "$LIB_SRC" -o "$BIN_DIR/$name" -pthread -lrt 2> "$LOG_DIR/build_$name.log"
    then
        echo "BUILD FAILED: $name"
        cat "$LOG_DIR/build_$name.log"
        exit 1
    fi
}

echo "Building test binaries..."
build torn_frame_writer
build torn_frame_reader
build timestamp_consistency_writer
build timestamp_consistency_reader
build protocol_edge_cases
build registry_edge_cases
build framerate_benchmark
if ! "$CC" -O2 -Wall -shared -fPIC -I"$SRC_DIR" "$LIB_SRC" -o "$BIN_DIR/libSharedMemoryVideoBuffers.so" -pthread -lrt 2> "$LOG_DIR/build_library.log"
then
    echo "BUILD FAILED: libSharedMemoryVideoBuffers.so"
    cat "$LOG_DIR/build_library.log"
    exit 1
fi

# run_case <label> <writer_bin> <reader_bin> <shm_name> <stream_name> <buffer_count|""> <must_pass yes|no>
run_case()
{
    local label=$1 writer=$2 reader=$3 shm=$4 stream=$5 buffer_count=$6 must_pass=$7

    rm -f "/dev/shm/$shm"* # the context and its streams' "<context>.<stream>.<generation>" objects

    echo ""
    echo "=== $label (SHMVB_BUFFER_COUNT=${buffer_count:-<default: 4>}) ==="

    if [ -n "$buffer_count" ]; then export SHMVB_BUFFER_COUNT="$buffer_count"; else unset SHMVB_BUFFER_COUNT; fi

    "$BIN_DIR/$writer" "$DURATION" "$shm" "$stream" > "$LOG_DIR/${label}_writer.log" 2>&1 &
    local wpid=$!
    sleep 0.2
    "$BIN_DIR/$reader" "$DURATION" "$shm" "$stream" > "$LOG_DIR/${label}_reader.log" 2>&1 &
    local rpid=$!

    wait "$rpid"; local rstatus=$?
    wait "$wpid"

    grep -E "frames written|reads," "$LOG_DIR/${label}_writer.log" "$LOG_DIR/${label}_reader.log" 2>/dev/null | sed 's/^/    /'
    sed -n 's/^/    /p' "$LOG_DIR/${label}_reader.log" | grep -E "MISMATCH|TORN" | head -5

    rm -f "/dev/shm/$shm"* # the context and its streams' "<context>.<stream>.<generation>" objects
    unset SHMVB_BUFFER_COUNT

    if [ "$rstatus" -eq 1 ]; then
        echo "  -> ERROR (harness/setup failure, see logs in $LOG_DIR)"
        OVERALL_STATUS=1
    elif [ "$must_pass" = "yes" ]; then
        if [ "$rstatus" -eq 0 ]; then
            echo "  -> PASS"
        else
            echo "  -> FAIL (expected zero, see logs in $LOG_DIR)"
            OVERALL_STATUS=1
        fi
    else
        if [ "$rstatus" -eq 0 ]; then
            echo "  -> informational: none detected"
        else
            echo "  -> informational: detected as expected (this mode has no tearing protection)"
        fi
    fi
}

run_case "torn_default"    torn_frame_writer              torn_frame_reader              shmvb_test_torn.shm torn ""  yes
run_case "torn_legacy"     torn_frame_writer              torn_frame_reader              shmvb_test_torn.shm torn "1" no
run_case "timestamp_default" timestamp_consistency_writer timestamp_consistency_reader   shmvb_test_ts.shm   ts   ""  yes
run_case "timestamp_legacy"  timestamp_consistency_writer timestamp_consistency_reader   shmvb_test_ts.shm   ts   "1" no

# run_single <label> <shm_prefix> <command...> - runs a self-checking test and
# reports its ok/FAIL lines; <shm_prefix> matches every /dev/shm object it creates
run_single()
{
    local label=$1 shm_prefix=$2
    shift 2

    echo ""
    echo "=== $label ==="
    rm -f "/dev/shm/$shm_prefix"*
    "$@" > "$LOG_DIR/$label.log" 2>&1
    local status=$?
    grep -E "^  (ok|FAIL)" "$LOG_DIR/$label.log"
    rm -f "/dev/shm/$shm_prefix"*
    if [ "$status" -eq 0 ]; then
        echo "  -> PASS"
    else
        echo "  -> FAIL (see logs in $LOG_DIR)"
        OVERALL_STATUS=1
    fi
}

# Single-binary edge cases: reader crash recovery, in-place writers, rejected
# copies, per-thread read tracking (protocol_edge_cases.c), and creating,
# joining, replacing and destroying streams across processes (registry_edge_cases.c)
run_single protocol_edge_cases shmvb_test_edge.shm "$BIN_DIR/protocol_edge_cases" shmvb_test_edge.shm edge
run_single registry_edge_cases shmvb_test_reg_     "$BIN_DIR/registry_edge_cases"

# Framerate for common image sizes: width height channels seconds (see framerate_benchmark.c
# to also require a minimum framerate)
run_single framerate_640x480x3   shmvb_test_fps "$BIN_DIR/framerate_benchmark" 640 480 3 "$DURATION"
run_single framerate_1920x1080x3 shmvb_test_fps "$BIN_DIR/framerate_benchmark" 1920 1080 3 "$DURATION"

# Python bindings (src/python/SharedMemoryManager.py) - see test_shared_memory_manager.py
PYTHON="${PYTHON:-python3}"
if ! "$PYTHON" -c "import numpy" 2> /dev/null; then
    echo ""
    echo "=== test_shared_memory_manager ==="
    echo "  -> SKIPPED ($PYTHON with numpy not available)"
else
    run_single test_shared_memory_manager shmvb_test_py.shm "$PYTHON" test_shared_memory_manager.py "$BIN_DIR/libSharedMemoryVideoBuffers.so" shmvb_test_py.shm py
fi

echo ""
if [ "$OVERALL_STATUS" -eq 0 ]; then
    echo "All mandatory checks passed."
    rm -rf "$LOG_DIR"
else
    echo "One or more mandatory checks FAILED. Logs kept in $LOG_DIR"
fi
exit "$OVERALL_STATUS"
