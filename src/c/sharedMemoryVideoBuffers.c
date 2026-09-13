/** @file sharedMemoryVideoBuffers.c
 *  @brief  A shared memory wrapper to make processing video streams from multiple processes easier.
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

#include "sharedMemoryVideoBuffers.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#define NORMAL   "\033[0m"
#define BLACK   "\033[30m"      /* Black */
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */

#include <signal.h>
#include <execinfo.h>
#include <unistd.h>

#include <stdarg.h>

void debug_message(const char *format, ...)
{
    #if DEBUG_MESSAGES
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    #endif // DEBUG_MESSAGES
}


void handle_segfault(int sig)
{
    void *array[100];
    size_t size;

    // get void*'s for all entries on the stack
    size = backtrace(array, 100);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
    //exit(1);
    abort();
}

void setup_signal_handlers()
{
    struct sigaction sa;

    sa.sa_handler = handle_segfault;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGSEGV, &sa, NULL) == -1)
    {
        debug_message("sigaction");
        //perror("sigaction");
        //exit(EXIT_FAILURE);
        abort();
    }
}

// How many physical slots (MAX_LOCAL_BUFFERS ceiling) each newly created stream
// gets. Configurable via SHMVB_BUFFER_COUNT so existing deployments can opt out
// (set to 1) without any code change; defaults to 2 (double buffering).
static unsigned int getConfiguredBufferCount()
{
    static int cached = -1;
    if (cached == -1)
    {
        int n = 2;
        const char * env = getenv("SHMVB_BUFFER_COUNT");
        if (env != NULL)
        {
            int parsed = atoi(env);
            if (parsed >= 1 && parsed <= MAX_LOCAL_BUFFERS) { n = parsed; }
        }
        cached = n;
    }
    return (unsigned int) cached;
}

// ---------------------------------------------------------------------------
// Per-thread bookkeeping of "which slot did *this* thread most recently latch
// onto for VideoFrame X via startReadingFromVideoBufferPointer()". This can't
// live in the shared VideoFrame struct itself: two reader processes (or two
// threads) can legitimately be looking at two different slots at once, and
// pointers/state written into shared memory by one process are meaningless to
// another anyway (see the warning on client_address_space_data_pointer). A
// small thread-local table keyed by the VideoFrame's address gives every
// start/stop pair its own private slot index for free, with zero change to
// any caller's code.
// ---------------------------------------------------------------------------
#define MAX_TLS_READ_ENTRIES 16

struct tlsReadEntry
{
    const struct VideoFrame *vf;
    unsigned int index;
    int inUse;
};

static __thread struct tlsReadEntry tlsReadTable[MAX_TLS_READ_ENTRIES];

static void tlsReadIndexStore(const struct VideoFrame *vf, unsigned int index)
{
    for (int i=0; i<MAX_TLS_READ_ENTRIES; i++)
    {
        if (!tlsReadTable[i].inUse || tlsReadTable[i].vf==vf)
        {
            tlsReadTable[i].vf    = vf;
            tlsReadTable[i].index = index;
            tlsReadTable[i].inUse = 1;
            return;
        }
    }
    // Table full (>16 concurrently in-progress reads on one thread - not seen
    // in practice). Silently dropped; readers fall back to "latestIndex" in
    // that case, which is still a complete frame, just not necessarily the
    // exact one this read call started on.
}

static int tlsReadIndexLookup(const struct VideoFrame *vf, unsigned int *outIndex)
{
    for (int i=0; i<MAX_TLS_READ_ENTRIES; i++)
    {
        if (tlsReadTable[i].inUse && tlsReadTable[i].vf==vf)
        {
            *outIndex = tlsReadTable[i].index;
            return 1;
        }
    }
    return 0;
}

