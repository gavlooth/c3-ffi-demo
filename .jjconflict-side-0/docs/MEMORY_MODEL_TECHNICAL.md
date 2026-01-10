# Memory Model Technical Reference (CTRR + Region Control Blocks)

> **Normative spec:** `docs/CTRR.md`  
> **Transmigration spec (detailed):** `runtime/docs/CTRR_TRANSMIGRATION.md`

## 1. Object Layout

OmniLisp objects use a header-light `Obj` structure. The primary reference counting and lifecycle management are handled by the **Region Control Block (RCB)**, though objects retain minimal metadata for hybrid operations and IPGE safety.

```c
typedef struct Obj {
    Generation generation;      // IPGE generation ID for memory safety (Use-After-Free detection)
    int mark;                   // General-purpose mark bit (Scanner/Debug)
    int tag;                    // Type tag (ObjTag enum)
    int is_pair;                // Optimization flag for cons cells
    int scc_id;                 // SCC identifier for cycle detection (-1 = none)
    unsigned int scan_tag : 31; // Traversal marker
    unsigned int tethered : 1;  // Scope tethering bit (Vale-style borrowing)
    union {
        long i;                 // Integers / Char codepoints
        double f;               // Floats
        struct { struct Obj *a, *b; }; // Pairs (car/cdr)
        void* ptr;              // Strings, Symbols, External resources
    };
} Obj;
```

### Tagged Pointers (Immediates)
To reduce heap pressure, small values are encoded directly in the pointer (NaN-boxing style or Tagged Pointer style).
*   **Integers**: 61-bit signed.
*   **Chars**: 21-bit Unicode.
*   **Booleans**: `OMNI_TRUE` / `OMNI_FALSE`.

## 2. Region Control Block (RCB)

The `Region` is the unit of memory management.

```c
typedef struct Region {
    Arena arena;                // Physical storage (Bump Pointer Allocator)
    int external_rc;            // Atomic: Strong references from OTHER regions
    int tether_count;           // Atomic: Temporary "borrows" by active threads
    bool scope_alive;           // Static liveness signal (from compiler analysis)
} Region;
```

> Note: this is a simplified view of the runtime `Region` control block. The
> implementation also contains optimization fields (inline buffers, region IDs,
> thread-local fast paths, and per-region type metadata). See
> `runtime/src/memory/region_core.h`.

## 3. Allocation Strategy

1.  **Stack Allocation (`ESCAPE_TARGET_NONE`)**:
    *   Variables analyzed as strictly local (non-escaping) are allocated on the C stack using `Obj` structs directly.
    *   Macros: `STACK_INT`, `STACK_CELL`.
    *   Cost: Zero heap overhead.

2.  **Region Allocation (`ESCAPE_TARGET_PARENT` / `RETURN`)**:
    *   Objects escaping the immediate scope but bound to a parent or return path are allocated in a `Region`.
    *   Backend: `tsoding/arena` (Bump Pointer).
    *   Cost: O(1) allocation (pointer increment).

## 4. Concurrency (Tethers)

Threads do not lock individual objects. They **Tether** the entire Region.

*   **`region_tether_start(Region*)`**: Atomically increments `tether_count`. Pins the region in memory, preventing deallocation even if `external_rc` drops to zero.
*   **`region_tether_end(Region*)`**: Decrements `tether_count`. If 0 and `external_rc` is 0, the region is freed.
*   **Thread-Local Cache**: Tethers are cached per-thread to avoid atomic operations for redundant borrows within the same call stack.

## 5. Transmigration (Ownership Transfer)

When an object needs to move between regions (e.g., returning a complex result from a function to its caller), the runtime performs **Transmigration**.

1.  **Deep Copy**: The object graph is traversed.
2.  **Cycle Detection**: A bitmap-based visited set handles cycles during traversal.
3.  **Region Splicing**: If the source region contains *only* the result data (common in pure functions), the underlying Arena blocks are simply unlinked from the Source and linked to the Destination. Cost: O(1).

> Important: CTRR requires transmigration to be **total** over all runtime tags
> (no “unknown tag” shallow-copy fallback) to satisfy the “everything can escape”
> guarantee. The detailed contract and current implementation status live in
> `runtime/docs/CTRR_TRANSMIGRATION.md`.

## 6. Safety (IPGE)

**In-Place Generational Evolution (IPGE)** guarantees memory safety without garbage collection.
*   Every `Obj` has a `generation` ID.
*   Pointers (BorrowedRefs) carry the expected generation.
*   Accessing a freed object (whose slot has been reused) fails the generation check.
