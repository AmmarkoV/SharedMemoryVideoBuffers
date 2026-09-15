/** @file sharedMemoryVideoBuffers.c
 *  @brief  A shared memory wrapper to make processing video streams from multiple processes easier.
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

#include "sharedMemoryVideoBuffers.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <time.h>
#include <pthread.h>

#define NORMAL   "\033[0m"
#define BLACK   "\033[30m"      /* Black */
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */

#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include <stdarg.h>

static void debug_message(const char *format, ...)
{
    #if DEBUG_MESSAGES
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    #else
    (void) format;
    #endif // DEBUG_MESSAGES
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

// A PID that no longer exists. ESRCH: no such process. EPERM means it exists
// but belongs to another user, so it counts as alive.
static int processIsDead(pid_t pid)
{
    return (pid > 0) && (kill(pid,0) == -1) && (errno == ESRCH);
}

// ---------------------------------------------------------------------------
// Process-local mappings of stream pixels. Shared memory is mapped at a
// different address in every process, so where *this* process mapped a stream
// is kept here - keyed by the VideoFrame's address in this process - and never
// in the shared VideoFrame itself. A mapping belongs to one generation of the
// stream: once the stream is destroyed and re-created, the next read or write
// maps the new backing object, and the old mapping is retired and unmapped as
// soon as no read or write in this process still uses it.
// ---------------------------------------------------------------------------
#define MAX_LOCAL_STREAM_MAPPINGS 64

struct localStreamMapping
{
    const struct VideoFrame *vf;  //<- shared slot this maps, at its address in this process
    uint64_t generation;          //<- incarnation of the stream `base` maps
    unsigned char *base;          //<- bufferCount * frameSize bytes
    size_t size;
    size_t frameSize;
    unsigned int bufferCount;
    int activeUses;               //<- reads/writes in progress in this process that use `base`
    int retired;                  //<- superseded or released: unmap once activeUses reaches 0
    int inUse;
};

static struct localStreamMapping localMappings[MAX_LOCAL_STREAM_MAPPINGS];
static pthread_mutex_t localMappingsLock = PTHREAD_MUTEX_INITIALIZER;

// Caller holds localMappingsLock.
static void retireMappingLocked(struct localStreamMapping *mapping)
{
    mapping->retired = 1;
    if (mapping->activeUses == 0)
    {
        munmap(mapping->base, mapping->size);
        memset(mapping, 0, sizeof(struct localStreamMapping));
    }
}

// Caller holds localMappingsLock. Returns this process's mapping of the stream
// currently in vf's slot, mapping it first if needed, or NULL if the slot holds
// no complete stream.
static struct localStreamMapping * currentMappingLocked(const struct VideoFrame *vf)
{
    struct localStreamMapping *current = NULL;
    for (int i=0; i<MAX_LOCAL_STREAM_MAPPINGS; i++)
    {
        if (localMappings[i].inUse && !localMappings[i].retired && localMappings[i].vf==vf) { current = &localMappings[i]; break; }
    }
    if ((current != NULL) && vf->is_populated && (current->generation == vf->generation)) { return current; }
    if (current != NULL) { retireMappingLocked(current); } // the stream was destroyed or re-created

    // Snapshot the stream's layout. Registration marks a slot unpopulated, then
    // changes its generation, then its other fields, then marks it populated -
    // so a slot that is populated and keeps its generation while we copy is stable.
    char backingName[MAX_SHM_NAME+1];
    uint64_t generation;
    size_t frameSize;
    unsigned int bufferCount;
    for (int attempts=0; ; attempts++)
    {
        generation = vf->generation;
        int populated = vf->is_populated;
        memcpy(backingName, vf->backingName, sizeof(backingName));
        backingName[MAX_SHM_NAME] = 0;
        frameSize   = vf->frame_size;
        bufferCount = vf->bufferCount;
        if (!populated) { return NULL; }
        if (vf->generation == generation) { break; }
        if (attempts >= ATTEMPTS_TO_LOCK_A_BUFFER) { return NULL; }
    }
    if ((bufferCount == 0) || (bufferCount > MAX_LOCAL_BUFFERS) || (frameSize == 0) || (frameSize > SIZE_MAX / bufferCount)) { return NULL; }
    size_t size = frameSize * bufferCount;

    struct localStreamMapping *freeEntry = NULL;
    for (int i=0; i<MAX_LOCAL_STREAM_MAPPINGS; i++)
    {
        if (!localMappings[i].inUse) { freeEntry = &localMappings[i]; break; }
    }
    if (freeEntry == NULL)
    {
        fprintf(stderr,"Can't map stream %s: this process already maps %u streams\n",vf->name,MAX_LOCAL_STREAM_MAPPINGS);
        return NULL;
    }

    int shm_fd = shm_open(backingName, O_RDWR, 0666);
    if (shm_fd == -1) { return NULL; } // destroyed since the snapshot

    // Never map more than the object holds: touching memory past its end is SIGBUS
    struct stat objectStat;
    if ((fstat(shm_fd, &objectStat) == -1) || ((size_t) objectStat.st_size < size)) { close(shm_fd); return NULL; }

    unsigned char *base = (unsigned char*) mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    close(shm_fd);
    if (base == MAP_FAILED)
    {
        fprintf(stderr,RED "mmap frame %s\n" NORMAL,backingName);
        return NULL;
    }

    freeEntry->vf          = vf;
    freeEntry->generation  = generation;
    freeEntry->base        = base;
    freeEntry->size        = size;
    freeEntry->frameSize   = frameSize;
    freeEntry->bufferCount = bufferCount;
    freeEntry->activeUses  = 0;
    freeEntry->retired     = 0;
    freeEntry->inUse       = 1;
    return freeEntry;
}

// Mapping for a read or write: stays mapped until the matching endMappingUse().
static struct localStreamMapping * beginMappingUse(const struct VideoFrame *vf)
{
    pthread_mutex_lock(&localMappingsLock);
    struct localStreamMapping *mapping = currentMappingLocked(vf);
    if (mapping != NULL) { mapping->activeUses++; }
    pthread_mutex_unlock(&localMappingsLock);
    return mapping;
}

static void endMappingUse(struct localStreamMapping *mapping)
{
    pthread_mutex_lock(&localMappingsLock);
    mapping->activeUses--;
    if (mapping->retired) { retireMappingLocked(mapping); }
    pthread_mutex_unlock(&localMappingsLock);
}

// Mapping for callers outside a read or write. Unprotected: another thread may
// retire it right after this returns.
static unsigned char * currentMappingBase(const struct VideoFrame *vf, size_t *size, size_t *frameSize, unsigned int *bufferCount)
{
    unsigned char *base = NULL;
    pthread_mutex_lock(&localMappingsLock);
    struct localStreamMapping *mapping = currentMappingLocked(vf);
    if (mapping != NULL)
    {
        base = mapping->base;
        if (size != NULL)        { *size        = mapping->size; }
        if (frameSize != NULL)   { *frameSize   = mapping->frameSize; }
        if (bufferCount != NULL) { *bufferCount = mapping->bufferCount; }
    }
    pthread_mutex_unlock(&localMappingsLock);
    return base;
}

// Unmaps this process's mapping of vf's slot as soon as nothing here uses it.
static void releaseMapping(const struct VideoFrame *vf)
{
    pthread_mutex_lock(&localMappingsLock);
    for (int i=0; i<MAX_LOCAL_STREAM_MAPPINGS; i++)
    {
        if (localMappings[i].inUse && !localMappings[i].retired && localMappings[i].vf==vf) { retireMappingLocked(&localMappings[i]); break; }
    }
    pthread_mutex_unlock(&localMappingsLock);
}

// ---------------------------------------------------------------------------
// Per-thread bookkeeping of "which slot did *this* thread most recently latch
// onto for VideoFrame X via startReadingFromVideoBufferPointer()". This can't
// live in the shared VideoFrame struct itself: two reader processes (or two
// threads) can legitimately be looking at two different slots at once, and
// pointers/state written into shared memory by one process are meaningless to
// another anyway. A small thread-local table keyed by the VideoFrame's address
// gives every start/stop pair its own private slot index for free, with zero
// change to any caller's code.
// ---------------------------------------------------------------------------
#define MAX_TLS_READ_ENTRIES 16

struct tlsReadEntry
{
    const struct VideoFrame *vf;
    struct localStreamMapping *mapping; //<- this process's mapping the read uses, kept mapped until stop
    unsigned int index;        //<- slot this thread latched onto
    int registered;            //<- readerEntry/registration hold a registration (multi-buffered streams only)
    unsigned int readerEntry;  //<- which vf->readers[] entry holds this read's registration
    uint64_t registration;     //<- value stored in that entry, so stop only ever releases its own
    int inUse;
};

static __thread struct tlsReadEntry tlsReadTable[MAX_TLS_READ_ENTRIES];

static struct tlsReadEntry * tlsReadFind(const struct VideoFrame *vf)
{
    for (int i=0; i<MAX_TLS_READ_ENTRIES; i++)
    {
        if (tlsReadTable[i].inUse && tlsReadTable[i].vf==vf) { return &tlsReadTable[i]; }
    }
    return NULL;
}

static struct tlsReadEntry * tlsReadFindFree()
{
    for (int i=0; i<MAX_TLS_READ_ENTRIES; i++)
    {
        if (!tlsReadTable[i].inUse) { return &tlsReadTable[i]; }
    }
    return NULL;
}

// Same idea for writers: which frames this thread currently holds the writer
// lock on, so getVideoFrameDataPointer() can hand a writer the slot it claimed
// and stopWritingToVideoBufferPointer() can skip publishing a slot whose
// copy_to_shared_memory() was rejected.
#define MAX_TLS_WRITE_ENTRIES 16

struct tlsWriteEntry
{
    const struct VideoFrame *vf;
    struct localStreamMapping *mapping; //<- this process's mapping the write uses, kept mapped until stop
    int aborted; //<- last copy_to_shared_memory() of this write was rejected
    int inUse;
};

static __thread struct tlsWriteEntry tlsWriteTable[MAX_TLS_WRITE_ENTRIES];

static struct tlsWriteEntry * tlsWriteFind(const struct VideoFrame *vf)
{
    for (int i=0; i<MAX_TLS_WRITE_ENTRIES; i++)
    {
        if (tlsWriteTable[i].inUse && tlsWriteTable[i].vf==vf) { return &tlsWriteTable[i]; }
    }
    return NULL;
}

static struct tlsWriteEntry * tlsWriteFindFree()
{
    for (int i=0; i<MAX_TLS_WRITE_ENTRIES; i++)
    {
        if (!tlsWriteTable[i].inUse) { return &tlsWriteTable[i]; }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Reader registrations (VideoFrame.readers[]). Each in-progress read owns one
// entry holding (reader PID << 32) | slot, so claiming, releasing and reclaiming
// an entry are each a single atomic compare-and-swap. 0 marks a free entry (no
// process has PID 0). The PID lets the writer free entries left behind by
// readers that exited without calling stopReadingFromVideoBufferPointer().
// ---------------------------------------------------------------------------
static uint64_t packReaderRegistration(pid_t pid, unsigned int slot)
{
    return ((uint64_t) (uint32_t) pid << 32) | slot;
}

static int slotHasReaders(const struct VideoFrame *vf, unsigned int slot)
{
    for (unsigned int i=0; i<MAX_READERS_PER_STREAM; i++)
    {
        uint64_t registration = vf->readers[i];
        if ((registration != 0) && ((unsigned int) (registration & 0xFFFFFFFF) == slot)) { return 1; }
    }
    return 0;
}

// Without this, one reader killed mid-read (crash, Ctrl-C, kill -9) pins its
// slot forever and, with the default of 2 slots, the writer can never publish
// again.
static void reclaimDeadReaders(struct VideoFrame *vf)
{
    for (unsigned int i=0; i<MAX_READERS_PER_STREAM; i++)
    {
        uint64_t registration = vf->readers[i];
        if (registration == 0) { continue; }

        pid_t pid = (pid_t) (registration >> 32);
        if (processIsDead(pid))
        {
            if (__sync_bool_compare_and_swap(&vf->readers[i], registration, 0))
            {
                fprintf(stderr,"Reclaimed slot %u of stream %s from a dead reader (pid %d)\n",(unsigned int) (registration & 0xFFFFFFFF),vf->name,pid);
            }
        }
    }
}

static int claimReaderEntry(struct VideoFrame *vf, uint64_t registration)
{
    for (int pass=0; pass<2; pass++)
    {
        for (unsigned int i=0; i<MAX_READERS_PER_STREAM; i++)
        {
            if (__sync_bool_compare_and_swap(&vf->readers[i], 0, registration)) { return (int) i; }
        }
        // Table full - free entries left by dead readers and try once more
        reclaimDeadReaders(vf);
    }
    return -1;
}

static unsigned int simplePowPPM(unsigned int base,unsigned int exp)
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
      for (unsigned int item=0; item<MAX_NUMBER_OF_BUFFERS; item++)
      {
          unmapLocalMappingItem(lm,item);
      }
      free(lm);
      return 1;
  }
  return 0;
}

unsigned char * getLocalMappingPointer(struct VideoFrameLocalMapping * lm,unsigned int item)
{
  if ( (lm!=0) && (lm->smc!=0) && (item<MAX_NUMBER_OF_BUFFERS) && (lm->data[item]!=0) )
  {
      // lm->data can be stale after the stream was re-created; the process-wide
      // mapping (and the slot this thread is reading) is always current
      return getVideoFrameDataPointer(&lm->smc->buffer[item]);
  }
  return 0;
}


int mapRemoteToLocal(struct SharedMemoryContext *context, struct VideoFrameLocalMapping * localMap,unsigned int item)
{
  if ( (context==0) || (localMap==0) || (item>=MAX_NUMBER_OF_BUFFERS) ) { return 0; }

  localMap->smc = context;
  size_t size = 0;
  unsigned char *base = currentMappingBase(&context->buffer[item], &size, NULL, NULL);
  localMap->data[item] = base;
  localMap->sz[item]   = (base != NULL) ? size : 0;
  return (base != NULL);
}


int unmapLocalMappingItem(struct VideoFrameLocalMapping * localmap,unsigned int item)
{
 if ( (localmap==0) || (localmap->smc==0) || (item>=MAX_NUMBER_OF_BUFFERS) || (localmap->data[item]==0) ) { return 0; }

 fprintf(stderr,"Unmapping memory for item %u\n",item);
 // Unmapped right away, unless a read or write in this process still uses it
 releaseMapping(&localmap->smc->buffer[item]);
 localmap->sz[item]   = 0;
 localmap->data[item] = 0;
 return 1;
}

// Slot index of the stream named streamName, or -1.
static int findStreamSlot(const struct SharedMemoryContext *context, const char *streamName)
{
    for (unsigned int i=0; i<MAX_NUMBER_OF_BUFFERS; i++)
    {
        if (context->buffer[i].is_populated && (strncmp(context->buffer[i].name, streamName, sizeof(context->buffer[i].name)) == 0))
        {
            return (int) i;
        }
    }
    return -1;
}

int resolveFeedNameToID(struct SharedMemoryContext * smvc, const char *feedName)
{
    if ((smvc==0) || (feedName==0)) { return -1; }
    return findStreamSlot(smvc, feedName);
}



// Auto-timestamp for writers that pass 0: MICROSECONDS since the Unix epoch.
// time(NULL) only advances once a second, so every frame published inside the
// same second carried an identical timestamp and any consumer using it as frame
// identity (e.g. a "skip what I already processed" limiter) throttled to 1 Hz.
static uint64_t getUnixTimestampMicroseconds()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME,&ts) != 0)
        {
            return (uint64_t) time(NULL) * 1000000;
        }
    return ((uint64_t) ts.tv_sec * 1000000) + ((uint64_t) ts.tv_nsec / 1000);
}

