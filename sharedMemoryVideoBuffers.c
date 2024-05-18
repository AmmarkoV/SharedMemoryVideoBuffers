#include "sharedMemoryVideoBuffers.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Create and open shared memory segment
int create_shared_memory(const char *shm_name, size_t total_size) {
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return -1;
    }

    if (ftruncate(shm_fd, total_size) == -1) {
        perror("ftruncate");
        close(shm_fd);
        return -1;
    }

    return shm_fd;
}

// Open existing shared memory segment
int open_shared_memory(const char *shm_name, size_t total_size) {
    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        return -1;
    }

    return shm_fd;
}

// Map shared memory segment to address space
void* map_shared_memory(int shm_fd, size_t total_size) {
    void *addr = mmap(0, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    return addr;
}

// Unmap shared memory segment
void unmap_shared_memory(void *addr, size_t total_size) {
    if (munmap(addr, total_size) == -1) {
        perror("munmap");
    }
}

// Close shared memory segment
void close_shared_memory(int shm_fd) {
    close(shm_fd);
}

// Initialize a video frame in shared memory
VideoFrame* init_video_frame(void *shared_mem, size_t width, size_t height, size_t channels) {
    VideoFrame *frame = (VideoFrame*)shared_mem;
    frame->width = width;
    frame->height = height;
    frame->channels = channels;
    frame->frame_size = width * height * channels;
    frame->data = (unsigned char*)(shared_mem + sizeof(VideoFrame));

    return frame;
}

// Get a pointer to a video frame from shared memory
VideoFrame* get_video_frame(void *shared_mem, size_t frame_index) {
    return (VideoFrame*)((unsigned char*)shared_mem + frame_index * (sizeof(VideoFrame) + ((VideoFrame*)shared_mem)->frame_size));
}

// Write data to a video frame
void write_video_frame(VideoFrame *frame, unsigned char *data) {
    memcpy(frame->data, data, frame->frame_size);
}

// Read data from a video frame
void read_video_frame(VideoFrame *frame, unsigned char *buffer) {
    memcpy(buffer, frame->data, frame->frame_size);
}

