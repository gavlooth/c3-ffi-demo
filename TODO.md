# OmniLisp TODO

## Review Directive

**All newly implemented features must be marked with `[R]` (Review Needed) until explicitly approved by the user.**

- When an agent completes implementing a feature, mark it `[R]` (not `[DONE]`)
- `[R]` means: code is written and working, but awaits user review/approval
- After user approval, change `[R]` to `[DONE]`
- Workflow: `[TODO]` → implement → `[R]` → user approves → `[DONE]`

This ensures no feature is considered "done" until a human has reviewed it.

---

## Phase 13: Region-Based Reference Counting (RC-G) Refactor

Replace hybrid memory management with a unified Region-RC architecture.

- [DONE] Label: T-rcg-arena
  Objective: Integrate `tsoding/arena` as the physical allocator backend.
  Where: `third_party/arena/arena.h`, `runtime/src/memory/arena_core.c`
  What to change:
    - [x] Download `arena.h` from `tsoding/arena`.
    - [x] Rename internal `Region` struct to `ArenaChunk` to avoid conflicts.
    - [x] Integrate arena into `region_core.c`.
  How to verify: Compile `region_core.c` and run allocation tests.
  Acceptance:
    - `arena.h` present in `third_party`.
    - `arena_alloc` works for bump allocation.

- [DONE] Label: T-rcg-region
  Objective: Implement the Logical `Region` Control Block.
  Where: `runtime/src/memory/region_core.h`, `runtime/src/memory/region_core.c`
  What to change:
    - [x] Define `Region` struct:
      ```c
      typedef struct Region {
          Arena arena;                // Physical storage (bump allocator)
          int external_rc;            // Strong refs from OTHER regions/stack (atomic)
          int tether_count;           // Temporary "borrows" by threads (atomic)
          bool scope_alive;           // True if the semantic scope is still active
      } Region;
      ```
    - [x] Implement `region_create()`: Mallocs Region, init arena, sets rc=0, scope_alive=true.
    - [x] Implement `region_destroy_if_dead(Region* r)`:
      - Logic: `if (!r->scope_alive && r->external_rc == 0 && r->tether_count == 0)` -> free arena and r.
    - [x] Implement `region_exit()`, `region_tether_start/end()`, `region_alloc()`.
  How to verify: Unit test creating a region, retaining/releasing, and verifying destroy is called at 0.
  Acceptance:
    - `Region` struct defined.
    - Lifecycle functions managed via RC/Liveness flags.

- [DONE] Label: T-rcg-ref
  Objective: Implement `RegionRef` fat pointer and atomic ops.
  Where: `runtime/src/memory/region_core.h`, `runtime/src/memory/region_core.c`
  What to change:
    - [x] Define `RegionRef { void* ptr; Region* region; }`.
    - [x] Implement atomic `region_retain(RegionRef ref)`: `ref.region->external_rc++`.
    - [x] Implement atomic `region_release(RegionRef ref)`: `ref.region->external_rc--`. If 0, call `region_destroy_if_dead`.
  How to verify: Multi-threaded test incrementing/decrementing refcounts.
  Acceptance:
    - Thread-safe RC operations.
    - Integration with `region_destroy_if_dead`.

- [DONE] Label: T-rcg-transmigrate
  Objective: Implement Adaptive Transmigration (Deep Copy + Promotion).
  Where: `runtime/src/memory/transmigrate.c`, `runtime/src/memory/transmigrate.h`
  What to change:
    - [x] Implement `transmigrate(void* root, Region* src_region, Region* dest_region)`:
      - Use `uthash` for `Map<void* old_ptr, void* new_ptr>` to handle cycles.
      - Recursively walk graph based on `obj->tag`.
      - Deep copy primitives and recursively copy children.
    - [x] Add adaptive logic: if `copied_bytes > 4096`, abort copy and perform **Region Promotion** (Arena Promotion).
  How to verify: Test copying cyclic graphs between regions.
  Acceptance:
    - Cycles preserved in copy.
    - Large copies trigger promotion (pointer reuse).

