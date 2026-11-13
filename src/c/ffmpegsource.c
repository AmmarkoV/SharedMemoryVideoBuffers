/** @file ffmpegsource.c
 *  @brief Example publisher: decodes anything ffmpeg can open (a video file, an RTSP/HTTP URL, a
 *  capture device...) and publishes it as an RGB stream ("ffmpeg" unless another name is given),
 *  until the input ends, SIGINT or SIGTERM.
 *
 *  Together with v4l2sink this serves the video as a webcam:
 *
 *      ./ffmpegsource -i video.mp4 -L &
 *      ./v4l2sink -s ffmpeg -d /dev/video44
 *
 *  (scripts/ffmpeg_to_webcam.sh does both.)
 *
 *  It runs the ffmpeg command line tool as a child process, decoding to raw rgb24 on a pipe, so it
 *  needs no libav* development files: only the ffmpeg (and, for automatic sizing, ffprobe)
 *  executables at run time.
 *
 *  Usage: ffmpegsource -i INPUT [options] [-- extra ffmpeg input options]
 *    -i, --input URL      file, URL or device, anything ffmpeg accepts (required)
 *    -c, --context NAME   shared memory context (default video_frames.shm, created if missing)
 *    -s, --stream NAME    stream to publish (default ffmpeg)
 *    -W, --width N        output width  (default: the input's, from ffprobe)
 *    -H, --height N       output height (default: the input's, from ffprobe)
 *    -r, --fps N          output frame rate (default: the input's)
 *    -L, --loop           loop a file input forever
 *    -R, --realtime       read at native speed (default: on for regular files, off otherwise)
 *    -N, --no-realtime    read as fast as the input delivers
 *    -h, --help
 *  Arguments after "--" go to ffmpeg before -i, e.g. "-- -f v4l2 -input_format mjpeg".
 *
 *  Repository : https://github.com/AmmarkoV/SharedMemoryVideoBuffers
 *  @author Ammar Qammaz (AmmarkoV)
 */

//Can also be compiled using :
//gcc -O3 src/c/ffmpegsource.c src/c/sharedMemoryVideoBuffers.c -pthread -lrt -lm -o ffmpegsource

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "sharedMemoryVideoBuffers.h"

#define MAX_FFMPEG_ARGS 128
#define MAX_DIMENSION   16384

static volatile sig_atomic_t running = 1;
static void onSignal(int sig) { (void) sig; running = 0; }

/**
 * @brief Runs argv (no shell) with its stdout on a pipe.
 * @param argv NULL-terminated argument list, argv[0] looked up in PATH.
 * @param pid  Receives the child's pid.
 * @return The read end of the pipe, or -1.
 */
static int spawnReader(char *const argv[], pid_t *pid)
{
    int fds[2];
    if (pipe(fds) != 0) { perror("pipe"); return -1; }
    *pid = fork();
    if (*pid < 0) { perror("fork"); close(fds[0]); close(fds[1]); return -1; }
    if (*pid == 0)
    {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]); close(fds[1]);
        execvp(argv[0], argv);
        fprintf(stderr, "Could not run %s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    close(fds[1]);
    return fds[0];
}

/** @brief Reads exactly n bytes unless EOF or an error comes first. @return Bytes read. */
static size_t readFully(int fd, unsigned char *buf, size_t n)
{
    size_t got = 0;
    while (got < n)
    {
        ssize_t r = read(fd, buf + got, n - got);
        if (r == 0) { break; }
        if (r < 0)
        {
            if ((errno == EINTR) && running) { continue; }
            break;
        }
        got += (size_t) r;
    }
    return got;
}

/** @brief Asks ffprobe for the size of the first video stream. @return 1 on success. */
static int probeSize(const char *input, char **extra, int extraCount, unsigned int *w, unsigned int *h)
{
    char *argv[MAX_FFMPEG_ARGS];
    int n = 0;
    argv[n++] = "ffprobe"; argv[n++] = "-v"; argv[n++] = "error";
    for (int i = 0; i < extraCount; i++) { argv[n++] = extra[i]; }
    argv[n++] = "-select_streams"; argv[n++] = "v:0";
    argv[n++] = "-show_entries";   argv[n++] = "stream=width,height";
    argv[n++] = "-of";             argv[n++] = "csv=p=0:s=x";
    argv[n++] = (char *) input;
    argv[n]   = NULL;

    pid_t pid;
    int fd = spawnReader(argv, &pid);
    if (fd < 0) { return 0; }
    char out[128] = { 0 };
    size_t got = readFully(fd, (unsigned char *) out, sizeof(out) - 1);
    out[got] = 0;
    close(fd);
    int status = 0;
    waitpid(pid, &status, 0);
    return (sscanf(out, "%ux%u", w, h) == 2) && (*w > 0) && (*h > 0);
}

