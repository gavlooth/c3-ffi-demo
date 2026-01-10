# Makefile for Immer bridge library
#
# Prerequisites:
#   - Immer library headers (header-only C++14)
#   - Install: git clone https://github.com/arximboldi/immer.git
#   - Or: brew install immer / apt install libimmer-dev
#
# Usage:
#   make              # Build libimmer_bridge.a
#   make test         # Build and run test
#   make clean        # Remove build artifacts

CXX = g++
CXXFLAGS = -std=c++17 -O2 -fPIC -Wall -Wextra

# Immer include path (adjust if needed)
# Default assumes immer is installed in system path or ./immer subdir
IMMER_INCLUDE ?= /usr/local/include
ifneq ($(wildcard ./immer/immer),)
	IMMER_INCLUDE = ./immer
endif
ifneq ($(wildcard ../../../immer/immer),)
	IMMER_INCLUDE = ../../../immer
endif

CXXFLAGS += -I$(IMMER_INCLUDE)

# Targets
.PHONY: all clean test

all: libimmer_bridge.a

immer_bridge.o: immer_bridge.cpp immer_bridge.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

libimmer_bridge.a: immer_bridge.o
	ar rcs $@ $^

# Simple test program
test_bridge: test_bridge.cpp libimmer_bridge.a
	$(CXX) $(CXXFLAGS) $< -L. -limmer_bridge -o $@

test: test_bridge
	./test_bridge

clean:
	rm -f *.o *.a test_bridge

# Install immer headers locally if not found
fetch-immer:
	@if [ ! -d "./immer" ]; then \
		echo "Fetching Immer library..."; \
		git clone --depth 1 https://github.com/arximboldi/immer.git; \
	else \
		echo "Immer already present"; \
	fi
