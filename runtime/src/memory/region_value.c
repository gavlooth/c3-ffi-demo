/*
 * region_value.c - Implementation of Region-Aware Obj Constructors
 *
 * This file implements mk_*_region functions for the new Obj type.
 */

#include "region_value.h"
#include "../internal_types.h"

// ============================================================================
// Primitive Allocator
// ============================================================================

/*
 * Mapping from TypeID to ObjTag for compatibility with existing code.
 *
 * This mapping allows us to use new type_id-based allocation while
 * maintaining compatibility with code that still uses tag-based operations.
 * Eventually, all code will use type_id and we can eliminate the tag field.
 *
 * NOTE: The TAG enum and TypeID enum have DIFFERENT orderings (TAG has
 * additional types like SYM, BOX inserted between primitives and collections).
 * Simple arithmetic (tag = type_id + 1) does NOT work. Use lookup tables.
 */
static inline int type_id_to_tag(TypeID type_id) {
    /* Lookup table: TypeID -> TAG */
    static const int type_id_to_tag_table[] = {
        [TYPE_ID_INT]     = TAG_INT,      /* 0 -> 1 */
        [TYPE_ID_FLOAT]   = TAG_FLOAT,    /* 1 -> 2 */
        [TYPE_ID_CHAR]    = TAG_CHAR,     /* 2 -> 3 */
        [TYPE_ID_PAIR]    = TAG_PAIR,     /* 3 -> 4 */
        [TYPE_ID_ARRAY]   = TAG_ARRAY,    /* 4 -> 12 */
        [TYPE_ID_STRING]  = TAG_STRING,   /* 5 -> 14 */
        [TYPE_ID_SYMBOL]  = TAG_SYM,      /* 6 -> 5 */
        [TYPE_ID_DICT]    = TAG_DICT,     /* 7 -> 13 */
        [TYPE_ID_CLOSURE] = TAG_CLOSURE,  /* 8 -> 7 */
        [TYPE_ID_BOX]     = TAG_BOX,      /* 9 -> 6 */
        [TYPE_ID_CHANNEL] = TAG_CHANNEL,  /* 10 -> 8 */
        [TYPE_ID_THREAD]  = TAG_THREAD,   /* 11 -> 11 */
        [TYPE_ID_ERROR]   = TAG_ERROR,    /* 12 -> 9 */
        [TYPE_ID_ATOM]    = TAG_ATOM,     /* 13 -> 10 */
        [TYPE_ID_SET]     = TAG_SET,      /* 14 -> 16 */
        [15]              = TAG_KEYWORD,  /* RESERVED_15 -> 15 (KEYWORD) */
        [TYPE_ID_GENERIC] = TAG_GENERIC,  /* 16 -> 18 */
        [TYPE_ID_KIND]    = TAG_KIND,     /* 17 -> 19 */
        [TYPE_ID_NOTHING] = TAG_NOTHING,  /* 18 -> 20 */
    };
    if (type_id < 0 || type_id >= (TypeID)(sizeof(type_id_to_tag_table) / sizeof(type_id_to_tag_table[0]))) {
        return TAG_GENERIC;  /* Fallback for unknown types */
    }
    return type_id_to_tag_table[type_id];
}

/*
 * Reverse mapping from ObjTag to TypeID.
 *
 * Phase 25: Enable constructors to use alloc_obj_typed() by converting
 * legacy tag values to TypeID constants.
 *
 * NOTE: Uses lookup table because TAG and TypeID enums are not aligned.
 */
