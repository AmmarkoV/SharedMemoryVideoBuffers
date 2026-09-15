/** @file registry_edge_cases.c
 *  @brief Regression tests for how streams are registered, shared and torn down
 *  across processes (see sharedMemoryVideoBuffers.c):
 *
 *   1. context_kept          - creating a context that already exists keeps its streams
 *   2. context_incompatible  - connecting to a context with a different layout fails,
 *                              and creating it again re-initializes it
 *   3. join_same_size        - another process registering an existing stream with the
 *                              same size joins it: nothing is reset and it can't destroy it
 *   4. resize_owner_alive    - registering a stream with a different size fails while
 *                              its owner is alive
 *   5. resize_owner_dead     - ...and replaces the stream once the owner is dead
 *   6. destroy_keeps_slots   - destroying a stream leaves other streams where they are,
 *                              and a new stream reuses the freed slot
 *   7. recreated_stream      - a reader in another process follows a stream that was
 *                              destroyed and re-created with a different size
 *   8. remap_during_read     - re-creating a stream doesn't unmap memory that a read in
 *                              progress is still using
 *   9. concurrent_creates    - processes registering streams at the same time get distinct
 *                              slots, and a stream they all register exists only once
 *  10. local_mapping_bounds  - a local mapping can be released after the stream count
 *                              shrinks, and mapping an empty slot reports failure
 *  11. namespaced_streams    - the same stream name in two contexts means two streams
 *
 *  Every case runs in its own forked process, so a crash counts as that case failing.
 *
 *  Exit code: 0 = all checks passed, 2 = a check failed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include "sharedMemoryVideoBuffers.h"

#define W 32
#define H 24
#define C 3

static int failures = 0;

static void check(int passed, const char *name)
{
    fprintf(stderr, "  %s : %s\n", passed ? "ok  " : "FAIL", name);
    if (!passed) { failures++; }
}

static struct SharedMemoryContext * freshContext(const char *name)
{
    shm_unlink(name);
    if (createSharedMemoryContextDescriptor(name) != 0) { return NULL; }
    return connectToSharedMemoryContextDescriptor(name);
}

// Fills the whole frame with `value` (also used as its timestamp).
static int writeValue(struct VideoFrame *frame, unsigned char value)
{
    size_t size = getVideoFrameDataSize(frame);
    unsigned char *buf = malloc(size);
    if (buf == NULL) { return 0; }
    memset(buf, value, size);
    int ok = 0;
    if (startWritingToVideoBufferPointer(frame))
    {
        ok = copy_to_shared_memory(frame, buf, size, value);
        stopWritingToVideoBufferPointer(frame);
    }
    free(buf);
    return ok;
}

// Returns the frame's value (first byte) or -1 if it can't be read or its first
// and last bytes differ. `data` is NULL for reads through getVideoFrameDataPointer().
static int readValueFrom(struct VideoFrame *frame, struct VideoFrameLocalMapping *lm, unsigned int item, unsigned long *timestamp)
{
    if (!startReadingFromVideoBufferPointer(frame)) { return -1; }
    unsigned char *data = (lm != NULL) ? getLocalMappingPointer(lm, item) : getVideoFrameDataPointer(frame);
    size_t size = getVideoFrameDataSize(frame);
    int value = -1;
    if ((data != NULL) && (data[0] == data[size-1])) { value = data[0]; }
    if (timestamp != NULL) { *timestamp = getVideoFrameTimestamp(frame); }
    stopReadingFromVideoBufferPointer(frame);
    return value;
}

static int readValue(struct VideoFrame *frame)
{
    return readValueFrom(frame, NULL, 0, NULL);
}

static int countStreamsNamed(struct SharedMemoryContext *ctx, const char *name)
{
    int count = 0;
    for (int i=0; i<MAX_NUMBER_OF_BUFFERS; i++)
    {
        if (ctx->buffer[i].is_populated && strcmp(ctx->buffer[i].name, name) == 0) { count++; }
    }
    return count;
}

// Waits for a child and returns its exit code (255 if it crashed).
static int childExitStatus(pid_t child)
{
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 255;
}

// ---------------------------------------------------------------------------

static void context_kept(const char *ctxName)
{
    struct SharedMemoryContext *ctx = freshContext(ctxName);
    if (!ctx || createVideoFrameMetaData(ctx, "reg1", W, H, C) != 0) { check(0, "context_kept: setup"); return; }
    createSharedMemoryContextDescriptor(ctxName); // e.g. another program starting up
    check(getVideoBufferPointer(ctx, "reg1") != NULL, "context_kept: creating an existing context keeps its streams");
    destroyVideoFrame(ctx, "reg1");
}

static void context_incompatible(const char *ctxName)
{
    if (!freshContext(ctxName)) { check(0, "context_incompatible: setup"); return; }
    int fd = shm_open(ctxName, O_RDWR, 0);
    uint32_t *firstWord = mmap(NULL, sizeof(uint32_t), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    firstWord[0] = 0xDEADBEEF; // not this build's layout
    munmap(firstWord, sizeof(uint32_t));
    check(connectToSharedMemoryContextDescriptor(ctxName) == NULL, "context_incompatible: connecting to a different layout fails");
    struct SharedMemoryContext *ctx = NULL;
    if (createSharedMemoryContextDescriptor(ctxName) == 0) { ctx = connectToSharedMemoryContextDescriptor(ctxName); }
    check(ctx != NULL && getSharedMemoryContextNumberOfBuffers(ctx) == 0, "context_incompatible: creating it again re-initializes it");
}

static void join_same_size(const char *ctxName)
{
    struct SharedMemoryContext *ctx = freshContext(ctxName);
    if (!ctx || createVideoFrameMetaData(ctx, "reg3", W, H, C) != 0) { check(0, "join_same_size: setup"); return; }
    struct VideoFrame *frame = getVideoBufferPointer(ctx, "reg3");
    writeValue(frame, 5);

    pid_t child = fork();
    if (child == 0)
    {
        struct SharedMemoryContext *childCtx = connectToSharedMemoryContextDescriptor(ctxName);
        int joined    = (createVideoFrameMetaData(childCtx, "reg3", W, H, C) == 0);
        int destroyed = (destroyVideoFrame(childCtx, "reg3") == 0);
        _exit((joined ? 0 : 1) | (destroyed ? 2 : 0));
    }
    int status = childExitStatus(child);
    check(status != 255 && !(status & 1), "join_same_size: registering an existing stream with the same size succeeds");
    check(status != 255 && !(status & 2), "join_same_size: a process that joined can't destroy the stream");
    unsigned long timestamp = 0;
    int value = readValueFrom(frame, NULL, 0, &timestamp);
    check(value == 5 && timestamp == 5, "join_same_size: joining resets nothing");
    check(writeValue(frame, 6) && readValue(frame) == 6, "join_same_size: the owner keeps publishing");
    destroyVideoFrame(ctx, "reg3");
}

static void resize_owner_alive(const char *ctxName)
{
    struct SharedMemoryContext *ctx = freshContext(ctxName);
    if (!ctx || createVideoFrameMetaData(ctx, "reg4", W, H, C) != 0) { check(0, "resize_owner_alive: setup"); return; }
    struct VideoFrame *frame = getVideoBufferPointer(ctx, "reg4");
    writeValue(frame, 7);

    pid_t child = fork();
    if (child == 0)
    {
        struct SharedMemoryContext *childCtx = connectToSharedMemoryContextDescriptor(ctxName);
        _exit(createVideoFrameMetaData(childCtx, "reg4", W*2, H, C) == 0 ? 1 : 0);
    }
    check(childExitStatus(child) == 0, "resize_owner_alive: a different size is refused while the owner is alive");
    check(frame->width == W && readValue(frame) == 7, "resize_owner_alive: the stream is left unchanged");
    destroyVideoFrame(ctx, "reg4");
}

static void resize_owner_dead(const char *ctxName)
{
    struct SharedMemoryContext *ctx = freshContext(ctxName);
    if (!ctx) { check(0, "resize_owner_dead: setup"); return; }

    pid_t child = fork();
    if (child == 0)
    {
        struct SharedMemoryContext *childCtx = connectToSharedMemoryContextDescriptor(ctxName);
        if (createVideoFrameMetaData(childCtx, "reg5", W, H, C) != 0) { _exit(1); }
        writeValue(getVideoBufferPointer(childCtx, "reg5"), 3);
        _exit(0); // exits without destroying its stream
    }
    childExitStatus(child);
    check(createVideoFrameMetaData(ctx, "reg5", W*2, H, C) == 0, "resize_owner_dead: a different size replaces the stream once its owner is dead");
    struct VideoFrame *frame = getVideoBufferPointer(ctx, "reg5");
    check(frame != NULL && frame->width == W*2 && writeValue(frame, 8) && readValue(frame) == 8,
          "resize_owner_dead: the replacement is usable at its new size");
    destroyVideoFrame(ctx, "reg5");
}

static void destroy_keeps_slots(const char *ctxName)
{
    struct SharedMemoryContext *ctx = freshContext(ctxName);
    if (!ctx || createVideoFrameMetaData(ctx, "reg6a", W, H, C) != 0 || createVideoFrameMetaData(ctx, "reg6b", W, H, C) != 0)
    { check(0, "destroy_keeps_slots: setup"); return; }
    struct VideoFrame *frameB = getVideoBufferPointer(ctx, "reg6b");
    writeValue(frameB, 9);
    int slotA = resolveFeedNameToID(ctx, "reg6a");

    destroyVideoFrame(ctx, "reg6a");
    check(strcmp(frameB->name, "reg6b") == 0 && readValue(frameB) == 9, "destroy_keeps_slots: other streams stay where they are");
    check(createVideoFrameMetaData(ctx, "reg6c", W, H, C) == 0 && resolveFeedNameToID(ctx, "reg6c") == slotA,
          "destroy_keeps_slots: a new stream reuses the freed slot");
    destroyVideoFrame(ctx, "reg6b");
    destroyVideoFrame(ctx, "reg6c");
}

static void recreated_stream(const char *ctxName)
{
    struct SharedMemoryContext *ctx = freshContext(ctxName);
    if (!ctx) { check(0, "recreated_stream: setup"); return; }
    int toWriter[2], fromWriter[2];
    if (pipe(toWriter) != 0 || pipe(fromWriter) != 0) { check(0, "recreated_stream: setup"); return; }

    pid_t writer = fork();
    if (writer == 0)
    {
        char token;
        close(toWriter[1]);
        close(fromWriter[0]);
        struct SharedMemoryContext *writerCtx = connectToSharedMemoryContextDescriptor(ctxName);
        createVideoFrameMetaData(writerCtx, "reg7", W, H, C);
        writeValue(getVideoBufferPointer(writerCtx, "reg7"), 1);
        write(fromWriter[1], "1", 1);
        if (read(toWriter[0], &token, 1) != 1) { _exit(1); } // reader has seen frame 1
        destroyVideoFrame(writerCtx, "reg7");                 // e.g. publisher restarting...
        createVideoFrameMetaData(writerCtx, "reg7", W*2, H, C); // ...at a new resolution
        writeValue(getVideoBufferPointer(writerCtx, "reg7"), 2);
        write(fromWriter[1], "2", 1);
        read(toWriter[0], &token, 1);                         // keep the stream alive until the reader is done
        destroyVideoFrame(writerCtx, "reg7");
        _exit(0);
    }
    // Close our copies of the writer's ends, so either side dying unblocks the other
    close(toWriter[0]);
    close(fromWriter[1]);

    char token;
    if (read(fromWriter[0], &token, 1) != 1) { check(0, "recreated_stream: writer died during setup"); return; }
    struct VideoFrame *frame = getVideoBufferPointer(ctx, "reg7");
    int item = resolveFeedNameToID(ctx, "reg7");
    struct VideoFrameLocalMapping *lm = allocateLocalMapping();
    int mapped = (frame != NULL) && mapRemoteToLocal(ctx, lm, (unsigned int) item);
    check(mapped && readValueFrom(frame, lm, (unsigned int) item, NULL) == 1, "recreated_stream: reader sees the original stream");

    write(toWriter[1], "g", 1);
    if (read(fromWriter[0], &token, 1) != 1) { check(0, "recreated_stream: writer died re-creating the stream"); return; }
    frame = getVideoBufferPointer(ctx, "reg7");
    int sameSlot = (frame != NULL) && (resolveFeedNameToID(ctx, "reg7") == item);
    check(sameSlot && readValueFrom(frame, lm, (unsigned int) item, NULL) == 2,
          "recreated_stream: reader follows the re-created, larger stream without remapping explicitly");
    check(sameSlot && readValue(frame) == 2, "recreated_stream: so does getVideoFrameDataPointer()");
    write(toWriter[1], "g", 1);
    childExitStatus(writer);
    freeLocalMapping(lm);
}

static void remap_during_read(const char *ctxName)
{
    struct SharedMemoryContext *ctx = freshContext(ctxName);
    if (!ctx || createVideoFrameMetaData(ctx, "reg8", W, H, C) != 0) { check(0, "remap_during_read: setup"); return; }
    struct VideoFrame *frame = getVideoBufferPointer(ctx, "reg8");
    writeValue(frame, 1);

    if (!startReadingFromVideoBufferPointer(frame)) { check(0, "remap_during_read: setup"); return; }
    unsigned char *oldData = getVideoFrameDataPointer(frame);
    destroyVideoFrame(ctx, "reg8");
    createVideoFrameMetaData(ctx, "reg8", W*2, H, C);
    struct VideoFrame *recreated = getVideoBufferPointer(ctx, "reg8");
    int wrote = (recreated != NULL) && writeValue(recreated, 2);
    check(oldData != NULL && oldData[0] == 1 && oldData[W*H*C-1] == 1, "remap_during_read: memory of a read in progress stays mapped");
    stopReadingFromVideoBufferPointer(frame);
    check(wrote && readValue(recreated) == 2, "remap_during_read: the next read sees the re-created stream");
    destroyVideoFrame(ctx, "reg8");
}

static void concurrent_creates(const char *ctxName)
{
    struct SharedMemoryContext *ctx = freshContext(ctxName);
    int startLine[2];
    if (!ctx || pipe(startLine) != 0) { check(0, "concurrent_creates: setup"); return; }

    enum { CHILDREN = 8 };
    pid_t children[CHILDREN];
    for (int i=0; i<CHILDREN; i++)
    {
        if ((children[i] = fork()) == 0)
        {
            char token, name[32];
            close(startLine[1]);
            read(startLine[0], &token, 1); // returns once the parent closes the write end
            struct SharedMemoryContext *childCtx = connectToSharedMemoryContextDescriptor(ctxName);
            snprintf(name, sizeof(name), "reg9_%d", i);
            int ok = (createVideoFrameMetaData(childCtx, name, W, H, C) == 0) &&
                     (createVideoFrameMetaData(childCtx, "reg9_shared", W, H, C) == 0);
            _exit(ok ? 0 : 1);
        }
    }
    close(startLine[1]); // go
    int allSucceeded = 1;
    for (int i=0; i<CHILDREN; i++) { if (childExitStatus(children[i]) != 0) { allSucceeded = 0; } }

    int eachOnce = 1;
    for (int i=0; i<CHILDREN; i++)
    {
        char name[32];
        snprintf(name, sizeof(name), "reg9_%d", i);
        if (countStreamsNamed(ctx, name) != 1) { eachOnce = 0; }
    }
    check(allSucceeded && eachOnce, "concurrent_creates: every stream gets its own slot");
    check(countStreamsNamed(ctx, "reg9_shared") == 1, "concurrent_creates: a stream registered by all of them exists once");

    // Their owners are dead now, so cleaning up is allowed
    for (int i=0; i<CHILDREN; i++) { char name[32]; snprintf(name, sizeof(name), "reg9_%d", i); destroyVideoFrame(ctx, name); }
    destroyVideoFrame(ctx, "reg9_shared");
}

static void local_mapping_bounds(const char *ctxName)
{
    struct SharedMemoryContext *ctx = freshContext(ctxName);
    if (!ctx || createVideoFrameMetaData(ctx, "reg10a", W, H, C) != 0 || createVideoFrameMetaData(ctx, "reg10b", W, H, C) != 0)
    { check(0, "local_mapping_bounds: setup"); return; }
    int slotB = resolveFeedNameToID(ctx, "reg10b");
    struct VideoFrameLocalMapping *lm = allocateLocalMapping();
    int mapped = mapRemoteToLocal(ctx, lm, (unsigned int) slotB);
    destroyVideoFrame(ctx, "reg10b"); // stream count shrinks below slotB
    check(mapped && unmapLocalMappingItem(lm, (unsigned int) slotB) == 1, "local_mapping_bounds: a mapping can be released after the stream count shrinks");
    check(mapRemoteToLocal(ctx, lm, (unsigned int) slotB) == 0, "local_mapping_bounds: mapping an empty slot reports failure");
    freeLocalMapping(lm);
    destroyVideoFrame(ctx, "reg10a");
}

static void namespaced_streams(const char *ctxName)
{
    char otherName[64];
    snprintf(otherName, sizeof(otherName), "%s.other", ctxName);
    struct SharedMemoryContext *ctxA = freshContext(ctxName);
    struct SharedMemoryContext *ctxB = freshContext(otherName);
    if (!ctxA || !ctxB || createVideoFrameMetaData(ctxA, "reg11", W, H, C) != 0) { check(0, "namespaced_streams: setup"); return; }
    int createdB = (createVideoFrameMetaData(ctxB, "reg11", W*2, H, C) == 0);
    struct VideoFrame *frameA = getVideoBufferPointer(ctxA, "reg11");
    struct VideoFrame *frameB = getVideoBufferPointer(ctxB, "reg11");
    int wrote = createdB && writeValue(frameA, 4) && writeValue(frameB, 5);
    check(wrote && readValue(frameA) == 4 && readValue(frameB) == 5, "namespaced_streams: the same name in two contexts means two streams");
    destroyVideoFrame(ctxA, "reg11");
    destroyVideoFrame(ctxB, "reg11");
    shm_unlink(otherName);
}

// ---------------------------------------------------------------------------

static void runCase(void (*body)(const char *), const char *label, int number)
{
    char ctxName[64];
    snprintf(ctxName, sizeof(ctxName), "shmvb_test_reg_%d.shm", number);
    pid_t child = fork();
    if (child == 0)
    {
        failures = 0; // report only this case's failures
        body(ctxName);
        shm_unlink(ctxName);
        _exit(failures);
    }
    int status = 0;
    waitpid(child, &status, 0);
    if (WIFSIGNALED(status))
    {
        fprintf(stderr, "  FAIL : %s: crashed with signal %d\n", label, WTERMSIG(status));
        failures++;
        shm_unlink(ctxName);
    }
    else { failures += WEXITSTATUS(status); }
}

int main()
{
    unsetenv("SHMVB_BUFFER_COUNT");
    runCase(context_kept,         "context_kept",          1);
    runCase(context_incompatible, "context_incompatible",  2);
    runCase(join_same_size,       "join_same_size",        3);
    runCase(resize_owner_alive,   "resize_owner_alive",    4);
    runCase(resize_owner_dead,    "resize_owner_dead",     5);
    runCase(destroy_keeps_slots,  "destroy_keeps_slots",   6);
    runCase(recreated_stream,     "recreated_stream",      7);
    runCase(remap_during_read,    "remap_during_read",     8);
    runCase(concurrent_creates,   "concurrent_creates",    9);
    runCase(local_mapping_bounds, "local_mapping_bounds", 10);
    runCase(namespaced_streams,   "namespaced_streams",   11);
    fprintf(stderr, "registry_edge_cases: %d failure(s)\n", failures);
    return failures > 0 ? 2 : 0;
}
