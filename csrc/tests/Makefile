CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -g -I.. -I../../runtime/include \
	-I../../third_party \
	-D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -L.. -lomnilisp -lpthread -lm

# All test source files
SRCS = $(wildcard test_*.c)
# Executables (strip .c extension)
TEST_BINS = $(SRCS:.c=)

.PHONY: all test clean test-cli FORCE

# Keep the compiler-side tests honest:
#
# These tests link against `../libomnilisp.a`, but the test harness does not
# track the library's underlying source dependencies. Without this rule, it
# is easy to change `csrc/codegen/*.c` (or other library sources) and still
# run stale tests against an old archive.
#
# We force `make -C .. libomnilisp.a` whenever the archive is needed.
../libomnilisp.a: FORCE
	$(MAKE) -C .. libomnilisp.a

all: $(TEST_BINS)

# Compile each test binary
# Depends on the static library in the parent directory
%: %.c ../libomnilisp.a
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Main test target
test: all test-cli
	@echo "Running unit tests..."
	@for t in $(TEST_BINS); do \
		echo "Running $$t..."; \
		./$$t || exit 1; \
	done
	@echo "All unit tests passed."

# CLI tests (moved from csrc/Makefile)
test-cli:
	@echo "Running CLI tests..."
	@if [ ! -f ../omnilisp ]; then echo "Error: ../omnilisp not found. Run 'make -C ..' first."; exit 1; fi
	@echo "(+ 1 2)" | ../omnilisp | grep -q "^3$$" && echo "PASS: arithmetic"
	@echo "(if (< 1 2) 10 20)" | ../omnilisp | grep -q "^10$$" && echo "PASS: conditionals"
	@echo "(if 0 1 2)" | ../omnilisp | grep -q "^1$$" && echo "PASS: zero truthy"
	@echo "(if false 1 2)" | ../omnilisp | grep -q "^2$$" && echo "PASS: false falsy"
	@echo "()" | ../omnilisp | grep -q "^()$$" && echo "PASS: empty list literal"
	@echo "nil" | ../omnilisp | grep -q "^()$$" && echo "PASS: nil literal"
	@echo "(let [x 5] (* x x))" | ../omnilisp | grep -q "^25$$" && echo "PASS: let bindings"
	@echo "(define (square n) (* n n)) (square 7)" | ../omnilisp | grep -q "^49$$" && echo "PASS: functions"
	@echo "CLI tests passed."

clean:
	rm -f $(TEST_BINS) *.o