// Function to copy data from a buffer to the shared memory buffer
int copy_to_shared_memory(struct VideoFrame *frame, const void* src, size_t n, uint64_t unix_timestamp)
{
  struct tlsWriteEntry *writeRecord = tlsWriteFind(frame);
  if ( (frame!=0) && (src!=0) && (n!=0) )
    {
        // Inside start/stopWritingToVideoBufferPointer() this is the claimed slot
        unsigned char *target = getVideoFrameDataPointer(frame);
        size_t capacity = (writeRecord != NULL) ? writeRecord->mapping->frameSize : frame->frame_size;
        if (target!=0)
        {
           if (capacity >= n)
           {
             //fprintf(stderr,"Will copy %lu bytes to stream %s, pointing @ %p\n",n,frame->name,target);
             memcpy(target,src, n);
             // Stamped per-slot (not a single shared field) so a reader holding
             // an older slot never sees a timestamp that belongs to a newer,
             // not-yet-visible-to-them frame.
             frame->timestamps[frame->writeIndex] = (unix_timestamp != 0) ? unix_timestamp : getUnixTimestampMicroseconds();
             if (writeRecord != NULL) { writeRecord->aborted = 0; }
             return 1;
           } else { fprintf(stderr,"copy_to_shared_memory: Will not overflow target \n"); }
        } else { fprintf(stderr,"copy_to_shared_memory: Stream %s has no memory mapped \n",frame->name); }
    } else { fprintf(stderr,"copy_to_shared_memory: No Target VideoFrame our valid source \n"); }

  // The claimed slot still holds an older frame - don't let the matching
  // stopWritingToVideoBufferPointer() publish it as the latest one.
  if (writeRecord != NULL) { writeRecord->aborted = 1; }
  return 0;
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
  fprintf(stderr,"Streams are in slots below : %u\n",context->numberOfBuffers);
  for (int i=0; i<MAX_NUMBER_OF_BUFFERS; i++)
     {
         const struct VideoFrame *frame = &context->buffer[i];
         fprintf(stderr,"Bank %u : %ux%u:%u %s%s\n",i,frame->width,frame->height,frame->channels,frame->name,frame->is_populated ? "" : "(free)");
     }
}

