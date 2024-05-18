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

// Add a new video buffer to shared memory
int add_new_video_buffer(void *shared_mem, const char *stream_name, size_t width, size_t height, size_t channels) {
    VideoBufferList *buffer_list = (VideoBufferList*)shared_mem;
    size_t new_frame_index = buffer_list->count;
    VideoFrame *frame = &buffer_list->frames[new_frame_index];

    strncpy(frame->name, stream_name, sizeof(frame->name) - 1);
    frame->name[sizeof(frame->name) - 1] = '\0';
    frame->width = width;
    frame->height = height;
    frame->channels = channels;
    frame->frame_size = width * height * channels;
    frame->data = (unsigned char*)(shared_mem + sizeof(VideoBufferList) + new_frame_index * (sizeof(VideoFrame) + frame->frame_size));

    buffer_list->count += 1;

    return 0;
}

// Get a pointer to a video buffer by name
VideoFrame* get_video_buffer_ptr(void *shared_mem, const char *stream_name) {
    VideoBufferList *buffer_list = (VideoBufferList*)shared_mem;

    for (size_t i = 0; i < buffer_list->count; ++i) {
        if (strncmp(buffer_list->frames[i].name, stream_name, sizeof(buffer_list->frames[i].name)) == 0) {
            return &buffer_list->frames[i];
        }
    }

    return NULL;
}

// Get a copy of the video frame structure by name
int get_video_buffer(void *shared_mem, const char *stream_name, VideoFrame *frame) {
    VideoFrame *frame_ptr = get_video_buffer_ptr(shared_mem, stream_name);
    if (frame_ptr == NULL) {
        return -1;
    }

    memcpy(frame, frame_ptr, sizeof(VideoFrame));
    return 0;
}

// Write data to a video frame
void write_video_frame(VideoFrame *frame, unsigned char *data) {
    memcpy(frame->data, data, frame->frame_size);
}

// Read data from a video frame
void read_video_frame(VideoFrame *frame, unsigned char *buffer) {
    memcpy(buffer, frame->data, frame->frame_size);
}
