CC = gcc
CFLAGS       = -Wall -pthread -lrt -lm -g
LDFLAGS      = -shared -fPIC -g
SERVER_SRC   = server.c sharedMemoryVideoBuffers.c
CLIENT_SRC   = client.c sharedMemoryVideoBuffers.c
LIBRARY_SRC  = sharedMemoryVideoBuffers.c
SERVER_OBJ   = $(SERVER_SRC:.c=.o)
CLIENT_OBJ   = $(CLIENT_SRC:.c=.o)
LIBRARY_OBJ  = $(LIBRARY_SRC:.c=.o)
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

%.o: %.c
	$(CC) $(CFLAGS) -fPIC -c $<

clean:
	rm -f $(SERVER_OBJ) $(CLIENT_OBJ) $(TARGETS) $(LIBRARY_NAME)

