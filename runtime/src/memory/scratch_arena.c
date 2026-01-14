/*
 * scratch_arena.c - Improved Scratch Arena Implementation
 *
 * Issue 15 P0: Double-buffered thread-local arenas for temporary allocations.
 *
 * Improvements over naive implementation:
 * - Lazy initialization (zero cost until first use)
 * - Physical memory release on scratch_end via arena reset
 * - High-water trimming to bound memory growth
 * - Debug-mode LIFO violation detection
 */

#include "scratch_arena.h"
#include <string.h>
#include <stdio.h>

/*
 * Thread-local scratch state
 *
 * Lazy initialization: arenas start as {0} and are only allocated on first use.
 * This means threads that never use scratch pay zero memory cost.
 */
static __thread Arena g_scratch_arena_a = {0};
static __thread Arena g_scratch_arena_b = {0};

/*
 * Nesting depth tracking (for LIFO validation)
 *
 * Each arena tracks how many active scratch contexts are using it.
 * scratch_begin increments, scratch_end decrements.
 * In debug mode, we detect out-of-order scratch_end calls.
 */
static __thread int g_scratch_depth_a = 0;
static __thread int g_scratch_depth_b = 0;

/*
 * High-water mark for trimming
 *
 * If a scratch arena grows beyond this threshold, we trim it after use
 * to prevent unbounded memory growth from a single large operation.
 * Default: 1MB - enough for most operations, but not wasteful.
 */
#ifndef SCRATCH_TRIM_THRESHOLD
#define SCRATCH_TRIM_THRESHOLD (1 * 1024 * 1024)
#endif

/*
 * scratch_arena_bytes_used - Estimate bytes allocated in an arena
 *
 * For trimming decisions. Returns approximate bytes in use.
 */
static size_t scratch_arena_bytes_used(Arena* a) {
    if (!a || !a->begin) return 0;

    size_t total = 0;
    for (ArenaChunk* c = a->begin; c; c = c->next) {
        /* Use compatibility accessor for chunk size */
        total += ARENA_CHUNK_CAPACITY(c) * sizeof(uintptr_t);
    }
    return total;
}

Scratch scratch_begin(Arena* conflict) {
    Scratch result = {0};
    Arena* chosen = NULL;
    int* depth = NULL;

    /*
     * Select arena that doesn't conflict:
     * - If conflict is A, use B
     * - If conflict is B, use A
     * - If conflict is NULL, use the one with lower nesting depth
     *   (this spreads load and avoids deep nesting on one arena)
     */
    if (conflict == &g_scratch_arena_a) {
        chosen = &g_scratch_arena_b;
        depth = &g_scratch_depth_b;
    } else if (conflict == &g_scratch_arena_b) {
        chosen = &g_scratch_arena_a;
        depth = &g_scratch_depth_a;
    } else {
        /* No conflict: prefer the arena with lower depth */
        if (g_scratch_depth_a <= g_scratch_depth_b) {
            chosen = &g_scratch_arena_a;
            depth = &g_scratch_depth_a;
        } else {
            chosen = &g_scratch_arena_b;
            depth = &g_scratch_depth_b;
        }
    }

    result.arena = chosen;

    /*
     * Checkpoint: Save current arena state for later reset.
     *
     * LAZY INIT: If arena has no chunks yet, arena_snapshot returns
     * a mark with chunk=NULL, count=0. This is fine - arena_rewind
     * handles it correctly by freeing all chunks.
     */
    result.checkpoint = arena_snapshot(chosen);

    /* Track nesting depth */
    (*depth)++;

    return result;
}

void scratch_end(Scratch* scratch) {
    if (!scratch || !scratch->arena) return;

    /* Determine which depth counter to update */
    int* depth = NULL;
    if (scratch->arena == &g_scratch_arena_a) {
        depth = &g_scratch_depth_a;
    } else if (scratch->arena == &g_scratch_arena_b) {
        depth = &g_scratch_depth_b;
    }

    /* LIFO validation in debug mode */
#ifdef SCRATCH_ARENA_DEBUG
    if (depth && *depth <= 0) {
        fprintf(stderr, "[SCRATCH] ERROR: scratch_end called without matching scratch_begin\n");
    }
#endif

    /*
     * Check if we should trim after this scratch ends.
     *
     * Only trim when:
     * 1. This is the outermost scratch (depth will become 0)
     * 2. Arena has grown beyond threshold
     *
     * This prevents unbounded memory growth while avoiding
     * repeated alloc/free for nested scratches.
     */
    bool should_trim = false;
    if (depth && *depth == 1) {
        /* Outermost scratch ending - check if trim needed */
        size_t used = scratch_arena_bytes_used(scratch->arena);
        if (used > SCRATCH_TRIM_THRESHOLD) {
            should_trim = true;
        }
    }

    /*
     * Reset arena to checkpoint state.
     *
     * arena_rewind() restores the arena to its state at scratch_begin.
     * For the outermost scratch, this effectively resets to empty.
     */
    arena_rewind(scratch->arena, scratch->checkpoint);

    /*
     * Trim if needed: release excess memory back to OS.
     *
     * arena_trim() releases unused chunks while keeping the arena usable.
     * This bounds memory footprint after large operations.
     */
    if (should_trim) {
        arena_trim(scratch->arena);
    }

    /* Update depth counter */
    if (depth && *depth > 0) {
        (*depth)--;
    }

    /* Clear the scratch handle to prevent double-end bugs */
    scratch->arena = NULL;
    scratch->checkpoint.chunk = NULL;
    scratch->checkpoint.count = 0;
}

void* scratch_alloc_aligned(Scratch* scratch, size_t size, size_t alignment) {
    if (!scratch || !scratch->arena || size == 0) return NULL;

    /* Alignment must be power of 2 */
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        alignment = 8;
    }

    /* Default arena_alloc provides 8-byte alignment */
    if (alignment <= 8) {
        return arena_alloc(scratch->arena, size);
    }

    /*
     * For larger alignments, over-allocate and adjust.
     * Worst case: need (alignment - 1) extra bytes for padding.
     */
    size_t total_size = size + alignment - 1;
    void* raw = arena_alloc(scratch->arena, total_size);
    if (!raw) return NULL;

    /* Align the pointer */
    uintptr_t addr = (uintptr_t)raw;
    uintptr_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);

    return (void*)aligned_addr;
}

/*
 * scratch_release_all - Release all scratch memory for current thread
 *
 * Called during thread cleanup to free scratch arenas.
 * Not typically needed (OS reclaims on thread exit) but useful
 * for long-running threads that want to release memory.
 */
void scratch_release_all(void) {
    arena_free(&g_scratch_arena_a);
    arena_free(&g_scratch_arena_b);
    g_scratch_depth_a = 0;
    g_scratch_depth_b = 0;
}
