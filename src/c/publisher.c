/** @file example.c
 *  @brief  An example to organize a large number of concurrently working threads without
 *  too many lines of code or difficulty understanding what is happening
 *  https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

//Can also be compiled using :
//gcc  -O3 example.c -pthread -lm -o example

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "sharedMemoryVideoBuffers.h"



int main(int argc, char *argv[])
{
    const char *shm_name    = "video_frames.shm";
    const char *stream_name = "stream1";
    // Client process
    if (createSharedMemoryContextDescriptor(shm_name) == -1)
    {
        return EXIT_FAILURE;
    }

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

    struct VideoFrameLocalMapping localMap={0};
    if (map_frame_shared_memory(frame,1) == NULL)  //We want to overwrite the frame->data because we are the client and this makes the python API easier
    {
        return EXIT_FAILURE;
    }


    srand((unsigned int)time(NULL)); // Seed the random number generator

    while (1)
    {
    printSharedMemoryContextState(context);
    fprintf(stderr,"Write %lu bytes of dummy data\n",frame->frame_size);
    // Example to write to buffer (Client)
    if (startWritingToVideoBufferPointer(frame))
    {
        unsigned char *data = (unsigned char*)malloc(frame->frame_size);
        for (size_t i = 0; i < frame->frame_size; i++)
        {
            data[i] = rand() % 255;
        }

        copy_to_shared_memory((void *)frame, data,frame->frame_size);

        //memcpy(frame->data, data, frame->frame_size);
        stopWritingToVideoBufferPointer(frame);
        free(data);
    }
     usleep(115000);

    }

    destroyVideoFrame(context,stream_name);

    fprintf(stderr,"Done..\n");
    return EXIT_SUCCESS;
}
