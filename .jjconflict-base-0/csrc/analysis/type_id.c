/*
 * type_id.c - Type ID Mapping Implementation
 */

#include "type_id.h"
#include <string.h>
#include <stdbool.h>

/*
 * Type name to TypeID mapping
 *
 * Maps string type names from type inference to TypeID enum values.
 * This is used during analysis to assign type_id to variables.
 */
TypeID type_name_to_type_id(const char* type_name) {
    if (!type_name) return TYPE_ID_GENERIC;

    /* Core types */
    if (strcmp(type_name, "Int") == 0 ||
        strcmp(type_name, "int") == 0 ||
        strcmp(type_name, "long") == 0) {
        return TYPE_ID_INT;
    }

    if (strcmp(type_name, "Float") == 0 ||
        strcmp(type_name, "float") == 0 ||
        strcmp(type_name, "double") == 0) {
        return TYPE_ID_FLOAT;
    }

    if (strcmp(type_name, "Char") == 0 ||
        strcmp(type_name, "char") == 0) {
        return TYPE_ID_CHAR;
    }

    /* Pair/Cons/Cell - all map to TYPE_ID_PAIR */
    if (strcmp(type_name, "Pair") == 0 ||
        strcmp(type_name, "Cons") == 0 ||
        strcmp(type_name, "Cell") == 0 ||
        strcmp(type_name, "cons") == 0) {
        return TYPE_ID_PAIR;
    }

    /* Collection types */
    if (strcmp(type_name, "Array") == 0) {
        return TYPE_ID_ARRAY;
    }

    if (strcmp(type_name, "String") == 0 ||
        strcmp(type_name, "string") == 0) {
        return TYPE_ID_STRING;
    }

    if (strcmp(type_name, "Symbol") == 0 ||
        strcmp(type_name, "Sym") == 0) {
        return TYPE_ID_SYMBOL;
    }

    if (strcmp(type_name, "Dict") == 0 ||
        strcmp(type_name, "Dictionary") == 0 ||
        strcmp(type_name, "Map") == 0) {
        return TYPE_ID_DICT;
    }

    /* Function types */
    if (strcmp(type_name, "Closure") == 0 ||
        strcmp(type_name, "Lambda") == 0 ||
        strcmp(type_name, "Function") == 0) {
        return TYPE_ID_CLOSURE;
    }

    /* Reference types */
    if (strcmp(type_name, "Box") == 0 ||
        strcmp(type_name, "Ref") == 0) {
        return TYPE_ID_BOX;
    }

    /* Concurrency types */
    if (strcmp(type_name, "Channel") == 0 ||
        strcmp(type_name, "Chan") == 0) {
        return TYPE_ID_CHANNEL;
    }

    if (strcmp(type_name, "Thread") == 0) {
        return TYPE_ID_THREAD;
    }

    /* Control flow types */
    if (strcmp(type_name, "Error") == 0) {
        return TYPE_ID_ERROR;
    }

    if (strcmp(type_name, "Atom") == 0 ||
        strcmp(type_name, "Atomic") == 0) {
        return TYPE_ID_ATOM;
    }

    /* Tuple types */
    if (strcmp(type_name, "Tuple") == 0) {
        return TYPE_ID_TUPLE;
    }

    if (strcmp(type_name, "NamedTuple") == 0) {
        return TYPE_ID_NAMED_TUPLE;
    }

    /* Type system types */
    if (strcmp(type_name, "Generic") == 0) {
        return TYPE_ID_GENERIC;
    }

    if (strcmp(type_name, "Kind") == 0 ||
        strcmp(type_name, "Type") == 0) {
        return TYPE_ID_KIND;
    }

    if (strcmp(type_name, "Nothing") == 0 ||
        strcmp(type_name, "Unit") == 0 ||
        strcmp(type_name, "Void") == 0) {
        return TYPE_ID_NOTHING;
    }

    /* Unknown type - use generic */
    return TYPE_ID_GENERIC;
}

/*
 * TypeID to name mapping (for debugging/codegen comments)
 */
const char* type_id_to_name(TypeID type_id) {
    switch (type_id) {
        case TYPE_ID_INT:         return "Int";
        case TYPE_ID_FLOAT:       return "Float";
        case TYPE_ID_CHAR:        return "Char";
        case TYPE_ID_PAIR:        return "Pair";
        case TYPE_ID_ARRAY:       return "Array";
        case TYPE_ID_STRING:      return "String";
        case TYPE_ID_SYMBOL:      return "Symbol";
        case TYPE_ID_DICT:        return "Dict";
        case TYPE_ID_CLOSURE:     return "Closure";
        case TYPE_ID_BOX:         return "Box";
        case TYPE_ID_CHANNEL:     return "Channel";
        case TYPE_ID_THREAD:      return "Thread";
        case TYPE_ID_ERROR:       return "Error";
        case TYPE_ID_ATOM:        return "Atom";
        case TYPE_ID_TUPLE:       return "Tuple";
        case TYPE_ID_NAMED_TUPLE: return "NamedTuple";
        case TYPE_ID_GENERIC:     return "Generic";
        case TYPE_ID_KIND:        return "Kind";
        case TYPE_ID_NOTHING:     return "Nothing";
        default:                  return "Unknown";
    }
}

