# OmniLisp - Top-level Makefile
# Pure C99 + POSIX toolchain

.PHONY: all clean compiler runtime test

all: compiler runtime

compiler:
	$(MAKE) -C csrc

runtime:
	$(MAKE) -C runtime

test: all
	$(MAKE) -C runtime/tests test

clean:
	$(MAKE) -C csrc clean
	$(MAKE) -C runtime clean

# Convenience target to run the compiler
run:
	./csrc/omnilisp

.PHONY: run
