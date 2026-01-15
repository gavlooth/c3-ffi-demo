# OmniLisp TODO (Active Tasks)

This file contains only active tasks: `[TODO]`, `[IN_PROGRESS]`, and `[BLOCKED]`.

**Completed tasks:** See `TODO_COMPLETED.md`

**Last Updated:** 2026-01-15

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

## Design Decisions

### Data Structure Simplification (2026-01-15)

**Decision:** OmniLisp uses exactly **3 core collection types**:

| Type | Syntax | Use Case |
|------|--------|----------|
| **List** | `(1 2 3)` | Cons cells, code representation, recursive processing |
| **Array** | `[1 2 3]` | Mutable, indexed access, general sequences |
| **Dict** | `#{:a 1 :b 2}` | Key-value storage, structured data |

**Deprecated types** (runtime support retained for backward compatibility):
- **Tuple** → Use arrays instead (same semantics, simpler model)
- **Named Tuple** → Use dicts instead (`:key value` pairs)
- **Alist/Plist** → Use dicts instead (unified key-value abstraction)

**Rationale:**
1. Tuples provide no semantic benefit over arrays in a Region-RC model
2. Named tuples are just dicts with ordered iteration (dicts suffice)
3. Alists/Plists are legacy Lisp patterns; dicts are more intuitive
4. Fewer types = simpler mental model, consistent destructuring syntax
5. No immutability notation needed: Region-RC handles mutation safety at region boundaries

**Documentation updated:**
- `docs/SYNTAX.md` - Collection syntax
- `language_reference.md` - Destructuring patterns, type examples
- `docs/QUICK_REFERENCE.md` - Let destructuring
- `docs/SYNTAX_REVISION.md` - Character Calculus table, variance examples
- `docs/LANGUAGE_COMPLETENESS_REPORT.md` - Data types status

**Runtime deprecation:**
- `mk_tuple()`, `mk_tuple_region()` - marked deprecated
- `mk_named_tuple()`, `mk_named_tuple_region()` - marked deprecated
- `print_tuple()`, `print_named_tuple()` - marked deprecated

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

### P2: Implement Partial Float Parsing `.5`, `3.` [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I6-p2-partial-floats
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

### P3: Complete Match Clause Parsing [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I6-p3-match-clauses
  Objective: Parse `[pattern result]` and `[pattern :when guard result]` match clauses.
  Where: `csrc/codegen/codegen.c`, `runtime/include/omni.h`
  What was done:
    1. Updated `codegen_match` to detect array-based clause syntax
    2. Extract pattern from array[0], result from array[1]
    3. Detect `:when` keyword at array[1] for guarded clauses
    4. Added `is_pattern_match` declaration to omni.h
    5. Both new syntax `[pattern result]` and legacy syntax work
  Verification:
    - Test: `(match x [1 "one"])` → works
    - Test: `(match x [n :when (> n 0) "positive"])` → works
    - Test file: `tests/test_match_clauses.lisp` - all 6 tests pass

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

### P0: Effect Tracing & Fiber Callbacks [IN_PROGRESS]

- [DONE] (Review Needed) Label: I9-t4-effect-tracing
  Objective: Implement effect trace printing, recording, and clearing.
  Where: `runtime/src/effect.c`
  What was done:
    1. Added TraceEntry struct and ring buffer (256 entries)
    2. Implemented `effect_trace_print()` - prints trace to stdout
    3. Implemented `effect_trace_to_string()` - returns trace as malloc'd string
    4. Implemented `effect_trace_record()` - records effect with timestamp
    5. Implemented `effect_trace_clear()` - clears trace buffer
    6. Added `effect_trace_mark_handled()` and `effect_trace_last_index()` helpers
  Verification: `runtime/tests/test_effect_tracing.c` passes (10 tests).

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

**DESIGN PRINCIPLE:** Continuations are the **foundational primitive** for control flow
abstractions. Effects, generators, and async/fibers build on continuations. The correct
integration order is: continuations → regions → effects → higher abstractions.

