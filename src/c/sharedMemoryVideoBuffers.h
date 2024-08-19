/** @file sharedMemoryVideoBuffers.h
 *  @brief  A shared memory wrapper to make processing video streams from multiple processes easier.
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

#define ATTEMPTS_TO_LOCK_A_BUFFER 1000
#define SLEEP_TIME_BETWEEN_LOCK_ATTEMPTS_MICROSECONDS 10

/** @brief Structure to hold video frame metadata.
 */
struct VideoFrame
{
    //Shared Data
    //-----------------------------------------------------------------------------------------------------------
    volatile char locked;
    char name[MAX_SHM_NAME+1];
    unsigned int width;
    unsigned int height;
    unsigned int channels;
    size_t frame_size;
    //-----------------------------------------------------------------------------------------------------------
    unsigned char *client_address_space_data_pointer; //<- BE VERY CAREFUL THIS POINTS TO THE CLIENT DATA, and is an invalid pointer for other processes
};

/** @brief Structure to hold local video frame pointers.
 */
struct VideoFrameLocalMapping
{
    struct SharedMemoryContext * smc;
    unsigned char *data[MAX_NUMBER_OF_BUFFERS];
    size_t         sz[MAX_NUMBER_OF_BUFFERS];
};

/** @brief Structure to hold shared memory context.
 */
struct SharedMemoryContext
{
   unsigned int numberOfBuffers;
   struct VideoFrame buffer[MAX_NUMBER_OF_BUFFERS];
};



int writePNM(const char * filename,int width,int height,int channels, unsigned char * data);

/**
 * @brief Writes a video frame to an image file.
 * @param filename Name of the image file to write.
 * @param pic Pointer to the video frame structure.
 * @param data Pointer to the image data.
 * @return 1 on success, 0 on failure.
 */
int writeVideoFrameToImage(const char * filename,struct VideoFrame * pic, unsigned char * data);

/**
 * @brief Gets the maximum number of buffers in a shared memory context.
 * @return The maximum number of buffers.
 */
int getSharedMemoryContextMAXBuffers();


/**
 * @brief Gets the number of buffers in a shared memory context.
 * @param context Pointer to the shared memory context.
 * @return The number of buffers.
 */
int getSharedMemoryContextNumberOfBuffers(struct SharedMemoryContext *context);

/**
 * @brief Gets a video frame from a shared memory context.
 * @param context Pointer to the shared memory context.
 * @param item Index of the video frame to get.
 * @return Pointer to the video frame.
 */
struct VideoFrame * getSharedMemoryContextVideoFrame(struct SharedMemoryContext *context, int item);

/**
 * @brief Checks if a video frame in a shared memory context is populated.
 * @param context Pointer to the shared memory context.
 * @param item Index of the video frame to check.
 * @return 1 if populated, 0 otherwise.
 */
int remoteSharedMemoryContextVideoFrameIsPopulated(struct SharedMemoryContext *context, int item);

/**
 * @brief Prints the state of a shared memory context.
 * @param context Pointer to the shared memory context.
 */
void printSharedMemoryContextState(struct SharedMemoryContext *context);


// Server process functions

/**
 * @brief Creates a shared memory context descriptor (the server should do that).
 * @param path Path to the shared memory.
 * @return 0 on success, -1 on failure.
 */
int createSharedMemoryContextDescriptor(const char *path);


// Client process functions

/**
 * @brief Connects to a shared memory context descriptor.
 * @param path Path to the shared memory.
 * @return Pointer to the shared memory context.
 */
struct SharedMemoryContext* connectToSharedMemoryContextDescriptor(const char *path);

/**
 * @brief Creates shared memory for a video frame.
 * @param frame Pointer to the video frame structure.
 * @return 0 on success, -1 on failure.
 */
int create_frame_shared_memory(struct VideoFrame *frame);

/**
 * @brief Creates metadata for a video frame.
 * @param context Pointer to the shared memory context.
 * @param streamName Name of the stream.
 * @param width Width of the video frame.
 * @param height Height of the video frame.
 * @param channels Number of channels in the video frame.
 * @return 0 on success, -1 on failure.
 */
int createVideoFrameMetaData(struct SharedMemoryContext* context,const char * streamName,unsigned int width, unsigned int height, unsigned int channels);