static inline TypeID tag_to_type_id(int tag) {
    /* Lookup table: TAG -> TypeID */
    static const TypeID tag_to_type_id_table[] = {
        [0]            = TYPE_ID_INT,     /* Invalid tag 0 -> fallback */
        [TAG_INT]      = TYPE_ID_INT,     /* 1 -> 0 */
        [TAG_FLOAT]    = TYPE_ID_FLOAT,   /* 2 -> 1 */
        [TAG_CHAR]     = TYPE_ID_CHAR,    /* 3 -> 2 */
        [TAG_PAIR]     = TYPE_ID_PAIR,    /* 4 -> 3 */
        [TAG_SYM]      = TYPE_ID_SYMBOL,  /* 5 -> 6 */
        [TAG_BOX]      = TYPE_ID_BOX,     /* 6 -> 9 */
        [TAG_CLOSURE]  = TYPE_ID_CLOSURE, /* 7 -> 8 */
        [TAG_CHANNEL]  = TYPE_ID_CHANNEL, /* 8 -> 10 */
        [TAG_ERROR]    = TYPE_ID_ERROR,   /* 9 -> 12 */
        [TAG_ATOM]     = TYPE_ID_ATOM,    /* 10 -> 13 */
        [TAG_THREAD]   = TYPE_ID_THREAD,  /* 11 -> 11 */
        [TAG_ARRAY]    = TYPE_ID_ARRAY,   /* 12 -> 4 */
        [TAG_DICT]     = TYPE_ID_DICT,    /* 13 -> 7 */
        [TAG_STRING]   = TYPE_ID_STRING,  /* 14 -> 5 */
        [TAG_KEYWORD]  = TYPE_ID_GENERIC, /* 15 -> 16 (no direct TypeID) */
        [TAG_SET]      = TYPE_ID_SET,     /* 16 -> 14 */
        [TAG_DATETIME] = TYPE_ID_GENERIC, /* 17 -> 16 (no direct TypeID) */
        [TAG_GENERIC]  = TYPE_ID_GENERIC, /* 18 -> 16 */
        [TAG_KIND]     = TYPE_ID_KIND,    /* 19 -> 17 */
        [TAG_NOTHING]  = TYPE_ID_NOTHING, /* 20 -> 18 */
    };
    if (tag < 0 || tag >= (int)(sizeof(tag_to_type_id_table) / sizeof(tag_to_type_id_table[0]))) {
        return TYPE_ID_GENERIC;  /* Fallback for unknown tags */
    }
    return tag_to_type_id_table[tag];
}

Obj* alloc_obj_region(Region* r, int tag) {
    /*
     * Phase 25: Delegate to alloc_obj_typed() to leverage type metadata optimization.
     * This converts legacy tag-based allocation to TypeID-based allocation.
     */
    TypeID type_id = tag_to_type_id(tag);
    return alloc_obj_typed(r, type_id);
}

/*
 * alloc_obj_typed - Allocate an Obj using compile-time type_id
 *
 * OPTIMIZATION (T-opt-region-metadata): Uses region-level type metadata
 * to determine allocation strategy based on type-specific properties.
 *
 * This function now uses region_alloc_typed() which considers:
 * - can_inline: Whether the type can use inline buffer
 * - inline_threshold: Maximum size for inline allocation
 * - size: The actual size of the object
 */
Obj* alloc_obj_typed(Region* r, TypeID type_id) {
    if (!r) {
        // Fallback to malloc if no region (shouldn't happen in practice)
        Obj* o = malloc(sizeof(Obj));
        if (!o) return NULL;
        o->mark = 0;
        o->tag = type_id_to_tag(type_id);
        o->generation = 0;
        o->tethered = 0;
        o->owner_region = NULL;  /* No owning region for malloc'd objects */
        return o;
    }

    /* Look up metadata to get size */
    const TypeMetadata* meta = type_metadata_get(r, type_id);
    size_t size = meta ? meta->size : sizeof(Obj);

    /* Use metadata-aware allocation (checks can_inline and inline_threshold) */
    Obj* o = region_alloc_typed(r, type_id, size);
    if (!o) return NULL;

    o->mark = 1;
    o->tag = type_id_to_tag(type_id);  /* Set tag for compatibility */
    o->generation = r->generation;  /* Initialize with region's current generation */
    o->tethered = 0;
    o->owner_region = r;  /* Issue 1 P1: Set owning region */

    /* TODO: Future optimization - store type_id instead of tag */
    /* For now, we maintain both for compatibility during transition */

    return o;
}

// ============================================================================
// Singleton Values
// ============================================================================

Obj* mk_nil_region(Region* r) {
    (void)r;
    return NULL; // NIL is NULL in new runtime
}

