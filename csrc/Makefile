# OmniLisp Compiler - C Toolchain
# Replaces Go-based toolchain with pure C99 + POSIX implementation

CC = gcc
# Note: -pedantic removed to allow anonymous unions (widely supported extension)
CFLAGS = -std=c99 -Wall -Wextra -g -O2 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
CFLAGS += -I. -I../third_party -I../runtime/include

LDFLAGS = -lpthread -lm -ldl

# Auto-detect readline library
READLINE_CHECK := $(shell pkg-config --exists readline 2>/dev/null && echo yes)
ifeq ($(READLINE_CHECK),yes)
    CFLAGS += -DUSE_READLINE
    LDFLAGS += -lreadline
else ifneq ($(wildcard /usr/include/readline/readline.h),)
    CFLAGS += -DUSE_READLINE
    LDFLAGS += -lreadline
endif

# Sanitizer profiles
ASAN_FLAGS = -fsanitize=address -fno-omit-frame-pointer
TSAN_FLAGS = -fsanitize=thread
UBSAN_FLAGS = -fsanitize=undefined

# Source files
AST_SRCS = ast/ast.c
PARSER_SRCS = parser/parser.c parser/pika_core.c parser/pika_codegen.c
ANALYSIS_SRCS = analysis/analysis.c analysis/dominator.c analysis/scc.c analysis/static_sym.c analysis/component.c analysis/tether_elision.c analysis/region_inference.c analysis/type_id.c analysis/type_env.c analysis/type_infer.c
CODEGEN_SRCS = codegen/codegen.c codegen/region_codegen.c codegen/spec_db.c codegen/spec_decision.c codegen/spec_codegen.c codegen/typed_array_codegen.c
MACRO_SRCS = macro/macro.c macro/pattern.c macro/template.c macro/hygiene.c
COMPILER_SRCS = compiler/compiler.c
CLI_SRCS = cli/main.c

# Object files
AST_OBJS = $(AST_SRCS:.c=.o)
PARSER_OBJS = $(PARSER_SRCS:.c=.o)
ANALYSIS_OBJS = $(ANALYSIS_SRCS:.c=.o)
CODEGEN_OBJS = $(CODEGEN_SRCS:.c=.o)
MACRO_OBJS = $(MACRO_SRCS:.c=.o)
COMPILER_OBJS = $(COMPILER_SRCS:.c=.o)
CLI_OBJS = $(CLI_SRCS:.c=.o)

ALL_LIB_OBJS = $(AST_OBJS) $(PARSER_OBJS) $(ANALYSIS_OBJS) $(CODEGEN_OBJS) $(MACRO_OBJS) $(COMPILER_OBJS)

# Static library
LIBRARY = libomnilisp.a

# Executable
TARGET = omnilisp

.PHONY: all clean debug release asan tsan ubsan test help

all: $(TARGET)

help:
	@echo "OmniLisp C Toolchain"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build the compiler (default)"
	@echo "  debug     - Build with debug symbols, no optimization"
	@echo "  release   - Build with full optimization"
	@echo "  asan      - Build with AddressSanitizer"
	@echo "  tsan      - Build with ThreadSanitizer"
	@echo "  ubsan     - Build with UndefinedBehaviorSanitizer"
	@echo "  test      - Run tests"
	@echo "  clean     - Remove build artifacts"
	@echo ""
	@echo "Usage:"
	@echo "  ./omnilisp -e '(+ 1 2)'         # Evaluate expression"
	@echo "  ./omnilisp -c file.omni         # Compile to C"
	@echo "  ./omnilisp -o prog file.omni    # Compile to binary"
	@echo "  ./omnilisp                      # Start REPL"

# Build modes
debug: CFLAGS += -DDEBUG -O0
debug: all

release: CFLAGS += -DNDEBUG -O3
release: all

asan: CFLAGS += $(ASAN_FLAGS)
asan: LDFLAGS += $(ASAN_FLAGS)
asan: all

tsan: CFLAGS += $(TSAN_FLAGS)
tsan: LDFLAGS += $(TSAN_FLAGS)
tsan: all

ubsan: CFLAGS += $(UBSAN_FLAGS)
ubsan: LDFLAGS += $(UBSAN_FLAGS)
ubsan: all

# Static library
$(LIBRARY): $(ALL_LIB_OBJS)
	ar rcs $@ $^

# Main executable
$(TARGET): $(CLI_OBJS) $(LIBRARY)
	$(CC) $(CFLAGS) -o $@ $(CLI_OBJS) -L. -lomnilisp $(LDFLAGS)

# Pattern rules
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Test target
test: $(TARGET)
	$(MAKE) -C tests test

# Clean
clean:
	rm -f $(ALL_LIB_OBJS) $(CLI_OBJS) $(LIBRARY) $(TARGET)

# Dependencies
ast/ast.o: ast/ast.c ast/ast.h
parser/parser.o: parser/parser.c parser/parser.h ast/ast.h
parser/pika_core.o: parser/pika_core.c parser/pika.h ast/ast.h
parser/pika_codegen.o: parser/pika_codegen.c parser/pika.h ast/ast.h
analysis/analysis.o: analysis/analysis.c analysis/analysis.h ast/ast.h
analysis/type_id.o: analysis/type_id.c analysis/type_id.h analysis/analysis.h
codegen/codegen.o: codegen/codegen.c codegen/codegen.h ast/ast.h analysis/analysis.h analysis/type_id.h
macro/macro.o: macro/macro.c macro/macro.h ast/ast.h
macro/pattern.o: macro/pattern.c macro/macro.h ast/ast.h
macro/template.o: macro/template.c macro/macro.h ast/ast.h
macro/hygiene.o: macro/hygiene.c macro/macro.h ast/ast.h
compiler/compiler.o: compiler/compiler.c compiler/compiler.h parser/parser.h analysis/analysis.h codegen/codegen.h macro/macro.h
cli/main.o: cli/main.c compiler/compiler.h
