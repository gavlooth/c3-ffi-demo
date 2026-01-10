# Advanced Region Algorithms: Transmigration & Tethering

This document outlines advanced algorithms to replace the current "naive" implementations of Region Transmigration and Tethering in the OmniLisp runtime.

## 1. Current "Naive" Implementation & Limitations

### 1.1 Transmigration (`transmigrate.c`)
- **Recursive Deep Copy:** Subject to stack overflow on deep graphs.
- **Hash Map Cycle Detection:** Uses `uthash` with `malloc` per entry; slow and fragments memory.
- **Switch-Based Traversal:** Hardcoded types make it difficult to extend and slow due to branching.
- **All-or-Nothing Promotion:** `arena_promote` merges the entire source arena into the destination, potentially inheriting massive amounts of garbage.

### 1.2 Tethering (`region_core.c`)
- **Global Atomic Counter:** `tether_count` is a single atomic variable per region. In multi-threaded workloads, this becomes a cache-contention bottleneck (False Sharing).
- **Coarse-Grained Lifecycle:** A single tether keeps the *entire* region alive, even if only one small object is being accessed.

---

## 2. Advanced Transmigration Algorithms

### 2.1 Iterative Worklist Copying
To avoid stack overflow and improve cache locality:
- **Algorithm:** Use an explicit stack/queue (worklist) for traversal.
- **Optimization:** For `T_CELL` (linked lists), use a specialized loop to handle the `cdr` tail-recursively without adding to the worklist.

### 2.2 Bitmap-Based Cycle Detection (The "Region Shadow")
Since regions are contiguous memory blocks, we can replace the hash map:
- **Mechanism:** For the source region being transmigrated, allocate a bit-array where 1 bit corresponds to 1 word (or 1 minimum object size).
- **Benefit:** O(1) lookup and mark. Zero allocations during traversal. Extremely cache-friendly.
- **Fallback:** Use a small hash map only for pointers *outside* the source region (if any).

### 2.3 Incremental / Background Transmigration
For large objects that exceed the budget but shouldn't trigger full promotion:
- **Ghosting with Progress:** Move the root object and create a **Forwarding Pointer** or **Proxy**.
- **Lazy Evacuation:** When the proxy is accessed, copy that specific subtree.
- **Evacuation Bitmap:** Track which parts of the old region have been evacuated. When the bitmap is full, the old region is finally freed.

### 2.4 Metadata-Driven Traversal (Shape-Based)
Instead of a `switch(tag)`:
- **Concept:** Every object type provides a `Trace` function or a `PointerMap` (bitmask of where pointers are in the struct).
- **Benefit:** Reduces branching and allows the compiler to generate optimized transmigration code for custom types/FFI.

### 2.5 Linear Ownership Transfer (Zero-Copy)
If the CTRR compiler can prove that the source region has **exactly one** external reference (the one being returned), and the region is "mostly" composed of the result:
- **Algorithm:** Transfer the entire Region Control Block (RCB) ownership to the caller.
- **Rename:** Instead of copying data *out* of the region, we "adopt" the region into the parent's scope.

### 2.6 Region Splicing & Sub-Regions
Instead of promoting an entire arena (which may contain garbage):
- **Mechanism:** Maintain arenas as a linked list of fixed-size blocks (e.g., 64KB).
- **Splicing:** If an object graph is contained within a subset of these blocks, we can "splice" only those blocks from the source region's chain into the destination region's chain.
- **Sub-Regions:** Allow a region to be a logical "view" or "child" of another region, sharing the same underlying arena but having its own lifecycle/tethering metadata.

---

## 3. Advanced Tethering Algorithms

### 3.1 Thread-Local Tether Caches
To avoid atomic contention on `tether_count`:
- **Mechanism:** Each thread maintains a small local cache of "Active Regions".
- **Check-out:** A thread increments the region's `tether_count` once and adds it to its local list.
- **Batching:** Subsequence tethers by the same thread to the same region are O(0) (just check the local list).
- **Quiescent Points:** At thread-safe points (e.g., end of an effect handler), the thread flushes its local tethers.

### 3.2 Hazard Pointers for Regions
Instead of an atomic counter:
- **Algorithm:** Each thread has a set of "Hazard Pointers" pointing to regions it is currently using.
- **Destruction:** When a region's `external_rc` reaches 0, the destroying thread checks the hazard pointers of all other threads. If the region is in use, it's added to a "Retire List" to be freed later.
- **Benefit:** Zero atomic writes on the "hot path" of tethering.

### 3.3 Epoch-Based Reclamation (EBR)
- **Concept:** Regions are retired in "Epochs".
- **Mechanism:** A region is only physically freed when all threads have moved to a newer epoch than the one in which the region was retired.
- **Pros:** Extremely high performance in read-heavy multi-threaded workloads.

---

## 4. Proposed Hybrid Strategy: "The Adaptive Evacuator"

1.  **Small Objects (< 512b):** Iterative Deep Copy with Bitmap detection.
2.  **Medium Objects (512b - 64kb):** Incremental Evacuation (Proxy).
3.  **Large/Dense Objects (> 64kb):** Region Splicing / Promotion.
4.  **Tethering:** Default to Thread-Local Caches with fallback to EBR for high-concurrency modules.

## 5. Implementation Roadmap

- [ ] Implement `RegionBitmap` for fast cycle detection.
- [ ] Refactor `transmigrate.c` to use an iterative worklist.
- [ ] Add `Trace` function pointers to `TypeInfo`.
- [ ] Implement Thread-Local Tethering in `region_core.c`.
