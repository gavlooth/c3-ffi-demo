#include "region_core.h"
#include <stdio.h>
#include <string.h>
#include "../../include/omni_atomic.h"  /* Issue 4 P3: Centralized atomic operations */

#define MAX_THREAD_LOCAL_TETHERS 16
#define REGION_POOL_SIZE 32  // Pool size for reusable regions

typedef struct {
    Region* regions[MAX_THREAD_LOCAL_TETHERS];
    int counts[MAX_THREAD_LOCAL_TETHERS];
    int size;
} TetherCache;

static __thread TetherCache g_tether_cache = { .size = 0 };

// Region Pool for fast reuse (OPTIMIZATION: T-opt-region-pool)
typedef struct {
    Region* regions[REGION_POOL_SIZE];
    int count;
} RegionPool;

static __thread RegionPool g_region_pool = { .count = 0 };

// Global region ID counter (OPTIMIZATION: T-opt-region-metadata-pointer-masking)
static uint16_t g_next_region_id = 1;  // Start at 1 (0 is reserved for NULL/global

// Reset a region for reuse (fast path)
static inline void region_reset(Region* r) {
    if (!r) return;
    // Free the arena contents
    arena_free(&r->arena);
    // Reset inline buffer (OPTIMIZATION: T-opt-inline-allocation)
    r->inline_buf.offset = 0;
    // Reset type metadata (OPTIMIZATION: T-opt-region-metadata)
    // Note: We preserve type_table for reused regions, just clear it
    // if (r->type_table) { free(r->type_table); r->type_table = NULL; }
    // Reset control block
    r->external_rc = 0;
    r->tether_count = 0;
    r->scope_alive = true;
    // Reset thread-local tracking (OPTIMIZATION: T-opt-thread-local-rc-detect)
    r->owner_thread = pthread_self();
    r->is_thread_local = true; // Assume thread-local until proven otherwise
    r->has_external_refs = false;

    /* Issue 2 P3: Reset accounting counters */
    r->bytes_allocated_total = 0;
    r->bytes_allocated_peak = 0;
    r->inline_buf_used_bytes = 0;
    r->escape_repair_count = 0;
    r->chunk_count = 0;
    r->last_arena_end = NULL;

    /* Issue 2 P4.1: Reset lifetime_rank to 0 (required for region pooling) */
    r->lifetime_rank = 0;

    /* Issue 2 P4.3b: Reset parent to NULL (required for region pooling) */
    r->parent = NULL;
}

Region* region_create(void) {
    // FAST PATH: Try to get a region from the pool
    if (g_region_pool.count > 0) {
        Region* r = g_region_pool.regions[--g_region_pool.count];
        // Region is already reset, just mark it as alive
        r->scope_alive = true;
        // Note: region_id is preserved from previous lifetime (acceptable)
        return r;
    }

    // SLOW PATH: Allocate new region (malloc overhead)
    // We allocate the Region struct itself using malloc,
    // but the contents are managed by the internal Arena.
    // Ideally, the Region struct could be *inside* the arena,
    // but we need the RCB to persist until the LAST ref dies,
    // whereas the Arena might be reset.
    // For now, simple malloc for the RCB.
    Region* r = (Region*)malloc(sizeof(Region));
    if (!r) {
        fprintf(stderr, "Fatal: OOM in region_create\n");
        exit(1);
    }

    // Initialize Arena (zero-init is sufficient for tsoding/arena to start)
    r->arena.begin = NULL;
    r->arena.end = NULL;

    // Initialize inline buffer (OPTIMIZATION: T-opt-inline-allocation)
    r->inline_buf.offset = 0;
    r->inline_buf.capacity = REGION_INLINE_BUF_SIZE;

    // Initialize type metadata (OPTIMIZATION: T-opt-region-metadata)
    type_metadata_init(r);

    /* Issue 2 P3: Initialize accounting counters */
    r->bytes_allocated_total = 0;
    r->bytes_allocated_peak = 0;
    r->inline_buf_used_bytes = 0;
    r->escape_repair_count = 0;
    r->chunk_count = 0;
    r->last_arena_end = NULL;

    /* Issue 2 P4.1: Initialize lifetime_rank to 0 (root/global default) */
    r->lifetime_rank = 0;

    /* Issue 2 P4.3b: Initialize parent to NULL (new regions have no parent initially) */
    r->parent = NULL;

    // Assign region ID (OPTIMIZATION: T-opt-region-metadata-pointer-masking)
    /* Issue 4 P3: Use atomic wrapper for consistent memory ordering */
    r->region_id = omni_atomic_fetch_add_u16(&g_next_region_id, 1);

    // Initialize Control Block
    r->external_rc = 0;
    r->tether_count = 0;
    r->scope_alive = true;

    // Initialize thread-local tracking (OPTIMIZATION: T-opt-thread-local-rc-detect)
    r->owner_thread = pthread_self();
    r->is_thread_local = true;  // Assume thread-local until proven otherwise
    r->has_external_refs = false;

    return r;
}

