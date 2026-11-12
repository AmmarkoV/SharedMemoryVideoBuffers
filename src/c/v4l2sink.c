/** @file v4l2sink.c
 *  @brief Bridges one SharedMemoryVideoBuffers stream to a v4l2loopback device, so any V4L2
 *  application (OBS, browsers, ffplay, OpenCV...) sees the stream as a webcam.
 *
 *  Usage: v4l2sink [options]
 *    -c, --context NAME   shared memory context (default video_frames.shm)
 *    -s, --stream NAME    stream to bridge (default stream1)
 *    -d, --device PATH    v4l2loopback node (default /dev/video43)
 *    -f, --format FMT     auto|grey|rgb24|bgr24|yuyv|yu12 (default auto: grey for 1 channel, else yuyv)
 *    -r, --fps N          advertised frame rate; the last frame is re-sent if the publisher is
 *                         slower than this, so consumers don't time out (0 = never re-send; default 30)
 *    -b, --swap-rb        the stream holds BGR(A) instead of RGB(A) (e.g. OpenCV publishers)
 *    -h, --help
 *
 *  The device must exist first, e.g.:
 *    sudo modprobe v4l2loopback devices=1 video_nr=43 card_label="SHMVB Camera" exclusive_caps=1
 *  (see scripts/v4l2_setup.sh).
 *
 *  Each frame is converted into a private staging buffer while the stream's read lock is held,
 *  and the lock is released before write()ing to the device, so a slow V4L2 consumer never
 *  stalls the publishers.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <linux/videodev2.h>

#include "sharedMemoryVideoBuffers.h"
#include "pixel_convert.h"

#define MAX_SINK_DIMENSION 16384

static volatile sig_atomic_t running = 1;
static void onSignal(int sig) { (void) sig; running = 0; }

static uint64_t nowNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

static void sleepMs(unsigned int ms) { usleep(ms * 1000u); }

struct FormatName { const char *name; int fmt; uint32_t fourcc; };
static const struct FormatName formats[] =
{
    { "grey",  PIXOUT_GREY,  V4L2_PIX_FMT_GREY   },
    { "rgb24", PIXOUT_RGB24, V4L2_PIX_FMT_RGB24  },
    { "bgr24", PIXOUT_BGR24, V4L2_PIX_FMT_BGR24  },
    { "yuyv",  PIXOUT_YUYV,  V4L2_PIX_FMT_YUYV   },
    { "yu12",  PIXOUT_YU12,  V4L2_PIX_FMT_YUV420 },
};
#define FORMAT_AUTO (-1)

static uint32_t fourccOf(enum PixelOutputFormat fmt)
{
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++)
    {
        if (formats[i].fmt == (int) fmt) { return formats[i].fourcc; }
    }
    return 0;
}

static const char *nameOf(enum PixelOutputFormat fmt)
{
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++)
    {
        if (formats[i].fmt == (int) fmt) { return formats[i].name; }
    }
    return "?";
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [-c context] [-s stream] [-d /dev/videoN] [-f auto|grey|rgb24|bgr24|yuyv|yu12] [-r fps] [-b]\n"
            "  -c, --context NAME  shared memory context (default video_frames.shm)\n"
            "  -s, --stream NAME   stream to bridge (default stream1)\n"
            "  -d, --device PATH   v4l2loopback device (default /dev/video43)\n"
            "  -f, --format FMT    output pixel format (default auto: grey for 1 channel, else yuyv)\n"
            "  -r, --fps N         advertised frame rate, last frame re-sent when idle (0 = no re-send, default 30)\n"
            "  -b, --swap-rb       stream holds BGR(A) instead of RGB(A)\n",
            argv0);
}

/** Device state: what the device is currently configured for. */
struct Sink
{
    const char *path;
    int fd;                        ///< -1 while closed
    int configured;                ///< S_FMT succeeded for the values below
    unsigned int width, height;
    enum PixelOutputFormat fmt;
    size_t imageSize;              ///< sizeimage the device expects per write()
};

