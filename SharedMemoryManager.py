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
    def __init__(self, libraryPath):
        # Create a shared memory segment
        
        self.libSharedMemoryVideoBuffers = loadLibrary(libraryPath, forceUpdate=forceLibUpdate)

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
