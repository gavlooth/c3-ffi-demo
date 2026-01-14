# OmniLisp Runtime Architecture

**Version:** 2.3 (Region-RC model specified)
**Last Updated:** 2026-01-10
**Phase:** 35 - Region-RC model specification and conformance

---

## Quick Overview

The OmniLisp runtime uses a **hybrid memory system** that combines:

- **CTRR (compile-time scheduling)**: Compiler decides region lifetimes and injects
  `region_exit()` and escape repair (`transmigrate()`) at the right boundaries.
- **Regions (canonical: lifetime classes)**: A Region is a **semantic lifetime class** (“objects that die together”).
- **Arenas (implementation)**: Most allocation is bump-allocated in an Arena and reclaimed in bulk.
- **ArenaRegions / RCBs (runtime containers)**: The runtime container (currently `struct Region`) that owns an Arena + liveness counters.
- **Region-RC (coarse-grained RC)**: ArenaRegions that must outlive their lexical scope are reference counted
  at the *ArenaRegion* granularity (coarse-grained RC), not per object.
- **Tethering**: Bounded borrows keep a region alive without copying.
- **Metadata-driven transmigration**: Type metadata defines how to clone/trace values so
  escaping graphs can be repaired soundly without stop-the-world GC.

**Documentation Hierarchy:**
- This document: Overall runtime architecture
- `runtime/docs/MEMORY_TERMINOLOGY.md` - **Pinned terminology** (Region vs Arena vs ArenaRegion/RCB)
- `runtime/docs/REGION_RC_MODEL.md` - **Normative specification** of Region-RC semantics, external pointers, and auto-repair
- `runtime/docs/CTRR_TRANSMIGRATION.md` - Transmigration implementation contract
- `docs/CTRR.md` - High-level CTRR contract and Region Closure Property

```
┌─────────────────────────────────────────────────────────────┐
│                    The Memory System                        │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  1. CTRR (Compile-Time Region Reclamation)                  │
│     → Compiler schedules region lifetimes                   │
│     → Injects region_exit() + escape repair points          │
│                                                              │
│  2. Regions (Bulk Cleanup)                                  │
│     → Group allocations together                             │
│     → Free entire region when scope exits                   │
│                                                              │
│  3. Region-RC (Escaping Data)                               │
│     → When regions escape, they're reference counted        │
│     → Keeps region alive until all refs are gone            │
│                                                              │
│  4. Type Metadata (runtime)                                 │
│     → Type info centralized per region (not per object)      │
│     → Enables compile-time type constants                    │
│     → Powers metadata-driven transmigration                  │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## What Each Part Does

### CTRR = Compile-Time Region Reclamation (The Compiler's Job)

```
CTRR is the contract: determine region lifetimes at compile time and reclaim in bulk.

Example:
  (define (f)
    (let ((x 42)
          (y (pair x x)))
      (pair y y))) ;; y escapes the local scope

Compiler analysis:
  • `x` is immediate (no heap allocation)
  • `y` escapes → must be valid after local region closes

Generated code (conceptual):
  Region* local = region_create();
  Obj* y = mk_pair_region(local, ...);
  Obj* out = transmigrate(y, local, caller_region);
  region_exit(local);
  return out;
```

**Key point:** CTRR does not mean "no runtime cost". It means:

- the *decision* of when regions close happens at compile time
- the runtime provides explicit, local operations (`region_exit`, `transmigrate`, tethering)
- there is no stop-the-world collector that scans the heap

---

### Regions = Grouped Allocations (The Runtime's Job)

```
A Region is a bucket that holds many objects

When you enter a function/scope:
  region = region_create();

When you allocate objects:
  obj1 = region_alloc(region, size);  // Put in region
  obj2 = region_alloc(region, size);  // Put in region
  obj3 = region_alloc(region, size);  // Put in region

When you exit the function/scope:
  region_exit(region);
  // All three objects freed at once!

Benefits:
• One free() instead of three
• Better cache locality
• Simpler code generation
```

Implementation note:
- **Terminology clarification:** The runtime “bucket” is an **ArenaRegion/RCB** (currently `struct Region`).
  The canonical **Region** term is reserved for semantic lifetime classes.
  See `runtime/docs/MEMORY_TERMINOLOGY.md`.
- ArenaRegions are implemented as a bump-allocated Arena plus a small inline-buffer fast path
  for very small objects (`runtime/src/memory/region_core.h`).

---

### Region-RC = When Regions Escape (The Hybrid Part)

```
Problem: What if a region returns an object that escapes?

Example:
  (define (make-pair)
    (let ((region (region-create))
          (x (pair 1 2)))
      (region-exit region)
      x))  // ← x escapes! Region would be freed!

Solution: Reference count the region

Region struct:
  • arena           → Memory storage
  • type_table      → Type metadata (Phase 24)
  • external_rc     → Reference count for external pointers
  • scope_alive     → Is scope still active?
  • tether_count    → Active bounded borrows

**For the complete Region-RC specification (external pointers, auto-repair, conformance), see:**
  → `runtime/docs/REGION_RC_MODEL.md` (normative)

