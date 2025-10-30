import ctypes
import numpy as np
#-------------------------------------------------------------------------------
class bcolors:
    HEADER = '\033[95m'
    OKBLUE = '\033[94m'
    OKGREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    UNDERLINE = '\033[4m'
#-------------------------------------------------------------------------------

from ctypes import *

# Load C library
def loadLibrary(filename, relativePath="", forceUpdate=False):
    import sys
    import os
    from os.path import exists
    if (relativePath != ""):
        filename = relativePath + "/" + filename

    if (forceUpdate) or (not exists(filename)):
        print(bcolors.FAIL,"Could not find DataLoader Library (", filename, "), compiling a fresh one..!",bcolors.ENDC)
        print("Current directory was (", os.getcwd(), ") ")
        directory = os.path.dirname(os.path.abspath(filename))
        #creationScript = directory + "/makeLibrary.sh"
        os.system("make")

    if not exists(filename):
        directory = os.path.dirname(os.path.abspath(filename))
        print(bcolors.FAIL,"Could not make DataLoader Library, terminating",bcolors.ENDC)
        print("Directory we tried was : ", directory)
        sys.exit(0)

    libDataLoader = CDLL(filename, mode=ctypes.RTLD_GLOBAL)
    libDataLoader.connect()

    return libDataLoader


