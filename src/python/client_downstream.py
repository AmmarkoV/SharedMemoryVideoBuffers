import time
import cv2
from SharedMemoryManager import SharedMemoryManager

def main(streamName):
    
    smm = SharedMemoryManager("libSharedMemoryVideoBuffers.so", 
                              descriptor = "video_frames.shm", 
                              frameName  = streamName,
                              connect    = True)

    lastTimestamp = None   # timestamp of the frame on screen, so it isn't converted and shown again
    reportedError = False  # report a stream that can't be read once, not on every poll

    # Loop to continuously read frames 
    while True:
        frame = None
        # Zero-copy read: the view is only valid inside the block, so it is converted in there
        # (cvtColor makes its own copy) instead of being copied first
        with smm.read_frame() as view:
            if (view is None) or (smm.frame_size==0):
                if not reportedError:
                    print("Error: Couldn't read frame from SHM")
                    reportedError = True
            elif (smm.unix_timestamp != lastTimestamp):
                reportedError = False
                lastTimestamp = smm.unix_timestamp
                if (view.shape[2]==4):
                   frame = cv2.cvtColor(view, cv2.COLOR_RGBA2GRAY)
                elif (view.shape[2]==3):
                   frame = cv2.cvtColor(view, cv2.COLOR_BGR2RGB)
                else:
                   frame = view.copy()

        if frame is not None:
           # Display the frame in a window
           cv2.imshow('SharedMemoryVideoBuffer', frame)
        else:
           time.sleep(0.001) # no new frame yet: don't spin

        # Break the loop if 'q' is pressed
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break
    
    # Release the webcam and close all OpenCV windows
    cv2.destroyAllWindows()

if __name__ == "__main__":
    import sys
    streamName = "stream1"
    if len(sys.argv) != 2 :
        print("\n\nYou did not supply a stream name, assuming ",streamName) 
    else:
        streamName = sys.argv[1]

    main(streamName)

