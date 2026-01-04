#ifndef OMNI_READER_H
#define OMNI_READER_H

#include "../types.h"
#include <stddef.h>

// Reader state
typedef struct {
    const char* input;
    size_t pos;
    size_t len;
    int line;
    int col;
} Reader;

// Initialize reader with input string
Reader* reader_new(const char* input);
void reader_free(Reader* r);

// Main read function - returns AST Value
Value* omni_read_expr(Reader* r);

// Read multiple expressions (for files)
Value* omni_read_all(Reader* r);

// Convenience: read from string
Value* omni_parse(const char* input);
Value* omni_parse_all(const char* input);

// Reader macros
typedef Value* (*ReaderMacroFn)(Reader* r, char dispatch);
void register_reader_macro(char c, ReaderMacroFn fn);

#endif // OMNI_READER_H
