/** @file viewer.c
 *  @brief  Example X11 viewer: shows the frames of a stream ("stream1" unless another name is
 *  given as the first argument) in a window, until a key is pressed or the window is closed.
 *
 *  Usage: viewer [stream_name]
 *
 *  - 3 channel frames are shown as RGB.
 *  - 1 channel frames are shown as grayscale.
 *  - Frames with any other number of channels (2, 4 or more) are shown as grayscale, each pixel
 *    being the average of its channels.
 *
 *  The window follows the size of the stream, including when the stream is re-created with another
 *  size. Creates the "video_frames.shm" context if needed and waits for the stream to appear, so it
 *  can be started before the publisher. Needs a TrueColor display.
 *
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include "sharedMemoryVideoBuffers.h"

/** @brief Largest frame width or height the viewer displays. */
#define MAX_VIEWER_DIMENSION 16384

/**
 * @brief Scales an 8-bit sample into the bits of a TrueColor channel mask.
 * @param sample Sample value, 0 to 255.
 * @param mask Channel mask of the visual (e.g. XImage::red_mask).
 * @return The sample, shifted into the mask's bits.
 */
static unsigned long sampleToMask(unsigned char sample, unsigned long mask)
{
    if (mask == 0) { return 0; }
    int shift = __builtin_ctzl(mask);       // lowest bit of the channel
    int bits  = __builtin_popcountl(mask);  // width of the channel
    unsigned long value = (bits >= 8) ? ((unsigned long) sample << (bits - 8)) : ((unsigned long) sample >> (8 - bits));
    return (value << shift) & mask;
}

/**
 * @brief Draws a frame into an XImage of the same size.
 * @param image TrueColor ZPixmap image of width x height pixels.
 * @param pixels width*height*channels bytes, channels interleaved.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param channels Bytes per pixel, at least 1: 3 is RGB, 1 is grayscale, any other count is
 * averaged into grayscale.
 */
static void convertFrameToXImage(XImage *image, const unsigned char *pixels, unsigned int width, unsigned int height, unsigned int channels)
{
    // Every sample value's bits in each channel, computed once instead of for every pixel
    unsigned long redTable[256], greenTable[256], blueTable[256];
    for (unsigned int sample = 0; sample < 256; sample++)
    {
        redTable[sample]   = sampleToMask((unsigned char) sample, image->red_mask);
        greenTable[sample] = sampleToMask((unsigned char) sample, image->green_mask);
        blueTable[sample]  = sampleToMask((unsigned char) sample, image->blue_mask);
    }
    // 24 and 32 bit pixels are written byte by byte, other sizes through the much slower XPutPixel()
    unsigned int bytesPerPixel = ((image->bits_per_pixel == 24) || (image->bits_per_pixel == 32)) ? (unsigned int) image->bits_per_pixel / 8 : 0;
    int lsbFirst = (image->byte_order == LSBFirst);

    for (unsigned int y = 0; y < height; y++)
    {
        const unsigned char *p = pixels + ((size_t) y * width * channels);
        unsigned char *row = (unsigned char *) image->data + ((size_t) y * (size_t) image->bytes_per_line);
        for (unsigned int x = 0; x < width; x++)
        {
            unsigned char r, g, b;
            if (channels == 3)
            {
                r = p[0]; g = p[1]; b = p[2];
            } else
            if (channels == 1)
            {
                r = g = b = p[0];
            } else
            {
                unsigned long sum = 0;
                for (unsigned int c = 0; c < channels; c++) { sum += p[c]; }
                r = g = b = (unsigned char) (sum / channels);
            }
            unsigned long pixel = redTable[r] | greenTable[g] | blueTable[b];
            if (bytesPerPixel == 0)
            {
                XPutPixel(image, (int) x, (int) y, pixel);
            } else
            {
                unsigned char *destination = row + ((size_t) x * bytesPerPixel);
                for (unsigned int i = 0; i < bytesPerPixel; i++)
                {
                    destination[lsbFirst ? i : (bytesPerPixel - 1 - i)] = (unsigned char) (pixel >> (8 * i));
                }
            }
            p += channels;
        }
    }
}

/**
 * @brief Creates a black ZPixmap image for the display's visual.
 * @param display X display.
 * @param visual TrueColor visual of the window.
 * @param depth Depth of the window.
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @return The image (free it with XDestroyImage(), which also frees its pixels), or NULL on failure.
 */
static XImage * createImage(Display *display, Visual *visual, int depth, unsigned int width, unsigned int height)
{
    // bytes_per_line 0: Xlib computes it from the depth and the 32 bit padding
    XImage *image = XCreateImage(display, visual, (unsigned int) depth, ZPixmap, 0, NULL, width, height, 32, 0);
    if (image == NULL) { return NULL; }
    image->data = (char *) calloc((size_t) image->bytes_per_line, height);
    if (image->data == NULL)
    {
        XDestroyImage(image);
        return NULL;
    }
    return image;
}

/**
 * @brief Shows a stream until a key is pressed or the window is closed.
 * @param argc Number of arguments.
 * @param argv argv[1], if given, is the name of the stream to show (default "stream1").
 * @return EXIT_SUCCESS when closed, EXIT_FAILURE on setup errors.
 */