/*
 * Check if TypeID is valid
 */
bool is_valid_type_id(TypeID type_id) {
    return type_id >= 0 && type_id < TYPE_ID_MAX;
}

/* ============== Phase 24: Inline Allocation Metadata Queries ============== */

/*
 * Compile-time metadata for inline allocation
 *
 * These tables mirror the runtime TypeMetadata and provide compile-time
 * access to can_inline and inline_threshold for code generation.
 *
 * Values must match runtime/src/memory/region_metadata.c
 */

/* can_inline values for each type */
static const bool g_can_inline[TYPE_ID_MAX] = {
    [TYPE_ID_INT]         = true,   /* Int: can_inline=true */
    [TYPE_ID_FLOAT]       = true,   /* Float: can_inline=true */
    [TYPE_ID_CHAR]        = true,   /* Char: can_inline=true */
    [TYPE_ID_PAIR]        = true,   /* Pair: can_inline=true */
    [TYPE_ID_ARRAY]       = false,  /* Array: can_inline=false */
    [TYPE_ID_STRING]      = false,  /* String: can_inline=false */
    [TYPE_ID_SYMBOL]      = true,   /* Symbol: can_inline=true */
    [TYPE_ID_DICT]        = false,  /* Dict: can_inline=false */
    [TYPE_ID_CLOSURE]     = false,  /* Closure: can_inline=false */
    [TYPE_ID_BOX]         = true,   /* Box: can_inline=true */
    [TYPE_ID_CHANNEL]     = false,  /* Channel: can_inline=false */
    [TYPE_ID_THREAD]      = false,  /* Thread: can_inline=false */
    [TYPE_ID_ERROR]       = true,   /* Error: can_inline=true */
    [TYPE_ID_ATOM]        = true,   /* Atom: can_inline=true */
    [TYPE_ID_TUPLE]       = true,   /* Tuple: can_inline=true */
    [TYPE_ID_NAMED_TUPLE] = true,   /* NamedTuple: can_inline=true */
    [TYPE_ID_GENERIC]     = false,  /* Generic: can_inline=false */
    [TYPE_ID_KIND]        = false,  /* Kind: can_inline=false */
    [TYPE_ID_NOTHING]     = true,   /* Nothing: can_inline=true */
};

/* inline_threshold values for each type */
static const size_t g_inline_threshold[TYPE_ID_MAX] = {
    [TYPE_ID_INT]         = 16,  /* Int: inline_threshold=16 */
    [TYPE_ID_FLOAT]       = 16,  /* Float: inline_threshold=16 */
    [TYPE_ID_CHAR]        = 8,   /* Char: inline_threshold=8 */
    [TYPE_ID_PAIR]        = 56,  /* Pair: inline_threshold=56 */
    [TYPE_ID_ARRAY]       = 0,   /* Array: inline_threshold=0 (not inlineable) */
    [TYPE_ID_STRING]      = 0,   /* String: inline_threshold=0 (not inlineable) */
    [TYPE_ID_SYMBOL]      = 24,  /* Symbol: inline_threshold=24 */
    [TYPE_ID_DICT]        = 0,   /* Dict: inline_threshold=0 (not inlineable) */
    [TYPE_ID_CLOSURE]     = 0,   /* Closure: inline_threshold=0 (not inlineable) */
    [TYPE_ID_BOX]         = 32,  /* Box: inline_threshold=32 */
    [TYPE_ID_CHANNEL]     = 0,   /* Channel: inline_threshold=0 (not inlineable) */
    [TYPE_ID_THREAD]      = 0,   /* Thread: inline_threshold=0 (not inlineable) */
    [TYPE_ID_ERROR]       = 32,  /* Error: inline_threshold=32 */
    [TYPE_ID_ATOM]        = 16,  /* Atom: inline_threshold=16 */
    [TYPE_ID_TUPLE]       = 48,  /* Tuple: inline_threshold=48 */
    [TYPE_ID_NAMED_TUPLE] = 64,  /* NamedTuple: inline_threshold=64 */
    [TYPE_ID_GENERIC]     = 0,   /* Generic: inline_threshold=0 (not inlineable) */
    [TYPE_ID_KIND]        = 0,   /* Kind: inline_threshold=0 (not inlineable) */
    [TYPE_ID_NOTHING]     = 8,   /* Nothing: inline_threshold=8 */
};

/*
 * Check if a type can be inline-allocated
 */
bool type_id_can_inline(TypeID type_id) {
    if (!is_valid_type_id(type_id)) return false;
    return g_can_inline[type_id];
}

/*
 * Get the inline threshold for a type
 */
size_t type_id_inline_threshold(TypeID type_id) {
    if (!is_valid_type_id(type_id)) return 0;
    return g_inline_threshold[type_id];
}
