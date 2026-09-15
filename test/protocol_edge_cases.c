/** @file protocol_edge_cases.c
 *  @brief Single-binary regression tests for edge cases of the multi-buffered
 *  reader/writer protocol (see MAX_LOCAL_BUFFERS in sharedMemoryVideoBuffers.h):
 *
 *   1. reader_crash     - a reader process that dies between start and stop
 *                         reading must not pin its slot forever (the writer
 *                         must reclaim it and keep publishing).
 *   2. inplace_writer   - getVideoFrameDataPointer() called between start and
 *                         stop writing must return the slot claimed for
 *                         writing, not the slot readers are currently using.
 *   3. rejected_copy    - a copy_to_shared_memory() that refuses the data must
 *                         not cause stopWritingToVideoBufferPointer() to publish
 *                         a stale slot as the latest frame.
 *   4. nested_read      - two starts on the same frame from one thread followed
 *                         by one stop must not leave a reader registered.
 *   5. read_table_full  - a start that can't be tracked on this thread must not
 *                         leave a reader registered.
 *
 *  Exit code: 0 = all checks passed, 2 = a check failed, 1 = setup error.
 *
 *  Usage: protocol_edge_cases [shm_name] [stream_name]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include "sharedMemoryVideoBuffers.h"

static int failures = 0;

static void check(int passed, const char *name)
{
    fprintf(stderr, "  %s : %s\n", passed ? "ok  " : "FAIL", name);
    if (!passed) { failures++; }
}

static int writeGeneration(struct VideoFrame *frame, unsigned char generation)
{
    unsigned char buf[64*48*3];
    memset(buf, generation, sizeof(buf));
    if (!startWritingToVideoBufferPointer(frame)) { return 0; }
    copy_to_shared_memory(frame, buf, frame->frame_size, generation);
    stopWritingToVideoBufferPointer(frame);
    return 1;
}

static int countSuccessfulWrites(struct VideoFrame *frame, int attempts)
{
    int ok = 0;
    for (int i=0; i<attempts; i++) { ok += writeGeneration(frame, (unsigned char) (100+i)); }
    return ok;
}

// Reads the latest frame's first pixel byte and timestamp, as seen by a reader.
static int readLatest(struct VideoFrame *frame, unsigned char *pixel, unsigned long *timestamp)
{
    if (!startReadingFromVideoBufferPointer(frame)) { return 0; }
    *pixel     = getVideoFrameDataPointer(frame)[0];
    *timestamp = getVideoFrameTimestamp(frame);
    stopReadingFromVideoBufferPointer(frame);
    return 1;
}

// (Re)creates the stream so every case starts from a clean, multi-buffered
// state with one frame already published, and a failure in one case can't
// cascade into the next.
static struct VideoFrame * freshStream(struct SharedMemoryContext *ctx, const char *stream_name)
{
    if (getVideoBufferPointer(ctx, stream_name)) { destroyVideoFrame(ctx, stream_name); }
    if (createVideoFrameMetaData(ctx, stream_name, 64, 48, 3) != 0) { return NULL; }
    struct VideoFrame *frame = getVideoBufferPointer(ctx, stream_name);
    if (!frame || frame->bufferCount < 2) { fprintf(stderr, "protocol_edge_cases: expected a multi-buffered stream\n"); return NULL; }
    if (map_frame_shared_memory(frame,1) == NULL) { return NULL; }
    if (!writeGeneration(frame, 1)) { return NULL; }
    return frame;
}

int main(int argc, char *argv[])
{
    const char *shm_name    = (argc > 1) ? argv[1] : "shmvb_test_edge.shm";
    const char *stream_name = (argc > 2) ? argv[2] : "edge";

    unsetenv("SHMVB_BUFFER_COUNT"); // these cases are about the default (multi-buffered) mode
    shm_unlink(stream_name);
    if (createSharedMemoryContextDescriptor(shm_name) == -1) { return 1; }
    struct SharedMemoryContext *ctx = connectToSharedMemoryContextDescriptor(shm_name);
    if (!ctx) { return 1; }
    struct VideoFrame *frame;

    // 1. reader_crash
    if (!(frame = freshStream(ctx, stream_name))) { return 1; }

    pid_t child = fork();
    if (child == 0)
    {
        struct SharedMemoryContext *childCtx = connectToSharedMemoryContextDescriptor(shm_name);
        struct VideoFrame *childFrame = getVideoBufferPointer(childCtx, stream_name);
        startReadingFromVideoBufferPointer(childFrame);
        _exit(0); // dies without stopReadingFromVideoBufferPointer()
    }
    waitpid(child, NULL, 0);
    check(countSuccessfulWrites(frame, 5) == 5, "reader_crash: writer keeps publishing after a reader died mid-read");

    // 2. inplace_writer
    if (!(frame = freshStream(ctx, stream_name))) { return 1; }
    if (!startWritingToVideoBufferPointer(frame)) { return 1; }
    unsigned char *writePtr = getVideoFrameDataPointer(frame);
    check(writePtr == map_frame_shared_memory(frame,1) + ((size_t) frame->writeIndex * frame->frame_size),
          "inplace_writer: getVideoFrameDataPointer() returns the claimed write slot");
    check(frame->writeIndex != frame->latestIndex, "inplace_writer: claimed write slot is not the published slot");
    memset(writePtr, 42, frame->frame_size);
    setVideoFrameTimestamp(frame, 42);
    stopWritingToVideoBufferPointer(frame);
    unsigned char pixel = 0; unsigned long timestamp = 0;
    check(readLatest(frame, &pixel, &timestamp) && pixel == 42 && timestamp == 42,
          "inplace_writer: data written in place is what readers see after publishing");

    // 3. rejected_copy
    if (!(frame = freshStream(ctx, stream_name))) { return 1; }
    if (!writeGeneration(frame, 7)) { return 1; }
    unsigned char oversized[64*48*3 + 1];
    memset(oversized, 99, sizeof(oversized));
    if (!startWritingToVideoBufferPointer(frame)) { return 1; }
    check(copy_to_shared_memory(frame, oversized, sizeof(oversized), 99) == 0, "rejected_copy: oversized copy reports failure");
    stopWritingToVideoBufferPointer(frame);
    check(readLatest(frame, &pixel, &timestamp) && pixel == 7 && timestamp == 7,
          "rejected_copy: latest frame is still the last successful write");

    // 4. nested_read
    if (!(frame = freshStream(ctx, stream_name))) { return 1; }
    startReadingFromVideoBufferPointer(frame);
    startReadingFromVideoBufferPointer(frame);
    stopReadingFromVideoBufferPointer(frame);
    check(countSuccessfulWrites(frame, 5) == 5, "nested_read: no reader left registered");

    // 5. read_table_full - more streams than one thread can track reads of at
    // once (two contexts' worth, since one context holds MAX_NUMBER_OF_BUFFERS)
    enum { TABLE_CONTEXTS = 2 };
    struct SharedMemoryContext *tableCtx[TABLE_CONTEXTS];
    char tableShm[TABLE_CONTEXTS][64];
    struct VideoFrame *tableFrames[TABLE_CONTEXTS * MAX_NUMBER_OF_BUFFERS];
    int tableFrameCount = 0;
    for (int c=0; c<TABLE_CONTEXTS; c++)
    {
        snprintf(tableShm[c], sizeof(tableShm[c]), "%s.table%d", shm_name, c);
        shm_unlink(tableShm[c]);
        if (createSharedMemoryContextDescriptor(tableShm[c]) == -1) { return 1; }
        if (!(tableCtx[c] = connectToSharedMemoryContextDescriptor(tableShm[c]))) { return 1; }
        for (int s=0; s<MAX_NUMBER_OF_BUFFERS; s++)
        {
            char name[32];
            snprintf(name, sizeof(name), "table%d", s);
            if (createVideoFrameMetaData(tableCtx[c], name, 4, 4, 1) != 0) { return 1; }
            tableFrames[tableFrameCount++] = getVideoBufferPointer(tableCtx[c], name);
        }
    }
    for (int i=0; i<tableFrameCount; i++) { startReadingFromVideoBufferPointer(tableFrames[i]); }
    for (int i=0; i<tableFrameCount; i++) { stopReadingFromVideoBufferPointer(tableFrames[i]); }
    int allWritable = 1;
    for (int i=0; i<tableFrameCount; i++)
    {
        for (int w=0; w<2; w++)
        {
            if (!startWritingToVideoBufferPointer(tableFrames[i])) { allWritable = 0; break; }
            stopWritingToVideoBufferPointer(tableFrames[i]);
        }
    }
    check(allWritable, "read_table_full: no reader left registered on any frame");
    for (int c=0; c<TABLE_CONTEXTS; c++)
    {
        for (int s=0; s<MAX_NUMBER_OF_BUFFERS; s++) { char name[32]; snprintf(name, sizeof(name), "table%d", s); destroyVideoFrame(tableCtx[c], name); }
        shm_unlink(tableShm[c]);
    }

    destroyVideoFrame(ctx, stream_name);
    shm_unlink(shm_name);
    fprintf(stderr, "protocol_edge_cases: %d failure(s)\n", failures);
    return failures > 0 ? 2 : 0;
}
