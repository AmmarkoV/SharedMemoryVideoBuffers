import cv2
import numpy as np
from SharedMemoryManager import SharedMemoryManager

def main(streamName):
    # Open the first webcam connected to the computer
    cap = cv2.VideoCapture(0)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH,  800)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 600)
    
    # Check if the webcam is opened successfully
    if not cap.isOpened():
        print("Error: Couldn't open webcam")
        return
    
    ret, frame = cap.read()
    smm = SharedMemoryManager("libSharedMemoryVideoBuffers.so", 
                              descriptor = "video_frames.shm", 
                              frameName  = streamName, 
                              width      = frame.shape[1],
                              height     = frame.shape[0],
                              channels   = frame.shape[2])

    # Loop to continuously read frames from the webcam
    while True:
        # Capture frame-by-frame
        ret, frame = cap.read()
        
        # Check if the frame is captured successfully
        if not ret:
            print("Error: Couldn't read frame from webcam")
            break
        
        # Display the frame in a window
        cv2.imshow('Webcam', frame)
        frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        #print("frame:",frame)

        smm.copy_numpy_to_shared_memory(frame)

        # Break the loop if 'q' is pressed
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break
    
    # Release the webcam and close all OpenCV windows
    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    import sys
    streamName = "stream1"
    if len(sys.argv) != 2 :
        print("\n\nYou did not supply a stream name, assuming ",streamName) 
    else:
        streamName = sys.argv[1]

    main(streamName)

