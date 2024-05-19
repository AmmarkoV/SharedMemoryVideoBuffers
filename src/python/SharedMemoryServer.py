import os
import ctypes
from ctypes import c_char_p, c_uint, c_void_p, c_int
from pathlib import Path

class SharedMemoryServer:
    def __init__(self, shm_name="video_frames.shm", data_dir="data"):
        self.lib       = self.load_library()
        self.shm_name  = shm_name.encode('utf-8')
        self.data_dir  = Path(data_dir)
        self.local_map = self.allocate_local_mapping()

        if self.create_shared_memory_context_descriptor(self.shm_name) == -1:
            raise RuntimeError("Failed to create shared memory context descriptor")

        self.context = self.connect_to_shared_memory_context_descriptor(self.shm_name)
        if not self.context:
            raise RuntimeError("Failed to connect to shared memory context descriptor")

        self.data_dir.mkdir(parents=True, exist_ok=True)
        print("Server is ready.")

    def load_library(self):
        lib = ctypes.CDLL("./libSharedMemoryVideoBuffers.so", mode=ctypes.RTLD_GLOBAL)
        lib.allocateLocalMapping.restype = ctypes.POINTER(c_void_p)
        lib.createSharedMemoryContextDescriptor.argtypes = [c_char_p]
        lib.createSharedMemoryContextDescriptor.restype  = c_int
        lib.connectToSharedMemoryContextDescriptor.argtypes = [c_char_p]
        lib.connectToSharedMemoryContextDescriptor.restype  = c_void_p
        lib.unmapLocalMappingItem.argtypes  = [c_void_p, c_uint]
        lib.getLocalMappingPointer.argtypes = [c_void_p, c_uint]
        lib.getLocalMappingPointer.restype  = c_void_p
        lib.startReadingFromVideoBufferPointer.argtypes = [c_void_p]
        lib.startReadingFromVideoBufferPointer.restype  = c_int
        lib.stopReadingFromVideoBufferPointer.argtypes  = [c_void_p]
        lib.stopReadingFromVideoBufferPointer.restype  = c_int
        lib.getSharedMemoryContextNumberOfBuffers.argtypes = [c_void_p]
        lib.getSharedMemoryContextNumberOfBuffers.restype  = c_int
        lib.printSharedMemoryContextState.argtypes = [c_void_p]
        lib.writeVideoFrameToImage.argtypes = [c_char_p, c_void_p, c_void_p]
        lib.mapRemoteToLocal.argtypes = [c_void_p, c_void_p, c_uint]
        lib.getVideoBufferPointer.argtypes = [c_void_p,ctypes.c_char_p]
        lib.getVideoBufferPointer.restype  = c_void_p

        lib.getSharedMemoryContextMAXBuffers.argtypes = [ ]
        lib.getSharedMemoryContextMAXBuffers.restype  = c_int

        lib.getSharedMemoryContextVideoFrame.argtypes = [c_void_p,c_int]
        lib.getSharedMemoryContextVideoFrame.restype  = c_void_p 

        lib.remoteSharedMemoryContextVideoFrameIsPopulated.argtypes = [c_void_p, c_int]
        lib.remoteSharedMemoryContextVideoFrameIsPopulated.restype  = c_int 

        return lib

    def allocate_local_mapping(self):
        return self.lib.allocateLocalMapping()

    def create_shared_memory_context_descriptor(self, shm_name):
        return self.lib.createSharedMemoryContextDescriptor(shm_name)

    def connect_to_shared_memory_context_descriptor(self, shm_name):
        return self.lib.connectToSharedMemoryContextDescriptor(shm_name)

    def unmap_local_mapping_item(self, index):
        self.lib.unmapLocalMappingItem(self.local_map, index)

    def get_local_mapping_pointer(self, index):
        return self.lib.getLocalMappingPointer(self.local_map, index)

    def start_reading_from_video_buffer_pointer(self, frame):
        return self.lib.startReadingFromVideoBufferPointer(frame)

    def stop_reading_from_video_buffer_pointer(self, frame):
        return self.lib.stopReadingFromVideoBufferPointer(frame)

    def get_shared_memory_context_max_buffers(self):
        return self.lib.getSharedMemoryContextMAXBuffers()

    def get_shared_memory_context_number_of_buffers(self):
        return self.lib.getSharedMemoryContextNumberOfBuffers(self.context)

    def get_shared_memory_context_video_frame(self,index):
        return self.lib.getSharedMemoryContextVideoFrame(self.context,index)

    def remote_shared_memory_context_video_frame_is_populated(self,index):
        return self.lib.remoteSharedMemoryContextVideoFrameIsPopulated(self.context,index) 

    def print_shared_memory_context_state(self):
        self.lib.printSharedMemoryContextState(self.context)

    def write_video_frame_to_image(self, filename, frame, data):
        path = filename.encode('utf-8')   
        self.lib.writeVideoFrameToImage(path, frame, data)

    def map_remote_to_local(self, index):
        self.lib.mapRemoteToLocal(self.context, self.local_map, index)

    def run(self):
        while True:
            input("Press Enter to encode frames...")

            if self.get_shared_memory_context_number_of_buffers() == 0:
                print("Server is empty!")
                for i in range(self.get_shared_memory_context_max_buffers()):
                    self.unmap_local_mapping_item(i)

            for i in range(self.get_shared_memory_context_number_of_buffers()):
                frame = self.get_shared_memory_context_video_frame(i)

                if (self.remote_shared_memory_context_video_frame_is_populated(i)!=0):
                    #print(f"Frame {i} - {frame.contents.width}x{frame.contents.height}:{frame.contents.channels} - {frame.contents.name.decode('utf-8')}")
                    filename = self.data_dir / f"server_stream{i}.pnm"
                    self.map_remote_to_local(i)

                    if self.start_reading_from_video_buffer_pointer(frame):
                        self.print_shared_memory_context_state()
                        self.write_video_frame_to_image(str(filename), frame, self.get_local_mapping_pointer(i))
                        self.stop_reading_from_video_buffer_pointer(frame)
                    else:
                        print(f"Failed to lock buffer {i} for reading")
                else:
                    self.unmap_local_mapping_item(i)

if __name__ == "__main__":
    server = SharedMemoryServer()
    server.run()

