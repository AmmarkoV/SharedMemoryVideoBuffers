#pip install numpy opencv-python --user

import numpy as np
import os
import sys
import cv2

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
    streamName = "stream3"
    targetWidth  = 800
    targetHeight = 600
    loop = True  # Set this to False if looping is not desired


    source = "./"
    if len(sys.argv) > 1:
        source = sys.argv[1]
    if len(sys.argv) > 2:
        streamName = sys.argv[2]

    # Open the video source
    cap = cv2.VideoCapture(source)
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
            break

    cap.release()
    cv2.destroyAllWindows()