static Obj nothing_obj = { .tag = TAG_NOTHING };
Obj* mk_nothing_region(Region* r) {
    (void)r;
    return &nothing_obj;
}

// ============================================================================
// Scalar Value Constructors
// ============================================================================

Obj* mk_int_region(Region* r, long i) {
    Obj* o = alloc_obj_region(r, TAG_INT);
    if (!o) return NULL;
    o->i = i;
    return o;
}

Obj* mk_char_region(Region* r, long codepoint) {
    // Note: runtime often uses immediates for char, this is for boxed backup
    Obj* o = alloc_obj_region(r, TAG_CHAR);
    if (!o) return NULL;
    o->i = codepoint; // Use i for codepoint storage in boxed char
    return o;
}

Obj* mk_float_region(Region* r, double f) {
    Obj* o = alloc_obj_region(r, TAG_FLOAT);
    if (!o) return NULL;
    o->f = f;
    return o;
}

// ============================================================================
// String/Sym Constructors
// ============================================================================

Obj* mk_sym_region(Region* r, const char* s) {
    /*
     * Defensive constructor:
     *   Treat NULL as a valid symbol object with a NULL payload pointer.
     *
     * Rationale:
     *   Several parts of the runtime/test-suite assume mk_sym(NULL) produces a
     *   valid symbol object (useful for OOM/edge-case testing without crashing),
     *   and that the underlying payload pointer remains NULL.
     */
    Obj* o = alloc_obj_region(r, TAG_SYM);
    if (!o) return NULL;

    if (!s) {
        o->ptr = NULL;
        return o;
    }

    size_t len = strlen(s);
    char* buf = (char*)region_alloc(r, len + 1);
    if (!buf) return NULL;
    strcpy(buf, s);
    o->ptr = buf;
    return o;
}

/* Proper string type implementation (T-wire-string-literal-02/03) */
Obj* mk_string_region(Region* r, const char* s, size_t len) {
    if (!s) return NULL;
    Obj* o = alloc_obj_region(r, TAG_STRING);
    if (!o) return NULL;

    /* Allocate space for the string (including null terminator) */
    char* buf = (char*)region_alloc(r, len + 1);
    if (!buf) return NULL;

    /* Copy the string data */
    memcpy(buf, s, len);
    buf[len] = '\0';
    o->ptr = buf;
    return o;
}

Obj* mk_string_cstr_region(Region* r, const char* s) {
    if (!s) return NULL;
    return mk_string_region(r, s, strlen(s));
}

Obj* mk_code_region(Region* r, const char* s) {
    // Map to SYM for now, or add TAG_CODE to omni.h
    return mk_sym_region(r, s);
}

Obj* mk_error_region(Region* r, const char* msg) {
    /*
     * Defensive constructor:
     *   Treat NULL as a valid error object with a NULL payload pointer.
     *
     * This keeps error reporting code simple: callers can always expect an error
     * object back from mk_error(), even in edge-case tests, and the tests assert
     * that the underlying message pointer is NULL when constructed from NULL.
     */
    Obj* o = alloc_obj_region(r, TAG_ERROR);
    if (!o) return NULL;

    if (!msg) {
        o->ptr = NULL;
        return o;
    }

    size_t len = strlen(msg);
    char* buf = (char*)region_alloc(r, len + 1);
    if (!buf) return NULL;
    strcpy(buf, msg);
    o->ptr = buf;
    return o;
}

Obj* mk_keyword_region(Region* r, const char* name) {
    if (!name) name = "";
    Obj* o = alloc_obj_region(r, TAG_KEYWORD);
    if (!o) return NULL;

    size_t len = strlen(name);
    char* buf = region_alloc(r, len + 1);
    if (!buf) return NULL;
    strcpy(buf, name);
    o->ptr = buf;
    return o;
}

// ============================================================================
// Cell/Box Constructors
// ============================================================================

Obj* mk_cell_region(Region* r, Obj* car, Obj* cdr) {
    Obj* o = alloc_obj_region(r, TAG_PAIR); // TAG_PAIR = T_CELL legacy
    if (!o) return NULL;
    o->a = car;
    o->b = cdr;
    o->is_pair = 1;
    return o;
}

