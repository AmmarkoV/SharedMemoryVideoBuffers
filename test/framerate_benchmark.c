/** @file framerate_benchmark.c
 *  @brief Establishes the framerate one stream of a given image size reaches through shared memory.
 *
 *  A writer process publishes frames as fast as it can, while a reader process copies out every
 *  new frame it sees, as fast as it can. Both run for the same time and report frames per second
 *  and MiB per second, so the numbers are the ceiling the library allows for that image size on
 *  this machine (the reader spins while waiting for a frame, so give it a core of its own).
 *
 *  The frames are generated before timing starts (each filled with one byte value, cycling), so
 *  only publishing and reading are measured. Every frame read is also checked for tearing: its
 *  first, middle and last bytes must match.
 *
 *  Checks: no torn frames (multi-buffered streams only; SHMVB_BUFFER_COUNT=1 has no tearing
 *  protection), and, when min_fps is given, a writer framerate of at least min_fps.
 *
 *  Exit code: 0 = all checks passed, 2 = a check failed, 1 = setup error.
 *
 *  Usage: framerate_benchmark [width=1920] [height=1080] [channels=3] [seconds=3] [min_fps=0] [shm_name]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "sharedMemoryVideoBuffers.h"

/** @brief Different frames generated up front and published in turn. */
#define GENERATED_FRAMES 4

static int failures = 0;

static void check(int passed, const char *name)
{
    fprintf(stderr, "  %s : %s\n", passed ? "ok  " : "FAIL", name);
    if (!passed) { failures++; }
}

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
}

/** @brief What the reader process sends back to the writer. */
struct readerResults
{
    long   framesRead;
    long   tornFrames;
    double elapsed;
};

// Reader process: copies out every new frame until `seconds` after the go signal
static struct readerResults runReader(const char *shm_name, const char *stream_name, double seconds, int goPipe, int readyPipe)
{
    struct readerResults results = { 0, 0, 0.0 };
    struct SharedMemoryContext *ctx = connectToSharedMemoryContextDescriptor(shm_name);
    struct VideoFrame *frame = (ctx != NULL) ? getVideoBufferPointer(ctx, stream_name) : NULL;
    unsigned char *copy = (frame != NULL) ? malloc(frame->frame_size) : NULL;
    if (copy == NULL) { results.framesRead = -1; return results; }
    size_t size = frame->frame_size;

    char token;
    if ((write(readyPipe, "r", 1) != 1) || (read(goPipe, &token, 1) != 1)) { results.framesRead = -1; free(copy); return results; }

    uint64_t lastTimestamp = 0;
    double start = now_seconds(), end = start + seconds;
    while (now_seconds() < end)
    {
        if (!startReadingFromVideoBufferPointer(frame)) { continue; } // nothing published yet
        uint64_t timestamp = getVideoFrameTimestamp(frame);
        if (timestamp != lastTimestamp)
        {
            memcpy(copy, getVideoFrameDataPointer(frame), size);
            lastTimestamp = timestamp;
            results.framesRead++;
            if ((copy[0] != copy[size / 2]) || (copy[0] != copy[size - 1])) { results.tornFrames++; }
        }
        stopReadingFromVideoBufferPointer(frame);
    }
    results.elapsed = now_seconds() - start;
    free(copy);
    return results;
}

