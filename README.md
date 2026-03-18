# SharedMemoryVideoBuffers

A Linux library for sharing video buffers between C and Python processes using POSIX shared memory.

---

## Architecture

```
Publisher(s)  ──write──►  Shared Memory Context  ──read──►  Consumer(s) / Server
                           (up to 10 named buffers)
```

One or more publisher processes write raw pixel data into named shared memory buffers. Consumer or server processes read those buffers concurrently. Access is coordinated via a spin-lock on a `locked` flag embedded in each `VideoFrame` struct.

Supported data types:
- **Video frames** — width × height × channels raw pixel data
- **Generic binary structs** — arbitrary fixed-size data via `createGenericMetaData()`

---

## Building

### Prerequisites

| Dependency | Purpose |
|---|---|
| `gcc` | C compiler |
| `libpthread`, `librt`, `libm` | POSIX threads, shared memory, math |
| `libX11-dev` | X11 viewer (`viewer` target only) |
| `valgrind` *(optional)* | Memory debugging scripts |
| Python `ctypes`, `numpy`, `opencv-python`, `Pillow` | Python utilities |

### Makefile (recommended)

```bash
make              # Build everything: server, client, consumer, publisher, viewer, libSharedMemoryVideoBuffers.so
make server       # Build server only
make client       # Build client only
make consumer     # Build consumer only
make publisher    # Build publisher only
make viewer       # Build X11 viewer only
make install      # Install library to /usr/local/lib and run ldconfig
make clean        # Remove all build artefacts
```

Compiler flags: `-Wall -pthread -lrt -lm -g`
Object files land in `obj/`. The shared library is built as `libSharedMemoryVideoBuffers.so`.

### CMake (alternative)

```bash
mkdir build && cd build
cmake ..
make
```

CMake mirrors all Makefile targets and uses the same compiler flags.

---

## C Executables (`src/c/`)

### `server` — Frame saver

Reads all populated shared memory buffers and writes them to disk as PNM images.

```bash
./server           # Interactive: press Enter to snapshot all buffers
./server --nokb    # Automated: snapshots every 100 ms without keyboard input
```

Output files: `data/server_stream{i}.pnm`
Responds to `SIGINT`/`SIGTERM` for clean shutdown.

---

### `publisher` — Random-data publisher

Connects to the shared memory context, creates stream `"stream1"` (640×480 RGB), and continuously writes random pixel data every 115 ms.

```bash
./publisher
```

---

### `publisher_data` — Generic data publisher

Like `publisher` but uses `createGenericMetaData()` to write an arbitrary binary struct (not tied to a specific video resolution) to stream `"data_stream1"`.

```bash
./publisher_data
```

---

### `client` — Publisher + read-back example

Creates stream `"stream1"` (640×480 RGB), writes random data in a tight loop (5 ms intervals), and reads it back to verify round-trip correctness.

```bash
./client
```

---

### `consumer` — Frame reader example

Connects to an existing stream `"stream1"`, locks it for reading, and saves the frame to `data/consumer_stream0.pnm` every 115 ms.

```bash
./consumer
```

---

### `viewer` — X11 display window

Opens an 800×600 X11 window and renders frames from stream `"stream1"` at approximately 33 FPS. Press any key to exit.

```bash
./viewer
```

Requires `libX11`. Links against `libSharedMemoryVideoBuffers.so`.

---

## Python Utilities (`src/python/`)

All Python scripts depend on `libSharedMemoryVideoBuffers.so`. The `SharedMemoryManager` wrapper loads it via `ctypes`.

### `SharedMemoryManager.py` — High-level ctypes wrapper

Central Python wrapper around the C library. Used by all other Python utilities.

```python
from SharedMemoryManager import SharedMemoryManager

# Publisher / server mode
smm = SharedMemoryManager(
    libraryPath="libSharedMemoryVideoBuffers.so",
    frameName="stream1",
    connect=False,          # False = create/write
    width=640, height=480, channels=3
)
smm.copy_numpy_to_shared_memory(numpy_array)

# Consumer / client mode
smm = SharedMemoryManager(
    libraryPath="libSharedMemoryVideoBuffers.so",
    frameName="stream1",
    connect=True            # True = read
)
frame = smm.read_from_shared_memory()   # returns numpy array
```

---

### `SharedMemoryServer.py` — Python server

Python equivalent of the C `server` executable. Waits for Enter, then snapshots all populated buffers to `data/server_stream{i}.pnm`. Handles `KeyboardInterrupt` and unmaps all buffers on exit.

```bash
python3 src/python/SharedMemoryServer.py
```

---

### `client_upstream.py` — Webcam publisher

Captures from a webcam (camera 0, 800×600) and streams frames into shared memory.

```bash
python3 src/python/client_upstream.py [stream_name]
# default stream_name: stream1
```

Press `q` to quit.

---

### `client_downstream.py` — Frame consumer / viewer

