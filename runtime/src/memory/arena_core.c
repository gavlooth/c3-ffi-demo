/*
 * arena_core.c - Arena allocator implementation selection
 *
 * Define OMNI_USE_VMEM_ARENA to use the virtual memory arena.
 * Otherwise, the original malloc-based arena is used.
 */
#ifndef ARENA_IMPLEMENTED
#define ARENA_IMPLEMENTED

#ifdef OMNI_USE_VMEM_ARENA
#define VMEM_ARENA_IMPLEMENTATION
#include "../../../third_party/arena/vmem_arena.h"
#else
#define ARENA_IMPLEMENTATION
#include "../../../third_party/arena/arena.h"
#endif

#endif /* ARENA_IMPLEMENTED */
