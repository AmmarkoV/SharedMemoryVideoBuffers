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

#define MAX_SHM_NAME 256
#define MAX_NUMBER_OF_BUFFERS 10

// Define a structure to hold video frame metadata
struct VideoFrame
{
    char locked;
    char name[MAX_SHM_NAME+1];
    unsigned int width;
    unsigned int height;
    unsigned int channels;
    size_t frame_size;
    unsigned char *data;
};


struct SharedMemoryContext
{
   unsigned int numberOfBuffers;
   struct VideoFrame buffer[MAX_NUMBER_OF_BUFFERS];

};
int WriteVideoFrame(const char * filename,struct VideoFrame * pic);

// Server process functions
int createSharedMemoryContextDescriptor(const char *path);

// Client process functions
struct SharedMemoryContext* connectToSharedMemoryContextDescriptor(const char *path);

int create_frame_shared_memory(struct VideoFrame *frame);

int map_frame_shared_memory(struct VideoFrame *frame);

// Buffer management functions
struct VideoFrame* getVideoBufferPointer(struct SharedMemoryContext *smvc, const char *feedName);
int startWritingToVideoBufferPointer(struct VideoFrame *vf);
int stopWritingToVideoBufferPointer(struct VideoFrame *vf);
int startReadingFromVideoBufferPointer(struct VideoFrame *vf);
int stopReadingFromVideoBufferPointer(struct VideoFrame *vf);


#ifdef __cplusplus
}
#endif

#endif // SHAREDMEMORYVIDEOBUFFERS_H_INCLUDED
