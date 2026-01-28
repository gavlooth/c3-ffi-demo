/*
 * region_metadata.c - Region-Level Type Metadata Implementation
 *
 * Initializes and manages type metadata for regions.
 */

#include "region_core.h"  /* Must be first - pulls in Region, Arena */
#include "../../include/omni.h"  /* For struct Obj definition (needed for sizeof/offsetof) */
#include "transmigrate.h"  /* PHASE 33.4: For pair_batch_alloc */
#include "../internal_types.h"  /* Array/Dict/NamedTuple layouts (Phase 34.2) */
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * CTRR TRANSMIGRATION: Clone and Trace Implementations
 * ============================================================================
 *
 * These functions implement the CTRR contract for metadata-driven transmigration.
 * Each type provides:
 *   - clone_fn: Allocate a copy of the object into the destination region
 *   - trace_fn: Enumerate all Obj* child slots for rewriting
 *
 * Reference: runtime/docs/CTRR_TRANSMIGRATION.md
 */

/* ============================================================================
 * SCALAR (BOXED) TYPES (no child pointers)
 * ============================================================================
 * Important nuance:
 * - Immediate (tagged-pointer) values NEVER reach metadata clone/trace, because
 *   transmigrate() short-circuits them via IS_IMMEDIATE().
 *
 * Therefore, the metadata entries for TAG_INT/TAG_FLOAT/TAG_CHAR must handle
 * the BOXED forms of these scalars (e.g., ints outside the immediate range).
 *
 * CTRR requirement:
 * - Boxed scalar objects MUST be cloned into the destination region; returning
 *   the old pointer would keep a pointer into the closing source region.
 */

static Obj* clone_int_boxed(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)tmp_ctx;
    return mk_int_region(dest, old_obj->i);
}

static Obj* clone_float_boxed(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)tmp_ctx;
    return mk_float_region(dest, old_obj->f);
}

static Obj* clone_char_boxed(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)tmp_ctx;
    return mk_char_region(dest, old_obj->i);
}

static Obj* clone_nothing_singleton(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)old_obj;
    (void)dest;
    (void)tmp_ctx;
    /* Canonicalize Nothing to the singleton object (never region-owned). */
    return mk_nothing();
}

static void trace_noop(Obj* obj, OmniVisitSlotFn visit_slot, void* ctx) {
    (void)obj;
    (void)visit_slot;
    (void)ctx;
    /* No child pointers to trace */
}

/* ============================================================================
 * TAG_PAIR (cons cell)
 * ============================================================================
 * Layout: obj->a and obj->b are both Obj* pointers (car and cdr)
 */

static struct Obj* clone_pair(struct Obj* old_obj, struct Region* dest, void* tmp_ctx) {
    /* PHASE 33.4: Use batch allocator for pairs to reduce overhead
     *
     * The tmp_ctx is a TransmigrateCloneCtx* (declared in transmigrate.h).
     * This avoids relying on internal struct layouts from transmigrate.c.
     */

    TransmigrateCloneCtx* ctx = (TransmigrateCloneCtx*)tmp_ctx;

    struct Obj* new_obj = NULL;
    if (ctx && ctx->pair_batch) {
        /* Use batch allocator for better performance (allocates in dest region) */
        new_obj = pair_batch_alloc(ctx->pair_batch, dest);
    }

    /* If batch allocation failed or not available, fallback to region allocation */
    if (!new_obj) {
        new_obj = region_alloc(dest, sizeof(struct Obj));
    }

    if (!new_obj) return NULL;

    /* Copy scalar fields (tag, etc.) */
    new_obj->tag = old_obj->tag;
    new_obj->mark = 0;
    new_obj->generation = 0;
    new_obj->tethered = 0;
    new_obj->owner_region = dest;  /* Issue 2 P4.3: Set owning region for transmigrated objects */

    /* Copy a and b as OLD pointers (will be rewritten by generic loop) */
    new_obj->a = old_obj->a;
    new_obj->b = old_obj->b;
    new_obj->is_pair = 1;  /* Preserve is_pair flag */

    return new_obj;
}