// ============================================================================
// SPECIALIZED CONSTRUCTORS (T-opt-specialized-constructors)
// Batch-allocate multiple objects in a single call to reduce allocation overhead.
// ============================================================================

/*
 * mk_list_region - Build a list of n integers in a single batch allocation
 *
 * OPTIMIZATION: Instead of n separate allocations (2n total with integers),
 * allocate all objects in a contiguous block and link them together.
 * This reduces allocation count from O(n) to O(1) and improves cache locality.
 */
Obj* mk_list_region(Region* r, int n) {
    if (!r || n <= 0) return NULL;

    // Allocate all cells and integers in a single contiguous block
    // Each cell needs: sizeof(Obj) for the cell + sizeof(Obj) for the integer
    // But since integers are immediate in many cases, we can optimize
    size_t block_size = n * sizeof(Obj);
    Obj* block = (Obj*)region_alloc(r, block_size);
    if (!block) return NULL;

    // Initialize all cells and link them together
    Obj* head = NULL;
    Obj* prev = NULL;

    for (int i = 0; i < n; i++) {
        Obj* cell = &block[i];
        cell->mark = 1;
        cell->tag = TAG_PAIR;
        cell->generation = 0;
        cell->tethered = 0;
        cell->is_pair = 1;

        // Store integer value directly in car (using immediate encoding if possible)
        cell->a = (Obj*)((intptr_t)i << 3 | 0x1);  // Immediate integer encoding
        cell->b = prev;

        if (i == n - 1) head = cell;  // Last element becomes head (reversed order)
        prev = cell;
    }

    return head;
}

/*
 * mk_tree_region - Build a complete binary tree of given depth in a single batch
 *
 * OPTIMIZATION: Pre-allocate all nodes for the tree in a single allocation.
 * A complete binary tree of depth d has 2^d - 1 internal nodes and 2^d leaves.
 * Total nodes = 2^(d+1) - 1.
 *
 * This reduces allocation count from O(2^depth) to O(1).
 */
Obj* mk_tree_region(Region* r, int depth) {
    if (!r || depth < 0) return NULL;

    // Depth 0 is a leaf (single integer)
    if (depth == 0) {
        return mk_int_region(r, 1);
    }

    // Calculate total nodes: 2^(depth+1) - 1
    // Internal nodes: 2^depth - 1, Leaves: 2^depth
    int total_nodes = (1 << (depth + 1)) - 1;
    int leaves = 1 << depth;

    // Allocate all nodes in a single block
    Obj* block = (Obj*)region_alloc(r, total_nodes * sizeof(Obj));
    if (!block) return NULL;

    // Initialize leaves (first 'leaves' elements in the block)
    for (int i = 0; i < leaves; i++) {
        Obj* leaf = &block[i];
        leaf->mark = 1;
        leaf->tag = TAG_INT;
        leaf->generation = 0;
        leaf->tethered = 0;
        leaf->is_pair = 0;
        leaf->i = 1;
    }

    // Initialize internal nodes (remaining elements)
    // Each internal node points to two children
    for (int level = depth - 1; level >= 0; level--) {
        int nodes_at_level = 1 << level;
        int offset = leaves;  // Skip leaves

        // Calculate offset for this level
        for (int l = depth - 1; l > level; l--) {
            offset += (1 << l);
        }

        for (int i = 0; i < nodes_at_level; i++) {
            Obj* node = &block[offset + i];
            node->mark = 1;
            node->tag = TAG_PAIR;
            node->generation = 0;
            node->tethered = 0;
            node->is_pair = 1;

            // Point to children (calculate indices based on level structure)
            int left_child_idx = offset + nodes_at_level + (i * 2);
            int right_child_idx = left_child_idx + 1;

            if (level == depth - 1) {
                // Children are leaves
                node->a = &block[i * 2];
                node->b = &block[i * 2 + 1];
            } else {
                node->a = &block[left_child_idx];
                node->b = &block[right_child_idx];
            }
        }
    }

    // Return root (last element in block)
    return &block[total_nodes - 1];
}

