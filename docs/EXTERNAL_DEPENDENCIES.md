# External Dependencies

This document catalogs third-party libraries and their integration points in OmniLisp.

**Last Updated:** 2026-01-18

---

## Overview

OmniLisp uses minimal external dependencies, preferring self-contained implementations where practical.

| Library | Location | Purpose | Integration Points |
|---------|----------|---------|-------------------|
| Arena Allocator | `third_party/arena/` | Bump allocator for regions | `region_core.c`, `transmigrate.c` |
| VMem Arena | `third_party/arena/` | Virtual memory commit-on-demand | `arena_core.c`, `region_core.h` |

---

## Arena Allocator (`third_party/arena/`)

### Files
- `arena.h` / `arena_config.h` - Malloc-based bump allocator
- `vmem_arena.h` - Virtual memory arena with commit-on-demand

### Key Types
```c
typedef struct Arena {
    ArenaChunk* begin;    // First chunk in linked list
    ArenaChunk* end;      // Current chunk (allocation target)
} Arena;

typedef struct ArenaChunk {
    size_t capacity;      // Total bytes in chunk
    size_t used;          // Bytes allocated so far
    struct ArenaChunk* next;
    char data[];          // Flexible array member
} ArenaChunk;
```

### Integration Points

| File | Function | Usage |
|------|----------|-------|
| `region_core.c` | `region_create()` | Creates arena for each region |
| `region_core.h` | `region_alloc()` | Delegates to `arena_alloc()` |
| `transmigrate.c` | Graph copying | Uses arena for destination allocations |
| `scratch_arena.c` | Temporary allocations | Thread-local double-buffered arenas |

### API Surface
```c
void  arena_init(Arena* a);
void* arena_alloc(Arena* a, size_t size);
void  arena_free(Arena* a);
Arena_Mark arena_mark(Arena* a);
void  arena_rewind(Arena* a, Arena_Mark mark);
```

---

## VMem Arena (`third_party/arena/vmem_arena.h`)

### Purpose
Virtual memory arena that reserves large address space but only commits pages on demand. Enables:
- O(1) allocation (bump pointer)
- No reallocation/copying during growth
- Transparent huge page support (MADV_HUGEPAGE)

### Configuration Macros
```c
#define VMEM_CHUNK_RESERVE   (64 * 1024 * 1024)  // 64MB reserve per chunk
#define VMEM_USE_HUGEPAGES   1                    // Enable huge page hints
#define VMEM_HUGEPAGE_THRESHOLD (2 * 1024 * 1024) // 2MB threshold
```

### Integration Points

| File | Function | Usage |
|------|----------|-------|
| `arena_core.c` | `arena_init_vmem()` | Initialize vmem-backed arena |
| `region_core.h` | `struct Region` | Contains `Arena arena` member |

---

## Removed/Replaced Dependencies

### UTHash (replaced)
- **Original use:** Hash tables for visited sets in transmigration
- **Replaced with:** Bitmap-based cycle detection
- **Reason:** 10-100x performance improvement
- **Location:** `transmigrate.c:147` (comment documents replacement)

### SDS (not used)
- **Status:** Not integrated
- **Alternative:** Custom string handling via `mk_string_cstr_region()`

### Linenoise (not used)
- **Status:** Not integrated
- **Alternative:** Standard input for REPL

### stb_ds (not used)
- **Status:** Not integrated
- **Alternative:** Custom hash map implementation (`hashmap.c`)

---

## Build Integration

Third-party code is compiled directly into the runtime:

```makefile
# In runtime/Makefile
THIRD_PARTY_SRCS = ../third_party/arena/arena.c
```

No separate library linking required - headers are included directly.

---

## CodeGraph Resolution Notes

For static analysis tools (CodeGraph), the following edges may appear unresolved:

1. **Arena functions** - Resolved via `third_party/arena/` includes
2. **Macro-generated calls** - See `MACRO_ARCHITECTURE.md`

To improve CodeGraph resolution:
```bash
# Add third_party to include paths
-I../third_party -I../third_party/arena
```
