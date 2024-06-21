CC = gcc
CFLAGS       = -Wall -pthread -lrt -lm -g
LDFLAGS      = -shared -fPIC -g
X11LIBS      = -lX11
SERVER_SRC   = src/c/server.c src/c/sharedMemoryVideoBuffers.c
CLIENT_SRC   = src/c/client.c src/c/sharedMemoryVideoBuffers.c
VIEWER_SRC   = src/c/viewer.c src/c/sharedMemoryVideoBuffers.c
LIBRARY_SRC  = src/c/sharedMemoryVideoBuffers.c
OBJ_DIR      = obj

SERVER_OBJ   = $(addprefix $(OBJ_DIR)/, $(notdir $(SERVER_SRC:.c=.o)))
CLIENT_OBJ   = $(addprefix $(OBJ_DIR)/, $(notdir $(CLIENT_SRC:.c=.o)))
VIEWER_OBJ   = $(addprefix $(OBJ_DIR)/, $(notdir $(VIEWER_SRC:.c=.o)))
LIBRARY_OBJ  = $(addprefix $(OBJ_DIR)/, $(notdir $(LIBRARY_SRC:.c=.o)))

LIBRARY_NAME = libSharedMemoryVideoBuffers.so
TARGETS      = server client viewer $(LIBRARY_NAME)

.PHONY: all clean

all: $(TARGETS)

server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

viewer: $(VIEWER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(X11LIBS)

$(LIBRARY_NAME): $(LIBRARY_OBJ)
	$(CC) $(LDFLAGS)  $^ -o $@

$(OBJ_DIR)/%.o: src/c/%.c
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

clean:
	rm -f $(OBJ_DIR)/*.o $(TARGETS) $(LIBRARY_NAME)

