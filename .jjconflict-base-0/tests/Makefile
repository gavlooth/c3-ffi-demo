# Test Suite Makefile
CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -g -I..

# Source directories
RUNTIME_DIR = ../src/runtime
UTIL_DIR = $(RUNTIME_DIR)/util
PIKA_DIR = $(RUNTIME_DIR)/pika
PIKA_C_DIR = $(RUNTIME_DIR)/pika_c
TOWER_DIR = $(RUNTIME_DIR)/tower

# Object files
RUNTIME_OBJS = $(RUNTIME_DIR)/types.o
UTIL_OBJS = $(UTIL_DIR)/dstring.o
PIKA_OBJS = $(PIKA_DIR)/omni_grammar.o $(PIKA_DIR)/pika_reader.o
PIKA_C_OBJS = $(PIKA_C_DIR)/pika.o
TOWER_OBJS = $(TOWER_DIR)/tower.o

ALL_OBJS = $(RUNTIME_OBJS) $(UTIL_OBJS) $(PIKA_OBJS) $(PIKA_C_OBJS) $(TOWER_OBJS)

# Test binaries
TESTS = test_pika_tower

all: $(TESTS)

test_pika_tower: test_pika_tower.c $(ALL_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Build dependencies
$(RUNTIME_DIR)/types.o: $(RUNTIME_DIR)/types.c
	$(CC) $(CFLAGS) -c $< -o $@

$(UTIL_DIR)/dstring.o: $(UTIL_DIR)/dstring.c
	$(CC) $(CFLAGS) -c $< -o $@

$(PIKA_DIR)/%.o: $(PIKA_DIR)/%.c
	$(CC) $(CFLAGS) -I$(RUNTIME_DIR) -I$(PIKA_C_DIR) -c $< -o $@

$(PIKA_C_DIR)/pika.o: $(PIKA_C_DIR)/pika.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TOWER_DIR)/%.o: $(TOWER_DIR)/%.c
	$(CC) $(CFLAGS) -I$(RUNTIME_DIR) -I$(PIKA_DIR) -I$(PIKA_C_DIR) -c $< -o $@

run: $(TESTS)
	@echo "Running tests..."
	@./test_pika_tower

clean:
	rm -f $(TESTS) $(ALL_OBJS)

.PHONY: all run clean
