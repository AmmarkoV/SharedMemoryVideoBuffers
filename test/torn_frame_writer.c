/** @file torn_frame_writer.c
 *  @brief Writer half of the torn-frame regression test. Paired with
 *  torn_frame_reader.c, this validates that startWritingToVideoBufferPointer()/
 *  stopWritingToVideoBufferPointer() never let a writer overwrite a slot a
 *  reader is currently draining (see MAX_LOCAL_BUFFERS in sharedMemoryVideoBuffers.h).
 *
 *  Writes frames as fast as possible (no sleep) where every byte of the frame
 *  equals a rolling generation counter. A torn/mixed frame (part old
 *  generation, part new) is then trivial for the reader to detect: it just
 *  has to check the bytes it read aren't all equal.
 *
 *  Usage: torn_frame_writer [duration_seconds] [shm_name] [stream_name]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "sharedMemoryVideoBuffers.h"

int main(int argc, char *argv[])
{
    int duration_seconds       = (argc > 1) ? atoi(argv[1]) : 3;
    const char *shm_name       = (argc > 2) ? argv[2] : "shmvb_test_torn.shm";
    const char *stream_name    = (argc > 3) ? argv[3] : "torn";

    if (createSharedMemoryContextDescriptor(shm_name) == -1) { return 1; }
    struct SharedMemoryContext *ctx = connectToSharedMemoryContextDescriptor(shm_name);
    if (!ctx) { return 1; }

    // 800x600x3 = 1,440,000 bytes/frame - large enough that a torn write is
    // easy to land inside, small enough that this test runs in a couple of
    // seconds.
    if (createVideoFrameMetaData(ctx, stream_name, 800, 600, 3) != 0) { return 1; }
    struct VideoFrame *frame = getVideoBufferPointer(ctx, stream_name);
    if (!frame) { return 1; }
    if (map_frame_shared_memory(frame,1) == NULL) { return 1; }

    unsigned char *buf = malloc(frame->frame_size);
    if (!buf) { return 1; }

    unsigned char generation = 0;
    long written = 0;
    time_t start = time(NULL);
    while (time(NULL) - start < duration_seconds)
    {
        memset(buf, generation, frame->frame_size);
        if (startWritingToVideoBufferPointer(frame))
        {
            copy_to_shared_memory(frame, buf, frame->frame_size, 0);
            stopWritingToVideoBufferPointer(frame);
            written++;
        }
        generation++;
        // Deliberately no sleep: maximize writer/reader contention.
    }

    fprintf(stderr, "torn_frame_writer: %ld frames written\n", written);
    free(buf);
    destroyVideoFrame(ctx, stream_name);
    return 0;
}