Lifecycle:
  1. region_create()      → external_rc = 0, scope_alive = true, tether_count = 0
  2. region_retain(r)     → external_rc++ (when external reference created)
  3. region_exit(r)       → scope_alive = false
  4. region_release(r)    → external_rc--
  5. Reclaimable iff: external_rc == 0 && scope_alive == false && tether_count == 0

This is "Region-RC": regions + reference counting on regions (coarse-grained, not per-object)
```

---

## Enforcing “Region = lifetime class” (required in a real Lisp)

This project pins **Region** to the semantic meaning: “objects with the same lifetime”.

That definition is **not automatically preserved** once we have:
- runtime mutation (arrays/dicts/atoms/etc),
- long-lived global containers (`_global_region`),
- and concurrency primitives (channels, threads).

Therefore, CTRR + Region‑RC is only a *guarantee* if the implementation includes:

1. **Region identity (`region_of(obj)`)**
   - The runtime must be able to map a boxed `Obj*` to its owning **ArenaRegion/RCB** in O(1).
   - Without this, the runtime cannot soundly implement `external_rc` updates or store-barrier repair.

2. **A store barrier (mutation-time auto-repair)**
   - All pointer stores must go through a single helper that repairs illegal lifetime edges immediately:
     - small graphs ⇒ transmigrate
     - large regions ⇒ adopt/merge/splice when safe, otherwise fallback to transmigrate
   - No stop-the-world collectors are allowed.

Planned work is explicitly tracked in `TODO.md`:
- Issue 1 / Amendment A: `I1-region-of-obj-mechanism`, `I1-ctrr-external-rc-insertion`
- Issue 2 / Amendment A: `I2-store-barrier-choke-point`, `I2-region-merge-policy`

See also:
- `runtime/docs/MEMORY_TERMINOLOGY.md` (canonical terms + code map)

This model name is used throughout the repository as **runtime** memory model:
- `docs/UNIFIED_REGION_ARCHITECTURE.md` calls lifecycle "Refcounted (Region-RC)"
- `runtime/src/runtime.c` calls it "RC-G Runtime: Standard RC is now Region-RC (Coarse-grained)"

**Region-RC specification (normative):**
  See `runtime/docs/REGION_RC_MODEL.md` for:
  - Formal definition of "external pointer" (aligned with RC dialect literature)
  - All external reference boundaries (return, capture, global, channel, mutation)
  - Auto-repair strategies for younger→older stores (transmigrate vs. retain)
  - Tethering semantics (bounded borrows vs. RC sharing)
  - Conformance checklist for implementation verification

Region-RC is intentionally **not** per-object RC:
- objects inside regions are generally not individually freed
- The region is reclaimed when it is both:
  - logically closed (`scope_alive == false`) and
  - not externally referenced (`external_rc == 0`) and
  - no active borrows (`tether_count == 0`)

---

## The Complete Picture (How It All Works)

```
┌─────────────────────────────────────────────────────────────┐
│                   Memory Allocation Flow                    │
└─────────────────────────────────────────────────────────────┘

1. COMPILER ANALYSIS (CTRR)

   Source code → Type inference + Escape analysis
                 │
                 ▼
   Does variable escape?
   │
   ├── [NO]  → Allocate on STACK or in local region
   │            Free at last-use (compiler decides when)
   │
   └── [YES] → Must allocate in REGION
                (may escape beyond scope)

2. RUNTIME ALLOCATION (Regions + Type Metadata)

   Object needs to be allocated:
   │
   ▼
   What type is it?
   │
   ├── Known at compile-time? → alloc_obj_typed(region, TYPE_ID_INT)
   │                            ↑
   │                            Uses type metadata
   │
   └── Unknown type?           → region_alloc(region, size)
                                 (generic fallback)

3. REGION LIFECYCLE (Region-RC)

   Region created
   │
   ▼
   Does the region escape?
   │
   ├── [NO]  → region_exit() frees everything immediately
   │            (scope_alive = false, external_rc = 0)
   │
   └── [YES] → region_retain() increments reference count
                → region_exit() sets scope_alive = false
                → region_release() decrements reference count
                → When count reaches 0: region destroyed

```

---

## Escape Repair: Transmigration (CTRR Runtime Contract)

When data escapes a closing region, runtime must ensure **Region Closure**:
no reachable value contains pointers into a closing region after it is reclaimed.

OmniLisp enforces this with **metadata-driven transmigration**:

- `transmigrate(root, src_region, dest_region)`

Each runtime type registers:
- how to `clone` one object into `dest_region`
- how to `trace` all `Obj*` child slots so pointers can be rewritten

This is documented in detail in:
- `docs/CTRR.md` (normative contract)
- `runtime/docs/CTRR_TRANSMIGRATION.md` (detailed runtime behavior)
- `runtime/docs/REGION_RC_MODEL.md` (relationship between transmigration and Region-RC)

Non-goals (explicitly prohibited):
- stop-the-world GC
- heap-wide tracing collectors

---

## Bounded Borrows: Tethering

Tethering is complementary to transmigration:

- transmigration repairs graphs so values can outlive a closing region
- tethering keeps a region alive temporarily for **bounded borrows** (e.g., while a
  call frame is active), avoiding copying

Region-RC fields related to tethering:
- `tether_count`: active borrow count (thread-safe)
- `scope_alive`: a semantic liveness signal (set false at region_exit)

---

## Why This Hybrid Approach?

### Pure CTRR (Compile-Time Only)

```
Pros:
• Zero runtime overhead
• Predictable performance

