import ctypes

# Load C library
def loadLibrary(filename, relativePath="", forceUpdate=False):
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
    def __init__(self, libraryPath, descriptor="video_frames.shm", frameName="stream1", width=640, height=480, channels=3):
        # Create a shared memory segment
        
        self.libSharedMemoryVideoBuffers = loadLibrary(libraryPath, forceUpdate=forceLibUpdate)

        #Connect to descriptor
        self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor.argtypes = [ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor.restype  = ctypes.c_void_p
        path = descriptor.encode('utf-8')  
        self.smc = self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor(path)

        #Get Video Buffer Pointer
        self.libSharedMemoryVideoBuffers.getVideoBufferPointer.argtypes = [ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.getVideoBufferPointer.restype  = ctypes.c_void_p
        path = frameName.encode('utf-8')  
        self.frame = self.libSharedMemoryVideoBuffers.getVideoBufferPointer(self.smc,path)

        #Map Video Buffer Pointer
        self.libSharedMemoryVideoBuffers.map_frame_shared_memory.argtypes = [ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.map_frame_shared_memory.restype  = ctypes.c_int
        res = self.libSharedMemoryVideoBuffers.map_frame_shared_memory(self.frame)


    def copy_numpy_to_shared_memory(self, array):
        #Lock Video Buffer
        self.libSharedMemoryVideoBuffers.startWritingToVideoBufferPointer.argtypes = [ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.startWritingToVideoBufferPointer.restype  = ctypes.c_int
        res = self.libSharedMemoryVideoBuffers.startWritingToVideoBufferPointer(self.frame)

        # Check if the array size matches the shared memory size
        if res != 0:
            raise RuntimeError("Failed to lock video buffer for writing")

        # Copy the array data to shared memory
        array_ptr = array.ctypes.data_as(ctypes.c_void_p)
        dest_ptr = ctypes.c_void_p(self.frame)
        size = array.nbytes
        self.libSharedMemoryVideoBuffers.copy_to_shared_memory(self.frame, array_ptr, size)


        # Copy the array data to shared memory
        self.libSharedMemoryVideoBuffers.stopWritingToVideoBufferPointer.argtypes = [ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.stopWritingToVideoBufferPointer.restype  = ctypes.c_int
        res = self.libSharedMemoryVideoBuffers.stopWritingToVideoBufferPointer(self.frame)

        if res != 0:
            raise RuntimeError("Failed to unlock video buffer after writing")

# Test
if __name__ == "__main__":
    # Your testing code here
