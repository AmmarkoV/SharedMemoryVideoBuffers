/** @file torn_frame_reader.c
 *  @brief Reader half of the torn-frame regression test - see torn_frame_writer.c.
 *
 *  Repeatedly reads the frame and checks it is internally consistent (every
 *  sampled byte equal to the first byte). Since the writer fills each frame
 *  with a single repeated generation byte, any inconsistency proves the
 *  writer clobbered the slot while this reader was still copying out of it.
 *
 *  Runs for a shorter duration than the writer and exits before the writer's
 *  final destroyVideoFrame() teardown, so this test isn't testing (and isn't
 *  tripped up by) the separate, already-known issue that destroying a stream
 *  isn't safe against a concurrent reader.
 *
 *  Exit code: 0 = no torn frames observed, 2 = torn frame(s) detected,
 *  1 = harness/setup error (e.g. the stream never appeared).
 *
 *  Usage: torn_frame_reader [duration_seconds] [shm_name] [stream_name]
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "sharedMemoryVideoBuffers.h"

int main(int argc, char *argv[])
{
    int duration_seconds    = (argc > 1) ? atoi(argv[1]) : 3;
    const char *shm_name    = (argc > 2) ? argv[2] : "shmvb_test_torn.shm";
    const char *stream_name = (argc > 3) ? argv[3] : "torn";

    // Reads for less wall-clock time than the writer runs, so it always
    // finishes and exits before the writer's end-of-run destroyVideoFrame().
    int read_duration_seconds = duration_seconds > 1 ? duration_seconds - 1 : duration_seconds;

    struct SharedMemoryContext *ctx = connectToSharedMemoryContextDescriptor(shm_name);
    if (!ctx) { fprintf(stderr, "torn_frame_reader: could not connect to %s\n", shm_name); return 1; }

    struct VideoFrame *frame = NULL;
    for (int i=0; i<2000; i++)
    {
        frame = getVideoBufferPointer(ctx, stream_name);
        if (frame) { break; }
        usleep(1000);
    }
    if (!frame) { fprintf(stderr, "torn_frame_reader: stream '%s' never appeared\n", stream_name); return 1; }

    int item = resolveFeedNameToID(ctx, stream_name);
    struct VideoFrameLocalMapping *lm = allocateLocalMapping();
    if (!lm || !mapRemoteToLocal(ctx, lm, item)) { fprintf(stderr, "torn_frame_reader: failed to map stream\n"); return 1; }

    long reads = 0, torn = 0;
    time_t start = time(NULL);
    while (time(NULL) - start < read_duration_seconds)
    {
        if (!startReadingFromVideoBufferPointer(frame)) { continue; }

        unsigned char *data = getLocalMappingPointer(lm, item);
        if (!data) { stopReadingFromVideoBufferPointer(frame); continue; }

        unsigned char first = data[0];
        size_t size = getVideoFrameDataSize(frame);
        int consistent = 1;
        // Sample rather than check every byte - keeps this test fast while
        // still reliably catching a torn write across a 1.44MB frame.
        for (size_t i=0; i<size; i+=997)
        {
            if (data[i] != first) { consistent = 0; break; }
        }
        stopReadingFromVideoBufferPointer(frame);

        reads++;
        if (!consistent)
        {
            torn++;
            fprintf(stderr, "TORN FRAME at read #%ld\n", reads);
        }
    }

    fprintf(stderr, "torn_frame_reader: %ld reads, %ld torn\n", reads, torn);
    freeLocalMapping(lm);
    return torn > 0 ? 2 : 0;
}