static int contextHasThisLayout(const struct SharedMemoryContext *context)
{
    return (context->magic == SHMVB_CONTEXT_MAGIC) && (context->version == SHMVB_CONTEXT_VERSION);
}

// Maps the context behind shm_fd once it has this build's size and layout,
// waiting briefly in case its creator is still initializing it. NULL if it doesn't.
static struct SharedMemoryContext * mapCompatibleContext(int shm_fd)
{
    size_t total_size = sizeof(struct SharedMemoryContext);
    for (int attempts=0; attempts<100; attempts++)
    {
        struct stat objectStat;
        if ((fstat(shm_fd, &objectStat) == 0) && ((size_t) objectStat.st_size == total_size))
        {
            struct SharedMemoryContext *context = (struct SharedMemoryContext*) mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
            if (context == MAP_FAILED) { return NULL; }
            if (contextHasThisLayout(context)) { return context; }
            munmap(context, total_size);
        }
        usleep(1000);
    }
    return NULL;
}

// Create and open shared memory context descriptor
int createSharedMemoryContextDescriptor(const char *path)
{
    if ((path == NULL) || (strlen(path) > MAX_SHM_NAME))
    {
        fprintf(stderr,RED "Invalid shared memory context name\n" NORMAL);
        return -1;
    }

    int created = 1;
    int shm_fd = shm_open(path, O_CREAT | O_EXCL | O_RDWR, 0666);
    if ((shm_fd == -1) && (errno == EEXIST))
    {
        created = 0;
        shm_fd  = shm_open(path, O_RDWR, 0666);
    }
    if (shm_fd == -1)
    {
        fprintf(stderr,RED "shm_open\n" NORMAL);
        //perror("shm_open");
        return -1;
    }

    size_t total_size = sizeof(struct SharedMemoryContext);
    if (!created)
    {
        // Keep a context this build can use, streams and all - several programs
        // call this at startup and must not wipe each other's streams
        struct SharedMemoryContext *existing = mapCompatibleContext(shm_fd);
        if (existing != NULL)
        {
            munmap(existing, total_size);
            close(shm_fd);
            return 0;
        }
        fprintf(stderr,"Shared memory context %s has an incompatible layout, re-initializing it\n",path);
    }

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

    // Initialize the shared memory context. The magic number goes in last:
    // connecting processes wait for it before using anything else.
    memset(context, 0, total_size);
    snprintf(context->descriptorName, sizeof(context->descriptorName), "%s", path);
    // Seeded from the clock so backing object names never repeat, even after re-initializing
    context->nextGeneration = getUnixTimestampMicroseconds();
    context->version = SHMVB_CONTEXT_VERSION;
    context->magic   = SHMVB_CONTEXT_MAGIC;
    printSharedMemoryContextState(context);
    munmap(context, total_size);
    close(shm_fd);
    return 0;
}

