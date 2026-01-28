# OmniLisp Runtime Makefile
#
# Builds the OmniLisp runtime as a static library (libomni.a)
# that can be linked with user-generated code.
#
# Includes modular memory management with ASAP static analysis.

CC ?= gcc
AR ?= ar
CFLAGS = -std=c99 -O2 -Wall -Wextra -fPIC -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -lpthread -lm -ldl

# Directories
SRCDIR = src
INCDIR = include
BUILDDIR = build
BENCHDIR = bench
PIKA_DIR = ../csrc/parser/pika_c

# Pika parser object (for regex engine)
PIKA_OBJ = $(PIKA_DIR)/pika.o
# PIC version for shared library
PIKA_PIC_OBJ = $(BUILDDIR)/pika_pic.o

# Source files - main runtime
MAIN_SOURCES = $(wildcard $(SRCDIR)/*.c)
MAIN_OBJECTS = $(MAIN_SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Memory module sources
MEMORY_SOURCES = $(wildcard $(SRCDIR)/memory/*.c)
MEMORY_OBJECTS = $(MEMORY_SOURCES:$(SRCDIR)/memory/%.c=$(BUILDDIR)/memory/%.o)

# Utility module sources
UTIL_SOURCES = $(wildcard $(SRCDIR)/util/*.c)
UTIL_OBJECTS = $(UTIL_SOURCES:$(SRCDIR)/util/%.c=$(BUILDDIR)/util/%.o)

# All source files
SOURCES = $(MAIN_SOURCES) $(MEMORY_SOURCES) $(UTIL_SOURCES)
OBJECTS = $(MAIN_OBJECTS) $(MEMORY_OBJECTS) $(UTIL_OBJECTS)

# All objects including pika dependency
ALL_OBJECTS = $(OBJECTS) $(PIKA_OBJ)

# Output
STATIC_LIB = libomni.a
SHARED_LIB = libomni.so

# Default target
all: $(BUILDDIR) $(STATIC_LIB)

# Create build directories
$(BUILDDIR):
	mkdir -p $(BUILDDIR) $(BUILDDIR)/util $(BUILDDIR)/memory

# Compile main source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.c $(INCDIR)/omni.h
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) -I$(PIKA_DIR) -c $< -o $@

# Ensure pika.o exists (build if needed)
$(PIKA_OBJ):
	$(CC) $(CFLAGS) -I$(PIKA_DIR) -c $(PIKA_DIR)/pika.c -o $@

# Build PIC version of pika for shared library
$(PIKA_PIC_OBJ): $(PIKA_DIR)/pika.c
	$(CC) $(CFLAGS) -fPIC -I$(PIKA_DIR) -c $< -o $@

# Compile utility module files
$(BUILDDIR)/util/%.o: $(SRCDIR)/util/%.c
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) -c $< -o $@

# Compile memory module files
$(BUILDDIR)/memory/%.o: $(SRCDIR)/memory/%.c
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) -I$(SRCDIR)/memory -c $< -o $@

# Build static library (includes pika dependency)
$(STATIC_LIB): $(ALL_OBJECTS)
	$(AR) rcs $@ $^
	@echo "Built static library: $@"

# Build shared library (optional) - uses PIC pika object
shared: $(BUILDDIR) $(OBJECTS) $(PIKA_PIC_OBJ)
	$(CC) -shared $(OBJECTS) $(PIKA_PIC_OBJ) -o $(SHARED_LIB) $(LDFLAGS)
	@echo "Built shared library: $(SHARED_LIB)"

# ========== Benchmark Targets ==========

# Build main benchmark runner (links all bench_*.c files together)
$(BENCHDIR)/bench_runner: $(BENCHDIR)/bench_runner.c $(BENCHDIR)/*.c $(STATIC_LIB)
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) -I$(BENCHDIR) \
		$(filter-out $(BENCHDIR)/bench_runner.c, $(wildcard $(BENCHDIR)/*.c)) \
		$(BENCHDIR)/bench_runner.c \
		$(STATIC_LIB) $(LDFLAGS) -o $@
	@echo "Built benchmark runner: $@"

# Run all benchmarks
bench: $(BENCHDIR)/bench_runner
	@echo "Running RC-G benchmark suite..."
	$(BENCHDIR)/bench_runner --all

# Run specific benchmark suite
bench-%: $(BENCHDIR)/bench_runner
	@echo "Running benchmark suite: $*"
	$(BENCHDIR)/bench_runner $*

# Clean build artifacts
clean:
	rm -rf $(BUILDDIR) $(STATIC_LIB) $(SHARED_LIB) $(BENCHDIR)/bench_runner

.PHONY: all clean shared bench bench-%
