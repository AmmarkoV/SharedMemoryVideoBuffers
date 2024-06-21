#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define WIDTH  800
#define HEIGHT 600

void update_image(unsigned char *buffer, int width, int height, int frame) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int offset = (y * width + x) * 3;
            buffer[offset + 0] = (x + frame) % 256; // Red
            buffer[offset + 1] = (y + frame) % 256; // Green
            buffer[offset + 2] = (x + y + frame) % 256; // Blue
        }
    }
}

int main() {
    Display *display;
    Window window;
    XEvent event;
    int screen;
    GC gc;
    XImage *ximage;
    unsigned char *image_data;
    int frame = 0;

    // Open connection to the X server
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }

    screen = DefaultScreen(display);

    // Create the window
    window = XCreateSimpleWindow(display, RootWindow(display, screen), 10, 10, WIDTH, HEIGHT, 1,
                                 BlackPixel(display, screen), WhitePixel(display, screen));

    // Select input events
    XSelectInput(display, window, ExposureMask | KeyPressMask);

    // Map (show) the window
    XMapWindow(display, window);

    // Create a graphics context
    gc = XCreateGC(display, window, 0, NULL);

    // Allocate memory for the image
    image_data = (unsigned char *)malloc(WIDTH * HEIGHT * 3);

    // Create the XImage structure
    ximage = XCreateImage(display, DefaultVisual(display, screen), 24, ZPixmap, 0,
                          (char *)image_data, WIDTH, HEIGHT, 32, 0);

    // Main event loop
    while (1) {
        // Check for events
        while (XPending(display)) {
            XNextEvent(display, &event);
            if (event.type == Expose) {
                // Redraw the image
                XPutImage(display, window, gc, ximage, 0, 0, 0, 0, WIDTH, HEIGHT);
            } else if (event.type == KeyPress) {
                // Exit on key press
                XCloseDisplay(display);
                free(image_data);
                exit(0);
            }
        }

        // Update the image buffer
        update_image(image_data, WIDTH, HEIGHT, frame);

        // Display the updated image
        XPutImage(display, window, gc, ximage, 0, 0, 0, 0, WIDTH, HEIGHT);

        // Increment frame counter
        frame++;

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