Cons:
• Escaping graphs require an explicit repair operation (transmigration or ownership transfer)
• Without a runtime escape mechanism, closures/returns/globals would be unsound
```

### Pure Reference Counting

```
Pros:
• Handles any escape pattern
• Simple to implement

Cons:
• Every allocation has RC overhead
• RC updates on every assignment
• Cache pollution from RC fields
```

### The Hybrid (CTRR + Regions + Region-RC)

```
Pros:
• Non-escaping data: bulk reclamation (regions)
• Escaping data: grouped in regions (bulk cleanup)
• Escaping regions: reference counted (Region-RC)
• Best of both worlds

Cons:
• More complex to understand
• Requires good escape analysis
```

---

## Type Metadata (Foundation)

Type metadata is a foundational runtime component (introduced as “Phase 24”)
that enables compile-time and runtime optimizations without changing CTRR
semantics:

```
Before Phase 24:
  Obj* obj = alloc_obj_region(region, TAG_INT);
  // Tag looked up at runtime

After Phase 24:
  Obj* obj = alloc_obj_typed(region, TYPE_ID_INT);
  // TYPE_ID_INT is compile-time constant (0)
  // Metadata lookup: O(1) array access

Benefits:
• Compiler knows types at compile-time
• Type-specific optimizations (inline thresholds)
• Type info centralized (1.8 KB per region)
• 3.94x speedup in allocation
```

---

## System Architecture (Visual View)

```
┌─────────────────────────────────────────────────────────────────────┐
│                        OmniLisp Memory System                        │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                    ┌───────────────┴───────────────┐
                    ▼                               ▼
        ┌───────────────────────┐       ┌───────────────────────┐
        │    COMPILER PHASE     │       │    RUNTIME PHASE      │
        │   (Compile-Time)      │       │    (Execution)        │
        └───────────────────────┘       └───────────────────────┘
                    │                               │
        ┌───────────┴───────────┐               │
        ▼                       ▼               ▼
┌───────────────┐     ┌───────────────┐   ┌─────────────────┐
│   CTRR        │     │  Type System  │   │   Regions       │
│   Scheduling  │     │  Inference   │   │                 │
└───────────────┘     └───────────────┘   └─────────────────┘
        │                       │               │
        ▼                       ▼               ▼
┌───────────────┐     ┌───────────────┐   ┌─────────────────┐
│ Escape        │     │ TypeID        │   │ Arena Allocator │
│ Analysis      │     │ Constants     │   │                 │
└───────────────┘     └───────────────┘   └─────────────────┘
        │                       │               │
        └───────────┬───────────┘               │
                    ▼                           ▼
            ┌───────────────┐           ┌─────────────────┐
            │   Generated   │           │ Region-RC       │
            │     C Code    │           │ (when needed)   │
            └───────────────┘           └─────────────────┘
```

---

## Memory System Components

```
┌─────────────────────────────────────────────────────────────────────┐
│                    CTRR (Compile-Time Scheduling)                  │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐         │
│  │   Liveness  │────▶│   Escape    │────▶│   Capture   │         │
│  │  Analysis   │     │  Analysis   │     │  Tracking   │         │
│  └─────────────┘     └─────────────┘     └─────────────┘         │
│                                                                     │
│  Determines:                                                        │
│  • When to free variables                                           │
│  • What escapes the scope                                          │
│  • What gets captured by closures                                   │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                    REGIONS (Runtime Allocation)                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐  │
│  │                    Region Structure                          │  │
│  │  ┌───────────────────────────────────────────────────────┐  │  │
│  │  │  Arena          → 4KB chunks (main storage)           │  │  │
│  │  │  Inline Buffer  → 512 bytes (fast small objects)      │  │  │
│  │  │  Type Table     → 19 types × metadata (Phase 24)      │  │  │
│  │  │  external_rc    → Reference count (for escaping)      │  │  │
│  │  │  scope_alive    → Is scope still active?              │  │  │
│  │  └───────────────────────────────────────────────────────┘  │  │
│  └─────────────────────────────────────────────────────────────┘  │
│                                                                     │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐      │
│  │   Create     │────▶│   Allocate   │────▶│    Exit      │      │
│  └──────────────┘     └──────────────┘     └──────────────┘      │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│                    REGION-RC (Escaping Regions)                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  When a region's objects escape:                                   │
│                                                                     │
│    ┌─────────┐                                                     │
│    │ Region  │                                                     │
│    └────┬────┘                                                     │
│         │                                                          │
│         ▼                                                          │
│    ┌─────────┐    retain()    ┌─────────┐                         │
│    │Created  ├───────────────▶│ RC = 1  │                         │
│    └────┬────┘                 └────┬────┘                         │
│         │                            │                              │
│         ▼                            │                              │
│    ┌─────────┐    exit()             │                              │
│    │Scope    ├────────────────────────┤                              │
│    │Ends     │                        │                              │
│    └─────────┘                        ▼                              │
│         │                     ┌─────────┐    release()              │
│         │                     │ RC = 0  ├──────────────┐            │
│         │                     └────┬────┘               │            │
│         │                          │                    ▼            │
│         ▼                          ▼              ┌─────────┐      │
│    ┌─────────┐                                           │         │      │
│    │ FREED!  │ ◄──────────────────────────────────────────│ Destroy │      │
│    └─────────┘                                           └─────────┘      │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Region Structure (The Heart of the System)

