/** @file screencapture.c
 *  @brief Example publisher: captures the X11 screen (or a region of it) and publishes it as an
 *  RGB stream ("screen" unless another name is given), until SIGINT or SIGTERM.
 *
 *  Together with v4l2sink this serves the desktop as a webcam:
 *
 *      ./screencapture -s screen &
 *      ./v4l2sink -s screen -d /dev/video43
 *
 *  (scripts/screen_to_webcam.sh does both.)
 *
 *  Usage: screencapture [options]
 *    -c, --context NAME   shared memory context (default video_frames.shm, created if missing)
 *    -s, --stream NAME    stream to publish (default screen)
 *    -g, --geometry WxH+X+Y  region to capture (default: the whole screen)
 *    -r, --fps N          capture rate (default 30)
 *    -D, --display NAME   X display (default $DISPLAY)
 *    -h, --help
 *
 *  Uses the MIT-SHM extension (XShmGetImage) when available, so the server writes the pixels
 *  straight into memory shared with us, and falls back to XGetImage otherwise (e.g. remote
 *  displays). On Wayland it captures only XWayland windows.
 *
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

//Can also be compiled using :
//gcc -O3 src/c/screencapture.c src/c/sharedMemoryVideoBuffers.c -lX11 -lXext -pthread -lrt -lm -o screencapture

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include "sharedMemoryVideoBuffers.h"

static volatile sig_atomic_t running = 1;
static void onSignal(int sig) { (void) sig; running = 0; }

static uint64_t nowNs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ull + (uint64_t) ts.tv_nsec;
}

/** @brief Set by onXError() so a failed XShmAttach() can fall back to XGetImage(). */
static int xErrorSeen = 0;
static int onXError(Display *d, XErrorEvent *e) { (void) d; (void) e; xErrorSeen = 1; return 0; }

/** @brief Position of the lowest set bit of a channel mask, and its width in bits. */
static void maskShift(unsigned long mask, int *shift, int *bits)
{
    *shift = 0; *bits = 0;
    if (mask == 0) { return; }
    while (!(mask & 1)) { mask >>= 1; (*shift)++; }
    while (mask & 1)    { mask >>= 1; (*bits)++; }
}

static inline unsigned char scaleTo8(unsigned long v, int bits)
{
    if (bits >= 8) { return (unsigned char) (v >> (bits - 8)); }
    return (unsigned char) ((v * 255u) / ((1u << bits) - 1u));
}

/** @brief Converts an XImage (any TrueColor layout) into packed RGB24. */
static void ximageToRGB(XImage *img, unsigned char *rgb)
{
    const int w = img->width, h = img->height;

    // Fast path: 32 bpp little-endian xRGB, what virtually every desktop uses
    if ((img->bits_per_pixel == 32) && (img->byte_order == LSBFirst) &&
        (img->red_mask == 0xff0000) && (img->green_mask == 0x00ff00) && (img->blue_mask == 0x0000ff))
    {
        for (int y = 0; y < h; y++)
        {
            const unsigned char *src = (const unsigned char *) img->data + (size_t) y * img->bytes_per_line;
            unsigned char *dst = rgb + (size_t) y * w * 3;
            for (int x = 0; x < w; x++, src += 4, dst += 3)
            {
                dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0];
            }
        }
        return;
    }

    // Generic path through XGetPixel and the channel masks
    int rs, rb, gs, gb, bs, bb;
    maskShift(img->red_mask,   &rs, &rb);
    maskShift(img->green_mask, &gs, &gb);
    maskShift(img->blue_mask,  &bs, &bb);
    for (int y = 0; y < h; y++)
    {
        unsigned char *dst = rgb + (size_t) y * w * 3;
        for (int x = 0; x < w; x++, dst += 3)
        {
            unsigned long p = XGetPixel(img, x, y);
            dst[0] = scaleTo8((p & img->red_mask)   >> rs, rb);
            dst[1] = scaleTo8((p & img->green_mask) >> gs, gb);
            dst[2] = scaleTo8((p & img->blue_mask)  >> bs, bb);
        }
    }
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -c, --context NAME      shared memory context (default video_frames.shm)\n"
        "  -s, --stream NAME       stream to publish (default screen)\n"
        "  -g, --geometry WxH+X+Y  region to capture (default: whole screen)\n"
        "  -r, --fps N             capture rate (default 30)\n"
        "  -D, --display NAME      X display (default $DISPLAY)\n"
        "  -h, --help\n", argv0);
}

