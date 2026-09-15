/** @file publisher.c
 *  @brief  Example publisher: writes frames of random pixels (640x480 RGB unless another size is
 *  given) to a stream ("stream1" unless another name is given) about 9 times a second, until SIGINT
 *  or SIGTERM, then destroys the stream. NUMBER_OF_FRAMES different frames are generated once at
 *  startup and published in turn.
 *
 *  Usage: publisher [stream_name] [width] [height] [channels]
 *
 *  Needs an existing "video_frames.shm" context (e.g. from server.c).
 *
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

//Can also be compiled using :
//gcc  -O3 src/c/publisher.c src/c/sharedMemoryVideoBuffers.c -pthread -lrt -lm -o publisher

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <limits.h>

#include "sharedMemoryVideoBuffers.h"

/** @brief How many different frames are generated at startup and cycled through. */
#define NUMBER_OF_FRAMES 16

/** @brief Cleared by handle_signal() to leave the publishing loop. */
static volatile int running = 1;

/**
 * @brief SIGINT/SIGTERM handler: asks the publishing loop to stop.
 * @param sig Signal number (unused).
 */
static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

/**
 * @brief Publishes random frames on a stream until interrupted.
 * @param argc Number of arguments.
 * @param argv Optional stream name, frame width, height and channels, in that order (default stream1 640 480 3).
 * @return EXIT_SUCCESS once interrupted, EXIT_FAILURE on invalid arguments or if the context or the stream can't be set up.
 */
int main(int argc, char *argv[])
{
    const char *shm_name    = "video_frames.shm";
    const char *stream_name = (argc > 1) ? argv[1] : "stream1";

    // Frame size: width height channels after the stream name, each optional
    unsigned int size[3] = { 640, 480, 3 };
    for (int i = 2; (i < argc) && (i <= 4); i++)
    {
        char *end = NULL;
        unsigned long value = strtoul(argv[i], &end, 10);
        if ((argv[i][0] == '-') || (end == argv[i]) || (*end != 0) || (value == 0) || (value > UINT_MAX))
        {
            fprintf(stderr, "Invalid frame size argument '%s'\nUsage: %s [stream_name] [width] [height] [channels]\n", argv[i], argv[0]);
            return EXIT_FAILURE;
        }
        size[i-2] = (unsigned int) value;
    }
    unsigned int width = size[0], height = size[1], channels = size[2];

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    // Connect to the existing shared memory context created by the server
    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(shm_name);
    if (!context)
    {
        return EXIT_FAILURE;
    }

    // Create the stream, or join it if it already exists with this size
    if (createVideoFrameMetaData(context,stream_name,width,height,channels) != EXIT_SUCCESS)
    {
        // e.g. a live publisher already owns the stream at another size
        fprintf(stderr,"Could not create stream %s at %ux%u:%u\n",stream_name,width,height,channels);
        return EXIT_FAILURE;
    }

    struct VideoFrame *frame = getVideoBufferPointer(context,stream_name);
    if (!frame)
    {
        return EXIT_FAILURE;
    }

    if (map_frame_shared_memory(frame,1) == NULL)  //We want to overwrite the frame->data because we are the client and this makes the python API easier
    {
        return EXIT_FAILURE;
    }

    // NUMBER_OF_FRAMES frames of random pixels, generated once: calling rand() for every
    // pixel of every published frame cost far more than publishing it
    unsigned char *data = NULL;
    if (frame->frame_size <= SIZE_MAX / NUMBER_OF_FRAMES)
    {
        data = (unsigned char*)malloc(NUMBER_OF_FRAMES * frame->frame_size);
    }
    if (data==0)
    {
        fprintf(stderr,"Could not allocate %u frames of %zu bytes\n",NUMBER_OF_FRAMES,frame->frame_size);
    }

    if (data!=0)
    {

    srand((unsigned int)time(NULL)); // Seed the random number generator
    for (size_t i = 0; i < NUMBER_OF_FRAMES * frame->frame_size; i++)
    {
        data[i] = rand() % 255;
    }

    unsigned int frameNumber = 0;
    while (running)
    {
     printSharedMemoryContextState(context);
     fprintf(stderr,"Write %lu bytes of dummy data\n",frame->frame_size);
     // Example to write to buffer (Client)
     if (startWritingToVideoBufferPointer(frame))
     {
        // The next of the pre-generated frames, in turn
        unsigned char *nextFrame = data + ((size_t) frameNumber * frame->frame_size);
        frameNumber = (frameNumber + 1) % NUMBER_OF_FRAMES;

        // Timestamp 0: stamped with the current time
        copy_to_shared_memory((void *)frame, nextFrame, frame->frame_size, 0);
        // Publishes the frame to readers and releases the writer lock
        stopWritingToVideoBufferPointer(frame);
     }
     usleep(115000);

    }

    free(data);
    } //Managed to allocate memory
    destroyVideoFrame(context,stream_name);

    fprintf(stderr,"Done..\n");
    return EXIT_SUCCESS;
}