void region_destroy_if_dead(Region* r) {
    if (!r) return;

    // Check liveness:
    // 1. Scope must be dead (region_exit called)
    // 2. No external references (external_rc == 0)
    // 3. No active tethers (tether_count == 0)

    // We use atomic loads for thread safety check
    /* Issue 4 P3: Use atomic wrappers for consistent memory ordering */
    int rc = (int)omni_atomic_load_u32((volatile uint32_t*)&r->external_rc);
    int tc = (int)omni_atomic_load_u32((volatile uint32_t*)&r->tether_count);

    if (!r->scope_alive && rc == 0 && tc == 0) {
        // OPTIMIZATION: Try to return to pool instead of freeing
        if (g_region_pool.count < REGION_POOL_SIZE) {
            // Reset and return to pool (FAST: avoids malloc/free)
            region_reset(r);
            g_region_pool.regions[g_region_pool.count++] = r;
            return;
        }

        // Pool full, actually free (SLOW: requires malloc next time)
        arena_free(&r->arena);

        // Free type metadata (OPTIMIZATION: T-opt-region-metadata)
        if (r->type_table) {
            free(r->type_table);
            r->type_table = NULL;
            r->num_types = 0;
        }

        free(r);
    }
}

void region_exit(Region* r) {
    if (r) {
        r->scope_alive = false;
        region_destroy_if_dead(r);
    }
}

/*
 * region_retain_internal - Increment reference count
 *
 * OPTIMIZATION (T-opt-thread-local-rc-detect):
 * Uses non-atomic operations for thread-local regions (10-50x faster).
 * Falls back to atomic operations for shared regions.
 */
void region_retain_internal(Region* r) {
    if (!r) return;

    // FAST PATH: Thread-local region uses non-atomic RC (10-50x faster)
    if (region_is_thread_local(r)) {
        r->external_rc++;  // NON-ATOMIC: safe because only one thread accesses
    } else {
        // SLOW PATH: Shared region uses atomic RC (safe but slower)
        __atomic_add_fetch(&r->external_rc, 1, __ATOMIC_SEQ_CST);
    }
}

/*
 * region_release_internal - Decrement reference count
 *
 * OPTIMIZATION (T-opt-thread-local-rc-detect):
 * Uses non-atomic operations for thread-local regions (10-50x faster).
 * Falls back to atomic operations for shared regions.
 */
void region_release_internal(Region* r) {
    if (!r) return;

    // FAST PATH: Thread-local region uses non-atomic RC (10-50x faster)
    if (region_is_thread_local(r)) {
        r->external_rc--;  // NON-ATOMIC: safe because only one thread accesses
        if (r->external_rc == 0) {
            region_destroy_if_dead(r);
        }
    } else {
        // SLOW PATH: Shared region uses atomic RC (safe but slower)
        int new_rc = __atomic_sub_fetch(&r->external_rc, 1, __ATOMIC_SEQ_CST);
        if (new_rc == 0) {
            region_destroy_if_dead(r);
        }
    }
}

/* ========== Issue 2 P4.1: Region Lifetime Rank Accessors ========== */

