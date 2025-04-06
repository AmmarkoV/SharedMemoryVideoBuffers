CC = gcc
CFLAGS        = -Wall -pthread -lrt -lm -g
LDFLAGS       = -shared -fPIC -g
X11LIBS       = -lX11
OBJ_DIR       = obj

SERVER_SRC    = src/c/server.c src/c/sharedMemoryVideoBuffers.c
CLIENT_SRC    = src/c/client.c src/c/sharedMemoryVideoBuffers.c
CONSUMER_SRC  = src/c/consumer.c src/c/sharedMemoryVideoBuffers.c
PUBLISHER_SRC = src/c/publisher.c src/c/sharedMemoryVideoBuffers.c
VIEWER_SRC    = src/c/viewer.c src/c/sharedMemoryVideoBuffers.c
LIBRARY_SRC   = src/c/sharedMemoryVideoBuffers.c

SERVER_OBJ    = $(addprefix $(OBJ_DIR)/, $(notdir $(SERVER_SRC:.c=.o)))
CLIENT_OBJ    = $(addprefix $(OBJ_DIR)/, $(notdir $(CLIENT_SRC:.c=.o)))
CONSUMER_OBJ  = $(addprefix $(OBJ_DIR)/, $(notdir $(CONSUMER_SRC:.c=.o)))
PUBLISHER_OBJ = $(addprefix $(OBJ_DIR)/, $(notdir $(PUBLISHER_SRC:.c=.o)))
VIEWER_OBJ    = $(addprefix $(OBJ_DIR)/, $(notdir $(VIEWER_SRC:.c=.o)))
LIBRARY_OBJ   = $(addprefix $(OBJ_DIR)/, $(notdir $(LIBRARY_SRC:.c=.o)))

LIBRARY_NAME = libSharedMemoryVideoBuffers.so
TARGETS      = server client viewer consumer publisher $(LIBRARY_NAME)

.PHONY: all clean

all: $(TARGETS)

server: $(SERVER_OBJ)
	$(CC) -o $@ $(CFLAGS) $^

client: $(CLIENT_OBJ)
	$(CC) -o $@ $(CFLAGS) $^

consumer: $(CONSUMER_OBJ)
	$(CC) -o $@ $(CFLAGS) $^

publisher: $(PUBLISHER_OBJ)
	$(CC) -o $@ $(CFLAGS) $^

viewer: $(VIEWER_OBJ)
	$(CC) -o $@ $^ $(X11LIBS)

$(LIBRARY_NAME): $(LIBRARY_OBJ)
	$(CC) $(LDFLAGS)  $^ -o $@ $(CFLAGS) 

$(OBJ_DIR)/%.o: src/c/%.c
	mkdir -p $(OBJ_DIR)
	$(CC) -fPIC -c $< -o $@ $(CFLAGS) 

clean:
	rm -f $(OBJ_DIR)/*.o $(TARGETS) $(LIBRARY_NAME)

