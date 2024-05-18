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
#include <unistd.h>


#include "sharedMemoryVideoBuffers.h"

#define SHM_NAME "video_frames.shm"

int main()
{
    // Server process
    if (createSharedMemoryContextDescriptor(SHM_NAME) == -1)
    {
        return EXIT_FAILURE;
    }

    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(SHM_NAME);
    if (!context)
    {
        return EXIT_FAILURE;
    }

    // Example to add a new buffer (Server)
    struct VideoFrame *newBuffer = &context->buffer[context->numberOfBuffers++];
    snprintf(newBuffer->name,MAX_SHM_NAME,"stream1");
    newBuffer->width      = 640;
    newBuffer->height     = 480;
    newBuffer->channels   = 3;
    newBuffer->frame_size = newBuffer->width * newBuffer->height * newBuffer->channels;

    if (create_frame_shared_memory(newBuffer) == -1)
    {
        return EXIT_FAILURE;
    }

    // Client process
    struct SharedMemoryContext *clientContext = connectToSharedMemoryContextDescriptor(SHM_NAME);
    if (!clientContext)
    {
        return EXIT_FAILURE;
    }

    struct VideoFrame *frame = getVideoBufferPointer(clientContext, "stream1");
    if (!frame)
    {
        return EXIT_FAILURE;
    }

    if (map_frame_shared_memory(frame) == -1)
    {
        return EXIT_FAILURE;
    }

    fprintf(stderr,"Write dummy data\n");
    // Example to write to buffer (Client)
    if (startWritingToVideoBufferPointer(frame) == 0)
    {
        unsigned char *data = (unsigned char*)malloc(frame->frame_size);
        for (size_t i = 0; i < frame->frame_size; i++)
        {
            data[i] = i % 256;
        }
        memcpy(frame->data, data, frame->frame_size);
        stopWritingToVideoBufferPointer(frame);
        free(data);
    }

    fprintf(stderr,"Read dummy data\n");
    // Example to read from buffer (Client)
    if (startReadingFromVideoBufferPointer(frame) == 0)
    {
        unsigned char *buffer = (unsigned char*)malloc(frame->frame_size);
        memcpy(buffer, frame->data, frame->frame_size);
        stopReadingFromVideoBufferPointer(frame);
        free(buffer);
    }

    fprintf(stderr,"Done..\n");
    return EXIT_SUCCESS;
}