static int openSink(struct Sink *sink)
{
    sink->fd = open(sink->path, O_RDWR);
    if (sink->fd < 0) { sink->fd = open(sink->path, O_WRONLY); }
    if (sink->fd < 0)
    {
        fprintf(stderr, "Can't open %s: %s\n", sink->path, strerror(errno));
        return 0;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(sink->fd, VIDIOC_QUERYCAP, &cap) < 0)
    {
        fprintf(stderr, "%s isn't a V4L2 device (VIDIOC_QUERYCAP: %s)\n", sink->path, strerror(errno));
        close(sink->fd); sink->fd = -1;
        return 0;
    }
    uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (!(caps & V4L2_CAP_VIDEO_OUTPUT))
    {
        fprintf(stderr, "%s (%s, driver %s) doesn't accept output frames.\n"
                        "Is it a v4l2loopback device? Try: sudo modprobe v4l2loopback video_nr=43 exclusive_caps=1\n",
                sink->path, (const char *) cap.card, (const char *) cap.driver);
        close(sink->fd); sink->fd = -1;
        return 0;
    }
    if (strcmp((const char *) cap.driver, "v4l2 loopback") != 0)
    {
        fprintf(stderr, "Warning: %s's driver is \"%s\", not v4l2loopback\n", sink->path, (const char *) cap.driver);
    }
    sink->configured = 0;
    return 1;
}

static void closeSink(struct Sink *sink)
{
    if (sink->fd >= 0) { close(sink->fd); }
    sink->fd = -1;
    sink->configured = 0;
}

static int setFormat(struct Sink *sink, unsigned int width, unsigned int height,
                     enum PixelOutputFormat fmt, unsigned int fps)
{
    struct v4l2_format vf;
    memset(&vf, 0, sizeof(vf));
    vf.type                    = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    vf.fmt.pix.width           = width;
    vf.fmt.pix.height          = height;
    vf.fmt.pix.pixelformat     = fourccOf(fmt);
    vf.fmt.pix.field           = V4L2_FIELD_NONE;
    vf.fmt.pix.bytesperline    = pixelOutputBytesPerLine(fmt, width);
    vf.fmt.pix.sizeimage       = (uint32_t) pixelOutputSize(fmt, width, height);
    vf.fmt.pix.colorspace      = (fmt == PIXOUT_YUYV || fmt == PIXOUT_YU12) ? V4L2_COLORSPACE_SMPTE170M : V4L2_COLORSPACE_SRGB;

    if (ioctl(sink->fd, VIDIOC_S_FMT, &vf) < 0) { return 0; }

    if (vf.fmt.pix.width != width || vf.fmt.pix.height != height || vf.fmt.pix.pixelformat != fourccOf(fmt))
    {
        fprintf(stderr, "%s adjusted the format to %ux%u %.4s, wanted %ux%u %s\n", sink->path,
                vf.fmt.pix.width, vf.fmt.pix.height, (const char *) &vf.fmt.pix.pixelformat, width, height, nameOf(fmt));
        errno = EINVAL;
        return 0;
    }

    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    parm.parm.output.timeperframe.numerator   = 1;
    parm.parm.output.timeperframe.denominator = fps ? fps : 30;
    if (ioctl(sink->fd, VIDIOC_S_PARM, &parm) < 0)
    {
        fprintf(stderr, "Warning: %s refused frame rate %u (%s)\n", sink->path, parm.parm.output.timeperframe.denominator, strerror(errno));
    }

    sink->width     = width;
    sink->height    = height;
    sink->fmt       = fmt;
    sink->imageSize = vf.fmt.pix.sizeimage ? vf.fmt.pix.sizeimage : pixelOutputSize(fmt, width, height);
    sink->configured = 1;
    return 1;
}

/** (Re)configures the device for a stream layout, reopening it if the open fd refuses the change. */
static int configureSink(struct Sink *sink, unsigned int width, unsigned int height,
                         enum PixelOutputFormat fmt, unsigned int fps)
{
    if (sink->configured && sink->width == width && sink->height == height && sink->fmt == fmt) { return 1; }
    if (sink->fd < 0 && !openSink(sink)) { return 0; }
    if (setFormat(sink, width, height, fmt, fps)) { goto ok; }

