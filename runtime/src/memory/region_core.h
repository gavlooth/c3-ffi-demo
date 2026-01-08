#ifndef OMNI_REGION_CORE_H
#define OMNI_REGION_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../../third_party/arena/arena.h"

// OPTIMIZATION: Inline allocation buffer for small objects (< 64 bytes)
// This avoids arena_alloc overhead for frequently allocated small objects
#define REGION_INLINE_BUF_SIZE 512
#define REGION_INLINE_MAX_ALLOC 64

typedef struct {
    char buffer[REGION_INLINE_BUF_SIZE];  // Inline storage for small objects
    size_t offset;                        // Current bump pointer offset
    size_t capacity;                      // Total capacity (REGION_INLINE_BUF_SIZE)
} InlineBuffer;

// Region Control Block (RCB)
// The logical owner of a memory region.
typedef struct Region {
    Arena arena;                // The physical storage (bump allocator)
    InlineBuffer inline_buf;    // Fast inline buffer for small objects (< 64 bytes)
    int external_rc;            // Strong refs from OTHER regions/stack (atomic)
    int tether_count;           // Temporary "borrows" by threads (atomic)
    bool scope_alive;           // True if the semantic scope is still active
} Region;

// Lifecycle
Region* region_create(void);
void region_destroy_if_dead(Region* r);

// Scope Management
void region_exit(Region* r);

// RC Management (Internal - use RegionRef for high level)
void region_retain_internal(Region* r);
void region_release_internal(Region* r);

// Global Region Support
Region* region_get_or_create(void);
char* region_strdup(Region* r, const char* s);

// Tethering
void region_tether_start(Region* r);
void region_tether_end(Region* r);

// Allocation
/*
 * region_alloc - Allocate memory from a region
 *
 * OPTIMIZATION (T-opt-inline-alloc-fastpath): Marked as static inline for hot path.
 * The inline buffer fast path is now inlineable, eliminating call overhead.
 *
 * For small objects (< 64 bytes): Uses inline buffer (O(1) bump pointer)
 * For large objects or exhausted buffer: Falls back to arena_alloc
 */
static inline void* region_alloc(Region* r, size_t size) {
    // FAST PATH: Inline buffer for small objects (< 64 bytes)
    if (size <= REGION_INLINE_MAX_ALLOC && r) {
        InlineBuffer* buf = &r->inline_buf;

        // Check if we have space in the inline buffer
        if (buf->offset + size <= buf->capacity) {
            void* ptr = &buf->buffer[buf->offset];
            buf->offset += size;
            return ptr;
        }
        // Fall through to arena if inline buffer is exhausted
    }

    // SLOW PATH: use arena allocator for large objects or exhausted inline buffer
    return arena_alloc(&r->arena, size);
}

void region_splice(Region* dest, Region* src, void* start_ptr, void* end_ptr);

// -- Region Reference (Smart Pointer) --
typedef struct RegionRef {
    void* ptr;          // The actual object
    Region* region;     // The region keeping it alive
} RegionRef;

// Atomic RC operations on the RegionRef
void region_retain(RegionRef ref);
void region_release(RegionRef ref);

#endif // OMNI_REGION_CORE_H
