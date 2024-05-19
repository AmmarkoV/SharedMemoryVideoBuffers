//gcc -o server server.c shared_video.c -lrt -pthread
#include "sharedMemoryVideoBuffers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main()
{
    struct VideoFrameLocalMapping localMap={0};
    const char *shm_name = "video_frames.shm";

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

    printf("Server is ready. Press Enter to encode frames.\n");

    while (1)
    {
        getchar();  // Wait for Enter key

        if (context->numberOfBuffers==0)
        {
          fprintf(stderr,"Server is empty!\n");
        }

        for (unsigned int i = 0; i < context->numberOfBuffers; i++)
        {
            struct VideoFrame *frame = &context->buffer[i];

            if (frame->client_address_space_data_pointer != NULL) //If the client has a memory address we are good to go
            {
                fprintf(stderr,"Frame %u - %ux%u:%u - ",i,frame->width,frame->height,frame->channels);
                fprintf(stderr,"%s\n",frame->name);

                char filename[256];
                snprintf(filename, sizeof(filename), "server_stream%u.pnm", i);

                //Dont copy the mmapped memory pointer to the "frame" data because we are the server and we dont want to overwrite the data of the client
                localMap.data[i] = map_frame_shared_memory(frame,0);
                if (startReadingFromVideoBufferPointer(frame) == 0)
                {
                    printSharedMemoryContextState(context);
                    WriteVideoFrame(filename, frame, localMap.data[i]);
                    stopReadingFromVideoBufferPointer(frame);
                }
                else
                {
                    fprintf(stderr, "Failed to lock buffer %u for reading\n", i);
                }

            }

        }
    }

    return EXIT_SUCCESS;
}
