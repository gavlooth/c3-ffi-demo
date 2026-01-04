# Memory Model Optimization Opportunities

Based on the v0.6.0 Component-Level Tethering architecture, the following optimizations provide the next steps for performance and capability.

## 1. Dynamic Component Merging ("Grand Unification")
**Target:** Runtime / Correctness
**Problem:** The current system relies on *static* SCC detection. If the program dynamically links two separate components (A and B) into a cycle at runtime (A -> B -> A), they will sustain each other's reference counts and leak until program exit.
**Solution:** Implement Union-Find logic in `sym_link`.
- When linking `objA` (Component 1) to `objB` (Component 2), merge Component 2 into Component 1.
- Use path compression or list splicing to move members.
- **Benefit:** Handles arbitrary dynamic cycles without a global GC.

## 2. Static Tether Elision
**Target:** Compiler / Performance
**Problem:** The compiler injects `tether_begin/end` for any borrow scope where safety isn't trivial.
**Solution:** Use Dominator Analysis to prove liveness.
- If a local variable `H` holds a strong handle to Component `C`.
- And `H` is live across the entire duration of a borrow `B`.
- Then `B` is safe by definition.
- **Benefit:** Removes atomic/counter overhead for strictly nested borrows.

## 3. Variable-Sized SymObj (Locality)
**Target:** Runtime / Cache Efficiency
**Problem:** `SymObj` has 3 inline refs. Objects with degree > 3 require a `malloc` for the refs array, causing a pointer chase.
**Solution:** Use flexible array members (`SymObj` + `N * ptr`) and size-class allocation.
- Allocate `sizeof(SymObj) + count * sizeof(SymObj*)` as one block.
- **Benefit:** Improved CPU cache locality for complex graph nodes.

## 4. Lazy / Incremental Dismantling
**Target:** Runtime / Latency
**Problem:** Dropping the last handle to a massive component (e.g., 100k nodes) causes an immediate, potentially long pause while it dismantles.
**Solution:** Push dead components to a `dismantle_queue`.
- Process the queue in small batches (e.g., 100 nodes) at safe points.
- **Benefit:** Smoother frame rates for soft-real-time applications.

## 5. Bit-Packed Component Headers
**Target:** Runtime / Throughput
**Problem:** `SymComponent` checks `handle_count` and `tether_count` separately.
**Solution:** Pack both into a single `_Atomic uint64_t` (e.g., top 32 bits for handles, low 32 for tethers).
- Check `state == 0` in one instruction.
- **Benefit:** Faster boundary operations (acquire/release).

## 6. Unified Region-Based Memory (The "Region-RC" Fusion)
**Target:** Architecture / All Strategies
**Problem:** Currently, we pay per-object overhead for Standard RC (atomics) and Components (headers/dismantle). If a function creates a complex DAG or a cluster of small cycles, managing them individually is wasteful.
**Solution:** Group objects by **Lifetime** into a **Region** (Arena) and apply RC to the *Region* instead of the objects.
- **Allocation:** All objects (DAG nodes or Component headers) are allocated in the Region.
- **Intra-Region References:** Pointers between objects in the same Region require **zero RC overhead**. We assume if the Region is alive, its contents are alive.
- **External References:** Only pointers *entering* the Region from outside trigger an increment on the **Region's RC**.
- **Bulk Free:** When the Region's RC drops to 0, free the entire arena. **No cascading decrements** (for DAGs) and **no dismantling** (for Components).
- **Migration:** If an object (DAG or Component) escapes the Region, migrate it (deep copy or move) to the **Parent Region**. This preserves locality and avoids degrading to global heap allocation unless necessary.
- **Benefit:** 
    - **DAGs:** Eliminates 90%+ of atomic operations (internal links) and enables O(1) free.
    - **Cycles:** Eliminates overhead for clustered small cycles.
    - **Cache:** Perfect locality.