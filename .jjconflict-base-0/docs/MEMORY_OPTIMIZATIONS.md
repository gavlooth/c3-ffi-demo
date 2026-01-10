# Memory Optimizations (RC-G)

OmniLisp's performance is driven by the following optimizations on top of the Region-RC model:

## 1. Region-RC Elision
Similar to Lobster's RC optimization, the compiler elides `region_retain`/`region_release` calls when the region is proven to be local or stack-captured.

## 2. Adaptive Transmigration
Instead of always deep-copying, the runtime switches to **Arena Promotion** for large objects, making "Moves" O(1) for significant data structures.

## 3. Coarse-Grained Locking
By locking at the Region level rather than the Object level, we avoid **Cache Line Contention** (False Sharing) during parallel processing. L1 caches remain clean for hot data.

## 4. Bump-Pointer Consolidation
All objects in a Region are packed contiguously. This maximizes spatial locality, keeping object traversal within fewer cache pages.

## 5. Implicit Cycle Collection
We eliminate the need for **Tarjan's SCC** scans or Tracing GC. Cycles are a zero-cost byproduct of the Region lifecycle.