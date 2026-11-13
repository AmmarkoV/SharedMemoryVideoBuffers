# SharedMemoryVideoBuffers

A Linux library for sharing video buffers between C and Python processes using POSIX shared memory.

---

## Architecture

```
Publisher(s)  ──write──►  Shared Memory Context  ──read──►  Consumer(s) / Server
                           (up to 10 named buffers)
```

One or more publisher processes write raw pixel data into named shared memory buffers. Consumer or server processes read those buffers concurrently. Writers are coordinated via a spin-lock embedded in each `VideoFrame` struct; it records the writer's PID, so a lock left by a writer that crashed mid-write is taken over.

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

Compiler flags: `-Wall -O2 -pthread -g`, linked with `-pthread -lrt -lm` (after the objects, so it also links on glibc < 2.34)
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

Reads all populated shared memory buffers and writes them to disk as PNM images each time Enter is pressed. A debugging tool, not for production. Publishers don't need it running: `SharedMemoryManager` publishers and the C `client`, `viewer` and `publisher_data` create the context themselves.

```bash
./server           # Press Enter to snapshot all buffers
```

Output files: `data/server_stream{i}.pnm`
Stops on `SIGINT`/`SIGTERM` (also while waiting for Enter) or when standard input is closed, so it can't run in the background.

---

### `publisher` — Random-data publisher

Connects to the shared memory context, creates a stream (`"stream1"`, 640×480 RGB, unless another name and size are given), and writes a frame every 115 ms. 16 frames of random pixels are generated once at startup and published in turn.

```bash
./publisher [stream_name] [width] [height] [channels]
./publisher stream2 1920 1080 3
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

Opens an X11 window that shows the frames of a stream (`"stream1"` unless another name is given) and follows the stream's size. 3-channel frames are shown as RGB, 1-channel frames as grayscale, and frames with any other channel count (2, 4 or more) as grayscale averaged over the channels. It can start before the publisher: it waits for the stream. Press any key or close the window to exit.

```bash
./viewer [stream_name]
./viewer stream3       # e.g. the default stream of openCVStream.py
```

Requires `libX11`. Links against `libSharedMemoryVideoBuffers.so`.

---

### `v4l2sink` — Stream as a webcam (`/dev/videoN`)

Bridges one stream to a [v4l2loopback](https://github.com/umlaeute/v4l2loopback) device, so any V4L2 application (OBS, Firefox/Chrome, Zoom, `ffplay`, OpenCV `VideoCapture`) sees it as a camera. It runs in user space: v4l2loopback provides the kernel side, `v4l2sink` copies each new frame out of shared memory (under the read lock, released before writing to the device, so publishers are never stalled) and `write()`s it to the device.

```bash
sudo apt install v4l2loopback-dkms v4l2loopback-utils   # kernel module + v4l2loopback-ctl
./publisher &                    # or any other publisher
./v4l2sink -s stream1 -d /dev/video43
ffplay /dev/video43              # or pick "SHMVB Camera" in a browser / OBS
```

| Option | Default | Meaning |
|---|---|---|
| `-c, --context NAME` | `video_frames.shm` | Shared memory context |
| `-s, --stream NAME` | `stream1` | Stream to bridge (one stream per process) |
| `-d, --device PATH` | `/dev/video43` | v4l2loopback node |
| `-f, --format FMT` | `auto` | `auto` (`grey` for 1 channel, else `yuyv`), `grey`, `rgb24`, `bgr24`, `yuyv`, `yu12` |
| `-r, --fps N` | `30` | Advertised frame rate; the last frame is re-sent when the publisher is slower, so consumers don't time out (`0` = never) |
| `-b, --swap-rb` | off | The stream holds BGR(A), e.g. from OpenCV |
| `-l, --label NAME` | `SHMVB Camera` | Camera name, if the device has to be created |
| `-n, --no-create` | off | Fail instead of creating a missing device |
| `--create-device` | | Only create the device (if missing) and exit |

If the device doesn't exist, `v4l2sink` creates it through v4l2loopback's control node (`/dev/v4l2loopback`, module 0.13+; the same as newer `v4l2loopback-ctl add`). Existing loopback devices and the programs using them are left alone. If the module isn't loaded, it is loaded with just this device. Creating needs root, so `v4l2sink` re-runs only that step through `sudo` (you are asked for your password once) and then keeps running as you. `scripts/v4l2_setup.sh [nr] [label]` creates the device ahead of time the same way.

Streams with 1, 3 (RGB) or 4 (RGBA) channels are supported. It can start before the publisher, follows a stream that is re-created or resized, and stops on `SIGINT`/`SIGTERM`.

Troubleshooting:
- *doesn't accept output frames*: the node isn't a v4l2loopback device, or it already has a producer.
- *Device or resource busy* when the size changes: v4l2loopback keeps its format while a consumer has the device open. Close the consumer and the sink retries every second.
- Browsers list the camera only with `exclusive_caps=1` (the setup script sets it), and most of them need YUYV or YU12, not RGB24.
- Permission denied: add yourself to the `video` group.

---

### `screencapture` — Screen as a webcam

Captures the X11 screen (or a region) with MIT-SHM (`XShmGetImage`, falling back to `XGetImage`) and publishes it as an RGB stream (`"screen"` by default). Paired with `v4l2sink`, the desktop becomes a webcam any V4L2 application can open:

```bash
scripts/screen_to_webcam.sh 43 30 1280x720+0+0   # nr, fps, optional region; Ctrl+C stops both
# equivalent to:
./screencapture -s screen -r 30 -g 1280x720+0+0 &
./v4l2sink -s screen -d /dev/video43 -l "SHMVB Screen"
ffplay /dev/video43
```

| Option | Default | Meaning |
|---|---|---|
| `-c, --context NAME` | `video_frames.shm` | Shared memory context (created if missing) |
| `-s, --stream NAME` | `screen` | Stream to publish |
| `-g, --geometry WxH+X+Y` | whole screen | Region to capture (clipped to the screen, rounded to even sizes) |
| `-r, --fps N` | `30` | Capture rate |
| `-D, --display NAME` | `$DISPLAY` | X display |

Requires `libX11` and `libXext`. On Wayland only XWayland windows are visible to it.

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
ok = smm.copy_numpy_to_shared_memory(numpy_array)   # False = frame dropped, every slot was busy

# Consumer / client mode
smm = SharedMemoryManager(
    libraryPath="libSharedMemoryVideoBuffers.so",
    frameName="stream1",
    connect=True            # True = read
)
frame = smm.read_from_shared_memory()   # returns numpy array (a copy), or None

# Zero-copy read: view points straight into shared memory
with smm.read_frame() as view:          # view is a read-only numpy array, or None
    if view is not None:
        edges = cv2.Canny(view, 100, 200)
        # smm.width, smm.height, smm.channels, smm.unix_timestamp (Unix nanoseconds) describe this frame
```

