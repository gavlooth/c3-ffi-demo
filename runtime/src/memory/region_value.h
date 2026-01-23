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
#include "region_metadata.h"  /* For TypeID and TypeMetadata */

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
 * alloc_obj_typed - Allocate an Obj in the given region using type_id
 *
 * OPTIMIZATION (T-opt-region-metadata): This is the new allocation API
 * that uses compile-time type_id instead of runtime tag.
 *
 * @param r: The region to allocate from
 * @param type_id: Compile-time type identifier (TYPE_ID_INT, TYPE_ID_PAIR, etc.)
 * @return: Pointer to allocated Obj, or NULL on failure
 *
 * This function looks up the TypeMetadata for the given type_id and allocates
 * the object with the correct size. It also sets the tag field for compatibility
 * with existing code (mapped from type_id).
 *
 * Future optimization: Once all code uses type_id, we can eliminate the tag field
 * entirely and rely solely on compile-time type information.
 */
Obj* alloc_obj_typed(Region* r, TypeID type_id);

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
 * SPECIALIZED CONSTRUCTORS (T-opt-specialized-constructors)
 * Batch-allocate multiple objects in a single call to reduce allocation overhead.
 */

/*
 * mk_list_region - Build a list of n integers in a single batch allocation
 * Reduces allocation count from O(n) to O(1) for list construction.
 * Each element is an integer from 0 to n-1.
 */
Obj* mk_list_region(Region* r, int n);

/*
 * mk_tree_region - Build a complete binary tree of given depth in a single batch
 * Reduces allocation count from O(2^depth) to O(1) for tree construction.
 * Returns a tree where leaves are integers and internal nodes are cells.
 */
Obj* mk_tree_region(Region* r, int depth);

/*
 * mk_list_from_array_region - Build a list from an array of values in one batch
 * Takes an array of n Obj* values and builds a linked list in a single allocation.
 */
Obj* mk_list_from_array_region(Region* r, Obj** values, int n);

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

/*
 * BATCH ALLOCATION CONSTRUCTORS (T-opt-batch-alloc-array, T-opt-batch-alloc-struct)
 * Single-allocation constructors for better cache locality and reduced overhead.
 */

/*
 * mk_array_region_batch - Batch allocate array + data in single allocation
 * Reduces allocation count from 3 to 1 for array construction.
 * Allocates: Obj wrapper + Array struct + data array as one contiguous block.
 */
Obj* mk_array_region_batch(Region* r, int capacity);

/*
 * mk_array_of_ints_region - Create pre-filled integer array in single allocation
 * Optimized for common case of homogeneous integer arrays.
 * Combines: Obj + Array + data + integer values in one allocation.
 */
Obj* mk_array_of_ints_region(Region* r, long* values, int count);

/*
 * mk_dict_region_batch - Batch allocate dict + buckets in single allocation
 * Reduces allocation count from 3 to 1 for dict construction.
 */
Obj* mk_dict_region_batch(Region* r, int initial_buckets);
Obj* mk_dict_region(Region* r);
Obj* mk_set_region(Region* r);  /* Issue 24: Set data structure */
Obj* mk_datetime_region(Region* r, int64_t timestamp, int32_t tz_offset);  /* Issue 25: DateTime */
Obj* mk_keyword_region(Region* r, const char* name);
Obj* mk_tuple_region(Region* r, Obj** items, int count);
Obj* mk_named_tuple_region(Region* r, Obj** keys, Obj** values, int count);
Obj* mk_generic_region(Region* r, const char* name);
Obj* mk_kind_region(Region* r, const char* name, Obj** params, int param_count);

#endif // OMNI_REGION_VALUE_H