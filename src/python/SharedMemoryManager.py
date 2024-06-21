import ctypes

from ctypes import *
# Load C library
def loadLibrary(filename, relativePath="", forceUpdate=False):
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
    def __init__(self, libraryPath, descriptor="video_frames.shm", frameName="stream1", width=640, height=480, channels=3, forceLibUpdate=False):
        # Create a shared memory segment
        self.frameName = frameName

        print("Loading libSharedMemoryVideoBuffers")
        self.libSharedMemoryVideoBuffers = loadLibrary(libraryPath, forceUpdate=forceLibUpdate)

        #Connect to descriptor
        print("Connecting to descriptor ",descriptor)
        self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor.argtypes = [ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor.restype  = ctypes.c_void_p
        path = descriptor.encode('utf-8')  
        self.smc = self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor(path)


        self.libSharedMemoryVideoBuffers.createVideoFrameMetaData.argtypes = [ctypes.c_void_p,ctypes.c_char_p,ctypes.c_uint,ctypes.c_uint,ctypes.c_uint]
        self.libSharedMemoryVideoBuffers.createVideoFrameMetaData.restype  = ctypes.c_int
        path = frameName.encode('utf-8')  
        res = self.libSharedMemoryVideoBuffers.createVideoFrameMetaData(self.smc,path,width,height,channels)

        #Get Video Buffer Pointer
        print("Getting frame ",frameName)
        self.libSharedMemoryVideoBuffers.getVideoBufferPointer.argtypes = [ctypes.c_void_p,ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.getVideoBufferPointer.restype  = ctypes.c_void_p
        path = frameName.encode('utf-8')  
        self.frame = self.libSharedMemoryVideoBuffers.getVideoBufferPointer(self.smc,path)

        #Map Video Buffer Pointer
        print("Mapping video buffer memory ")
        self.libSharedMemoryVideoBuffers.map_frame_shared_memory.argtypes = [ctypes.c_void_p,ctypes.c_int]
        self.libSharedMemoryVideoBuffers.map_frame_shared_memory.restype  = ctypes.c_int
        res = self.libSharedMemoryVideoBuffers.map_frame_shared_memory(self.frame,1) #The 1 is very important, it copies the mmapped region to our context 

        #Common C functions used in member python functions
        self.libSharedMemoryVideoBuffers.startWritingToVideoBufferPointer.argtypes = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.startWritingToVideoBufferPointer.restype  = ctypes.c_int
        self.libSharedMemoryVideoBuffers.stopWritingToVideoBufferPointer.argtypes = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.stopWritingToVideoBufferPointer.restype  = ctypes.c_int

    def __del__(self):
        print('Destructor called, unloading libSharedMemoryVideoBuffers')
        self.libSharedMemoryVideoBuffers.destroyVideoFrame.argtypes = [ctypes.c_void_p,ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.destroyVideoFrame.restype  = ctypes.c_void_p
        path = self.frameName.encode('utf-8')  
        self.frame = self.libSharedMemoryVideoBuffers.destroyVideoFrame(self.smc,path) 

    def copy_numpy_to_shared_memory(self, array):
        print("copy_numpy_to_shared_memory ")
        #Lock Video Buffer
        res = self.libSharedMemoryVideoBuffers.startWritingToVideoBufferPointer(self.frame)

        # Check if the array size matches the shared memory size
        if res == 0:
            raise RuntimeError("Failed to lock video buffer for writing")

        # Copy the array data to shared memory
        array_ptr = array.ctypes.data_as(ctypes.c_void_p)
        size      = array.nbytes
        try:
          print(f"copy_to_shared_memory {size} bytes ({array.shape[0]} x {array.shape[1]} x {array.shape[2]})")
          print("copy_to_shared_memory ",size," bytes (",array.shape[0] * array.shape[1] * array.shape[2],")")
          self.libSharedMemoryVideoBuffers.copy_to_shared_memory.argtypes = [ctypes.c_void_p,ctypes.c_void_p,ctypes.c_uint]
          self.libSharedMemoryVideoBuffers.copy_to_shared_memory(self.frame, array_ptr, size)
        except Exception as e:
          print("An exception occurred while copy_to_shared_memory:", str(e))

        print("stopWritingToVideoBufferPointer ")
        # Copy the array data to shared memory
        res = self.libSharedMemoryVideoBuffers.stopWritingToVideoBufferPointer(self.frame)

        if res == 0:
            raise RuntimeError("Failed to unlock video buffer after writing")



    def read_from_shared_memory(self):
        print("read_from_shared_memory ")
        # Lock Video Buffer for reading
        res = self.libSharedMemoryVideoBuffers.startReadingFromVideoBufferPointer(self.frame)
        if res == 0:
            raise RuntimeError("Failed to lock video buffer for reading")

        frame_size = self.libSharedMemoryVideoBuffers.getFrameSize(self.frame)  # Assuming getFrameSize is available
        buffer = (ctypes.c_ubyte * frame_size)()
        ctypes.memmove(buffer, self.frame.client_address_space_data_pointer, frame_size)

        # Unlock Video Buffer after reading
        self.libSharedMemoryVideoBuffers.stopReadingFromVideoBufferPointer(self.frame)

        # Convert buffer to numpy array
        #array = np.ctypeslib.as_array(buffer)
        array = np.ctypeslib.as_array(pixels, shape=(numberOfImages, self.outHeight, self.outWidth,  self.outChannels))
        return array

# Test
if __name__ == "__main__":
    # Your testing code here
    pass
