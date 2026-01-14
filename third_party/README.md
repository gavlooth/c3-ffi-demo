# Third-Party Dependencies

This directory contains vendored C libraries used by the OmniLisp runtime.

## Libraries

### arena (Alexey Kutepov)
- **URL**: https://github.com/tsoding/arena
- **License**: MIT
- **Description**: Header-only bump allocator for temporary memory
- **Files**: `arena/arena.h`

## Usage

```c
#include "arena/arena.h"

Arena tmp_arena = {0};
void* ptr = arena_alloc(&tmp_arena, size);
// ... use ptr ...
arena_free(&tmp_arena);  // Free all at once
```

Used in OmniLisp for:
- Transmigration temporary allocations (forwarding tables, work queues)
- Short-lived data structures during region operations
