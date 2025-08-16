# Makefile for C/SDL2 projects compiled for Linux
#
# This Makefile uses pkg-config to correctly find libraries
# in a multi-architecture environment.

# The C compiler to use.
CC = gcc

# The name of the final Linux executable.
TARGET = fractal

# All C source files used in the project.
SRCS = main.c

# Use pkg-config to get the compiler flags for SDL2.
CFLAGS = -Wall -O3 -march=native $(shell pkg-config --cflags sdl2) -pthread

# Use pkg-config for the base SDL2 library, and add others manually.
# Note the addition of -pthread for multithreading.
LDFLAGS = $(shell pkg-config --libs sdl2) -lSDL2_mixer -lSDL2_ttf -lm -pthread

# --- Build Rules ---

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(SRCS) -o $(TARGET) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGET)