/*
 * mk_list_from_array_region - Build a list from an array of values in one batch
 *
 * OPTIMIZATION: Single allocation for all list cells, improved cache locality.
 */
Obj* mk_list_from_array_region(Region* r, Obj** values, int n) {
    if (!r || !values || n <= 0) return NULL;

    // Allocate all cells in a single block
    Obj* block = (Obj*)region_alloc(r, n * sizeof(Obj));
    if (!block) return NULL;

    // Initialize all cells and link them together
    Obj* head = NULL;
    Obj* prev = NULL;

    for (int i = n - 1; i >= 0; i--) {
        Obj* cell = &block[n - 1 - i];  // Reverse order for correct linking
        cell->mark = 1;
        cell->tag = TAG_PAIR;
        cell->generation = 0;
        cell->tethered = 0;
        cell->is_pair = 1;

        cell->a = values[i];
        cell->b = prev;

        if (!head) head = cell;
        prev = cell;
    }

    return head;
}

Obj* mk_box_region(Region* r, Obj* initial) {
    Obj* o = alloc_obj_region(r, TAG_BOX);
    if (!o) return NULL;
    o->a = initial; // Store value in 'a'
    return o;
}

// ============================================================================
// Collections
// ============================================================================

Obj* mk_array_region(Region* r, int capacity) {
    Obj* o = alloc_obj_region(r, TAG_ARRAY);
    if (!o) return NULL;

    Array* arr = region_alloc(r, sizeof(Array));
    if (!arr) return NULL;

    arr->capacity = capacity > 0 ? capacity : 8;
    arr->len = 0;
    arr->data = region_alloc(r, arr->capacity * sizeof(Obj*));
    arr->has_boxed_elems = false; /* Phase 34.2: assume immediate-only until proven otherwise */

    o->ptr = arr;
    return o;
}

// ============================================================================
// BATCH ALLOCATION CONSTRUCTORS (T-opt-batch-alloc-array, T-opt-batch-alloc-struct)
// Single-allocation constructors for better performance.
// ============================================================================

/*
 * mk_array_region_batch - Allocate array + data in single contiguous block
 *
 * OPTIMIZATION: Instead of 3 separate allocations (Obj, Array, data),
 * allocate everything in one block for better cache locality.
 */
Obj* mk_array_region_batch(Region* r, int capacity) {
    if (!r) return NULL;

    int actual_capacity = capacity > 0 ? capacity : 8;

    // Calculate total size: sizeof(Array) + data array
    size_t total_size = sizeof(Array) + (actual_capacity * sizeof(Obj*));

    // Allocate single block
    void* block = region_alloc(r, total_size);
    if (!block) return NULL;

    // Layout: Array struct first, then data array
    Array* arr = (Array*)block;
    Obj** data = (Obj**)((char*)block + sizeof(Array));

    arr->capacity = actual_capacity;
    arr->len = 0;
    arr->data = data;
    arr->has_boxed_elems = false; /* Phase 34.2 */

    // Allocate Obj wrapper
    Obj* o = alloc_obj_region(r, TAG_ARRAY);
    if (!o) return NULL;

    o->ptr = arr;
    return o;
}

/*
 * mk_array_of_ints_region - Create pre-filled integer array in single allocation
 *
 * OPTIMIZATION: Combine Obj + Array + data + integer values in one allocation.
 * Useful for homogeneous integer arrays (very common in functional programming).
 */