**ARCHITECTURE NOTE:** Trampolines are **intentionally separate** from the continuation
infrastructure. They are a simple tail-call optimization mechanism that predates the CEK
machine and do not require continuation semantics. The two systems coexist independently.

**Analysis (2026-01-14):** The runtime has comprehensive infrastructure that is NOT connected:
- CEK machine with continuation frames exists but codegen doesn't use it
- Effect system exists but no primitives are registered
- Region system doesn't connect to continuation prompts as designed
- Conditions/restarts now unified with effects (P3 DONE)

**Reference (read first):**
- `runtime/src/memory/continuation.h` (CEK machine, fibers, generators, promises)
- `runtime/src/effect.h` (effect handlers, resumptions)
- `docs/SYNTAX_REVISION.md` (Section 7: Algebraic Effects)
- `docs/CTRR.md` (Region memory model)

### P0: Region-Continuation Boundary Integration [DONE] (Review Needed) (FOUNDATION)

- [DONE] (Review Needed) Label: I14-p0-prompt-region-boundary
  Objective: Connect continuation prompts with region boundaries.
  Where: `runtime/src/memory/continuation.c`, `runtime/src/memory/continuation.h`
  What was done (already implemented in codebase):
    1. `Region* region` field exists in Prompt frame (continuation.h:99)
    2. `cont_prompt()` creates child region on prompt install (continuation.c:487)
    3. `cont_capture()` transmigrates frame contents to parent (continuation.c:363-374)
    4. `cont_prompt_exit()` calls `region_exit()` on prompt's region (continuation.c:574)
    5. `frame_transmigrate_contents()` handles all frame types (continuation.c:248-301)
  Verification:
    - Region-Continuation boundary is fully integrated
    - Continuation system uses CTRR for memory safety

### P1: Wire Effect Primitives [DONE] (Review Needed) (BUILD ON CONTINUATIONS)

- [DONE] (Review Needed) Label: I14-p2-register-effect-primitives
  Objective: Register effect primitives for use from Lisp code.
  Where: `runtime/src/runtime.c`, `runtime/src/effect.c`
  What was done:
    1. `prim_perform` - performs effect by name, captures continuation
    2. `prim_resume` - resumes captured continuation with value
    3. `effect_handle` - runs body thunk with handlers installed
    4. Primitives already registered in runtime
  Verification: ✓ All 7 effect tests pass (see tests/test_effects.lisp)

