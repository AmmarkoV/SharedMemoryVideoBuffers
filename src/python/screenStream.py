import sys
import time
import numpy as np
import cv2
from PIL import ImageGrab

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
            self.image_np = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        return success, self.image_np

    def visualize(self, windowname='Screen Grab', width=800, height=600):
        if self.image_np is not None:
            cv2.imshow(windowname, cv2.resize(self.image_np, (width, height)))

            key = cv2.waitKey(1) & 0xFF
            if key == ord('q'):
                cv2.destroyAllWindows()
                self.should_stop = True

if __name__ == '__main__':
    region = None  # Define a specific region if needed (e.g., (x, y, w, h))
    if len(sys.argv) > 1:
        region = tuple(map(int, sys.argv[1:5]))  # If region is provided via command line

    cap = ScreenGrabber(region=region)
    
    while not cap.should_stop:
        cap.read()
        cap.visualize()
        time.sleep(cap.capture_interval)
    
    cap.release()

