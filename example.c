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
#define TOTAL_SIZE (sizeof(VideoBufferList) + 10 * (sizeof(VideoFrame) + 640 * 480 * 3))

int main() {
    // Create shared memory
    int shm_fd = create_shared_memory(SHM_NAME, TOTAL_SIZE);
    if (shm_fd == -1) {
        return EXIT_FAILURE;
    }

    // Map shared memory
    void *shared_mem = map_shared_memory(shm_fd, TOTAL_SIZE);
    if (!shared_mem) {
        close_shared_memory(shm_fd);
        return EXIT_FAILURE;
    }

    // Initialize the buffer list
    VideoBufferList *buffer_list = (VideoBufferList*)shared_mem;
    buffer_list->count = 0;

    // Add a new video buffer
    if (add_new_video_buffer(shared_mem, "stream1", 640, 480, 3) == -1) {
        unmap_shared_memory(shared_mem, TOTAL_SIZE);
        close_shared_memory(shm_fd);
        return EXIT_FAILURE;
    }

    // Get the video buffer by name
    VideoFrame *frame = get_video_buffer_ptr(shared_mem, "stream1");
    if (!frame) {
        unmap_shared_memory(shared_mem, TOTAL_SIZE);
        close_shared_memory(shm_fd);
        return EXIT_FAILURE;
    }

    // Write and read example
    unsigned char *data = (unsigned char*)malloc(frame->frame_size);
    for (size_t i = 0; i < frame->frame_size; i++) {
        data[i] = i % 256;
    }
    write_video_frame(frame, data);

    unsigned char *buffer = (unsigned char*)malloc(frame->frame_size);
    read_video_frame(frame, buffer);

    // Clean up
    free(data);
    free(buffer);
    unmap_shared_memory(shared_mem, TOTAL_SIZE);
    close_shared_memory(shm_fd);

    return EXIT_SUCCESS;
}