static void trace_pair(Obj* obj, OmniVisitSlotFn visit_slot, void* ctx) {
    /* Phase 34.3: Skip visitor calls for immediates (not graph edges). */
    if (!IS_IMMEDIATE(obj->a)) visit_slot(&obj->a, ctx);
    if (!IS_IMMEDIATE(obj->b)) visit_slot(&obj->b, ctx);
}

/* ============================================================================
 * TAG_BOX
 * ============================================================================
 * Layout: obj->a contains the boxed value (canonical layout)
 */

static Obj* clone_box(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)tmp_ctx;

    Obj* new_obj = region_alloc(dest, sizeof(Obj));
    if (!new_obj) return NULL;

    new_obj->tag = old_obj->tag;
    new_obj->mark = 0;
    new_obj->generation = 0;
    new_obj->tethered = 0;
    new_obj->owner_region = dest;  /* Issue 2 P4.3: Set owning region for transmigrated objects */
    new_obj->a = old_obj->a;  /* Copy as old pointer */

    return new_obj;
}

static void trace_box(Obj* obj, OmniVisitSlotFn visit_slot, void* ctx) {
    visit_slot(&obj->a, ctx);
}

/* ============================================================================
 * TAG_STRING, TAG_SYM, TAG_KEYWORD
 * ============================================================================
 * Layout: obj->ptr points to null-terminated char array
 * Note: These have NO child Obj* pointers (just char data)
 */

static Obj* clone_string_like(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)tmp_ctx;

    Obj* new_obj = region_alloc(dest, sizeof(Obj));
    if (!new_obj) return NULL;

    new_obj->tag = old_obj->tag;

    /* Deep copy the string data into destination region */
    if (old_obj->ptr) {
        const char* s = (const char*)old_obj->ptr;
        size_t len = strlen(s);
        char* s_copy = (char*)region_alloc(dest, len + 1);
        if (!s_copy) return NULL;
        strcpy(s_copy, s);
        new_obj->ptr = s_copy;
    }

    return new_obj;
}

static void trace_string_like(Obj* obj, OmniVisitSlotFn visit_slot, void* ctx) {
    (void)obj;
    (void)visit_slot;
    (void)ctx;
    /* No Obj* child pointers - string data is just chars */
}

/* ============================================================================
 * TAG_CLOSURE
 * ============================================================================
 * Layout: obj->ptr points to Closure struct with capture array
 *
 * Closure struct (from omni.h):
 *   struct Closure {
 *       ClosureFn fn;           // Function pointer
 *       Obj** captures;         // Array of Obj* pointers
 *       int capture_count;      // Number of captures
 *       int arity;              // Expected number of arguments
 *       const char* name;       // Function name (for debugging)
 *   };
 */

static Obj* clone_closure(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)tmp_ctx;

    Obj* new_obj = region_alloc(dest, sizeof(Obj));
    if (!new_obj) return NULL;

    new_obj->tag = old_obj->tag;

    if (old_obj->ptr) {
        Closure* old_c = (Closure*)old_obj->ptr;

        /* Allocate new Closure struct in destination */
        Closure* new_c = (Closure*)region_alloc(dest, sizeof(Closure));
        if (!new_c) return NULL;

        /* Copy scalar fields */
        *new_c = *old_c;

        /* CTRR SOUNDNESS: NULL out captures pointer before conditional allocation
         * This prevents dangling pointers from source region if capture_count == 0 */
        new_c->captures = NULL;

        /* Allocate new capture array in destination */
        if (old_c->capture_count > 0) {
            new_c->captures = (Obj**)region_alloc(dest, sizeof(Obj*) * old_c->capture_count);
            if (!new_c->captures) return NULL;
            /* Copy capture pointers as OLD pointers (will be rewritten) */
            for (int i = 0; i < old_c->capture_count; i++) {
                new_c->captures[i] = old_c->captures[i];
            }
        }

        new_obj->ptr = new_c;
    }

    return new_obj;
}

static void trace_closure(Obj* obj, OmniVisitSlotFn visit_slot, void* ctx) {
    if (obj->ptr) {
        Closure* c = (Closure*)obj->ptr;
        /* Visit each capture slot */
        for (int i = 0; i < c->capture_count; i++) {
            visit_slot(&c->captures[i], ctx);
        }
    }
}

