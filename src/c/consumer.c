/** @file consumer.c
 *  @brief  Example consumer: reads the "stream1" stream through a VideoFrameLocalMapping and
 *  saves the latest frame to data/consumer_stream0.pnm about 9 times a second, until interrupted.
 *
 *  Needs an existing "video_frames.shm" context and "stream1" stream (e.g. from publisher.c),
 *  and an existing data/ directory.
 *
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

//Can also be compiled using :
//gcc  -O3 src/c/consumer.c src/c/sharedMemoryVideoBuffers.c -pthread -lrt -lm -o consumer

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
 * @brief Saves frames of "stream1" to data/consumer_stream0.pnm until interrupted.
 * @param argc Unused.
 * @param argv Unused.
 * @return EXIT_FAILURE if the context, the stream or the mapping isn't available.
 */
int main(int argc, char *argv[])
{
    const char *shm_name    = "video_frames.shm";
    const char *stream_name = "stream1";

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    // A consumer only connects: it never creates the context or the stream
    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(shm_name);
    if (!context)
    {
        return EXIT_FAILURE;
    }

    struct VideoFrame *frame = getVideoBufferPointer(context,stream_name);
    if (!frame)
    {
        return EXIT_FAILURE;
    }

    // Slot index of the stream, used to address it in the local mapping
    int item = resolveFeedNameToID(context,stream_name);
    if (item==-1)
    {
        fprintf(stderr,"Could not resolve feed %s\n",stream_name);
        return EXIT_FAILURE;
    }

    struct VideoFrameLocalMapping * localMap = allocateLocalMapping();
    if (localMap==0)
    {
        fprintf(stderr,"Could not allocate a local map\n");
        return EXIT_FAILURE;
    }

    if (mapRemoteToLocal(context,localMap,item))
    {
     while (running)
     {
      printSharedMemoryContextState(context);

      fprintf(stderr,"Read %lu bytes of dummy data\n",frame->frame_size);
      // Example to read from buffer (Client)
      if (startReadingFromVideoBufferPointer(frame))
      {
         // Resolves to the slot this read latched onto
         unsigned char * data = getLocalMappingPointer(localMap,item);
         writePNM("data/consumer_stream0.pnm",frame->width,frame->height,frame->channels,data);
         stopReadingFromVideoBufferPointer(frame);
      }
      usleep(115000);
     }
    }

    // A consumer never owns the stream it reads, so it only lets go of its own
    // mapping - destroying the stream is the publisher's job (see publisher.c)
    freeLocalMapping(localMap);

    fprintf(stderr,"Done..\n");
    return EXIT_SUCCESS;
}