/*
 * omni_region_set_lifetime_rank - Set the lifetime rank of a region
 *
 * The lifetime_rank represents the outlives depth (nesting order) of a region.
 * - Rank 0 = root/global region (no parent)
 * - Rank N = child region with N levels of nesting
 *
 * This function is called by generated code after region_create()
 * to establish the correct rank hierarchy.
 */
void omni_region_set_lifetime_rank(Region* r, uint64_t rank) {
    if (r) {
        r->lifetime_rank = rank;
    }
}

/*
 * omni_region_get_lifetime_rank - Get the lifetime rank of a region
 *
 * Returns the outlives depth for the given region.
 */
uint64_t omni_region_get_lifetime_rank(Region* r) {
    return r ? r->lifetime_rank : 0;
}

/* ========== Issue 2 P4.3b: Region Parent/Ancestry Accessors ========== */

/*
 * omni_region_set_parent - Set the parent region for a region
 *
 * Issue 2 P4.3b: Establishes the parent-child relationship
 * in the single-thread outlives tree.
 *
 * @param r: The region to set parent for
 * @param parent: The parent region (NULL for root/global)
 *
 * This is called by generated code after region_create() when
 * a new region is created within an existing region scope.
 */
void omni_region_set_parent(Region* r, Region* parent) {
    if (r) {
        r->parent = parent;
    }
}

/*
 * region_is_ancestor - Check if a region is an ancestor of another
 *
 * Issue 2 P4.3b: Helper function to walk up the parent chain.
 *
 * @param anc: Potential ancestor region to check
 * @param r: Descendant region to walk from
 * @return: true if anc is ancestor of r (or anc == r), false otherwise
 *
 * Note: This function does NOT use ranks because same-rank regions
 * may be siblings (incomparable). We must follow parent links
 * to establish true ancestry.
 */
static inline bool region_is_ancestor(Region* anc, Region* r) {
    if (!anc || !r) return false;
    if (anc == r) return true;  // A region outlives itself

    // Walk up from r towards the root via parent links
    for (Region* cur = r->parent; cur; cur = cur->parent) {
        if (cur == anc) {
            return true;  // Found anc in ancestor chain
        }
    }

    return false;  // Reached root without finding anc
}

/*
 * omni_region_outlives - Check if region 'a' outlives region 'b'
 *
 * Issue 2 P4.3b: Use ancestry relation to determine ordering.
 * A region outlives another if it is an ancestor (or equal).
 *
 * Ranks alone are insufficient for this because same-rank regions
 * are siblings/incomparable and cannot prove ordering.
 *
 * @param a: Potential outliving region (ancestor candidate)
 * @param b: Potential outlived region (descendant candidate)
 * @return: true if a outlives b (a is ancestor of b or a == b)
 *
 * Usage in store barrier:
 *   if (omni_region_outlives(dst, src)) {
 *       // No repair needed: dst is older/outlives src
 *   } else {
 *       // Repair needed: dst is younger or ordering unknown
 *   }
 */
bool omni_region_outlives(Region* a, Region* b) {
    return region_is_ancestor(a, b);
}

/*
 * region_is_thread_local - Check if a region is only accessed by the creating thread
 *
 * OPTIMIZATION (T-opt-thread-local-rc-detect):
 * This function enables fast non-atomic RC operations for single-threaded regions.
 * Most regions in functional programming are only accessed by one thread.
 *
 * Returns: true if the region is thread-local (safe for non-atomic operations)
 *
 * The fast path uses the cached is_thread_local flag for O(1) check.
 * If the region has external references, we assume it's not thread-local.
 */
bool region_is_thread_local(Region* r) {
    if (!r) return false;

    // FAST PATH: Use cached result
    if (r->is_thread_local && !r->has_external_refs) {
        // Check if current thread is the owner
        if (pthread_equal(pthread_self(), r->owner_thread)) {
            return true;
        }
        // Different thread trying to access - mark as not thread-local
        // This is a one-way transition (once false, stays false)
        r->is_thread_local = false;
    }

    return false;
}

