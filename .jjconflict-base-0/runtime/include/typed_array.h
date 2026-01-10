/*
 * typed_array.h - Typed Array Runtime
 *
 * Typed arrays store unboxed primitive values directly in contiguous memory,
 * avoiding the boxing overhead of generic arrays.
 *
 * Part of Phase 27: Julia-Level Type Specialization.
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 4)
 */

#ifndef OMNILISP_TYPED_ARRAY_H
#define OMNILISP_TYPED_ARRAY_H

#include "../include/omni.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Forward Declarations ============== */
/* Region is defined/typedef'd by omni.h (included above). */

/* ============== Array Element Type Enumeration ============== */

/**
 * ArrayElementType - Type of elements in a typed array
 */
typedef enum {
    ARRAY_TYPE_INT64 = 0,
    ARRAY_TYPE_FLOAT64,
    ARRAY_TYPE_CHAR,
    ARRAY_TYPE_BOOL,
    ARRAY_TYPE_UNKNOWN,
} ArrayElementType;

/* ============== Typed Array Structure ============== */

/**
 * TypedArray - Array of unboxed primitive values
 *
 * Stores primitive values directly in a contiguous buffer without
 * boxing. This provides 20-30x speedup for numeric operations.
 */
typedef struct TypedArray {
    ArrayElementType element_type;  /* Type of elements */
    int rank;                       /* Number of dimensions (1=vector, 2=matrix) */
    int* dimensions;                /* Size of each dimension */
    int64_t total_size;             /* Total number of elements */
    void* data;                     /* Raw data buffer */
    size_t element_size;            /* Size of each element in bytes */
    Region* region;                 /* Owning region */
    int ref_count;                  /* Reference count */
} TypedArray;

/* ============== TypedArray API ============== */

/**
 * Create a new typed array
 *
 * Args:
 *   r: Region to allocate from
 *   element_type: Type of elements
 *   rank: Number of dimensions
 *   dimensions: Array of dimension sizes
 *
 * Returns:
 *   New TypedArray, or NULL on failure
 */
TypedArray* omni_typed_array_create(Region* r,
                                    ArrayElementType element_type,
                                    int rank,
                                    int* dimensions);

/**
 * Free a typed array
 *
 * Args:
 *   arr: Array to free
 */
void omni_typed_array_free(TypedArray* arr);

/**
 * Increment reference count
 *
 * Args:
 *   arr: Array
 */
void omni_typed_array_inc_ref(TypedArray* arr);

/**
 * Decrement reference count (free if zero)
 *
 * Args:
 *   arr: Array
 */
void omni_typed_array_dec_ref(TypedArray* arr);

/* ============== Element Access (Generic Obj* interface) ============== */

/**
 * Get element as Obj* (boxed)
 *
 * Args:
 *   arr: Array
 *   indices: Array of indices
 *
 * Returns:
 *   Boxed element, or NULL if out of bounds
 */
Obj* omni_typed_array_ref(TypedArray* arr, int* indices);

/**
 * Set element from Obj* (unboxed)
 *
 * Args:
 *   arr: Array
 *   indices: Array of indices
 *   value: Boxed value to set
 *
 * Returns:
 *   true on success, false on failure
 */
bool omni_typed_array_set(TypedArray* arr, int* indices, Obj* value);

/* ============== Element Access (Typed interface) ============== */

/**
 * Get int64_t element
 *
 * Args:
 *   arr: Array
 *   indices: Array of indices
 *
 * Returns:
 *   Element value, or 0 if out of bounds
 */
int64_t omni_typed_array_get_int(TypedArray* arr, int* indices);

/**
 * Get double element
 *
 * Args:
 *   arr: Array
 *   indices: Array of indices
 *
 * Returns:
 *   Element value, or 0.0 if out of bounds
 */
double omni_typed_array_get_float(TypedArray* arr, int* indices);

/**
 * Get char element
 *
 * Args:
 *   arr: Array
 *   indices: Array of indices
 *
 * Returns:
 *   Element value, or '\0' if out of bounds
 */
char omni_typed_array_get_char(TypedArray* arr, int* indices);

/**
 * Get bool element
 *
 * Args:
 *   arr: Array
 *   indices: Array of indices
 *
 * Returns:
 *   Element value, or false if out of bounds
 */
