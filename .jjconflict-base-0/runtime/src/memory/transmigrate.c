/*
 * transmigrate.c - Optimized Iterative Transmigration with Bitmap Cycle Detection
 *
 * Implements high-performance moving of object graphs between regions.
 * Uses low-level Runtime `Obj` types (TAG_*) instead of high-level AST `Value.
 */

/* ============================================================================
 * PHASE 32.5: REMAP PROBING STRATEGY UPGRADE
 * ============================================================================
 * Robin-hood hashing reduces probe length variance by swapping entries
 * when the incoming entry has traveled farther (higher DIB) than the existing entry.
 *
 * To benchmark linear probing, define REMAP_USE_LINEAR_PROBING.
 * Default is robin-hood hashing for better cache locality under adversarial patterns.
 */
#define REMAP_USE_LINEAR_PROBING 0  /* Set to 1 to use linear probing instead */

/* ============================================================================
 * PHASE 33.5: REMAP INSTRUMENTATION AND TUNING
 * ============================================================================
 * Lightweight instrumentation to explain performance regressions and guide
 * probing strategy choices (linear vs robin-hood).
 *
 * Define OMNI_TRANSMIGRATE_STATS to enable statistics collection.
 * This adds minimal overhead and provides valuable insights into remap performance.
 */
#ifdef OMNI_TRANSMIGRATE_STATS
/* Statistics tracking for remap table performance */
typedef struct {
    size_t total_get_calls;     /* Total remap_get calls */
    size_t total_put_calls;     /* Total remap_put calls */
    size_t total_probes;        /* Total probes across all lookups/inserts */
    size_t max_probes;          /* Maximum probes in one operation */
    size_t num_grows;           /* Number of times the table was grown */
} RemapStats;

/* Global statistics (reset at the start of each transmigrate) */
static RemapStats g_remap_stats = {0};

#define REMAP_STATS_INIT() do { \
    memset(&g_remap_stats, 0, sizeof(g_remap_stats)); \
} while (0)

#define REMAP_STATS_INC(field) do { \
    g_remap_stats.field++; \
} while (0)

#define REMAP_STATS_ADD(field, val) do { \
    g_remap_stats.field += (val); \
} while (0)

#define REMAP_STATS_MAX(field, val) do { \
    if ((val) > g_remap_stats.field) { \
        g_remap_stats.field = (val); \
    } \
} while (0)

/* Print statistics at end of transmigrate */
#define REMAP_STATS_PRINT() do { \
    fprintf(stderr, "\n=== Transmigration Statistics ===\n"); \
    fprintf(stderr, "  remap_get calls:   %zu\n", g_remap_stats.total_get_calls); \
    fprintf(stderr, "  remap_put calls:   %zu\n", g_remap_stats.total_put_calls); \
    fprintf(stderr, "  total probes:      %zu\n", g_remap_stats.total_probes); \
    fprintf(stderr, "  max probes:        %zu\n", g_remap_stats.max_probes); \
    fprintf(stderr, "  table grows:       %zu\n", g_remap_stats.num_grows); \
    fprintf(stderr, "===================================\n\n"); \
} while (0)

#else
/* No-op macros when stats are disabled */
#define REMAP_STATS_INIT() do {} while (0)
#define REMAP_STATS_INC(field) do {} while (0)
#define REMAP_STATS_ADD(field, val) do {} while (0)
#define REMAP_STATS_MAX(field, val) do {} while (0)
#define REMAP_STATS_PRINT() do {} while (0)
#endif

#include "transmigrate.h"
#include "region_metadata.h"  /* CTRR: TypeMetadata, type_metadata_get, TypeID */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../../third_party/arena/arena.h"
#include "../../include/omni.h"

/* ============================================================================
 * PHASE 35 (P0): Dense Forwarding-Table Remap
 * ============================================================================
 * The robin-hood remap hash is correct but still costly for long, mostly-acyclic
 * graphs (notably linked lists). When the source region address domain is small
 * enough, we can replace hashing with an O(1) forwarding table indexed by the
 * bitmap word offset:
 *
 *   old_ptr -> forward[word_offset] == new_ptr
 *
 * This is temporary state allocated in the transmigrate() temp arena and freed
 * at the end of the operation (not GC).
 */

#ifndef OMNI_REMAP_FWD_MAX_BYTES
/* Default cap for dense forwarding table memory (tuned by benchmark). */
#define OMNI_REMAP_FWD_MAX_BYTES ((size_t)16u * 1024u * 1024u)
#endif

/* Minimum number of cloned objects before we consider paying the one-time cost
 * of allocating/zeroing a dense forwarding table.
 *
 * Rationale:
 * - For tiny graphs (e.g., 100-element lists), the forwarding table's memset
 *   cost can dominate and make performance worse than robin-hood hashing.
 * - For large graphs (10k+ lists), the allocation cost is amortized and the
 *   O(1) forwarding lookups can win.
 *
 * Tests/bench can override this via `-DOMNI_REMAP_FORWARDING_MIN_CLONES=...`.
 */
#ifndef OMNI_REMAP_FORWARDING_MIN_CLONES
#define OMNI_REMAP_FORWARDING_MIN_CLONES ((size_t)2048u)
#endif

/*
 * OMNI_REMAP_SHOULD_USE_FORWARDING
 *
 * Forwarding table is a performance optimization, so we:
 * - enable it by default when the temp memory footprint is bounded
 * - allow forcing it on for tests/bench builds
 * - allow disabling it for A/B comparisons
 */
#ifdef OMNI_REMAP_DISABLE_FORWARDING
#define OMNI_REMAP_SHOULD_USE_FORWARDING(bytes) (0)
#else
#ifdef OMNI_REMAP_FORCE_FORWARDING
#define OMNI_REMAP_SHOULD_USE_FORWARDING(bytes) (1)
#else
#define OMNI_REMAP_SHOULD_USE_FORWARDING(bytes) ((bytes) <= OMNI_REMAP_FWD_MAX_BYTES)
#endif
#endif

#ifdef OMNI_TRANSMIGRATE_DEBUG_REMAP_MODE
static int g_last_used_forwarding_table = 0;
int omni_transmigrate_debug_used_forwarding_table(void) {
    return g_last_used_forwarding_table;
}
#endif

