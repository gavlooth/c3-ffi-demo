# C3 FFI Demo Makefile

CC = gcc
C3C = c3c
CFLAGS = -Wall -Wextra -O2 -fPIC

BUILD_DIR = build
CLIB_DIR = clib
SRC_DIR = src

# C library
CLIB_SRC = $(CLIB_DIR)/mathutils.c
CLIB_OBJ = $(BUILD_DIR)/mathutils.o
CLIB_STATIC = $(BUILD_DIR)/libmathutils.a

# Output
TARGET = $(BUILD_DIR)/main

.PHONY: all clean run

all: $(TARGET)

# Build C library object
$(CLIB_OBJ): $(CLIB_SRC) $(CLIB_DIR)/mathutils.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Create static library
$(CLIB_STATIC): $(CLIB_OBJ)
	ar rcs $@ $^

# Build C3 project (links against our C library)
$(TARGET): $(CLIB_STATIC)
	$(C3C) build

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
