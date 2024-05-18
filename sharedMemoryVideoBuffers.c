#include "sharedMemoryVideoBuffers.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>



unsigned int simplePowPPM(unsigned int base,unsigned int exp)
{
    if (exp==0) return 1;
    unsigned int retres=base;
    unsigned int i=0;
    for (i=0; i<exp-1; i++)
    {
        retres*=base;
    }
    return retres;
}


int WriteVideoFrame(const char * filename,struct VideoFrame * pic)
{
    //fprintf(stderr,"saveRawImageToFile(%s) called\n",filename);
    if (pic==0) { return 0; }

    if(pic->data==0) { fprintf(stderr,"saveRawImageToFile(%s) called for an unallocated (empty) frame , will not write any file output\n",filename); return 0; }


    FILE *fd=0;
    fd = fopen(filename,"wb");

    if (fd!=0)
    {
        unsigned int n;
        if (pic->channels==3) fprintf(fd, "P6\n");
        else if (pic->channels==1) fprintf(fd, "P5\n");
        else
        {
            fprintf(stderr,"Invalid channels arg (%u) for SaveRawImageToFile\n",pic->channels);
            fclose(fd);
            return 1;
        }

        fprintf(fd, "%d %d\n%u\n", pic->width, pic->height , simplePowPPM(2 ,8 )-1);

        float tmp_n = (float) 8 / 8;
        tmp_n = tmp_n *  pic->width * pic->height * pic->channels ;
        n = (unsigned int) tmp_n;

        fwrite(pic->data, 1 , n , fd);
        fflush(fd);
        fclose(fd);
        return 1;
    }
    else
    {
        fprintf(stderr,"SaveRawImageToFile could not open output file %s\n",filename);
        return 0;
    }
    return 0;
}




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

// Create shared memory for a video frame
int create_frame_shared_memory(struct VideoFrame *frame)
{
    int shm_fd = shm_open(frame->name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("shm_open frame");
        return -1;
    }

    if (ftruncate(shm_fd, frame->frame_size) == -1)
    {
        perror("ftruncate frame");
        close(shm_fd);
        return -1;
    }

    frame->data = (unsigned char*) mmap(NULL, frame->frame_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (frame->data == MAP_FAILED)
    {
        perror("mmap frame");
        close(shm_fd);
        return -1;
    }

    close(shm_fd);
    return 0;
}

// Map existing shared memory for a video frame
int map_frame_shared_memory(struct VideoFrame *frame)
{
    int shm_fd = shm_open(frame->name, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("shm_open frame");
        return -1;
    }

    frame->data = (unsigned char*) mmap(NULL, frame->frame_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (frame->data == MAP_FAILED)
    {
        perror("mmap frame");
        close(shm_fd);
        return -1;
    }

    close(shm_fd);
    return 0;
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