// ----------------------------------------------------------------------------
// Region Bitmap for Cycle Detection (OPTIMIZATION: 10-100x faster than uthash)
// ----------------------------------------------------------------------------

typedef struct {
    uintptr_t start;
    size_t size_words;
    uint64_t* bits;
} RegionBitmap;

static RegionBitmap* bitmap_create(Region* r, Arena* tmp_arena) {
    if (!r) return NULL;

    // Check if region has any allocations at all (arena or inline buffer)
    if (!r->arena.begin && r->inline_buf.offset == 0) {
        return NULL;  /* Region is completely empty */
    }

    // Find the min/max address range of the source region
    uintptr_t min_addr = UINTPTR_MAX;
    uintptr_t max_addr = 0;

    // CTRR SOUNDNESS: Include arena chunks in address range
    for (ArenaChunk* c = r->arena.begin; c; c = c->next) {
        uintptr_t start = (uintptr_t)c->data;
        uintptr_t end = start + (c->capacity * sizeof(uintptr_t));
        if (start < min_addr) min_addr = start;
        if (end > max_addr) max_addr = end;
    }

    // CTRR SOUNDNESS: Include inline buffer in address range
    // Objects allocated from inline_buf have different addresses than arena chunks
    if (r->inline_buf.offset > 0) {
        uintptr_t inline_start = (uintptr_t)r->inline_buf.buffer;
        uintptr_t inline_end = inline_start + r->inline_buf.offset;
        if (inline_start < min_addr) min_addr = inline_start;
        if (inline_end > max_addr) max_addr = inline_end;
    }

    size_t range = max_addr - min_addr;
    size_t words = (range + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);
    size_t bitmap_len = (words + 63) / 64;

    RegionBitmap* b = arena_alloc(tmp_arena, sizeof(RegionBitmap));
    b->start = min_addr;
    b->size_words = words;
    b->bits = arena_alloc(tmp_arena, bitmap_len * sizeof(uint64_t));
    memset(b->bits, 0, bitmap_len * sizeof(uint64_t));

    return b;
}

// Inline bitmap operations for performance
static inline bool bitmap_test(RegionBitmap* b, void* ptr) {
    if (!b || !ptr) return false;
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < b->start) return false;

    size_t word_offset = (addr - b->start) / sizeof(uintptr_t);
    if (word_offset >= b->size_words) return false;

    size_t bit_index = word_offset % 64;
    size_t word_index = word_offset / 64;

    // Safety check: ensure we don't access beyond bitmap
    size_t bitmap_len = (b->size_words + 63) / 64;
    if (word_index >= bitmap_len) return false;

    return (b->bits[word_index] & (1ULL << bit_index)) != 0;
}

/*
 * bitmap_in_range - Check if ptr falls within the bitmap's address range
 *
 * This is NOT a "visited" check. It is a membership predicate for:
 * - "does this pointer live inside the source region's allocation domain?"
 *
 * CTRR rule:
 * - Only pointers into the closing src_region require transmigration.
 * - Pointers outside src_region should be left unchanged (identity preserved).
 */
static inline bool bitmap_in_range(RegionBitmap* b, void* ptr) {
    if (!b || !ptr) return false;
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < b->start) return false;
    size_t word_offset = (addr - b->start) / sizeof(uintptr_t);
    return word_offset < b->size_words;
}

static inline void bitmap_set(RegionBitmap* b, void* ptr) {
    if (!b || !ptr) return;
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < b->start) return;

    size_t word_offset = (addr - b->start) / sizeof(uintptr_t);
    if (word_offset >= b->size_words) return;

    size_t bit_index = word_offset % 64;
    size_t word_index = word_offset / 64;

    // Safety check: ensure we don't access beyond bitmap
    size_t bitmap_len = (b->size_words + 63) / 64;
    if (word_index >= bitmap_len) return;

    b->bits[word_index] |= (1ULL << bit_index);
}

/*
 * fwd_index - Compute forwarding-table index for a pointer in the bitmap domain.
 *
 * Callers should already have applied external pointer filtering:
 * - `bitmap_in_range(b, ptr)` must be true before this is meaningful.
 */
static inline bool fwd_index(RegionBitmap* b, void* ptr, size_t* out_index) {
    if (!b || !ptr || !out_index) return false;
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < b->start) return false;
    size_t off = (addr - b->start) / sizeof(uintptr_t);
    if (off >= b->size_words) return false;
    *out_index = off;
    return true;
}

/*
 * fwd_get - Look up a remapped pointer in the forwarding table.
 *
 * @param forward: Forwarding table (or NULL)
 * @param bitmap: Region bitmap for address domain
 * @param old_ptr: Original pointer to look up
 * @return: New pointer, or NULL if not found
 *
 * If forward is NULL, returns NULL (caller should fall back to hash map).
 */
static inline void* fwd_get(void** forward, RegionBitmap* bitmap, void* old_ptr) {
    if (!forward || !bitmap || !old_ptr) return NULL;

    size_t idx;
    if (!fwd_index(bitmap, old_ptr, &idx)) return NULL;

    return forward[idx];
}

/*
 * fwd_put - Store a remapped pointer in the forwarding table.
 *
 * @param forward: Forwarding table (or NULL)
 * @param bitmap: Region bitmap for address domain
 * @param old_ptr: Original pointer
 * @param new_ptr: New pointer to store
 *
 * If forward is NULL, this is a no-op (caller should use hash map instead).
 */
static inline void fwd_put(void** forward, RegionBitmap* bitmap, void* old_ptr, void* new_ptr) {
    if (!forward || !bitmap || !old_ptr) return;

    size_t idx;
    if (!fwd_index(bitmap, old_ptr, &idx)) return;

    forward[idx] = new_ptr;
}

// ----------------------------------------------------------------------------
// TAG_* to TypeID Mapping (CTRR Metadata Support)
// ----------------------------------------------------------------------------

