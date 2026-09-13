/** @file timestamp_consistency_reader.c
 *  @brief Reader half of the timestamp/pixel consistency regression test -
 *  see timestamp_consistency_writer.c.
 *
 *  For every frame read, checks that getVideoFrameTimestamp() reports exactly
 *  the generation number encoded in that frame's pixel bytes. A mismatch
 *  would mean the timestamp and the pixel data for a slot got published (or
 *  read back) out of sync with each other.
 *
 *  Exit code: 0 = no mismatches observed, 2 = mismatch(es) detected,
 *  1 = harness/setup error (e.g. the stream never appeared).
 *
 *  Usage: timestamp_consistency_reader [duration_seconds] [shm_name] [stream_name]
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "sharedMemoryVideoBuffers.h"

int main(int argc, char *argv[])
{
    int duration_seconds    = (argc > 1) ? atoi(argv[1]) : 3;
    const char *shm_name    = (argc > 2) ? argv[2] : "shmvb_test_ts.shm";
    const char *stream_name = (argc > 3) ? argv[3] : "ts";

    int read_duration_seconds = duration_seconds > 1 ? duration_seconds - 1 : duration_seconds;

    struct SharedMemoryContext *ctx = connectToSharedMemoryContextDescriptor(shm_name);
    if (!ctx) { fprintf(stderr, "timestamp_consistency_reader: could not connect to %s\n", shm_name); return 1; }

    struct VideoFrame *frame = NULL;
    for (int i=0; i<2000; i++)
    {
        frame = getVideoBufferPointer(ctx, stream_name);
        if (frame) { break; }
        usleep(1000);
    }
    if (!frame) { fprintf(stderr, "timestamp_consistency_reader: stream '%s' never appeared\n", stream_name); return 1; }

    int item = resolveFeedNameToID(ctx, stream_name);
    struct VideoFrameLocalMapping *lm = allocateLocalMapping();
    if (!lm || !mapRemoteToLocal(ctx, lm, item)) { fprintf(stderr, "timestamp_consistency_reader: failed to map stream\n"); return 1; }

    long reads = 0, mismatched = 0;
    time_t start = time(NULL);
    while (time(NULL) - start < read_duration_seconds)
    {
        if (!startReadingFromVideoBufferPointer(frame)) { continue; }

        unsigned char *data = getLocalMappingPointer(lm, item);
        unsigned long timestamp = getVideoFrameTimestamp(frame);

        if (data)
        {
            unsigned char expected = (unsigned char)(timestamp % 256);
            size_t size = getVideoFrameDataSize(frame);
            // Check the first byte and one deep into the frame - enough to
            // catch the pixel data and the timestamp having come from two
            // different writes.
            if (data[0] != expected || data[size/2] != expected)
            {
                mismatched++;
                fprintf(stderr, "MISMATCH at read #%ld: timestamp=%lu (expects byte %u) but pixel=%u\n",
                        reads, timestamp, expected, data[0]);
            }
            reads++;
        }

        stopReadingFromVideoBufferPointer(frame);
    }

    fprintf(stderr, "timestamp_consistency_reader: %ld reads, %ld mismatches\n", reads, mismatched);
    freeLocalMapping(lm);
    return mismatched > 0 ? 2 : 0;
}