static int parseDimension(const char *s, unsigned int *out)
{
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if ((s[0] == '-') || (end == s) || (*end != 0) || (v == 0) || (v > MAX_DIMENSION)) { return 0; }
    *out = (unsigned int) v;
    return 1;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s -i INPUT [options] [-- extra ffmpeg input options]\n"
        "  -i, --input URL      file, URL or device, anything ffmpeg accepts\n"
        "  -c, --context NAME   shared memory context (default video_frames.shm)\n"
        "  -s, --stream NAME    stream to publish (default ffmpeg)\n"
        "  -W, --width N        output width  (default: the input's)\n"
        "  -H, --height N       output height (default: the input's)\n"
        "  -r, --fps N          output frame rate (default: the input's)\n"
        "  -L, --loop           loop a file input forever\n"
        "  -R, --realtime       read at native speed (default for regular files)\n"
        "  -N, --no-realtime    read as fast as the input delivers\n"
        "  -h, --help\n", argv0);
}

int main(int argc, char *argv[])
{
    const char *shm_name    = "video_frames.shm";
    const char *stream_name = "ffmpeg";
    const char *input       = NULL;
    const char *fps         = NULL;
    unsigned int width = 0, height = 0;
    int loop = 0, realtime = -1; // -1: decide from the input

    static const struct option longOpts[] =
    {
        { "input",       required_argument, 0, 'i' },
        { "context",     required_argument, 0, 'c' },
        { "stream",      required_argument, 0, 's' },
        { "width",       required_argument, 0, 'W' },
        { "height",      required_argument, 0, 'H' },
        { "fps",         required_argument, 0, 'r' },
        { "loop",        no_argument,       0, 'L' },
        { "realtime",    no_argument,       0, 'R' },
        { "no-realtime", no_argument,       0, 'N' },
        { "help",        no_argument,       0, 'h' },
        { 0, 0, 0, 0 }
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "+i:c:s:W:H:r:LRNh", longOpts, NULL)) != -1)
    {
        switch (opt)
        {
            case 'i': input       = optarg; break;
            case 'c': shm_name    = optarg; break;
            case 's': stream_name = optarg; break;
            case 'r': fps         = optarg; break;
            case 'L': loop        = 1; break;
            case 'R': realtime    = 1; break;
            case 'N': realtime    = 0; break;
            case 'W': if (!parseDimension(optarg, &width))  { fprintf(stderr, "Invalid width '%s'\n", optarg);  return EXIT_FAILURE; } break;
            case 'H': if (!parseDimension(optarg, &height)) { fprintf(stderr, "Invalid height '%s'\n", optarg); return EXIT_FAILURE; } break;
            case 'h': usage(argv[0]); return EXIT_SUCCESS;
            default:  usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if (!input) { usage(argv[0]); return EXIT_FAILURE; }
    char **extra   = argv + optind;   // whatever followed "--"
    int extraCount = argc - optind;
    if (extraCount > MAX_FFMPEG_ARGS - 32) { fprintf(stderr, "Too many extra ffmpeg arguments\n"); return EXIT_FAILURE; }

    struct stat st;
    int isFile = (stat(input, &st) == 0) && S_ISREG(st.st_mode);
    if (realtime < 0) { realtime = isFile; }

    // Missing dimensions come from the input; one given alone keeps the aspect ratio
    if (!width || !height)
    {
        unsigned int pw = 0, ph = 0;
        if (!probeSize(input, extra, extraCount, &pw, &ph))
        {
            fprintf(stderr, "Could not probe the size of %s (is ffprobe installed?); pass -W and -H\n", input);
            return EXIT_FAILURE;
        }
        if (!width && !height) { width = pw; height = ph; }
        else if (!width)       { width  = (unsigned int) ((unsigned long long) pw * height / ph); }
        else                   { height = (unsigned int) ((unsigned long long) ph * width  / pw); }
    }
    // Even sizes: v4l2sink's default YUYV/YU12 output pairs pixels
    width &= ~1u; height &= ~1u;
    if (!width || !height || (width > MAX_DIMENSION) || (height > MAX_DIMENSION))
    {
        fprintf(stderr, "Invalid output size %ux%u\n", width, height);
        return EXIT_FAILURE;
    }

    // ffmpeg [-re] [-stream_loop -1] [extra] -i INPUT -an -vf scale=W:H[,fps=N] -f rawvideo -pix_fmt rgb24 -
    char filter[128];
    if (fps) { snprintf(filter, sizeof(filter), "scale=%u:%u,fps=%s", width, height, fps); }
    else     { snprintf(filter, sizeof(filter), "scale=%u:%u", width, height); }
    char *ffArgs[MAX_FFMPEG_ARGS];
    int n = 0;
    ffArgs[n++] = "ffmpeg"; ffArgs[n++] = "-hide_banner"; ffArgs[n++] = "-loglevel"; ffArgs[n++] = "warning";
    ffArgs[n++] = "-nostdin";
    if (realtime)         { ffArgs[n++] = "-re"; }
    if (loop && isFile)   { ffArgs[n++] = "-stream_loop"; ffArgs[n++] = "-1"; }
    for (int i = 0; i < extraCount; i++) { ffArgs[n++] = extra[i]; }
    ffArgs[n++] = "-i"; ffArgs[n++] = (char *) input;
    ffArgs[n++] = "-an"; ffArgs[n++] = "-sn";
    ffArgs[n++] = "-vf"; ffArgs[n++] = filter;
    ffArgs[n++] = "-f"; ffArgs[n++] = "rawvideo"; ffArgs[n++] = "-pix_fmt"; ffArgs[n++] = "rgb24";
    ffArgs[n++] = "-";
    ffArgs[n]   = NULL;
    if (loop && !isFile) { fprintf(stderr, "--loop only applies to regular files, ignored\n"); }

    // Shared memory stream
    if (createSharedMemoryContextDescriptor(shm_name) == -1) { fprintf(stderr, "Could not create context %s\n", shm_name); return EXIT_FAILURE; }
    struct SharedMemoryContext *context = connectToSharedMemoryContextDescriptor(shm_name);
    if (!context) { return EXIT_FAILURE; }
    if (createVideoFrameMetaData(context, stream_name, width, height, 3) != EXIT_SUCCESS)
    {
        fprintf(stderr, "Could not create stream %s at %ux%u:3\n", stream_name, width, height);
        return EXIT_FAILURE;
    }
    struct VideoFrame *frame = getVideoBufferPointer(context, stream_name);
    if (!frame || !map_frame_shared_memory(frame, 1)) { return EXIT_FAILURE; }

    size_t frameSize = (size_t) width * height * 3;
    unsigned char *rgb = malloc(frameSize);
    if (!rgb) { fprintf(stderr, "Out of memory\n"); return EXIT_FAILURE; }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = onSignal;  // no SA_RESTART: a signal interrupts the blocking read()
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "Decoding %s -> %ux%u RGB%s%s -> stream '%s'\n", input, width, height,
            realtime ? ", realtime" : "", (loop && isFile) ? ", looped" : "", stream_name);

    pid_t ffPid;
    int fd = spawnReader(ffArgs, &ffPid);
    if (fd < 0) { free(rgb); destroyVideoFrame(context, stream_name); return EXIT_FAILURE; }

    unsigned long frames = 0;
    while (running)
    {
        // Read the whole frame outside the writer lock, so readers are blocked only for the memcpy
        if (readFully(fd, rgb, frameSize) != frameSize) { break; }
        if (startWritingToVideoBufferPointer(frame))
        {
            copy_to_shared_memory(frame, rgb, frameSize, 0);
            stopWritingToVideoBufferPointer(frame);
            frames++;
        }
    }

    // Stop ffmpeg, and drain the pipe until it exits: it flushes on SIGTERM, and closing the
    // pipe first would make it report broken pipe errors
    int interrupted = !running;
    kill(ffPid, SIGTERM);
    running = 1; // let readFully() retry after EINTR while draining
    while (readFully(fd, rgb, frameSize) > 0) { }
    close(fd);
    int status = 0;
    waitpid(ffPid, &status, 0);
    if (!interrupted && WIFEXITED(status) && (WEXITSTATUS(status) != 0))
    {
        fprintf(stderr, "ffmpeg exited with status %d\n", WEXITSTATUS(status));
    }

    free(rgb);
    destroyVideoFrame(context, stream_name);
    fprintf(stderr, "Published %lu frames. Done..\n", frames);
    return EXIT_SUCCESS;
}