    // v4l2loopback locks the format once frames were written; a fresh open can change it
    // as long as no consumer still has the device open
    int err = errno;
    closeSink(sink);
    if (openSink(sink) && setFormat(sink, width, height, fmt, fps)) { goto ok; }
    fprintf(stderr, "Can't set %s to %ux%u %s: %s%s\n", sink->path, width, height, nameOf(fmt), strerror(err),
            (err == EBUSY) ? " (close the applications reading it, then it'll be retried)" : "");
    return 0;

ok:
    fprintf(stderr, "%s: %ux%u %s, %zu bytes/frame\n", sink->path, width, height, nameOf(fmt), sink->imageSize);
    return 1;
}

static int writeFrame(struct Sink *sink, const unsigned char *data, size_t size)
{
    size_t done = 0;
    while (done < size)
    {
        ssize_t n = write(sink->fd, data + done, size - done);
        if (n < 0)
        {
            if (errno == EINTR) { if (!running) { return 0; } continue; }
            fprintf(stderr, "Writing to %s failed: %s\n", sink->path, strerror(errno));
            return 0;
        }
        if (n == 0) { return 0; }
        done += (size_t) n;
    }
    return 1;
}

int main(int argc, char *argv[])
{
    const char *contextName = "video_frames.shm";
    const char *streamName  = "stream1";
    struct Sink sink;
    memset(&sink, 0, sizeof(sink));
    sink.path = "/dev/video43";
    sink.fd   = -1;
    int requestedFormat = FORMAT_AUTO;
    unsigned int fps    = 30;
    int swapRB          = 0;

    static const struct option longOptions[] =
    {
        { "context", required_argument, 0, 'c' },
        { "stream",  required_argument, 0, 's' },
        { "device",  required_argument, 0, 'd' },
        { "format",  required_argument, 0, 'f' },
        { "fps",     required_argument, 0, 'r' },
        { "swap-rb", no_argument,       0, 'b' },
        { "help",    no_argument,       0, 'h' },
        { 0, 0, 0, 0 }
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "c:s:d:f:r:bh", longOptions, NULL)) != -1)
    {
        switch (opt)
        {
        case 'c': contextName = optarg; break;
        case 's': streamName  = optarg; break;
        case 'd': sink.path   = optarg; break;
        case 'b': swapRB      = 1;      break;
        case 'r':
        {
            char *end;
            long v = strtol(optarg, &end, 10);
            if (*end != '\0' || v < 0 || v > 1000) { fprintf(stderr, "Bad --fps %s\n", optarg); return EXIT_FAILURE; }
            fps = (unsigned int) v;
            break;
        }
        case 'f':
        {
            if (strcasecmp(optarg, "auto") == 0) { requestedFormat = FORMAT_AUTO; break; }
            requestedFormat = -2;
            for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++)
            {
                if (strcasecmp(optarg, formats[i].name) == 0) { requestedFormat = formats[i].fmt; }
            }
            if (requestedFormat == -2) { fprintf(stderr, "Unknown format %s\n", optarg); usage(argv[0]); return EXIT_FAILURE; }
            break;
        }
        case 'h': usage(argv[0]); return EXIT_SUCCESS;
        default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if (optind < argc) { usage(argv[0]); return EXIT_FAILURE; }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onSignal; // no SA_RESTART: a blocked write() returns EINTR so Ctrl-C works
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    // Check the device up front so a wrong path fails immediately, not after the first frame
    if (!openSink(&sink)) { return EXIT_FAILURE; }

    // Create the context if nobody did yet (an existing one is kept), so the sink can start first
    if (createSharedMemoryContextDescriptor(contextName) == -1) { closeSink(&sink); return EXIT_FAILURE; }
    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(contextName);
    if (!context) { closeSink(&sink); return EXIT_FAILURE; }

    unsigned char *staging    = NULL;
    size_t stagingSize        = 0;
    int haveFrame             = 0;     // staging holds a complete frame for the current config
    uint64_t lastGeneration   = 0;
    uint64_t lastTimestamp    = 0;
    uint64_t lastWriteNs      = 0;
    uint64_t lastRetryNs      = 0;
    int waitingReported       = 0;
    unsigned long framesOut   = 0;
    const uint64_t repeatNs   = fps ? 1000000000ull / fps : 0;

    fprintf(stderr, "Bridging %s/%s -> %s\n", contextName, streamName, sink.path);

    while (running)
    {
        struct VideoFrame *frame = getVideoBufferPointer(context, streamName);
        if (frame == NULL || getVideoFrameTimestamp(frame) == 0)
        {
            if (!waitingReported) { fprintf(stderr, "Waiting for stream %s...\n", streamName); waitingReported = 1; }
            // Keep consumers fed with the last frame while the publisher restarts
            if (haveFrame && repeatNs && nowNs() - lastWriteNs >= repeatNs)
            {
                if (writeFrame(&sink, staging, sink.imageSize)) { lastWriteNs = nowNs(); } else { haveFrame = 0; closeSink(&sink); }
            }
            sleepMs(50);
            continue;
        }
        waitingReported = 0;

        int fresh = (frame->generation != lastGeneration) || (getVideoFrameTimestamp(frame) != lastTimestamp);
        if (fresh)
        {
            // Snapshot the layout; only trusted if the generation survives until the read started
            int populated         = frame->is_populated;
            uint64_t generation   = frame->generation;
            unsigned int w        = frame->width;
            unsigned int h        = frame->height;
            unsigned int channels = frame->channels;
            size_t frameSize      = frame->frame_size;

            int usable = populated && w >= 1 && h >= 1 && w <= MAX_SINK_DIMENSION && h <= MAX_SINK_DIMENSION &&
                         channels >= 1 && (size_t) w * h <= frameSize / channels;
            if (!usable)
            {
                if (generation != lastGeneration) { fprintf(stderr, "Can't bridge a %ux%u:%u stream\n", w, h, channels); }
                lastGeneration = generation;
                lastTimestamp  = getVideoFrameTimestamp(frame);
                sleepMs(100);
                continue;
            }

            enum PixelOutputFormat fmt = (requestedFormat == FORMAT_AUTO) ?
                                         ((channels == 1) ? PIXOUT_GREY : PIXOUT_YUYV) :
                                         (enum PixelOutputFormat) requestedFormat;

            if (!sink.configured || sink.width != w || sink.height != h || sink.fmt != fmt)
            {
                // Don't hammer a busy device: retry a refused reconfiguration once a second
                uint64_t now = nowNs();
                if (lastRetryNs && now - lastRetryNs < 1000000000ull) { sleepMs(10); continue; }
                lastRetryNs = now;
                haveFrame = 0;
                if (!configureSink(&sink, w, h, fmt, fps)) { continue; }
                lastRetryNs = 0;
                if (stagingSize < sink.imageSize)
                {
                    unsigned char *grown = realloc(staging, sink.imageSize);
                    if (grown == NULL) { fprintf(stderr, "Out of memory\n"); break; }
                    staging     = grown;
                    stagingSize = sink.imageSize;
                }
                memset(staging, 0, sink.imageSize);
            }

            if (startReadingFromVideoBufferPointer(frame))
            {
                const unsigned char *pixels = getVideoFrameDataPointer(frame);
                uint64_t timestamp          = getVideoFrameTimestamp(frame);
                int stable = frame->is_populated && frame->generation == generation;
                int converted = 0;
                if (pixels != NULL && stable)
                {
                    converted = convertPixels(staging, fmt, pixels, w, h, channels, swapRB);
                    lastGeneration = generation;
                    lastTimestamp  = timestamp;
                }
                stopReadingFromVideoBufferPointer(frame);

                if (converted)
                {
                    haveFrame = 1;
                    if (writeFrame(&sink, staging, sink.imageSize))
                    {
                        lastWriteNs = nowNs();
                        if (++framesOut == 1) { fprintf(stderr, "First frame written\n"); }
                    } else
                    {
                        haveFrame = 0;
                        closeSink(&sink); // reopened and reconfigured with the next frame
                    }
                }
                continue; // look for the next frame right away
            }
        } else
        if (haveFrame && repeatNs && nowNs() - lastWriteNs >= repeatNs)
        {
            if (writeFrame(&sink, staging, sink.imageSize)) { lastWriteNs = nowNs(); } else { haveFrame = 0; closeSink(&sink); }
        }

        sleepMs(2);
    }

    fprintf(stderr, "Stopping after %lu frames\n", framesOut);
    closeSink(&sink);
    free(staging);
    return EXIT_SUCCESS;
}
