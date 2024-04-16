CC ?= gcc
CFLAGS = -Wall -Wextra -Wbad-function-cast -Wcast-function-type -g -O0 -std=c2x
LDFLAGS = -lm
CXX ?= g++
CXXFLAGS = -Wall -Wextra -g -O0 -std=c++23

default: test

all: test example

test: test.c mpsc.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

example: example.c mpsc.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
