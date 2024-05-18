CC = gcc
CFLAGS = -Wall -pthread -lrt -lm
SERVER_SRC = server.c sharedMemoryVideoBuffers.c
CLIENT_SRC = client.c sharedMemoryVideoBuffers.c
SERVER_OBJ = $(SERVER_SRC:.c=.o)
CLIENT_OBJ = $(CLIENT_SRC:.c=.o)
TARGETS = server client

.PHONY: all clean

all: $(TARGETS)

server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(SERVER_OBJ) $(CLIENT_OBJ) $(TARGETS)

