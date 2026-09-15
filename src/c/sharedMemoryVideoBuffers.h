/** @file sharedMemoryVideoBuffers.h
 *  @brief  A shared memory wrapper to make processing video streams from multiple processes easier.
 *
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 *
 *  @section shmvb_overview Overview
 *  Processes share frames through a named POSIX shared memory *context*
 *  (struct SharedMemoryContext), created with createSharedMemoryContextDescriptor() and
 *  mapped with connectToSharedMemoryContextDescriptor(). The context is a registry of up to
 *  MAX_NUMBER_OF_BUFFERS named *streams* (struct VideoFrame), created or joined with
 *  createVideoFrameMetaData() or createGenericMetaData() and looked up with getVideoBufferPointer().
 *  The pixels of each stream live in a shared memory object of their own, holding
 *  VideoFrame::bufferCount copies ("slots") of one frame, so a writer never overwrites a frame
 *  that is still being read.
 *
 *  @section shmvb_writing Writing a frame
 *  @code
 *  struct VideoFrame *frame = getVideoBufferPointer(context,"stream1");
 *  if (startWritingToVideoBufferPointer(frame))
 *  {
 *      copy_to_shared_memory(frame, pixels, frame->frame_size, 0); // 0 = stamp with the current time
 *      stopWritingToVideoBufferPointer(frame);                     // publishes the frame
 *  }
 *  @endcode
 *
 *  @section shmvb_reading Reading a frame
 *  @code
 *  if (startReadingFromVideoBufferPointer(frame))
 *  {
 *      unsigned char *pixels = getVideoFrameDataPointer(frame); // frame->frame_size bytes, valid until stop
 *      uint64_t timestamp    = getVideoFrameTimestamp(frame);   // of the frame being read
 *      // ... use pixels ...
 *      stopReadingFromVideoBufferPointer(frame);
 *  }
 *  @endcode
 *
 *  @section shmvb_threads Threads and processes
 *  - A start/stop pair must be called on the same thread: which slot a read or write uses is
 *    remembered per thread.
 *  - Processes that died mid-read, while creating a stream or while owning a stream are detected
 *    by PID, so every process sharing a context must see the same PIDs (same PID namespace;
 *    with docker, run containers with --pid host).
 *
 *  @section shmvb_environment Environment variables
 *  - SHMVB_BUFFER_COUNT : slots given to newly created streams, 1 to MAX_LOCAL_BUFFERS
 *    (default MAX_LOCAL_BUFFERS). 1 disables multi-buffering.
 *  - SHMVB_VERBOSE : "1" or "true" makes printSharedMemoryContextState() print.
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
#include <stdint.h>

/** @brief Longest shared memory context name accepted (stream names must be shorter).
 *  Name buffers in shared structures are MAX_SHM_NAME+1 bytes, for the terminating NUL. */
#define MAX_SHM_NAME 256
/** @brief Maximum number of streams in one SharedMemoryContext (the size of SharedMemoryContext::buffer). */
#define MAX_NUMBER_OF_BUFFERS 10

/** @brief How many times a lock (writer lock, registry lock) or a free slot is retried before giving up. */
#define ATTEMPTS_TO_LOCK_A_BUFFER 1000
/** @brief Sleep between two of the ATTEMPTS_TO_LOCK_A_BUFFER attempts. usleep() oversleeps such
 *  short requests, so giving up takes tens of milliseconds in practice. */
#define SLEEP_TIME_BETWEEN_LOCK_ATTEMPTS_MICROSECONDS 10

/** @brief Number of physical copies ("slots") kept per stream.
 *
 *  With several slots a writer never has to overwrite data a reader might still be copying out
 *  (which is otherwise a real torn-read race: the old design only checked "is a writer active?"
 *  once before reading, with nothing stopping a writer from starting mid-read). The count
 *  actually used per stream is chosen at creation time (env var SHMVB_BUFFER_COUNT,
 *  default MAX_LOCAL_BUFFERS), clamped to this ceiling; 1 disables multi-buffering entirely and
 *  reproduces the original single-buffer behavior byte-for-byte.
 */
#define MAX_LOCAL_BUFFERS 4

