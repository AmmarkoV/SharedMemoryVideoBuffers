import sys
import time
import numpy as np
import cv2
from PIL import ImageGrab

from SharedMemoryManager import SharedMemoryManager

def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

class ScreenGrabber:
    def __init__(self, 
                 region=None, 
                 max_retries=10, 
                 retry_delay=1, 
                 capture_interval=0.033): # ~30 FPS
        self.region = region
        self.max_retries = max_retries
        self.retry_delay = retry_delay
        self.capture_interval = capture_interval
        self.should_stop = False
        self.image_np = None

    def isOpened(self):
        return True

    def release(self):
        self.should_stop = True

    def grab_screen(self):
        retries = 0
        while retries < self.max_retries:
            try:
                img = ImageGrab.grab(bbox=self.region) # bbox specifies specific region (bbox= x,y,width,height)
                return True, np.array(img)
            except Exception as err:
                print('Exception while capturing screen:', err)
                retries += 1
                if retries < self.max_retries:
                    print('Retrying in', self.retry_delay, 'seconds...')
                    time.sleep(self.retry_delay)
                else:
                    print('Max retries exceeded, giving up.')
                    return False, None

    def read(self):
        success, img = self.grab_screen()
        if success and img is not None:
            # PIL's ImageGrab already returns RGB (unlike cv2.VideoCapture, which
            # is BGR) - keep that order, matching what every other stream source
            # in this project publishes to shared memory.
            self.image_np = img
        return success, self.image_np

    def visualize(self, windowname='Screen Grab', width=800, height=600):
        if self.image_np is not None:
            # cv2.imshow expects BGR, so swap channels only for the local preview
            preview = cv2.cvtColor(self.image_np, cv2.COLOR_RGB2BGR)
            cv2.imshow(windowname, cv2.resize(preview, (width, height)))

            key = cv2.waitKey(1) & 0xFF
            if key == ord('q'):
                cv2.destroyAllWindows()
                self.should_stop = True

if __name__ == '__main__':
    streamName = "stream4"
    region = None  # Define a specific region if needed (e.g., (x, y, w, h))
    if len(sys.argv) > 1:
        region = tuple(map(int, sys.argv[1:5]))  # If region is provided via command line
    if len(sys.argv) > 5:
        streamName = sys.argv[5]

    cap = ScreenGrabber(region=region)

    ret, frame = cap.read()
    if not ret or frame is None:
        eprint("Error: Could not grab an initial screen frame")
        sys.exit(1)

    smm = SharedMemoryManager("libSharedMemoryVideoBuffers.so",
                              descriptor = "video_frames.shm",
                              frameName  = streamName,
                              width      = frame.shape[1],
                              height     = frame.shape[0],
                              channels   = frame.shape[2])

    while not cap.should_stop:
        ret, frame = cap.read()
        if not ret or frame is None:
            continue
        smm.copy_numpy_to_shared_memory(frame)
        cap.visualize()
        time.sleep(cap.capture_interval)

    cap.release()

