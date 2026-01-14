# OmniLisp TODO (Active Tasks)

This file contains only active tasks: `[TODO]`, `[IN_PROGRESS]`, and `[BLOCKED]`.

**Completed tasks:** See `TODO_COMPLETED.md`

**Last Updated:** 2026-01-14

---

## Review Directive

**All newly implemented features must be marked with `[DONE] (Review Needed)` until explicitly approved by the user.**
- When an agent completes implementing a feature, mark it `[DONE] (Review Needed)` (not `[DONE]`)
- If a feature is blocked, mark it `[BLOCKED]` with a reason
- `[DONE] (Review Needed)` means: code is written and working, but awaits user review/approval
- After user approval, move completed tasks to `TODO_COMPLETED.md`
- Workflow: `[TODO]` → implement → `[DONE] (Review Needed)` → user approves → move to archive

---

## Global Directives (Read First)

### Repo Root + Path Discipline

**Do not assume an absolute clone path.** All paths are **repo-relative**.

When writing or running verification commands:
- Prefer: `REPO_ROOT="$(git rev-parse --show-toplevel)"` then use `"$REPO_ROOT/..."`
- Prefer `rg -n "pattern" path/` over `ls ... | grep ...`
- Prefer `find path -name 'pattern'` over `ls | grep` for existence checks

### Transmigration Directive (Non-Negotiable)

**Correctness invariant:** For every in-region heap object `src` reached during transmigration, `remap(src)` yields exactly one stable destination `dst`, and all pointer discovery/rewrites happen only via metadata-driven `clone/trace`.

**Do not bypass the metadata-driven transmigration machinery for "fast paths".**

### Issue Authoring Directive

- **Append-only numbering:** Never renumber existing issues
- **No duplicates:** One header per issue number
- **Status required:** Every task line must be `[TODO]`, `[IN_PROGRESS]`, `[DONE] (Review Needed)`, `[BLOCKED]`, or `[N/A]`

### Jujutsu Commit Directive

**Use Jujutsu (jj) for ALL version control operations.**

Before beginning ANY implementation subtask:
1. Run `jj describe -m "Issue N: task description"`
2. Run `jj log` to see current state
3. If mismatch: Either `jj squash` to consolidate or `jj new` to start fresh

---

## Issue 1: Adopt "RC external pointers" semantics as Region-RC spec cross-check [IN_PROGRESS]

**Objective:** Cross-check OmniLisp's per-region reference counting model against the RC dialect's definition of "external pointers".

**Reference (read first):**
- `runtime/docs/MEMORY_TERMINOLOGY.md`
- `docs/CTRR.md`

### P2: External RC Insertion [IN_PROGRESS]

- [IN_PROGRESS] Label: I1-ctrr-external-rc-insertion (P2)
  Objective: Implement external reference counting at region boundaries.
  Where: `csrc/codegen/codegen.c`, `runtime/src/region.c`
  Why: Required for correct cross-region reference management.
  What to change:
    1. Identify external reference sites in codegen
    2. Emit retain/release calls at appropriate boundaries
    3. Integrate with transmigration system

### RF: Regression Fix

- [DONE] (Review Needed) Label: I1-transmigrate-external-root-identity-regression (RF-I1-1)
  - Fixed root identity regression in transmigration

---

## Issue 6: Parser Syntax Completion (Align SYNTAX.md with Implementation) [TODO]

**Objective:** Implement missing parser features documented in SYNTAX.md.

**Reference (read first):**
- `docs/SYNTAX.md` (Implementation Status section)
- `csrc/parser/parser.c`

### P0: Implement Dict Literal Parsing `#{}` [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I6-p0-dict-literals
  Objective: Parse `#{:a 1 :b 2}` as dictionary literal.
  Where: `csrc/parser/parser.c`
  Why: Documented syntax but no parser rule exists.
  What to change:
    1. Add `R_DICT` rule matching `#{` ... `}`
    2. Parse key-value pairs (alternating symbols and expressions)
    3. Return `OMNI_DICT` AST node
  Verification:
    - Add test: `#{:a 1}` parses to dict with key 'a value 1
    - Run: `make -C csrc/tests test`