/** @brief Maximum number of reads that can be in progress on one stream at the same
 *  time (across all processes and threads). A start-reading call beyond this
 *  fails (returns 0) instead of reading unprotected.
 */
#define MAX_READERS_PER_STREAM 32

/** @brief Compile-time switch (0/1) for the library's internal debug messages on stderr. */
#define DEBUG_MESSAGES 0

/** @brief Identifies a SharedMemoryContext laid out by this library ("SHMV"). */
#define SHMVB_CONTEXT_MAGIC   0x53484D56 /* "SHMV" */
/** @brief Layout version of the shared structures.
 *
 *  Bump SHMVB_CONTEXT_VERSION whenever a shared structure below changes, so
 *  programs built against different layouts refuse to share memory instead of
 *  silently reading each other's fields at the wrong offsets.
 */
#define SHMVB_CONTEXT_VERSION 2

/** @brief Structure to hold video frame metadata: one stream of a SharedMemoryContext.
 *
 *  Lives entirely in shared memory: it must not contain pointers, which are only meaningful in one process.
 *
 *  Multi-buffering: the shared memory backing this stream actually holds
 *  #bufferCount copies of #frame_size bytes each. A writer always fills a
 *  slot that isn't #latestIndex and has no active readers, then publishes
 *  it; readers latch onto whatever #latestIndex is and register themselves
 *  on it in #readers for the duration of their read, so the writer can
 *  never be filling a slot a reader is draining. Registrations carry the
 *  reader's PID so the writer can reclaim ones left by readers that died
 *  mid-read. A #bufferCount of 1 disables all of this (no extra memory, no
 *  protection - identical to the original design).
 *
 *  Where this process mapped the pixels is tracked by the library per process
 *  (see getVideoFrameDataPointer()), never in this shared structure.
 */
struct VideoFrame
{
    //Shared Data
    //-----------------------------------------------------------------------------------------------------------
    volatile char locked;              ///< Writer lock (test-and-set): set while a writer is between start/stopWritingToVideoBufferPointer()
    volatile int is_populated;         ///< 1 while this slot holds a stream, 0 while it's free or being (re)created
    char name[MAX_SHM_NAME+1];         ///< Stream name, as passed to createVideoFrameMetaData() / createGenericMetaData()
    char backingName[MAX_SHM_NAME+1];  ///< shm object holding the pixels: "/<context>.<stream>.<generation>"
    uint64_t generation;               ///< Changes whenever this slot gets a new backing object, so processes notice a re-created stream
    volatile int32_t ownerPid;         ///< Process that created the stream; only it may destroy or resize it (anyone may once it has exited)
    unsigned int width;                ///< Frame width in pixels (the data size, for generic data streams)
    unsigned int height;               ///< Frame height in pixels (1 for generic data streams)
    unsigned int channels;             ///< Bytes per pixel (1 for generic data streams)
    size_t frame_size;                 ///< Size in bytes of ONE slot/buffer (NOT the total shared memory size when bufferCount>1)

    unsigned int bufferCount;                                ///< Number of slots in the backing object (1 to MAX_LOCAL_BUFFERS), fixed when the stream is created
    unsigned int writeIndex;                                 ///< Slot claimed by the writer; only meaningful while #locked
    volatile unsigned int latestIndex;                       ///< Slot most recently published as a complete frame
    volatile uint64_t readers[MAX_READERS_PER_STREAM];       ///< Active reads: (reader PID << 32) | slot, 0 = free entry
    volatile uint64_t timestamps[MAX_LOCAL_BUFFERS];         ///< Per-slot Unix epoch NANOSECONDS (unless a writer passed another value); 0 = no frame published in this slot yet
    //-----------------------------------------------------------------------------------------------------------
};

/** @brief Structure to hold local video frame pointers. Process-local.
 *
 *  Resolve pixel pointers with getLocalMappingPointer(): #data can go stale
 *  once the stream is re-created.
 */
struct VideoFrameLocalMapping
{
    struct SharedMemoryContext * smc;                ///< Context the items belong to, set by mapRemoteToLocal()
    unsigned char *data[MAX_NUMBER_OF_BUFFERS];      ///< Start of this process's mapping of each item (all its slots), NULL if not mapped
    size_t         sz[MAX_NUMBER_OF_BUFFERS];        ///< Size in bytes of each item's mapping (bufferCount * frame_size), 0 if not mapped
};