/*
 * transmigrate_tag_to_type_id - Maps runtime TAG_* enum values to TypeID enum values
 *
 * This enables metadata lookup for transmigration. Each tag maps to its
 * corresponding type_id for use with type_metadata_get().
 *
 * NOTE: Similar to tag_to_type_id in region_value.c but kept separate
 *       to avoid circular dependencies.
 *
 * TODO: Ideally, make TAG_* and TypeID enum values align so this becomes
 *       a simple cast: (TypeID)obj->tag
 */
static TypeID transmigrate_tag_to_type_id(int tag) {
    switch (tag) {
        case TAG_INT:         return TYPE_ID_INT;
        case TAG_FLOAT:       return TYPE_ID_FLOAT;
        case TAG_CHAR:        return TYPE_ID_CHAR;
        case TAG_PAIR:        return TYPE_ID_PAIR;
        case TAG_ARRAY:       return TYPE_ID_ARRAY;
        case TAG_STRING:      return TYPE_ID_STRING;
        case TAG_SYM:         return TYPE_ID_SYMBOL;
        case TAG_DICT:        return TYPE_ID_DICT;
        case TAG_CLOSURE:     return TYPE_ID_CLOSURE;
        case TAG_BOX:         return TYPE_ID_BOX;
        case TAG_CHANNEL:     return TYPE_ID_CHANNEL;
        case TAG_THREAD:      return TYPE_ID_THREAD;
        case TAG_ERROR:       return TYPE_ID_ERROR;
        case TAG_ATOM:        return TYPE_ID_ATOM;
        case TAG_TUPLE:       return TYPE_ID_TUPLE;
        case TAG_NAMED_TUPLE: return TYPE_ID_NAMED_TUPLE;
        case TAG_GENERIC:     return TYPE_ID_GENERIC;
        case TAG_KIND:        return TYPE_ID_KIND;
        case TAG_NOTHING:     return TYPE_ID_NOTHING;
        case TAG_KEYWORD:     return TYPE_ID_SYMBOL;  /* Keywords share symbol metadata */
        default:              return TYPE_ID_MAX;      /* Invalid */
    }
}

/*
 * meta_for_obj - Get TypeMetadata for an object's tag
 *
 * @param obj: The object to get metadata for
 * @param r: Region containing the type_table (can be src_region or dest_region)
 * @return: Pointer to TypeMetadata, or NULL if invalid
 */
static inline const TypeMetadata* meta_for_obj(Obj* obj, Region* r) {
    if (!obj || !r) return NULL;

    TypeID type_id = transmigrate_tag_to_type_id(obj->tag);
    if (type_id >= TYPE_ID_MAX) return NULL;

    return type_metadata_get(r, type_id);
}

// ----------------------------------------------------------------------------
// Iterative Transmigration with Bitmap Cycle Detection
// ----------------------------------------------------------------------------

typedef struct WorkItem {
    Obj** slot;         // Where to write the new pointer
    Obj* old_ptr;       // Original object to copy
    struct WorkItem* next;
} WorkItem;

/*
 * Hash-based remap table (OPTIMIZATION: O(1) lookups instead of O(n) linear scan)
 *
 * This replaces the linear PtrMapEntry[] array to eliminate the O(n²) cliff
 * for large graphs with many shared substructures.
 *
 * The bitmap remains for fast O(1) visited checking, while the hash map provides
 * O(1) expected-time lookup for the actual remapped pointer.
 *
 * PHASE 32.5: Robin-hood hashing uses "distance to initial bucket" (DIB)
 * to reduce probe length variance. When inserting, if the incoming entry has
 * traveled farther than the existing entry, we swap them (robin-hood policy).
 */
#if REMAP_USE_LINEAR_PROBING
typedef struct {
    void* key_old;      /* old pointer (non-NULL) */
    void* val_new;      /* new pointer */
} RemapSlot;
#else
/* Robin-hood hashing: track distance to initial bucket */
typedef struct {
    void* key_old;      /* old pointer (non-NULL) */
    void* val_new;      /* new pointer */
    uint16_t dib;       /* distance to initial bucket (for robin-hood) */
} RemapSlot;
#endif

typedef struct {
    RemapSlot* slots;   /* Hash table slots */
    size_t capacity;    /* Power-of-two for fast masking */
    size_t count;       /* Number of active slots */
} RemapMap;

/* ============================================================================
 * PHASE 33.4: Batch Allocator for Linked List Transmigration
 * ============================================================================
 * Reduces per-node allocation overhead for pairs during transmigration.
 *
 * PairBatchAllocator is defined in transmigrate.h for public access.
 */
#define PAIR_BATCH_SIZE 64  /* Allocate pairs in batches of 64 */

/* Initialize a new batch allocator */
static inline void pair_batch_init(PairBatchAllocator* batch, Arena* arena) {
    (void)arena;
    batch->capacity = PAIR_BATCH_SIZE;
    /* Mark as empty; actual allocation happens lazily from the destination region.
     *
     * IMPORTANT (CTRR soundness):
     * The batch buffer MUST be allocated in the destination region, not in a
     * temporary arena that is freed at the end of transmigrate().
     */
    batch->used = PAIR_BATCH_SIZE; /* force allocate on first request */
    batch->batch = NULL;
}

/* PHASE 33.4: Public batch allocation function (called from region_metadata.c) */
Obj* pair_batch_alloc(PairBatchAllocator* batch, Region* dest) {
    if (!batch || !dest) return NULL;

    /* Allocate a new batch if empty or exhausted */
    if (!batch->batch || batch->used >= batch->capacity) {
        batch->capacity = PAIR_BATCH_SIZE;
        batch->used = 0;

        /* CTRR SOUNDNESS:
         * Allocate the batch in the DESTINATION REGION so returned pairs survive
         * beyond arena_free(tmp_arena) at the end of transmigrate(). */
        batch->batch = (Obj*)region_alloc(dest, sizeof(Obj) * PAIR_BATCH_SIZE);

        /* If the batch allocation fails, fall back to a single Obj allocation.
         * This keeps semantics correct even under memory pressure. */
        if (!batch->batch) {
            return (Obj*)region_alloc(dest, sizeof(Obj));
        }
    }

    /* Carve out a slot from the batch */
    return &batch->batch[batch->used++];
}

