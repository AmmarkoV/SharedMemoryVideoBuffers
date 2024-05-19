/** @file sharedMemoryVideoBuffers.h
 *  @brief  A header-only thread automization library to make processing streams from multiple processes easier.
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
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
    //Shared Data
    //-----------------------------------------------------------------------------------------------------------
    char locked;
    char name[MAX_SHM_NAME+1];
    unsigned int width;
    unsigned int height;
    unsigned int channels;
    size_t frame_size;
    //-----------------------------------------------------------------------------------------------------------
    unsigned char *client_address_space_data_pointer; //<- BE VERY CAREFUL THIS POINTS TO THE CLIENT DATA, and is an invalid pointer for other processes
};

// Define a structure to hold the local video frame pointers
struct VideoFrameLocalMapping
{
    unsigned char *data[MAX_NUMBER_OF_BUFFERS];
    size_t         sz[MAX_NUMBER_OF_BUFFERS];
};


struct SharedMemoryContext
{
   unsigned int numberOfBuffers;
   struct VideoFrame buffer[MAX_NUMBER_OF_BUFFERS];
};



int writeVideoFrameToImage(const char * filename,struct VideoFrame * pic, unsigned char * data);

void printSharedMemoryContextState(struct SharedMemoryContext *context);
// Server process functions
int createSharedMemoryContextDescriptor(const char *path);


// Client process functions
struct SharedMemoryContext* connectToSharedMemoryContextDescriptor(const char *path);

int create_frame_shared_memory(struct VideoFrame *frame);

int createVideoFrameMetaData(struct SharedMemoryContext* context,const char * streamName,unsigned int width, unsigned int height, unsigned int channels);
int destroyVideoFrame(struct SharedMemoryContext* context,const char * streamName);


struct VideoFrameLocalMapping * allocateLocalMapping();
int freeLocalMapping(struct VideoFrameLocalMapping * lm);
unsigned char * getLocalMappingPointer(struct VideoFrameLocalMapping * lm,int item);
int mapRemoteToLocal(struct SharedMemoryContext *context, struct VideoFrameLocalMapping * localMap,int item);
int unmapLocalMappingItem(struct VideoFrameLocalMapping * localmap,int item);

void copy_to_shared_memory(struct VideoFrame *frame, const void* src, size_t n);


unsigned char * map_frame_shared_memory(struct VideoFrame *frame,int copyToVideoFramePointer);

// Buffer management functions
struct VideoFrame* getVideoBufferPointer(struct SharedMemoryContext * smvc, const char *feedName);
int startWritingToVideoBufferPointer(struct VideoFrame *vf);
int stopWritingToVideoBufferPointer(struct VideoFrame *vf);
int startReadingFromVideoBufferPointer(struct VideoFrame *vf);
int stopReadingFromVideoBufferPointer(struct VideoFrame *vf);


#ifdef __cplusplus
}
#endif

#endif // SHAREDMEMORYVIDEOBUFFERS_H_INCLUDED
