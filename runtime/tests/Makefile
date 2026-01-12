# OmniLisp Runtime Test Suite Makefile

CC ?= gcc
CFLAGS = -std=c99 -Wall -Wextra -g -I../include
LDFLAGS = -L.. -lomni -lpthread -lm

# Source files - only test_main.c (it #includes all other test files and runtime.c)
TESTS = test_main.c

# Output
TEST_BIN = run_tests
API_TEST_BIN = run_tests_api
BENCH_BIN = bench_transmigrate_vs_c

# Sanitizer builds
ASAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer
TSAN_FLAGS = -fsanitize=thread
UBSAN_FLAGS = -fsanitize=undefined

# Toolchain note (constructive criticism):
# Some environments provide `clang` without the matching compiler-rt ASAN
# runtime installed (clang then tries to link `libclang_rt.asan*.a` from its
# resource dir and fails). Default ASAN builds to gcc, which typically ships
# a working libasan runtime. Override if you have a working clang toolchain:
#   make -C runtime/tests asan ASAN_CC=clang
ASAN_CC ?= gcc

.PHONY: all clean test fast slow api bench asan tsan ubsan asan-slow tsan-slow ubsan-slow stats test-fwd runtime-lib

# Build runtime library (always consult runtime/Makefile for staleness).
# This avoids benchmarking against a stale ../libomni.a when runtime sources changed.
runtime-lib:
	$(MAKE) -C ..

# Default: build and run tests
all: $(TEST_BIN)
	./$(TEST_BIN)

# Build test binary with stats enabled (PHASE 33.5)
stats: CFLAGS += -DOMNI_TRANSMIGRATE_STATS
stats: $(TEST_BIN)
	./$(TEST_BIN)

# Build test binary forcing forwarding-table remap mode (PHASE 35 P0)
#
# This target exists to validate the forwarding-table implementation without
# relying on performance assertions.
test-fwd: CFLAGS += -DOMNI_REMAP_FORCE_FORWARDING -DOMNI_TRANSMIGRATE_DEBUG_REMAP_MODE
test-fwd: $(TEST_BIN)
	./$(TEST_BIN)

# Build test binary
$(TEST_BIN): $(TESTS) runtime-lib
	$(CC) $(CFLAGS) -o $@ $(TESTS) $(LDFLAGS)

# Build public-API test binary (header + library only)
$(API_TEST_BIN): test_api.c runtime-lib
	$(CC) $(CFLAGS) -o $@ test_api.c $(LDFLAGS)

# Run tests
test: $(TEST_BIN)
	./$(TEST_BIN)

# Fast/slow convenience targets
fast: $(TEST_BIN)
	./$(TEST_BIN)

slow: $(TEST_BIN)
	RUNTIME_TEST_LEVEL=slow ./$(TEST_BIN)

api: $(API_TEST_BIN)
	./$(API_TEST_BIN)

# Benchmarks
#
# IMPORTANT: For static libraries, link order matters.
# The objects (bench source) must come BEFORE $(LDFLAGS) so -lomni is searched last.
$(BENCH_BIN): bench_transmigrate_vs_c.c runtime-lib
	$(CC) $(CFLAGS) -o $@ bench_transmigrate_vs_c.c $(LDFLAGS)

bench: $(BENCH_BIN)
	./$(BENCH_BIN)

# AddressSanitizer build
asan: $(TESTS) runtime-lib
	$(ASAN_CC) $(CFLAGS) $(ASAN_FLAGS) -o $(TEST_BIN)_asan $(TESTS) $(LDFLAGS)
	./$(TEST_BIN)_asan

asan-slow: $(TESTS) runtime-lib
	$(ASAN_CC) $(CFLAGS) $(ASAN_FLAGS) -o $(TEST_BIN)_asan $(TESTS) $(LDFLAGS)
	RUNTIME_TEST_LEVEL=slow ./$(TEST_BIN)_asan

# ThreadSanitizer build
tsan: $(TESTS) runtime-lib
	$(CC) $(CFLAGS) $(TSAN_FLAGS) -o $(TEST_BIN)_tsan $(TESTS) $(LDFLAGS)
	./$(TEST_BIN)_tsan

tsan-slow: $(TESTS) runtime-lib
	$(CC) $(CFLAGS) $(TSAN_FLAGS) -o $(TEST_BIN)_tsan $(TESTS) $(LDFLAGS)
	RUNTIME_TEST_LEVEL=slow ./$(TEST_BIN)_tsan

# UndefinedBehaviorSanitizer build
ubsan: $(TESTS) runtime-lib
	$(CC) $(CFLAGS) $(UBSAN_FLAGS) -o $(TEST_BIN)_ubsan $(TESTS) $(LDFLAGS)
	./$(TEST_BIN)_ubsan

ubsan-slow: $(TESTS) runtime-lib
	$(CC) $(CFLAGS) $(UBSAN_FLAGS) -o $(TEST_BIN)_ubsan $(TESTS) $(LDFLAGS)
	RUNTIME_TEST_LEVEL=slow ./$(TEST_BIN)_ubsan

# Run with valgrind
valgrind: $(TEST_BIN)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TEST_BIN)

# Clean
clean:
	rm -f $(TEST_BIN) $(API_TEST_BIN) $(BENCH_BIN) $(TEST_BIN)_asan $(TEST_BIN)_tsan $(TEST_BIN)_ubsan

# Help
help:
	@echo "Targets:"
	@echo "  all      - Build and run tests (default)"
	@echo "  test     - Run tests"
	@echo "  test-fwd - Run tests with forced forwarding-table mode (Phase 35)"
	@echo "  stats    - Run tests with transmigrate statistics (Phase 33.5)"
	@echo "  asan     - Run with AddressSanitizer"
	@echo "  tsan     - Run with ThreadSanitizer"
	@echo "  ubsan    - Run with UndefinedBehaviorSanitizer"
	@echo "  valgrind - Run with Valgrind"
	@echo "  clean    - Remove binaries"