// ---------------------------------------------------------------------------
// Stream registry: creating, joining, replacing and destroying streams.
// ---------------------------------------------------------------------------

// Serializes registry changes across processes. The holder's PID is stored so a
// process that died holding the lock can't jam it.
static int acquireRegistryLock(struct SharedMemoryContext *context)
{
    pid_t pid = getpid();
    for (int attempts=0; attempts<ATTEMPTS_TO_LOCK_A_BUFFER; attempts++)
    {
        if (__sync_bool_compare_and_swap(&context->registryLockPid, 0, pid)) { return 1; }
        int32_t holder = context->registryLockPid;
        if (processIsDead(holder) && __sync_bool_compare_and_swap(&context->registryLockPid, holder, pid)) { return 1; }
        usleep(SLEEP_TIME_BETWEEN_LOCK_ATTEMPTS_MICROSECONDS);
    }
    fprintf(stderr,RED "Timed out waiting to create or destroy a stream\n" NORMAL);
    return 0;
}

static void releaseRegistryLock(struct SharedMemoryContext *context)
{
    __sync_bool_compare_and_swap(&context->registryLockPid, getpid(), 0);
}

// Caller holds the registry lock.
static void trimNumberOfBuffers(struct SharedMemoryContext *context)
{
    while ((context->numberOfBuffers > 0) && (!context->buffer[context->numberOfBuffers-1].is_populated))
    {
        context->numberOfBuffers--;
    }
}

