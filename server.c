//gcc -o server server.c shared_video.c -lrt -pthread
#include "sharedMemoryVideoBuffers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main()
{
    const char *shm_name = "video_frames.shm";

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
        for (unsigned int i = 0; i < context->numberOfBuffers; i++)
        {
            struct VideoFrame *frame = &context->buffer[i];

            if (frame->data != NULL)
            {
                fprintf(stderr,"Frame %u - %ux%u:%u - ",i,frame->width,frame->height,frame->channels);
                fprintf(stderr,"%s\n",frame->name);
                map_frame_shared_memory(frame);

                char filename[256];
                snprintf(filename, sizeof(filename), "server_stream%u.pnm", i);

                if (startReadingFromVideoBufferPointer(frame) == 0)
                {
                    WriteVideoFrame(filename, frame);
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