static int tlsReadIndexLookupAndClear(const struct VideoFrame *vf, unsigned int *outIndex)
{
    for (int i=0; i<MAX_TLS_READ_ENTRIES; i++)
    {
        if (tlsReadTable[i].inUse && tlsReadTable[i].vf==vf)
        {
            *outIndex = tlsReadTable[i].index;
            tlsReadTable[i].inUse = 0;
            return 1;
        }
    }
    return 0;
}

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

int writePNM(const char * filename,int width,int height,int channels, unsigned char * data)
{
    //fprintf(stderr,"saveRawImageToFile(%s) called\n",filename);
    if (filename==0) { return 0; }

    if(data==0) { fprintf(stderr,"saveRawImageToFile(%s) called for an unallocated (empty) frame , will not write any file output\n",filename); return 0; }

    FILE *fd=0;
    fd = fopen(filename,"wb");

    if (fd!=0)
    {
        unsigned int n;
        if (channels==3) fprintf(fd, "P6\n");
        else if (channels==1) fprintf(fd, "P5\n");
        else
        {
            fprintf(stderr,"Invalid channels arg (%u) for SaveRawImageToFile\n",channels);
            fclose(fd);
            return 1;
        }

        fprintf(fd, "%d %d\n%u\n", width, height , simplePowPPM(2,8)-1);

        n =  width * height * channels;

        //fprintf(stderr,"fwrite(pic->data, 1 , n , fd);\n");
        fwrite(data, 1 , n , fd);
        //fprintf(stderr,"survived\n");
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

int writeVideoFrameToImage(const char * filename,struct VideoFrame * pic, unsigned char * data)
{
   if (pic==0)      { return 0; }
   return writePNM(filename,pic->width,pic->height,pic->channels,data);
}

struct VideoFrameLocalMapping * allocateLocalMapping()
{
    struct VideoFrameLocalMapping * lm = (struct VideoFrameLocalMapping *) malloc(sizeof(struct VideoFrameLocalMapping));
    if (lm!=0)
    {
        memset(lm,0,sizeof(struct VideoFrameLocalMapping));
    }
    return lm;
}

int freeLocalMapping(struct VideoFrameLocalMapping * lm)
{
  if (lm!=0)
  {
      free(lm);
      return 1;
  }
  return 0;
}

unsigned char * getLocalMappingPointer(struct VideoFrameLocalMapping * lm,unsigned int item)
{
  if (lm!=0)
  {
     if ((lm->smc!=0) && (item<lm->smc->numberOfBuffers) )
     {
      struct VideoFrame *frame = &lm->smc->buffer[item];
      if (frame->bufferCount <= 1)
      {
          return (unsigned char *) lm->data[item];
      }

      unsigned int index;
      if (!tlsReadIndexLookup(frame,&index)) { index = frame->latestIndex; }
      return (unsigned char *) lm->data[item] + ((size_t) index * frame->frame_size);
     }
  }
  return 0;
}


int mapRemoteToLocal(struct SharedMemoryContext *context, struct VideoFrameLocalMapping * localMap,unsigned int item)
{
  if (context!=0)
  {
    if (localMap!=0)
    {
     if (item<context->numberOfBuffers)
     {
      localMap->smc = context;
      struct VideoFrame *frame = &context->buffer[item];
      if (frame!=0)
      {
       if (localMap->data[item]==0)
                {
                  //Only do the local mapping if we haven't already
                  localMap->data[item] = map_frame_shared_memory(frame,0);
                  localMap->sz[item]   = frame->frame_size * frame->bufferCount;
                  return 1;
                } else
                {
                  fprintf(stderr,"Item %u was already mapped\n",item);
                  return 1;
                }

      } // there is a frame to map
    } //item number is valid
   } //local map exists
  } //context exists

  return 0;
}


int unmapLocalMappingItem(struct VideoFrameLocalMapping * localmap,unsigned int item)
{
 if (localmap!=0)
  {
   if ((localmap->smc!=0) && (item<localmap->smc->numberOfBuffers) )
     {
      if ( (localmap->data[item]!=0) && (localmap->sz[item]!=0) )
       {
        fprintf(stderr,"Unmapping memory for item %u\n",item);
        munmap(localmap->data[item],localmap->sz[item]);
        localmap->sz[item]   = 0;
        localmap->data[item] = 0;
        return 1;
       }
     }
  }
 return 0;
}

int resolveFeedNameToID(struct SharedMemoryContext * smvc, const char *feedName)
{
    if (smvc==0) { return -1; }

    for (unsigned int i=0; i<smvc->numberOfBuffers; i++)
    {
        if (strncmp(smvc->buffer[i].name, feedName, sizeof(smvc->buffer[i].name)) == 0)
        {
            return i;
        }
    }
    return -1;
}



// Auto-timestamp for writers that pass 0: MICROSECONDS since the Unix epoch.
// time(NULL) only advances once a second, so every frame published inside the
// same second carried an identical timestamp and any consumer using it as frame
// identity (e.g. a "skip what I already processed" limiter) throttled to 1 Hz.
static unsigned long getUnixTimestampMicroseconds()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME,&ts) != 0)
        {
            return (unsigned long) time(NULL) * 1000000;
        }
    return ((unsigned long) ts.tv_sec * 1000000) + ((unsigned long) ts.tv_nsec / 1000);
}

