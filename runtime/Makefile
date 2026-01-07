# OmniLisp Runtime Makefile
#
# Builds the OmniLisp runtime as a static library (libomni.a)
# that can be linked with user-generated code.
#
# Includes modular memory management with ASAP static analysis.

CC ?= gcc
AR ?= ar
CFLAGS = -std=c99 -O2 -Wall -Wextra -fPIC -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -lpthread

# Directories
SRCDIR = src
INCDIR = include
BUILDDIR = build

# Source files - main runtime
MAIN_SOURCES = $(wildcard $(SRCDIR)/*.c)
MAIN_OBJECTS = $(MAIN_SOURCES:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

# Memory module sources - slot pool for sound generational references
# Also includes continuation-based concurrency (green threads, generators, promises)
MEMORY_SOURCES = $(SRCDIR)/memory/slot_pool.c $(SRCDIR)/memory/handle.c $(SRCDIR)/memory/region_core.c $(SRCDIR)/memory/continuation.c $(SRCDIR)/memory/arena_core.c $(SRCDIR)/memory/transmigrate.c $(SRCDIR)/memory/region_value.c
MEMORY_OBJECTS = $(BUILDDIR)/memory/slot_pool.o $(BUILDDIR)/memory/handle.o $(BUILDDIR)/memory/region_core.o $(BUILDDIR)/memory/continuation.o $(BUILDDIR)/memory/arena_core.o $(BUILDDIR)/memory/transmigrate.o $(BUILDDIR)/memory/region_value.o

# Utility module sources - useful helpers for the runtime
UTIL_SOURCES = $(wildcard $(SRCDIR)/util/*.c)
UTIL_OBJECTS = $(UTIL_SOURCES:$(SRCDIR)/util/%.c=$(BUILDDIR)/util/%.o)

# All source files (including memory module for sound generational refs)
SOURCES = $(MAIN_SOURCES) $(MEMORY_SOURCES)
OBJECTS = $(MAIN_OBJECTS) $(MEMORY_OBJECTS)

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
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) -c $< -o $@

# Compile utility module files
$(BUILDDIR)/util/%.o: $(SRCDIR)/util/%.c
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) -c $< -o $@

# Compile memory module files (stable slot pool for sound generational refs)
$(BUILDDIR)/memory/%.o: $(SRCDIR)/memory/%.c $(SRCDIR)/memory/slot_pool.h $(SRCDIR)/memory/component.h $(SRCDIR)/memory/symmetric.h $(SRCDIR)/memory/arena.h
	$(CC) $(CFLAGS) -I$(INCDIR) -I$(SRCDIR) -I$(SRCDIR)/memory -c $< -o $@

# Build static library
$(STATIC_LIB): $(OBJECTS)
	$(AR) rcs $@ $^
	@echo "Built static library: $@"

# Build shared library (optional)
shared: $(BUILDDIR) $(OBJECTS)
	$(CC) -shared $(OBJECTS) -o $(SHARED_LIB) $(LDFLAGS)
	@echo "Built shared library: $(SHARED_LIB)"

# Install to system (optional)
install: $(STATIC_LIB)
	install -d $(DESTDIR)/usr/local/lib
	install -d $(DESTDIR)/usr/local/include/omni
	install -m 644 $(STATIC_LIB) $(DESTDIR)/usr/local/lib/
	install -m 644 $(INCDIR)/omni.h $(DESTDIR)/usr/local/include/omni/

# Clean build artifacts
clean:
	rm -rf $(BUILDDIR) $(STATIC_LIB) $(SHARED_LIB)

# Test compilation
test: $(STATIC_LIB)
	@echo "Testing runtime compilation..."
	@echo 'int main() { return 0; }' > /tmp/test_runtime.c
	$(CC) -I$(INCDIR) /tmp/test_runtime.c -L. -lomni $(LDFLAGS) -o /tmp/test_runtime
	@rm -f /tmp/test_runtime /tmp/test_runtime.c
	@echo "Runtime compiles successfully!"

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: clean all

# Optimized build
release: CFLAGS += -O3 -DNDEBUG
release: clean all

.PHONY: all clean install test debug release shared
