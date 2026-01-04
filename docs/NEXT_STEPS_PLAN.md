# Next Steps: Optimizing Component Tethering

This roadmap prioritizes correctness first, followed by architectural efficiency, and finally micro-optimizations.

## Phase 1: Correctness (Dynamic Merging)
**Goal:** Prevent leaks when two separate cyclic components are linked at runtime.
- [ ] Implement **Union-Find** data structure in `SymComponent`.
- [ ] Update `sym_link(a, b)`:
    - Check if `a->comp != b->comp`.
    - If different, **merge** the smaller component into the larger one.
    - Move member list from child to parent.
    - Update `comp` pointers for all moved members.
- [ ] Verify with test case: Create two separate cycles, link them, drop handles.

## Phase 2: Architecture (Unified Regions)
**Goal:** Optimize allocation and RefCounting for both DAGs and Cycles.
- [ ] Create `Region` struct (wraps an Arena).
- [ ] Implement `region_alloc_component()`: Allocate `SymComponent` inside Region.
- [ ] Implement **Region-RC**:
    - `inc_ref` check: If `target` is in same Region as `source`, **skip** atomic op.
    - `dec_ref` check: Only decrement if `target` is in different Region.
- [ ] Implement **Bulk Free**:
    - When Region RC == 0, free entire Arena.
    - Skip `sym_dismantle_component` (implicit reclamation).

## Phase 3: Compiler Optimization (Tether Elision)
**Goal:** Remove runtime overhead for strictly nested scopes.
- [ ] Update `AnalysisContext`: Track `active_handles` (dominating variables).
- [ ] New Pass `omni_optimize_tethers`:
    - For each `tether_begin(C)`:
    - Check if any variable `H` in `active_handles` holds a handle to `C`.
    - If `H` dominates the tether scope and is not released inside it:
    - **Remove** the `tether_begin/end` pair (Static Elision).

## Phase 4: Micro-Optimizations (Post-Region)
- [ ] **Lazy Dismantling**: If using global heap (non-Region), queue huge components for incremental freeing.
- [ ] **Bit-Packing**: Compress `SymComponent` header to 64 bits.
