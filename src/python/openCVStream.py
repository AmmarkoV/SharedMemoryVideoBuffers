#pip install numpy opencv-python --user

import argparse
import numpy as np
import os
import re
import sys
import cv2
import time

from SharedMemoryManager import SharedMemoryManager

def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

def resize_with_padding(image, width, height, DO_PADDING=True, TINY_FLOAT=1e-5):
    """
    Resizes an image to the specified size,
    adding padding to preserve the aspect ratio.
    """
    shape_out = (height, width)
    if image.ndim == 3 and len(shape_out) == 2:
        shape_out = [*shape_out, 3]
    hw_out, hw_image = [np.array(x[:2]) for x in (shape_out, image.shape)]
    resize_ratio = np.min(hw_out / hw_image)
    hw_wk = (hw_image * resize_ratio + TINY_FLOAT).astype(int)

    # Resize the image
    resized_image = cv2.resize(
        image, tuple(hw_wk[::-1]), interpolation=cv2.INTER_NEAREST
    )
    if not DO_PADDING or np.all(hw_out == hw_wk):
        return resized_image

    # Create a black image with the target size
    padded_image = np.zeros(shape_out, dtype=np.uint8)
    
    # Calculate the number of rows/columns to add as padding
    dh, dw = (hw_out - hw_wk) // 2
    # Add the resized image to the padded image, with padding on the left and right sides
    padded_image[dh : hw_wk[0] + dh, dw : hw_wk[1] + dw] = resized_image

    return padded_image

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Publish a video file or camera to a shared memory stream")
    parser.add_argument("source", nargs="?", default="./", help="video path, camera index (0) or /dev/videoN")
    parser.add_argument("streamName", nargs="?", default="stream3", help="name of the published stream")
    parser.add_argument("--size", nargs=2, type=int, metavar=("W", "H"),
                        help="published size (default 800 600), also requested from a camera")
    parser.add_argument("--fps", type=float,
                        help="publishing rate (default: the source's), also requested from a camera")
    args = parser.parse_args()
    if (args.size is not None) and (min(args.size) <= 0):
        parser.error("--size values must be positive")
    if (args.fps is not None) and (args.fps <= 0):
        parser.error("--fps must be positive")

    streamName = args.streamName
    targetWidth, targetHeight = args.size if (args.size is not None) else (800, 600)
    sleepTimeMS  = 30 / 1000  # frame period (seconds) for sources that don't report their frame rate
    loop = True  # Set this to False if looping is not desired

    # OpenCV treats any string as a filename, so a camera has to be passed as an integer
    source = args.source
    cameraMatch = re.fullmatch(r'(?:/dev/video)?(\d+)', source)
    isCamera = (cameraMatch is not None)
    if isCamera:
        source = int(cameraMatch.group(1))

    # Open the video source
    cap = cv2.VideoCapture(source)
    if isCamera and ((args.size is not None) or (args.fps is not None)):
        # Most UVC webcams only offer high resolutions and framerates under MJPG
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))
        if args.size is not None:
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, targetWidth)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, targetHeight)
        if args.fps is not None:
            cap.set(cv2.CAP_PROP_FPS, args.fps)
    if not cap.isOpened():
        eprint("Error: Could not open video source")
        sys.exit(1)

    ret, frame = cap.read()
    if not ret:
        eprint("Error: Could not read frame from video source")
        cap.release()
        sys.exit(1)

    #Resize frame
    frame = resize_with_padding(frame, targetWidth, targetHeight)

    smm = SharedMemoryManager("libSharedMemoryVideoBuffers.so", 
                              descriptor = "video_frames.shm", 
                              frameName = streamName, 
                              width = frame.shape[1],
                              height = frame.shape[0],
                              channels = frame.shape[2])

    # Publish at the source's frame rate: sleep until the next frame is due, so the time spent on
    # each frame (decoding, resizing, showing, publishing) is part of the period, not added to it
    sourceFPS     = cap.get(cv2.CAP_PROP_FPS)
    if args.fps is not None:
        sourceFPS = args.fps
    framePeriod   = (1.0 / sourceFPS) if (sourceFPS > 0) else sleepTimeMS
    nextFrameTime = time.monotonic()

    while cap.isOpened():
        ret, frame = cap.read()
        #if not ret:
        #    eprint("Error: Could not read frame from video source")
        #    break
        if not ret:
            if loop:
                print("Looping Stream %s "%streamName)
                cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                continue
            else:
                eprint("Error: Could not read frame from video source")
                break


        #Resize frame
        frame = resize_with_padding(frame, targetWidth, targetHeight)

        # Display output
        cv2.imshow('Stream %s'%streamName, frame)

        #Pass to shared memory using RGB order instead of BGR
        frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        smm.copy_numpy_to_shared_memory(frame)

        #Accept Escape or Q to terminate this script
        if cv2.waitKey(1) & 0xff == ord('q'):
            print("Terminating after keyboard request")
            break

        if (framePeriod!=0):
            nextFrameTime += framePeriod
            delay = nextFrameTime - time.monotonic()
            if (delay > 0):
                time.sleep(delay)
            else:
                nextFrameTime = time.monotonic() # running late: carry on instead of rushing to catch up


    cap.release()
    cv2.destroyAllWindows()