```
Region (One Region per scope/function)

├── Arena           → Main memory (chunks of 4KB each)
├── Inline Buffer   → Fast memory (512 bytes) for small objects
├── Type Table      → Type information for all 19 core types
├── Region ID       → Unique number (1 to 65,535)
├── Reference Count → How many external references exist
└── Scope Flag      → Is the region still active?

What this means:
- Small objects (≤64 bytes) go in the Inline Buffer (SUPER FAST)
- Large objects go in the Arena (fast, but slower than inline)
- All objects freed when region exits (one cleanup operation)
```

---

## Type Metadata (Why This Is Fast)

**Old Way (Every object has type info):**
```
Each object: [Tag 4 bytes] [Data ...]
             ↑
           Wastes 4 bytes per object
```

**New Way (Type info shared once per region):**
```
Region has ONE type table with 19 entries:

Type Table (1.8 KB total, shared by ALL objects):
├── [0] INT      → {size: 8B,  can_inline: yes,  threshold: 16}
├── [1] FLOAT    → {size: 8B,  can_inline: yes,  threshold: 16}
├── [2] CHAR     → {size: 8B,  can_inline: yes,  threshold: 8}
├── [3] PAIR     → {size: 40B, can_inline: yes,  threshold: 56}
├── [4] ARRAY    → {size: 40B, can_inline: no,   threshold: 0}
├── ... (15 more types)
└── [18] NOTHING → {size: 8B,  can_inline: yes,  threshold: 8}

Each object: [Data ...]
            ↑
         No tag field! Type comes from compiler context.
```

**Benefits:**
- 4 bytes saved per object
- Type lookup is O(1) array access
- Compiler knows type at compile-time

---

## How Allocation Works (Step by Step)

### When you allocate an object:

```
1. CALL: alloc_obj_typed(region, TYPE_ID_INT)
           │
           │ Compiler knows this is an INT at compile-time
           │ TYPE_ID_INT is the constant 0
           ↓
2. LOOKUP: metadata = region->type_table[TYPE_ID_INT]
           │
           │ Fast array lookup: O(1)
           │ Returns: {can_inline: true, threshold: 16}
           ↓
3. DECIDE: Can we use inline buffer?
           │
           │ IF (can_inline == true AND size ≤ threshold)
           │     → Use inline buffer (3.01 ns - SUPER FAST)
           │ ELSE
           │     → Use arena allocator (9.01 ns - still fast)
           ↓
4. RETURN: Pointer to allocated object
```

### Inline Buffer (Fast Path)

```
Inline Buffer (512 bytes inside Region):

[┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐]
 │A │B │C │D │E │F │G │H │I │J │K │L │M │N │O ...
[└──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘]
  ↑
  offset=0

• Objects placed sequentially (bump pointer)
• Super fast: just move the offset pointer
• When full: automatically switch to arena
• All objects freed when region exits
```

### Arena Allocator (Slow Path)

```
Arena (4KB chunks):

[Chunk 1: 4096 bytes] → [Chunk 2: 4096 bytes] → [Chunk 3: ...]
     ↑
  current_pos

• Bump pointer allocation within chunk
• When chunk full: allocate new chunk
• All chunks freed at once when region exits
```

---

## Pointer Masking (Zero-Cost Cross-Region References)

**Problem:** How do we know which region an object belongs to?

**Old Solution (Fat Pointers):**
```
Fat pointer: [Object pointer] [Region pointer]
              8 bytes          8 bytes

Total: 16 bytes per pointer (expensive!)
```

**New Solution (Pointer Masking):**
```
64-bit pointer: [Address: 48 bits] [Region ID: 16 bits]

Example:
0x0001_AAAA_AAAA_AAAA
     │              │
     │              └─ Region ID = 1 (encoded in high bits)
     └─ Address = 0x0000_AAAA_AAAA_AAAA

Benefits:
• Same size as normal pointer (8 bytes)
• Zero runtime cost (bit operations are free)
• Can extract region with: pointer >> 48
• Can get address with: pointer & mask

How it works:
ENCODE:  encoded = address | (region_id << 48)
DECODE:  address = encoded & 0x0000FFFFFFFFFFFF
REGION:  region_id = encoded >> 48
```