/* ============================================================================
 * TAG_ARRAY
 * ============================================================================
 * Layout: obj->ptr points to Array struct
 *
 * Array struct (from internal_types.h):
 *   typedef struct Array {
 *       Obj** data;      // Array of Obj* pointers
 *       int len;
 *       int capacity;
 *   } Array;
 *
 * CRITICAL: Must trace through payload buffer, not just Obj fields
 */

static Obj* clone_array(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)tmp_ctx;

    Obj* new_obj = region_alloc(dest, sizeof(Obj));
    if (!new_obj) return NULL;

    new_obj->tag = old_obj->tag;

    if (old_obj->ptr) {
        Array* old_a = (Array*)old_obj->ptr;

        /* Allocate new Array struct */
        Array* new_a = (Array*)region_alloc(dest, sizeof(Array));
        if (!new_a) return NULL;

        *new_a = *old_a;

        /* CTRR SOUNDNESS: NULL out data pointer before conditional allocation
         * This prevents dangling pointers from source region if capacity == 0 */
        new_a->data = NULL;

        /* Allocate new data buffer sized to capacity */
        if (old_a->capacity > 0) {
            new_a->data = (Obj**)region_alloc(dest, sizeof(Obj*) * old_a->capacity);
            if (!new_a->data) return NULL;
            /* Phase 34.2: bulk copy of element slots.
             * This is safe because children are rewritten by the generic loop
             * (and immediates don't require rewriting). */
            if (old_a->len > 0) {
                memcpy(new_a->data, old_a->data, sizeof(Obj*) * (size_t)old_a->len);
            }
        }

        new_obj->ptr = new_a;
    }

    return new_obj;
}

/* OPTIMIZATION: Prefetch macro for array trace (Opt 2C)
 * Uses compiler builtin when available for cache prefetching.
 */
#ifdef __GNUC__
#define TRACE_PREFETCH_READ(addr) __builtin_prefetch((addr), 0, 3)
#else
#define TRACE_PREFETCH_READ(addr) ((void)(addr))
#endif

static void trace_array(Obj* obj, OmniVisitSlotFn visit_slot, void* ctx) {
    if (obj->ptr) {
        Array* a = (Array*)obj->ptr;

        /* Phase 34.2: If the array has never had boxed elements inserted,
         * it cannot contain region pointers and can skip tracing entirely. */
        if (!a->has_boxed_elems) {
            return;
        }

        /* PHASE 33.3: Immediate array fast-path optimization
         *
         * For arrays containing immediate values (integers, chars, bools),
         * we can skip the expensive visitor/remap activity since immediates:
         * - Do not require pointer rewriting (they are self-contained)
         * - Cannot create cycles (no heap pointers)
         *
         * This optimization reduces overhead for immediate-heavy arrays by ~10-25x.
         */
        for (int i = 0; i < a->len; i++) {
            /* OPTIMIZATION (Opt 2C): Prefetch 4 elements ahead
             * This hides memory latency during array traversal by prefetching
             * upcoming elements into the cache before they are accessed.
             */
            if (i + 4 < a->len && !IS_IMMEDIATE(a->data[i + 4])) {
                TRACE_PREFETCH_READ(a->data[i + 4]);
            }

            Obj* v = a->data[i];
            /* Skip immediates - they don't need pointer rewriting */
            if (IS_IMMEDIATE(v)) continue;
            visit_slot(&a->data[i], ctx);
        }
    }
}

/* ============================================================================
 * TAG_DICT
 * ============================================================================
 * Layout: obj->ptr points to Dict struct containing HashMap
 *
 * Dict struct (from internal_types.h):
 *   typedef struct Dict {
 *       HashMap map;
 *   } Dict;
 *
 * HashMap contains HashEntry** buckets where each HashEntry has:
 *   void* key;   // Obj* key
 *   void* value; // Obj* value
 *   struct HashEntry* next;
 *
 * CRITICAL: Must trace through hash table buckets
 */

