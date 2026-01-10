#ifndef OMNI_REGION_CORE_H
#define OMNI_REGION_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
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

/* Region is part of the public runtime API. Many translation units include:
 * - internal headers (region_core.h) and
 * - the public header (runtime/include/omni.h)
 *
 * To keep the build warning-clean under C99 + extensions, we must NOT
 * redefine typedefs depending on include order.
 *
 * Canonical rule:
 * - If the typedef already exists, do not re-typedef it here.
 * - Always define the struct layout here (internal ownership).
 */
#ifndef OMNI_REGION_TYPEDEF
#define OMNI_REGION_TYPEDEF 1
typedef struct Region Region;
#endif

// Region Control Block (RCB)
// The logical owner of a memory region.
struct Region {
    Arena arena;                // The physical storage (bump allocator)
    InlineBuffer inline_buf;    // Fast inline buffer for small objects (< 64 bytes)

    /* OPTIMIZATION (T-opt-region-metadata): Region-level type metadata */
    struct TypeMetadata* type_table;   /* Centralized metadata (one per region) */
    uint32_t num_types;          /* Number of types in type_table */

    /* OPTIMIZATION (T-opt-region-metadata-pointer-masking): Region ID for pointer masking */
    uint16_t region_id;          /* Unique region identifier (for encoded pointers) */

    /* OPTIMIZATION (T-opt-thread-local-rc-detect): Thread-local detection for fast non-atomic RC */
    pthread_t owner_thread;      /* Thread that created this region */
    bool is_thread_local;        /* Cached detection result (true if only accessed by owner) */
    bool has_external_refs;      /* Track if any external refs exist from other threads */

    int external_rc;            // Strong refs from OTHER regions/stack (atomic)
    int tether_count;           // Temporary "borrows" by threads (atomic)
    bool scope_alive;           // True if the semantic scope is still active

    /* Issue 2 P3: Region accounting counters */
    size_t bytes_allocated_total;     /* Total bytes allocated in this region */
    size_t bytes_allocated_peak;      /* Peak allocation for memory pressure detection */
    size_t inline_buf_used_bytes;   /* Bytes used in inline buffer */
    size_t escape_repair_count;      /* How often auto-repair triggered */
    size_t chunk_count;             /* Number of arena chunks allocated */
    ArenaChunk* last_arena_end;     /* Internal: detect chunk growth without touching third_party */
};

#include "region_metadata.h"  /* Region-level type metadata (must come after Region typedef) */

// Lifecycle
Region* region_create(void);
void region_destroy_if_dead(Region* r);

// Scope Management
void region_exit(Region* r);

// RC Management (Internal - use RegionRef for high level)
void region_retain_internal(Region* r);
void region_release_internal(Region* r);

/* OPTIMIZATION (T-opt-thread-local-rc-detect): Thread-local detection */
bool region_is_thread_local(Region* r);
void region_mark_external_ref(Region* r);

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
    if (!r) return NULL;

    // FAST PATH: Inline buffer for small objects (< 64 bytes)
    if (size <= REGION_INLINE_MAX_ALLOC && r) {
        InlineBuffer* buf = &r->inline_buf;

        // ALIGNMENT: Ensure 8-byte alignment for all allocations
        // This is critical for the tagged pointer system to work correctly
        // (low 3 bits of pointer must be 0 for heap objects)
        size_t aligned_size = (size + 7) & ~7ULL;  // Round up to multiple of 8

        // Check if we have space in the inline buffer
        if (buf->offset + aligned_size <= buf->capacity) {
            void* ptr = &buf->buffer[buf->offset];
            buf->offset += aligned_size;

            /* Issue 2 P3: Update accounting for inline allocation */
            r->bytes_allocated_total += aligned_size;
            r->inline_buf_used_bytes += aligned_size;
            if (r->inline_buf_used_bytes > r->bytes_allocated_peak) {
                r->bytes_allocated_peak = r->inline_buf_used_bytes;
            }

            return ptr;
        }
        // Fall through to arena if inline buffer is exhausted
    }

    /* Issue 2 P3: Update accounting for arena allocation and track chunk count */
    ArenaChunk* before = r->arena.end;
    void* ptr = arena_alloc(&r->arena, size);

    if (r->arena.end != before) {
        r->chunk_count++;
        r->last_arena_end = r->arena.end;
    }

    r->bytes_allocated_total += size;
    if (r->bytes_allocated_total > r->bytes_allocated_peak) {
        r->bytes_allocated_peak = r->bytes_allocated_total;
    }

    return ptr;
}

