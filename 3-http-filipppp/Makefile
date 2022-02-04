# @file Makefile
# @author filipppp
# @date 12.01.2021

CC = gcc
DEFS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS = -Wall -g -std=c99 -pedantic $(DEFS)
LDFLAGS = -lz


.PHONY: all clean
all: client server

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

client: client.o
	$(CC) -o $@ $^ $(LDFLAGS)

server: server.o
	$(CC) -o $@ $^ $(LDFLAGS)

clean_after:
	rm -rf *.o

clean:
	rm -rf *.o client server