### P1: Implement Signed Integer Parsing `+456` [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I6-p1-signed-integers
  Objective: Parse `+456` as positive integer literal.
  Where: `csrc/parser/parser.c`
  Why: Currently `+` parsed as symbol, not sign.
  What to change:
    1. Modify `R_INT` to optionally accept leading `+` or `-`
    2. Ensure `-123` and `+456` both parse as integers
  Verification:
    - Add test: `+456` parses to integer 456
    - Add test: `-123` parses to integer -123
    - Run: `make -C csrc/tests test`

### P2: Implement Partial Float Parsing `.5`, `3.` [TODO]

- [TODO] Label: I6-p2-partial-floats
  Objective: Parse `.5` and `3.` as float literals.
  Where: `csrc/parser/parser.c`
  Why: Current parser requires `INT.INT` format.
  What to change:
    1. Update `R_FLOAT` to accept `.DIGITS` and `DIGITS.`
    2. Handle edge cases (`.` alone should not be float)
  Verification:
    - Add test: `.5` parses to float 0.5
    - Add test: `3.` parses to float 3.0
    - Run: `make -C csrc/tests test`

### P3: Complete Match Clause Parsing [TODO]

- [TODO] Label: I6-p3-match-clauses
  Objective: Parse `[pattern result]` and `[pattern :when guard result]` match clauses.
  Where: `csrc/parser/parser.c`, `csrc/analysis/analysis.c`
  Why: Match clauses currently return empty/nil.
  What to change:
    1. Update match parsing to extract pattern and result
    2. Handle `:when` guard syntax
    3. Return structured list for analyzer
  Verification:
    - Add test: `(match x [1 "one"])` parses correctly
    - Add test: `(match x [n :when (> n 0) "positive"])` parses with guard
    - Run: `make -C csrc/tests test`

---

## Issue 8: Codebase Connectivity & Static Helper Audit [TODO]

**Objective:** Resolve unresolved edges and audit static functions for codebase integrity.

**Reference (read first):**
- `DISCONNECTED_EDGES_ANALYSIS.md` (Sections 1, 3, 8)
- `runtime/include/omni.h`

### P0: External Library & Macro Edge Recovery [TODO]

- [TODO] Label: I8-t1-resolve-library-edges
  Objective: Create manual bridge index for third-party libraries (SDS, Linenoise, UTHash, stb_ds).
  Where: Add to `docs/EXTERNAL_DEPENDENCIES.md`
  Why: CodeGraph has blind spots for external libraries (40% of unresolved edges).
  Verification: CodeGraph resolution rate improves.

- [TODO] Label: I8-t3-macro-edge-recovery
  Objective: Document edges generated by core macros (IS_BOXED, ATOMIC_INC_REF, etc.).
  Where: `docs/MACRO_ARCHITECTURE.md`
  Why: Macro-generated calls represent 20% of unresolved edges.

### P1: Static Function Audit & Cleanup [TODO]

- [TODO] Label: I8-t2-audit-static-helpers
  Objective: Audit 467 static functions to identify orphans or expose useful utilities.
  Where: `runtime/src/runtime.c`, `runtime/src/generic.c`, `runtime/src/modules.c`
  What to change:
    1. Scan for unused helpers using `-Wunused-function`
    2. Move useful utilities to public headers
  Verification: `make test` passes; static function count reduced.

---

## Issue 9: Feature Completion: Algebraic Effects, Continuations, and Typed Arrays [TODO]

**Objective:** Implement missing core functionality in the Algebraic Effect system, Fiber/Continuation system, and Typed Arrays.

**IMPORTANT Design Decision (2026-01-14):**
- `try`/`catch` is **NOT supported** in OmniLisp
- Error handling uses **algebraic effects** instead (see `SYNTAX_REVISION.md` Section 7)
- Algebraic effects are implemented using **delimited continuations** (shift/reset style)
- `handle` installs a prompt; effect operations capture continuation to handler
- Handlers can resume, discard, or invoke multiple times the captured continuation

