/*
 * arena_config.h - Arena allocator selection
 *
 * Define OMNI_USE_VMEM_ARENA before including to use the virtual memory
 * based arena allocator. Otherwise, the original malloc-based arena is used.
 *
 * Example:
 *   #define OMNI_USE_VMEM_ARENA
 *   #include "arena_config.h"
 *
 * Both implementations provide the same API:
 *   - Arena, ArenaChunk types
 *   - arena_alloc, arena_free, arena_reset
 *   - arena_snapshot, arena_rewind
 *   - arena_detach_blocks, arena_attach_blocks (O(1) splice)
 */

#ifndef ARENA_CONFIG_H_
#define ARENA_CONFIG_H_

#ifdef OMNI_USE_VMEM_ARENA
/*
 * VMemArena - Virtual memory based allocator
 *
 * Advantages:
 *   - O(1) allocation via bump pointer
 *   - O(1) splice for region merging
 *   - Commit-on-demand (no wasted RAM)
 *   - madvise returns pages to OS on reset
 *   - No malloc heap fragmentation
 *   - 2MB chunks for THP alignment
 *
 * Best for:
 *   - Memory-constrained systems
 *   - Long-running processes
 *   - Heavy region transmigration
 */
#include "vmem_arena.h"

#else
/*
 * Original Arena - malloc-based allocator (tsoding)
 *
 * Advantages:
 *   - Slightly faster small allocations (glibc optimization)
 *   - Simpler implementation
 *   - Works on all platforms including WASM
 *
 * Best for:
 *   - Short-lived processes
 *   - WASM targets
 *   - Maximum allocation throughput
 */
#include "arena.h"

#endif /* OMNI_USE_VMEM_ARENA */

#endif /* ARENA_CONFIG_H_ */