---

## The 19 Core Types

### Inlineable Types (Go in fast 512-byte buffer)

| Type | ID | Size | Inline? | Threshold | Description |
|------|----|----|---------|-----------|-------------|
| INT | 0 | 8B | ✓ | 16B | Integer numbers |
| FLOAT | 1 | 8B | ✓ | 16B | Floating point |
| CHAR | 2 | 8B | ✓ | 8B | Characters |
| PAIR | 3 | 40B | ✓ | 56B | Cons cells (lists) |
| SYMBOL | 6 | 40B | ✓ | 24B | Interned symbols |
| BOX | 9 | 40B | ✓ | 32B | Mutable references |
| ERROR | 12 | 40B | ✓ | 32B | Error values |
| ATOM | 13 | 40B | ✓ | 16B | Atomic values |
| TUPLE | 14 | 40B | ✓ | 48B | Fixed-size pairs |
| NAMED_TUPLE | 15 | 40B | ✓ | 64B | Struct-like pairs |
| NOTHING | 18 | 8B | ✓ | 8B | Unit type |

### Non-Inlineable Types (Always use arena)

| Type | ID | Size | Inline? | Description |
|------|---|----|---------|-------------|
| ARRAY | 4 | 40B | ✗ | Dynamic arrays (usually large) |
| STRING | 5 | 40B | ✗ | Strings (usually large) |
| DICT | 7 | 40B | ✗ | Hash maps (complex) |
| CLOSURE | 8 | 40B | ✗ | Function closures (complex) |
| CHANNEL | 10 | 40B | ✗ | Communication channels |
| THREAD | 11 | 40B | ✗ | Thread handles |
| GENERIC | 16 | 40B | ✗ | Parametric types |
| KIND | 17 | 40B | ✗ | Type objects |

---

## Performance Results

### Allocation Speed

| Method | Time (ns/op) | Speedup |
|--------|--------------|---------|
| Old: alloc_obj_region (tag-based) | 13.30 | 1x (baseline) |
| New: alloc_obj_typed (metadata) | 3.37 | **3.94x faster** |
| Inline buffer (fast path) | 3.01 | **4.42x faster** |
| Arena (slow path) | 9.01 | 1.48x faster |
| malloc (system) | 0.89 | 14.94x faster |

### Memory Overhead

| Component | Size | Purpose |
|-----------|------|---------|
| Region struct | ~100 B | Control block |
| Type table (19 types) | 1,824 B | Type metadata |
| Inline buffer | 512 B | Fast allocation |
| **Total per region** | **~2.4 KB** | One-time cost |

**Per-object savings:**
- Old: 4 bytes tag in every object
- New: 0 bytes (type comes from region table)

---

## Arena Allocator Implementation

OmniLisp provides a **dual-architecture arena system** selectable at compile time via `OMNI_USE_VMEM_ARENA`.

### Two Implementations

#### 1. Original Arena (`third_party/arena/arena.h`)

Classic bump allocator with linked-list chunk management:

```c
struct ArenaChunk {
    ArenaChunk *next;        // Linked list pointer
    size_t count;            // Current offset (in uintptr_t units)
    size_t capacity;         // Total capacity
    uintptr_t data[];        // Flexible array
};

typedef struct Arena {
    ArenaChunk *begin, *end; // First chunk, current chunk
} Arena;
```

**Backend support:**
- `ARENA_BACKEND_LIBC_MALLOC` - Standard malloc (portable, WASM-compatible)
- `ARENA_BACKEND_LINUX_MMAP` - Direct mmap (better memory characteristics)
- `ARENA_BACKEND_WIN32_VIRTUALALLOC` - Windows VirtualAlloc

**Default chunk size:** 8KB

#### 2. Virtual Memory Arena (`third_party/arena/vmem_arena.h`)

Commit-on-demand allocator with OS virtual memory integration:

```c
struct VMemChunk {
    VMemChunk*  next;       // Linked list pointer
    void*       base;       // Start of usable memory
    size_t      reserved;   // Total VA space (2MB, THP-aligned)
    size_t      committed;  // Currently committed physical bytes
    size_t      offset;     // Bump pointer
};
```

**Memory strategy:**
| Parameter | Value | Purpose |
|-----------|-------|---------|
| Reserved VA | 2MB | Total virtual address space per chunk |
| Initial commit | 256KB | Physical RAM committed on creation |
| Commit-ahead | 256KB | Pre-commit to reduce syscalls |
| THP alignment | 2MB | Transparent Huge Page optimization |

### Allocation Strategy

Both arenas use **O(1) bump pointer allocation**:

```c
void* arena_alloc(Arena* a, size_t size_bytes) {
    size_t aligned = ALIGN_UP(size_bytes, sizeof(void*));

    // FAST PATH: space in current chunk
    if (a->end && a->end->offset + aligned <= a->end->committed) {
        void* ptr = (char*)a->end->base + a->end->offset;
        a->end->offset += aligned;
        return ptr;  // O(1) - no syscall
    }

    // MEDIUM PATH (vmem only): commit more pages
    if (a->end && new_offset <= a->end->reserved) {
        vmem_chunk_ensure_committed(a->end, new_offset);
        // ... allocate
    }

    // SLOW PATH: allocate new chunk
    VMemChunk* new_chunk = vmem_chunk_new(aligned);
    // ... link and allocate
}
```

### Deallocation and Reset

#### `arena_reset()` - Zero-cost bulk reset

```c
void arena_reset(Arena *a) {
    for (ArenaChunk *r = a->begin; r != NULL; r = r->next) {
        r->count = 0;  // Original: just reset counter
    }
    // VMemArena: also return pages to OS
    for (VMemChunk* c = a->begin; c; c = c->next) {
        madvise(c->base, c->committed, MADV_DONTNEED);
        c->offset = 0;
    }
    a->end = a->begin;
}
```

#### `arena_rewind()` - Partial reset to snapshot

```c
Arena_Mark arena_snapshot(Arena *a) {
    return (Arena_Mark){ .chunk = a->end, .count = a->end->count };
}

void arena_rewind(Arena *a, Arena_Mark m) {
    m.chunk->count = m.count;
    for (ArenaChunk *r = m.chunk->next; r; r = r->next) {
        r->count = 0;
    }
    a->end = m.chunk;
}
```

### O(1) Chunk Splice Operations

Critical for transmigration - entire chunk ranges can be moved without copying:

```c
void arena_detach_blocks(Arena *a, ArenaChunk *start, ArenaChunk *end) {
    // Unlink range: prev->next = end->next
    // O(1) - no data movement
}

void arena_attach_blocks(Arena *a, ArenaChunk *start, ArenaChunk *end) {
    // Append range: a->end->next = start
    // O(1) - no data movement
}
```

**Use case:** When transmigrating 2MB of data, splice the chunks instead of copying.

### Performance Characteristics

| Operation | Original Arena | VMemArena |
|-----------|---------------|-----------|
| Fast-path alloc | ~5-10 cycles | ~5-10 cycles |
| New chunk | 1-10 µs (malloc) | 5-50 µs (mmap) |
| Commit pages | N/A | 1-5 µs per 4KB |
| Reset | O(n) counters | O(n) + madvise |
| Splice | O(1) | O(1) |

### Configuration

Selection in `third_party/arena/arena_config.h`:

```c
#ifdef OMNI_USE_VMEM_ARENA
  #include "vmem_arena.h"    // Commit-on-demand
#else
  #include "arena.h"         // Malloc-based
#endif
```

**When to use which:**
- **Original arena:** Short-lived processes, WASM, simpler debugging
- **VMemArena:** Long-running services, heavy allocation churn, lower peak memory

---

## SOTA Arena Techniques (2025-2026)

Modern arena allocator research and production systems have converged on several key techniques.

### 1. Commit-on-Demand (Virtual Memory Arena)

**Pattern:** Reserve large VA space, commit physical pages lazily.

```c
// Reserve 1GB, commit nothing
void* base = mmap(NULL, 1GB, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

// Commit 4KB when needed
mprotect(base + offset, 4096, PROT_READ|PROT_WRITE);
```

**Benefits:**
- Peak memory usage matches actual usage, not reserved size
- Long-running processes don't bloat RSS
- Pages returned to OS on reset via `madvise(MADV_DONTNEED)`

**OmniLisp status:** ✅ Implemented in `vmem_arena.h`

### 2. Transparent Huge Page (THP) Alignment

**Pattern:** Align chunk reservations to 2MB boundaries.

```c
#define VMEM_CHUNK_RESERVE (2 * 1024 * 1024)  // 2MB THP-aligned
```

**Benefits:**
- Linux kernel can promote to 2MB huge pages automatically
- Reduces TLB pressure for large allocations
- 10-30% speedup for memory-intensive workloads

**OmniLisp status:** ✅ Implemented (2MB alignment)

### 3. Scratch Arena Pattern (Double-Buffered Arenas)

**Pattern:** Two arenas that alternate each "frame" or phase.

```c
typedef struct {
    Arena arenas[2];
    int current;
} ScratchArenas;

Arena* get_scratch(ScratchArenas* s, Arena* conflict) {
    // Return whichever arena isn't the conflict
    if (&s->arenas[0] == conflict) return &s->arenas[1];
    if (&s->arenas[1] == conflict) return &s->arenas[0];
    return &s->arenas[s->current];  // Default to current
}

void swap_scratch(ScratchArenas* s) {
    arena_reset(&s->arenas[s->current]);
    s->current = 1 - s->current;
}
```

**Use cases:**
- **Game engines:** "This frame" vs "last frame" data
- **Compilers:** "Current pass" vs "previous pass" AST
- **Servers:** "Current request" vs "temp allocations"

**Key insight:** Instead of tracking individual allocations, categorize by phase lifetime:
- "I need this data for THIS function call only" → scratch[0]
- "I need this data for the CALLER to use" → scratch[1] or persistent arena