/**
 * @brief Destroys a video frame.
 * @param context Pointer to the shared memory context.
 * @param streamName Name of the stream.
 * @return 0 on success, -1 on failure.
 */
int destroyVideoFrame(struct SharedMemoryContext* context,const char * streamName);

/**
 * @brief Allocates local mapping for video frames.
 * @return Pointer to the allocated local mapping.
 */
struct VideoFrameLocalMapping * allocateLocalMapping();

/**
 * @brief Frees local mapping for video frames.
 * @param lm Pointer to the local mapping structure.
 * @return 1 on success, 0 on failure.
 */
int freeLocalMapping(struct VideoFrameLocalMapping * lm);


int resolveFeedNameToID(struct SharedMemoryContext * smvc, const char *feedName);

/**
 * @brief Gets the pointer to the local mapping for a video frame.
 * @param lm Pointer to the local mapping structure.
 * @param item Index of the video frame.
 * @return Pointer to the local mapping.
 */
unsigned char * getLocalMappingPointer(struct VideoFrameLocalMapping * lm,int item);

/**
 * @brief Maps a remote video frame to local memory.
 * @param context Pointer to the shared memory context.
 * @param localMap Pointer to the local mapping structure.
 * @param item Index of the video frame to map.
 * @return 1 on success, 0 on failure.
 */
int mapRemoteToLocal(struct SharedMemoryContext *context, struct VideoFrameLocalMapping * localMap,int item);

/**
 * @brief Unmaps a local mapping for a video frame.
 * @param localmap Pointer to the local mapping structure.
 * @param item Index of the video frame to unmap.
 * @return 1 on success, 0 on failure.
 */
int unmapLocalMappingItem(struct VideoFrameLocalMapping * localmap,int item);

/**
 * @brief Copies data to shared memory for a video frame.
 * @param frame Pointer to the video frame structure.
 * @param src Pointer to the source data.
 * @param n Number of bytes to copy.
 */
void copy_to_shared_memory(struct VideoFrame *frame, const void* src, size_t n);

/**
 * @brief Maps shared memory for a video frame.
 * @param frame Pointer to the video frame structure.
 * @param copyToVideoFramePointer Flag to indicate whether to copy to video frame pointer.
 * @return Pointer to the mapped shared memory.
 */
unsigned char * map_frame_shared_memory(struct VideoFrame *frame,int copyToVideoFramePointer);

// Buffer management functions

/**
 * @brief Gets the pointer to a video buffer.
 * @param smvc Pointer to the shared memory context.
 * @param feedName Name of the feed.
 * @return Pointer to the video buffer.
 */
struct VideoFrame* getVideoBufferPointer(struct SharedMemoryContext * smvc, const char *feedName);



unsigned char * getVideoFrameDataPointer(struct VideoFrame * frame);

unsigned long getVideoFrameDataSize(struct VideoFrame * frame);
unsigned int getVideoFrameWidth(struct VideoFrame * frame);
unsigned int getVideoFrameHeight(struct VideoFrame * frame);
unsigned int getVideoFrameChannels(struct VideoFrame * frame);



/**
 * @brief Starts writing to a video buffer.
 * @param vf Pointer to the video frame structure.
 * @return 1 on success, 0 on failure.
 */
int startWritingToVideoBufferPointer(struct VideoFrame *vf);

/**
 * @brief Stops writing to a video buffer.
 * @param vf Pointer to the video frame structure.
 * @return 1 on success, 0 on failure.
 */
int stopWritingToVideoBufferPointer(struct VideoFrame *vf);

/**
 * @brief Starts reading from a video buffer.
 * @param vf Pointer to the video frame structure.
 * @return 1 on success, 0 on failure.
 */
int startReadingFromVideoBufferPointer(struct VideoFrame *vf);

/**
 * @brief Stops reading from a video buffer.
 * @param vf Pointer to the video frame structure.
 * @return 1 on success, 0 on failure.
 */
int stopReadingFromVideoBufferPointer(struct VideoFrame *vf);


#ifdef __cplusplus
}
#endif

#endif // SHAREDMEMORYVIDEOBUFFERS_H_INCLUDED