static Obj* clone_dict(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)tmp_ctx;

    Obj* new_obj = region_alloc(dest, sizeof(Obj));
    if (!new_obj) return NULL;

    new_obj->tag = old_obj->tag;

    if (old_obj->ptr) {
        /* Forward declaration for HashMap structures */
        typedef struct HashEntry {
            void* key;
            void* value;
            struct HashEntry* next;
        } HashEntry;

        typedef struct {
            HashEntry** buckets;
            size_t bucket_count;
            size_t entry_count;
            float load_factor;
            int had_alloc_failure;
        } HashMap;

        typedef struct {
            HashMap map;
        } Dict;

        Dict* old_d = (Dict*)old_obj->ptr;

        /* Allocate new Dict struct */
        Dict* new_d = (Dict*)region_alloc(dest, sizeof(Dict));
        if (!new_d) return NULL;

        /* Copy scalar fields */
        new_d->map = old_d->map;

        /* Allocate new buckets array */
        if (old_d->map.bucket_count > 0) {
            new_d->map.buckets = (HashEntry**)region_alloc(dest, sizeof(HashEntry*) * old_d->map.bucket_count);
            if (!new_d->map.buckets) return NULL;

            /* Copy entries - allocate new entries for each bucket */
            for (size_t i = 0; i < old_d->map.bucket_count; i++) {
                HashEntry* old_entry = old_d->map.buckets[i];
                HashEntry** new_slot = &new_d->map.buckets[i];

                while (old_entry) {
                    /* Allocate new entry */
                    HashEntry* new_entry = (HashEntry*)region_alloc(dest, sizeof(HashEntry));
                    if (!new_entry) return NULL;

                    /* Copy key and value as OLD pointers (will be rewritten) */
                    new_entry->key = old_entry->key;
                    new_entry->value = old_entry->value;
                    new_entry->next = NULL;

                    /* Link into new bucket */
                    *new_slot = new_entry;
                    new_slot = &new_entry->next;

                    old_entry = old_entry->next;
                }
            }
        }

        new_obj->ptr = new_d;
    }

    return new_obj;
}

static void trace_dict(Obj* obj, OmniVisitSlotFn visit_slot, void* ctx) {
    if (obj->ptr) {
        typedef struct HashEntry {
            void* key;
            void* value;
            struct HashEntry* next;
        } HashEntry;

        typedef struct {
            HashEntry** buckets;
            size_t bucket_count;
            size_t entry_count;
            float load_factor;
            int had_alloc_failure;
        } HashMap;

        typedef struct {
            HashMap map;
        } Dict;

        Dict* d = (Dict*)obj->ptr;

        /* Trace through all hash table entries */
        for (size_t i = 0; i < d->map.bucket_count; i++) {
            HashEntry* entry = d->map.buckets[i];
            while (entry) {
                /* Visit both key and value as Obj* slots */
                visit_slot((Obj**)&entry->key, ctx);
                visit_slot((Obj**)&entry->value, ctx);
                entry = entry->next;
            }
        }
    }
}

/* ============================================================================
 * TAG_ATOM, TAG_CHANNEL, TAG_THREAD
 * ============================================================================
 * These types are handles with no Obj* children or simple scalar fields
 *
 * - ATOM: Thread-safe mutable reference (single value or OS-level handle)
 * - CHANNEL: OS-level synchronization primitive
 * - THREAD: OS-level thread handle
 *
 * CTRR CLONE-WRAPPER POLICY (Phase 32.3):
 * Handle wrappers are CTRR-safe by allocating a fresh wrapper in the
 * destination region that shares the same underlying handle payload.
 *
 * This ensures "everything can escape" guarantee:
 * - The wrapper itself lives in the destination region
 * - The underlying handle (ptr) is shared between wrappers
 * - region_exit() never invalidates the destination wrapper
 *
 * Rationale: Avoids introducing a "pinned" lifetime class and keeps
 * CTRR reasoning local and simple.
 */

static Obj* clone_handle(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)tmp_ctx;

    /* CTRR: Allocate fresh wrapper in destination region */
    Obj* new_obj = region_alloc(dest, sizeof(Obj));
    if (!new_obj) return NULL;

    /* Copy scalar fields */
    new_obj->tag = old_obj->tag;

    /* Copy handle payload - this is the underlying OS handle or mutex,
     * NOT an Obj* pointer, so sharing it is safe */
    new_obj->ptr = old_obj->ptr;

    return new_obj;
}

static void trace_noop_handle(Obj* obj, OmniVisitSlotFn visit_slot, void* ctx) {
    (void)obj;
    (void)visit_slot;
    (void)ctx;
    /* No Obj* children to trace for handles */
}

