/** @file sharedMemoryVideoBuffers.h
 *  @brief  A header-only thread automization library to make your multithreaded-lives easier.
 *  To add to your project just copy this header to your code and don't forget to link with
 *  pthreads, for example : gcc -O3 -pthread yourProject.c -o threadsExample
 *  Repository : https://github.com/AmmarkoV/PThreadWorkerPool
 *  @author Ammar Qammaz (AmmarkoV)
 */

#ifndef SHAREDMEMORYVIDEOBUFFERS_H_INCLUDED
#define SHAREDMEMORYVIDEOBUFFERS_H_INCLUDED

//The star of the show
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>

// Define a structure to hold video frame metadata
typedef struct
{
    size_t width;
    size_t height;
    size_t channels;
    size_t frame_size;
    unsigned char *data;
} VideoFrame;

// Functions to manage shared memory
int create_shared_memory(const char *shm_name, size_t total_size);
int open_shared_memory(const char *shm_name, size_t total_size);
void* map_shared_memory(int shm_fd, size_t total_size);
void unmap_shared_memory(void *addr, size_t total_size);
void close_shared_memory(int shm_fd);

// Functions to manage video frames
VideoFrame* init_video_frame(void *shared_mem, size_t width, size_t height, size_t channels);
VideoFrame* get_video_frame(void *shared_mem, size_t frame_index);
void write_video_frame(VideoFrame *frame, unsigned char *data);
void read_video_frame(VideoFrame *frame, unsigned char *buffer);


#ifdef __cplusplus
}
#endif

#endif // SHAREDMEMORYVIDEOBUFFERS_H_INCLUDED