typedef struct {
    WorkItem** worklist;
    Arena* tmp_arena;
    RemapMap* remap;    /* Hash table for old_ptr -> new_ptr mappings */
    RegionBitmap* bitmap; /* Fast cycle detection */
    void** forward;     /* Phase 35 (P0): optional dense forwarding table */
    Region* dest;
    PairBatchAllocator pair_batch;  /* Batch allocator for pairs (Phase 33.4) */
} TraceCtx;

/*
 * ptr_hash - C99-friendly pointer hash mixer
 *
 * Uses only uintptr_t operations (no non-portable intrinsics).
 * Good enough for open-addressing remap tables (not cryptographic).
 *
 * NOTE: We assume keys are aligned pointers, so low bits are often 0.
 * Shifting helps spread entropy into lower bits used by &mask.
 */
static inline size_t ptr_hash(void* p) {
    uintptr_t x = (uintptr_t)p;
    /*
     * C99-friendly pointer hash mixer:
     * - Uses only uintptr_t ops (no non-portable intrinsics)
     * - Avoids "fancy" 64-bit-only constants in case we build 32-bit
     * - Good enough for open-addressing remap tables (not cryptographic)
     *
     * NOTE: We assume keys are aligned pointers, so low bits are often 0.
     * Shifting helps spread entropy into lower bits used by &mask.
     */
    x ^= x >> 16;
    x ^= x >> 8;
    x *= (uintptr_t)0x9e3779b1u; /* Knuth multiplicative mix (fits 32-bit) */
    x ^= x >> 16;
    return (size_t)x;
}

/*
 * remap_grow - Grow the hash table when load factor exceeds threshold
 *
 * PHASE 32.5: For robin-hood hashing, DIB is recomputed during rehashing.
 *
 * @param m: Hash map to grow
 * @param a: Arena for allocating new slot array
 */
static void remap_grow(RemapMap* m, Arena* a) {
    /* PHASE 33.5: Track table grows */
    REMAP_STATS_INC(num_grows);

    size_t old_capacity = m->capacity;
    RemapSlot* old_slots = m->slots;

    /* Double the capacity */
    size_t new_capacity = old_capacity * 2;
    if (new_capacity == 0) new_capacity = 1024; /* Initial capacity */

    RemapSlot* new_slots = arena_alloc(a, new_capacity * sizeof(RemapSlot));
    memset(new_slots, 0, new_capacity * sizeof(RemapSlot));

    /* Rehash all entries */
    size_t new_mask = new_capacity - 1;
    for (size_t i = 0; i < old_capacity; i++) {
        if (old_slots[i].key_old != NULL) {
#if REMAP_USE_LINEAR_PROBING
            /* Linear probing: simple rehash */
            size_t h = ptr_hash(old_slots[i].key_old) & new_mask;
            while (new_slots[h].key_old != NULL) {
                h = (h + 1) & new_mask;
            }
            new_slots[h] = old_slots[i];
#else
            /* Robin-hood probing: recompute DIB during rehash */
            RemapSlot incoming = old_slots[i];
            incoming.dib = 0;  /* Reset DIB for rehashing */
            size_t h = ptr_hash(incoming.key_old) & new_mask;

            while (1) {
                RemapSlot* slot = &new_slots[h];

                if (slot->key_old == NULL) {
                    *slot = incoming;
                    break;
                }

                /* Robin-hood swap */
                if (incoming.dib > slot->dib) {
                    RemapSlot temp = *slot;
                    *slot = incoming;
                    incoming = temp;
                    incoming.dib++;
                } else {
                    incoming.dib++;
                }

                h = (h + 1) & new_mask;
            }
#endif
        }
    }

    m->slots = new_slots;
    m->capacity = new_capacity;
}

/*
 * remap_get - Look up a remapped pointer by old_ptr
 *
 * @param m: Hash map
 * @param old_ptr: Original pointer to look up
 * @return: New pointer, or NULL if not found
 */
static inline void* remap_get(RemapMap* m, void* old_ptr) {
    if (!m || !old_ptr) return NULL;

    /* PHASE 33.5: Track get calls and probe count */
    REMAP_STATS_INC(total_get_calls);
#ifdef OMNI_TRANSMIGRATE_STATS
    size_t probe_count = 0;
#endif

    size_t mask = m->capacity - 1;
    size_t i = ptr_hash(old_ptr) & mask;

    while (m->slots[i].key_old != NULL) {
#ifdef OMNI_TRANSMIGRATE_STATS
        probe_count++;
#endif
        if (m->slots[i].key_old == old_ptr) {
            /* PHASE 33.5: Record probe statistics */
#ifdef OMNI_TRANSMIGRATE_STATS
            REMAP_STATS_ADD(total_probes, probe_count);
            REMAP_STATS_MAX(max_probes, probe_count);
#endif
            return m->slots[i].val_new;
        }
        i = (i + 1) & mask;
    }

    /* Also count the final probe that found NULL */
#ifdef OMNI_TRANSMIGRATE_STATS
    probe_count++;
    REMAP_STATS_ADD(total_probes, probe_count);
    REMAP_STATS_MAX(max_probes, probe_count);
#endif

    return NULL;
}

/*
 * remap_put - Insert or update a remapping
 *
 * PHASE 32.5: Implements robin-hood hashing for reduced probe length variance.
 * When inserting, if the existing entry has a lower DIB than the incoming entry,
 * we swap them (the "rich" entry helps the "poor" entry).
 *
 * @param m: Hash map
 * @param a: Arena for allocation (used only when growing)
 * @param old_ptr: Original pointer
 * @param new_ptr: New pointer
 */