int main(int argc, char *argv[])
{
    const char *shm_name    = "video_frames.shm";
    const char *stream_name = (argc > 1) ? argv[1] : "stream1";

    // Open connection to the X server
    Display *display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        fprintf(stderr, "Cannot open display\n");
        return EXIT_FAILURE;
    }

    int screen     = DefaultScreen(display);
    Visual *visual = DefaultVisual(display, screen);
    int depth      = DefaultDepth(display, screen);
    // Pixels are composed from the visual's red/green/blue masks, which only TrueColor visuals have
    if (visual->class != TrueColor)
    {
        fprintf(stderr, "The display isn't TrueColor, can't show frames\n");
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }

    // Create the context if nobody did yet (an existing one is kept), so the viewer can start first
    if (createSharedMemoryContextDescriptor(shm_name) == -1)
    {
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }

    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(shm_name);
    if (!context)
    {
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }

    // Window size until the first frame arrives
    unsigned int width  = 640;
    unsigned int height = 480;

    // Create the window
    Window window = XCreateSimpleWindow(display,
                                        RootWindow(display, screen),
                                        10, 10,
                                        width, height, 1,
                                        BlackPixel(display, screen),
                                        BlackPixel(display, screen));
    XStoreName(display, window, stream_name);

    // Ask the window manager for a ClientMessage instead of killing the connection when the window is closed
    Atom deleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &deleteWindow, 1);

    // Select input events
    XSelectInput(display, window, ExposureMask | KeyPressMask);

    // Map (show) the window
    XMapWindow(display, window);

    // Create a graphics context
    GC gc = XCreateGC(display, window, 0, NULL);

    XImage *image = createImage(display, visual, depth, width, height);
    if (image == NULL)
    {
        fprintf(stderr, "Failed to create XImage\n");
        XFreeGC(display, gc);
        XCloseDisplay(display);
        return EXIT_FAILURE;
    }

    uint64_t shownGeneration = 0; // stream incarnation of the last frame read, 0 = none
    uint64_t shownTimestamp  = 0; // timestamp of the last frame read

    // Main event loop
    int running = 1;
    while (running)
    {
        int redraw = 0;

        // Check for events
        while (XPending(display))
        {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == Expose)
            {
                redraw = 1;
            } else
            if (event.type == KeyPress)
            {
                running = 0; // Exit on key press
            } else
            if ((event.type == ClientMessage) && ((Atom) event.xclient.data.l[0] == deleteWindow))
            {
                running = 0; // Window closed
            }
        }

        // Looked up every iteration: NULL until the stream exists, and it may be destroyed and re-created
        struct VideoFrame *frame = getVideoBufferPointer(context, stream_name);
        if ( (frame != NULL) && (getVideoFrameTimestamp(frame) != 0) &&
             ((frame->generation != shownGeneration) || (getVideoFrameTimestamp(frame) != shownTimestamp)) )
        {
            // Snapshot the layout. It only matches the pixels the read maps if the stream keeps
            // its generation (and stays populated) from here until the read has started.
            int populated          = frame->is_populated;
            uint64_t generation    = frame->generation;
            unsigned int w         = frame->width;
            unsigned int h         = frame->height;
            unsigned int channels  = frame->channels;
            size_t frameSize       = frame->frame_size;

            if (startReadingFromVideoBufferPointer(frame))
            {
                const unsigned char *pixels = getVideoFrameDataPointer(frame); // valid until stopReadingFromVideoBufferPointer()
                uint64_t timestamp          = getVideoFrameTimestamp(frame);
                int layoutStable = populated && frame->is_populated && (frame->generation == generation);

                if ((pixels != NULL) && layoutStable)
                {
                    int displayable = (w >= 1) && (w <= MAX_VIEWER_DIMENSION) && (h >= 1) && (h <= MAX_VIEWER_DIMENSION) &&
                                      (channels >= 1) && ((size_t) w * h <= frameSize / channels);
                    if (!displayable)
                    {
                        if (generation != shownGeneration) { fprintf(stderr, "Can't display a %ux%u:%u frame\n", w, h, channels); }
                    } else
                    {
                        if ((w != width) || (h != height))
                        {
                            // The stream changed size: follow it
                            XImage *resized = createImage(display, visual, depth, w, h);
                            if (resized != NULL)
                            {
                                XDestroyImage(image);
                                image  = resized;
                                width  = w;
                                height = h;
                                XResizeWindow(display, window, width, height);
                            }
                        }
                        if ((w == width) && (h == height))
                        {
                            convertFrameToXImage(image, pixels, width, height, channels);
                            redraw = 1;
                        }
                    }
                    shownGeneration = generation;
                    shownTimestamp  = timestamp;
                }
                stopReadingFromVideoBufferPointer(frame);
            }
        }

        if (redraw)
        {
            // Display the updated image
            XPutImage(display, window, gc, image, 0, 0, 0, 0, width, height);
            XFlush(display);
        }

        // Poll for new frames and events about 100 times a second
        usleep(10000);
    }

    // Free resources
    XDestroyImage(image);
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);

    return EXIT_SUCCESS;
}
