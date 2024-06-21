import cv2
import numpy as np
from SharedMemoryManager import SharedMemoryManager

def main(streamName):
    
    smm = SharedMemoryManager("libSharedMemoryVideoBuffers.so", 
                              descriptor = "video_frames.shm", 
                              frameName  = streamName)

    # Loop to continuously read frames 
    while True:
        # Capture frame-by-frame
        frame = smm.read_from_shared_memory()
        
        # Check if the frame is captured successfully
        if smm.frame_size==0:
            print("Error: Couldn't read frame from SHM")
            break
        
        # Display the frame in a window
        cv2.imshow('SharedMemoryVideoBuffer', frame)
        frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        
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