int main(int argc, char *argv[])
{
    const char *shm_name    = "video_frames.shm";
    const char *stream_name = "screen";
    const char *displayName = NULL;
    const char *geometry    = NULL;
    double fps = 30.0;

    static const struct option longOpts[] =
    {
        { "context",  required_argument, 0, 'c' },
        { "stream",   required_argument, 0, 's' },
        { "geometry", required_argument, 0, 'g' },
        { "fps",      required_argument, 0, 'r' },
        { "display",  required_argument, 0, 'D' },
        { "help",     no_argument,       0, 'h' },
        { 0, 0, 0, 0 }
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "c:s:g:r:D:h", longOpts, NULL)) != -1)
    {
        switch (opt)
        {
            case 'c': shm_name    = optarg; break;
            case 's': stream_name = optarg; break;
            case 'g': geometry    = optarg; break;
            case 'D': displayName = optarg; break;
            case 'r':
                fps = strtod(optarg, NULL);
                if (!(fps > 0.0) || (fps > 1000.0)) { fprintf(stderr, "Invalid fps '%s'\n", optarg); return EXIT_FAILURE; }
                break;
            case 'h': usage(argv[0]); return EXIT_SUCCESS;
            default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }

    Display *dpy = XOpenDisplay(displayName);
    if (!dpy) { fprintf(stderr, "Cannot open X display %s\n", displayName ? displayName : "(DISPLAY)"); return EXIT_FAILURE; }
    int screen  = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    Visual *vis = DefaultVisual(dpy, screen);
    int depth   = DefaultDepth(dpy, screen);
    if (vis->class != TrueColor) { fprintf(stderr, "Only TrueColor displays are supported\n"); XCloseDisplay(dpy); return EXIT_FAILURE; }

    // Capture region, clipped to the screen
    int sw = DisplayWidth(dpy, screen), sh = DisplayHeight(dpy, screen);
    int rx = 0, ry = 0, rw = sw, rh = sh;
    if (geometry)
    {
        unsigned int gw = 0, gh = 0;
        int mask = XParseGeometry(geometry, &rx, &ry, &gw, &gh);
        if (mask & WidthValue)  { rw = (int) gw; }
        if (mask & HeightValue) { rh = (int) gh; }
    }
    if (rx < 0) { rx = 0; }
    if (ry < 0) { ry = 0; }
    if (rx + rw > sw) { rw = sw - rx; }
    if (ry + rh > sh) { rh = sh - ry; }
    // Even sizes: v4l2sink's default YUYV/YU12 output pairs pixels
    rw &= ~1; rh &= ~1;
    if ((rw <= 0) || (rh <= 0)) { fprintf(stderr, "Empty capture region\n"); XCloseDisplay(dpy); return EXIT_FAILURE; }

    // Try MIT-SHM, fall back to plain XGetImage
    XImage *img = NULL;
    XShmSegmentInfo shmInfo;
    memset(&shmInfo, 0, sizeof(shmInfo));
    shmInfo.shmid = -1;
    int useShm = XShmQueryExtension(dpy);
    if (useShm)
    {
        img = XShmCreateImage(dpy, vis, depth, ZPixmap, NULL, &shmInfo, rw, rh);
        if (img) { shmInfo.shmid = shmget(IPC_PRIVATE, (size_t) img->bytes_per_line * img->height, IPC_CREAT | 0600); }
        if (img && (shmInfo.shmid >= 0))
        {
            shmInfo.shmaddr = img->data = shmat(shmInfo.shmid, NULL, 0);
            shmInfo.readOnly = False;
            XErrorHandler old = XSetErrorHandler(onXError);
            xErrorSeen = 0;
            if ((shmInfo.shmaddr == (char *) -1) || !XShmAttach(dpy, &shmInfo)) { xErrorSeen = 1; }
            XSync(dpy, False);
            XSetErrorHandler(old);
            // Removed now: the segment lives on until both we and the X server detach
            shmctl(shmInfo.shmid, IPC_RMID, NULL);
            if (xErrorSeen)
            {
                if (shmInfo.shmaddr != (char *) -1) { shmdt(shmInfo.shmaddr); }
                img->data = NULL;
                XDestroyImage(img);
                img = NULL;
                useShm = 0;
            }
        }
        else
        {
            if (shmInfo.shmid >= 0) { shmctl(shmInfo.shmid, IPC_RMID, NULL); }
            if (img) { XDestroyImage(img); img = NULL; }
            useShm = 0;
        }
    }
    fprintf(stderr, "Capturing %dx%d+%d+%d at %.1f fps (%s) -> stream '%s'\n",
            rw, rh, rx, ry, fps, useShm ? "MIT-SHM" : "XGetImage", stream_name);

    // Shared memory stream
    if (createSharedMemoryContextDescriptor(shm_name) == -1) { fprintf(stderr, "Could not create context %s\n", shm_name); return EXIT_FAILURE; }
    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(shm_name);
    if (!context) { return EXIT_FAILURE; }
    if (createVideoFrameMetaData(context, stream_name, (unsigned int) rw, (unsigned int) rh, 3) != EXIT_SUCCESS)
    {
        fprintf(stderr, "Could not create stream %s at %dx%d:3\n", stream_name, rw, rh);
        return EXIT_FAILURE;
    }
    struct VideoFrame *frame = getVideoBufferPointer(context, stream_name);
    if (!frame || !map_frame_shared_memory(frame, 1)) { return EXIT_FAILURE; }

    size_t frameSize = (size_t) rw * rh * 3;
    unsigned char *rgb = malloc(frameSize);
    if (!rgb) { fprintf(stderr, "Out of memory\n"); return EXIT_FAILURE; }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onSignal;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    const uint64_t period = (uint64_t) (1e9 / fps);
    uint64_t next = nowNs();
    uint64_t statStart = next;
    unsigned int statFrames = 0;
    while (running)
    {
        int ok;
        if (useShm) { ok = XShmGetImage(dpy, root, img, rx, ry, AllPlanes); }
        else
        {
            if (img) { XDestroyImage(img); }
            img = XGetImage(dpy, root, rx, ry, (unsigned int) rw, (unsigned int) rh, AllPlanes, ZPixmap);
            ok = (img != NULL);
        }

        if (ok)
        {
            // Convert outside the writer lock, so readers are blocked only for the memcpy
            ximageToRGB(img, rgb);
            if (startWritingToVideoBufferPointer(frame))
            {
                copy_to_shared_memory(frame, rgb, frameSize, 0);
                stopWritingToVideoBufferPointer(frame);
                statFrames++;
            }
        }

        uint64_t now = nowNs();
        if (now - statStart >= 5000000000ull)
        {
            fprintf(stderr, "%.1f fps\n", statFrames * 1e9 / (double) (now - statStart));
            statStart = now; statFrames = 0;
        }
        next += period;
        if (next > now) { usleep((useconds_t) ((next - now) / 1000)); }
        else            { next = now; } // fell behind: don't try to catch up
    }

    free(rgb);
    if (useShm) { XShmDetach(dpy, &shmInfo); XSync(dpy, False); shmdt(shmInfo.shmaddr); img->data = NULL; }
    if (img) { XDestroyImage(img); }
    XCloseDisplay(dpy);
    destroyVideoFrame(context, stream_name);
    fprintf(stderr, "Done..\n");
    return EXIT_SUCCESS;
}
