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
#include <math.h>
#include <unistd.h>


#include "sharedMemoryVideoBuffers.h"

#define SHM_NAME "/video_frames"
#define FRAME_WIDTH 640
#define FRAME_HEIGHT 480
#define FRAME_CHANNELS 3
#define NUM_FRAMES 10

int main()
{
    size_t frame_size = FRAME_WIDTH * FRAME_HEIGHT * FRAME_CHANNELS;
    size_t total_size = NUM_FRAMES * (sizeof(VideoFrame) + frame_size);

    // Create shared memory
    int shm_fd = create_shared_memory(SHM_NAME, total_size);
    if (shm_fd == -1)
    {
        return EXIT_FAILURE;
    }

    // Map shared memory
    void *shared_mem = map_shared_memory(shm_fd, total_size);
    if (!shared_mem)
    {
        close_shared_memory(shm_fd);
        return EXIT_FAILURE;
    }

    // Initialize video frames
    for (size_t i = 0; i < NUM_FRAMES; i++)
    {
        init_video_frame((unsigned char*)shared_mem + i * (sizeof(VideoFrame) + frame_size), FRAME_WIDTH, FRAME_HEIGHT, FRAME_CHANNELS);
    }

    // Write and read example
    VideoFrame *frame = get_video_frame(shared_mem, 0);
    unsigned char *data = (unsigned char*)malloc(frame->frame_size);
    // Fill data with some values
    for (size_t i = 0; i < frame->frame_size; i++)
    {
        data[i] = i % 256;
    }
    write_video_frame(frame, data);

    unsigned char *buffer = (unsigned char*)malloc(frame->frame_size);
    read_video_frame(frame, buffer);

    // Clean up
    free(data);
    free(buffer);
    unmap_shared_memory(shared_mem, total_size);
    close_shared_memory(shm_fd);

    return EXIT_SUCCESS;
}