`read_frame()` keeps the frame's slot protected until the block exits, so `view` can't change while you use it:

- **Don't hold many frames at once.** A stream has 4 slots by default (`SHMVB_BUFFER_COUNT`, 1-4, set by the process that creates the stream): the latest frame, the one being written, and two more. So up to two readers can hold older frames without slowing the publisher. When readers hold every slot, the publisher waits up to the lock timeout, then drops the frame (`copy_numpy_to_shared_memory` returns `False`).
- **Don't use `view` after the block.** The writer will reuse the slot, so copy anything you want to keep (`view.copy()`).
- **Don't read the same manager again inside the block.** `read_frame()`, `read_from_shared_memory()` and `get_timestamp()` raise `RuntimeError` there, because a second read would release the block's protection.
- Exit the block on the same thread that entered it.
- Streams created with `SHMVB_BUFFER_COUNT=1` have no protection.

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
python3 src/python/openCVStream.py <video_source> [stream_name] [--size W H] [--fps HZ]
# examples:
python3 src/python/openCVStream.py test.mp4 stream3
python3 src/python/openCVStream.py 0 stream3          # camera index
python3 src/python/openCVStream.py /dev/video0 shm1 --size 640 480 --fps 30
```

Default stream name: `stream3`. Loops video on end-of-file. Frames are published at 800x600 unless `--size` is given, with black bars to keep the aspect ratio. `--size` and `--fps` are also requested from a camera, and `--fps` overrides the source's publishing rate.

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
# Terminal 1 (optional) — snapshot streams to disk for debugging
python3 src/python/SharedMemoryServer.py

# Terminal 2 — stream a video file
python3 src/python/openCVStream.py test.mp4 stream1

# Terminal 3 (optional) — display the stream
python3 src/python/client_downstream.py stream1
```

### Mixed C/Python pipeline

```bash
# C server saves frames to disk each time Enter is pressed (separate terminal)
./server

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
