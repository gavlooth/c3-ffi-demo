/*
 * region_value.h - Region-Aware Value Constructors
 *
 * This file provides region-aware variants of all Obj constructors.
 * These are used when the compiler emits code with explicit region management.
 *
 * Each mk_*_region function takes a Region* parameter and allocates
 * the object (and any child allocations) in that region.
 *
 * This integrates Region-RC with the Obj type system.
 */

#ifndef OMNI_REGION_VALUE_H
#define OMNI_REGION_VALUE_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Use the clean public API definition of Obj
#include "../../include/omni.h"
#include "region_core.h"

// ============================================================================
// Region-Aware Value Constructors
// ============================================================================

/*
 * alloc_obj_region - Allocate an Obj in the given region
 *
 * This is the primitive that all other mk_*_region functions use.
 */
Obj* alloc_obj_region(Region* r, int tag);

/*
 * Scalar value constructors (no child allocations)
 */
Obj* mk_int_region(Region* r, long i);
Obj* mk_char_region(Region* r, long codepoint);
Obj* mk_float_region(Region* r, double f);

// Singletons - don't allocate, just return global
Obj* mk_nil_region(Region* r);  // Ignores region, returns singleton
Obj* mk_nothing_region(Region* r);  // Ignores region, returns singleton

/*
 * String-allocating constructors
 */
Obj* mk_sym_region(Region* r, const char* s);
Obj* mk_code_region(Region* r, const char* s);
Obj* mk_string_region(Region* r, const char* s, size_t len);
Obj* mk_string_cstr_region(Region* r, const char* s);
Obj* mk_error_region(Region* r, const char* msg);

/*
 * Pair/Cell constructor (deep copy children via region_alloc)
 */
Obj* mk_cell_region(Region* r, Obj* car, Obj* cdr);

/*
 * Lambda/Closure constructor
 */
Obj* mk_lambda_region(Region* r, Obj* params, Obj* body, Obj* env);
Obj* mk_lambda_with_defaults_region(Region* r, Obj* params, Obj* body, Obj* env, Obj* defaults);

/*
 * Box constructor
 */
Obj* mk_box_region(Region* r, Obj* initial);

/*
 * Continuation constructor
 * Note: ContFn is defined in omni.h as typedef Obj* (*OmniContFn)(Obj* val) ?
 * No, omni.h doesn't define ContFn directly, it defines ClosureFn.
 * But we need to support legacy-ish behavior or update it.
 * Let's assume ContFn is void* for now or use ClosureFn if appropriate.
 * Actually omni.h doesn't export ContFn.
 */
typedef Obj* (*ContFn)(Obj* val);
Obj* mk_cont_region(Region* r, ContFn fn, Obj* menv, int tag);

/*
 * Primitive Constructor (code pointer)
 */
/* PrimFn is defined in omni.h? No, generic Obj* (*)(Obj*, Obj*) */
typedef Obj* (*PrimFn)(Obj* args, Obj* menv); 
Obj* mk_prim_region(Region* r, PrimFn fn);

/*
 * Thread constructor
 */
Obj* mk_thread_region(Region* r, Obj* thunk);

/*
 * Port and Syntax constructors
 */
Obj* mk_port_region(Region* r, FILE* fp, const char* filename, int mode);
Obj* mk_syntax_region(Region* r, const char* name, Obj* literals, Obj* rules, Obj* def_env);

/*
 * FFI constructors
 */
Obj* mk_ffi_lib_region(Region* r, void* handle, const char* name);
Obj* mk_ffi_ptr_region(Region* r, void* ptr, const char* type_name, int owned);

/*
 * Collection constructors
 */
Obj* mk_array_region(Region* r, int capacity);
Obj* mk_dict_region(Region* r);
Obj* mk_keyword_region(Region* r, const char* name);
Obj* mk_tuple_region(Region* r, Obj** items, int count);
Obj* mk_named_tuple_region(Region* r, Obj** keys, Obj** values, int count);
Obj* mk_generic_region(Region* r, const char* name);
Obj* mk_kind_region(Region* r, const char* name, Obj** params, int param_count);

#endif // OMNI_REGION_VALUE_H