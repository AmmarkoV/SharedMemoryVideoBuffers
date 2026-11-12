# Plan: Shared Memory → v4l2loopback bridge (`v4l2sink`)

## Goal
Expose any SharedMemoryVideoBuffers stream as a regular webcam (`/dev/videoN`) so that
unmodified V4L2 applications (OBS, Firefox/Chrome, Zoom, `ffplay`, OpenCV `VideoCapture`) can consume it.

## Key decision: userspace bridge, not a new kernel module
- `/dev/inputX` is the evdev (keyboard/mouse/joystick) subsystem; cameras live at `/dev/videoX`.
  The plan targets `/dev/videoX`.
- The `v4l2loopback` kernel module already exists (installed here via DKMS for 7.0.0-42) and
  provides exactly the kernel half: an *output* side a process writes frames into, and a
  *capture* side every app sees as a camera.
- Writing our own kernel driver that reads POSIX shm would mean the kernel mapping a userspace
  tmpfs object and honouring our PID-based spin-locks — fragile, unsafe, and needs signing for
  Secure Boot. Not worth it.
- So the "driver" is a small C daemon, linked against `sharedMemoryVideoBuffers.c`, that reads
  frames from the shm context and `write()`s them into the v4l2loopback output node.

```
Publisher ──► SHM context/stream ──► v4l2sink (C, userspace) ──write()──► /dev/video43 (v4l2loopback) ──► OBS / browser / ffplay
```

## Components

### 1. `src/c/v4l2sink.c` (new executable)
CLI:
```
./v4l2sink [--context video_frames.shm] [--stream stream1] [--device /dev/video43]
           [--format auto|rgb24|bgr24|yuyv|yu12|grey] [--fps 30] [--swap-rb]
```
Steps:
1. **Connect** – `connectToSharedMemoryContextDescriptor()`; wait (retry loop, `SIGINT`-aware)
   until `getVideoBufferPointer(stream)` returns a populated stream.
2. **Open device** – `open(device, O_WRONLY)`; `VIDIOC_QUERYCAP` must report
   `V4L2_CAP_VIDEO_OUTPUT` (otherwise: clear error suggesting `modprobe v4l2loopback exclusive_caps=1`).
3. **Negotiate format** – `VIDIOC_S_FMT` with `V4L2_BUF_TYPE_VIDEO_OUTPUT`, width/height from the
   `VideoFrame`, pixel format from channel count:
   | channels | native fourcc | default output |
   |---|---|---|
   | 1 | `GREY` | `GREY` |
   | 3 | `RGB3` (RGB24) | `YUYV` (most apps, esp. browsers, reject RGB24) |
   | 4 | `RGB4`/`AR24` | `YUYV` |
   Also set `sizeimage`, `bytesperline`, `field=NONE`, `colorspace=SRGB`, and
   `VIDIOC_S_PARM` with the frame rate.
4. **Main loop**
   - `startReadingFromVideoBufferPointer()` → check `getVideoFrameTimestamp()`; if unchanged,
     release and sleep briefly (library has no wakeup primitive; viewer.c polls at 10 ms too).
   - Copy/convert the slot into a local staging buffer *while holding the read lock*, then
     `stopReadingFromVideoBufferPointer()` immediately so publishers are never stalled by V4L2.
   - `write(fd, staging, sizeimage)` outside the lock.
   - Optional `--fps` pacing: re-send the last frame if the publisher is slower than the
     target rate (keeps some consumers from timing out).
5. **Stream changes** – detect a changed `generation`/size (stream re-created or resized):
   re-run `S_FMT`; if the device refuses while a reader is attached, close and reopen.
6. **Shutdown** – `SIGINT`/`SIGTERM` handler sets `running = 0`; release lock, close fd.

### 2. `src/c/pixel_convert.c/.h` (small, no deps)
- `rgb24_to_yuyv()`, `rgb24_to_yu12()`, `rgba_to_yuyv()`, `swap_rb()` — BT.601 integer math.
- Kept separate so it can be unit-tested and reused (e.g. by viewer).

### 3. Build
- `Makefile`: new `v4l2sink` target in `TARGETS` (same `-Wall -O2 -pthread -g`, `-lrt -lm`);
  only needs `<linux/videodev2.h>` (kernel uapi headers, no extra library).
- `CMakeLists.txt`: mirror target.
- `src/project/SharedMemoryVideoBuffersV4L2Sink.cbp` for Code::Blocks, matching the others.

### 4. Helper script `scripts/v4l2_setup.sh`
```
sudo modprobe v4l2loopback devices=1 video_nr=43 card_label="SHMVB Camera" exclusive_caps=1
```
plus a check that the module is loaded and the node is an output device.

### 5. Docs / tests
- README section: setup, usage, format notes, troubleshooting (Secure Boot, `exclusive_caps`,
  browsers needing YUYV, permissions / `video` group).
- `test/`: conversion unit test (known RGB → expected YUYV values); manual end-to-end:
  `./publisher` + `./v4l2sink` + `ffplay /dev/video43` / `v4l2-ctl --stream-mmap -d /dev/video43`.

## Milestones
1. Minimal: GREY/RGB24 passthrough, fixed size, polling loop → verify with `ffplay`.
2. YUYV/YU12 conversion + `--format auto` → verify in a browser and OBS.
3. FPS pacing, stream re-creation/resize handling, robust reconnect.
4. Build integration, setup script, README, tests.

## Open questions
- Is `/dev/inputX` really meant as `/dev/videoX`? (Assumed yes.) If you actually need an
  **input event** device (e.g. publish gesture/keypoint data from a generic shm stream as a
  virtual joystick), that is a different bridge using `/dev/uinput` — doable, but separate.
- One stream → one device, or should a single process bridge several streams to several
  `/dev/videoN` nodes (one thread each)? Plan assumes one per process; multi-stream is easy later.
- Python counterpart (`pyfakewebcam`-style) wanted too, or C only?