- [DONE] Label: T-rcg-constructors
  Objective: Implement Region-Aware Value Constructors.
  Where: `runtime/src/memory/region_value.h`, `runtime/src/memory/region_value.c`
  What to change:
    - [x] Create `region_value.h` with declarations for all `mk_*_region` functions.
    - [x] Implement `region_value.c` with all region-aware constructors.
  How to verify: Unit test creating various values in a region.
  Acceptance:
    - All `mk_*_region` functions work correctly.

- [TODO] Label: T-rcg-inference
  Objective: Implement Advanced Lifetime-Based Region Inference.
  Where: `csrc/analysis/region_inference.c`
  What to change:
    - [ ] **Step 1: Build Variable Interaction Graph (VIG)**
      - Iterate all instructions in CFG.
      - Add nodes for all variables.
      - Add undirected edge `(u, v)` if they interact (assignment, cons, call args).
    - [ ] **Step 2: Find Connected Components**
      - Use Union-Find or BFS to group variables into Candidate Regions.
    - [ ] **Step 3: Liveness Analysis**
      - For each Component `C`, find `Start(C)` (earliest def) and `End(C)` (latest use).
    - [ ] **Step 4: Dominator Placement**
      - Insert `region_create` at Dominator of `Start(C)`.
      - Insert `region_destroy` at Post-Dominator of `End(C)`.
  How to verify: Inspect compiler output for `region_create/destroy` calls matching variable lifetimes.
  Acceptance:
    - Variables grouped by interaction.
    - Scope boundaries correctly identified.

- [TODO] Label: T-rcg-codegen-lifecycle
  Objective: Wire Region lifecycle into Codegen.
  Where: `csrc/codegen/codegen.c`, `src/runtime/compiler/omni_compile.c`
  What to change:
    - [ ] **Modify Function Signatures**: Add `Region* _caller_region` as the first argument to all generated C functions.
    - [ ] **Inject Local Region**: At the start of each function (or at dominator), emit `Region* _local_region = region_create();`.
    - [ ] **Constructor Swap**: Replace all `mk_int(...)`, `mk_pair(...)`, etc., with `mk_int_region(_local_region, ...)`.
    - [ ] **Call Site Update**: Pass `_local_region` as the first argument to all nested function calls.
    - [ ] **Region Exit**: Emit `region_exit(_local_region)` before each `return` and at the end of the function.
  How to verify: Compile a simple OmniLisp program and verify the generated C code includes region management.
  Acceptance:
    - Generated code uses regions for all allocations.
    - Regions are correctly created and exited.

- [TODO] Label: T-rcg-escape
  Objective: Implement Transmigration for escaping values.
  Where: `csrc/codegen/codegen.c`, `csrc/analysis/escape.c`
  What to change:
    - [ ] **Return Path Logic**: Identify values that escape to the caller (returned values).
    - [ ] **Emit Transmigrate**: Before the `return` statement, emit `Value* _result = transmigrate(val, _local_region, _caller_region);`.
    - [ ] **Cleanup**: Ensure `region_exit(_local_region)` is called AFTER `transmigrate`.
  How to verify: Compile a function returning a list and verify the result is transmigrated to the caller's region.
  Acceptance:
    - Returned objects are safely moved to the caller's region.
    - No dangling references to the local region.

- [TODO] Label: T-rcg-codegen-tether
  Objective: Emit Tethering for RegionRef arguments.
  Where: `csrc/codegen/codegen.c`
  What to change:
    - [ ] For `RegionRef` arguments, emit `region_tether_start(arg.region);` at function entry.
    - [ ] Emit `region_tether_end(arg.region);` at function exit.
  How to verify: Inspect generated code for tether calls.
  Acceptance:
    - Arguments are tethered during function execution.

- [DONE] Label: T-rcg-cleanup
  Objective: Remove obsolete runtime components.
  Where: `runtime/src/memory/`, `src/runtime/memory/`, `runtime/src/runtime.c`
  What to change:
    - [x] Delete `runtime/src/memory/scc.c`, `scc.h`, `component.c`, `component.h`.
    - [x] Delete `src/runtime/memory/scc.c`, `scc.h`.
    - [x] Modify `Obj` struct in `runtime/src/runtime.c`: Remove `int scc_id`, `int scan_tag`.
  How to verify: `make clean && make test`.
  Acceptance:
    - Codebase compiles without old cycle detector.
    - All tests pass with new Region-RC runtime.