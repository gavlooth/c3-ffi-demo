#include "region_core.h"
#include <stdio.h>
#include <string.h>

#define MAX_THREAD_LOCAL_TETHERS 16

typedef struct {
    Region* regions[MAX_THREAD_LOCAL_TETHERS];
    int counts[MAX_THREAD_LOCAL_TETHERS];
    int size;
} TetherCache;

static __thread TetherCache g_tether_cache = { .size = 0 };

Region* region_create(void) {
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

void* region_alloc(Region* r, size_t size) {
    return arena_alloc(&r->arena, size);
}

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