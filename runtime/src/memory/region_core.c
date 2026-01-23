#include "region_core.h"
#include "region_pointer.h"  /* Issue 15 P3: For pointer_mask_get_region() in region_of() */
#include <stdio.h>
#include <string.h>
#include "../../include/omni_atomic.h"  /* Issue 4 P3: Centralized atomic operations */
#include "../../include/omni_debug.h"   /* Phase 4.4: Profiling infrastructure */

/* Phase 4.4: Forward declarations for profiling */
void _omni_stats_region_created(void);
void _omni_stats_region_destroyed(size_t bytes_freed);
extern OmniAllocCallback g_alloc_callback;
extern OmniRegionCallback g_region_callback;

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

/* ========== Issue 15 P3: Region Registry for O(1) region_of() lookup ========== */

/*
 * g_region_registry - Global registry mapping region_id -> Region*
 *
 * This enables O(1) lookup of Region* from any encoded pointer.
 * The pointer masking system encodes region_id in high 16 bits.
 * Combined with this registry, we can implement region_of(obj) in O(1).
 *
 * Thread-safety:
 * - Registration/unregistration are atomic (single pointer writes)
 * - Lookup is lock-free (single pointer read)
 * - Region* is only set to NULL after region_destroy_if_dead() confirms death
 *
 * Note: POINTER_MAX_REGIONS = 65536, but we use a smaller table to save memory.
 * If more than REGION_REGISTRY_SIZE concurrent regions exist, some will share slots.
 * This is a time-space tradeoff; can be expanded if needed.
 */
#define REGION_REGISTRY_SIZE 8192  // 8K slots = 64KB memory footprint (good for most workloads)
#define REGION_REGISTRY_MASK (REGION_REGISTRY_SIZE - 1)

static Region* g_region_registry[REGION_REGISTRY_SIZE] = {0};

/*
 * region_registry_register - Add region to global registry
 *
 * Called by region_create() after assigning region_id.
 * Thread-safe: atomic pointer store.
 */
static inline void region_registry_register(Region* r) {
    if (r) {
        uint16_t slot = r->region_id & REGION_REGISTRY_MASK;
        __atomic_store_n(&g_region_registry[slot], r, __ATOMIC_RELEASE);
    }
}

/*
 * region_registry_unregister - Remove region from global registry
 *
 * Called by region_destroy_if_dead() when region is actually freed (not pooled).
 * Thread-safe: atomic pointer store.
 *
 * Note: For pooled regions, we keep them in registry since region_id is preserved.
 */
