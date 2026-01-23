#ifndef OMNI_INTERNAL_TYPES_H
#define OMNI_INTERNAL_TYPES_H

#include "../include/omni.h"
#include "util/hashmap.h"

/* Global region access (defined in runtime.c) */
extern Region* _global_region;
void _ensure_global_region(void);

/* Array struct */
typedef struct Array {
    Obj** data;
    int len;
    int capacity;
    /* Phase 34.2: Monotonic flag indicating whether this array may contain
     * any boxed (non-immediate) elements.
     *
     * Why:
     * - If false, array cannot contain region pointers and can skip trace
     *   entirely during transmigration (fast-path for immediate-only arrays).
     * - If true, trace must scan elements and visit boxed slots for rewrite.
     *
     * NOTE: This is monotonic under mutation: once true, it stays true.
     */
    bool has_boxed_elems;
} Array;

/* Dict struct */
typedef struct Dict {
    HashMap map;
} Dict;

/* Set struct (Issue 24)
 * Implemented as a hashmap where element is both key and value.
 * This provides O(1) average-case add/remove/contains operations.
 */
typedef struct Set {
    HashMap map;
} Set;

/* DateTime struct (Issue 25)
 * Stores time as Unix timestamp (seconds since 1970-01-01 00:00:00 UTC)
 * plus timezone offset in seconds from UTC.
 * Internal representation allows both UTC and local time operations.
 */
typedef struct DateTime {
    int64_t timestamp;     /* Unix timestamp (seconds since epoch) */
    int32_t tz_offset;     /* Timezone offset in seconds from UTC */
} DateTime;

/* NamedTuple: Immutable set of Key-Value pairs
 * Optimized for small sets of keys (linear scan) */
typedef struct NamedTuple {
    Obj** keys;
    Obj** values;
    int count;
} NamedTuple;

/* Generic Tuple and Kind are defined in omni.h */
typedef struct Tuple {
    int count;
    Obj* items[];
} Tuple;

#endif
