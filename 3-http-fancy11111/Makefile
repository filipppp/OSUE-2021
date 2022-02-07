# Makefile for program client & server
# Author: Daniel FENZ, 12019861 <daniel.fenz@tuwien.ac.at>

CC = gcc
DEFS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS = -Wall -g -std=c99 -pedantic $(DEFS) 

client.o: client.c shared.h
server.o: server.c shared.h
shared.o: shared.c shared.h

.PHONY: all clean
all: server client

server: server.o shared.o
	$(CC) -o $@ $^ -lrt -pthread

client: client.o shared.o
	$(CC) -o $@ $^ -lrt -pthread

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf *.o server client