// Function to copy data from a buffer to the shared memory buffer
void copy_to_shared_memory(struct VideoFrame *frame, const void* src, size_t n, unsigned long unix_timestamp)
{
  if ( (frame!=0) && (src!=0) && (n!=0) )
    {
        if (frame->client_address_space_data_pointer!=0)
        {
           if (frame->frame_size >= n)
           {
             //fprintf(stderr,"Will copy %lu bytes to stream %s, pointing @ %p\n",n,frame->name,frame->client_address_space_data_pointer);
             memcpy(frame->client_address_space_data_pointer,src, n);
             // Stamped per-slot (not a single shared field) so a reader holding
             // an older slot never sees a timestamp that belongs to a newer,
             // not-yet-visible-to-them frame.
             frame->timestamps[frame->writeIndex] = (unix_timestamp != 0) ? unix_timestamp : getUnixTimestampMicroseconds();
           } else { fprintf(stderr,"copy_to_shared_memory: Will not overflow target \n"); }
        } else { fprintf(stderr,"copy_to_shared_memory: No client address space data pointer \n"); }
    } else { fprintf(stderr,"copy_to_shared_memory: No Target VideoFrame our valid source \n"); }
}

int getSharedMemoryContextMAXBuffers()
{
  return MAX_NUMBER_OF_BUFFERS;
}

int getSharedMemoryContextNumberOfBuffers(struct SharedMemoryContext *context)
{
  if (context!=0)
  {
     return context->numberOfBuffers;
  }
  return 0;
}

struct VideoFrame * getSharedMemoryContextVideoFrame(struct SharedMemoryContext *context, unsigned int item)
{
  if (context!=0)
  {
    if ( context->numberOfBuffers > item)
    {
      return &context->buffer[item];
    }
  }
  return 0;
}

int remoteSharedMemoryContextVideoFrameIsPopulated(struct SharedMemoryContext *context, unsigned int item)
{
  if (context!=0)
  {
    if ( context->numberOfBuffers > item)
    {
     return context->buffer[item].is_populated;
    }
  }
  return 0;
}

// Runtime-gated so callers that poll this every frame don't pay for fprintf
// unless the caller opted in. DEBUG_MESSAGES is a compile-time switch and
// this function is called far too often (every frame, from several example
// binaries and from the Python wrapper) to compile it in unconditionally.
static int verboseEnabled()
{
    static int cached = -1;
    if (cached == -1)
    {
        const char * env = getenv("SHMVB_VERBOSE");
        cached = (env != NULL) && (strcmp(env,"1")==0 || strcmp(env,"true")==0);
    }
    return cached;
}