Reads frames from shared memory and displays them in an OpenCV window. Handles RGB and RGBA inputs.

```bash
python3 src/python/client_downstream.py [stream_name]
# default stream_name: stream1
```

Press `q` to quit.

---

### `folderStream.py` — Image sequence streamer

Streams a numbered image sequence from disk into shared memory. Expects files named `colorFrame_0_<number>.[jpg|png|pnm]`.

```bash
python3 src/python/folderStream.py <folder_path> [stream_name]
# example:
python3 src/python/folderStream.py /path/to/frames/ stream2
```

Loops through the sequence continuously. Supports optional aspect-ratio-preserving resize with padding.

---

### `openCVStream.py` — Video file streamer

Streams any OpenCV-readable video file (MP4, AVI, …) or camera index into shared memory.

```bash
python3 src/python/openCVStream.py <video_source> [stream_name]
# examples:
python3 src/python/openCVStream.py test.mp4 stream3
python3 src/python/openCVStream.py 0 stream3          # camera index
```

Default stream name: `stream3`. Loops video on end-of-file.

---

### `processStreamOpenCV.py` — Sobel edge-detection filter

Reads from one shared memory stream, applies a Sobel edge-detection filter, and writes the result to a second stream. Demonstrates a processing pipeline between two `SharedMemoryManager` instances.

```bash
python3 src/python/processStreamOpenCV.py
# Input:  stream "street"       on video_frames.shm
# Output: stream "dance_output" on depth_frames.shm
```

Press `q` to quit.

---

### `espStream.py` — ESP32 camera MJPEG streamer

Fetches an MJPEG HTTP multipart stream from an ESP32 camera and writes decoded frames to shared memory. Includes exponential-backoff reconnect logic.

```bash
python3 src/python/espStream.py [camera_ip] [stream_name] [sleep_ms]
# example:
python3 src/python/espStream.py 192.168.1.119 stream2 0
```

---

### `screenStream.py` — Screen capture streamer

Captures the desktop (or a screen region) via PIL `ImageGrab` and streams frames into shared memory at ~30 FPS.

```bash
python3 src/python/screenStream.py                        # full screen
python3 src/python/screenStream.py <x> <y> <w> <h>       # capture region
```

---

## Typical Usage

### Quickstart (C)

```bash
# Terminal 1 — start the server
./server

# Terminal 2 — publish random frames
./publisher

# Terminal 3 (optional) — consume and display
./viewer
```

### Quickstart (Python)

```bash
# Terminal 1 — start the server
python3 src/python/SharedMemoryServer.py

# Terminal 2 — stream a video file
python3 src/python/openCVStream.py test.mp4 stream1

# Terminal 3 (optional) — display the stream
python3 src/python/client_downstream.py stream1
```

### Mixed C/Python pipeline

```bash
# C server saves frames to disk
./server --nokb &

# Python publishes a webcam feed
python3 src/python/client_upstream.py stream1

# Python runs edge detection and writes to a second context
python3 src/python/processStreamOpenCV.py
```

---

## Debugging / Memory Checking

Valgrind wrapper scripts are provided in `scripts/`:

```bash
./scripts/debug_server.sh      # Valgrind server
./scripts/debug_publisher.sh   # Valgrind publisher
./scripts/debug_consumer.sh    # Valgrind consumer
./scripts/debug_viewer.sh      # Valgrind viewer
./scripts/debug.sh             # Valgrind default target
```

Each script runs `make` first and writes the Valgrind report to `error.txt`.

---

## Library API (`src/c/sharedMemoryVideoBuffers.h`)

Key constants:

| Constant | Value | Meaning |
|---|---|---|
| `MAX_NUMBER_OF_BUFFERS` | 10 | Maximum concurrent named streams |
| `MAX_SHM_NAME` | 256 | Maximum length of a stream or context name |
| `ATTEMPTS_TO_LOCK_A_BUFFER` | 1000 | Spin-lock retry limit |
| `SLEEP_TIME_BETWEEN_LOCK_ATTEMPTS_MICROSECONDS` | 10 | Sleep between retries (µs) |

Key function groups:

| Group | Functions |
|---|---|
| Context lifecycle | `createSharedMemoryContextDescriptor`, `connectToSharedMemoryContextDescriptor` |
| Frame lifecycle | `createVideoFrameMetaData`, `createGenericMetaData`, `destroyVideoFrame` |
| Memory mapping | `map_frame_shared_memory`, `mapRemoteToLocal`, `unmapLocalMappingItem` |
| Write locking | `startWritingToVideoBufferPointer`, `stopWritingToVideoBufferPointer` |
| Read locking | `startReadingFromVideoBufferPointer`, `stopReadingFromVideoBufferPointer` |
| Data access | `getVideoFrameDataPointer`, `copy_to_shared_memory`, `getVideoBufferPointer` |
| Image output | `writePNM`, `writeVideoFrameToImage` |
| Diagnostics | `printSharedMemoryContextState` |
