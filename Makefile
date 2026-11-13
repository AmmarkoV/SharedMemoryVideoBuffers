CC = gcc
CFLAGS        = -Wall -O2 -pthread -g
LDFLAGS       = -shared -fPIC -g
# Libraries go after the objects on every link line: linkers using --as-needed (the
# default on Ubuntu and others) drop a library listed before the objects that need it,
# and before glibc 2.34 shm_open/shm_unlink live in librt, not libc
LDLIBS        = -pthread -lrt -lm
X11LIBS       = -lX11
XSHMLIBS      = -lX11 -lXext
OBJ_DIR       = obj

SERVER_SRC    = src/c/server.c src/c/sharedMemoryVideoBuffers.c
CLIENT_SRC    = src/c/client.c src/c/sharedMemoryVideoBuffers.c
CONSUMER_SRC  = src/c/consumer.c src/c/sharedMemoryVideoBuffers.c
PUBLISHER_SRC = src/c/publisher.c src/c/sharedMemoryVideoBuffers.c
VIEWER_SRC    = src/c/viewer.c src/c/sharedMemoryVideoBuffers.c
SCREENCAP_SRC = src/c/screencapture.c src/c/sharedMemoryVideoBuffers.c
FFSOURCE_SRC  = src/c/ffmpegsource.c src/c/sharedMemoryVideoBuffers.c
V4L2SINK_SRC  = src/c/v4l2sink.c src/c/pixel_convert.c src/c/sharedMemoryVideoBuffers.c
LIBRARY_SRC   = src/c/sharedMemoryVideoBuffers.c

SERVER_OBJ    = $(addprefix $(OBJ_DIR)/, $(notdir $(SERVER_SRC:.c=.o)))
CLIENT_OBJ    = $(addprefix $(OBJ_DIR)/, $(notdir $(CLIENT_SRC:.c=.o)))
CONSUMER_OBJ  = $(addprefix $(OBJ_DIR)/, $(notdir $(CONSUMER_SRC:.c=.o)))
PUBLISHER_OBJ = $(addprefix $(OBJ_DIR)/, $(notdir $(PUBLISHER_SRC:.c=.o)))
VIEWER_OBJ    = $(addprefix $(OBJ_DIR)/, $(notdir $(VIEWER_SRC:.c=.o)))
SCREENCAP_OBJ = $(addprefix $(OBJ_DIR)/, $(notdir $(SCREENCAP_SRC:.c=.o)))
FFSOURCE_OBJ  = $(addprefix $(OBJ_DIR)/, $(notdir $(FFSOURCE_SRC:.c=.o)))
V4L2SINK_OBJ  = $(addprefix $(OBJ_DIR)/, $(notdir $(V4L2SINK_SRC:.c=.o)))
LIBRARY_OBJ   = $(addprefix $(OBJ_DIR)/, $(notdir $(LIBRARY_SRC:.c=.o)))

LIBRARY_NAME = libSharedMemoryVideoBuffers.so
TARGETS      = server client viewer consumer publisher v4l2sink ffmpegsource $(LIBRARY_NAME)

# screencapture is optional: built only when the XShm headers (libxext-dev) are present,
# so a missing library never breaks the rest of the build
HAVE_XSHM := $(shell printf '\043include <X11/Xlib.h>\n\043include <X11/extensions/XShm.h>\nint main(void){return 0;}\n' | $(CC) -x c - -o /dev/null -lX11 -lXext 2>/dev/null && echo 1)
ifeq ($(HAVE_XSHM),1)
TARGETS     += screencapture
else
$(info screencapture skipped: libX11/libXext development files not found (apt install libx11-dev libxext-dev))
endif

.PHONY: all clean test

all: $(TARGETS)

test:
	./test/run_tests.sh

server: $(SERVER_OBJ)
	$(CC) -o $@ $(CFLAGS) $^ $(LDLIBS)

client: $(CLIENT_OBJ)
	$(CC) -o $@ $(CFLAGS) $^ $(LDLIBS)

consumer: $(CONSUMER_OBJ)
	$(CC) -o $@ $(CFLAGS) $^ $(LDLIBS)

publisher: $(PUBLISHER_OBJ)
	$(CC) -o $@ $(CFLAGS) $^ $(LDLIBS)

viewer: $(VIEWER_OBJ)
	$(CC) -o $@ $(CFLAGS) $^ $(X11LIBS) $(LDLIBS)

screencapture: $(SCREENCAP_OBJ)
	$(CC) -o $@ $(CFLAGS) $^ $(XSHMLIBS) $(LDLIBS)

ffmpegsource: $(FFSOURCE_OBJ)
	$(CC) -o $@ $(CFLAGS) $^ $(LDLIBS)

v4l2sink: $(V4L2SINK_OBJ)
	$(CC) -o $@ $(CFLAGS) $^ $(LDLIBS)

$(LIBRARY_NAME): $(LIBRARY_OBJ)
	$(CC) $(LDFLAGS)  $^ -o $@ $(CFLAGS) $(LDLIBS)

$(OBJ_DIR)/%.o: src/c/%.c
	mkdir -p $(OBJ_DIR)
	$(CC) -fPIC -MMD -MP -c $< -o $@ $(CFLAGS)

install: $(LIBRARY_NAME)
	sudo cp $(LIBRARY_NAME) /usr/local/lib/
	sudo ldconfig

clean:
	rm -f $(OBJ_DIR)/*.o $(OBJ_DIR)/*.d $(TARGETS) $(LIBRARY_NAME) screencapture ffmpegsource
	rm -rf test/bin

# Header dependencies written by -MMD, so changing a header rebuilds every object that includes it
-include $(wildcard $(OBJ_DIR)/*.d)

