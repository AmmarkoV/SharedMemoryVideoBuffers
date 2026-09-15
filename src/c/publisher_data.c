/** @file publisher_data.c
 *  @brief  Example publisher of a generic (non-image) data stream: writes a struct sample_data
 *  worth of random bytes to the "data_stream1" stream about 9 times a second, forever.
 *
 *  Creates the "video_frames.shm" context and the stream if needed.
 *
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

//Can also be compiled using :
//gcc  -O3 src/c/publisher_data.c src/c/sharedMemoryVideoBuffers.c -pthread -lrt -lm -o publisher_data

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include "sharedMemoryVideoBuffers.h"


/** @brief Example payload of a generic data stream: any fixed-size, pointer-free struct works. */
struct sample_data
{
    float typeA[6000]; ///< First block of samples
    float typeB[4000]; ///< Second block of samples
    float typeC[1000]; ///< Third block of samples
};


/**
 * @brief Publishes random struct sample_data payloads on "data_stream1" until killed.
 * @param argc Unused.
 * @param argv Unused.
 * @return EXIT_FAILURE if the context or the stream can't be set up (the loop never ends otherwise).
 */
int main(int argc, char *argv[])
{
    const char *shm_name    = "video_frames.shm";
    const char *stream_name = "data_stream1";
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


    // A generic stream is just a size in bytes
    createGenericMetaData(context,stream_name,(unsigned int) sizeof(struct sample_data));
    //createVideoFrameMetaData(context,stream_name,640,480,3);

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

    // Scratch payload, filled with new random bytes every iteration
    unsigned char *data = (unsigned char*)malloc(frame->frame_size);

    if (data!=0)
    {

    srand((unsigned int)time(NULL)); // Seed the random number generator

    while (1)
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
        // Publishes the data to readers and releases the writer lock
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