**Reference (read first):**
- `docs/SYNTAX_REVISION.md` (Section 7: Algebraic Effects)
- `DISCONNECTED_EDGES_ANALYSIS.md` (Sections 2, 6.1, 6.3, 6.4)
- `runtime/src/effect.c`
- `runtime/src/memory/continuation.c`

**Constraints:**
- `try`/`catch` syntax is explicitly NOT supported
- Must satisfy existing test stubs in `runtime/tests`

### P0: Effect Tracing & Fiber Callbacks [TODO]

- [TODO] Label: I9-t4-effect-tracing
  Objective: Implement effect trace printing, recording, and clearing.
  Where: `runtime/src/effect.c`
  Why: 4 TODOs currently block effect system observability.
  What to change: Implement `omni_effect_trace_print`, `omni_effect_trace_record`, etc.
  Verification: New test `runtime/tests/test_effect_tracing.c` passes.

- [TODO] Label: I9-t5-continuation-repair
  Objective: Complete fiber callback execution (on_fulfill, on_reject) and error handling.
  Where: `runtime/src/memory/continuation.c`
  Why: 6 TODOs indicate incomplete async/coroutine functionality.
  Verification: `test_with_fibers.lisp` passes with full callback coverage.

### P1: Typed Array Functional Primitives [TODO]

- [TODO] Label: I9-t6-typed-array-primitives
  Objective: Implement map, filter, and reduce for typed arrays.
  Where: `runtime/src/typed_array.c`
  Why: Core collection types lack functional parity.
  Verification: `runtime/tests/test_typed_array_fp.c` passes.

### P2: Algebraic Effects Compiler Support [TODO]

- [TODO] Label: I9-p2-effect-declaration
  Objective: Parse `{effect Name}` as effect type declaration.
  Where: `csrc/parser/parser.c`, `csrc/analysis/analysis.c`
  What to change:
    1. Add parser rule for `{effect ...}` syntax
    2. Store effect operations in type registry
  Verification: Effect declaration parses and registers.

- [TODO] Label: I9-p2-handle-form
  Objective: Implement `handle` as special form in compiler.
  Where: `csrc/codegen/codegen.c`, `runtime/src/memory/continuation.c`
  What to change:
    1. Add `handle` to special form detection
    2. Generate code that installs continuation prompt via runtime
    3. Effect operations capture delimited continuation to handler
    4. Handlers receive continuation + effect args, can resume/discard
    5. Integrate with CTRR region boundaries (prompt = region boundary)
  Verification: Basic `(handle (raise "err") [raise [msg] "caught"])` works.

- [TODO] Label: I9-p2-continuation-primitives
  Objective: Expose delimited continuation primitives for effect implementation.
  Where: `runtime/src/memory/continuation.c`
  What to change:
    1. Implement `prompt` (install delimiter on continuation stack)
    2. Implement `control` (capture continuation up to prompt)
    3. Implement `resume` (invoke captured continuation with value)
  Verification: `runtime/tests/test_delimited_continuations.c` passes.

---

## Issue 10: Advanced Memory Safety: IPGE Integration & Region-Aware Realloc [TODO]

**Objective:** Strengthen memory safety by integrating generation checking with IPGE and implementing region-aware reallocation.

**Reference (read first):**
- `runtime/src/memory/region_pointer.h`
- `docs/CTRR.md`
- `DISCONNECTED_EDGES_ANALYSIS.md` (Sections 6.2, 7.3)

### P0: IPGE Generation & Region Realloc [TODO]

- [TODO] Label: I10-t7-generation-checking
  Objective: Integrate IPGE (Indexed Pointer Generation Epoch) with region system.
  Where: `runtime/src/memory/region_pointer.c`
  Why: Detect stale pointers after region reuse.
  Verification: Test detects use-after-region-exit.