void printSharedMemoryContextState(struct SharedMemoryContext *context)
{
  if (!verboseEnabled()) { return; }
  if (context==0) { fprintf(stderr,"Empty Context\n"); return; }
  fprintf(stderr,"Populated Streams : %u\n",context->numberOfBuffers);
  for (int i=0; i<MAX_NUMBER_OF_BUFFERS; i++)
     {
         fprintf(stderr,"Bank %u : %ux%u:%u @ %p\n",i,context->buffer[i].width,context->buffer[i].height,context->buffer[i].channels,context->buffer[i].client_address_space_data_pointer);
     }
}

// Create and open shared memory context descriptor
int createSharedMemoryContextDescriptor(const char *path)
{
    int shm_fd = shm_open(path, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        fprintf(stderr,RED "shm_open\n" NORMAL);
        //perror("shm_open");
        return -1;
    }

    size_t total_size = sizeof(struct SharedMemoryContext);
    if (ftruncate(shm_fd, total_size) == -1)
    {
        fprintf(stderr,RED "ftruncate\n" NORMAL);
        //perror("ftruncate");
        close(shm_fd);
        return -1;
    }

    struct SharedMemoryContext *context = (struct SharedMemoryContext*) mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (context == MAP_FAILED)
    {
        fprintf(stderr,RED "mmap\n" NORMAL);
        //perror("mmap");
        close(shm_fd);
        return -1;
    }

    // Initialize the shared memory context
    memset(context, 0, total_size);
    printSharedMemoryContextState(context);
    munmap(context, total_size);
    close(shm_fd);
    return 0;
}


