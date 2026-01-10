#ifndef OMNI_INTERNAL_TYPES_H
#define OMNI_INTERNAL_TYPES_H

#include "../include/omni.h"
#include "util/hashmap.h"

/* Global region access (defined in runtime.c) */
extern Region* _global_region;
void _ensure_global_region(void);

typedef struct Array {
    Obj** data;
    int len;
    int capacity;
    /* Phase 34.2: Monotonic flag indicating whether this array may contain
     * any boxed (non-immediate) elements.
     *
     * Why:
     * - If false, the array cannot contain region pointers and can skip trace
     *   entirely during transmigration (fast-path for immediate-only arrays).
     * - If true, trace must scan elements and visit boxed slots for rewrite.
     *
     * NOTE: This is monotonic under mutation: once true, it stays true. */
    bool has_boxed_elems;
} Array;

// Dict: Region-resident HashMap
typedef struct Dict {
    HashMap map;
} Dict;

// NamedTuple: Immutable set of Key-Value pairs
// Optimized for small sets of keys (linear scan)
typedef struct NamedTuple {
    Obj** keys;
    Obj** values;
    int count;
} NamedTuple;

/* Generic and Kind are defined in omni.h */

typedef struct Tuple {
    int count;
    Obj* items[];
} Tuple;

#endif
