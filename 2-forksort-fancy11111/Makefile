# Makefile for program forksort
# Author: Daniel FENZ, 12019861 <daniel.fenz@tuwien.ac.at>

CC = gcc
DEFS = -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_SVID_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS = -Wall -g -std=c99 -pedantic $(DEFS) 

OBJECTS = mydiff.o

.PHONY: all clean
all: forksort

mydiff: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

forksort.o: forksort.c

clean:
	rm -rf *.o forksort