/* ============================================================================
 * TAG_GENERIC
 * ============================================================================
 * Layout: obj->ptr points to Generic struct with method table
 *
 * Generic struct (from omni.h):
 *   typedef struct Generic {
 *       const char* name;
 *       MethodInfo* methods;    // Linked list of methods
 *       int method_count;
 *   } Generic;
 *
 * CTRR GENERIC POLICY (Phase 32.4):
 * Generics are pinned global objects that define polymorphic function dispatch.
 * The method table and its param_kinds are globally allocated and shared.
 *
 * Rationale: Generics are typically defined once and used everywhere.
 * Treating them as pinned global objects:
 * - Preserves the complete method list without deep copying
 * - Avoids complex graph cloning of MethodInfo structures
 * - Matches the semantic model of generics as global method tables
 *
 * Under this policy, clone_generic returns the original object unchanged.
 * This is safe because Generic objects are not region-owned.
 */

static Obj* clone_generic(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)dest;
    (void)tmp_ctx;

    /* CTRR: Generics are pinned global objects - return the original */
    return old_obj;
}

static void trace_generic(Obj* obj, OmniVisitSlotFn visit_slot, void* ctx) {
    if (obj->ptr) {
        typedef struct MethodInfo {
            Obj** param_kinds;
            int param_count;
            void* impl;
            int specificity;
            struct MethodInfo* next;
        } MethodInfo;

        typedef struct {
            const char* name;
            MethodInfo* methods;
            int method_count;
        } Generic;

        Generic* g = (Generic*)obj->ptr;

        /* Trace through method parameter kinds (Kind objects) */
        for (MethodInfo* m = g->methods; m; m = m->next) {
            for (int i = 0; i < m->param_count; i++) {
                visit_slot(&m->param_kinds[i], ctx);
            }
        }
    }
}

/* ============================================================================
 * TAG_KIND
 * ============================================================================
 * Layout: obj->ptr points to Kind struct
 *
 * Kind struct (from omni.h):
 *   typedef struct Kind {
 *       char* name;
 *       Obj** params;
 *       int param_count;
 *   } Kind;
 */

static Obj* clone_kind(Obj* old_obj, Region* dest, void* tmp_ctx) {
    (void)tmp_ctx;

    Obj* new_obj = region_alloc(dest, sizeof(Obj));
    if (!new_obj) return NULL;

    new_obj->tag = old_obj->tag;

    if (old_obj->ptr) {
        typedef struct {
            char* name;
            Obj** params;
            int param_count;
        } Kind;

        Kind* old_k = (Kind*)old_obj->ptr;

        /* Allocate new Kind struct */
        Kind* new_k = (Kind*)region_alloc(dest, sizeof(Kind));
        if (!new_k) return NULL;

        new_k->name = old_k->name;  /* String literals are shared */
        new_k->param_count = old_k->param_count;

        /* Allocate and copy params array */
        if (old_k->param_count > 0) {
            new_k->params = (Obj**)region_alloc(dest, sizeof(Obj*) * old_k->param_count);
            if (!new_k->params) return NULL;
            for (int i = 0; i < old_k->param_count; i++) {
                new_k->params[i] = old_k->params[i];
            }
        } else {
            new_k->params = NULL;
        }

        new_obj->ptr = new_k;
    }

    return new_obj;
}

static void trace_kind(Obj* obj, OmniVisitSlotFn visit_slot, void* ctx) {
    if (obj->ptr) {
        typedef struct {
            char* name;
            Obj** params;
            int param_count;
        } Kind;

        Kind* k = (Kind*)obj->ptr;

        /* Trace through params array (Kind objects) */
        for (int i = 0; i < k->param_count; i++) {
            visit_slot(&k->params[i], ctx);
        }
    }
}

/* ========== Metadata Initialization ========== */

/*
 * init_core_type_metadata - Initialize metadata for all core types
 *
 * This function populates the region's type_table with metadata
 * for all core OmniLisp types. Called during region_create().
 */
