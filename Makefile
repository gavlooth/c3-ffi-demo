# Omnilisp Runtime Makefile

CC = gcc
CFLAGS = -Wall -Wextra -g -I./src/runtime -std=c99 -D_POSIX_C_SOURCE=200809L

# Directories
RUNTIME_DIR = src/runtime
MEMORY_DIR = $(RUNTIME_DIR)/memory
UTIL_DIR = $(RUNTIME_DIR)/util
PIKA_DIR = $(RUNTIME_DIR)/pika
PIKA_C_DIR = $(RUNTIME_DIR)/pika_c
READER_DIR = $(RUNTIME_DIR)/reader
EVAL_DIR = $(RUNTIME_DIR)/eval
COMPILER_DIR = $(RUNTIME_DIR)/compiler
TOWER_DIR = $(RUNTIME_DIR)/tower

# Library
LIBRARY = libomniruntime.a

# Source files
SRCS = $(RUNTIME_DIR)/types.c \
       $(PIKA_C_DIR)/pika.c \
       $(PIKA_DIR)/pika_core.c \
       $(PIKA_DIR)/pika_reader.c \
       $(PIKA_DIR)/omni_grammar.c \
       $(TOWER_DIR)/tower.c \
       $(READER_DIR)/omni_pika.c \
       $(READER_DIR)/omni_reader.c \
       $(EVAL_DIR)/omni_eval.c \
       $(COMPILER_DIR)/omni_compile.c \
       $(UTIL_DIR)/dstring.c \
       $(UTIL_DIR)/hashmap.c \
       $(RUNTIME_DIR)/memory/deferred.c \
       $(RUNTIME_DIR)/memory/arena.c \
       $(RUNTIME_DIR)/memory/region.c \
       $(RUNTIME_DIR)/memory/genref.c \
       $(RUNTIME_DIR)/memory/constraint.c \
       $(RUNTIME_DIR)/memory/exception.c \
       $(RUNTIME_DIR)/memory/concurrent.c \
       runtime/src/memory/arena_core.c \
       runtime/src/memory/region_core.c \
       runtime/src/memory/transmigrate.c \
       runtime/src/memory/region_value.c

# Object files
OBJS = $(SRCS:.c=.o)

# Executable
TARGET = omni

.PHONY: all clean test

all: $(LIBRARY) $(TARGET)

$(LIBRARY): $(OBJS)
	ar rcs $@ $^

$(TARGET): $(LIBRARY) src/runtime/main.o
	$(CC) $(CFLAGS) -o $@ src/runtime/main.o -L. -lomniruntime -ldl -lm

# Pattern rule for object files
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Clean build artifacts
clean:
	rm -f $(OBJS) $(LIBRARY) $(TARGET) src/runtime/main.o
	rm -f src/runtime/compiler/*.o

# Test target
test: $(TARGET)
	./$(TARGET) "(+ 1 2)"
	./$(TARGET) "(let [x 10] (* x x))"
	./$(TARGET) "(define (fact n) (if (<= n 1) 1 (* n (fact (- n 1)))))"
