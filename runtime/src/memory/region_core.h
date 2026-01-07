#ifndef OMNI_REGION_CORE_H
#define OMNI_REGION_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "../../../third_party/arena/arena.h"

// Region Control Block (RCB)
// The logical owner of a memory region.
typedef struct Region {
    Arena arena;                // The physical storage (bump allocator)
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
void* region_alloc(Region* r, size_t size);
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