static void init_core_type_metadata(Region* r) {
    if (!r) return;
    
    /* Allocate type table for all core types */
    r->num_types = TYPE_ID_MAX;
    r->type_table = (TypeMetadata*)calloc(r->num_types, sizeof(TypeMetadata));
    
    if (!r->type_table) {
        /* Memory allocation failed */
        return;
    }
    
    /* TYPE_ID_INT */
    r->type_table[TYPE_ID_INT] = (TypeMetadata){
        .name = "Int",
        .type_id = TYPE_ID_INT,
        .size = sizeof(struct Obj),  /* Must be full Obj size, not just payload */
        .alignment = 8,
        .num_pointer_fields = 0,
        .pointer_offsets = {0},
        .can_inline = true,
        .inline_threshold = 64,
        .clone = clone_int_boxed,      /* CTRR: boxed scalar clone (immediates bypass metadata) */
        .trace = trace_noop,           /* CTRR: no child pointers */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Integer type (stored in immediate or heap)"
    };
    
    /* TYPE_ID_FLOAT */
    r->type_table[TYPE_ID_FLOAT] = (TypeMetadata){
        .name = "Float",
        .type_id = TYPE_ID_FLOAT,
        .size = sizeof(struct Obj),  /* Must be full Obj size, not just payload */
        .alignment = 8,
        .num_pointer_fields = 0,
        .pointer_offsets = {0},
        .can_inline = true,
        .inline_threshold = 64,
        .clone = clone_float_boxed,    /* CTRR: boxed scalar clone */
        .trace = trace_noop,           /* CTRR: no child pointers */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Floating-point type"
    };

    /* TYPE_ID_CHAR */
    r->type_table[TYPE_ID_CHAR] = (TypeMetadata){
        .name = "Char",
        .type_id = TYPE_ID_CHAR,
        .size = sizeof(struct Obj),  /* Must be full Obj size, not just payload */
        .alignment = 8,
        .num_pointer_fields = 0,
        .pointer_offsets = {0},
        .can_inline = true,
        .inline_threshold = 64,
        .clone = clone_char_boxed,     /* CTRR: boxed scalar clone */
        .trace = trace_noop,           /* CTRR: no child pointers */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Character type (often immediate)"
    };
    
    /* TYPE_ID_PAIR (Cons cell) */
    r->type_table[TYPE_ID_PAIR] = (TypeMetadata){
        .name = "Pair",
        .type_id = TYPE_ID_PAIR,
        .size = sizeof(struct Obj),  /* Current size */
        .alignment = 8,
        .num_pointer_fields = 2,
        .pointer_offsets = {offsetof(struct Obj, a), offsetof(struct Obj, b)},
        .can_inline = true,
        .inline_threshold = 64,
        .clone = clone_pair,          /* CTRR: pair-specific clone */
        .trace = trace_pair,          /* CTRR: pair-specific trace */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Cons cell (pair)"
    };
    
    /* TYPE_ID_ARRAY */
    r->type_table[TYPE_ID_ARRAY] = (TypeMetadata){
        .name = "Array",
        .type_id = TYPE_ID_ARRAY,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 1,
        .pointer_offsets = {offsetof(struct Obj, ptr)},
        .can_inline = false,  /* Arrays are usually large */
        .inline_threshold = 0,
        .clone = clone_array,         /* CTRR: array-specific clone */
        .trace = trace_array,         /* CTRR: trace data buffer */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Array type"
    };
    
    /* TYPE_ID_STRING */
    r->type_table[TYPE_ID_STRING] = (TypeMetadata){
        .name = "String",
        .type_id = TYPE_ID_STRING,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 0,
        .pointer_offsets = {0},
        .can_inline = false,
        .inline_threshold = 0,
        .clone = clone_string_like,   /* CTRR: string-specific clone */
        .trace = trace_string_like,   /* CTRR: no child pointers */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "String type"
    };
    
    /* TYPE_ID_SYMBOL */
    r->type_table[TYPE_ID_SYMBOL] = (TypeMetadata){
        .name = "Symbol",
        .type_id = TYPE_ID_SYMBOL,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 0,
        .pointer_offsets = {0},
        .can_inline = true,
        .inline_threshold = 24,
        .clone = clone_string_like,   /* CTRR: symbol-specific clone */
        .trace = trace_string_like,   /* CTRR: no child pointers */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Symbol type (often interned)"
    };
    
    /* TYPE_ID_DICT */
    r->type_table[TYPE_ID_DICT] = (TypeMetadata){
        .name = "Dict",
        .type_id = TYPE_ID_DICT,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 1,
        .pointer_offsets = {offsetof(struct Obj, ptr)},
        .can_inline = false,
        .inline_threshold = 0,
        .clone = clone_dict,          /* CTRR: dict-specific clone */
        .trace = trace_dict,          /* CTRR: trace hash table */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Dictionary (hash map)"
    };
    
    /* TYPE_ID_CLOSURE */
    r->type_table[TYPE_ID_CLOSURE] = (TypeMetadata){
        .name = "Closure",
        .type_id = TYPE_ID_CLOSURE,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 1,
        .pointer_offsets = {offsetof(struct Obj, ptr)},
        .can_inline = false,
        .inline_threshold = 0,
        .clone = clone_closure,       /* CTRR: closure-specific clone */
        .trace = trace_closure,       /* CTRR: trace captures */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Function closure"
    };
    
    /* TYPE_ID_BOX */
    r->type_table[TYPE_ID_BOX] = (TypeMetadata){
        .name = "Box",
        .type_id = TYPE_ID_BOX,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 1,
        .pointer_offsets = {offsetof(struct Obj, a)},
        .can_inline = true,
        .inline_threshold = 32,
        .clone = clone_box,            /* CTRR: box-specific clone */
        .trace = trace_box,            /* CTRR: box-specific trace */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Boxed value"
    };
    
    /* TYPE_ID_CHANNEL - DIRECTIVE: NO CHANNELS
     * This metadata is kept for enum stability but Channels are not created at runtime.
     * Use algebraic effects for structured concurrency instead. */
    r->type_table[TYPE_ID_CHANNEL] = (TypeMetadata){
        .name = "Channel",
        .type_id = TYPE_ID_CHANNEL,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 1,
        .pointer_offsets = {offsetof(struct Obj, ptr)},
        .can_inline = false,
        .inline_threshold = 0,
        .clone = clone_handle,         /* CTRR: handle types use shared clone */
        .trace = trace_noop_handle,    /* CTRR: no Obj* children */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "REMOVED - use algebraic effects"
    };

    /* TYPE_ID_THREAD */
    r->type_table[TYPE_ID_THREAD] = (TypeMetadata){
        .name = "Thread",
        .type_id = TYPE_ID_THREAD,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 1,
        .pointer_offsets = {offsetof(struct Obj, ptr)},
        .can_inline = false,
        .inline_threshold = 0,
        .clone = clone_handle,         /* CTRR: handle types use shared clone */
        .trace = trace_noop_handle,    /* CTRR: no Obj* children */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Thread handle"
    };
    
    /* TYPE_ID_ERROR */
    r->type_table[TYPE_ID_ERROR] = (TypeMetadata){
        .name = "Error",
        .type_id = TYPE_ID_ERROR,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 1,
        .pointer_offsets = {offsetof(struct Obj, ptr)},
        .can_inline = true,
        .inline_threshold = 32,
        .clone = clone_string_like,   /* CTRR: error-specific clone (string message) */
        .trace = trace_string_like,   /* CTRR: no child pointers */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Error value"
    };
    
    /* TYPE_ID_ATOM */
    r->type_table[TYPE_ID_ATOM] = (TypeMetadata){
        .name = "Atom",
        .type_id = TYPE_ID_ATOM,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 0,
        .pointer_offsets = {0},
        .can_inline = true,
        .inline_threshold = 16,
        .clone = clone_handle,         /* CTRR: atoms are handles */
        .trace = trace_noop_handle,    /* CTRR: no Obj* children (ptr is void*) */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Atom (symbol-like)"
    };

    /* TYPE_ID_SET (Issue 24: Set data structure) */
    r->type_table[TYPE_ID_SET] = (TypeMetadata){
        .name = "Set",
        .type_id = TYPE_ID_SET,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 1,
        .pointer_offsets = {offsetof(struct Obj, ptr)},
        .can_inline = true,
        .inline_threshold = 64,
        .clone = clone_dict,            /* CTRR: set uses same clone as dict (hashmap-based) */
        .trace = trace_dict,            /* CTRR: trace hashmap entries */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Set data structure (Issue 24)"
    };

    /* TYPE_ID_RESERVED_15 (Was TYPE_ID_NAMED_TUPLE - REMOVED 2026-01-15)
     * Named tuples are deprecated - use dicts instead */

    /* TYPE_ID_GENERIC */
    r->type_table[TYPE_ID_GENERIC] = (TypeMetadata){
        .name = "Generic",
        .type_id = TYPE_ID_GENERIC,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 1,
        .pointer_offsets = {offsetof(struct Obj, ptr)},
        .can_inline = false,
        .inline_threshold = 0,
        .clone = clone_generic,         /* CTRR: generic-specific clone */
        .trace = trace_generic,         /* CTRR: trace method param kinds */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Generic type (parametric)"
    };

    /* TYPE_ID_KIND */
    r->type_table[TYPE_ID_KIND] = (TypeMetadata){
        .name = "Kind",
        .type_id = TYPE_ID_KIND,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 1,
        .pointer_offsets = {offsetof(struct Obj, ptr)},
        .can_inline = false,
        .inline_threshold = 0,
        .clone = clone_kind,            /* CTRR: kind-specific clone */
        .trace = trace_kind,            /* CTRR: trace params array */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Kind (type object)"
    };
    
    /* TYPE_ID_NOTHING */
    r->type_table[TYPE_ID_NOTHING] = (TypeMetadata){
        .name = "Nothing",
        .type_id = TYPE_ID_NOTHING,
        .size = sizeof(struct Obj),
        .alignment = 8,
        .num_pointer_fields = 0,
        .pointer_offsets = {0},
        .can_inline = true,
        .inline_threshold = 8,
        .clone = clone_nothing_singleton, /* CTRR: singleton canonicalization */
        .trace = trace_noop,           /* CTRR: no child pointers */
        .destroy = NULL,
        .equals = NULL,
        .hash = NULL,
        .debug_info = "Nothing type (unit)"
    };
}

