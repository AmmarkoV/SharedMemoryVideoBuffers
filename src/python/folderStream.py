#pip install numpy opencv-python --user

import numpy as np
import os
import sys
import cv2

from SharedMemoryManager import SharedMemoryManager
def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)

"""
Check if a file exists
"""
def checkIfFileExists(filename):
    return os.path.isfile(filename) 

def resize_with_padding(image, width, height, DO_PADDING=True, TINY_FLOAT=1e-5):
    """
    Resizes an image to the specified size,
    adding padding to preserve the aspect ratio.
    """
    shape_out=(height,width)
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

class FolderStreamer():
  def __init__(self,
               path       = "./",
               label      = "colorFrame_0_",
               width      = 0,
               height     = 0,
               loop       = True 
              ):
      self.path        = path
      self.label       = label
      self.frameNumber = 0
      self.width       = width
      self.height      = height
      self.should_stop = False
      self.loop        = loop

      # Pre-scan available frames to know how many exist
      self.available_frames = sorted([
            int(f.replace(label, "").split(".")[0])
            for f in os.listdir(path)
            if f.startswith(label) and (f.endswith(".jpg") or f.endswith(".png") or f.endswith(".pnm"))
        ])

      if not self.available_frames:
            eprint("No frames found in folder:", path)
      else:
            eprint(f"Found {len(self.available_frames)} frames, looping = {self.loop}")


  def isOpened(self):
      return not self.should_stop

  def release(self):
      print("Release Called ") 
      self.should_stop = True 

  def read(self):
            # Clamp or wrap frame index depending on loop mode
            if self.frameNumber >= len(self.available_frames):
              if self.loop:
                self.frameNumber = 0
              else:
                self.should_stop = True
                return False, None

            # Read frame from camera
            #----------------------------------------------------------------------
            filenameJPG = "%s/%s%05u.jpg" % (self.path,self.label,self.frameNumber)
            filenamePNG = "%s/%s%05u.png" % (self.path,self.label,self.frameNumber)
            filenamePNM = "%s/%s%05u.pnm" % (self.path,self.label,self.frameNumber)
            #----------------------------------------------------------------------
            try:
              if (checkIfFileExists(filenameJPG)):
                   self.img = cv2.imread(filenameJPG, cv2.IMREAD_UNCHANGED)
              elif (checkIfFileExists(filenamePNG)):  
                   self.img = cv2.imread(filenamePNG, cv2.IMREAD_UNCHANGED)
              elif (checkIfFileExists(filenamePNM)):  
                   self.img = cv2.imread(filenamePNM, cv2.IMREAD_UNCHANGED)
              else: 
                   eprint("Could not find ",filenameJPG," or ",filenamePNG," or ",filenamePNM)
                   self.img = None
            except Exception as e:
                   eprint("Failed to open ",filenameJPG," or ",filenamePNG," or ",filenamePNM)
                   eprint(e)
            #----------------------------------------------------------------------

            if not self.img is None:
                if (self.width != 0) and (self.height != 0):
                  #-------------------------- 
                  width  = self.img.shape[1]
                  height = self.img.shape[0]
                  eprint("Received Image size was ",width,"x",height, end = "")
                  #-------------------------- 
                  self.img = resize_with_padding(self.img, self.width, self.height)
                  #-------------------------- 
                  width  = self.img.shape[1]
                  height = self.img.shape[0]
                  eprint(" Rescaled Image size is ",width,"x",height)
                  #-------------------------- 

                success = True
               
                self.frameNumber+=1
            else:
                success = False
                self.should_stop = True 
             
            return success,self.img

  def visualize(
                self,
                windowname=None,
                width=800,
                height=600
               ): 
            # Display output
            if windowname is None:
               windowname='Folder Stream %s (scaled)' % self.path

            cv2.imshow(windowname, cv2.resize(self.img,(width,height)))

            if cv2.waitKey(10) & 0xff == ord('q'):
                cv2.destroyAllWindows()
                self.should_stop = True
                #break


if __name__ == '__main__':
     streamName = "stream3"
     source ="./"
     if (len(sys.argv)>1):
         source = sys.argv[1] 
     if (len(sys.argv)>2):
         streamName = sys.argv[2] 

     cap = FolderStreamer(path = source)#,width = 800,height = 600)

     ret, frame = cap.read()

     width      = frame.shape[1]
     height     = frame.shape[0]
     channels   = 1
     if (len(frame.shape)>2):
          channels   = frame.shape[2]

     smm = SharedMemoryManager("libSharedMemoryVideoBuffers.so", 
                               descriptor = "video_frames.shm", 
                               frameName  = streamName, 
                               width      = width,
                               height     = height,
                               channels   = channels)

     while not cap.should_stop:
       ret, frame = cap.read()

       if (channels==3):
          frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
       smm.copy_numpy_to_shared_memory(frame)
       cap.visualize()


     # Release the stream
     cap.release() 
