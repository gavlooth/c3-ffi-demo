#ifndef OMNI_COMPILE_H
#define OMNI_COMPILE_H

#include "../types.h"
#include "../eval/omni_eval.h"

// Compilation context
typedef struct CompileCtx {
    char* output;           // Output buffer
    size_t output_len;
    size_t output_cap;
    int temp_counter;       // For generating temp variable names
    int label_counter;      // For generating labels
    Value* functions;       // List of defined functions
    int indent;             // Current indentation level
} CompileCtx;

// Initialize/cleanup compiler context
CompileCtx* compile_ctx_new(void);
void compile_ctx_free(CompileCtx* ctx);

// Compile an expression to C code
int compile_expr(CompileCtx* ctx, Value* expr);

// Compile a top-level form (define, etc.)
int compile_toplevel(CompileCtx* ctx, Value* form);

// Compile entire program
int compile_program(CompileCtx* ctx, Value* forms);

// Get generated C code
const char* compile_get_output(CompileCtx* ctx);

// JIT compilation: compile and load
typedef Value* (*JitFn)(void);
JitFn jit_compile(Value* expr, Env* env);

// AOT compilation: generate standalone C file
int aot_compile_to_file(const char* source_file, const char* output_file);

// AOT compilation: generate executable
int aot_compile_executable(const char* source_file, const char* output_file);

#endif // OMNI_COMPILE_H