/** @brief Structure to hold shared memory context: the registry of streams, shared by every process that connects to it.
 */
struct SharedMemoryContext
{
   uint32_t magic;                              ///< SHMVB_CONTEXT_MAGIC once fully initialized
   uint32_t version;                            ///< SHMVB_CONTEXT_VERSION
   char descriptorName[MAX_SHM_NAME+1];         ///< This context's shm name, used to namespace its streams' backing objects
   volatile int32_t registryLockPid;            ///< Process creating/destroying a stream right now, 0 = nobody
   uint64_t nextGeneration;                     ///< Next VideoFrame::generation to hand out (under the registry lock)
   unsigned int numberOfBuffers;                ///< Every stream is in a slot below this; slots below it can be free (check is_populated)
   struct VideoFrame buffer[MAX_NUMBER_OF_BUFFERS]; ///< The stream slots
};



/**
 * @brief Writes raw pixels to a binary PNM image (P6 for 3 channels, P5 for 1 channel).
 * @param filename Name of the image file to write.
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @param channels Bytes per pixel: 1 (grayscale) or 3 (RGB).
 * @param data width*height*channels bytes of pixels.
 * @return 1 on success, 0 if filename or data is NULL, channels is neither 1 nor 3 or the size is not
 * positive (no file is created then), or the file couldn't be opened or completely written.
 */
int writePNM(const char * filename,int width,int height,int channels, unsigned char * data);

/**
 * @brief Writes a video frame to an image file.
 * @param filename Name of the image file to write.
 * @param pic Pointer to the video frame structure (provides the size and channels).
 * @param data Pointer to the image data, e.g. from getVideoFrameDataPointer().
 * @return As writePNM(); 0 if pic is NULL.
 */
int writeVideoFrameToImage(const char * filename,struct VideoFrame * pic, unsigned char * data);

/**
 * @brief Gets the maximum number of buffers in a shared memory context.
 * @return The maximum number of buffers (MAX_NUMBER_OF_BUFFERS).
 */
int getSharedMemoryContextMAXBuffers();


/**
 * @brief Gets the number of buffers in a shared memory context.
 * @param context Pointer to the shared memory context.
 * @return One past the highest slot holding a stream, 0 if context is NULL. Destroying a stream doesn't move
 * the others, so slots below this can be free: check remoteSharedMemoryContextVideoFrameIsPopulated().
 */
int getSharedMemoryContextNumberOfBuffers(struct SharedMemoryContext *context);

/**
 * @brief Gets a video frame from a shared memory context.
 * @param context Pointer to the shared memory context.
 * @param item Index of the video frame to get.
 * @return Pointer to the video frame, or NULL if context is NULL or item is not below
 * getSharedMemoryContextNumberOfBuffers(). The slot may be free.
 */
struct VideoFrame * getSharedMemoryContextVideoFrame(struct SharedMemoryContext *context, unsigned int item);

/**
 * @brief Checks if a video frame in a shared memory context is populated.
 * @param context Pointer to the shared memory context.
 * @param item Index of the video frame to check.
 * @return 1 if populated, 0 otherwise.
 */
int remoteSharedMemoryContextVideoFrameIsPopulated(struct SharedMemoryContext *context, unsigned int item);

/**
 * @brief Prints the state of a shared memory context to stderr. Prints nothing unless the
 * SHMVB_VERBOSE environment variable is "1" or "true", so it is cheap to call every frame.
 * @param context Pointer to the shared memory context.
 */
void printSharedMemoryContextState(struct SharedMemoryContext *context);


// Server process functions

/**
 * @brief Creates a shared memory context descriptor if needed. A context that already
 * exists with this build's layout is left untouched, streams included; a missing one is
 * created empty. One laid out by an incompatible build is replaced by a new, empty one:
 * programs still using the old one keep it (unaffected, but no longer shared with anyone new).
 * @param path Name of the shared memory object (as for shm_open(), e.g. "video_frames.shm"), at most MAX_SHM_NAME characters.
 * @return 0 on success, -1 on failure.
 */