static inline void remap_put(RemapMap* m, Arena* a, void* old_ptr, void* new_ptr) {
    if (!m || !old_ptr) return;

    /* PHASE 33.5: Track put calls */
    REMAP_STATS_INC(total_put_calls);

    /* Grow if load factor exceeds 70% */
    if (m->count >= m->capacity * 7 / 10) {
        remap_grow(m, a);
    }

    size_t mask = m->capacity - 1;
    size_t i = ptr_hash(old_ptr) & mask;

#if REMAP_USE_LINEAR_PROBING
    /* PHASE 33.5: Track probes for linear probing */
#ifdef OMNI_TRANSMIGRATE_STATS
    size_t probe_count = 0;
#endif

    /* Linear probing: find empty slot or existing entry */
    while (m->slots[i].key_old != NULL && m->slots[i].key_old != old_ptr) {
#ifdef OMNI_TRANSMIGRATE_STATS
        probe_count++;
#endif
        i = (i + 1) & mask;
    }

    /* Insert or update */
    if (m->slots[i].key_old == NULL) {
        m->count++;
        m->slots[i].key_old = old_ptr;
    }
    m->slots[i].val_new = new_ptr;

    /* PHASE 33.5: Record probe statistics */
#ifdef OMNI_TRANSMIGRATE_STATS
    probe_count++;
    REMAP_STATS_ADD(total_probes, probe_count);
    REMAP_STATS_MAX(max_probes, probe_count);
#endif
#else
    /* PHASE 33.5: Track probes for robin-hood hashing */
#ifdef OMNI_TRANSMIGRATE_STATS
    size_t probe_count = 0;
#endif

    /* Robin-hood hashing: track DIB and swap when we find a "richer" entry */
    RemapSlot incoming;
    incoming.key_old = old_ptr;
    incoming.val_new = new_ptr;
    incoming.dib = 0;

    while (1) {
#ifdef OMNI_TRANSMIGRATE_STATS
        probe_count++;
#endif
        RemapSlot* slot = &m->slots[i];

        /* Empty slot found - place incoming entry here */
        if (slot->key_old == NULL) {
            *slot = incoming;
            m->count++;
            /* PHASE 33.5: Record probe statistics */
#ifdef OMNI_TRANSMIGRATE_STATS
            REMAP_STATS_ADD(total_probes, probe_count);
            REMAP_STATS_MAX(max_probes, probe_count);
#endif
            return;
        }

        /* Key already exists - update the value */
        if (slot->key_old == old_ptr) {
            slot->val_new = new_ptr;
            /* PHASE 33.5: Record probe statistics */
#ifdef OMNI_TRANSMIGRATE_STATS
            REMAP_STATS_ADD(total_probes, probe_count);
            REMAP_STATS_MAX(max_probes, probe_count);
#endif
            return;
        }

        /* Robin-hood swap: if incoming has traveled farther (higher DIB),
         * swap with the existing entry and continue searching */
        if (incoming.dib > slot->dib) {
            /* Swap incoming with slot */
            RemapSlot temp = *slot;
            *slot = incoming;
            incoming = temp;
            incoming.dib++;
        } else {
            /* Just continue probing */
            incoming.dib++;
        }

        i = (i + 1) & mask;
    }
#endif
}

// Visitor for TraceFn
static void transmigrate_visitor(Obj** slot, void* context) {
    TraceCtx* ctx = (TraceCtx*)context;
    Obj* old_child = *slot;
    if (!old_child) return;

    // Skip immediate values (integers, etc. masked in pointer)
    if (IS_IMMEDIATE(old_child)) return;

    /* Phase 34.1: External pointer filtering
     * If old_child is not within src_region's allocation domain, it is already
     * safe and must be preserved by identity (do not clone). */
    if (!bitmap_in_range(ctx->bitmap, old_child)) {
        return;
    }

    // Check visited using bitmap (FAST: O(1) bitmap test)
    if (bitmap_test(ctx->bitmap, old_child)) {
        // Phase 35 P0: Try forwarding table first (O(1)), fall back to hash map
        void* new_ptr = fwd_get(ctx->forward, ctx->bitmap, old_child);
        if (!new_ptr) {
            if (ctx->remap) {
                new_ptr = remap_get(ctx->remap, old_child);
            }
        }
        if (new_ptr) {
            *slot = (Obj*)new_ptr;
        }
        return;
    }

    // Push to worklist
    WorkItem* item = arena_alloc(ctx->tmp_arena, sizeof(WorkItem));
    item->old_ptr = old_child;
    item->slot = slot;
    item->next = *ctx->worklist;
    *ctx->worklist = item;
}

