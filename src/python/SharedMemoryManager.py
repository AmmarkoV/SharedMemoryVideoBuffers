import os
import ctypes
import contextlib
import threading
import numpy as np
#-------------------------------------------------------------------------------
# Debug/status printing is off by default since copy_numpy_to_shared_memory and
# read_from_shared_memory run on every frame - printing there at video framerates
# costs more than the shared-memory copy it's supposedly logging. Set
# SHMVB_VERBOSE=1 to get the old behaviour back.
_VERBOSE = os.environ.get("SHMVB_VERBOSE", "0") in ("1", "true", "True")
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
        self.libSharedMemoryVideoBuffers.map_frame_shared_memory.restype  = POINTER(ctypes.c_ubyte)

        self.libSharedMemoryVideoBuffers.resolveFeedNameToID.argtypes = [ctypes.c_void_p,ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.resolveFeedNameToID.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.mapRemoteToLocal.argtypes = [ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int]
        self.libSharedMemoryVideoBuffers.mapRemoteToLocal.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.getLocalMappingPointer.argtypes = [ctypes.c_void_p,ctypes.c_int]
        self.libSharedMemoryVideoBuffers.getLocalMappingPointer.restype  = POINTER(ctypes.c_ubyte)

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
        self.libSharedMemoryVideoBuffers.getVideoFrameDataPointer.restype  = POINTER(ctypes.c_ubyte)

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

        self.libSharedMemoryVideoBuffers.getVideoFrameTimestamp.argtypes = [ctypes.c_void_p]
        self.libSharedMemoryVideoBuffers.getVideoFrameTimestamp.restype  = ctypes.c_ulong

        self.libSharedMemoryVideoBuffers.setLatestVideoFrameTimestamp.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
        self.libSharedMemoryVideoBuffers.setLatestVideoFrameTimestamp.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.copy_to_shared_memory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_ulong]
        self.libSharedMemoryVideoBuffers.copy_to_shared_memory.restype  = ctypes.c_int

    def server(self, descriptor="video_frames.shm", frameName="stream1"):
        path = descriptor.encode('utf-8')
        self.smc      = self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor(path)
        if not self.smc:
            raise RuntimeError(f"Failed to connect to shared memory descriptor '{descriptor}'")

        if _VERBOSE: print("Creating descriptor ",frameName)
        path = frameName.encode('utf-8')
        res = self.libSharedMemoryVideoBuffers.createVideoFrameMetaData(self.smc,path,self.width,self.height,self.channels)
        if res != 0:
            raise RuntimeError(f"createVideoFrameMetaData failed for stream '{frameName}'")

        #Get Video Buffer Pointer
        if _VERBOSE: print("Getting frame ",frameName)
        self.frame = self.libSharedMemoryVideoBuffers.getVideoBufferPointer(self.smc,path)

        #Map Video Buffer Pointer
        if _VERBOSE: print("Mapping video buffer memory ")
        res = self.libSharedMemoryVideoBuffers.map_frame_shared_memory(self.frame,1) #The 1 is very important, it copies the mmapped region to our context
        if not res:
            raise RuntimeError(f"map_frame_shared_memory failed for stream '{frameName}'")

    def client(self, descriptor="video_frames.shm", frameName="stream1"):   
        path = descriptor.encode('utf-8')  
        self.smc      = self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor(path)
        # NULL pointers come back from ctypes (c_void_p) as None, never 0
        if not self.smc:
            raise RuntimeError(f"Failed to connect to shared memory descriptor '{descriptor}'")
        #Get Video Buffer Pointer
        if _VERBOSE: print("Getting frame ",frameName)
        path = frameName.encode('utf-8')
        self.frame    = self.libSharedMemoryVideoBuffers.getVideoBufferPointer(self.smc,path)
        if not self.frame:
            raise RuntimeError(f"Failed to find stream '{frameName}' in '{descriptor}'")

        self.localMap = self.libSharedMemoryVideoBuffers.allocateLocalMapping()
        if not self.localMap:
            raise RuntimeError("Failed to allocate local mapping")

        self.item     = self.libSharedMemoryVideoBuffers.resolveFeedNameToID(self.smc,path)
        res = self.libSharedMemoryVideoBuffers.mapRemoteToLocal(self.smc,self.localMap,self.item)
        if (res==0):
            raise RuntimeError("Failed to map remote to local")
            

    def __init__(self, libraryPath, descriptor="video_frames.shm", frameName="stream1", connect=False, width=640, height=480, channels=3, forceLibUpdate=False):
        # Create a shared memory segment
        self.frameName = frameName

        if _VERBOSE: print("Loading libSharedMemoryVideoBuffers")
        self.libSharedMemoryVideoBuffers = loadLibrary(libraryPath, forceUpdate=forceLibUpdate)
        self.link()

        #Connect to descriptor
        if _VERBOSE: print("Connecting to descriptor ",descriptor)
        self.smc      = None
        self.localMap = None
        self.item     = 0

        self.width      = width
        self.height     = height
        self.channels   = channels
        self.frame_size = width * height * channels
        self.connect    = connect
        self._thread_state = threading.local() # per-thread "inside a read_frame() block" flag

        if (connect):
          self.client(descriptor=descriptor, frameName=frameName)
        else:
          self.server(descriptor=descriptor, frameName=frameName)

        if _VERBOSE: print("Ready ")


    def __del__(self):
        if _VERBOSE: print('Destructor called, unloading libSharedMemoryVideoBuffers')

        # Guard against AttributeError if __init__ raised before all attributes were set
        if not hasattr(self, 'libSharedMemoryVideoBuffers'):
            return

        if getattr(self, 'connect', False):
            if getattr(self, 'localMap', None):
                self.libSharedMemoryVideoBuffers.freeLocalMapping(self.localMap)
        else:
            smc       = getattr(self, 'smc', None)
            frameName = getattr(self, 'frameName', None)
            if smc and frameName:
                path = frameName.encode('utf-8')
                self.libSharedMemoryVideoBuffers.destroyVideoFrame(smc, path)

    def copy_numpy_to_shared_memory(self, array, unix_timestamp=0):
        #print("copy_numpy_to_shared_memory ")
        # The C side copies raw bytes, so reject anything that isn't exactly one
        # frame of uint8 data before touching the buffer.
        if array.dtype != np.uint8:
            raise TypeError(f"copy_numpy_to_shared_memory expects a uint8 array, got {array.dtype}")
        expected_size = self.libSharedMemoryVideoBuffers.getVideoFrameDataSize(self.frame)
        if array.nbytes != expected_size:
            raise ValueError(f"copy_numpy_to_shared_memory: array of shape {array.shape} is {array.nbytes} bytes, stream '{self.frameName}' expects {expected_size}")
        # array.ctypes.data is where the first element lives, which for a strided
        # view (transpose, [::-1], ...) is not the array's logical byte order.
        array = np.ascontiguousarray(array)

        #Lock Video Buffer
        res = self.libSharedMemoryVideoBuffers.startWritingToVideoBufferPointer(self.frame)

        # Check if the array size matches the shared memory size
        if res == 0:
            raise RuntimeError("Failed to lock video buffer for writing")

        # Copy the array data to shared memory
        array_ptr = array.ctypes.data_as(ctypes.c_void_p)
        size      = array.nbytes
        try:
          # NumPy image shape is (height, width[, channels])
          height   = array.shape[0]
          width    = array.shape[1]
          channels = 1
          if (len(array.shape)>2):
                channels = array.shape[2]
          if _VERBOSE: print(f"copy_to_shared_memory {size} bytes ({width} x {height} x {channels})")
          copied = self.libSharedMemoryVideoBuffers.copy_to_shared_memory(self.frame, array_ptr, size, ctypes.c_ulong(unix_timestamp))
        finally:
          # Every C writer (client.c, publisher.c, publisher_data.c) pairs
          # startWritingToVideoBufferPointer with stopWritingToVideoBufferPointer;
          # without releasing it here the buffer stays locked forever and every
          # write after the first times out in startWritingToVideoBufferPointer.
          self.libSharedMemoryVideoBuffers.stopWritingToVideoBufferPointer(self.frame)
        if not copied:
            raise RuntimeError(f"copy_to_shared_memory rejected the frame for stream '{self.frameName}'")

    def _check_not_in_read_frame(self):
        # A second read of the same stream on one thread supersedes the first in
        # the C library, which would silently unprotect an open read_frame() view.
        if getattr(self._thread_state, "in_read_frame", False):
            raise RuntimeError("Can't read this stream again inside a read_frame() block on the same thread "
                               "(it would release the block's protection) - use the view and smm.unix_timestamp instead")

    def get_timestamp(self):
        self._check_not_in_read_frame()
        res = self.libSharedMemoryVideoBuffers.startReadingFromVideoBufferPointer(self.frame)
        if not res:
            return None
        try:
            return self.libSharedMemoryVideoBuffers.getVideoFrameTimestamp(self.frame)
        finally:
            self.libSharedMemoryVideoBuffers.stopReadingFromVideoBufferPointer(self.frame)

    def set_timestamp(self, unix_timestamp=0):
        # Re-stamps the frame currently published as latest (0 = now). This takes
        # the writer lock itself, so it waits for (and can time out on) a write
        # that is in progress.
        res = self.libSharedMemoryVideoBuffers.setLatestVideoFrameTimestamp(self.frame, ctypes.c_ulong(unix_timestamp))
        if res == 0:
            raise RuntimeError("Failed to lock video buffer to set its timestamp")

    @contextlib.contextmanager
    def read_frame(self):
        """Zero-copy read of the latest frame:

            with smm.read_frame() as view:
                if view is not None:
                    process(view)

        `view` is a read-only numpy array pointing straight into shared memory,
        or None if the frame couldn't be read. The writer won't reuse its slot
        until the block exits, so the view is a complete, unchanging frame for
        the whole block - but not after it: copy anything you need to keep.
        smm.width/height/channels/unix_timestamp describe the frame.

        - Keep the block short. With the default 2 slots the writer can publish
          one more frame while the block is open, then blocks (and copy_numpy_to_shared_memory
          raises once it times out). More slots (SHMVB_BUFFER_COUNT, set by the
          process that creates the stream) give the writer more room.
        - Exit the block on the thread that entered it, and don't read this
          manager again inside it (read_frame(), read_from_shared_memory() and
          get_timestamp() raise RuntimeError there).
        - Streams created with SHMVB_BUFFER_COUNT=1 have no protection at all.
        """
        self._check_not_in_read_frame()

        # Lock Video Buffer for reading
        res = self.libSharedMemoryVideoBuffers.startReadingFromVideoBufferPointer(self.frame)
        if not res:
            yield None
            return

        self._thread_state.in_read_frame = True
        try:
            self.frame_size     = self.libSharedMemoryVideoBuffers.getVideoFrameDataSize(self.frame)
            self.width          = self.libSharedMemoryVideoBuffers.getVideoFrameWidth(self.frame)
            self.height         = self.libSharedMemoryVideoBuffers.getVideoFrameHeight(self.frame)
            self.channels       = self.libSharedMemoryVideoBuffers.getVideoFrameChannels(self.frame)
            self.unix_timestamp = self.libSharedMemoryVideoBuffers.getVideoFrameTimestamp(self.frame)

            if (self.connect):
               pixels = self.libSharedMemoryVideoBuffers.getLocalMappingPointer(self.localMap, self.item)
            else:
               pixels = self.libSharedMemoryVideoBuffers.getVideoFrameDataPointer(self.frame)

            if not pixels:
               yield None
            else:
               # getLocalMappingPointer/getVideoFrameDataPointer are declared as
               # POINTER(c_ubyte), matching the actual (unsigned) pixel data, so
               # this is already uint8. Read-only: writing through it would change
               # the published frame under every other reader.
               view = np.ctypeslib.as_array(pixels, shape=(self.height, self.width, self.channels))
               view.flags.writeable = False

               if _VERBOSE: print("Reading %ux%u:%u (size %lu) frame at "% (self.width, self.height, self.channels, self.frame_size), pixels)

               yield view
        finally:
            self._thread_state.in_read_frame = False
            # Unlock Video Buffer after reading - also on exceptions (e.g. Ctrl-C),
            # since a live process that never stops reading pins its slot
            self.libSharedMemoryVideoBuffers.stopReadingFromVideoBufferPointer(self.frame)

    def read_from_shared_memory(self):
        # Copy while read_frame() still protects the slot: once the read is
        # stopped the writer may reuse the slot for a later frame, which would
        # change a zero-copy view under the caller.
        with self.read_frame() as view:
            return None if view is None else view.copy()

# Test
if __name__ == "__main__":
    # Your testing code here
    pass
