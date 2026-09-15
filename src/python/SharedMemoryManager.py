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

    # Streams published by live server-mode managers in this process, counted per
    # (descriptor, stream): a stream is destroyed only when its last manager goes,
    # so a manager replacing another one (e.g. a publisher restarted in-process)
    # doesn't get its stream destroyed by the old manager's destructor.
    _published_streams = {}
    _published_streams_lock = threading.Lock()

    def link(self):
        #Common C functions used in member python functions
        self.libSharedMemoryVideoBuffers.createSharedMemoryContextDescriptor.argtypes = [ctypes.c_char_p]
        self.libSharedMemoryVideoBuffers.createSharedMemoryContextDescriptor.restype  = ctypes.c_int

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

        self.libSharedMemoryVideoBuffers.unmapLocalMappingItem.argtypes = [ctypes.c_void_p,ctypes.c_uint]
        self.libSharedMemoryVideoBuffers.unmapLocalMappingItem.restype  = ctypes.c_int

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
        self.libSharedMemoryVideoBuffers.getVideoFrameTimestamp.restype  = ctypes.c_uint64

        self.libSharedMemoryVideoBuffers.setLatestVideoFrameTimestamp.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        self.libSharedMemoryVideoBuffers.setLatestVideoFrameTimestamp.restype  = ctypes.c_int

        self.libSharedMemoryVideoBuffers.copy_to_shared_memory.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint64]
        self.libSharedMemoryVideoBuffers.copy_to_shared_memory.restype  = ctypes.c_int

    def server(self, descriptor="video_frames.shm", frameName="stream1"):
        path = descriptor.encode('utf-8')
        # Publishers create the context if nobody has yet (an existing one keeps its
        # streams), so no separate server process has to be running first
        if self.libSharedMemoryVideoBuffers.createSharedMemoryContextDescriptor(path) != 0:
            raise RuntimeError(f"Failed to create shared memory descriptor '{descriptor}'")
        self.smc      = self.libSharedMemoryVideoBuffers.connectToSharedMemoryContextDescriptor(path)
        if not self.smc:
            raise RuntimeError(f"Failed to connect to shared memory descriptor '{descriptor}'")

        if _VERBOSE: print("Creating descriptor ",frameName)
        path = frameName.encode('utf-8')
        res = self.libSharedMemoryVideoBuffers.createVideoFrameMetaData(self.smc,path,self.width,self.height,self.channels)
        if res != 0:
            raise RuntimeError(f"createVideoFrameMetaData failed for stream '{frameName}'")
        self._published_key = (descriptor, frameName)
        with SharedMemoryManager._published_streams_lock:
            SharedMemoryManager._published_streams[self._published_key] = SharedMemoryManager._published_streams.get(self._published_key, 0) + 1

        #Get Video Buffer Pointer
        if _VERBOSE: print("Getting frame ",frameName)
        self.frame = self.libSharedMemoryVideoBuffers.getVideoBufferPointer(self.smc,path)

        #Map Video Buffer Pointer
        if _VERBOSE: print("Mapping video buffer memory ")
        res = self.libSharedMemoryVideoBuffers.map_frame_shared_memory(self.frame,1) # maps the stream into this process now, so a failure surfaces here
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
        self.unix_timestamp = 0 # set by every successful read: nanoseconds since the Unix epoch
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
            smc = getattr(self, 'smc', None)
            key = getattr(self, '_published_key', None)
            if smc and key:
                with SharedMemoryManager._published_streams_lock:
                    remaining = SharedMemoryManager._published_streams.get(key, 1) - 1
                    if remaining > 0:
                        SharedMemoryManager._published_streams[key] = remaining
                    else:
                        SharedMemoryManager._published_streams.pop(key, None)
                if remaining <= 0:
                    self.libSharedMemoryVideoBuffers.destroyVideoFrame(smc, key[1].encode('utf-8'))

    def copy_numpy_to_shared_memory(self, array, unix_timestamp=0):
        """Publishes one frame. Returns True once readers can see it, or False if it
        was dropped because the writer couldn't get a free slot in time (readers are
        holding all of them) - a transient condition, the next frame may succeed.
        Raises TypeError/ValueError for an array that isn't one uint8 frame of the
        stream's size."""
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

        if res == 0:
            if not getattr(self, '_warned_dropped_frame', False):
                print(f"copy_numpy_to_shared_memory: dropped a frame for stream '{self.frameName}', "
                      "no free slot in time (reported once)")
                self._warned_dropped_frame = True
            return False

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
          copied = self.libSharedMemoryVideoBuffers.copy_to_shared_memory(self.frame, array_ptr, size, ctypes.c_uint64(unix_timestamp))
        finally:
          # Every C writer (client.c, publisher.c, publisher_data.c) pairs
          # startWritingToVideoBufferPointer with stopWritingToVideoBufferPointer;
          # without releasing it here the buffer stays locked forever and every
          # write after the first times out in startWritingToVideoBufferPointer.
          self.libSharedMemoryVideoBuffers.stopWritingToVideoBufferPointer(self.frame)
        if not copied:
            raise RuntimeError(f"copy_to_shared_memory rejected the frame for stream '{self.frameName}'")
        return True

    def _check_not_in_read_frame(self):
        # A second read of the same stream on one thread supersedes the first in
        # the C library, which would silently unprotect an open read_frame() view.
        if getattr(self._thread_state, "in_read_frame", False):
            raise RuntimeError("Can't read this stream again inside a read_frame() block on the same thread "
                               "(it would release the block's protection) - use the view and smm.unix_timestamp instead")

    def _follow_stream(self):
        # A stream keeps its slot while it exists, but one that was destroyed and
        # re-created (e.g. its publisher restarted) can land in another slot. Look
        # it up again so reads follow it; the C library remaps re-created streams
        # on its own. Returns the stream's VideoFrame pointer, or None while it
        # doesn't exist.
        frame = self.libSharedMemoryVideoBuffers.getVideoBufferPointer(self.smc, self.frameName.encode('utf-8'))
        if frame and (frame != self.frame):
            if self.connect:
                item = self.libSharedMemoryVideoBuffers.resolveFeedNameToID(self.smc, self.frameName.encode('utf-8'))
                if not self.libSharedMemoryVideoBuffers.mapRemoteToLocal(self.smc, self.localMap, item):
                    return None
                self.libSharedMemoryVideoBuffers.unmapLocalMappingItem(self.localMap, self.item)
                self.item = item
            self.frame = frame
        return frame

    def get_timestamp(self):
        self._check_not_in_read_frame()
        frame = self._follow_stream()
        if not frame or not self.libSharedMemoryVideoBuffers.startReadingFromVideoBufferPointer(frame):
            return None
        try:
            return self.libSharedMemoryVideoBuffers.getVideoFrameTimestamp(frame)
        finally:
            self.libSharedMemoryVideoBuffers.stopReadingFromVideoBufferPointer(frame)

    def set_timestamp(self, unix_timestamp=0):
        # Re-stamps the frame currently published as latest (Unix nanoseconds, 0 = now).
        # This takes the writer lock itself, so it waits for (and can time out on) a
        # write that is in progress.
        res = self.libSharedMemoryVideoBuffers.setLatestVideoFrameTimestamp(self.frame, ctypes.c_uint64(unix_timestamp))
        if res == 0:
            raise RuntimeError("Failed to set the timestamp: the video buffer stayed locked, or no frame was published yet")

    @contextlib.contextmanager
    def read_frame(self):
        """Zero-copy read of the latest frame:

            with smm.read_frame() as view:
                if view is not None:
                    process(view)

        `view` is a read-only numpy array pointing straight into shared memory,
        or None if the frame couldn't be read (including before the first frame is published). The writer won't reuse its slot
        until the block exits, so the view is a complete, unchanging frame for
        the whole block - but not after it: copy anything you need to keep.
        smm.width/height/channels/unix_timestamp (nanoseconds since the Unix epoch) describe the frame.

        - Streams have 4 slots by default: the latest frame, the one being written,
          and room for two readers holding older frames. If readers hold all of them
          the writer stalls and copy_numpy_to_shared_memory drops the frame (returns
          False), so don't keep blocks open across many frames. SHMVB_BUFFER_COUNT,
          set by the process that creates the stream, can lower the count.
        - Exit the block on the thread that entered it, and don't read this
          manager again inside it (read_frame(), read_from_shared_memory() and
          get_timestamp() raise RuntimeError there).
        - Streams created with SHMVB_BUFFER_COUNT=1 have no protection at all.
        - If the publisher re-creates the stream (e.g. restarts, even at another
          resolution), reads follow the new stream.
        """
        self._check_not_in_read_frame()

        # Use this frame/item for the whole block, even if another thread follows
        # the stream elsewhere meanwhile: stop must match this start.
        frame = self._follow_stream()
        item  = self.item

        # Lock Video Buffer for reading
        if not frame or not self.libSharedMemoryVideoBuffers.startReadingFromVideoBufferPointer(frame):
            yield None
            return

        self._thread_state.in_read_frame = True
        try:
            self.frame_size     = self.libSharedMemoryVideoBuffers.getVideoFrameDataSize(frame)
            self.width          = self.libSharedMemoryVideoBuffers.getVideoFrameWidth(frame)
            self.height         = self.libSharedMemoryVideoBuffers.getVideoFrameHeight(frame)
            self.channels       = self.libSharedMemoryVideoBuffers.getVideoFrameChannels(frame)
            self.unix_timestamp = self.libSharedMemoryVideoBuffers.getVideoFrameTimestamp(frame)

            if (self.connect):
               pixels = self.libSharedMemoryVideoBuffers.getLocalMappingPointer(self.localMap, item)
            else:
               pixels = self.libSharedMemoryVideoBuffers.getVideoFrameDataPointer(frame)

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
            self.libSharedMemoryVideoBuffers.stopReadingFromVideoBufferPointer(frame)

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