bool omni_typed_array_get_bool(TypedArray* arr, int* indices);

/**
 * Set int64_t element
 *
 * Args:
 *   arr: Array
 *   indices: Array of indices
 *   value: Value to set
 *
 * Returns:
 *   true on success, false on failure
 */
bool omni_typed_array_set_int(TypedArray* arr, int* indices, int64_t value);

/**
 * Set double element
 *
 * Args:
 *   arr: Array
 *   indices: Array of indices
 *   value: Value to set
 *
 * Returns:
 *   true on success, false on failure
 */
bool omni_typed_array_set_float(TypedArray* arr, int* indices, double value);

/**
 * Set char element
 *
 * Args:
 *   arr: Array
 *   indices: Array of indices
 *   value: Value to set
 *
 * Returns:
 *   true on success, false on failure
 */
bool omni_typed_array_set_char(TypedArray* arr, int* indices, char value);

/**
 * Set bool element
 *
 * Args:
 *   arr: Array
 *   indices: Array of indices
 *   value: Value to set
 *
 * Returns:
 *   true on success, false on failure
 */
bool omni_typed_array_set_bool(TypedArray* arr, int* indices, bool value);

/* ============== Conversion Functions ============== */

/**
 * Convert a list to a typed array
 *
 * Args:
 *   r: Region to allocate from
 *   element_type: Type of elements
 *   list: List to convert
 *
 * Returns:
 *   New TypedArray, or NULL on failure
 */
TypedArray* omni_list_to_typed_array(Region* r,
                                     ArrayElementType element_type,
                                     Obj* list);

/**
 * Convert a typed array to a list
 *
 * Args:
 *   arr: Array to convert
 *   r: Region for result allocation
 *
 * Returns:
 *   New list, or NULL on failure
 */
Obj* omni_typed_array_to_list(TypedArray* arr, Region* r);

/* ============== Array Operations ============== */

/**
 * Map a function over a typed array
 *
 * Args:
 *   func: Function to apply (Obj* func, Obj* arg -> Obj*)
 *   arr: Array to map
 *   result_type: Type of result array
 *   r: Region for allocation
 *
 * Returns:
 *   New TypedArray with results
 */
TypedArray* omni_typed_array_map(Obj* func,
                                 TypedArray* arr,
                                 ArrayElementType result_type,
                                 Region* r);

/**
 * Filter a typed array
 *
 * Args:
 *   pred: Predicate function
 *   arr: Array to filter
 *   r: Region for allocation
 *
 * Returns:
 *   New TypedArray with matching elements
 */
TypedArray* omni_typed_array_filter(Obj* pred,
                                    TypedArray* arr,
                                    Region* r);

/**
 * Reduce a typed array
 *
 * Args:
 *   func: Reduction function (Obj* acc, Obj* elem -> Obj*)
 *   init: Initial value
 *   arr: Array to reduce
 *
 * Returns:
 *   Reduced value
 */
Obj* omni_typed_array_reduce(Obj* func,
                            Obj* init,
                            TypedArray* arr);

/**
 * Fill a typed array with a value
 *
 * Args:
 *   arr: Array to fill
 *   value: Boxed value to fill with
 *
 * Returns:
 *   true on success
 */
bool omni_typed_array_fill(TypedArray* arr, Obj* value);

/* ============== Utility Functions ============== */

/**
 * Calculate linear index from multi-dimensional indices
 *
 * Args:
 *   arr: Array
 *   indices: Array of indices
 *
 * Returns:
 *   Linear index, or -1 if out of bounds
 */
int64_t omni_typed_array_linear_index(TypedArray* arr, int* indices);

/**
 * Get element size in bytes
 *
 * Args:
 *   type: Element type
 *
 * Returns:
 *   Size in bytes
 */
size_t omni_typed_array_element_size(ArrayElementType type);

/**
 * Get element type name
 *
 * Args:
 *   type: Element type
 *
 * Returns:
 *   String name
 */
const char* omni_typed_array_type_name(ArrayElementType type);

/**
 * Get element type from string
 *
 * Args:
 *   name: Type name
 *
 * Returns:
 *   Element type
 */
ArrayElementType omni_typed_array_type_from_string(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_TYPED_ARRAY_H */