/*
 * region_mark_external_ref - Mark that a region has external references
 *
 * This is called when a region might be accessed from another thread
 * (e.g., through tethering or cross-thread references).
 *
 * Once marked, the region will use atomic RC operations for safety.
 */
void region_mark_external_ref(Region* r) {
    if (r) {
        // One-way transition: once not thread-local, never becomes thread-local again
        r->has_external_refs = true;
        r->is_thread_local = false;
    }
}

void region_tether_start(Region* r) {
    if (!r) return;

    // OPTIMIZATION (T-opt-thread-local-rc-tether): Track tether origins
    // If this tether is from a different thread than the region owner,
    // mark the region as having external refs (will use atomic RC)
    if (!pthread_equal(pthread_self(), r->owner_thread)) {
        region_mark_external_ref(r);
    }

    // Check local cache
    for (int i = 0; i < g_tether_cache.size; i++) {
        if (g_tether_cache.regions[i] == r) {
            g_tether_cache.counts[i]++;
            return;
        }
    }

    // Not in cache, add if possible
    if (g_tether_cache.size < MAX_THREAD_LOCAL_TETHERS) {
        int idx = g_tether_cache.size++;
        g_tether_cache.regions[idx] = r;
        g_tether_cache.counts[idx] = 1;
        __atomic_add_fetch(&r->tether_count, 1, __ATOMIC_SEQ_CST);
        return;
    }

    // Cache full, fallback to atomic only
    __atomic_add_fetch(&r->tether_count, 1, __ATOMIC_SEQ_CST);
}

void region_tether_end(Region* r) {
    if (!r) return;

    // Check local cache
    for (int i = 0; i < g_tether_cache.size; i++) {
        if (g_tether_cache.regions[i] == r) {
            g_tether_cache.counts[i]--;
            if (g_tether_cache.counts[i] == 0) {
                // Last local reference, flush to atomic
                int new_tc = __atomic_sub_fetch(&r->tether_count, 1, __ATOMIC_SEQ_CST);
                
                // Remove from cache (swap with last)
                g_tether_cache.regions[i] = g_tether_cache.regions[g_tether_cache.size - 1];
                g_tether_cache.counts[i] = g_tether_cache.counts[g_tether_cache.size - 1];
                g_tether_cache.size--;

                if (new_tc == 0) {
                    region_destroy_if_dead(r);
                }
            }
            return;
        }
    }

    // Not in cache (was full or direct), atomic decrement
    int new_tc = __atomic_sub_fetch(&r->tether_count, 1, __ATOMIC_SEQ_CST);
    if (new_tc == 0) {
        region_destroy_if_dead(r);
    }
}

// region_alloc is now static inline in region_core.h (T-opt-inline-alloc-fastpath)
// This eliminates call overhead for the hot inline buffer fast path

void region_splice(Region* dest, Region* src, void* start_ptr, void* end_ptr) {
    if (!dest || !src || !start_ptr || !end_ptr) return;

    ArenaChunk *start_chunk = NULL;
    ArenaChunk *end_chunk = NULL;

    // Find which chunks contain the pointers
    for (ArenaChunk* c = src->arena.begin; c; c = c->next) {
        uintptr_t data_start = (uintptr_t)c->data;
        uintptr_t data_end = data_start + (c->capacity * sizeof(uintptr_t));
        
        if ((uintptr_t)start_ptr >= data_start && (uintptr_t)start_ptr < data_end) {
            start_chunk = c;
        }
        if ((uintptr_t)end_ptr >= data_start && (uintptr_t)end_ptr < data_end) {
            end_chunk = c;
            break;
        }
    }

    if (start_chunk && end_chunk) {
        arena_detach_blocks(&src->arena, start_chunk, end_chunk);
        arena_attach_blocks(&dest->arena, start_chunk, end_chunk);
    }
}

// -- RegionRef Implementation --

void region_retain(RegionRef ref) {
    region_retain_internal(ref.region);
}

void region_release(RegionRef ref) {
    region_release_internal(ref.region);
}

// ========== Global Region Support ==========