int main(int argc, char *argv[])
{
    unsigned int width    = (argc > 1) ? (unsigned int) atoi(argv[1]) : 1920;
    unsigned int height   = (argc > 2) ? (unsigned int) atoi(argv[2]) : 1080;
    unsigned int channels = (argc > 3) ? (unsigned int) atoi(argv[3]) : 3;
    double seconds        = (argc > 4) ? atof(argv[4]) : 3.0;
    double min_fps        = (argc > 5) ? atof(argv[5]) : 0.0;
    const char *shm_name  = (argc > 6) ? argv[6] : "shmvb_test_fps.shm";
    const char *stream_name = "fps";

    if ((width == 0) || (height == 0) || (channels == 0) || (seconds <= 0.0))
    {
        fprintf(stderr, "Usage: %s [width=1920] [height=1080] [channels=3] [seconds=3] [min_fps=0] [shm_name]\n", argv[0]);
        return 1;
    }

    shm_unlink(shm_name);
    if (createSharedMemoryContextDescriptor(shm_name) == -1) { return 1; }
    struct SharedMemoryContext *ctx = connectToSharedMemoryContextDescriptor(shm_name);
    if (!ctx || (createVideoFrameMetaData(ctx, stream_name, width, height, channels) != 0)) { return 1; }
    struct VideoFrame *frame = getVideoBufferPointer(ctx, stream_name);
    if (!frame || (map_frame_shared_memory(frame, 1) == NULL)) { return 1; }
    size_t size = frame->frame_size;
    int protectedStream = (frame->bufferCount > 1);

    unsigned char *frames = (size <= SIZE_MAX / GENERATED_FRAMES) ? malloc(GENERATED_FRAMES * size) : NULL;
    if (!frames) { fprintf(stderr, "Could not allocate %d frames of %zu bytes\n", GENERATED_FRAMES, size); return 1; }
    for (int k = 0; k < GENERATED_FRAMES; k++) { memset(frames + (size_t) k * size, k + 1, size); }

    int goPipe[2], readyPipe[2], resultsPipe[2];
    if ((pipe(goPipe) != 0) || (pipe(readyPipe) != 0) || (pipe(resultsPipe) != 0)) { return 1; }

    pid_t reader = fork();
    if (reader == 0)
    {
        close(goPipe[1]); close(readyPipe[0]); close(resultsPipe[0]);
        struct readerResults results = runReader(shm_name, stream_name, seconds, goPipe[0], readyPipe[1]);
        _exit((write(resultsPipe[1], &results, sizeof(results)) == sizeof(results)) ? 0 : 1);
    }
    close(goPipe[0]); close(readyPipe[1]); close(resultsPipe[1]);

    char token;
    if (read(readyPipe[0], &token, 1) != 1) { fprintf(stderr, "The reader failed to start\n"); return 1; }
    if (write(goPipe[1], "g", 1) != 1) { return 1; }

    // Writer: publish the generated frames in turn, as fast as possible
    long framesWritten = 0, droppedWrites = 0;
    double start = now_seconds(), end = start + seconds;
    while (now_seconds() < end)
    {
        if (!startWritingToVideoBufferPointer(frame)) { droppedWrites++; continue; }
        // Timestamps count frames, so every publish is a new frame to the reader
        copy_to_shared_memory(frame, frames + (size_t) (framesWritten % GENERATED_FRAMES) * size, size, (uint64_t) framesWritten + 1);
        stopWritingToVideoBufferPointer(frame);
        framesWritten++;
    }
    double writerElapsed = now_seconds() - start;

    struct readerResults results;
    int gotResults = (read(resultsPipe[0], &results, sizeof(results)) == sizeof(results)) && (results.framesRead >= 0);
    waitpid(reader, NULL, 0);

    double mib = (double) size / (1024.0 * 1024.0);
    double writerFps = (double) framesWritten / writerElapsed;
    char label[128], line[512];
    snprintf(label, sizeof(label), "framerate %ux%ux%u (%.2f MiB/frame, %u slots)", width, height, channels, mib, frame->bufferCount);

    if (!gotResults) { check(0, "framerate: the reader failed"); }
    else
    {
        double readerFps = (double) results.framesRead / results.elapsed;
        snprintf(line, sizeof(line), "%s: writer %.1f fps (%.0f MiB/s, %ld dropped writes), reader %.1f fps (%.0f MiB/s)",
                 label, writerFps, writerFps * mib, droppedWrites, readerFps, readerFps * mib);
        check(framesWritten > 0 && results.framesRead > 0, line);

        if (protectedStream)
        {
            snprintf(line, sizeof(line), "%s: no torn frames (%ld of %ld)", label, results.tornFrames, results.framesRead);
            check(results.tornFrames == 0, line);
        } else
        {
            fprintf(stderr, "  info : %s: %ld torn frames of %ld (single-buffered stream: no tearing protection)\n",
                    label, results.tornFrames, results.framesRead);
        }
    }
    if (min_fps > 0.0)
    {
        snprintf(line, sizeof(line), "%s: writer reaches at least %.1f fps", label, min_fps);
        check(writerFps >= min_fps, line);
    }

    free(frames);
    destroyVideoFrame(ctx, stream_name);
    shm_unlink(shm_name);
    return (failures > 0) ? 2 : 0;
}