void region_splice(Region* dest, Region* src, void* start_ptr, void* end_ptr);

/*
 * region_can_splice_arena_only - Check if splice fast-path is sound
 *
 * OPTIMIZATION (T-opt-transmigrate-soundness): The splice optimization
 * (O(1) arena chunk transfer) is only safe when NO allocations were made
 * to the inline buffer, because inline buffer allocations would NOT be
 * transferred by the arena splice.
 *
 * This predicate makes the splice rule self-documenting and prevents
 * silent dangling pointer bugs.
 *
 * @param r: Region to check
 * @return: true if inline_buf.offset == 0 (no inline allocations made)
 *
 * Usage in transmigrate.c:
 *   if (src_region && src_region->external_rc == 0 && !src_region->scope_alive) {
 *       if (region_can_splice_arena_only(src_region)) {
 *           // Safe to splice arena chunks only
 *       }
 *   }
 */
static inline bool region_can_splice_arena_only(const Region* r) {
    return r && (r->inline_buf.offset == 0);
}

/*
 * region_alloc_typed - Allocate memory using type metadata
 *
 * OPTIMIZATION (T-opt-region-metadata): Uses TypeMetadata to determine
 * allocation strategy based on type-specific inline thresholds.
 *
 * @param r: The region to allocate from
 * @param type_id: Compile-time type identifier
 * @param size: Requested allocation size
 * @return: Pointer to allocated memory, or NULL on failure
 *
 * This function combines the speed of inline allocation with type-specific
 * knowledge. For types with can_inline=true and small inline_threshold,
 * it uses the inline buffer. For larger types or those that can't be inlined,
 * it falls back to arena allocation.
 *
 * Example:
 *   Obj* obj = region_alloc_typed(r, TYPE_ID_INT, sizeof(Obj));
 *   // Uses inline buffer because TYPE_ID_INT has can_inline=true
 *   // and inline_threshold=16 (larger than sizeof(Obj)=8)
 */
static inline void* region_alloc_typed(Region* r, TypeID type_id, size_t size) {
    if (!r) return NULL;

    /* Look up metadata to check if this type can be inlined */
    const TypeMetadata* meta = type_metadata_get(r, type_id);

    /* FAST PATH: Use inline buffer for small, inlineable types */
    if (meta && meta->can_inline && size <= meta->inline_threshold) {
        InlineBuffer* buf = &r->inline_buf;

        /* ALIGNMENT: Ensure 8-byte alignment */
        size_t aligned_size = (size + 7) & ~7ULL;

        /* Check inline buffer capacity */
        if (buf->offset + aligned_size <= buf->capacity) {
            void* ptr = &buf->buffer[buf->offset];
            buf->offset += aligned_size;
            return ptr;
        }
        /* Fall through to arena if inline buffer exhausted */
    }

    /* SLOW PATH: Arena allocation for large or non-inlineable types */
    return arena_alloc(&r->arena, size);
}

// -- Region Reference (Smart Pointer) --
typedef struct RegionRef {
    void* ptr;          // The actual object
    Region* region;     // The region keeping it alive
} RegionRef;

// Atomic RC operations on the RegionRef
void region_retain(RegionRef ref);
void region_release(RegionRef ref);

#endif // OMNI_REGION_CORE_H
