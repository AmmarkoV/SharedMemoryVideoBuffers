/** @file server.c
 *  @brief  Debugging tool that snapshots every stream of the "video_frames.shm" context to
 *  data/server_streamN.pnm, N being the stream's slot.
 *
 *  Creates the context if needed. Takes a snapshot each time Enter is pressed, until
 *  SIGINT, SIGTERM or the end of standard input. Not meant for production: streams don't
 *  need a running server.
 *
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

//gcc -o server src/c/server.c src/c/sharedMemoryVideoBuffers.c -pthread -lrt
#include "sharedMemoryVideoBuffers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>

/** @brief Cleared by handle_signal() to leave the main loop. */
static volatile int running = 1;
/** @brief The connected context (not used outside main()). */
static struct SharedMemoryContext  *g_context  = NULL;
/** @brief The local mapping of every stream (not used outside main()). */
static struct VideoFrameLocalMapping *g_localMap = NULL;

/**
 * @brief SIGINT/SIGTERM handler: asks the main loop to stop. Installed without SA_RESTART,
 * so it also interrupts the wait for Enter.
 * @param sig Signal number (unused).
 */
static void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

/**
 * @brief Snapshots every stream to data/ each time Enter is pressed, until interrupted.
 * @return EXIT_SUCCESS once interrupted or standard input ends, EXIT_FAILURE if the context can't be created or connected.
 */
int main()
{
    // No SA_RESTART: a signal makes the blocking getchar() below return instead of waiting for Enter
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT,  &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    struct VideoFrameLocalMapping * localMap = allocateLocalMapping();
    const char *shm_name = "video_frames.shm";
    char filename[256]={0};

    //Server creates the context if needed (an existing compatible one keeps its streams)
    if (createSharedMemoryContextDescriptor(shm_name) == -1)
    {
        return EXIT_FAILURE;
    }

    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(shm_name);
    if (!context)
    {
        return EXIT_FAILURE;
    }
    g_context  = context;
    g_localMap = localMap;


    if ( system("mkdir -p data/")!=0 )
    {
      fprintf(stderr,"Could not create data/ directory");
    }
    printf("Server is ready. Press Enter to encode frames.\n");

    while (running)
    {
        // Wait for Enter. EOF: interrupted by a signal, or standard input is closed
        // (e.g. started in the background), where waiting would never block again
        if (getchar() == EOF)
        {
          if (running) { fprintf(stderr,"Standard input closed, stopping\n"); }
          break;
        }

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
            //struct VideoFrame *frame = &context->buffer[i];
            struct VideoFrame * frame = getSharedMemoryContextVideoFrame(context,i);

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
                    // e.g. nothing was published to the stream yet
                    fprintf(stderr, "Failed to lock buffer %u for reading\n", i);
                }
            } //client has an allocated data pointer
            else
            {
              // Free slot: drop our mapping of the stream that was there
              unmapLocalMappingItem(localMap,i);
            }
        } //we scan each of the available buffers
    } //server main loop

    // Cleanup on SIGINT/SIGTERM or end of input
    for (unsigned int i = 0; i < MAX_NUMBER_OF_BUFFERS; i++)
    {
        unmapLocalMappingItem(localMap, i);
    }
    freeLocalMapping(localMap);
    munmap(context, sizeof(struct SharedMemoryContext));

    return EXIT_SUCCESS;
}
