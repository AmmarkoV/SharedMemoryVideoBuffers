/** @file timestamp_consistency_writer.c
 *  @brief Writer half of the timestamp/pixel consistency regression test.
 *  Paired with timestamp_consistency_reader.c.
 *
 *  Validates that a frame's timestamp (per-slot `timestamps[]` in VideoFrame)
 *  is always published atomically together with that same frame's pixel data,
 *  so a reader can never see a timestamp that belongs to a different
 *  generation than the pixel data it's paired with.
 *
 *  Every frame is filled with a single repeated byte equal to a rolling
 *  generation counter, and that same counter is passed as the frame's
 *  timestamp (copy_to_shared_memory's timestamp parameter accepts any
 *  non-zero unsigned long, it doesn't have to be a real wall-clock value).
 *  The reader then checks getVideoFrameTimestamp() always matches the pixel
 *  generation it read.
 *
 *  Usage: timestamp_consistency_writer [duration_seconds] [shm_name] [stream_name]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "sharedMemoryVideoBuffers.h"

int main(int argc, char *argv[])
{
    int duration_seconds    = (argc > 1) ? atoi(argv[1]) : 3;
    const char *shm_name    = (argc > 2) ? argv[2] : "shmvb_test_ts.shm";
    const char *stream_name = (argc > 3) ? argv[3] : "ts";

    if (createSharedMemoryContextDescriptor(shm_name) == -1) { return 1; }
    struct SharedMemoryContext *ctx = connectToSharedMemoryContextDescriptor(shm_name);
    if (!ctx) { return 1; }

    if (createVideoFrameMetaData(ctx, stream_name, 800, 600, 3) != 0) { return 1; }
    struct VideoFrame *frame = getVideoBufferPointer(ctx, stream_name);
    if (!frame) { return 1; }
    if (map_frame_shared_memory(frame,1) == NULL) { return 1; }

    unsigned char *buf = malloc(frame->frame_size);
    if (!buf) { return 1; }

    unsigned long generation = 1; // start at 1: copy_to_shared_memory treats a
                                  // timestamp of 0 as "use the wall clock instead"
    long written = 0;
    time_t start = time(NULL);
    while (time(NULL) - start < duration_seconds)
    {
        memset(buf, (unsigned char)(generation % 256), frame->frame_size);
        if (startWritingToVideoBufferPointer(frame))
        {
            copy_to_shared_memory(frame, buf, frame->frame_size, generation);
            stopWritingToVideoBufferPointer(frame);
            written++;
        }
        generation++;
        // Deliberately no sleep: maximize writer/reader contention.
    }

    fprintf(stderr, "timestamp_consistency_writer: %ld frames written\n", written);
    free(buf);
    destroyVideoFrame(ctx, stream_name);
    return 0;
}
