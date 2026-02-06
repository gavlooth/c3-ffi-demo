# Arena + Inline Optimization

## Problem
Original design allocated every object individually on the heap via `malloc()`:
- **All data boxed** - even a single `int` got heap-allocated
- **Heap fragmentation** - thousands of small allocations scattered across memory
- **Poor cache locality** - related objects far apart
- **Allocator overhead** - malloc metadata per object

## Solution: Hybrid Approach

### 1. Inline Small Objects (≤16 bytes)
Store primitives and small structs directly in `PoolSlot`:

```c3
struct PoolSlot {
    bool   is_inline;
    void*  heap_ptr;           // large objects (>16 bytes)
    char[16] inline_data;      // small objects (≤16 bytes)
}
```

**Benefits:**
- `int`, `float`, `bool`, small structs → **zero heap allocation**
- Stored inline in the pool's packed array
- Better cache locality

### 2. Arena Allocation for Large Objects (>16 bytes)
Allocate from contiguous memory blocks (arenas):

```c3
const usz ARENA_SIZE = 64 * 1024;  // 64KB per arena

struct ArenaBlock {
    char* data;
    usz   capacity;
    usz   used;
}
```

**Benefits:**
- **Bump allocator** - O(1) allocation, just increment offset
- **Batch free** - entire arena freed at once when region dies
- **Better locality** - objects allocated together live near each other
- **Reduced fragmentation** - one large block instead of thousands of small ones

### 3. Free-List for Reuse
Track freed chunks within arenas for reuse:

```c3
struct FreeListEntry {
    usz offset;  // offset in arena
    usz size;    // size of free chunk
}
```

**How it works:**
1. When object removed → add its arena chunk to free-list
2. Next allocation of similar size? → reuse from free-list
3. No match? → allocate from arena bump allocator

**Benefits:**
- Freed space gets recycled (reduces waste)
- Still O(1) for free-list search (small list)
- Avoids fragmentation until region dies

## Performance Characteristics

| Operation | Before | After |
|-----------|--------|-------|
| Allocate int | `malloc(4)` | inline (0 allocs) |
| Allocate 32-byte struct | `malloc(32)` | arena bump (O(1)) |
| Free object | `free()` | add to free-list (O(1)) |
| Region destroy | N × `free()` | free K arenas (K << N) |

## Trade-offs

**Pros:**
- ✅ 10-100x faster allocation for small objects
- ✅ Better cache performance (packed storage)
- ✅ Reduced fragmentation
- ✅ Simpler teardown (bulk free)

**Cons:**
- ⚠️ Individual free is less efficient (tracked in free-list, not immediately reclaimed)
- ⚠️ Slight memory overhead (free-list tracking)
- ⚠️ More complex implementation

## Summary
**Small objects inline, large objects in arenas, freed chunks tracked for reuse.**

This is the standard approach for high-performance memory systems (see: Lua, V8, jemalloc size classes).
