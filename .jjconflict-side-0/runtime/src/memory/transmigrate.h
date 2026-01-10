#ifndef OMNI_TRANSMIGRATE_H
#define OMNI_TRANSMIGRATE_H

#include "region_core.h"
#include "../../include/omni.h" // For Obj definition and types

// Forward declaration to avoid circular dependency
// Value is defined in src/runtime/types.h but transmigrate operates
// on opaque pointers for maximum flexibility
struct Value;

/*
 * Transmigrates (moves) an object graph from its current region
 * to the destination region.
 *
 * This is the core operation for handling data that escapes its
 * original region (e.g., returning local data from a function).
 *
 * Strategy:
 * - Small graphs (< 4KB): Deep copy with cycle detection
 * - Large graphs: Arena promotion (append source arena to destination)
 *
 * Args:
 *   root: The root object to transmigrate (must be a Value*)
 *   dest_region: The destination region
 *
 * Returns:
 *   Pointer to the transmigrated object in dest_region
 */
void* transmigrate(void* root, Region* src_region, Region* dest_region);

/*
 * transmigrate_incremental - Incrementally transmigrate large graphs (OPTIMIZATION: T-opt-transmigrate-incremental)
 *
 * For very large graphs with sparse access patterns, processes the graph in chunks
 * to reduce peak memory usage and improve latency for partially-accessed data.
 *
 * Strategy:
 * - Process objects in configurable batch sizes
 * - Returns after each chunk, allowing early termination
 * - Useful for graphs where only a subset of nodes are accessed
 *
 * Args:
 *   root: The root object to transmigrate
 *   src_region: Source region
 *   dest_region: Destination region
 *   chunk_size: Number of objects to process per chunk (0 = all at once)
 *   progress_out: Optional output parameter for progress tracking (0.0 to 1.0)
 *
 * Returns:
 *   Pointer to the transmigrated root (same as transmigrate, but chunked)
 *
 * Usage:
 *   float progress = 0.0f;
 *   Obj* result = transmigrate_incremental(root, src, dest, 100, &progress);
 *   // progress is now 0.0 to 1.0 indicating how much of the graph was processed
 */
void* transmigrate_incremental(void* root, Region* src_region, Region* dest_region,
                                size_t chunk_size, float* progress_out);

/* ============================================================================
 * PHASE 33.4: Batch Allocator for Linked List Transmigration
 * ============================================================================
 * Public interface for batch allocation of pairs during transmigration.
 *
 * This reduces per-node allocation overhead by allocating pairs in batches
 * from the arena instead of calling region_alloc for each pair.
 */

/* PairBatchAllocator - Batches pair allocations for performance
 *
 * Instead of calling region_alloc for each pair (expensive), we allocate
 * batches of 64 Obj structures at once from the arena and carve them up.
 * This reduces allocation overhead by ~2-3x for large lists.
 */
typedef struct PairBatchAllocator {
    Obj* batch;         /* Batch buffer */
    size_t capacity;    /* Number of Obj slots in batch */
    size_t used;        /* Number of slots already used */
} PairBatchAllocator;

/*
 * TransmigrateCloneCtx - Context passed to CloneFn implementations
 *
 * CloneFn gets a `void* tmp_ctx`. For CTRR transmigration, we use it to pass
 * optional performance helpers (like pair batch allocation) without relying
 * on internal `transmigrate.c` struct layouts.
 *
 * IMPORTANT: Keep this struct stable. It is part of the runtime-internal ABI
 * between transmigrate.c and region_metadata.c.
 */
typedef struct TransmigrateCloneCtx {
    PairBatchAllocator* pair_batch; /* Optional: batch allocator for TAG_PAIR */
} TransmigrateCloneCtx;

/*
 * pair_batch_alloc - Allocate an Obj from the batch allocator
 *
 * @param batch: The batch allocator (owned by transmigrate.c, lives for the call)
 * @param dest: Destination region (batch memory is allocated here)
 * @return: Pointer to allocated Obj, or NULL on failure
 *
 * Note: This function is called internally by clone_pair during transmigration.
 * The batch allocator automatically allocates new batches from the destination region
 * when needed (CTRR soundness requirement).
 */
Obj* pair_batch_alloc(PairBatchAllocator* batch, Region* dest);

#endif // OMNI_TRANSMIGRATE_H