class SharedMemoryManager:

    def link(self):
        #Common C functions used in member python functions
        self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor.argtypes = [ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor.restype  = ctypes.c_void_p

        self.libSharedMemoryVideoBuffers.createVideoFrameMetaData.argtypes = [ctypes.c_void_p,ctypes.c_char_p,ctypes.c_uint,ctypes.c_uint,ctypes.c_uint]
        self.libSharedMemoryVideoBuffers.createVideoFrameMetaData.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.destroyVideoFrame.argtypes = [ctypes.c_void_p,ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.destroyVideoFrame.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.map_frame_shared_memory.argtypes = [ctypes.c_void_p,ctypes.c_int]
        self.libSharedMemoryVideoBuffers.map_frame_shared_memory.restype  = POINTER(ctypes.c_byte)

        self.libSharedMemoryVideoBuffers.resolveFeedNameToID.argtypes = [ctypes.c_void_p,ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.resolveFeedNameToID.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.mapRemoteToLocal.argtypes = [ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int]
        self.libSharedMemoryVideoBuffers.mapRemoteToLocal.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.getLocalMappingPointer.argtypes = [ctypes.c_void_p,ctypes.c_int]
        self.libSharedMemoryVideoBuffers.getLocalMappingPointer.restype  = POINTER(ctypes.c_byte)

        self.libSharedMemoryVideoBuffers.printSharedMemoryContextState.argtypes = [ctypes.c_void_p] 


        self.libSharedMemoryVideoBuffers.allocateLocalMapping.restype = ctypes.c_void_p
      
        self.libSharedMemoryVideoBuffers.freeLocalMapping.argtypes = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.freeLocalMapping.restype     = ctypes.c_int

        self.libSharedMemoryVideoBuffers.startWritingToVideoBufferPointer.argtypes = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.startWritingToVideoBufferPointer.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.stopWritingToVideoBufferPointer.argtypes = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.stopWritingToVideoBufferPointer.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.startReadingFromVideoBufferPointer.argtypes = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.startReadingFromVideoBufferPointer.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.stopReadingFromVideoBufferPointer.argtypes = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.stopReadingFromVideoBufferPointer.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.getVideoFrameDataPointer.argtypes = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.getVideoFrameDataPointer.restype  = POINTER(ctypes.c_byte)

        self.libSharedMemoryVideoBuffers.getVideoBufferPointer.argtypes = [ctypes.c_void_p,ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.getVideoBufferPointer.restype  = ctypes.c_void_p

        self.libSharedMemoryVideoBuffers.getVideoFrameDataSize.argtypes = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.getVideoFrameDataSize.restype  = ctypes.c_ulong

        self.libSharedMemoryVideoBuffers.getVideoFrameWidth.argtypes    = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.getVideoFrameWidth.restype     = ctypes.c_uint
        self.libSharedMemoryVideoBuffers.getVideoFrameHeight.argtypes   = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.getVideoFrameHeight.restype    = ctypes.c_uint
        self.libSharedMemoryVideoBuffers.getVideoFrameChannels.argtypes = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.getVideoFrameChannels.restype  = ctypes.c_uint

    def server(self, descriptor="video_frames.shm", frameName="stream1"):
        path = descriptor.encode('utf-8')  
        self.smc      = self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor(path)

        print("Creating descriptor ",frameName)
        path = frameName.encode('utf-8')  
        res = self.libSharedMemoryVideoBuffers.createVideoFrameMetaData(self.smc,path,self.width,self.height,self.channels)

        #Get Video Buffer Pointer
        print("Getting frame ",frameName)
        self.frame = self.libSharedMemoryVideoBuffers.getVideoBufferPointer(self.smc,path)

        #Map Video Buffer Pointer
        print("Mapping video buffer memory ")
        res = self.libSharedMemoryVideoBuffers.map_frame_shared_memory(self.frame,1) #The 1 is very important, it copies the mmapped region to our context 

    def client(self, descriptor="video_frames.shm", frameName="stream1"):   
        path = descriptor.encode('utf-8')  
        self.smc      = self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor(path)
        #Get Video Buffer Pointer
        print("Getting frame ",frameName)
        path = frameName.encode('utf-8')  
        self.frame    = self.libSharedMemoryVideoBuffers.getVideoBufferPointer(self.smc,path)
        if (self.frame==0):
            raise RuntimeError("Failed to find video buffer pointer")

        self.localMap = self.libSharedMemoryVideoBuffers.allocateLocalMapping()
        if (self.localMap==0):
            raise RuntimeError("Failed to allocate local mapping")

        self.item     = self.libSharedMemoryVideoBuffers.resolveFeedNameToID(self.smc,path)
        res = self.libSharedMemoryVideoBuffers.mapRemoteToLocal(self.smc,self.localMap,self.item)
        if (res==0):
            raise RuntimeError("Failed to map remote to local")
            

    def __init__(self, libraryPath, descriptor="video_frames.shm", frameName="stream1", connect=False, width=640, height=480, channels=3, forceLibUpdate=False):
        # Create a shared memory segment
        self.frameName = frameName

        print("Loading libSharedMemoryVideoBuffers")
        self.libSharedMemoryVideoBuffers = loadLibrary(libraryPath, forceUpdate=forceLibUpdate)
        self.link()

        #Connect to descriptor
        print("Connecting to descriptor ",descriptor)
        self.smc      = None
        self.localMap = None
        self.item     = 0

        self.width      = width
        self.height     = height
        self.channels   = channels
        self.frame_size = width * height * channels
        self.connect    = connect

        if (connect):
          self.client(descriptor=descriptor, frameName=frameName)
        else:
          self.server(descriptor=descriptor, frameName=frameName)

        print("Ready ")


    def __del__(self):
        print('Destructor called, unloading libSharedMemoryVideoBuffers')

        if (self.connect):
           self.libSharedMemoryVideoBuffers.freeLocalMapping(self.localMap) 
        else:
           path = self.frameName.encode('utf-8')  
           self.libSharedMemoryVideoBuffers.destroyVideoFrame(self.smc,path) 

    def copy_numpy_to_shared_memory(self, array):
        #print("copy_numpy_to_shared_memory ")
        #Lock Video Buffer
        res = self.libSharedMemoryVideoBuffers.startWritingToVideoBufferPointer(self.frame)

        # Check if the array size matches the shared memory size
        if res == 0:
            raise RuntimeError("Failed to lock video buffer for writing")

        # Copy the array data to shared memory
        array_ptr = array.ctypes.data_as(ctypes.c_void_p)
        size      = array.nbytes
        try:
          width    = array.shape[0]
          height   = array.shape[1]
          channels = 1
          if (len(array.shape)>2): 
                channels = array.shape[2]
          print(f"copy_to_shared_memory {size} bytes ({width} x {height} x {channels})")
          print("copy_to_shared_memory ",size," bytes (",width * height * channels,")")
          self.libSharedMemoryVideoBuffers.copy_to_shared_memory.argtypes = [ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint]
          self.libSharedMemoryVideoBuffers.copy_to_shared_memory(self.frame, array_ptr, size)
        except Exception as e:
          print("An exception occurred in copy_to_shared_memory:", str(e))

        #print("stopWritingToVideoBufferPointer ")
        # Copy the array data to shared memory
        res = self.libSharedMemoryVideoBuffers.stopWritingToVideoBufferPointer(self.frame)
        if res == 0:
            raise RuntimeError("Failed to unlock video buffer after writing")

    def read_from_shared_memory(self):
        print("read_from_shared_memory ")

        #self.libSharedMemoryVideoBuffers.printSharedMemoryContextState(self.smc)

        # Lock Video Buffer for reading
        res = self.libSharedMemoryVideoBuffers.startReadingFromVideoBufferPointer(self.frame)
        if res:
          self.frame_size = self.libSharedMemoryVideoBuffers.getVideoFrameDataSize(self.frame)
          self.width      = self.libSharedMemoryVideoBuffers.getVideoFrameWidth(self.frame)
          self.height     = self.libSharedMemoryVideoBuffers.getVideoFrameHeight(self.frame)
          self.channels   = self.libSharedMemoryVideoBuffers.getVideoFrameChannels(self.frame)
  
          if (self.connect):
             pixels = self.libSharedMemoryVideoBuffers.getLocalMappingPointer(self.localMap, self.item)
             print("Pixels pointer ",pixels)
             #buffer = (ctypes.c_ubyte * self.frame_size).from_address(pixels)
             #array = np.ctypeslib.as_array(buffer).reshape((self.height, self.width, self.channels)).astype(np.uint8)
             array = np.ctypeslib.as_array(pixels, shape=(self.height, self.width, self.channels))#.copy()
          else:
             pixels = self.libSharedMemoryVideoBuffers.getVideoFrameDataPointer(self.frame)
             array = np.ctypeslib.as_array(pixels, shape=(self.height, self.width, self.channels)).copy()

          print("Reading %ux%u:%u (size %lu) frame at "% (self.width, self.height, self.channels, self.frame_size), pixels)

          # Unlock Video Buffer after reading
          self.libSharedMemoryVideoBuffers.stopReadingFromVideoBufferPointer(self.frame)
           
          return array.astype(np.uint8)
        return None

# Test
if __name__ == "__main__":
    # Your testing code here
    pass
