# HTTP Multipart MJPEG downloader .. 
# Edited version to work with HTTP Multipart MJPEG
# Based on https://github.com/sglvladi/TensorFlowObjectDetectionTutorial

#pip install numpy opencv-python --user
import time
import numpy as np
import os
import six.moves.urllib as urllib
import sys
#import tarfile
#import zipfile
import cv2
from SharedMemoryManager import SharedMemoryManager

 
# Helper code
def load_image_into_numpy_array(image):
    (im_width, im_height) = image.size
    return np.array(image.getdata()).reshape((im_height, im_width, 3)).astype(np.uint8)


class ESP32CamStreamer():
  def __init__(self,
               url        = "http://192.168.1.119:80",
               timeout    = 32,
               readBuffer = 2048,
               max_retries = 10
              ):
      self.url     = url
      self.timeout = timeout
      print("urlopen ", end="")
      self.stream  = urllib.request.urlopen(url,timeout=timeout)
      self.should_stop = False
      self.bytebuffer  = bytes()
      self.readBuffer  = readBuffer
      self.max_retries = max_retries
      self.retry_delay = 1  # Initial delay between retries
      self.image_np    = None

  def isOpened(self):
      return True

  def release(self):  
      del self.stream
      del self.should_stop
      del self.bytebuffer

  def decodeBuffer(self,bytebuffer):
                a = self.bytebuffer.find(b'\xff\xd8')
                b = self.bytebuffer.find(b'\xff\xd9')
                if a != -1 and b != -1:
                    jpg = bytebuffer[a:b+2]
                    self.bytebuffer = bytebuffer[b+2:]
                    try:
                       return cv2.imdecode(np.frombuffer(jpg, dtype=np.uint8), cv2.IMREAD_COLOR)
                    except:
                       print("Error decoding buffer")
                return None
 
  def read(self):
        # Read frame from camera
        retries = 0
        success = False
        while not success and retries < self.max_retries: 
            try:
                while True: 
                    self.lastread = self.stream.read(self.readBuffer)
                    self.bytebuffer += self.lastread
                    self.image_np = self.decodeBuffer(self.bytebuffer)
                    if self.image_np is not None:
                        success = True
                        return success, self.image_np

                    if not self.lastread:
                        return success, self.image_np
            except Exception as err:
                print('Exception while reading from ESP32:', err)
                retries += 1
                if retries < self.max_retries:
                    print('Retrying in', self.retry_delay, 'seconds...')
                    time.sleep(self.retry_delay)
                    self.retry_delay *= 2  # Exponential backoff
                    print("urlopen ", end="")
                    self.stream = urllib.request.urlopen(self.url, timeout=self.timeout)
                else:
                    print('Max retries exceeded, giving up.')
        return success, self.image_np

  def visualize(
                self,
                windowname='ESP32 Stream',
                width=800,
                height=600
               ): 
            if self.image_np is not None: 
              # Display output
              cv2.imshow(windowname, cv2.resize(self.image_np,(width,height)))

              key = cv2.waitKey(1) & 0xFF
              if key == ord('q'):
                cv2.destroyAllWindows()
                self.should_stop = True
                #break

if __name__ == '__main__':
     streamName = "stream2"
     source ="http://192.168.1.119:80"
     if (len(sys.argv)>1):
         source = "http://%s:80" % sys.argv[1] 
     if (len(sys.argv)>2):
         streamName = sys.argv[2] 

     cap = ESP32CamStreamer(url = source)

     ret, frame = cap.read()
     smm = SharedMemoryManager("libSharedMemoryVideoBuffers.so", 
                               descriptor = "video_frames.shm", 
                               frameName  = streamName, 
                               width      = frame.shape[1],
                               height     = frame.shape[0],
                               channels   = frame.shape[2])

     while not cap.should_stop:
       ret, frame = cap.read()
       smm.copy_numpy_to_shared_memory(frame)
       cap.visualize()

