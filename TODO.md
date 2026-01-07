# OmniLisp TODO

## Review Directive

**All newly implemented features must be marked with `[R]` (Review Needed) until explicitly approved by the user.**

- When an agent completes implementing a feature, mark it `[R]` (not `[DONE]`)
- `[R]` means: code is written and working, but awaits user review/approval
- After user approval, change `[R]` to `[DONE]`
- Workflow: `[TODO]` → implement → `[R]` → user approves → `[DONE]`

---

## Phase 13: Region-Based Reference Counting (RC-G) Refactor

Replace hybrid memory management with a unified Region-RC architecture.

- [DONE] Label: T-rcg-arena
  Objective: Integrate `tsoding/arena` as the physical allocator backend.
- [DONE] Label: T-rcg-region
  Objective: Implement the Logical `Region` Control Block.
- [DONE] Label: T-rcg-ref
  Objective: Implement `RegionRef` fat pointer and atomic ops.
- [DONE] Label: T-rcg-transmigrate
  Objective: Implement Adaptive Transmigration (Deep Copy + Promotion).
- [DONE] Label: T-rcg-constructors
  Objective: Implement Region-Aware Value Constructors.
- [DONE] Label: T-rcg-cleanup
  Objective: Remove obsolete runtime components.

---

## Phase 14: ASAP Region Management (Static Lifetimes)

**Objective:** Implement static liveness analysis to drive region deallocation, with RC as a fallback only.
**Reference:** `docs/STATIC_REGION_LIFETIME_ARCHITECTURE.md`

- [TODO] Label: T-asap-region-liveness
  Objective: Implement static liveness analysis for inferred Region Handles.
  Where: `csrc/analysis/region_inference.c`, `csrc/analysis/analysis.c`
  What to change:
    - [ ] Calculate the **Last Use Point** of each inferred Region.
    - [ ] Mark Regions as `LIFETIME_STATIC` (default) or `LIFETIME_DYNAMIC` (if escaping).
  How to verify: Inspect compiler debug logs for correctly identified region death points.
  Acceptance:
    - Regions tracked as first-class analysis entities.
    - Last use points accurately calculated across control flow.

- [TODO] Label: T-asap-region-codegen
  Objective: Emit static deallocation calls (`region_destroy`).
  Where: `csrc/codegen/codegen.c`
  What to change:
    - [ ] For `LIFETIME_STATIC` regions, emit `region_destroy()` at the last use point.
    - [ ] Remove `external_rc` increments/decrements for static regions.
  How to verify: Compile a local-only Lisp program; verify the generated C has NO refcounting calls, only create/destroy.
  Acceptance:
    - Zero-overhead static cleanup for local regions.

- [TODO] Label: T-asap-region-fallback
  Objective: Wire the RC fallback for escaping regions.
  Where: `csrc/codegen/codegen.c`
  What to change:
    - [ ] At escape points (return/global store), emit `region_retain()`.
    - [ ] Emit `RegionRef` wrapping for escaping data.
  How to verify: Compile a function returning data; verify RC is enabled only for that return path.
  Acceptance:
    - Seamless transition from Static to RC on escape.

- [TODO] Label: T-asap-region-subsumption
  Objective: Implement Region Subsumption (Flattening) pass.
  Where: `csrc/analysis/region_inference.c`
  What to change:
    - [ ] Identify candidate regions whose lifetimes are strictly nested within a parent.
    - [ ] Merge nested regions into their parents to eliminate redundant RCBs and arenas.
  How to verify: Compile nested let-blocks and verify only one region is created in the generated C.
  Acceptance:
    - Reduced region cardinality for nested lifecycles.
    - No loss of safety for cyclic data within merged regions.

- [TODO] Label: T-asap-region-main

  Objective: Bootstrap the Root Region in the interpreter.

  Where: `src/runtime/main.c`

  What to change:

    - [ ] Implement `region_create()` at main start and `region_destroy()` at exit.

  How to verify: Run interpreter; verify all top-level values are reclaimed.



---



## Phase 15: Advanced Region Optimization



**Objective:** Implement high-performance transmigration and tethering algorithms.

**Reference:** `docs/ADVANCED_REGION_ALGORITHMS.md`



- [DONE] Label: T-opt-bitmap-cycle



  Objective: Implement Bitmap-based Cycle Detection for transmigration.







  Where: `runtime/src/memory/region_core.c`, `runtime/src/memory/transmigrate.c`



  What to change:



    - [x] Add `RegionBitmap` utility to track visited addresses within a region.



    - [x] Replace `uthash` in `transmigrate.c` with bitmap lookup for internal pointers.



  How to verify: Run `test_rcg_transmigrate` with large cyclic graphs; verify speedup and reduced allocations.



  Acceptance:



    - Zero `malloc` calls for cycle detection during intra-region transmigration.







- [DONE] Label: T-opt-iterative-trans







  Objective: Implement Iterative Worklist Transmigration.











  Where: `runtime/src/memory/transmigrate.c`



  What to change:



    - [x] Replace recursive `copy_value` with an explicit worklist (stack).



    - [x] Implement tail-recursion optimization for `T_CELL` chains.



  How to verify: Run transmigration on a list of 1M elements; verify it doesn't segfault.



  Acceptance:



    - O(1) stack usage for deep object graphs.







- [DONE] Label: T-opt-metadata-trace







  Objective: Implement Metadata-Driven Traversal (Trace functions).











  Where: `runtime/src/runtime.c`, `runtime/src/memory/transmigrate.c`



  What to change:



    - [x] Add `TraceFn` pointer to `TypeInfo` or `Value` structure.



    - [x] Update `transmigrate.c` to use `obj->trace()` instead of large switch.



  How to verify: Add a new custom type and verify it can be transmigrated without modifying `transmigrate.c`.



  Acceptance:



    - Traversal logic decoupled from the core transmigration loop.







- [DONE] Label: T-opt-region-splicing







  Objective: Implement Block-level Region Splicing.











  Where: `runtime/src/memory/arena_core.c`, `runtime/src/memory/region_core.c`



  What to change:



    - [x] Modify `Arena` to support detaching and attaching individual memory blocks.



    - [x] Implement `region_splice(src, dest, blocks)` to move chunks of memory between regions.



  How to verify: Perform a large transmigration and verify that blocks are moved O(1) instead of copied.



  Acceptance:



    - Zero-copy movement of large contiguous data segments between regions.







- [DONE] Label: T-opt-tether-cache







  Objective: Implement Thread-Local Tether Caching.











  Where: `runtime/src/memory/region_core.c`



  What to change:



    - [x] Add `__thread` local cache for active regions.



    - [x] Update `region_tether_start/end` to use local cache before atomics.



  How to verify: Benchmark multi-threaded access to a shared region; verify reduced cache contention.



  Acceptance:



    - Reduced atomic operations for redundant tethers in the same thread.








