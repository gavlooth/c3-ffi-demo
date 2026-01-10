# OmniLisp Master Makefile

.PHONY: all clean runtime compiler test

all: runtime compiler

# Build the Modern Runtime (libomni.a)
runtime:
	$(MAKE) -C runtime

# Build the Compiler (omnilisp)
compiler:
	$(MAKE) -C csrc

# Clean all subprojects
clean:
	$(MAKE) -C runtime clean
	$(MAKE) -C csrc clean

# Run tests
test: compiler
	$(MAKE) -C csrc test