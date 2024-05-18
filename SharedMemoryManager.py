
class SharedMemoryManager:
    def __init__(self, size):
        # Create a shared memory segment
        self.shm = ctypes.CDLL(None).shm_open
        self.shm.restype = ctypes.c_void_p
        self.shm.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
        self.shm_name = "/example_shm"
        self.shm_fd = self.shm(self.shm_name.encode(), ctypes.c_int(2), ctypes.c_int(0o777))

        # Set the size of the shared memory segment
        self.size = size
        self.ftruncate = ctypes.CDLL(None).ftruncate
        self.ftruncate.argtypes = [ctypes.c_int, ctypes.c_size_t]
        self.ftruncate(self.shm_fd, self.size)

        # Map the shared memory segment to a ctypes pointer
        self.mmap = ctypes.CDLL(None).mmap
        self.mmap.restype = ctypes.POINTER(ctypes.c_ubyte)
        self.mmap.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_long]
        self.shared_memory = self.mmap(0, self.size, 3, 1, self.shm_fd, 0)

    def copy_numpy_to_shared_memory(self, array):
        # Check if the array size matches the shared memory size
        if array.nbytes != self.size:
            raise ValueError("Array size does not match shared memory size")

        # Copy the array data to shared memory
        ctypes.memmove(self.shared_memory, array.ctypes.data, self.size)

class DataLoader:
    def __init__(self, ...):  # Your DataLoader initialization here
        # Your initialization code here

    def copy_numpy_to_shared_memory(self, array):
        # Create a SharedMemoryManager instance
        shared_memory_manager = SharedMemoryManager(array.nbytes)

        # Copy the array to shared memory
        shared_memory_manager.copy_numpy_to_shared_memory(array)

# Test
if __name__ == "__main__":
    # Your testing code here
