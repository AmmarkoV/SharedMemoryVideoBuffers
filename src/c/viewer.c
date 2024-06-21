#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include "sharedMemoryVideoBuffers.h"

void update_image(unsigned char * fbuffer, int width, int height, int frameNumber)
{
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int offset = (y * width * 3) +  (x * 3);
            fbuffer[offset + 0] = (x + frameNumber) % 256; // Red
            fbuffer[offset + 1] = (y + frameNumber) % 256; // Green
            fbuffer[offset + 2] = (x + y + frameNumber) % 256; // Blue
        }
    }
}

int main()
{
    Display *display;
    Window window;
    XEvent event;
    int screen;
    GC gc;
    XImage *ximage;
    unsigned char *image_data;
    int frameNumber = 0;

    // Open connection to the X server
    display = XOpenDisplay(NULL);
    if (display == NULL)
    {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }

    screen = DefaultScreen(display);

    //Dimensions
    int WIDTH  = 800;
    int HEIGHT = 600;


    //------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------

    const char *shm_name    = "video_frames.shm";
    const char *stream_name = "stream1";
    // Client process
    if (createSharedMemoryContextDescriptor(shm_name) == -1)
    {
        return EXIT_FAILURE;
    }

    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(shm_name);
    if (!context)
    {
        return EXIT_FAILURE;
    }

    createVideoFrameMetaData(context,stream_name,640,480,3);

    struct VideoFrame *frame = getVideoBufferPointer(context,stream_name);
    if (!frame)
    {
        return EXIT_FAILURE;
    }

    struct VideoFrameLocalMapping localMap={0};
    if (map_frame_shared_memory(frame,1) == NULL)  //We want to overwrite the frame->data because we are the client and this makes the python API easier
    {
        return EXIT_FAILURE;
    }

    printSharedMemoryContextState(context);

    fprintf(stderr,"Read %lu bytes of dummy data\n",frame->frame_size);
    // Example to read from buffer (Client)
    if (startReadingFromVideoBufferPointer(frame))
    {
        unsigned char *buffer = (unsigned char*)malloc(frame->frame_size);
        if (buffer!=0)
        {
         memcpy(buffer, frame->client_address_space_data_pointer, frame->frame_size);
         stopReadingFromVideoBufferPointer(frame);
         free(buffer);

         WIDTH   = frame->width;
         HEIGHT  = frame->height;
        } else
        {
         fprintf(stderr,"Failed reading back dummy data..\n");
        }
    }
    //------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------

    // Create the window
    window = XCreateSimpleWindow(display,
                                 RootWindow(display, screen),
                                 10, 10,
                                 WIDTH, HEIGHT, 1,
                                 BlackPixel(display, screen),
                                 WhitePixel(display, screen));


    // Select input events
    XSelectInput(display, window, ExposureMask | KeyPressMask);

    // Map (show) the window
    XMapWindow(display, window);

    // Create a graphics context
    gc = XCreateGC(display, window, 0, NULL);

    // Allocate memory for the image
    image_data = (unsigned char *) malloc(WIDTH * HEIGHT * 3);
    memset(image_data,0,WIDTH*HEIGHT*3);


    // Calculate bytes_per_line
    int offset = 24;
    int bitmap_pad = 32;
    int bytes_per_line = 0;//WIDTH * 3;

    // Create the XImage structure
    ximage = XCreateImage(display, DefaultVisual(display, screen), offset, ZPixmap, 0,
                          (char *)image_data, WIDTH, HEIGHT, bitmap_pad, bytes_per_line);

    if (ximage == NULL) {
        fprintf(stderr, "Failed to create XImage\n");
        free(image_data);
        XFreeGC(display, gc);
        XCloseDisplay(display);
        exit(1);
    }


  // Main event loop
    while (1)
        {
        // Check for events
        while (XPending(display))
        {
            XNextEvent(display, &event);
            if (event.type == Expose)
            {
                // Redraw the image
                XPutImage(display, window, gc, ximage, 0, 0, 0, 0, WIDTH, HEIGHT);
            } else
            if (event.type == KeyPress)
            {
                // Exit on key press
                XCloseDisplay(display);
                free(image_data);
                exit(0);
            }
        }

        // Update the image buffer
        update_image(image_data, WIDTH, HEIGHT, frameNumber);

        /*
    fprintf(stderr,"Read %lu bytes of dummy data\n",frame->frame_size);
    // Example to read from buffer (Client)
    if (startReadingFromVideoBufferPointer(frame))
    {
        if (image_data!=0)
        {
         memcpy(image_data, frame->client_address_space_data_pointer, frame->frame_size);
         stopReadingFromVideoBufferPointer(frame);

         WIDTH   = frame->width;
         HEIGHT  = frame->height;
        } else
        {
         fprintf(stderr,"Failed reading back dummy data..\n");
        }
    }*/


        // Display the updated image
        XPutImage(display, window, gc, ximage, 0, 0, 0, 0, WIDTH, HEIGHT);

        // Increment frame counter
        frameNumber++;

        // Sleep for a short while to control the update rate
        usleep(30000); // 30ms delay (about 33 frames per second)
    }

    // Free resources (not reached in this example)
    XDestroyImage(ximage);
    XFreeGC(display, gc);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    free(image_data);

    return 0;
}