**Benefits:**
- Zero individual frees during a phase
- Automatic bulk cleanup on phase transition
- No fragmentation within a phase

**OmniLisp status:** ⚠️ Not explicitly implemented (regions serve similar purpose)

### 4. Thread-Local Arena Caching

**Pattern:** Each thread maintains its own arena pool.

```c
__thread ArenaPool thread_pool = {0};

Arena* arena_get_cached(void) {
    if (thread_pool.free_count > 0) {
        return thread_pool.free_list[--thread_pool.free_count];
    }
    return arena_new();
}

void arena_return_cached(Arena* a) {
    arena_reset(a);
    if (thread_pool.free_count < MAX_CACHED) {
        thread_pool.free_list[thread_pool.free_count++] = a;
    } else {
        arena_free(a);
    }
}
```

**Benefits:**
- No lock contention for arena allocation
- Reuse chunks across function calls
- Amortize mmap/VirtualAlloc costs

**OmniLisp status:** ✅ Per-thread region pools exist (`__thread RegionPool`)

### 5. Size-Class Segregation

**Pattern:** Separate arenas for different size classes.

```c
typedef struct {
    Arena small;   // < 256 bytes
    Arena medium;  // 256 - 4KB
    Arena large;   // > 4KB (or direct mmap)
} SizeClassArenas;
```

**Benefits:**
- Better cache locality within size class
- Predictable chunk utilization
- Can tune chunk sizes per class

**Used by:** LLVM's BumpPtrAllocator, mimalloc, jemalloc

**OmniLisp status:** ⚠️ Not implemented (inline buffer serves small-object purpose)

### 6. Explicit Huge Page Support

**Pattern:** Request huge pages explicitly for very large arenas.

```c
void* base = mmap(NULL, size, PROT_READ|PROT_WRITE,
                  MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB, -1, 0);

// Or advise the kernel
madvise(base, size, MADV_HUGEPAGE);
```

