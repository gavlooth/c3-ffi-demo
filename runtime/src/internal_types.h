#ifndef OMNI_INTERNAL_TYPES_H
#define OMNI_INTERNAL_TYPES_H

#include "../include/omni.h"
#include "util/hashmap.h"

typedef struct Array {
    Obj** data;
    int len;
    int capacity;
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