// Create shared memory for a video frame
int create_frame_shared_memory(struct VideoFrame *frame)
{
    if (frame==0) {return -1; }

    frame->bufferCount = getConfiguredBufferCount();
    frame->writeIndex  = 0;
    frame->latestIndex = 0;
    for (unsigned int i=0; i<MAX_LOCAL_BUFFERS; i++)
    {
        frame->readerCount[i] = 0;
        frame->timestamps[i]  = 0;
    }

    size_t totalSize = frame->frame_size * frame->bufferCount;

    fprintf(stderr,"Creating new video frame shared memory for %s (%u slot(s))\n",frame->name,frame->bufferCount);
    int shm_fd = shm_open(frame->name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        fprintf(stderr,RED "shm_open frame\n" NORMAL);
        //perror("shm_open frame");
        return -1;
    }

    if (ftruncate(shm_fd, totalSize) == -1)
    {
        fprintf(stderr,RED "ftruncate frame\n" NORMAL);
        //perror("ftruncate frame");
        close(shm_fd);
        return -1;
    }

    fprintf(stderr,"MMAP shared memory for %s , size %lu\n",frame->name,totalSize);
    frame->mmap_base_pointer = (unsigned char*) mmap(NULL, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    frame->client_address_space_data_pointer = frame->mmap_base_pointer;
    fprintf(stderr,"MMAP pointer for %s @ %p\n",frame->name,frame->client_address_space_data_pointer);
    if (frame->client_address_space_data_pointer == MAP_FAILED)
    {
        fprintf(stderr,RED "mmap frame\n" NORMAL);
        perror("mmap frame");
        close(shm_fd);
        return -1;
    }

    frame->locked = 0;       // Initialize the lock to 0
    frame->is_populated = 1; // Mark frame as populated
    close(shm_fd);
    return 0;
}

// Map existing shared memory for a video frame
unsigned char * map_frame_shared_memory(struct VideoFrame *frame,int copyToVideoFramePointer)
{
    unsigned char * result = NULL;
    if (frame==0) { fprintf(stderr,"error: map_frame_shared_memory called without a valid video frame!\n"); return NULL; }

    // frame->bufferCount was set by whichever process created this stream and
    // lives in shared memory, so it's already correct here - map the WHOLE
    // multi-slot region, not just one slot's worth.
    size_t totalSize = frame->frame_size * frame->bufferCount;
    fprintf(stderr,"MMAP shared memory for %s , size %lu\n",frame->name,totalSize);

    int shm_fd = shm_open(frame->name, O_RDWR, 0666);
    if (shm_fd  == -1)
    {
        fprintf(stderr,RED "shm_open frame\n" NORMAL);
        //perror("shm_open frame");
        return NULL;
    }

    result = (unsigned char*) mmap(NULL, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd , 0); //
    if (result  == MAP_FAILED)
    {
        fprintf(stderr,RED "mmap frame\n" NORMAL);
        //perror("mmap frame");
        close(shm_fd);
        return NULL;
    }

    if (copyToVideoFramePointer)
    {
        frame->mmap_base_pointer = result;
        frame->client_address_space_data_pointer = result;
    }

    fprintf(stderr,"MMAP shared memory now points at %p\n",result);
    close(shm_fd);
    return result;
}

// Get a pointer to a video buffer by feed name
struct VideoFrame* getVideoBufferPointer(struct SharedMemoryContext * smvc, const char *feedName)
{
    if (smvc==0)     {return NULL; }
    if (feedName==0) {return NULL; }

    for (unsigned int i = 0; i < smvc->numberOfBuffers; i++)
    {
        if (strncmp(smvc->buffer[i].name, feedName, sizeof(smvc->buffer[i].name)) == 0)
        {
            return &smvc->buffer[i];
        }
    }
    return NULL;
}
//------------------------------------------------------------
//------------------------------------------------------------
//------------------------------------------------------------
unsigned char * getVideoFrameDataPointer(struct VideoFrame * frame)
{
  if (frame)
  {
    if (frame->bufferCount <= 1)
    {
        return frame->client_address_space_data_pointer;
    }

    // Resolve to whichever slot *this thread* latched onto via a matching
    // startReadingFromVideoBufferPointer() call; if none is on record (misuse,
    // or the table overflowed - see MAX_TLS_READ_ENTRIES) fall back to the
    // current published slot, which is still always a complete frame.
    unsigned int index;
    if (!tlsReadIndexLookup(frame,&index)) { index = frame->latestIndex; }
    return frame->mmap_base_pointer + ((size_t) index * frame->frame_size);
  }
  return 0;
}

unsigned long getVideoFrameDataSize(struct VideoFrame * frame)
{
  if (frame)
  {
    return frame->frame_size;
  }
  return 0;
}

unsigned int getVideoFrameWidth(struct VideoFrame * frame)
{
  if (frame)
  {
    return frame->width;
  }
  return 0;
}

unsigned int getVideoFrameHeight(struct VideoFrame * frame)
{
  if (frame)
  {
    return frame->height;
  }
  return 0;
}

unsigned int getVideoFrameChannels(struct VideoFrame * frame)
{
  if (frame)
  {
    return frame->channels;
  }
  return 0;
}

unsigned long getVideoFrameTimestamp(struct VideoFrame * frame)
{
  if (frame)
  {
    if (frame->bufferCount <= 1) { return frame->timestamps[0]; }

    unsigned int index;
    if (!tlsReadIndexLookup(frame,&index)) { index = frame->latestIndex; }
    return frame->timestamps[index];
  }
  return 0;
}

void setVideoFrameTimestamp(struct VideoFrame * frame, unsigned long unix_timestamp)
{
  if (frame)
  {
    // Only meaningful while a write is in flight (between start/stopWriting),
    // matching how copy_to_shared_memory stamps the slot it just wrote.
    frame->timestamps[frame->writeIndex] = (unix_timestamp != 0) ? unix_timestamp : getUnixTimestampMicroseconds();
  }
}





//------------------------------------------------------------
//------------------------------------------------------------
//------------------------------------------------------------
int createVideoFrameMetaData(struct SharedMemoryContext* context,const char * streamName,unsigned int width, unsigned int height, unsigned int channels)
{
   if (context!=0)
   {
      int contextID = -1;
      for (unsigned int i=0; i<context->numberOfBuffers; i++)
      {
         if (strncmp(streamName,context->buffer[i].name,MAX_SHM_NAME)==0)
         {
           fprintf(stderr,"Stream already exists\n");
           contextID = i;
         }
      }

      if (contextID==-1)
      {
         if (context->numberOfBuffers >= MAX_NUMBER_OF_BUFFERS)
         {
             fprintf(stderr,"createVideoFrameMetaData: maximum number of buffers (%u) reached\n", MAX_NUMBER_OF_BUFFERS);
             return EXIT_FAILURE;
         }
         fprintf(stderr,"Creating new stream %s\n",streamName);
         contextID = context->numberOfBuffers++;
      }
    // Example to add a new buffer (Server)
    struct VideoFrame *newBuffer = &context->buffer[contextID];
    snprintf(newBuffer->name,MAX_SHM_NAME,"%s",streamName);
    newBuffer->width      = width;
    newBuffer->height     = height;
    newBuffer->channels   = channels;

    // Check for multiplication overflow before computing frame_size
    if (newBuffer->height != 0 && newBuffer->width > (SIZE_MAX / newBuffer->height))
    {
        fprintf(stderr,"createVideoFrameMetaData: width*height would overflow\n");
        return EXIT_FAILURE;
    }
    size_t wh = (size_t)newBuffer->width * newBuffer->height;
    if (newBuffer->channels != 0 && wh > (SIZE_MAX / newBuffer->channels))
    {
        fprintf(stderr,"createVideoFrameMetaData: width*height*channels would overflow\n");
        return EXIT_FAILURE;
    }
    newBuffer->frame_size = wh * newBuffer->channels;

    if (create_frame_shared_memory(newBuffer) == 0)
    {
     return EXIT_SUCCESS;
    }

   }
   return EXIT_FAILURE;
}



//------------------------------------------------------------
//------------------------------------------------------------
//------------------------------------------------------------
int createGenericMetaData(struct SharedMemoryContext* context,const char * streamName,unsigned int dataSize)
{
   if (context!=0)
   {
      int contextID = -1;
      for (unsigned int i=0; i<context->numberOfBuffers; i++)
      {
         if (strncmp(streamName,context->buffer[i].name,MAX_SHM_NAME)==0)
         {
           fprintf(stderr,"Stream already exists\n");
           contextID = i;
         }
      }

      if (contextID==-1)
      {
         if (context->numberOfBuffers >= MAX_NUMBER_OF_BUFFERS)
         {
             fprintf(stderr,"createGenericMetaData: maximum number of buffers (%u) reached\n", MAX_NUMBER_OF_BUFFERS);
             return EXIT_FAILURE;
         }
         fprintf(stderr,"Creating new stream %s\n",streamName);
         contextID = context->numberOfBuffers++;
      }
    // Example to add a new buffer (Server)
    struct VideoFrame *newBuffer = &context->buffer[contextID];
    snprintf(newBuffer->name,MAX_SHM_NAME,"%s",streamName);
    newBuffer->width      = dataSize;
    newBuffer->height     = 1;
    newBuffer->channels   = 1;
    newBuffer->frame_size = dataSize;

    if (create_frame_shared_memory(newBuffer) == 0)
    {
     return EXIT_SUCCESS;
    }

   }
   return EXIT_FAILURE;
}



// Destroy a video frame and its shared memory
int destroyVideoFrame(struct SharedMemoryContext* context, const char *streamName)
{
    if (context == NULL || streamName == NULL) { return EXIT_FAILURE; }

    int index = -1;
    for (unsigned int i = 0; i < context->numberOfBuffers; i++)
    {
        if (strncmp(context->buffer[i].name, streamName, MAX_SHM_NAME) == 0)
        {
            index = i;
            break;
        }
    }

    if (index == -1)
    {
        fprintf(stderr, "Stream %s not found\n", streamName);
        return EXIT_FAILURE;
    }

    struct VideoFrame *frame = &context->buffer[index];

    if (frame->mmap_base_pointer != NULL)
    {
        // Unmap the shared memory (the full bufferCount*frame_size mapping,
        // not just one slot - client_address_space_data_pointer may currently
        // point at a slot other than the base after prior writes)
        if (munmap(frame->mmap_base_pointer, frame->frame_size * frame->bufferCount) == -1)
        {
            debug_message("munmap frame");
            return EXIT_FAILURE;
        }
        frame->mmap_base_pointer = NULL;
        frame->client_address_space_data_pointer = NULL;
    }

    // Remove the shared memory object
    if (shm_unlink(frame->name) == -1)
    {
        debug_message("shm_unlink frame");
        return EXIT_FAILURE;
    }

    // Shift remaining buffers to fill the gap
    for (unsigned int i = index; i < context->numberOfBuffers - 1; i++)
    {
        context->buffer[i] = context->buffer[i + 1];
    }

    context->numberOfBuffers--;
    fprintf(stderr, "Stream %s destroyed\n", streamName);

    // Zero out the now-unused last slot (not 'frame', which was shifted over)
    fprintf(stderr, "Final cleanup of %s\n",streamName);
    memset(&context->buffer[context->numberOfBuffers], 0, sizeof(struct VideoFrame));
    fprintf(stderr, "Done with %s\n",streamName);


    return EXIT_SUCCESS;
}

// Connect to existing shared memory context descriptor
struct SharedMemoryContext* connectToSharedMemoryContextDescriptor(const char *path)
{
    setup_signal_handlers();

    int shm_fd = shm_open(path, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        debug_message(RED "shm_open frame\n" NORMAL);
        //perror("shm_open");
        return NULL;
    }

    size_t total_size = sizeof(struct SharedMemoryContext);
    struct SharedMemoryContext *context = (struct SharedMemoryContext*) mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (context == MAP_FAILED)
    {
        debug_message(RED "mmap\n" NORMAL);
        //perror("mmap");
        close(shm_fd);
        return NULL;
    }

    close(shm_fd);
    return context;
}

// Start writing to a video buffer
int startWritingToVideoBufferPointer(struct VideoFrame *vf)
{
    if (vf==0) { return 0; }

    debug_message("startWritingToVideoBufferPointer :");
    int attempts = 0;
    int result   = 0;

    while (attempts<ATTEMPTS_TO_LOCK_A_BUFFER)
    {
      if (__sync_lock_test_and_set(&vf->locked, 1))
      {
        usleep(SLEEP_TIME_BETWEEN_LOCK_ATTEMPTS_MICROSECONDS);
      } else
      {
          result = 1;
          break;
      }

      ++attempts;
    }


    if (!result)
    {
        debug_message(RED "failed\n" NORMAL);
        return 0; // Buffer is already locked and we timed out waiting for it
    }

    // Legacy single-buffer mode: client_address_space_data_pointer already
    // points at the one and only slot, nothing else to do.
    if (vf->bufferCount <= 1)
    {
        debug_message(GREEN "success\n" NORMAL);
        return 1;
    }

    // Multi-buffering: claim a slot that isn't the currently-published one and
    // has no active readers, so this write can never clobber a frame a reader
    // is still copying out. `locked` (held for the remainder of this write)
    // already serializes this search against any other writer, so a plain
    // read-then-write of readerCount here is safe - no reader ever targets a
    // slot other than the live vf->latestIndex, and that can't change while we
    // hold the writer lock.
    unsigned int chosen = vf->bufferCount; // sentinel: "not found yet"
    attempts = 0;
    while (attempts<ATTEMPTS_TO_LOCK_A_BUFFER)
    {
        for (unsigned int k=0; k<vf->bufferCount; k++)
        {
            unsigned int candidate = (vf->latestIndex + 1 + k) % vf->bufferCount;
            if (candidate == vf->latestIndex) { continue; }
            if (__sync_fetch_and_add(&vf->readerCount[candidate], 0) == 0)
            {
                chosen = candidate;
                break;
            }
        }
        if (chosen != vf->bufferCount) { break; }
        usleep(SLEEP_TIME_BETWEEN_LOCK_ATTEMPTS_MICROSECONDS);
        ++attempts;
    }

    if (chosen == vf->bufferCount)
    {
        // Every non-latest slot has a lingering reader - extremely unlikely at
        // video framerates. Give up without touching any data (frame dropped,
        // never corrupted) and release the writer lock we're holding.
        __sync_lock_release(&vf->locked);
        debug_message(RED "failed\n" NORMAL);
        return 0;
    }

    vf->writeIndex = chosen;
    vf->client_address_space_data_pointer = vf->mmap_base_pointer + ((size_t) chosen * vf->frame_size);

    debug_message(GREEN "success\n" NORMAL);
    return 1; // We have locked the buffer
}

// Stop writing to a video buffer
int stopWritingToVideoBufferPointer(struct VideoFrame *vf)
{
    if (vf==0) { return 0; }
    debug_message("stopWritingToVideoBufferPointer :");

    if (vf->bufferCount > 1)
    {
        // Publish: make the just-written slot the one readers will latch onto.
        // The barrier ensures the memcpy done under copy_to_shared_memory is
        // visible to any thread that observes the new latestIndex.
        __sync_synchronize();
        vf->latestIndex = vf->writeIndex;
        __sync_synchronize();
    }

    __sync_lock_release(&vf->locked);
    debug_message(GREEN "success\n" NORMAL);
    return 1;
}

// Start reading from a video buffer.
// Legacy (bufferCount==1) mode: readers acquire no real lock, they only check
// that no writer is currently active - this is the original design and still
// carries the original torn-read risk if a writer starts mid-read.
// Multi-buffered mode: the reader latches onto the current "latest" slot and
// registers itself in readerCount[] for it, so the writer (which always skips
// slots with readerCount>0) can never overwrite the data being read. The
// load-refcount-recheck sequence below closes the narrow window where the
// published slot changes between reading it and registering interest in it.
int startReadingFromVideoBufferPointer(struct VideoFrame *vf)
{
    if (vf==0) { return 0; }
    debug_message("startReadingFromVideoBufferPointer :");

    if (vf->bufferCount <= 1)
    {
        if (__sync_fetch_and_add(&vf->locked, 0))
        {
            debug_message(RED "failed\n" NORMAL);
            return 0; // Buffer is locked by a writer
        }
        debug_message(GREEN "success\n" NORMAL);
        return 1;
    }

    unsigned int idx;
    for (;;)
    {
        idx = vf->latestIndex;
        __sync_fetch_and_add(&vf->readerCount[idx], 1);
        if (vf->latestIndex == idx) { break; } // still current - we're protected
        __sync_fetch_and_sub(&vf->readerCount[idx], 1); // stale, a publish raced us - retry
    }

    tlsReadIndexStore(vf, idx);
    debug_message(GREEN "success\n" NORMAL);
    return 1;
}

// Stop reading from a video buffer. Legacy mode: no-op, matching the original
// design (readers never held anything). Multi-buffered mode: releases the
// refcount claimed by the matching startReadingFromVideoBufferPointer() call
// on this thread.
int stopReadingFromVideoBufferPointer(struct VideoFrame *vf)
{
    if (vf==0) { return 0; }
    if (vf->bufferCount > 1)
    {
        unsigned int idx;
        if (tlsReadIndexLookupAndClear(vf,&idx))
        {
            __sync_fetch_and_sub(&vf->readerCount[idx], 1);
        }
    }
    return 1;
}