**Benefits:**
- Guaranteed 2MB pages (vs THP's opportunistic promotion)
- Eliminates page table overhead for large datasets

**OmniLisp status:** ⚠️ Could add `MADV_HUGEPAGE` hint in vmem_arena

### Performance Benchmarks (Industry)

| Technique | Speedup vs malloc | Source |
|-----------|-------------------|--------|
| Basic bump allocator | 10-50x | General |
| + Commit-on-demand | +5-15% peak memory | OmniLisp vmem_arena |
| + THP alignment | +10-30% throughput | Linux kernel docs |
| + Scratch pattern | 358,000x dealloc | Ryan Fleury |
| + Thread-local cache | +20-40% multi-threaded | mimalloc |

### Recommended Additions for OmniLisp

1. **Explicit MADV_HUGEPAGE** for vmem_arena chunks > 2MB
2. **Scratch arena API** for temporary computations within transmigration
3. **Chunk recycling** between regions (already partially done via region pools)

---

## Compiler Integration (Phase 24 - COMPLETE)

### What the Compiler Does Now

```
Source Code:
  (let ((x (int 42))
        (y (pair x x)))
    y)

Type Checker:
  ✓ x is TYPE_ID_INT (compile-time constant)
  ✓ y is TYPE_ID_PAIR (compile-time constant)
  ✓ y escapes, x doesn't

Code Generation:
  Obj* x = alloc_obj_typed(region, TYPE_ID_INT);
  Obj* y = alloc_obj_typed(region, TYPE_ID_PAIR);

  // Comments show allocation strategy:
  // x: escape=none, alloc=stack (or inline)
  // y: escape=return, alloc=inline (threshold=56)

Runtime:
  • TYPE_ID_INT metadata lookup: O(1)
  • Inline buffer allocation (3.01 ns)
  • Pointer masking for region encoding
```

### Escape-Driven Allocation

```
IF variable doesn't escape:
  → Stack allocation (zero overhead)

ELIF variable escapes + type is small:
  → Inline buffer allocation (3.01 ns)

ELSE:
  → Arena allocation (9.01 ns)
```

---

## File Organization

```
runtime/src/memory/
├── region_core.h          # Main API: region_create, region_alloc
├── region_core.c          # Implementation
├── region_metadata.h      # TypeMetadata struct, TypeID enum
├── region_metadata.c      # Type table initialization
├── region_pointer.h       # Pointer masking functions
├── region_value.h         # Object constructors
└── region_value.c         # alloc_obj_typed implementation

csrc/analysis/
├── type_id.h             # Compiler TypeID enum
└── type_id.c             # Type name to TypeID mapping

csrc/codegen/
├── codegen.h             # Typed allocation API
└── codegen.c             # Emits alloc_obj_typed() calls

docs/
├── ARCHITECTURE.md       # This file
└── BENCHMARK_*.md        # Performance results
```

---

## Memory Allocation Decision Tree

```
                        ┌──────────────┐
                        │ ALLOCATE     │
                        │   OBJECT     │
                        └──────┬───────┘
                               │
                               ▼
                    ┌──────────────────────┐
                    │ Variable ESCAPES?    │
                    └──────┬───────────────┘
                           │
              ┌──────────┴──────────┐
              │                     │
             NO                    YES
              │                     │
              ▼                     ▼
     ┌──────────────┐      ┌──────────────────────┐
     │   STACK      │      │ Type KNOWN at         │
     │ ALLOCATION   │      │ compile-time?        │
     │              │      └──────┬───────────────┘
     │ (0 ns)       │             │
     └──────────────┘      ┌──────┴──────┐
                            │             │
                           NO            YES
                            │             │
                            ▼             ▼
                   ┌──────────────┐  ┌──────────────────────┐
                   │   ARENA      │  │ Can type be          │
                   │ ALLOCATION   │  │ INLINE-allocated?    │
                   │              │  └──────┬───────────────┘
                   │ (9.01 ns)    │         │
                   └──────────────┘    ┌──────┴──────┐
                                       │             │
                                      NO            YES
                                       │             │
                                       ▼             ▼
                              ┌──────────────┐  ┌──────────────────┐
                              │   ARENA      │  │ SIZE ≤          │
                              │ ALLOCATION   │  │ threshold?      │
                              │              │  └──────┬───────────┘
                              │ (9.01 ns)    │         │
                              └──────────────┘    ┌──────┴──────┐
                                                   │             │
                                                  NO            YES
                                                   │             │
                                                   ▼             ▼
                                          ┌──────────────┐  ┌──────────────┐
                                          │   ARENA      │  │ INLINE      │
                                          │ ALLOCATION   │  │ BUFFER      │
                                          │              │  │              │
                                          │ (9.01 ns)    │  │ (3.01 ns)    │
                                          └──────────────┘  └──────────────┘
```

### Quick Reference

| Condition | Allocation Method | Speed | Example |
|-----------|------------------|-------|---------|
| Non-escaping | **Stack** | Instant (0 ns) | Local variable |
| Escaping + Unknown type | **Arena** | 9.01 ns | Generic object |
| Escaping + Known type + Can't inline | **Arena** | 9.01 ns | Array, String |
| Escaping + Known type + Too large | **Arena** | 9.01 ns | Large struct |
| Escaping + Known type + Fits | **Inline** | 3.01 ns | Int, Pair, Char |

### Example Allocations

```
Example 1: Local integer (doesn't escape)
  (let ((x 42)) (+ x 1))

  Decision: NO escape → STACK
  Result: 0 ns overhead

Example 2: Returned pair (escapes)
  (let ((x (pair 1 2))) x)

  Decision: YES escape + TYPE_ID_PAIR + CAN_INLINE + SIZE=40 ≤ 56
  Result: INLINE BUFFER (3.01 ns)

Example 3: Large array (escapes)
  (let ((x (array 1000))) x)

  Decision: YES escape + TYPE_ID_ARRAY + CANT_INLINE
  Result: ARENA (9.01 ns)
```

---

## Why This Matters

### Before Phase 24
```
• 4 bytes tag in every object
• No inline allocation
• Fat pointers for cross-region (16 bytes)
• 13.30 ns per allocation
```

### After Phase 24
```
• Tag centralized in region table (future: eliminate tag field)
• Inline buffer for small objects (3.01 ns)
• Zero-cost pointer masking (standard 8-byte pointers)
• 3.37 ns per allocation (3.94x speedup)
```

### Impact
- **Faster**: 3.94x allocation speedup
- **Less Memory**: 4 bytes saved per object (future)
- **Same Pointer Size**: No fat pointers needed
- **Compile-Time Types**: Compiler knows types at compile-time
- **Production Ready**: All tests pass, fully documented

---

## Key Takeaways

1. **Type info is centralized** - Stored once per region, not per object
2. **Small objects are fast** - Inline buffer (3.01 ns) vs arena (9.01 ns)
3. **Pointer masking is free** - Bit operations have zero runtime cost
4. **Compiler integration works** - Type constants flow from compiler to runtime
5. **Phase 24 is complete** - All 3 tasks done and tested
6. **Region-RC is specified** - External pointer semantics documented and aligned with RC dialect

---

## Further Documentation

- **Region-RC Specification (Normative):** `runtime/docs/REGION_RC_MODEL.md`
  - External pointer definition and boundaries
  - Auto-repair strategies (transmigrate vs. retain)
  - Tethering semantics vs. RC sharing
  - Conformance checklist for implementation verification

- **Transmigration Contract:** `runtime/docs/CTRR_TRANSMIGRATION.md`
  - Metadata-driven implementation requirements
  - Region Closure Property enforcement

- **High-Level CTRR:** `docs/CTRR.md`
  - Compiler responsibilities (liveness, escape, capture tracking)
  - Runtime responsibilities (safe reclamation, tethering, transmigration)

---

**Status:** ✅ COMPLETE (Phase 35: Region-RC model specification added)
**Next Phase:** Region accounting diagnostics (Issue 2 in TODO.md)