int createSharedMemoryContextDescriptor(const char *path);


// Client process functions

/**
 * @brief Connects to a shared memory context descriptor.
 * @param path Name of the shared memory object, as passed to createSharedMemoryContextDescriptor().
 * @return Pointer to the shared memory context, or NULL if it doesn't exist or was
 * laid out by an incompatible build of this library. Unmap it with
 * munmap(context, sizeof(struct SharedMemoryContext)) when done.
 */
struct SharedMemoryContext* connectToSharedMemoryContextDescriptor(const char *path);

/**
 * @brief Creates metadata for a video frame, or joins the stream if it already exists.
 * An existing stream with the same size is joined as-is (nothing is reset). One with a
 * different size is replaced only if this process created it or its creator has exited;
 * otherwise this fails.
 * @param context Pointer to the shared memory context.
 * @param streamName Name of the stream: not empty, shorter than MAX_SHM_NAME, no '/'.
 * @param width Width of the video frame.
 * @param height Height of the video frame.
 * @param channels Number of channels in the video frame.
 * @return EXIT_SUCCESS (0) on success, EXIT_FAILURE on failure.
 */
int createVideoFrameMetaData(struct SharedMemoryContext* context,const char * streamName,unsigned int width, unsigned int height, unsigned int channels);



/**
 * @brief Creates metadata for a generic data frame. Joins or replaces an existing
 * stream exactly like createVideoFrameMetaData(). The stream is described as a
 * dataSize x 1 frame with 1 channel.
 * @param context Pointer to the shared memory context.
 * @param streamName Name of the stream: not empty, shorter than MAX_SHM_NAME, no '/'.
 * @param dataSize Size of data in bytes for the data frame.
 * @return EXIT_SUCCESS (0) on success, EXIT_FAILURE on failure.
 */
int createGenericMetaData(struct SharedMemoryContext* context,const char * streamName,unsigned int dataSize);


/**
 * @brief Destroys a video frame. Only the process that created the stream may do this,
 * or any process once the creator has exited. Other streams keep their slots.
 * Processes still using the stream's memory keep it mapped until they let go of it.
 * @param context Pointer to the shared memory context.
 * @param streamName Name of the stream.
 * @return EXIT_SUCCESS (0) on success, EXIT_FAILURE on failure.
 */
int destroyVideoFrame(struct SharedMemoryContext* context,const char * streamName);

/**
 * @brief Allocates local mapping for video frames.
 * @return Pointer to the allocated, zeroed local mapping (free it with freeLocalMapping()), or NULL if out of memory.
 */
struct VideoFrameLocalMapping * allocateLocalMapping();

/**
 * @brief Frees local mapping for video frames, unmapping every item still mapped.
 * @param lm Pointer to the local mapping structure.
 * @return 1 on success, 0 on failure.
 */
int freeLocalMapping(struct VideoFrameLocalMapping * lm);


/**
 * @brief Finds the slot of a stream by name.
 * @param smvc Pointer to the shared memory context.
 * @param feedName Name of the stream.
 * @return Index of the stream's slot in SharedMemoryContext::buffer, or -1 if there is no such stream.
 */
int resolveFeedNameToID(struct SharedMemoryContext * smvc, const char *feedName);

/**
 * @brief Gets the pointer to the local mapping for a video frame.
 * @param lm Pointer to the local mapping structure.
 * @param item Index of the video frame.
 * @return Pointer to the frame's pixels, as getVideoFrameDataPointer() resolves them, or NULL
 * if the item was never mapped with mapRemoteToLocal() or its slot holds no stream.
 */
unsigned char * getLocalMappingPointer(struct VideoFrameLocalMapping * lm, unsigned int item);

/**
 * @brief Maps a remote video frame to local memory. Calling it again after the stream
 * was re-created is harmless: reads follow a re-created stream automatically.
 * @param context Pointer to the shared memory context.
 * @param localMap Pointer to the local mapping structure.
 * @param item Index of the video frame to map.
 * @return 1 on success, 0 on failure (including when the slot holds no stream).
 */
