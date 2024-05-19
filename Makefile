CC = gcc
CFLAGS       = -Wall -pthread -lrt -lm -g
LDFLAGS      = -shared -fPIC -g
SERVER_SRC   = src/c/server.c src/c/sharedMemoryVideoBuffers.c
CLIENT_SRC   = src/c/client.c src/c/sharedMemoryVideoBuffers.c
LIBRARY_SRC  = src/c/sharedMemoryVideoBuffers.c
SERVER_OBJ   = $(patsubst src/c/%, %, $(SERVER_SRC:.c=.o))
CLIENT_OBJ   = $(patsubst src/c/%, %, $(CLIENT_SRC:.c=.o))
LIBRARY_OBJ  = $(patsubst src/c/%, %, $(LIBRARY_SRC:.c=.o))
LIBRARY_NAME = libSharedMemoryVideoBuffers.so
TARGETS      = server client $(LIBRARY_NAME)

.PHONY: all clean

all: $(TARGETS) $(LIBRARY_NAME)

server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(LIBRARY_NAME): $(LIBRARY_OBJ)
	$(CC) $(LDFLAGS)  $< -o $@

%.o: src/c/%.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

clean:
	rm -f $(SERVER_OBJ) $(CLIENT_OBJ) $(TARGETS) $(LIBRARY_NAME)