/* ========== Public API ========== */

/*
 * type_metadata_init - Initialize type metadata for a region
 *
 * This function is called during region_create() to set up the
 * type_table with metadata for all core types.
 */
void type_metadata_init(Region* r) {
    if (!r) return;

    init_core_type_metadata(r);
}

/*
 * type_metadata_get - Get metadata for a type_id
 *
 * @param region: The region containing the metadata
 * @param type_id: The type identifier
 * @return: Pointer to TypeMetadata, or NULL if invalid
 */
const TypeMetadata* type_metadata_get(const Region* region, TypeID type_id) {
    if (!region) return NULL;

    if (type_id >= 0 && (uint32_t)type_id < region->num_types) {
        return &region->type_table[type_id];
    }
    return NULL;
}

/*
 * type_metadata_print - Print metadata information for debugging
 */
void type_metadata_print(const TypeMetadata* meta) {
    if (!meta) {
        printf("TypeMetadata: NULL\n");
        return;
    }
    
    printf("TypeMetadata:\n");
    printf("  name: %s\n", meta->name);
    printf("  type_id: %d\n", meta->type_id);
    printf("  size: %zu bytes\n", meta->size);
    printf("  alignment: %zu bytes\n", meta->alignment);
    printf("  pointer_fields: %d\n", meta->num_pointer_fields);
    printf("  can_inline: %s\n", meta->can_inline ? "yes" : "no");
    printf("  inline_threshold: %zu bytes\n", meta->inline_threshold);
    printf("  debug_info: %s\n", meta->debug_info ? meta->debug_info : "none");
}

/*
 * type_metadata_dump - Dump all metadata from a region
 */
void type_metadata_dump(const Region* region) {
    if (!region) {
        printf("Region: NULL\n");
        return;
    }
    
    printf("\n=== Region Metadata Dump ===\n");
    printf("num_types: %u\n", region->num_types);
    printf("type_table: %p\n\n", (void*)region->type_table);
    
    if (!region->type_table) {
        printf("  (no metadata table)\n");
        return;
    }
    
    for (uint32_t i = 0; i < region->num_types; i++) {
        const TypeMetadata* meta = &region->type_table[i];
        if (meta->name) {
            printf("[%2u] %s\n", i, meta->name);
        }
    }
    
    printf("=============================\n\n");
}