void* transmigrate(void* root, Region* src_region, Region* dest_region) {
    if (!root || !dest_region) return root;

    /* PHASE 33.5: Initialize statistics at start of transmigrate */
    REMAP_STATS_INIT();

    // CTRR IMMEDIATE FAST-PATH: Handle immediate values without any allocation
    // Immediate values (tagged integers, chars, etc.) don't need bitmap or metadata
    // This ensures "everything can escape" even when src_region is empty
    if (IS_IMMEDIATE((Obj*)root)) {
        /* PHASE 33.5: Print statistics (immediate fast-path) */
        REMAP_STATS_PRINT();
        return root;  /* Immediate values are self-contained and can escape trivially */
    }

    /* Phase 34.1: If the source region is empty (or unspecified), there is nothing
     * to repair. This must be a no-op rather than an abort. */
    if (!src_region || (!src_region->arena.begin && src_region->inline_buf.offset == 0)) {
        REMAP_STATS_PRINT();
        return root;
    }

    // FAST PATH: Region Splicing (O(1) transfer for result-only regions)
    // If source region has no external refs and is closing, we can splice the entire arena
    if (src_region && src_region->external_rc == 0 && !src_region->scope_alive) {
        // Check if source has only one chunk (simple case)
        ArenaChunk* src_begin = src_region->arena.begin;
        // CTRR SOUNDNESS: Ensure no inline buffer allocations before splicing
        // Inline buffer allocations would NOT be transferred by arena splice
        if (src_begin && src_begin->next == NULL && region_can_splice_arena_only(src_region)) {
            // PERF: O(1) splice - just move the arena chunk!
            // Transfer the chunk from source to destination region
            ArenaChunk* chunk = src_begin;

            // Remove from source
            src_region->arena.begin = NULL;
            src_region->arena.end = NULL;

            // Add to destination (prepend)
            chunk->next = dest_region->arena.begin;
            dest_region->arena.begin = chunk;
            if (!dest_region->arena.end) {
                dest_region->arena.end = chunk;
            }

            /* PHASE 33.5: Print statistics (splice fast-path) */
            REMAP_STATS_PRINT();

            return root;  // Same pointer, now in destination region
        }
    }

    // NORMAL PATH: Full transmigration with cycle detection
    Arena tmp_arena = {0};

    // ENABLED: Bitmap-based cycle detection (10-100x faster than uthash)
    RegionBitmap* bitmap = bitmap_create(src_region, &tmp_arena);
    if (!bitmap) {
        // CTRR REQUIREMENT: Fail loudly for missing metadata or allocation failure
        // FORBIDDEN: Returning root creates silent shallow copy violation
        fprintf(stderr, "[FATAL] bitmap_create failed - cannot proceed safely\n");
        abort();
    }

    /* PHASE 35 P0: Allocate dense forwarding table when address domain is small enough.
     *
     * The forwarding table replaces the hash map for O(1) old_ptr -> new_ptr lookups.
     * It is indexed by the bitmap word offset (same domain as the cycle detection bitmap).
     */
    void** forward = NULL;
#ifdef OMNI_TRANSMIGRATE_DEBUG_REMAP_MODE
    g_last_used_forwarding_table = 0;
#endif
    /* Guard against overflow in size calculation. */
    size_t forward_bytes = SIZE_MAX;
    bool forward_overflow = (bitmap->size_words > (SIZE_MAX / sizeof(void*)));
    if (!forward_overflow) {
        forward_bytes = bitmap->size_words * sizeof(void*);
    }

    /* Eligibility means:
     * - not overflow
     * - forwarding not disabled
     * - size bounded (unless forced by build flags)
     *
     * NOTE: We intentionally do NOT allocate the table immediately in the
     * default (non-forced) case. We allocate lazily once the graph size is
     * large enough to amortize the memset cost.
     */
    bool forward_eligible = (!forward_overflow) && OMNI_REMAP_SHOULD_USE_FORWARDING(forward_bytes);

#ifdef OMNI_REMAP_FORCE_FORWARDING
    if (!forward_eligible) {
        fprintf(stderr, "[FATAL] OMNI_REMAP_FORCE_FORWARDING set but forwarding is not eligible\n");
        abort();
    }
    forward = (void**)arena_alloc(&tmp_arena, forward_bytes);
    if (!forward) {
        fprintf(stderr, "[FATAL] OMNI_REMAP_FORCE_FORWARDING set but forwarding allocation failed\n");
        abort();
    }
    memset(forward, 0, forward_bytes);
#ifdef OMNI_TRANSMIGRATE_DEBUG_REMAP_MODE
    g_last_used_forwarding_table = 1;
#endif
#endif

    // Initialize hash map for remapping (O(1) expected lookups).
    // Even when forwarding is enabled, we keep the hash map for early objects
    // cloned before the forwarding table is allocated lazily.
    RemapMap remap_struct = {0};
    remap_struct.capacity = 1024;  /* Initial capacity */
    remap_struct.slots = arena_alloc(&tmp_arena, remap_struct.capacity * sizeof(RemapSlot));
    memset(remap_struct.slots, 0, remap_struct.capacity * sizeof(RemapSlot));

    WorkItem* worklist = NULL;
    Obj* result = NULL;

    // Initial root
    WorkItem* item = arena_alloc(&tmp_arena, sizeof(WorkItem));
    item->old_ptr = (Obj*)root;
    item->slot = &result;
    item->next = worklist;
    worklist = item;

    TraceCtx trace_ctx = {
        .worklist = &worklist,
        .tmp_arena = &tmp_arena,
        .remap = &remap_struct,
        .bitmap = bitmap,
        .forward = forward,  /* Phase 35 P0: forwarding table (or NULL) */
        .dest = dest_region,
        .pair_batch = {0}  /* Initialize batch allocator (Phase 33.4) */
    };

    /* Initialize the pair batch allocator */
    pair_batch_init(&trace_ctx.pair_batch, &tmp_arena);

    /* Clone context passed to metadata clone functions (stable ABI) */
    TransmigrateCloneCtx clone_ctx = {
        .pair_batch = &trace_ctx.pair_batch
    };

    /* Number of distinct objects cloned so far (not counting revisits).
     * Used to decide when to pay the one-time forwarding-table memset cost. */
    size_t clones_created = 0;

    while (worklist) {
        WorkItem* current = worklist;
        worklist = worklist->next;

        Obj* old_obj = current->old_ptr;

        // Handle immediate values (integers, etc. masked in pointer)
        if (IS_IMMEDIATE(old_obj)) {
            *current->slot = old_obj;
            continue;
        }

        if (!old_obj) {
            *current->slot = NULL;
            continue;
        }

        /* Phase 34.1: If the object is outside the source region allocation domain,
         * leave it untouched. This preserves identity for external objects. */
        if (!bitmap_in_range(bitmap, old_obj)) {
            *current->slot = old_obj;
            continue;
        }

        // Cycle detection using bitmap (FAST: O(1))
        if (bitmap_test(bitmap, old_obj)) {
            // Phase 35 P0: Try forwarding table first (O(1)), fall back to hash map
            void* new_ptr = fwd_get(forward, bitmap, old_obj);
            if (!new_ptr) {
                new_ptr = remap_get(&remap_struct, old_obj);
            }
            if (new_ptr) {
                *current->slot = (Obj*)new_ptr;
            }
            continue;
        }

        /* ========================================================================
         * METADATA-DRIVEN TRANSMIGRATION (CTRR compliant)
         * ======================================================================== */

        /* Look up metadata for this object's type */
        const TypeMetadata* meta = meta_for_obj(old_obj, src_region);

        /* CTRR REQUIREMENT: Fail loudly for missing metadata */
        if (!meta || !meta->clone || !meta->trace) {
            fprintf(stderr,
                    "[FATAL] transmigrate: missing metadata for tag %d (type_id %d)\n",
                    old_obj->tag, transmigrate_tag_to_type_id(old_obj->tag));
            fprintf(stderr, "  meta=%p, clone=%p, trace=%p\n",
                    (void*)meta, meta ? (void*)meta->clone : NULL,
                    meta ? (void*)meta->trace : NULL);
            abort();  /* In debug builds: assert(false && "Missing metadata"); */
        }

        /* Clone the object using metadata callback */
        Obj* new_obj = meta->clone(old_obj, dest_region, &clone_ctx);

        if (!new_obj) {
            fprintf(stderr, "[FATAL] transmigrate: clone failed for tag %d\n", old_obj->tag);
            abort();
        }

        /* Register in visited and update slot */
        *current->slot = new_obj;
        bitmap_set(bitmap, old_obj);

        /* Phase 35 P0: Lazily allocate forwarding table only after we have
         * cloned enough objects to amortize the zeroing cost. */
        clones_created++;

        if (!forward && forward_eligible && clones_created >= OMNI_REMAP_FORWARDING_MIN_CLONES) {
            forward = (void**)arena_alloc(&tmp_arena, forward_bytes);
            if (forward) {
                memset(forward, 0, forward_bytes);
                trace_ctx.forward = forward; /* Make visitor see forwarding immediately. */
#ifdef OMNI_TRANSMIGRATE_DEBUG_REMAP_MODE
                g_last_used_forwarding_table = 1;
#endif
            }
            /* If allocation fails, we continue using the hash map. */
        }

        /* Store remap mapping:
         * - If forwarding is active, prefer it (O(1) indexing).
         * - Otherwise, use the robin-hood hash map. */
        if (forward) {
            fwd_put(forward, bitmap, old_obj, new_obj);
        } else {
            remap_put(&remap_struct, &tmp_arena, old_obj, new_obj);
        }

        /* Trace children - this schedules them for rewriting */
        meta->trace(new_obj, transmigrate_visitor, &trace_ctx);
    }

    // Cleanup (OPTIMIZED: Single arena_free instead of iterating hash table)
    arena_free(&tmp_arena);

    /* PHASE 33.5: Print statistics at end of transmigrate */
    REMAP_STATS_PRINT();

    return result;
}