- [TODO] Label: I10-t8-region-realloc
  Objective: Implement region-aware realloc that preserves region membership.
  Where: `runtime/src/region.c`
  Why: Current realloc may move objects between regions.
  Verification: Reallocated object stays in original region.

---

## Issue 11: Build/Test Consolidation & Debug Instrumentation [TODO]

**Objective:** Consolidate test suites and add debug instrumentation toggles.

**Reference (read first):**
- `DISCONNECTED_EDGES_ANALYSIS.md` (Section 5)
- `runtime/tests/Makefile`

### P0: Test Suite Consolidation & Debug Toggles [TODO]

- [TODO] Label: I11-t13-test-consolidation
  Objective: Merge fragmented test files into cohesive suites.
  Where: `runtime/tests/`, `csrc/tests/`
  Why: Current tests are scattered across directories.
  Verification: Single `make test` runs all tests.

- [TODO] Label: I11-t14-debug-integration
  Objective: Add compile-time debug toggles for instrumentation.
  Where: `runtime/include/omni_debug.h`
  What to change:
    1. Create debug toggle macros
    2. Gate debug output with OMNI_DEBUG flag
  Verification: Debug build produces extra output; release is clean.

---

## Issue 12: Parser Syntax Completion [N/A - Duplicate of Issue 6]

**Status:** N/A - This is a duplicate of Issue 6. All work tracked under Issue 6.

---

## Issue 14: Continuation Infrastructure Integration [TODO]

**Objective:** Wire the existing continuation/effect infrastructure into the compiler and runtime.

**DESIGN PRINCIPLE:** Continuations are the **foundational primitive**. Everything else
(effects, generators, async, trampolines) builds on top of continuations. The correct
integration order is: continuations → regions → trampolines → effects → higher abstractions.

**Analysis (2026-01-14):** The runtime has comprehensive infrastructure that is NOT connected:
- CEK machine with continuation frames exists but codegen doesn't use it
- Effect system exists but no primitives are registered
- Region system doesn't connect to continuation prompts as designed
- Duplicate systems: conditions/restarts vs effects, trampoline vs CEK

**Reference (read first):**
- `runtime/src/memory/continuation.h` (CEK machine, fibers, generators, promises)
- `runtime/src/effect.h` (effect handlers, resumptions)
- `docs/SYNTAX_REVISION.md` (Section 7: Algebraic Effects)
- `docs/CTRR.md` (Region memory model)

### P0: Region-Continuation Boundary Integration [TODO] (FOUNDATION)

- [TODO] Label: I14-p0-prompt-region-boundary
  Objective: Connect continuation prompts with region boundaries.
  Where: `runtime/src/memory/continuation.c`, `runtime/src/memory/region_core.c`
  Why: **This is the foundation.** Design specifies "prompt = region boundary" for memory safety.
       Continuations capture state; regions manage that state's lifetime.
  What to change:
    1. Add `Region*` field to Prompt frame in continuation.h
    2. On prompt install: create child region (prompt owns region)
    3. On control capture: transmigrate captured values out of prompt's region
    4. On prompt exit: exit region (deallocate prompt-local allocations)
    5. Ensure continuation frames track their owning region
  Verification:
    - Captured continuations don't hold stale region pointers
    - Memory allocated during delimited computation is freed on prompt exit
    - `runtime/tests/test_continuation_region.c` passes

### P1: Trampoline-CEK Unification [TODO] (USE FOUNDATION)

- [TODO] Label: I14-p1-trampoline-use-cek
  Objective: Reimplement trampoline using CEK machine.
  Where: `runtime/src/trampoline.c`
  Why: Trampoline is manual continuation-passing. CEK provides this natively.
       Once regions are connected, CEK frames have proper lifetime management.
  What to change:
    1. Replace bounce objects with CEK APP frames
    2. Use `omni_cek_step` instead of manual trampoline loop
    3. Bounce thunks become proper continuation frames
    4. Remove manual mark-field hack for function pointers
  Verification: Mutual recursion tests pass with CEK backend.

### P2: Wire Effect Primitives [TODO] (BUILD ON CONTINUATIONS)