// Creates the shm object holding a stream's pixels. A leftover object with the
// same name (e.g. from before the context was re-initialized) is replaced.
static int createBackingObject(const char *backingName, size_t size)
{
    int shm_fd = shm_open(backingName, O_CREAT | O_EXCL | O_RDWR, 0666);
    if ((shm_fd == -1) && (errno == EEXIST))
    {
        shm_unlink(backingName);
        shm_fd = shm_open(backingName, O_CREAT | O_EXCL | O_RDWR, 0666);
    }
    if (shm_fd == -1)
    {
        fprintf(stderr,RED "shm_open frame %s\n" NORMAL,backingName);
        return 0;
    }
    if (ftruncate(shm_fd, size) == -1)
    {
        fprintf(stderr,RED "ftruncate frame %s\n" NORMAL,backingName);
        close(shm_fd);
        shm_unlink(backingName);
        return 0;
    }
    close(shm_fd);
    return 1;
}

// Creates streamName, joins it if it already exists with the same size, or
// replaces it if its size differs and its owner is this process or has exited.
static int registerStream(struct SharedMemoryContext* context,const char * streamName,unsigned int width, unsigned int height, unsigned int channels, size_t frameSize)
{
    if ((context==0) || (streamName==0)) { return EXIT_FAILURE; }
    if ((streamName[0]==0) || (strlen(streamName) >= MAX_SHM_NAME) || (strchr(streamName,'/') != NULL))
    {
        fprintf(stderr,"Invalid stream name\n");
        return EXIT_FAILURE;
    }
    unsigned int bufferCount = getConfiguredBufferCount();
    if ((frameSize == 0) || (frameSize > SIZE_MAX / bufferCount))
    {
        fprintf(stderr,"Invalid size for stream %s\n",streamName);
        return EXIT_FAILURE;
    }

    if (!acquireRegistryLock(context)) { return EXIT_FAILURE; }

    int slot = findStreamSlot(context, streamName);
    if (slot != -1)
    {
        struct VideoFrame *existing = &context->buffer[slot];
        pid_t owner = existing->ownerPid;
        int ownerExited = processIsDead(owner);
        if ((existing->width == width) && (existing->height == height) && (existing->channels == channels) && (existing->frame_size == frameSize))
        {
            fprintf(stderr,"Stream %s already exists, joining it\n",streamName);
            if (ownerExited)
            {
                // Its creator is gone, possibly mid-write: take the stream over
                existing->ownerPid = getpid();
                __sync_lock_release(&existing->locked);
            }
            releaseRegistryLock(context);
            return EXIT_SUCCESS;
        }
        if ((owner != getpid()) && !ownerExited)
        {
            fprintf(stderr,"Stream %s already exists with a different size, and its owner (pid %d) is still running\n",streamName,owner);
            releaseRegistryLock(context);
            return EXIT_FAILURE;
        }
    } else
    {
        for (unsigned int i=0; i<MAX_NUMBER_OF_BUFFERS; i++)
        {
            if (!context->buffer[i].is_populated) { slot = (int) i; break; }
        }
        if (slot == -1)
        {
            fprintf(stderr,"createVideoFrameMetaData: maximum number of buffers (%u) reached\n", MAX_NUMBER_OF_BUFFERS);
            releaseRegistryLock(context);
            return EXIT_FAILURE;
        }
    }

    // Name the new backing object after the context, the stream and a fresh
    // generation, so it never collides with another context's stream of the
    // same name or with the object it replaces (which may still be mapped).
    uint64_t generation = context->nextGeneration;
    const char *contextName = (context->descriptorName[0]=='/') ? context->descriptorName+1 : context->descriptorName;
    char backingName[MAX_SHM_NAME+1];
    int nameLength = snprintf(backingName, sizeof(backingName), "/%s.%s.%llu", contextName, streamName, (unsigned long long) generation);
    if ((nameLength < 0) || (nameLength >= NAME_MAX))
    {
        fprintf(stderr,"Context and stream names are too long for stream %s\n",streamName);
        releaseRegistryLock(context);
        return EXIT_FAILURE;
    }

    struct VideoFrame *frame = &context->buffer[slot];
    if (frame->is_populated)
    {
        fprintf(stderr,"Replacing stream %s with a %ux%u:%u one\n",streamName,width,height,channels);
        frame->is_populated = 0;
        // Processes that mapped the old object keep it until they let go
        shm_unlink(frame->backingName);
    } else
    {
        fprintf(stderr,"Creating new stream %s\n",streamName);
    }

    // Readers only trust a slot that is populated and keeps its generation while
    // they copy it, so: unpopulated (above), new generation, fields, populated last.
    context->nextGeneration = generation + 1;
    frame->generation  = generation;
    snprintf(frame->name, sizeof(frame->name), "%s", streamName);
    memcpy(frame->backingName, backingName, sizeof(frame->backingName));
    frame->width       = width;
    frame->height      = height;
    frame->channels    = channels;
    frame->frame_size  = frameSize;
    frame->bufferCount = bufferCount;
    frame->writeIndex  = 0;
    frame->latestIndex = 0;
    for (unsigned int i=0; i<MAX_LOCAL_BUFFERS; i++)
    {
        frame->timestamps[i]  = 0;
    }
    for (unsigned int i=0; i<MAX_READERS_PER_STREAM; i++)
    {
        frame->readers[i] = 0;
    }
    frame->locked   = 0;
    frame->ownerPid = getpid();

    int result = EXIT_FAILURE;
    if (createBackingObject(backingName, frameSize * bufferCount))
    {
        if ((unsigned int) slot >= context->numberOfBuffers) { context->numberOfBuffers = (unsigned int) slot + 1; }
        frame->is_populated = 1;
        result = EXIT_SUCCESS;
    } else
    {
        memset(frame, 0, sizeof(struct VideoFrame));
        trimNumberOfBuffers(context);
    }
    releaseRegistryLock(context);
    return result;
}