Obj* mk_array_of_ints_region(Region* r, long* values, int count) {
    if (!r || count < 0) return NULL;

    int capacity = count > 0 ? count : 8;

    // Calculate total size: Obj (allocated separately) + Array + data
    // Note: We can't easily include Obj in the same block due to alloc_obj_region
    // But we can batch Array + data + integer objects

    // Allocate integer objects inline with the array
    size_t total_size = sizeof(Array) + (capacity * sizeof(Obj*)) + (count * sizeof(Obj));

    void* block = region_alloc(r, total_size);
    if (!block) return NULL;

    // Layout: Array struct, then data array, then integer objects
    Array* arr = (Array*)block;
    Obj** data = (Obj**)((char*)block + sizeof(Array));
    Obj* int_objs = (Obj*)((char*)data + (capacity * sizeof(Obj*)));

    // Initialize integer objects
    for (int i = 0; i < count; i++) {
        Obj* int_obj = &int_objs[i];
        int_obj->mark = 1;
        int_obj->tag = TAG_INT;
        int_obj->generation = 0;
        int_obj->tethered = 0;
        int_obj->is_pair = 0;
        int_obj->i = values[i];
        data[i] = int_obj;
    }

    arr->capacity = capacity;
    arr->len = count;
    arr->data = data;
    /* Phase 34.2:
     * This constructor creates boxed integer Obj nodes and stores Obj* pointers
     * in the data buffer. Therefore the array contains boxed elements and must
     * be traced during transmigration. */
    arr->has_boxed_elems = true;

    // Allocate Obj wrapper
    Obj* o = alloc_obj_region(r, TAG_ARRAY);
    if (!o) return NULL;

    o->ptr = arr;
    return o;
}

/*
 * mk_dict_region_batch - Allocate dict + buckets in single allocation
 *
 * OPTIMIZATION: Combine Dict struct + bucket array in one block.
 */
Obj* mk_dict_region_batch(Region* r, int initial_buckets) {
    if (!r) return NULL;

    int bucket_count = initial_buckets > 0 ? initial_buckets : 16;

    // Calculate total size: Dict + bucket array
    size_t total_size = sizeof(Dict) + (bucket_count * sizeof(HashEntry*));

    // Allocate single block
    void* block = region_alloc(r, total_size);
    if (!block) return NULL;

    // Layout: Dict struct first, then buckets
    Dict* d = (Dict*)block;
    HashEntry** buckets = (HashEntry**)((char*)block + sizeof(Dict));

    // Initialize buckets to NULL
    memset(buckets, 0, bucket_count * sizeof(HashEntry*));

    d->map.bucket_count = bucket_count;
    d->map.entry_count = 0;
    d->map.load_factor = 0.75f;
    d->map.buckets = buckets;
    d->map.had_alloc_failure = 0;

    // Allocate Obj wrapper
    Obj* o = alloc_obj_region(r, TAG_DICT);
    if (!o) return NULL;

    o->ptr = d;
    return o;
}

Obj* mk_dict_region(Region* r) {
    Obj* o = alloc_obj_region(r, TAG_DICT);
    if (!o) return NULL;

    Dict* d = region_alloc(r, sizeof(Dict));
    if (!d) return NULL;

    // Initialize region-resident hash map
    d->map.bucket_count = 16;
    d->map.entry_count = 0;
    d->map.load_factor = 0.75f;
    d->map.buckets = region_alloc(r, sizeof(HashEntry*) * d->map.bucket_count);
    memset(d->map.buckets, 0, sizeof(HashEntry*) * d->map.bucket_count);
    d->map.had_alloc_failure = 0;
    
    o->ptr = d;
    return o;
}

/* Issue 24: Set data structure
 * Implemented as a hashmap where element is stored as both key and value.
 * This provides O(1) average-case operations.
 */
Obj* mk_set_region(Region* r) {
    Obj* o = alloc_obj_region(r, TAG_SET);
    if (!o) return NULL;

    Set* s = region_alloc(r, sizeof(Set));
    if (!s) return NULL;

    // Initialize region-resident hash map
    s->map.bucket_count = 16;
    s->map.entry_count = 0;
    s->map.load_factor = 0.75f;
    s->map.buckets = region_alloc(r, sizeof(HashEntry*) * s->map.bucket_count);
    memset(s->map.buckets, 0, sizeof(HashEntry*) * s->map.bucket_count);
    s->map.had_alloc_failure = 0;

    o->ptr = s;
    return o;
}

/* Issue 25: DateTime constructor */
Obj* mk_datetime_region(Region* r, int64_t timestamp, int32_t tz_offset) {
    Obj* o = alloc_obj_region(r, TAG_DATETIME);
    if (!o) return NULL;

    DateTime* dt = region_alloc(r, sizeof(DateTime));
    if (!dt) return NULL;

    dt->timestamp = timestamp;
    dt->tz_offset = tz_offset;

    o->ptr = dt;
    return o;
}