- [TODO] Label: I14-p2-register-effect-primitives
  Objective: Register effect primitives for use from Lisp code.
  Where: `runtime/src/primitives.c` or new `runtime/src/primitives_effect.c`
  Why: Effects use delimited continuations (prompt/control). Must be done after P0.
  What to change:
    1. Create `prim_handle` - install effect handler (creates prompt + region)
    2. Create `prim_perform` - perform effect operation (captures continuation)
    3. Create `prim_resume` - resume captured continuation
    4. Register with primitive table
  Verification: `(handle (raise "test") [raise [msg] "caught"])` works.

- [TODO] Label: I14-p2-codegen-handle-form
  Objective: Emit C code for `handle` special form.
  Where: `csrc/codegen/codegen.c`
  What to change:
    1. Add `handle` to special form detection
    2. Emit calls to `omni_effect_push_handler` (installs prompt)
    3. Emit handler clauses as C functions
    4. Emit `omni_effect_pop_handler` at scope exit (exits region)
  Verification: Generated C compiles and runs effect handling.

### P3: Unify Condition/Restart with Effects [TODO]

- [TODO] Label: I14-p3-unify-conditions-effects
  Objective: Reimplement conditions/restarts on top of effect system.
  Where: `runtime/src/condition.c`, `runtime/src/restart.c`
  Why: Currently duplicate error handling systems. Effects subsume conditions.
  What to change:
    1. Define `{effect Condition}` with raise/handle operations
    2. Rewrite condition_signal using omni_effect_perform
    3. Rewrite restarts as effect handlers with resume
  Verification: Existing condition tests pass using effect backend.

### P4: Iterator-Generator Integration [TODO]

- [TODO] Label: I14-p4-iterator-generator
  Objective: Implement iterators using generator continuations.
  Where: `runtime/src/iterator.c`
  Why: Generators provide proper lazy evaluation with suspend/resume.
       Generators are implemented using delimited continuations.
  What to change:
    1. Implement `prim_iterate` using `omni_generator_create`
    2. Implement `prim_iter_next` using `omni_generator_next`
    3. Add `yield` primitive using generator yield
  Verification: `(take 5 (iterate inc 0))` returns `[0 1 2 3 4]`.

### P4: Iterator-Generator Integration [TODO]

- [TODO] Label: I14-p4-iterator-generator
  Objective: Implement iterators using generator continuations.
  Where: `runtime/src/iterator.c`
  Why: Generators provide proper lazy evaluation with suspend/resume.
  What to change:
    1. Implement `prim_iterate` using `omni_generator_create`
    2. Implement `prim_iter_next` using `omni_generator_next`
    3. Add `yield` primitive using generator yield
  Verification: `(take 5 (iterate inc 0))` returns `[0 1 2 3 4]`.

---

## Issue 15: Arena & Memory System Enhancements [TODO]

**Objective:** Implement remaining SOTA arena techniques and complete memory system integration.

**Reference (read first):**
- `runtime/docs/ARCHITECTURE.md` (Arena Allocator Implementation, SOTA Techniques sections)
- `third_party/arena/vmem_arena.h`
- `runtime/src/memory/region_core.h`

**Background (2026-01-14):** Arena implementation review completed. Current system has:
- ✅ Dual arena (malloc-based + vmem commit-on-demand)
- ✅ O(1) chunk splice for transmigration
- ✅ Inline buffer (512B) for small objects
- ✅ Thread-local region pools
- ✅ THP alignment (2MB chunks)
- ⚠️ Partial store barrier / region_of support

### P0: Scratch Arena API [TODO]

- [TODO] Label: I15-p0-scratch-arena-api
  Objective: Implement double-buffered scratch arenas for temporary allocations.
  Where: `runtime/src/memory/scratch_arena.h` (new), `runtime/src/memory/transmigrate.c`
  Why: Transmigration temporaries (forwarding table, worklist) currently pollute destination region.
  What to change:
    1. Create `ScratchArenas` struct with two arenas + current index
    2. Implement `get_scratch(Arena* conflict)` - return non-conflicting arena
    3. Implement `scratch_checkpoint()` / `scratch_rewind()` for scoped usage
    4. Add `__thread ScratchArenas tls_scratch` per-thread pool
    5. Update transmigrate.c to use scratch for forwarding table
  Verification:
    - Transmigration doesn't bloat destination region with temporaries
    - `runtime/tests/test_scratch_arena.c` passes