// Map existing shared memory for a video frame
unsigned char * map_frame_shared_memory(struct VideoFrame *frame,int copyToVideoFramePointer)
{
    (void) copyToVideoFramePointer; // mappings are always tracked per process now
    if (frame==0) { fprintf(stderr,"error: map_frame_shared_memory called without a valid video frame!\n"); return NULL; }

    unsigned char *base = currentMappingBase(frame, NULL, NULL, NULL);
    if (base == NULL) { fprintf(stderr,RED "Could not map stream %s\n" NORMAL,frame->name); }
    return base;
}

// Get a pointer to a video buffer by feed name
struct VideoFrame* getVideoBufferPointer(struct SharedMemoryContext * smvc, const char *feedName)
{
    if (smvc==0)     {return NULL; }
    if (feedName==0) {return NULL; }

    int slot = findStreamSlot(smvc, feedName);
    return (slot == -1) ? NULL : &smvc->buffer[slot];
}
//------------------------------------------------------------
//------------------------------------------------------------
//------------------------------------------------------------
unsigned char * getVideoFrameDataPointer(struct VideoFrame * frame)
{
  if (frame)
  {
    // A writer (between start/stopWritingToVideoBufferPointer() on this thread)
    // gets the slot it claimed, so writing in place never touches the slot
    // readers are using and is exactly what gets published.
    struct tlsWriteEntry *writeRecord = tlsWriteFind(frame);
    if (writeRecord != NULL)
    {
        return writeRecord->mapping->base + ((size_t) frame->writeIndex * writeRecord->mapping->frameSize);
    }

    // A reader gets whichever slot *this thread* latched onto via a matching
    // startReadingFromVideoBufferPointer() call, in the mapping that read keeps alive.
    struct tlsReadEntry *readRecord = tlsReadFind(frame);
    if (readRecord != NULL)
    {
        return readRecord->mapping->base + ((size_t) readRecord->index * readRecord->mapping->frameSize);
    }

    // Outside a read or write (unprotected): the currently published slot
    size_t frameSize = 0;
    unsigned int bufferCount = 0;
    unsigned char *base = currentMappingBase(frame, NULL, &frameSize, &bufferCount);
    if (base == NULL) { return 0; }
    unsigned int index = frame->latestIndex;
    if (index >= bufferCount) { index = 0; }
    return base + ((size_t) index * frameSize);
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

uint64_t getVideoFrameTimestamp(struct VideoFrame * frame)
{
  if (frame)
  {
    struct tlsReadEntry *readRecord = tlsReadFind(frame);
    unsigned int index = (readRecord != NULL) ? readRecord->index : frame->latestIndex;
    if (index >= MAX_LOCAL_BUFFERS) { index = 0; }
    return frame->timestamps[index];
  }
  return 0;
}

void setVideoFrameTimestamp(struct VideoFrame * frame, uint64_t unix_timestamp)
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
    // Check for multiplication overflow before computing frame_size
    if (height != 0 && width > (SIZE_MAX / height))
    {
        fprintf(stderr,"createVideoFrameMetaData: width*height would overflow\n");
        return EXIT_FAILURE;
    }
    size_t wh = (size_t)width * height;
    if (channels != 0 && wh > (SIZE_MAX / channels))
    {
        fprintf(stderr,"createVideoFrameMetaData: width*height*channels would overflow\n");
        return EXIT_FAILURE;
    }
    return registerStream(context, streamName, width, height, channels, wh * channels);
}



//------------------------------------------------------------
//------------------------------------------------------------
//------------------------------------------------------------
int createGenericMetaData(struct SharedMemoryContext* context,const char * streamName,unsigned int dataSize)
{
    return registerStream(context, streamName, dataSize, 1, 1, dataSize);
}



// Destroy a video frame and its shared memory
int destroyVideoFrame(struct SharedMemoryContext* context, const char *streamName)
{
    if (context == NULL || streamName == NULL) { return EXIT_FAILURE; }
    if (!acquireRegistryLock(context)) { return EXIT_FAILURE; }

    int index = findStreamSlot(context, streamName);
    if (index == -1)
    {
        fprintf(stderr, "Stream %s not found\n", streamName);
        releaseRegistryLock(context);
        return EXIT_FAILURE;
    }

    struct VideoFrame *frame = &context->buffer[index];
    pid_t owner = frame->ownerPid;
    if ((owner != getpid()) && !processIsDead(owner))
    {
        fprintf(stderr, "Not destroying stream %s: it belongs to process %d, which is still running\n", streamName, owner);
        releaseRegistryLock(context);
        return EXIT_FAILURE;
    }

    // Other processes notice through is_populated/generation; the pixels stay
    // wherever they're still mapped until those processes let go of them.
    frame->is_populated = 0;
    if (shm_unlink(frame->backingName) == -1)
    {
        debug_message("shm_unlink frame");
    }
    releaseMapping(frame);

    // Only this slot is cleared: other streams keep their slots, and the
    // VideoFrame pointers other processes hold to them stay valid
    memset(frame, 0, sizeof(struct VideoFrame));
    trimNumberOfBuffers(context);
    releaseRegistryLock(context);
    fprintf(stderr, "Stream %s destroyed\n", streamName);
    return EXIT_SUCCESS;
}

// Connect to existing shared memory context descriptor
struct SharedMemoryContext* connectToSharedMemoryContextDescriptor(const char *path)
{
    int shm_fd = shm_open(path, O_RDWR, 0666);
    if (shm_fd == -1)
    {
        debug_message(RED "shm_open frame\n" NORMAL);
        //perror("shm_open");
        return NULL;
    }

    struct SharedMemoryContext *context = mapCompatibleContext(shm_fd);
    close(shm_fd);
    if (context == NULL)
    {
        fprintf(stderr,RED "Shared memory context %s isn't laid out for this build of the library (version %u); rebuild every program using it, or recreate it with createSharedMemoryContextDescriptor()" NORMAL "\n",path,SHMVB_CONTEXT_VERSION);
    }
    return context;
}

// Start writing to a video buffer
// Spin until the writer lock is ours, or give up after ATTEMPTS_TO_LOCK_A_BUFFER tries.
static int acquireWriterLock(struct VideoFrame *vf)
{
    for (int attempts=0; attempts<ATTEMPTS_TO_LOCK_A_BUFFER; attempts++)
    {
      if (!__sync_lock_test_and_set(&vf->locked, 1)) { return 1; }
      usleep(SLEEP_TIME_BETWEEN_LOCK_ATTEMPTS_MICROSECONDS);
    }
    return 0;
}

int startWritingToVideoBufferPointer(struct VideoFrame *vf)
{
    if (vf==0) { return 0; }

    debug_message("startWritingToVideoBufferPointer :");
    int attempts = 0;

    if (!acquireWriterLock(vf))
    {
        debug_message(RED "failed\n" NORMAL);
        return 0; // Buffer is already locked and we timed out waiting for it
    }

    // Without a record (this thread already holds the writer lock on too many
    // frames) getVideoFrameDataPointer() couldn't find the claimed slot, and
    // without a mapping (the stream is gone) there's nowhere to write.
    struct tlsWriteEntry *writeRecord = tlsWriteFindFree();
    struct localStreamMapping *mapping = (writeRecord != NULL) ? beginMappingUse(vf) : NULL;
    if (mapping == NULL)
    {
        __sync_lock_release(&vf->locked);
        debug_message(RED "failed\n" NORMAL);
        return 0;
    }

    // Legacy single-buffer mode: the one and only slot
    unsigned int chosen = 0;
    if (mapping->bufferCount > 1)
    {
        // Multi-buffering: claim a slot that isn't the currently-published one and
        // has no active readers, so this write can never clobber a frame a reader
        // is still copying out. `locked` (held for the remainder of this write)
        // already serializes this search against any other writer, so a plain
        // read of readers[] here is safe - no reader ever registers on a slot
        // other than the live vf->latestIndex, and that can't change while we
        // hold the writer lock.
        chosen = mapping->bufferCount; // sentinel: "not found yet"
        attempts = 0;
        while (attempts<ATTEMPTS_TO_LOCK_A_BUFFER)
        {
            for (unsigned int k=0; k<mapping->bufferCount; k++)
            {
                unsigned int candidate = (vf->latestIndex + 1 + k) % mapping->bufferCount;
                if (candidate == vf->latestIndex) { continue; }
                if (!slotHasReaders(vf, candidate))
                {
                    chosen = candidate;
                    break;
                }
            }
            if (chosen != mapping->bufferCount) { break; }
            // Every candidate slot is busy - free any held by readers that died mid-read
            reclaimDeadReaders(vf);
            usleep(SLEEP_TIME_BETWEEN_LOCK_ATTEMPTS_MICROSECONDS);
            ++attempts;
        }

        if (chosen == mapping->bufferCount)
        {
            // Every non-latest slot has a lingering reader - extremely unlikely at
            // video framerates. Give up without touching any data (frame dropped,
            // never corrupted) and release the writer lock we're holding.
            endMappingUse(mapping);
            __sync_lock_release(&vf->locked);
            debug_message(RED "failed\n" NORMAL);
            return 0;
        }
    }

    vf->writeIndex = chosen;

    writeRecord->vf      = vf;
    writeRecord->mapping = mapping;
    writeRecord->aborted = 0;
    writeRecord->inUse   = 1;

    debug_message(GREEN "success\n" NORMAL);
    return 1; // We have locked the buffer
}

// Stop writing to a video buffer
int stopWritingToVideoBufferPointer(struct VideoFrame *vf)
{
    if (vf==0) { return 0; }
    debug_message("stopWritingToVideoBufferPointer :");

    struct tlsWriteEntry *writeRecord = tlsWriteFind(vf);
    // A rejected copy left the claimed slot holding an older frame; publishing
    // it would send readers back in time, so the previous frame stays latest.
    int aborted = (writeRecord != NULL) && writeRecord->aborted;
    struct localStreamMapping *mapping = (writeRecord != NULL) ? writeRecord->mapping : NULL;
    if (writeRecord != NULL) { writeRecord->inUse = 0; }

    if ((vf->bufferCount > 1) && (!aborted))
    {
        // Publish: make the just-written slot the one readers will latch onto.
        // The barrier ensures the memcpy done under copy_to_shared_memory is
        // visible to any thread that observes the new latestIndex.
        __sync_synchronize();
        vf->latestIndex = vf->writeIndex;
        __sync_synchronize();
    }

    __sync_lock_release(&vf->locked);
    if (mapping != NULL) { endMappingUse(mapping); }
    debug_message(GREEN "success\n" NORMAL);
    return 1;
}

// Re-stamp the frame currently published as latest, without writing new data.
// Holding the writer lock keeps latestIndex from moving while we stamp it.
int setLatestVideoFrameTimestamp(struct VideoFrame *vf, uint64_t unix_timestamp)
{
    if (vf==0) { return 0; }
    if (!acquireWriterLock(vf)) { return 0; }
    unsigned int index = vf->latestIndex;
    if (index >= MAX_LOCAL_BUFFERS) { index = 0; }
    vf->timestamps[index] = (unix_timestamp != 0) ? unix_timestamp : getUnixTimestampMicroseconds();
    __sync_lock_release(&vf->locked);
    return 1;
}

// Start reading from a video buffer.
// Legacy (bufferCount==1) mode: readers acquire no real lock, they only check
// that no writer is currently active - this is the original design and still
// carries the original torn-read risk if a writer starts mid-read.
// Multi-buffered mode: the reader latches onto the current "latest" slot and
// registers itself in readers[] for it, so the writer (which always skips
// slots with registered readers) can never overwrite the data being read. The
// load-register-recheck sequence below closes the narrow window where the
// published slot changes between reading it and registering interest in it.
// Either way the read keeps this process's mapping of the stream alive until
// the matching stop, even if the stream is re-created meanwhile.
int startReadingFromVideoBufferPointer(struct VideoFrame *vf)
{
    if (vf==0) { return 0; }
    debug_message("startReadingFromVideoBufferPointer :");

    // An earlier start on this frame from this thread that was never stopped
    // is superseded rather than leaked.
    stopReadingFromVideoBufferPointer(vf);

    // Reserve this thread's record before registering in shared memory, so a
    // registration can never exist without the record needed to release it.
    struct tlsReadEntry *record = tlsReadFindFree();
    if (record == NULL)
    {
        debug_message(RED "failed (this thread is reading too many frames)\n" NORMAL);
        return 0;
    }

    struct localStreamMapping *mapping = beginMappingUse(vf);
    if (mapping == NULL)
    {
        debug_message(RED "failed (no stream in this slot)\n" NORMAL);
        return 0;
    }

    unsigned int idx = 0;
    record->registered = 0;
    if (mapping->bufferCount <= 1)
    {
        if (vf->locked)
        {
            endMappingUse(mapping);
            debug_message(RED "failed\n" NORMAL);
            return 0; // Buffer is locked by a writer
        }
    } else
    {
        pid_t pid = getpid();
        uint64_t registration;
        int entry;
        for (;;)
        {
            idx          = vf->latestIndex;
            registration = packReaderRegistration(pid, idx);
            entry        = claimReaderEntry(vf, registration);
            if (entry == -1)
            {
                endMappingUse(mapping);
                debug_message(RED "failed (too many concurrent readers)\n" NORMAL);
                return 0;
            }
            if (vf->latestIndex == idx) { break; } // still current - we're protected
            __sync_bool_compare_and_swap(&vf->readers[entry], registration, 0); // stale, a publish raced us - retry
        }

        if (idx >= mapping->bufferCount)
        {
            // The stream was re-created with fewer slots right after we mapped it
            __sync_bool_compare_and_swap(&vf->readers[entry], registration, 0);
            endMappingUse(mapping);
            return 0;
        }
        record->registered   = 1;
        record->readerEntry  = (unsigned int) entry;
        record->registration = registration;
    }

    record->vf      = vf;
    record->mapping = mapping;
    record->index   = idx;
    record->inUse   = 1;
    debug_message(GREEN "success\n" NORMAL);
    return 1;
}

// Stop reading from a video buffer: releases the registration (multi-buffered
// streams) and the mapping claimed by the matching
// startReadingFromVideoBufferPointer() call on this thread.
int stopReadingFromVideoBufferPointer(struct VideoFrame *vf)
{
    if (vf==0) { return 0; }
    struct tlsReadEntry *record = tlsReadFind(vf);
    if (record != NULL)
    {
        // Compare-and-swap so only this read's own registration is released,
        // even if the entry was cleared (stream re-created) in the meantime.
        if (record->registered)
        {
            __sync_bool_compare_and_swap(&vf->readers[record->readerEntry], record->registration, 0);
        }
        record->inUse = 0;
        endMappingUse(record->mapping);
    }
    return 1;
}
