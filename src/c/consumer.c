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

    int item = 0;
    item = resolveFeedNameToID(context,stream_name);

    if (item==-1)
    {
        fprintf(stderr,"Could not resolve feed %s\n",stream_name);
        return EXIT_FAILURE;
    }

    struct VideoFrameLocalMapping localMap={0};
    mapRemoteToLocal(context,&localMap,item);


    srand((unsigned int)time(NULL)); // Seed the random number generator

    while (1)
    {
    printSharedMemoryContextState(context);

    fprintf(stderr,"Read %lu bytes of dummy data\n",frame->frame_size);
    // Example to read from buffer (Client)
    if (startReadingFromVideoBufferPointer(frame))
    {
        unsigned char *buffer = (unsigned char*)malloc(frame->frame_size);
        if (buffer!=0)
        {
         unsigned char * data = getLocalMappingPointer(&localMap,item);

         memcpy(buffer, data, frame->frame_size);

         writePNM("data/consumer_stream0.pnm",frame->width,frame->height,frame->channels,data);
         stopReadingFromVideoBufferPointer(frame);
         free(buffer);
        } else
        {
         fprintf(stderr,"Failed reading back dummy data..\n");
        }
    }
     usleep(115000);

    }

    destroyVideoFrame(context,stream_name);

    fprintf(stderr,"Done..\n");
    return EXIT_SUCCESS;
}