### P1: Explicit Huge Page Support [TODO]

- [TODO] Label: I15-p1-madv-hugepage
  Objective: Add MADV_HUGEPAGE hint for vmem_arena chunks.
  Where: `third_party/arena/vmem_arena.h`
  Why: Explicit hint guarantees huge pages vs THP's opportunistic promotion.
  What to change:
    1. Add `madvise(base, reserved, MADV_HUGEPAGE)` after mmap in `vmem_chunk_new()`
    2. Guard with `#ifdef MADV_HUGEPAGE` for portability
    3. Add `VMEM_USE_HUGEPAGES` compile flag to enable/disable
  Verification:
    - Check `/proc/meminfo` shows AnonHugePages increasing
    - No regression on systems without huge page support

### P2: Store Barrier Choke-Point [TODO]

- [TODO] Label: I15-p2-store-barrier-impl
  Objective: Implement single choke-point for all pointer stores with lifetime checking.
  Where: `runtime/src/memory/store_barrier.h` (new), codegen integration
  Why: Required to enforce Region Closure Property at mutation time.
  What to change:
    1. Create `omni_store_barrier(Obj* container, int field, Obj* value)` function
    2. Implement lifetime check: `if (!omni_region_outlives(dst_region, src_region))`
    3. Auto-repair via transmigrate or merge based on `get_merge_threshold()`
    4. Update codegen to emit store barrier for all pointer field assignments
    5. Increment `region->escape_repair_count` on auto-repair
  Verification:
    - Younger→older store triggers auto-repair
    - `runtime/tests/test_store_barrier.c` passes

### P3: region_of(obj) Lookup [TODO]

- [TODO] Label: I15-p3-region-of-lookup
  Objective: Implement O(1) lookup from object pointer to owning region.
  Where: `runtime/src/memory/region_pointer.h`, `runtime/src/memory/region_core.c`
  Why: Store barrier needs to determine object's region efficiently.
  What to change:
    1. Option A: Use existing pointer masking (region_id in high bits)
       - Add `region_lookup_by_id(uint16_t id)` global registry
    2. Option B: Store region pointer in object header (space tradeoff)
    3. Implement `Region* region_of(Obj* obj)` using chosen strategy
  Verification:
    - `region_of(arena_allocated_obj)` returns correct region
    - O(1) lookup performance

### P4: Size-Class Segregation [OPTIONAL]

- [TODO] Label: I15-p4-size-class-segregation
  Objective: Separate arenas for different allocation sizes.
  Where: `runtime/src/memory/region_core.h`
  Why: Better cache locality for homogeneous object traversal.
  What to change:
    1. Add `Arena pair_arena` for TYPE_ID_PAIR allocations
    2. Add `Arena container_arena` for arrays/dicts/strings
    3. Route `region_alloc_typed()` to appropriate arena
  Note: May not be needed - inline buffer already handles small objects.
  Verification:
    - List traversal benchmark shows improved cache behavior
    - No regression in allocation throughput

---

## Summary

| Issue | Status | Description |
|-------|--------|-------------|
| 1 | IN_PROGRESS | RC external pointers / Region-RC spec |
| 6 | TODO | Parser syntax completion |
| 8 | TODO | Codebase connectivity audit |
| 9 | TODO | Algebraic effects, continuations, typed arrays |
| 10 | TODO | IPGE integration, region realloc |
| 11 | TODO | Build/test consolidation |
| 14 | TODO | **Continuation infrastructure integration** |
| 15 | TODO | **Arena & memory system enhancements** |

**Completed issues:** See `TODO_COMPLETED.md` for Issues 2, 3, 4, 5, 7.
