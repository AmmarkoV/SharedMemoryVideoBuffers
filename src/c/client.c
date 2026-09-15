/** @file client.c
 *  @brief  Example client that both writes and reads a stream: it publishes a 640x480 RGB
 *  frame of random pixels, then reads it back, every few milliseconds, until interrupted.
 *
 *  Creates the "video_frames.shm" context and the "stream1" stream if needed.
 *
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

//Can also be compiled using :
//gcc  -O3 src/c/client.c src/c/sharedMemoryVideoBuffers.c -pthread -lrt -lm -o client

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "sharedMemoryVideoBuffers.h"

/** @brief Cleared by handle_signal() to leave the main loop. */
static volatile int running = 1;

/**
 * @brief SIGINT/SIGTERM handler: asks the main loop to stop.
 * @param sig Signal number (unused).
 */
static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

/**
 * @brief Writes and reads back random frames on "stream1" until interrupted.
 * @param argc Unused.
 * @param argv Unused.
 * @return EXIT_FAILURE if the context or the stream can't be set up (the loop never ends otherwise).
 */
int main(int argc, char *argv[])
{
    const char *shm_name    = "video_frames.shm";
    const char *stream_name = "stream1";

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    // Client process: create the context if nobody did yet (an existing one is kept)
    if (createSharedMemoryContextDescriptor(shm_name) == -1)
    {
        return EXIT_FAILURE;
    }

    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(shm_name);
    if (!context)
    {
        return EXIT_FAILURE;
    }

    // Create the stream, or join it if it already exists with this size
    createVideoFrameMetaData(context,stream_name,640,480,3);

    struct VideoFrame *frame = getVideoBufferPointer(context,stream_name);
    if (!frame)
    {
        return EXIT_FAILURE;
    }

    //struct VideoFrameLocalMapping localMap={0};
    if (map_frame_shared_memory(frame,1) == NULL)  //We want to overwrite the frame->data because we are the client and this makes the python API easier
    {
        return EXIT_FAILURE;
    }


    srand((unsigned int)time(NULL)); // Seed the random number generator

    // Scratch buffers, allocated once: malloc/free on every iteration (at up to
    // ~200 Hz here) cost far more than the shared-memory copy they wrap
    unsigned char *data   = (unsigned char*)malloc(frame->frame_size);
    unsigned char *buffer = (unsigned char*)malloc(frame->frame_size);
    if ((data==0) || (buffer==0))
    {
        fprintf(stderr,"Could not allocate %zu bytes of scratch buffers\n",frame->frame_size);
        free(data);
        free(buffer);
        return EXIT_FAILURE;
    }

    while (running)
    {
    printSharedMemoryContextState(context);
    fprintf(stderr,"Write %lu bytes of dummy data\n",frame->frame_size);
    // Example to write to buffer (Client)
    if (startWritingToVideoBufferPointer(frame))
    {
        for (size_t i = 0; i < frame->frame_size; i++)
        {
            data[i] = rand() % 255;
        }

        // Timestamp 0: stamped with the current time
        copy_to_shared_memory((void *)frame, data, frame->frame_size, 0);

        //memcpy(frame->data, data, frame->frame_size);
        // Publishes the frame to readers and releases the writer lock
        stopWritingToVideoBufferPointer(frame);
    }
     usleep(5000);

    fprintf(stderr,"Read %lu bytes of dummy data\n",frame->frame_size);
    // Example to read from buffer (Client)
    if (startReadingFromVideoBufferPointer(frame))
    {
         // The pointer is only valid until stopReadingFromVideoBufferPointer(): copy what is needed first
         memcpy(buffer, getVideoFrameDataPointer(frame), frame->frame_size);
         stopReadingFromVideoBufferPointer(frame);
    }
     usleep(5000);

    }

    free(data);
    free(buffer);
    destroyVideoFrame(context,stream_name);

    fprintf(stderr,"Done..\n");
    return EXIT_SUCCESS;
}