int mapRemoteToLocal(struct SharedMemoryContext *context, struct VideoFrameLocalMapping * localMap, unsigned int item);

/**
 * @brief Unmaps a local mapping for a video frame. The memory is unmapped right away,
 * unless a read or write in this process still uses it (then when that one stops).
 * @param localmap Pointer to the local mapping structure.
 * @param item Index of the video frame to unmap.
 * @return 1 on success, 0 on failure (including when the item wasn't mapped).
 */
int unmapLocalMappingItem(struct VideoFrameLocalMapping * localmap,unsigned int item);

/**
 * @brief Copies data to shared memory for a video frame.
 * Between start/stopWritingToVideoBufferPointer() the data goes to the slot claimed by the write;
 * outside a write it overwrites the latest slot, unprotected against concurrent readers.
 * @param frame Pointer to the video frame structure.
 * @param src Pointer to the source data.
 * @param n Number of bytes to copy, at most VideoFrame::frame_size.
 * @param unix_timestamp Timestamp to associate with the frame, stored as given: by convention nanoseconds
 * since the Unix epoch. Pass 0 to use the current time (nanoseconds).
 * @return 1 on success, 0 if the data was rejected (e.g. n larger than the frame). A rejected copy
 * inside start/stopWritingToVideoBufferPointer() is not published.
 */
int copy_to_shared_memory(struct VideoFrame *frame, const void* src, size_t n, uint64_t unix_timestamp);

/**
 * @brief Maps shared memory for a video frame into this process, if it isn't already.
 * Optional: reads and writes map the stream on demand.
 * @param frame Pointer to the video frame structure.
 * @param copyToVideoFramePointer Ignored; kept for compatibility.
 * @return Start of this process's mapping (all bufferCount slots), or NULL on failure.
 */
unsigned char * map_frame_shared_memory(struct VideoFrame *frame,int copyToVideoFramePointer);

// Buffer management functions

/**
 * @brief Gets the pointer to a video buffer.
 * @param smvc Pointer to the shared memory context.
 * @param feedName Name of the feed.
 * @return Pointer to the video buffer, or NULL if there is no stream with that name.
 * The pointer stays valid while the context is mapped; if the stream is destroyed its slot becomes free.
 */
struct VideoFrame* getVideoBufferPointer(struct SharedMemoryContext * smvc, const char *feedName);



/**
 * @brief Gets this process's pointer to the frame's pixels: the slot claimed for writing
 * between start/stopWritingToVideoBufferPointer(), the slot latched onto between
 * start/stopReadingFromVideoBufferPointer(), or (unprotected) the latest slot otherwise.
 * @param frame Pointer to the video frame structure.
 * @return Pointer to frame_size bytes, or NULL if the slot holds no stream. Inside a read or
 * write it stays valid until the matching stop call.
 */
unsigned char * getVideoFrameDataPointer(struct VideoFrame * frame);

/**
 * @brief Gets the size of one frame.
 * @param frame Pointer to the video frame structure.
 * @return VideoFrame::frame_size in bytes, 0 if frame is NULL.
 */
unsigned long getVideoFrameDataSize(struct VideoFrame * frame);
/**
 * @brief Gets the frame width.
 * @param frame Pointer to the video frame structure.
 * @return VideoFrame::width, 0 if frame is NULL.
 */
unsigned int getVideoFrameWidth(struct VideoFrame * frame);
/**
 * @brief Gets the frame height.
 * @param frame Pointer to the video frame structure.
 * @return VideoFrame::height, 0 if frame is NULL.
 */
unsigned int getVideoFrameHeight(struct VideoFrame * frame);
/**
 * @brief Gets the number of channels (bytes per pixel).
 * @param frame Pointer to the video frame structure.
 * @return VideoFrame::channels, 0 if frame is NULL.
 */
unsigned int getVideoFrameChannels(struct VideoFrame * frame);
/**
 * @brief Gets the frame's timestamp: nanoseconds since the Unix epoch, unless its writer passed a
 * value in another unit. Between start/stopReadingFromVideoBufferPointer() it belongs to the frame
 * being read; outside a read, to the latest frame.
 * @param frame Pointer to the video frame structure.
 * @return The timestamp; 0 if no frame was published yet or frame is NULL.
 */
