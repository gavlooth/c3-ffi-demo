#ifndef OMNI_SCRATCH_ARENA_H
#define OMNI_SCRATCH_ARENA_H

/*
 * scratch_arena.h - Scratch Arena API for Temporary Allocations
 *
 * Issue 15 P0: Implements the "scratch arena" pattern (double-buffered arenas)
 * for fast temporary allocations that are automatically released when a scope ends.
 *
 * Key benefits:
 * - O(1) allocation (bump pointer)
 * - O(1) deallocation (arena reset, no per-object free)
 * - Nested scratch contexts using "conflict" parameter
 * - Thread-local arenas (no locking for single-threaded hot paths)
 *
 * Usage pattern:
 *   Scratch scratch = scratch_begin(NULL);  // Get a scratch arena
 *   void* temp = scratch_alloc(&scratch, 1024);  // Allocate temporary memory
 *   // ... use temp ...
 *   scratch_end(&scratch);  // Free all scratch allocations in O(1)
 *
 * Nested usage with conflict:
 *   Scratch outer = scratch_begin(NULL);
 *   void* outer_data = scratch_alloc(&outer, 100);
 *
 *   Scratch inner = scratch_begin(outer.arena);  // Uses different arena!
 *   void* inner_data = scratch_alloc(&inner, 50);
 *   scratch_end(&inner);  // Frees inner_data only
 *
 *   // outer_data still valid
 *   scratch_end(&outer);  // Now frees outer_data
 *
 * SOTA reference: Ryan Fleury's "Untangling Lifetimes: Arena Allocator" (2022)
 */

#include "../../../third_party/arena/arena_config.h"
#include <stdbool.h>
#include <stddef.h>

/*
 * Scratch - Handle to a scratch allocation context
 *
 * Contains the arena pointer and a snapshot for reset.
 * Created by scratch_begin(), released by scratch_end().
 */
typedef struct Scratch {
    Arena* arena;           /* The underlying arena (thread-local A or B) */
    Arena_Mark checkpoint;  /* Snapshot for reset (saves arena state) */
} Scratch;

/*
 * scratch_begin - Begin a scratch allocation context
 *
 * @param conflict: Optional arena to avoid (for nested scratch contexts)
 * @return: Scratch handle for temporary allocations
 *
 * If conflict is NULL, returns scratch using the first available arena.
 * If conflict matches arena A, returns scratch using arena B (and vice versa).
 *
 * Thread-safety: Uses thread-local arenas, safe for concurrent use.
 */
Scratch scratch_begin(Arena* conflict);

/*
 * scratch_end - End a scratch allocation context
 *
 * @param scratch: The scratch handle to release
 *
 * Resets the arena to its state at scratch_begin() time.
 * All allocations made through this scratch handle are freed in O(1).
 *
 * IMPORTANT: Must be called in LIFO order (nested scratches must end
 * before outer scratches). Failure to do so corrupts arena state.
 */
void scratch_end(Scratch* scratch);

/*
 * scratch_alloc - Allocate memory from a scratch context
 *
 * @param scratch: The scratch handle
 * @param size: Bytes to allocate
 * @return: Pointer to allocated memory, or NULL on failure
 *
 * Memory is 8-byte aligned. All allocations are freed when scratch_end() is called.
 */
static inline void* scratch_alloc(Scratch* scratch, size_t size) {
    if (!scratch || !scratch->arena) return NULL;
    return arena_alloc(scratch->arena, size);
}

/*
 * scratch_alloc_aligned - Allocate aligned memory from a scratch context
 *
 * @param scratch: The scratch handle
 * @param size: Bytes to allocate
 * @param alignment: Required alignment (must be power of 2)
 * @return: Pointer to allocated memory, or NULL on failure
 */
void* scratch_alloc_aligned(Scratch* scratch, size_t size, size_t alignment);

/*
 * scratch_get_arena - Get the underlying arena (for passing to functions)
 *
 * @param scratch: The scratch handle
 * @return: Pointer to the underlying arena
 *
 * Use this when you need to pass the arena to functions that take Arena*.
 */
static inline Arena* scratch_get_arena(Scratch* scratch) {
    return scratch ? scratch->arena : NULL;
}

/*
 * scratch_is_active - Check if a scratch context is currently active
 *
 * @param scratch: The scratch handle to check
 * @return: true if the scratch is active (begun but not ended)
 */
static inline bool scratch_is_active(Scratch* scratch) {
    return scratch && scratch->arena != NULL;
}

/*
 * scratch_release_all - Release all scratch memory for current thread
 *
 * Frees both thread-local scratch arenas. Useful for:
 * - Long-running threads that want to release memory pressure
 * - Thread pool workers between jobs
 * - Testing/benchmarking memory behavior
 *
 * Not typically needed - OS reclaims memory on thread exit.
 */
void scratch_release_all(void);

#endif /* OMNI_SCRATCH_ARENA_H */