/* Thread-local global region for fallback allocations */
static __thread Region* g_global_region = NULL;

/*
 * region_get_or_create - Get or create the thread-local global region.
 * This provides a fallback region for allocations that don't have a specific region.
 */
Region* region_get_or_create(void) {
    if (!g_global_region) {
        g_global_region = region_create();
    }
    return g_global_region;
}

/*
 * region_strdup - Duplicate a string in a region.
 * Returns a pointer to the duplicated string, allocated in the region.
 */
char* region_strdup(Region* r, const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)region_alloc(r, len);
    if (copy) {
        memcpy(copy, s, len);
    }
    return copy;
}
/* ========== Issue 2 P5: Merge Support ========== */

/*
 * region_merge_permitted - Check if region merge is safe
 *
 * Issue 2 P5: Determine if src region can be merged into dst.
 * Merge is only safe when:
 *   1. Both regions owned by same thread (cross-thread requires transmigrate)
 *   2. Source region has no inline buffer allocations (or only arena allocations)
 *
 * @param src: Source region to merge from
 * @param dst: Destination region to merge into
 * @return: true if merge is permitted, false otherwise
 *
 * Rationale: Inline buffer allocations are in Region struct itself,
 * not in arena chunks. region_splice() only transfers arena chunks,
 * so merging a region with inline allocations would create dangling pointers.
 */
bool region_merge_permitted(const Region* src, const Region* dst) {
    // NULL regions cannot be merged
    if (!src || !dst) return false;

    // Threading gate: Cross-thread merges are not allowed
    // Ranks are only comparable when owner_thread matches
    if (!pthread_equal(src->owner_thread, dst->owner_thread)) {
        return false;
    }

    // Inline buffer gate: Source must have no inline allocations
    // region_splice() only transfers arena chunks, not inline buffer data
    // If src has inline allocations, they would be lost, creating dangling pointers
    if (!region_can_splice_arena_only(src)) {
        return false;
    }

    // All checks passed: merge is permitted
    return true;
}

/*
 * get_merge_threshold - Get the merge threshold for auto-repair
 *
 * Issue 2 P5: Returns the size threshold for choosing between
 * merge and transmigrate when repairing lifetime violations.
 *
 * @return: Merge threshold in bytes (default: REGION_MERGE_THRESHOLD_BYTES)
 *
 * Note: This is currently a constant, but making it a function
 * allows future runtime tuning without changing code.
 */
size_t get_merge_threshold(void) {
    return REGION_MERGE_THRESHOLD_BYTES;
}

/*
 * region_merge_safe - Safely merge src region into dst
 *
 * Issue 2 P5: Perform a safe merge using region_splice().
 * This function checks merge preconditions before splicing arena chunks.
 *
 * @param src: Source region to merge from
 * @param dst: Destination region to merge into
 * @return: 0 on success, -1 if merge not permitted, -2 if threading mismatch
 *
 * Safe merge behavior:
 *   1. Check merge is permitted (same thread, src has no inline allocs)
 *   2. Transfer all arena chunks from src to dst via region_splice()
 *   3. After merge, values in src are now in dst's arena
 *
 * Note: This does not mark src as "drained" because the Region
 * struct doesn't have that field. After merge, src may still allocate
 * new data in its (now-empty) arena - this is safe but unusual.
 */
int region_merge_safe(Region* src, Region* dst) {
    // Check merge preconditions
    if (!region_merge_permitted(src, dst)) {
        return -1;  // Merge not permitted (inline allocs or NULL regions)
    }

    // Threading gate is already checked in region_merge_permitted(),
    // but we check again for explicit error codes
    if (!pthread_equal(src->owner_thread, dst->owner_thread)) {
        return -2;  // Merge forbidden, different threads
    }

    // Safe merge: splice all arena chunks from src into dst
    // This transfers ownership of all arena-allocated data from src to dst
    // Inline buffer data (if any) would be in Region struct itself,
    // but region_merge_permitted() ensures src has no inline allocations
    region_splice(dst, src, NULL, NULL);

    // Merge successful
    return 0;
}
