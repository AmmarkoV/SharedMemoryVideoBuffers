# Required libraries
# pip install numpy opencv-python --user

import numpy as np
import cv2
import time
import sys
from SharedMemoryManager import SharedMemoryManager

def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

def sobel_filter(image):
    """
    Apply Sobel edge detection to an image.
    """
    gray = cv2.cvtColor(image, cv2.COLOR_RGB2GRAY)
    sobelx = cv2.Sobel(gray, cv2.CV_64F, 1, 0, ksize=3)
    sobely = cv2.Sobel(gray, cv2.CV_64F, 0, 1, ksize=3)
    sobel_combined = cv2.sqrt(sobelx**2 + sobely**2)
    gray = cv2.convertScaleAbs(sobel_combined)

    image = cv2.cvtColor(gray, cv2.COLOR_GRAY2RGB)
    return image

if __name__ == '__main__':
    input_stream_name = "dance"
    output_stream_name = "dance_output"
    
    # Shared memory descriptors and dimensions
    shm_descriptor = "video_frames.shm"  # shared memory identifier
    shm_descriptor_out = "depth_frames.shm"  # shared memory identifier
    width, height, channels = 800, 600, 3  # default dimensions

    # Initialize shared memory managers for input and output
    smm_input = SharedMemoryManager("libSharedMemoryVideoBuffers.so",
                                    descriptor=shm_descriptor,
                                    frameName=input_stream_name,
                                    width=width,
                                    height=height,
                                    channels=channels)

    smm_output = SharedMemoryManager("libSharedMemoryVideoBuffers.so",
                                     descriptor=shm_descriptor_out,
                                     frameName=output_stream_name,
                                     width=width,
                                     height=height,
                                     channels=1)  # Output is grayscale
    
    while True:
        # Receive frame from shared memory
        frame = smm_input.read_from_shared_memory()
        if frame is None:
            eprint("Error: Could not read frame from shared memory")
            time.sleep(0.03)  # Prevent excessive polling
            continue
        
        # Process frame with Sobel filter
        sobel_frame = sobel_filter(frame)
        
        # Display the Sobel-processed frame (optional)
        cv2.imshow('Sobel Output', sobel_frame)

        # Send processed frame to output shared memory
        smm_output.copy_numpy_to_shared_memory(sobel_frame)

        # Press 'q' to exit
        if cv2.waitKey(1) & 0xff == ord('q'):
            break

    cv2.destroyAllWindows()

