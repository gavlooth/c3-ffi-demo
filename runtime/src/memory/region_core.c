#include "region_core.h"
#include <stdio.h>
#include <string.h>

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

// Reset a region for reuse (fast path)
static inline void region_reset(Region* r) {
    if (!r) return;
    // Free the arena contents
    arena_free(&r->arena);
    // Reset inline buffer (OPTIMIZATION: T-opt-inline-allocation)
    r->inline_buf.offset = 0;
    // Reset control block
    r->external_rc = 0;
    r->tether_count = 0;
    r->scope_alive = true;
}

Region* region_create(void) {
    // FAST PATH: Try to get a region from the pool
    if (g_region_pool.count > 0) {
        Region* r = g_region_pool.regions[--g_region_pool.count];
        // Region is already reset, just mark it as alive
        r->scope_alive = true;
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

    // Initialize Control Block
    r->external_rc = 0;
    r->tether_count = 0;
    r->scope_alive = true;

    return r;
}

void region_destroy_if_dead(Region* r) {
    if (!r) return;

    // Check liveness:
    // 1. Scope must be dead (region_exit called)
    // 2. No external references (external_rc == 0)
    // 3. No active tethers (tether_count == 0)

    // We use atomic loads for thread safety check
    int rc = __atomic_load_n(&r->external_rc, __ATOMIC_ACQUIRE);
    int tc = __atomic_load_n(&r->tether_count, __ATOMIC_ACQUIRE);

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
        free(r);
    }
}

void region_exit(Region* r) {
    if (r) {
        r->scope_alive = false;
        region_destroy_if_dead(r);
    }
}

void region_retain_internal(Region* r) {
    if (r) {
        __atomic_add_fetch(&r->external_rc, 1, __ATOMIC_SEQ_CST);
    }
}

void region_release_internal(Region* r) {
    if (r) {
        int new_rc = __atomic_sub_fetch(&r->external_rc, 1, __ATOMIC_SEQ_CST);
        if (new_rc == 0) {
            region_destroy_if_dead(r);
        }
    }
}

void region_tether_start(Region* r) {
    if (!r) return;

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