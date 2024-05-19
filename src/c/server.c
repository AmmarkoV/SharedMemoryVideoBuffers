//gcc -o server server.c shared_video.c -lrt -pthread
#include "sharedMemoryVideoBuffers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main()
{
    struct VideoFrameLocalMapping * localMap = allocateLocalMapping();
    const char *shm_name = "video_frames.shm";
    char filename[256]={0};


    //Server creates and zeroes out all existing data..
    if (createSharedMemoryContextDescriptor(shm_name) == -1)
    {
        return EXIT_FAILURE;
    }

    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(shm_name);
    if (!context)
    {
        return EXIT_FAILURE;
    }

    system("mkdir -p data/");
    printf("Server is ready. Press Enter to encode frames.\n");

    while (1)
    {
        getchar();  // Wait for Enter key

        if (context->numberOfBuffers==0)
        {
          fprintf(stderr,"Server is empty!\n");

          //Make sure everything is unmapped if server is empty
          for (unsigned int i=0; i<MAX_NUMBER_OF_BUFFERS; i++)
          {
             unmapLocalMappingItem(localMap,i);
          }
        }

        for (unsigned int i = 0; i < getSharedMemoryContextNumberOfBuffers(context); i++)
        {
            struct VideoFrame *frame = &context->buffer[i];

            if (remoteSharedMemoryContextVideoFrameIsPopulated(context,i) != 0) //If the client has a memory address we are good to go
            {
                fprintf(stderr,"Frame %u - %ux%u:%u - %s\n",i,frame->width,frame->height,frame->channels,frame->name);
                snprintf(filename, sizeof(filename), "data/server_stream%u.pnm", i);

                //Dont copy the mmapped memory pointer to the "frame" data because we are the server and
                //we dont want to overwrite the data of the client
                mapRemoteToLocal(context,localMap,i);

                if (startReadingFromVideoBufferPointer(frame))
                {
                    printSharedMemoryContextState(context);
                    writeVideoFrameToImage(filename, frame, getLocalMappingPointer(localMap,i));
                    stopReadingFromVideoBufferPointer(frame);
                }
                else
                {
                    fprintf(stderr, "Failed to lock buffer %u for reading\n", i);
                }
            } //client has an allocated data pointer
            else
            {
              unmapLocalMappingItem(localMap,i);
            }
        } //we scan each of the available buffers
    } //server always on

    return EXIT_SUCCESS;
}
