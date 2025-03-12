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

#define NORMAL   "\033[0m"
#define BLACK   "\033[30m"      /* Black */
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */

#include <signal.h>
#include <execinfo.h>
#include <unistd.h>

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
        perror("sigaction");
       // exit(EXIT_FAILURE);
        abort();
    }
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

unsigned char * getLocalMappingPointer(struct VideoFrameLocalMapping * lm,int item)
{
  if (lm!=0)
  {
     if ((lm->smc!=0) && (item<lm->smc->numberOfBuffers) )
     {
      return (unsigned char *) lm->data[item];
     }
  }
  return 0;
}


int mapRemoteToLocal(struct SharedMemoryContext *context, struct VideoFrameLocalMapping * localMap,int item)
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
                  localMap->sz[item]   = frame->frame_size;
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


int unmapLocalMappingItem(struct VideoFrameLocalMapping * localmap,int item)
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



// Function to copy data from a buffer to the shared memory buffer
void copy_to_shared_memory(struct VideoFrame *frame, const void* src, size_t n)
{
  if ( (frame!=0) && (src!=0) && (n!=0) )
    {
        if (frame->client_address_space_data_pointer!=0)
        {
           if (frame->frame_size >= n)
           {
             fprintf(stderr,"Will copy %lu bytes to stream %s, pointing @ %p\n",n,frame->name,frame->client_address_space_data_pointer);
             memcpy(frame->client_address_space_data_pointer,src, n);
           }
        }
    }
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

struct VideoFrame * getSharedMemoryContextVideoFrame(struct SharedMemoryContext *context, int item)
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

int remoteSharedMemoryContextVideoFrameIsPopulated(struct SharedMemoryContext *context, int item)
{
  if (context!=0)
  {
    if ( context->numberOfBuffers > item)
    {
     return (context->buffer[item].client_address_space_data_pointer!=NULL);
    }
  }
  return 0;
}

void printSharedMemoryContextState(struct SharedMemoryContext *context)
{
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
        perror("shm_open");
        return -1;
    }

    size_t total_size = sizeof(struct SharedMemoryContext);
    if (ftruncate(shm_fd, total_size) == -1)
    {
        fprintf(stderr,RED "ftruncate\n" NORMAL);
        perror("ftruncate");
        close(shm_fd);
        return -1;
    }

    struct SharedMemoryContext *context = (struct SharedMemoryContext*) mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (context == MAP_FAILED)
    {
        fprintf(stderr,RED "mmap\n" NORMAL);
        perror("mmap");
        close(shm_fd);
        return -1;
    }

    // Initialize the shared memory context
    context->numberOfBuffers = 0;

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

    fprintf(stderr,"Creating new video frame shared memory for %s\n",frame->name);
    int shm_fd = shm_open(frame->name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1)
    {
        fprintf(stderr,RED "shm_open frame\n" NORMAL);
        perror("shm_open frame");
        return -1;
    }

    if (ftruncate(shm_fd, frame->frame_size) == -1)
    {
        fprintf(stderr,RED "ftruncate frame\n" NORMAL);
        perror("ftruncate frame");
        close(shm_fd);
        return -1;
    }

    fprintf(stderr,"MMAP shared memory for %s , size %lu\n",frame->name,frame->frame_size);
    frame->client_address_space_data_pointer = (unsigned char*) mmap(NULL, frame->frame_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    fprintf(stderr,"MMAP pointer for %s @ %p\n",frame->name,frame->client_address_space_data_pointer);
    if (frame->client_address_space_data_pointer == MAP_FAILED)
    {
        fprintf(stderr,RED "mmap frame\n" NORMAL);
        perror("mmap frame");
        close(shm_fd);
        return -1;
    }

    frame->locked = 0;  // Initialize the lock to 0
    close(shm_fd);
    return 0;
}

// Map existing shared memory for a video frame
unsigned char * map_frame_shared_memory(struct VideoFrame *frame,int copyToVideoFramePointer)
{
    unsigned char * result = NULL;
    fprintf(stderr,"MMAP shared memory for %s , size %lu\n",frame->name,frame->frame_size);
    if (frame==0)    { return NULL; }

    int shm_fd = shm_open(frame->name, O_RDWR, 0666);
    if (shm_fd  == -1)
    {
        fprintf(stderr,RED "shm_open frame\n" NORMAL);
        perror("shm_open frame");
        return NULL;
    }

    result = (unsigned char*) mmap(NULL, frame->frame_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd , 0); //
    if (result  == MAP_FAILED)
    {
        fprintf(stderr,RED "mmap frame\n" NORMAL);
        perror("mmap frame");
        close(shm_fd);
        return NULL;
    }

    if (copyToVideoFramePointer)
    {
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
    return frame->client_address_space_data_pointer;
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
//------------------------------------------------------------
//------------------------------------------------------------
//------------------------------------------------------------
int createVideoFrameMetaData(struct SharedMemoryContext* context,const char * streamName,unsigned int width, unsigned int height, unsigned int channels)
{
   if (context!=0)
   {
      int contextID = -1;
      for (int i=0; i<context->numberOfBuffers; i++)
      {
         if (strcmp(streamName,context->buffer[i].name)==0)
         {
           fprintf(stderr,"Stream already existing \n");
           contextID = i;
         }
      }

      if (contextID==-1)
      {
         fprintf(stderr,"Creating new stream %s\n",streamName);
         contextID = context->numberOfBuffers++;
      }
    // Example to add a new buffer (Server)
    struct VideoFrame *newBuffer = &context->buffer[contextID];
    snprintf(newBuffer->name,MAX_SHM_NAME,"%s",streamName);
    newBuffer->width      = width;
    newBuffer->height     = height;
    newBuffer->channels   = channels;
    newBuffer->frame_size = newBuffer->width * newBuffer->height * newBuffer->channels;

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
        if (strcmp(context->buffer[i].name, streamName) == 0)
        {
            index = i;
            break;
        }
    }

    if (index == -1) {
        fprintf(stderr, "Stream %s not found\n", streamName);
        return EXIT_FAILURE;
    }

    struct VideoFrame *frame = &context->buffer[index];

    if (frame->client_address_space_data_pointer != NULL)
    {
        // Unmap the shared memory
        if (munmap(frame->client_address_space_data_pointer, frame->frame_size) == -1)
        {
            perror("munmap frame");
            return EXIT_FAILURE;
        }
        frame->client_address_space_data_pointer = NULL;
    }

    // Remove the shared memory object
    if (shm_unlink(frame->name) == -1)
    {
        perror("shm_unlink frame");
        return EXIT_FAILURE;
    }

    // Shift remaining buffers to fill the gap
    for (unsigned int i = index; i < context->numberOfBuffers - 1; i++)
    {
        context->buffer[i] = context->buffer[i + 1];
    }

    context->numberOfBuffers--;
    fprintf(stderr, "Stream %s destroyed\n", streamName);

    fprintf(stderr, "Final cleanup of %s\n",streamName);
    memset(frame,0,sizeof(struct VideoFrame));
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
        fprintf(stderr,RED "shm_open frame\n" NORMAL);
        perror("shm_open");
        return NULL;
    }

    size_t total_size = sizeof(struct SharedMemoryContext);
    struct SharedMemoryContext *context = (struct SharedMemoryContext*) mmap(NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (context == MAP_FAILED)
    {
        fprintf(stderr,RED "mmap\n" NORMAL);
        perror("mmap");
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

    fprintf(stderr,"startWritingToVideoBufferPointer :");
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
        fprintf(stderr,RED "failed\n" NORMAL);
        return 0; // Buffer is already locked and we timed out waiting for it
    }
    fprintf(stderr,GREEN "success\n" NORMAL);
    return 1; // We have locked the buffer
}

// Stop writing to a video buffer
int stopWritingToVideoBufferPointer(struct VideoFrame *vf)
{
    if (vf==0) { return 0; }
    fprintf(stderr,"stopWritingToVideoBufferPointer :");
    __sync_lock_release(&vf->locked);
    fprintf(stderr,GREEN "success\n" NORMAL);
    return 1;
}

// Start reading from a video buffer
int startReadingFromVideoBufferPointer(struct VideoFrame *vf)
{
    return startWritingToVideoBufferPointer(vf);
    /*
    if (vf==0) { return 0; }
    fprintf(stderr,"startReadingFromVideoBufferPointer :");
    if (__sync_fetch_and_add(&vf->locked, 0))
    {
        fprintf(stderr,RED "failed\n" NORMAL);
        return 0; // Buffer is locked
    }
    fprintf(stderr,GREEN "success\n" NORMAL);
    return 1;*/
}

// Stop reading from a video buffer
int stopReadingFromVideoBufferPointer(struct VideoFrame *vf)
{
    return stopWritingToVideoBufferPointer(vf);
    /*
    fprintf(stderr,"stopWritingToVideoBufferPointer :");
    if (vf==0) { return 0; }
    // No-op for readers
    fprintf(stderr,GREEN "success\n" NORMAL);
    return 1;*/
}