/* DEPRECATED: Use mk_array_region() instead - REMOVED 2026-01-15 */
/* Tuples replaced with arrays per design decision (see TODO.md Issue 6 Design Decisions) */
Obj* mk_tuple_region(Region* r, Obj** items, int count) {
    /* Create array instead of tuple */
    Obj* o = mk_array_region(r, count);
    if (!o) return NULL;

    Array* a = (Array*)o->ptr;
    for (int i = 0; i < count; i++) {
        a->data[i] = items[i];
    }
    a->len = count;

    return o;
}

/* DEPRECATED: Use mk_dict_region() instead - REMOVED 2026-01-15 */
/* Named tuples replaced with dicts per design decision (see TODO.md Issue 6 Design Decisions) */
Obj* mk_named_tuple_region(Region* r, Obj** keys, Obj** values, int count) {
    /* Create dict instead of named tuple */
    Obj* dict = mk_dict_region(r);
    if (!dict) return NULL;

    for (int i = 0; i < count; i++) {
        dict_set(dict, keys[i], values[i]);
    }

    return dict;
}

Obj* mk_generic_region(Region* r, const char* name) {
    Obj* o = alloc_obj_region(r, TAG_GENERIC);
    if (!o) return NULL;
    
    Generic* g = region_alloc(r, sizeof(Generic));
    if (!g) return NULL;
    
    size_t len = strlen(name);
    g->name = (char*)region_alloc(r, len + 1);
    strcpy((char*)g->name, name);
    
    o->ptr = g;
    return o;
}

Obj* mk_kind_region(Region* r, const char* name, Obj** params, int param_count) {
    Obj* o = alloc_obj_region(r, TAG_KIND);
    if (!o) return NULL;
    
    Kind* k = region_alloc(r, sizeof(Kind));
    if (!k) return NULL;
    
    size_t len = strlen(name);
    k->name = (char*)region_alloc(r, len + 1);
    strcpy((char*)k->name, name);

    k->param_count = param_count;
    if (param_count > 0 && params) {
        k->params = region_alloc(r, sizeof(Obj*) * param_count);
        for (int i = 0; i < param_count; i++) {
            k->params[i] = params[i];
        }
    } else {
        k->params = NULL;
    }
    
    o->ptr = k;
    return o;
}

// ============================================================================
// Lambda/Closure - Simplified/Stubs
// ============================================================================

Obj* mk_lambda_region(Region* r, Obj* params, Obj* body, Obj* env) {
    (void)r; (void)params; (void)body; (void)env;
    return NULL;
}

Obj* mk_lambda_with_defaults_region(Region* r, Obj* params, Obj* body, Obj* env, Obj* defaults) {
    (void)r; (void)params; (void)body; (void)env; (void)defaults;
    return NULL;
}

// ============================================================================
// Other Stubs
// ============================================================================

Obj* mk_cont_region(Region* r, ContFn fn, Obj* menv, int tag) {
    (void)r; (void)fn; (void)menv; (void)tag;
    return NULL;
}

Obj* mk_prim_region(Region* r, PrimFn fn) {
    (void)r; (void)fn;
    return NULL;
}

Obj* mk_thread_region(Region* r, Obj* thunk) {
    (void)r; (void)thunk;
    return NULL;
}

Obj* mk_port_region(Region* r, FILE* fp, const char* filename, int mode) {
    (void)r; (void)fp; (void)filename; (void)mode;
    return NULL;
}

Obj* mk_syntax_region(Region* r, const char* name, Obj* literals, Obj* rules, Obj* def_env) {
    (void)r; (void)name; (void)literals; (void)rules; (void)def_env;
    return NULL;
}

Obj* mk_ffi_lib_region(Region* r, void* handle, const char* name) {
    (void)r; (void)handle; (void)name;
    return NULL;
}

Obj* mk_ffi_ptr_region(Region* r, void* ptr, const char* type_name, int owned) {
    (void)r; (void)ptr; (void)type_name; (void)owned;
    return NULL;
}