- [DONE] (Review Needed) Label: I14-p2-codegen-handle-form
  Objective: Emit C code for `handle` special form.
  Where: `csrc/codegen/codegen.c`
  What was done:
    1. Added `handle` to special form detection
    2. Added `codegen_handler_closure` - generates static handler functions with region-aware signature
    3. Added `codegen_body_thunk` - generates 0-arg closure for handle body
    4. Emit `effect_handle(_body_thunk, _h_clauses, _h_return_clause, NULL)`
    5. Handler closures receive (payload, resume) args and can call `prim_resume`
  IMPORTANT: Built-in `Fail` effect is RECOVERY_ABORT mode (can't resume).
             Use custom effect names for resumable effects.
  Verification: ✓ Generated C compiles and runs, 7 tests pass:
    - Test 1: No effect (body returns 3)
    - Test 2: Perform+resume (1+42=43)
    - Test 5: Chained ops (5+10=15)
    - Test 6: Payload use (5*2=10)
    - Test 7: Nested expr (2*(3+5)=16)

### P2: Unify Condition/Restart with Effects [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I14-p2-unify-conditions-effects
  Objective: Reimplement conditions/restarts on top of effect system.
  Where: `runtime/src/condition.c`, `runtime/src/condition.h`, `runtime/src/effect.c`
  What was done:
    1. Added `TAG_CONDITION` to omni.h for Obj-wrapped conditions
    2. Added `EFFECT_CONDITION` built-in effect type (RECOVERY_ONE_SHOT mode)
    3. Implemented `mk_condition_obj()` - wraps Condition in Obj with TAG_CONDITION
    4. Implemented `condition_signal()` - performs EFFECT_CONDITION (resumable)
    5. Implemented `condition_error()` - performs EFFECT_FAIL (non-resumable)
    6. Implemented `condition_signal_with_message()` - convenience wrapper
    7. Implemented `condition_from_obj()` - extracts Condition from Obj
  Design: Adapter approach - preserves condition type hierarchy but uses effects
         for signaling. Handlers using `handle` form can catch condition effects.
  Note: Lisp-level `signal`, `handler-case`, `restart-case` require separate codegen work.

### P3: Iterator-Generator Integration [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I14-p3-iterator-generator
  Objective: Implement iterators using generator continuations.
  Where: `runtime/src/iterator.c`, `runtime/src/memory/continuation.c`
  What was done:
    1. Added `TAG_GENERATOR` to omni.h for Obj-wrapped generators
    2. Implemented `mk_generator_obj()` in continuation.c - wraps Generator in Obj
    3. Added generator-based iterator functions to iterator.c:
       - `prim_make_generator(producer)` - create generator from closure
       - `prim_generator_next(gen)` - get next value using `generator_next`
       - `prim_generator_done(gen)` - check if generator exhausted
       - `prim_yield(value)` - yield value from within generator
       - `is_generator(obj)` - helper to check if Obj is a generator
       - `prim_iter_next_unified(iter)` - supports both pair and generator iterators
       - `prim_take_unified(n, seq)` - supports both pair and generator sequences
  Design: Existing pair-based iterators preserved for backwards compatibility.
          Generator-based iteration uses delimited continuations for suspend/resume.
  Note: Lisp codegen for `make-generator` and `yield` forms needs separate work.

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

## Issue 16: Region-RC Dynamic Closure Integration [DONE] (Review Needed)

**Objective:** Make closures, HOFs, and generic methods region-aware for proper Region-RC lifecycle.

**Completed:** 2026-01-15

### P0: Region-Aware Closure Types [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I16-p0-closure-fn-region
  Objective: Add region-aware closure function pointer type and struct support.
  Where: `runtime/include/omni.h`, `runtime/src/runtime.c`
  What was done:
    1. Added `ClosureFnRegion` typedef: `Obj* (*)(Region*, Obj** captures, Obj** args, int argc)`
    2. Updated `Closure` struct with union for `fn`/`fn_region` + `region_aware` flag
    3. Added `mk_closure_region()` with store barriers for captured values
    4. Added `call_closure_region()` that passes caller region to closure

### P1: Region-Aware HOFs [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I16-p1-region-aware-hofs
  Objective: Create region-aware versions of higher-order functions.
  Where: `runtime/src/runtime.c`
  What was done:
    1. Added `list_map_region`, `list_filter_region`, `list_fold_region`, `list_foldr_region`
    2. HOFs use `call_closure_region` to propagate region context
    3. All allocations within HOFs use caller's region

### P2: HOF Codegen [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I16-p2-hof-codegen
  Objective: Update codegen to emit region-aware HOF calls.
  Where: `csrc/codegen/codegen.c`
  What was done:
    1. Updated `map`, `filter`, `fold`, `foldr` handlers to emit `list_*_region` calls
    2. Pass `_local_region` as first argument to all HOFs

### P3: Inline Lambda Closure Wrappers [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I16-p3-lambda-closure-wrapper
  Objective: Generate closure wrappers for inline lambdas in HOF calls.
  Where: `csrc/codegen/codegen.c`
  What was done:
    1. Added `is_lambda_expr()` to detect lambda forms (lambda, fn, λ)
    2. Added `count_lambda_params()` to count parameters
    3. Added `codegen_lambda_as_closure()` that generates:
       - Static lambda function (`_lambda_N`)
       - Trampoline function (`_lambda_N_tramp`) adapting signature to `ClosureFnRegion`
       - `mk_closure_region()` call to wrap trampoline
    4. HOF handlers now detect inline lambdas and use closure wrapper
    5. Removed arrow syntax (`->`) - superseded by `(fn [params] body)`
  Verification: `(map (fn [x] (* x 2)) '(1 2 3))` → `(2 4 6)` ✓

### P4: Generic Method Region Support [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I16-p4-generic-method-region
  Objective: Add region-aware method support to generic functions.
  Where: `runtime/src/generic.c`
  What was done:
    1. Updated `MethodInfo` struct with `impl_region` union + `region_aware` flag
    2. Added `generic_add_method_region()` for region-aware methods
    3. Added `call_generic_region()` / `omni_generic_invoke_region()`

### P5: prim_deep_put Store Barriers [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I16-p5-deep-put-barriers
  Objective: Implement deep_set with store barriers for nested mutations.
  Where: `runtime/src/runtime.c`
  What was done:
    1. Implemented `deep_set()` with `omni_store_repair()` for all mutations
    2. Supports alist-style structures: `((key1 . val1) (key2 . val2) ...)`
    3. Recursive path traversal with barrier at each level

---

## Issue 17: Remaining Integration Tasks [DONE] (Review Needed)

**Objective:** Complete integration of Region-RC with continuation, effect, and codegen systems.

**Reference (read first):**
- Issue 14 (Continuation Infrastructure)
- Issue 9 (Algebraic Effects)
- `runtime/src/memory/continuation.c`
- `runtime/src/effect.c`

### P0: Array Growth with Region Realloc [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I17-p0-array-growth
  Objective: Implement proper array growth that preserves region membership.
  Where: `runtime/src/runtime.c:852-884`
  What was done:
    1. Implemented `array_grow()` function that allocates new data buffer in same region
    2. Updated `array_push()` to grow array when full (2x capacity, minimum 8)
    3. Elements copied directly (no barrier needed, already safe)
    4. Old data becomes garbage, reclaimed with region
  Verification: ✓ Test confirms array grows from 4 → 8 → 16 → 32 with 20 pushes

### P1: Store Barrier Merge Path [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I17-p1-store-barrier-merge
  Objective: Add merge path to store barrier (currently only transmigrates).
  Where: `runtime/src/runtime.c:979-1006`
  What was done: (Already implemented in prior work)
    1. `omni_store_repair()` checks `get_merge_threshold()` for large regions
    2. Calls `region_merge_safe()` for regions above threshold
    3. Falls back to transmigrate for smaller regions or merge failure
  Verification: ✓ Tests "store barrier checks merge threshold" and "merge safe basic" pass

### P2: Print Functions Completion [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I17-p2-print-completion
  Objective: Complete print functions for container types.
  Where: `runtime/src/runtime.c:1247-1330`
  What was done:
    1. Implemented `print_array()` - iterates and prints `[elem1 elem2 ...]`
    2. Implemented `print_tuple()` - iterates and prints `{elem1 elem2 ...}`
    3. Implemented `print_dict()` - iterates buckets and prints `#{:key val ...}`
    4. Implemented `print_named_tuple()` - prints `#(:key val ...)`
    5. Fixed `alloc_obj_region()` to preserve original tag for unmapped types (TAG_KEYWORD)
  Verification: ✓ `[1 2 3]`, `{10 20 30}`, `#{:a 100}`, `#(:x 5)` all print correctly

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
| 14 | DONE (Review) | **Continuation infrastructure** (P0-P3 DONE; trampolines intentionally separate) |
| 15 | TODO | **Arena & memory system enhancements** |
| 16 | DONE (Review) | **Region-RC dynamic closure integration** |
| 17 | DONE (Review) | **Remaining integration tasks (array growth, print functions)** |

**Completed issues:** See `TODO_COMPLETED.md` for Issues 2, 3, 4, 5, 7.