// ----------------------------------------------------------------------------
// Batched/Incremental Transmigration (OPTIMIZATION: T-opt-transmigrate-batch)
// ----------------------------------------------------------------------------

/*
 * transmigrate_incremental - Batched transmigration for large graphs
 *
 * Processes the object graph in configurable chunks to:
 * - Reduce peak memory usage during transmigration
 * - Allow early termination for sparse access patterns
 * - Improve cache locality through batched processing
 *
 * Implementation: Same algorithm as transmigrate(), but processes worklist
 * in chunks instead of all at once.
 */
void* transmigrate_incremental(void* root, Region* src_region, Region* dest_region,
                                size_t chunk_size, float* progress_out) {
    if (!root || !dest_region) return root;

    /* PHASE 33.5: Initialize statistics at start of transmigrate */
    REMAP_STATS_INIT();

    // CTRR IMMEDIATE FAST-PATH: Handle immediate values without any allocation
    // Immediate values (tagged integers, chars, etc.) don't need bitmap or metadata
    // This ensures "everything can escape" even when src_region is empty
    if (IS_IMMEDIATE((Obj*)root)) {
        if (progress_out) *progress_out = 1.0f;
        /* PHASE 33.5: Print statistics (immediate fast-path) */
        REMAP_STATS_PRINT();
        return root;  /* Immediate values are self-contained and can escape trivially */
    }

    /* Phase 34.1: Empty source region => no-op (external boxed roots must not abort). */
    if (!src_region || (!src_region->arena.begin && src_region->inline_buf.offset == 0)) {
        if (progress_out) *progress_out = 1.0f;
        REMAP_STATS_PRINT();
        return root;
    }

    // If chunk_size is 0 or very small, use standard transmigrate (no chunking)
    if (chunk_size == 0 || chunk_size > 10000) {
        if (progress_out) *progress_out = 1.0f;
        return transmigrate(root, src_region, dest_region);  /* Stats printed by transmigrate() */
    }

    // Check for O(1) splice fast path (same as standard transmigrate)
    if (src_region && src_region->external_rc == 0 && !src_region->scope_alive) {
        ArenaChunk* src_begin = src_region->arena.begin;
        // CTRR SOUNDNESS: Ensure no inline buffer allocations before splicing
        if (src_begin && src_begin->next == NULL && region_can_splice_arena_only(src_region)) {
            // O(1) splice - just move the arena chunk
            ArenaChunk* chunk = src_begin;
            src_region->arena.begin = NULL;
            src_region->arena.end = NULL;
            chunk->next = dest_region->arena.begin;
            dest_region->arena.begin = chunk;
            if (!dest_region->arena.end) {
                dest_region->arena.end = chunk;
            }
            if (progress_out) *progress_out = 1.0f;
            /* PHASE 33.5: Print statistics (splice fast-path) */
            REMAP_STATS_PRINT();
            return root;
        }
    }

    // Batched transmigration with chunking
    Arena tmp_arena = {0};

    // Create bitmap for cycle detection
    RegionBitmap* bitmap = bitmap_create(src_region, &tmp_arena);
    if (!bitmap) {
        if (progress_out) *progress_out = 0.0f;
        // CTRR REQUIREMENT: Fail loudly for missing metadata or allocation failure
        fprintf(stderr, "[FATAL] bitmap_create failed in transmigrate_incremental\n");
        abort();
    }

    /* PHASE 35 P0: Allocate dense forwarding table when address domain is small enough. */
    void** forward = NULL;
#ifdef OMNI_TRANSMIGRATE_DEBUG_REMAP_MODE
    g_last_used_forwarding_table = 0;
#endif
    size_t forward_bytes = SIZE_MAX;
    bool forward_overflow = (bitmap->size_words > (SIZE_MAX / sizeof(void*)));
    if (!forward_overflow) {
        forward_bytes = bitmap->size_words * sizeof(void*);
    }
    bool forward_eligible = (!forward_overflow) && OMNI_REMAP_SHOULD_USE_FORWARDING(forward_bytes);

#ifdef OMNI_REMAP_FORCE_FORWARDING
    if (!forward_eligible) {
        fprintf(stderr, "[FATAL] OMNI_REMAP_FORCE_FORWARDING set but forwarding is not eligible\n");
        abort();
    }
    forward = (void**)arena_alloc(&tmp_arena, forward_bytes);
    if (!forward) {
        fprintf(stderr, "[FATAL] OMNI_REMAP_FORCE_FORWARDING set but forwarding allocation failed\n");
        abort();
    }
    memset(forward, 0, forward_bytes);
#ifdef OMNI_TRANSMIGRATE_DEBUG_REMAP_MODE
    g_last_used_forwarding_table = 1;
#endif
#endif

    // Initialize hash map for remapping only if forwarding not used.
    RemapMap remap_struct = {0};
    remap_struct.capacity = 1024;  /* Initial capacity */
    remap_struct.slots = arena_alloc(&tmp_arena, remap_struct.capacity * sizeof(RemapSlot));
    memset(remap_struct.slots, 0, remap_struct.capacity * sizeof(RemapSlot));

    WorkItem* worklist = NULL;
    Obj* result = NULL;

    // Initial root
    WorkItem* item = arena_alloc(&tmp_arena, sizeof(WorkItem));
    item->old_ptr = (Obj*)root;
    item->slot = &result;
    item->next = worklist;
    worklist = item;

    TraceCtx trace_ctx = {
        .worklist = &worklist,
        .tmp_arena = &tmp_arena,
        .remap = &remap_struct,
        .bitmap = bitmap,
        .forward = forward,  /* Phase 35 P0: forwarding table (or NULL) */
        .dest = dest_region,
        .pair_batch = {0}  /* Initialize batch allocator (Phase 33.4) */
    };

    /* Initialize the pair batch allocator */
    pair_batch_init(&trace_ctx.pair_batch, &tmp_arena);

    /* Clone context passed to metadata clone functions (stable ABI) */
    TransmigrateCloneCtx clone_ctx = {
        .pair_batch = &trace_ctx.pair_batch
    };

    // First pass: Count total objects to track progress
    // (Optional: adds overhead, so we estimate based on chunk size)
    size_t objects_processed = 0;
    size_t estimated_total = chunk_size * 2;  // Rough estimate
    size_t clones_created = 0;  /* Phase 35 P0: forwarding allocation threshold */

    // Process worklist in chunks
    while (worklist) {
        // Process up to chunk_size objects
        size_t chunk_count = 0;
        while (worklist && chunk_count < chunk_size) {
            WorkItem* current = worklist;
            worklist = worklist->next;

            Obj* old_obj = current->old_ptr;

            // Handle immediate values
            if (IS_IMMEDIATE(old_obj)) {
                *current->slot = old_obj;
                chunk_count++;
                continue;
            }

            if (!old_obj) {
                *current->slot = NULL;
                chunk_count++;
                continue;
            }

            /* Phase 34.1: External pointer filtering
             * If old_obj is outside the source region allocation domain, do not clone. */
            if (!bitmap_in_range(bitmap, old_obj)) {
                *current->slot = old_obj;
                chunk_count++;
                continue;
            }

            // Cycle detection
            if (bitmap_test(bitmap, old_obj)) {
                // Phase 35 P0: Try forwarding table first (O(1)), fall back to hash map
                void* new_ptr = fwd_get(forward, bitmap, old_obj);
                if (!new_ptr) {
                    if (!forward) {
                        new_ptr = remap_get(&remap_struct, old_obj);
                    }
                }
                if (new_ptr) {
                    *current->slot = (Obj*)new_ptr;
                }
                chunk_count++;
                continue;
            }

            /* ========================================================================
             * METADATA-DRIVEN TRANSMIGRATION (CTRR compliant)
             * ======================================================================== */

            /* Look up metadata for this object's type */
            const TypeMetadata* meta = meta_for_obj(old_obj, src_region);

            /* CTRR REQUIREMENT: Fail loudly for missing metadata */
            if (!meta || !meta->clone || !meta->trace) {
                fprintf(stderr,
                        "[FATAL] transmigrate_incremental: missing metadata for tag %d\n",
                        old_obj->tag);
                abort();
            }

            /* Clone the object using metadata callback */
            Obj* new_obj = meta->clone(old_obj, dest_region, &clone_ctx);

            if (!new_obj) {
                fprintf(stderr, "[FATAL] transmigrate_incremental: clone failed for tag %d\n", old_obj->tag);
                abort();
            }

            /* Register in visited and update slot */
            *current->slot = new_obj;
            bitmap_set(bitmap, old_obj);

            clones_created++;

            /* Phase 35 P0: Lazily allocate forwarding table after enough clones. */
            if (!forward && forward_eligible && clones_created >= OMNI_REMAP_FORWARDING_MIN_CLONES) {
                forward = (void**)arena_alloc(&tmp_arena, forward_bytes);
                if (forward) {
                    memset(forward, 0, forward_bytes);
                    trace_ctx.forward = forward;
#ifdef OMNI_TRANSMIGRATE_DEBUG_REMAP_MODE
                    g_last_used_forwarding_table = 1;
#endif
                }
            }

            /* Store mapping: prefer forwarding when active, fall back to hash. */
            if (forward) {
                fwd_put(forward, bitmap, old_obj, new_obj);
            } else {
                remap_put(&remap_struct, &tmp_arena, old_obj, new_obj);
            }

            /* Trace children - this schedules them for rewriting */
            meta->trace(new_obj, transmigrate_visitor, &trace_ctx);

            objects_processed++;
            chunk_count++;
        }

        // Update progress estimate
        if (progress_out) {
            // Estimate progress based on processed objects vs estimated total
            // This is a rough estimate since we don't know the total upfront
            if (objects_processed >= estimated_total) {
                estimated_total = objects_processed * 2;  // Adjust estimate
            }
            *progress_out = (float)objects_processed / (float)estimated_total;
            // Clamp to 0.99 since we're not done yet
            if (*progress_out > 0.99f) *progress_out = 0.99f;
        }

        // Check if we should continue (worklist not empty)
        // For true incremental behavior, caller can check progress and decide
        // whether to continue or stop early
    }

    // Final progress update
    if (progress_out) *progress_out = 1.0f;

    // Cleanup
    arena_free(&tmp_arena);

    /* PHASE 33.5: Print statistics at end of transmigrate */
    REMAP_STATS_PRINT();

    return result;
}
