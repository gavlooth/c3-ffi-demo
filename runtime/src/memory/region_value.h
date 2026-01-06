/*
 * region_value.h - Region-Aware Value Constructors
 *
 * This file provides region-aware variants of all Value constructors.
 * These are used when the compiler emits code with explicit region management.
 *
 * Each mk_*_region function takes a Region* parameter and allocates
 * the value (and any child allocations) in that region.
 *
 * This integrates Region-RC with the existing Value type system.
 */

#ifndef OMNI_REGION_VALUE_H
#define OMNI_REGION_VALUE_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Forward declarations
struct Value;
struct Region;
typedef struct Value Value;
typedef struct Region Region;

// Path from runtime/src/memory/ to src/runtime/types.h
#include "../../../src/runtime/types.h"
#include "region_core.h"

// ============================================================================
// Region-Aware Value Constructors
// ============================================================================

/*
 * alloc_val_region - Allocate a Value in the given region
 *
 * This is the primitive that all other mk_*_region functions use.
 */
Value* alloc_val_region(Region* r, Tag tag);

/*
 * Scalar value constructors (no child allocations)
 */
Value* mk_int_region(Region* r, long i);
Value* mk_char_region(Region* r, long codepoint);
Value* mk_float_region(Region* r, double f);

// Singletons - don't allocate, just return global
Value* mk_nil_region(Region* r);  // Ignores region, returns singleton
Value* mk_nothing_region(Region* r);  // Ignores region, returns singleton

/*
 * String-allocating constructors
 */
Value* mk_sym_region(Region* r, const char* s);
Value* mk_code_region(Region* r, const char* s);
Value* mk_string_region(Region* r, const char* s, size_t len);
Value* mk_string_cstr_region(Region* r, const char* s);
Value* mk_error_region(Region* r, const char* msg);

/*
 * Pair/Cell constructor (deep copy children via region_alloc)
 */
Value* mk_cell_region(Region* r, Value* car, Value* cdr);

/*
 * Lambda/Closure constructor
 */
Value* mk_lambda_region(Region* r, Value* params, Value* body, Value* env);
Value* mk_lambda_with_defaults_region(Region* r, Value* params, Value* body, Value* env, Value* defaults);

/*
 * Box constructor
 */
Value* mk_box_region(Region* r, Value* initial);

/*
 * Continuation constructor
 */
Value* mk_cont_region(Region* r, ContFn fn, Value* menv, int tag);

/*
 * Complex constructors (use malloc for external resources, but Value in region)
 */
Value* mk_chan_region(Region* r, int capacity);
Value* mk_process_region(Region* r, Value* thunk);
Value* mk_bounce_region(Region* r, Value* fn, Value* args);

/*
 * Port and Syntax constructors
 */
Value* mk_port_region(Region* r, FILE* fp, const char* filename, int mode);
Value* mk_syntax_region(Region* r, const char* name, Value* literals, Value* rules, Value* def_env);

/*
 * Grammar and FFI constructors
 */
Value* mk_grammar_region(Region* r, struct PikaGrammar* grammar, const char* name);
Value* mk_ffi_lib_region(Region* r, void* handle, const char* name);
Value* mk_ffi_ptr_region(Region* r, void* ptr, const char* type_name, int owned);

/*
 * Thread constructor
 */
Value* mk_thread_region(Region* r, Value* thunk);

// ============================================================================
// Primitive Constructor (code pointer)
// ============================================================================

Value* mk_prim_region(Region* r, PrimFn fn);

#endif // OMNI_REGION_VALUE_H