static inline void region_registry_unregister(Region* r) {
    if (r) {
        uint16_t slot = r->region_id & REGION_REGISTRY_MASK;
        /* Only clear if this region is still the registered one */
        Region* expected = r;
        __atomic_compare_exchange_n(&g_region_registry[slot], &expected, NULL,
                                    false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
    }
}

// Reset a region for reuse (fast path)
static inline void region_reset(Region* r) {
    if (!r) return;
    // Free the arena contents
    arena_free(&r->arena);
    /* Issue 15 P4: Free size-class segregated arenas */
    arena_free(&r->pair_arena);
    arena_free(&r->container_arena);
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

    /* Issue 10 P0: Increment generation on region reuse for IPGE */
    r->generation++;  /* Bump generation to invalidate old pointers */
}

Region* region_create(void) {
    // FAST PATH: Try to get a region from the pool
    if (g_region_pool.count > 0) {
        Region* r = g_region_pool.regions[--g_region_pool.count];
        // Region is already reset, just mark it as alive
        r->scope_alive = true;
        // Note: region_id is preserved from previous lifetime (acceptable)

        /* Phase 4.4: Update global stats and invoke callback for pooled region */
        _omni_stats_region_created();
        if (g_region_callback) {
            g_region_callback(r, "create");
        }

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

    /* Issue 15 P4: Initialize size-class segregated arenas */
    r->pair_arena.begin = NULL;
    r->pair_arena.end = NULL;
    r->container_arena.begin = NULL;
    r->container_arena.end = NULL;

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

    /* Issue 10 P0: Initialize generation for IPGE (Indexed Pointer Generation Epoch) */
    r->generation = 1;  /* Start at generation 1 (0 reserved for unallocated) */

    // Assign region ID (OPTIMIZATION: T-opt-region-metadata-pointer-masking)
    /* Issue 4 P3: Use atomic wrapper for consistent memory ordering */
    r->region_id = omni_atomic_fetch_add_u16(&g_next_region_id, 1);

    /* Issue 15 P3: Register in global registry for O(1) region_of() lookup */
    region_registry_register(r);

    // Initialize Control Block
    r->external_rc = 0;
    r->tether_count = 0;
    r->scope_alive = true;

    // Initialize thread-local tracking (OPTIMIZATION: T-opt-thread-local-rc-detect)
    r->owner_thread = pthread_self();
    r->is_thread_local = true;  // Assume thread-local until proven otherwise
    r->has_external_refs = false;

    /* Phase 4.4: Initialize allocation_count */
    r->allocation_count = 0;

    /* Phase 4.4: Update global stats and invoke callback */
    _omni_stats_region_created();
    if (g_region_callback) {
        g_region_callback(r, "create");
    }

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
        /* Phase 4.4: Update stats and invoke callback before cleanup */
        _omni_stats_region_destroyed(r->bytes_allocated_total);
        if (g_region_callback) {
            g_region_callback(r, "destroy");
        }

        // OPTIMIZATION: Try to return to pool instead of freeing
        if (g_region_pool.count < REGION_POOL_SIZE) {
            // Reset and return to pool (FAST: avoids malloc/free)
            region_reset(r);
            g_region_pool.regions[g_region_pool.count++] = r;
            return;
        }

        // Pool full, actually free (SLOW: requires malloc next time)
        arena_free(&r->arena);
        /* Issue 15 P4: Free size-class segregated arenas */
        arena_free(&r->pair_arena);
        arena_free(&r->container_arena);

        // Free type metadata (OPTIMIZATION: T-opt-region-metadata)
        if (r->type_table) {
            free(r->type_table);
            r->type_table = NULL;
            r->num_types = 0;
        }

        /* Issue 15 P3: Unregister from global registry before freeing */
        region_registry_unregister(r);

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
        uintptr_t data_start = (uintptr_t)ARENA_CHUNK_DATA(c);
        uintptr_t data_end = data_start + (ARENA_CHUNK_CAPACITY(c) * sizeof(uintptr_t));
        
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

/* ========== Issue 15 P3: region_of() O(1) Lookup ========== */

/*
 * region_of - Get the Region* that owns an encoded pointer in O(1)
 *
 * Issue 15 P3: This is the core lookup function for the store barrier.
 * Given any encoded pointer (with region_id in high bits), returns
 * the owning Region* in constant time.
 *
 * @param encoded_ptr: Any pointer with region_id encoded in high 16 bits
 * @return: Region* if found in registry, NULL otherwise
 *
 * How it works:
 * 1. Extract region_id from pointer high bits (via pointer_mask_get_region)
 * 2. Hash region_id to registry slot (& REGION_REGISTRY_MASK)
 * 3. Load Region* from registry with atomic acquire
 * 4. Validate that the region's region_id matches (collision check)
 *
 * Thread-safety: Lock-free read with atomic acquire semantics.
 *
 * Edge cases:
 * - NULL pointer: Returns NULL
 * - Unencoded pointer (region_id=0): Returns NULL (0 is reserved)
 * - Freed region: Returns NULL (unregistered on free)
 * - Pooled region: Returns valid Region* (stays registered)
 *
 * Performance: O(1) - single array lookup + validation
 *
 * Usage in store barrier:
 *   Region* src_region = region_of(src_obj);
 *   Region* dst_region = region_of(dst_obj);
 *   if (!omni_region_outlives(dst_region, src_region)) {
 *       // Repair needed: dst is younger or ordering unknown
 *   }
 */
Region* region_of(const void* encoded_ptr) {
    if (!encoded_ptr) return NULL;

    /* Extract region_id from pointer high bits */
    uint16_t region_id = pointer_mask_get_region(encoded_ptr);

    /* Region ID 0 is reserved for NULL/unencoded pointers */
    if (region_id == 0) return NULL;

    /* O(1) lookup in registry */
    uint16_t slot = region_id & REGION_REGISTRY_MASK;
    Region* r = __atomic_load_n(&g_region_registry[slot], __ATOMIC_ACQUIRE);

    /* Validate region_id matches (handle hash collisions) */
    if (r && r->region_id == region_id) {
        return r;
    }

    /* Collision or region was freed */
    return NULL;
}

/* ============================================================
 * Phase 4.4: Memory Profiling Infrastructure
 * ============================================================ */

#include "../../include/omni_debug.h"

/* Global statistics tracking */
static GlobalMemStats g_global_stats = {0};
static pthread_mutex_t g_stats_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Profiling callbacks */
/* Phase 4.4: Profiling callbacks (non-static for inline access) */
OmniAllocCallback g_alloc_callback = NULL;
OmniRegionCallback g_region_callback = NULL;

/*
 * omni_region_get_stats - Get statistics for a single region
 */
void omni_region_get_stats(Region* r, RegionStats* stats) {
    if (!r || !stats) return;

    memset(stats, 0, sizeof(RegionStats));

    stats->bytes_allocated = r->bytes_allocated_total;
    stats->bytes_peak = r->bytes_allocated_peak;
    stats->allocation_count = r->allocation_count;  /* Phase 4.4: Now tracked */

    stats->inline_bytes_used = r->inline_buf_used_bytes;
    stats->inline_capacity = REGION_INLINE_BUF_SIZE;

    stats->arena_chunks = r->chunk_count;
    stats->escape_repairs = r->escape_repair_count;

    stats->external_rc = r->external_rc;
    stats->tether_count = r->tether_count;

    stats->lifetime_rank = r->lifetime_rank;
    stats->scope_alive = r->scope_alive;
    stats->is_thread_local = r->is_thread_local;

    /* Calculate parent depth */
    int depth = 0;
    Region* p = r->parent;
    while (p) {
        depth++;
        p = p->parent;
    }
    stats->parent_depth = depth;
}

/*
 * omni_region_print_stats - Print region statistics to stderr
 */
void omni_region_print_stats(Region* r, const char* label) {
    if (!r) {
        fprintf(stderr, "[Region Stats] %s: NULL region\n", label ? label : "");
        return;
    }

    RegionStats stats;
    omni_region_get_stats(r, &stats);

    fprintf(stderr, "[Region Stats] %s (id=%u, rank=%lu)\n",
            label ? label : "", r->region_id, (unsigned long)stats.lifetime_rank);
    fprintf(stderr, "  allocated: %zu bytes (peak: %zu)\n",
            stats.bytes_allocated, stats.bytes_peak);
    fprintf(stderr, "  inline: %zu/%zu bytes\n",
            stats.inline_bytes_used, stats.inline_capacity);
    fprintf(stderr, "  arena chunks: %zu\n", stats.arena_chunks);
    fprintf(stderr, "  escape repairs: %zu\n", stats.escape_repairs);
    fprintf(stderr, "  external_rc: %d, tethers: %d\n",
            stats.external_rc, stats.tether_count);
    fprintf(stderr, "  scope_alive: %s, thread_local: %s\n",
            stats.scope_alive ? "yes" : "no",
            stats.is_thread_local ? "yes" : "no");
    fprintf(stderr, "  parent depth: %d\n", stats.parent_depth);
}

/*
 * omni_get_global_stats - Get aggregate memory statistics
 */
void omni_get_global_stats(GlobalMemStats* stats) {
    if (!stats) return;

    pthread_mutex_lock(&g_stats_mutex);
    memcpy(stats, &g_global_stats, sizeof(GlobalMemStats));
    pthread_mutex_unlock(&g_stats_mutex);
}

/*
 * omni_print_memory_summary - Print global memory summary
 */
void omni_print_memory_summary(void) {
    GlobalMemStats stats;
    omni_get_global_stats(&stats);

    fprintf(stderr, "=== OmniLisp Memory Summary ===\n");
    fprintf(stderr, "Regions: %zu created, %zu destroyed, %zu active (peak: %zu)\n",
            stats.regions_created, stats.regions_destroyed,
            stats.regions_active, stats.regions_peak);
    fprintf(stderr, "Memory: %zu total allocated, %zu current, %zu peak\n",
            stats.total_bytes_allocated, stats.current_bytes, stats.peak_bytes);
    fprintf(stderr, "Operations: %zu transmigrates, %zu merges, %zu store repairs\n",
            stats.transmigrate_calls, stats.merge_calls, stats.store_repairs);
    fprintf(stderr, "================================\n");
}

/*
 * omni_reset_global_stats - Reset statistics (for testing)
 */
void omni_reset_global_stats(void) {
    pthread_mutex_lock(&g_stats_mutex);
    memset(&g_global_stats, 0, sizeof(GlobalMemStats));
    pthread_mutex_unlock(&g_stats_mutex);
}

/*
 * omni_print_active_regions - Print all active regions
 */
void omni_print_active_regions(void) {
    fprintf(stderr, "=== Active Regions ===\n");
    int count = 0;

    for (int i = 0; i < REGION_REGISTRY_SIZE; i++) {
        Region* r = __atomic_load_n(&g_region_registry[i], __ATOMIC_ACQUIRE);
        if (r && r->scope_alive) {
            char label[32];
            snprintf(label, sizeof(label), "Region #%d", count++);
            omni_region_print_stats(r, label);
            fprintf(stderr, "\n");
        }
    }

    if (count == 0) {
        fprintf(stderr, "(no active regions)\n");
    }
    fprintf(stderr, "======================\n");
}

/*
 * omni_set_alloc_callback - Set profiling callback for allocations
 */
void omni_set_alloc_callback(OmniAllocCallback callback) {
    g_alloc_callback = callback;
}

/*
 * omni_set_region_callback - Set profiling callback for region lifecycle
 */
void omni_set_region_callback(OmniRegionCallback callback) {
    g_region_callback = callback;
}

/*
 * Internal: Update global stats on region create
 */
void _omni_stats_region_created(void) {
    pthread_mutex_lock(&g_stats_mutex);
    g_global_stats.regions_created++;
    g_global_stats.regions_active++;
    if (g_global_stats.regions_active > g_global_stats.regions_peak) {
        g_global_stats.regions_peak = g_global_stats.regions_active;
    }
    pthread_mutex_unlock(&g_stats_mutex);
}

/*
 * Internal: Update global stats on region destroy
 */
void _omni_stats_region_destroyed(size_t bytes_freed) {
    pthread_mutex_lock(&g_stats_mutex);
    g_global_stats.regions_destroyed++;
    if (g_global_stats.regions_active > 0) {
        g_global_stats.regions_active--;
    }
    if (g_global_stats.current_bytes >= bytes_freed) {
        g_global_stats.current_bytes -= bytes_freed;
    }
    pthread_mutex_unlock(&g_stats_mutex);
}

/*
 * Internal: Update global stats on allocation
 */
void _omni_stats_allocation(size_t size) {
    pthread_mutex_lock(&g_stats_mutex);
    g_global_stats.total_bytes_allocated += size;
    g_global_stats.current_bytes += size;
    if (g_global_stats.current_bytes > g_global_stats.peak_bytes) {
        g_global_stats.peak_bytes = g_global_stats.current_bytes;
    }
    pthread_mutex_unlock(&g_stats_mutex);
}

/*
 * Internal: Update global stats on transmigrate
 */
void _omni_stats_transmigrate(void) {
    pthread_mutex_lock(&g_stats_mutex);
    g_global_stats.transmigrate_calls++;
    pthread_mutex_unlock(&g_stats_mutex);
}

/*
 * Internal: Update global stats on store repair
 */
void _omni_stats_store_repair(void) {
    pthread_mutex_lock(&g_stats_mutex);
    g_global_stats.store_repairs++;
    pthread_mutex_unlock(&g_stats_mutex);
}

#if OMNI_DEBUG >= 1

/* ============================================================
 * Leak Detection (Debug builds only)
 * ============================================================ */

#define MAX_TRACKED_ALLOCS 10000

typedef struct {
    void* ptr;
    size_t size;
    const char* file;
    int line;
} TrackedAlloc;

static TrackedAlloc g_tracked_allocs[MAX_TRACKED_ALLOCS];
static size_t g_tracked_count = 0;
static pthread_mutex_t g_track_mutex = PTHREAD_MUTEX_INITIALIZER;

void omni_debug_track_alloc(void* ptr, size_t size, const char* file, int line) {
    if (!ptr) return;

    pthread_mutex_lock(&g_track_mutex);
    if (g_tracked_count < MAX_TRACKED_ALLOCS) {
        g_tracked_allocs[g_tracked_count].ptr = ptr;
        g_tracked_allocs[g_tracked_count].size = size;
        g_tracked_allocs[g_tracked_count].file = file;
        g_tracked_allocs[g_tracked_count].line = line;
        g_tracked_count++;
    }
    pthread_mutex_unlock(&g_track_mutex);
}

void omni_debug_track_free(void* ptr, const char* file, int line) {
    (void)file;
    (void)line;
    if (!ptr) return;

    pthread_mutex_lock(&g_track_mutex);
    for (size_t i = 0; i < g_tracked_count; i++) {
        if (g_tracked_allocs[i].ptr == ptr) {
            /* Swap with last and decrement count */
            g_tracked_allocs[i] = g_tracked_allocs[g_tracked_count - 1];
            g_tracked_count--;
            break;
        }
    }
    pthread_mutex_unlock(&g_track_mutex);
}

void omni_debug_print_leaks(void) {
    pthread_mutex_lock(&g_track_mutex);
    if (g_tracked_count == 0) {
        fprintf(stderr, "[Leak Check] No leaks detected.\n");
    } else {
        fprintf(stderr, "[Leak Check] %zu potential leaks:\n", g_tracked_count);
        for (size_t i = 0; i < g_tracked_count && i < 20; i++) {
            fprintf(stderr, "  %p: %zu bytes at %s:%d\n",
                    g_tracked_allocs[i].ptr,
                    g_tracked_allocs[i].size,
                    g_tracked_allocs[i].file,
                    g_tracked_allocs[i].line);
        }
        if (g_tracked_count > 20) {
            fprintf(stderr, "  ... and %zu more\n", g_tracked_count - 20);
        }
    }
    pthread_mutex_unlock(&g_track_mutex);
}

size_t omni_debug_leak_count(void) {
    pthread_mutex_lock(&g_track_mutex);
    size_t count = g_tracked_count;
    pthread_mutex_unlock(&g_track_mutex);
    return count;
}

void omni_debug_clear_tracking(void) {
    pthread_mutex_lock(&g_track_mutex);
    g_tracked_count = 0;
    pthread_mutex_unlock(&g_track_mutex);
}

#endif /* OMNI_DEBUG >= 1 */