uint64_t getVideoFrameTimestamp(struct VideoFrame * frame);

/**
 * @brief Sets the timestamp of the frame being written, between start/stopWritingToVideoBufferPointer().
 * @param frame Pointer to the video frame structure.
 * @param unix_timestamp Stored as given (by convention Unix nanoseconds); 0 = the current time.
 * @warning Outside a write this stamps the slot of the last write, which may be the latest published frame.
 */
void setVideoFrameTimestamp(struct VideoFrame * frame, uint64_t unix_timestamp);

/**
 * @brief Changes the timestamp of the frame currently published as latest, without
 * writing new pixel data. Takes the writer lock, so call it outside start/stopWritingToVideoBufferPointer().
 * Readers that already latched onto that frame may see the new timestamp.
 * @param frame Pointer to the video frame structure.
 * @param unix_timestamp Stored as given (by convention Unix nanoseconds). Pass 0 to use the current time.
 * @return 1 on success, 0 if the writer lock could not be acquired or no frame was published yet.
 */
int setLatestVideoFrameTimestamp(struct VideoFrame * frame, uint64_t unix_timestamp);



/**
 * @brief Starts writing to a video buffer. When multi-buffering is enabled
 * (see MAX_LOCAL_BUFFERS), this claims a free slot other than the one
 * currently published as "latest" - copy_to_shared_memory()/getVideoFrameDataPointer()
 * target that slot automatically until stopWritingToVideoBufferPointer() publishes it.
 * Holds the stream's writer lock until then, so only one writer writes at a time.
 * @param vf Pointer to the video frame structure.
 * @return 1 on success, 0 on failure (timed out waiting for the writer lock, the stream
 * no longer exists, this thread is already writing too many frames, or - only possible under
 * unusually heavy concurrent reader load - no free slot was available).
 */
int startWritingToVideoBufferPointer(struct VideoFrame *vf);

/**
 * @brief Stops writing to a video buffer, publishing the slot just written as
 * the new "latest" complete frame for readers. If the last copy_to_shared_memory()
 * of this write was rejected, nothing is published and the previous frame stays latest.
 * A write that set no timestamp (e.g. filled getVideoFrameDataPointer() in place) is
 * stamped with the current time, so it never carries the timestamp of an older frame.
 * @param vf Pointer to the video frame structure.
 * @return 1 on success, 0 if this thread has no write in progress on vf (nothing is
 * published and the writer lock is left alone).
 */
int stopWritingToVideoBufferPointer(struct VideoFrame *vf);

/**
 * @brief Starts reading from a video buffer. When multi-buffering is enabled,
 * this latches onto whichever slot is currently published as "latest" and
 * marks it as being read, so the writer will never fill it out from under the
 * caller - getVideoFrameDataPointer()/getLocalMappingPointer() resolve to that
 * exact slot for the calling thread until stopReadingFromVideoBufferPointer()
 * is called. Must be paired with a matching stop call on the same thread.
 * Nested reads of the same frame on one thread are not supported: a second
 * start releases the first one's protection.
 *
 * If the stream was destroyed and re-created since this process last used it, the
 * read follows the new stream; memory of the old one stays mapped until reads and
 * writes still using it in this process have stopped.
 * @param vf Pointer to the video frame structure.
 * @return 1 on success, 0 on failure (the slot holds no stream, no frame was published
 * to it yet, a writer holds a single-buffered stream, MAX_READERS_PER_STREAM reads are already in progress on this
 * stream, or this thread is already reading too many frames).
 */
int startReadingFromVideoBufferPointer(struct VideoFrame *vf);

/**
 * @brief Stops reading from a video buffer, releasing the slot claimed by the
 * matching startReadingFromVideoBufferPointer() call on this thread.
 * Harmless when this thread isn't reading vf.
 * @param vf Pointer to the video frame structure.
 * @return 1, or 0 if vf is NULL.
 */
int stopReadingFromVideoBufferPointer(struct VideoFrame *vf);


#ifdef __cplusplus
}
#endif

#endif // SHAREDMEMORYVIDEOBUFFERS_H_INCLUDED
