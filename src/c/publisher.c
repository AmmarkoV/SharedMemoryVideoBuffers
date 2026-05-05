/** @file publisher.c
 *  @brief  An example to organize a large number of concurrently working threads without
 *  too many lines of code or difficulty understanding what is happening
 *  https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

//Can also be compiled using :
//gcc  -O3 publisher.c -pthread -lm -o publisher

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#include "sharedMemoryVideoBuffers.h"

static volatile int running = 1;

static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

int main(int argc, char *argv[])
{
    const char *shm_name    = "video_frames.shm";
    const char *stream_name = "stream1";

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    // Connect to the existing shared memory context created by the server
    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(shm_name);
    if (!context)
    {
        return EXIT_FAILURE;
    }

    createVideoFrameMetaData(context,stream_name,640,480,3);

    struct VideoFrame *frame = getVideoBufferPointer(context,stream_name);
    if (!frame)
    {
        return EXIT_FAILURE;
    }

    if (map_frame_shared_memory(frame,1) == NULL)  //We want to overwrite the frame->data because we are the client and this makes the python API easier
    {
        return EXIT_FAILURE;
    }

    unsigned char *data = (unsigned char*)malloc(frame->frame_size);

    if (data!=0)
    {

    srand((unsigned int)time(NULL)); // Seed the random number generator

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

        copy_to_shared_memory((void *)frame, data, frame->frame_size, 0);
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
