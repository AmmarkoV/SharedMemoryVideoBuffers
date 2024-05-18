#include "sharedMemoryVideoBuffers.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Create and open shared memory context descriptor
int createSharedMemoryContextDescriptor(const char *path)
{
    int shm_fd = shm_open(path, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("shm_open");
        return -1;
    }

    size_t total_size = sizeof(struct SharedMemoryContext);
    if (ftruncate(shm_fd, total_size) == -1)
    {
        perror("ftruncate");
        close(shm_fd);
        return -1;
    }

    struct SharedMemoryContext *context = (struct SharedMemoryContext*) mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (context == MAP_FAILED)
    {
        perror("mmap");
        close(shm_fd);
        return -1;
    }

    // Initialize the shared memory context
    context->numberOfBuffers = 0;
    for (unsigned int i = 0; i < MAX_NUMBER_OF_BUFFERS; i++)
    {
        context->buffer[i].locked = 0;
        context->buffer[i].data = (unsigned char*)(context + 1) + i * (640 * 480 * 3); // Adjust size as needed
    }

    munmap(context, total_size);
    close(shm_fd);
    return 0;
}

// Connect to existing shared memory context descriptor
struct SharedMemoryContext* connectToSharedMemoryContextDescriptor(const char *path)
{
    int shm_fd = shm_open(path, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("shm_open");
        return NULL;
    }

    size_t total_size = sizeof(struct SharedMemoryContext);
    struct SharedMemoryContext *context = (struct SharedMemoryContext*) mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (context == MAP_FAILED)
    {
        perror("mmap");
        close(shm_fd);
        return NULL;
    }

    close(shm_fd);
    return context;
}

// Get a pointer to a video buffer by feed name
struct VideoFrame* getVideoBufferPointer(struct SharedMemoryContext *smvc, const char *feedName)
{
    for (unsigned int i = 0; i < smvc->numberOfBuffers; i++)
    {
        if (strncmp(smvc->buffer[i].name, feedName, sizeof(smvc->buffer[i].name)) == 0)
        {
            return &smvc->buffer[i];
        }
    }
    return NULL;
}

// Start writing to a video buffer
int startWritingToVideoBufferPointer(struct VideoFrame *vf)
{
    if (__sync_lock_test_and_set(&vf->locked, 1))
    {
        return -1; // Buffer is already locked
    }
    return 0;
}

// Stop writing to a video buffer
int stopWritingToVideoBufferPointer(struct VideoFrame *vf)
{
    __sync_lock_release(&vf->locked);
    return 0;
}

// Start reading from a video buffer
int startReadingFromVideoBufferPointer(struct VideoFrame *vf)
{
    if (__sync_fetch_and_add(&vf->locked, 0))
    {
        return -1; // Buffer is locked
    }
    return 0;
}

// Stop reading from a video buffer
int stopReadingFromVideoBufferPointer(struct VideoFrame *vf)
{
    // No-op for readers
    return 0;
}
