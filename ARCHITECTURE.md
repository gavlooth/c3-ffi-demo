# Architecture: OmniLisp Region-RC (RC-G)

OmniLisp uses a **Region-Based Reference Counting (RC-G)** architecture. Memory management is coarse-grained, topology-aware, and strictly **GC-less**, targeting ANSI C99 + POSIX pthreads.

---

## 1. Core Principles

- **Everything is a Region**: Objects belong to logical groups (Regions) sharing a single lifecycle.
- **ASAP (As Static As Possible)**: Compile-time analysis determines where to create and destroy regions.
- **Coarse-Grained RC**: Reference counting applies to the **Region (RCB)**, not individual objects.
- **Thread-Local Tethering**: Safe multi-threaded access via local caches and atomic region-level "pins".
- **No Stop-the-World**: Reclamation is O(1) per block; zero global pauses.

---

## 2. Memory Model Diagram

```text
┌─────────────────────────────────────────────────────────────────┐
│ Thread Local Storage (TLS)                                      │
│ ┌─────────────────────────────────────────────────────────────┐ │
│ │ Tether Cache (MAX=16)                                       │ │
│ │ ├─ [Region A]: Count 3  ───┐                                │ │
│ │ └─ [Region B]: Count 1  ───│──┐                             │ │
│ └────────────────────────────│──│─────────────────────────────┘ │
└──────────────────────────────│──│───────────────────────────────┘
                               │  │
        Refers to/Pins         │  │
                               ▼  ▼
┌─────────────────────────────────────────────────────────────────┐
│ Region Control Block (RCB)                                      │
│ ├─ external_rc:  [Atomic] (Strong refs from other regions/stack)│
│ ├─ tether_count: [Atomic] (Sum of all active thread borrows)    │
│ ├─ scope_alive:  [Bool]   (Static liveness signal from ASAP)    │
│ └─ bitmap:       [Opt]    (Cycle detection during move)         │
└──────────────────────────┬──────────────────────────────────────┘
                           │
                           │ owns (Physical Storage)
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│ Arena (Linked Physical Blocks)                                  │
│ ┌──────────────┐      ┌──────────────┐      ┌──────────────┐    │
│ │ Block 1      │───▶  │ Block 2      │───▶  │ Block 3 (End)│    │
│ │ [Obj][Obj]   │      │ [Obj][String]│      │ [Free Space] │    │
│ └──────────────┘      └──────────────┘      └──────────────┘    │
└─────────────────────────────────────────────────────────────────┘
             ▲
             │ pointed to by
┌────────────┴────────────────────────────────────────────────────┐
│ RegionRef (Fat Pointer)                                         │
│ ├─ ptr: [Value*] (Direct pointer to data)                       │
│ └─ rcb: [Region*] (Pointer to the lifecycle owner)              │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. Memory Strategy

### A. Scoped Regions (Pure ASAP)
For data that doesn't escape its scope, the compiler inserts static `region_create()` and `region_exit()` calls. The region uses an **Arena Backend** (Bump pointer), making both allocation and deallocation O(1).

### B. Escaping Regions (Region-RC)
If data escapes to the heap or another thread, the Region transitions to reference counting. 
- **`external_rc`**: Tracks strong references to the Region handle.
- **Bulk Deallocation**: When RC hits 0, `tether_count` is 0, and `scope_alive` is false, the entire Arena is freed. This naturally handles internal cycles without a collector.

### C. Advanced Transmigration
When a subset of a region escapes (e.g., a function return), the runtime performs **Adaptive Transmigration**:
- **Iterative Deep Copy**: Uses an explicit worklist (stack-safe) and **Bitmap-based cycle detection** for fast, allocation-free visited checks.
- **Region Splicing**: O(1) movement of individual physical memory blocks between arenas when data is contiguous.
- **Arena Promotion**: Merging the entire physical block chain into the destination region for large results.

### D. Concurrency (Tethers)
Threads do not lock individual objects. They **Tether** the entire Region.
- **Thread-Local Caches**: Threads increment `tether_count` once and cache the reference. Subsequent borrows in the same thread are O(0).
- **Redundant Atomic Elision**: Atomic operations only occur on the first "check-out" and last "check-in" of a region per thread.

---

## 4. Key Components

- **`Value`**: Header-light object (`mark`, `tag`, `type`, `union payload`).
- **`TypeInfo`**: Metadata provider containing the `trace` function for transmigration.
- **`Region`**: Logical lifecycle owner managing RC and Tethers.
- **`Arena`**: Physical storage backend using linked blocks of memory.
- **`RegionRef`**: The primary handle for cross-region data sharing.
