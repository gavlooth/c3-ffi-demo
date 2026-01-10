/*
 * immer_bridge.h - C interface to Immer persistent data structures
 *
 * This provides a C-compatible wrapper around Immer's C++ library
 * for use with OmniLisp's FFI mechanism.
 *
 * All functions use void* for opaque handles to Immer structures.
 * Memory management: caller must call *_free() when done.
 */

#ifndef IMMER_BRIDGE_H
#define IMMER_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Vector (immer::vector<void*>) ========== */

/* Create empty vector */
void* immer_vector_empty(void);

/* Push element to back, returns NEW vector (original unchanged) */
void* immer_vector_push(void* vec, void* elem);

/* Set element at index, returns NEW vector */
void* immer_vector_set(void* vec, int idx, void* elem);

/* Get element at index (NULL if out of bounds) */
void* immer_vector_get(void* vec, int idx);

/* Get vector size */
int immer_vector_size(void* vec);

/* Pop last element, returns NEW vector */
void* immer_vector_pop(void* vec);

/* Take first n elements, returns NEW vector */
void* immer_vector_take(void* vec, int n);

/* Drop first n elements, returns NEW vector */
void* immer_vector_drop(void* vec, int n);

/* Free vector */
void immer_vector_free(void* vec);

/* ========== Map (immer::map<void*, void*>) ========== */

/* Create empty map */
void* immer_map_empty(void);

/* Associate key with value, returns NEW map */
void* immer_map_assoc(void* m, void* key, void* val);

/* Get value for key (returns not_found if missing) */
void* immer_map_get(void* m, void* key, void* not_found);

/* Remove key, returns NEW map */
void* immer_map_dissoc(void* m, void* key);

/* Check if key exists (1=yes, 0=no) */
int immer_map_contains(void* m, void* key);

/* Get map size (number of key-value pairs) */
int immer_map_count(void* m);

/* Free map */
void immer_map_free(void* m);

/* ========== Set (immer::set<void*>) ========== */

/* Create empty set */
void* immer_set_empty(void);

/* Add element, returns NEW set */
void* immer_set_conj(void* s, void* elem);

/* Remove element, returns NEW set */
void* immer_set_disj(void* s, void* elem);

/* Check if element exists (1=yes, 0=no) */
int immer_set_contains(void* s, void* elem);

/* Get set size */
int immer_set_count(void* s);

/* Free set */
void immer_set_free(void* s);

/* ========== Iteration Support ========== */

/*
 * For iteration, we use a simple callback approach.
 * The callback receives each element and a user context pointer.
 * Return 0 to continue, non-zero to stop early.
 */

typedef int (*immer_iter_fn)(void* elem, void* ctx);
typedef int (*immer_iter_kv_fn)(void* key, void* val, void* ctx);

/* Iterate over vector elements */
void immer_vector_foreach(void* vec, immer_iter_fn fn, void* ctx);

/* Iterate over map key-value pairs */
void immer_map_foreach(void* m, immer_iter_kv_fn fn, void* ctx);

/* Iterate over set elements */
void immer_set_foreach(void* s, immer_iter_fn fn, void* ctx);

#ifdef __cplusplus
}
#endif

#endif /* IMMER_BRIDGE_H */
