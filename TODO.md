# OmniLisp TODO

## Review Directive

**All newly implemented features must be marked with `[R]` (Review Needed) until explicitly approved by the user.**

- When an agent completes implementing a feature, mark it `[R]` (not `[DONE]`)
- `[R]` means: code is written and working, but awaits user review/approval
- After user approval, change `[R]` to `[DONE]`
- Workflow: `[TODO]` → implement → `[R]` → user approves → `[DONE]`

---

## STANDBY Notice
Phases marked with **[STANDBY]** are implementation-complete or architecturally stable but are currently deprioritized to focus on **Phase 17: Runtime Bridge & Feature Wiring**. This ensures the language is usable and "wired" before further optimizing the memory substrate.

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
  Where: `runtime/src/memory/region_value.h`, `runtime/src/memory/region_value.c`
  What to change:
    - [x] Create `region_value.h` with declarations for all `mk_*_region` functions.
    - [x] Implement `region_value.c` with all region-aware constructors.
  How to verify: Unit test creating various values in a region.
  Acceptance:
    - All `mk_*_region` functions work correctly.

- [TODO] Label: T-fix-transmigrate
  Objective: Fix type mismatches in transmigrate.c to enable it in the runtime build.
  Reference: docs/REGION_RC_ARCHITECTURE.md
  Where: `runtime/src/memory/transmigrate.c`, `runtime/Makefile`
  Why:
    The current implementation uses high-level `Value` types (T_*) but the runtime uses low-level `Obj` types (TAG_*). This prevents compilation.
  What to change:
    1.  Update `transmigrate.c` to include `omni.h` and use `Obj*` instead of `Value*`.
    2.  Replace `T_INT`, `T_CELL` etc. with `TAG_INT`, `TAG_PAIR` etc.
    3.  Update struct field access (e.g., `cell.car` -> `a`, `cell.cdr` -> `b`).
    4.  Enable `transmigrate.c` in `runtime/Makefile`.
  Implementation Details:
    ```c
    void* transmigrate(void* root, Region* src_region, Region* dest_region) {
        // ...
        Obj* old_obj = (Obj*)root;
        // ...
        switch (old_obj->tag) {
            case TAG_PAIR:
                // ...
        }
    }
    ```
  How to verify: `make -C runtime` succeeds with `transmigrate.c` enabled.
  Acceptance:
    - `transmigrate.c` compiles without errors.
    - `libomni.a` includes transmigration logic.

- [DONE] Label: T-rcg-cleanup
  Objective: Remove obsolete runtime components.

---

## Phase 14: ASAP Region Management (Static Lifetimes) [STANDBY]

**Objective:** Implement static liveness analysis to drive region deallocation, with RC as a fallback only.
**Reference:** `docs/STATIC_REGION_LIFETIME_ARCHITECTURE.md`

- [TODO] Label: T-asap-region-liveness
...
- [TODO] Label: T-asap-region-main
...

---

## Phase 15: Branch-Level Region Narrowing

**Objective:** Reduce RC overhead by keeping branch-local data out of RC-managed regions.
**Reference:** `docs/BRANCH_LEVEL_REGION_NARROWING.md`

- [DONE] Label: T1-analysis-scoped-escape
  Objective: Implement hierarchical, branch-level escape analysis to support "Region Narrowing", allowing temporary variables in non-escaping branches to be allocated on the stack or scratchpad instead of the parent region.
  Reference: docs/BRANCH_LEVEL_REGION_NARROWING.md
  Where: csrc/analysis/analysis.h, csrc/analysis/analysis.c

  Implementation completed:
    - Added `EscapeTarget` enum (NONE, PARENT, RETURN, GLOBAL)
    - Added `ScopedVarInfo` struct to track variables per scope
    - Added `ScopeInfo` struct to build scope tree
    - Implemented `omni_scope_enter()`, `omni_scope_exit()`, `omni_scope_add_var()`
    - Implemented `omni_scope_mark_escape()`, `omni_scope_get_escape_target()`
    - Implemented `omni_scope_find_var()`, `omni_scope_get_cleanup_vars()`
    - Implemented `omni_analyze_scoped_escape()` entry point
    - Updated `analyze_if_scoped()` to track branch scopes
    - Updated `analyze_let_scoped()` to track let scopes
    - Updated `analyze_lambda_scoped()` for lambda scopes
    - Added debug printing: `omni_scope_print_tree()`

  Acceptance:
    - `AnalysisContext` tracks scope depth via `current_scope` and `root_scope`
    - Variables correctly identify their defining scope
    - Variables that do not leave a branch are flagged as non-escaping *for that branch*
    - Variables that leave a branch (e.g. via return or assignment to outer var) are flagged as escaping

- [DONE] Label: T2-codegen-narrowing
  Objective: Update code generation to utilize scoped escape analysis, emitting stack/scratch allocations for non-escaping variables and inserting precise cleanup at branch exits.
  Reference: docs/BRANCH_LEVEL_REGION_NARROWING.md
  Where: csrc/codegen/codegen.c, csrc/codegen/codegen.h

  Implementation completed:
    - Added `omni_codegen_emit_narrowed_alloc()` for allocation routing
    - Added `omni_codegen_emit_scope_cleanup()` for branch cleanup
    - Added `omni_codegen_if_narrowed()` for narrowed if codegen
    - Added `omni_codegen_let_narrowed()` for narrowed let codegen
    - Allocation routing: ESCAPE_TARGET_NONE → stack, others → region_alloc
    - Scope cleanup emits free_tree/free_obj for non-escaping variables
    - Shape-aware cleanup based on `ScopedVarInfo->shape`

  Acceptance:
    - Code generator queries escape analysis via `omni_scope_get_escape_target()`
    - Non-escaping variables result in stack-allocated C code (e.g., `Obj _stack_x = {0};`)
    - Escaping variables use region_alloc
    - Scope exit points contain specific cleanup for locals

  Test file created: tests/test_phase15_narrowing.omni

---

## Phase 15b: Adaptive Region Sizing [STANDBY]

**Objective:** Scale region overhead with actual data size to reduce penalty for small lifecycle groups.
**Reference:** `docs/BRANCH_LEVEL_REGION_NARROWING.md` (Section 12)

- [TODO] Label: T-adaptive-size-classes
...
- [TODO] Label: T-adaptive-non-atomic-tiny
...

---

## Phase 16: Advanced Region Optimization [STANDBY]

**Objective:** Implement high-performance transmigration and tethering algorithms.
**Reference:** `docs/ADVANCED_REGION_ALGORITHMS.md`

- [DONE] Label: T-opt-bitmap-cycle
...
- [DONE] Label: T-opt-tether-cache
...

---

## Phase 17: Runtime Bridge & Feature Wiring

**Objective:** Complete the wiring of core language features into the compiler codegen and modern runtime.
**Note:** Legacy evaluator `src/runtime` has been removed. Features must be implemented via compiler intrinsics (`csrc/codegen`) or runtime library functions (`runtime/src`).

- [R] Label: T-wire-multiple-dispatch
  Objective: Implement robust Multiple Dispatch selection logic.
  Where: `csrc/codegen/codegen.c`, `runtime/src/generic.c`, `runtime/include/omni.h`
  Implementation completed:
    - Added TAG_GENERIC and TAG_KIND to ObjTag enum in runtime/include/omni.h
    - Added Closure, Generic, MethodInfo, and Kind structures for multiple dispatch
    - Added ClosureFn typedef: Obj* (*ClosureFn)(Obj** captures, Obj** args, int argc)
    - Implemented full multiple dispatch runtime in runtime/src/generic.c:
      - mk_kind() - Create type objects for parametric types
      - mk_generic() - Create generic function objects
      - generic_add_method() - Add methods to generic functions with specificity scoring
      - call_generic() - Dispatch to the most specific method
      - kind_equals() - Structural equality for Kinds
      - is_subtype() - Subtype checking with covariance
      - compute_specificity() - Method specificity scoring (concrete > parametric)
    - Added region_get_or_create() and region_strdup() to region_core module
    - Updated codegen_define in csrc/codegen/codegen.c:
      - Detect function redefinitions via check_function_defined()
      - Track definitions via track_function_definition()
      - On first definition: emit TAG_CLOSURE with trampoline
      - On redefinition: emit TAG_GENERIC and add method
      - Generate trampoline functions to convert ClosureFn to Region* signature
    - Excluded transmigrate.c from build (pre-existing Value/Obj type mismatch issue)
  How to verify: Run `tests/unwired_features.omni`; verify correct method selected for different Kinds.
  Notes:
    - Runtime library (libomni.a) builds successfully with generic.c included
    - Test file uses type annotation syntax {Int} which may need parser support
    - Codegen generates trampolines that convert between ClosureFn and Region* calling conventions

- [R] Label: T-wire-parametric-subtyping
  Objective: Implement subtyping logic for parametric Kinds.
  Where: `runtime/src/generic.c` (`is_subtype` implementation)
  Implementation completed:
    - is_subtype() already implements recursive parametric comparison
    - Supports covariance: (List A) <: (List B) if A <: B (recursive)
    - Handles Any type: all types are subtypes of Any
    - Structural equality via kind_equals()
  How to verify: `(type? {List Int} {List Any})` returns `true`.
  Notes:
    - The implementation is complete in generic.c
    - Test requires type annotation syntax support in parser

- [R] Label: T-wire-pika-api
  Objective: Bridge Pika Grammar engine to Lisp environment.
  Where: `runtime/src/runtime.c`, `runtime/include/omni.h`
  Implementation completed:
    - Added prim_pika_parse_grammar() - Parse grammar spec into grammar object
    - Added prim_pika_match() - Match string against grammar rule
    - Both functions are wrapped in #ifdef OMNI_PIKA_ENABLED for conditional compilation
    - Returns list of matches as (start . matched-text) pairs
  How to verify: Run `tests/unwired_features.omni`; verify grammar definition can be used to parse.
  Notes:
    - Runtime library builds successfully with Pika primitives
    - To enable: compile with -DOMNI_PIKA_ENABLED and link pika_c library
    - Test syntax `(define [grammar simple] [greeting "hello"])` requires parser support for grammar definition

- [R] Label: T-wire-deep-put
  Objective: Support recursive path mutation in `put!`.
  Where: `csrc/codegen/codegen.c` (`codegen_put_bang`), `runtime/src/runtime.c`
  Implementation completed:
    - Added prim_deep_put() to runtime - takes root, dotted path string, and new value
    - Updated codegen_put_bang() to detect symbol paths and call prim_deep_put()
    - Generates: `root = prim_deep_put(root, "path.sub.field", new_value);`
  How to verify: Mutate a nested dict field and verify the update reflects in the parent.
  Notes:
    - Runtime library builds successfully
    - prim_deep_put() is a skeleton - needs proper dict/record get/set operations
    - Test syntax `(put! data.user.address.city "New York")` requires parser to treat dots as part of symbol name

- [R] Label: T-wire-iter-ext
  Objective: Implement `iterate` and `take` for infinite sequence support.
  Where: `runtime/src/runtime.c`
  Implementation completed:
    - Added prim_iterate() - Create lazy iterator from function + seed
    - Added prim_iter_next() - Get next value from iterator
    - Added prim_take() - Take n elements from iterator or list
  How to verify: `(collect-list (take 5 (iterate inc 0)))` returns `(0 1 2 3 4)`.
  Notes:
    - Runtime library builds successfully
    - Uses TAG_BOX as iterator marker (could use dedicated TAG_ITERATOR in future)
    - Test requires collect-list primitive or equivalent

- [R] Label: T-wire-reader-macros
  Objective: Implement Sign `#` dispatch for literals.
  Where: `csrc/parser/parser.c` (Reader macros handled at compile time)
  Implementation completed:
    - Added R_NAMED_CHAR rule for named character literals
    - Added act_named_char() action function
    - Supports: #\newline (10), #\space (32), #\tab (9), #\return (13), #\nul (0), #\bell (7), #\backspace (8), #\escape (27), #\delete (127)
    - Updated EXPR rule to include R_NAMED_CHAR
  How to verify: Read `#\newline` and verify it produces character code 10.
  Notes:
    - Parser changes complete; needs compile-time verification
    - Note: Standard Lisp syntax is #\newline (with backslash), but parser may need adjustment for exact format

- [N/A] Label: T-wire-modules
  Objective: Implement module system wiring.
  Status: Deferred (N/A) - Requires architectural work
  - Module namespacing in symbol table/codegen mangling
  - Import statement for selective symbol mapping
  - Recommended: Implement as dedicated phase with proper design

---

## Phase 18: Standard Library & Ecosystem

**Objective:** Transform OmniLisp from a "Core Language" to a "Usable Ecosystem" by providing a battery-included standard library and robust tooling.
**Reference:** docs/LANGUAGE_COMPLETENESS_REPORT.md

- [TODO] Label: T-stdlib-pika-regex
  Objective: Implement a high-level Regex API that capitalizes on Pika's superiority (left-recursion, non-anchored matching).
  Reference: docs/PATTERN_SYNTAX.md
  Where: `runtime/src/regex.c`, `runtime/include/omni.h`, `csrc/codegen/codegen.c`
  Why:
    Pika is more powerful than PCRE (handles left-recursion) and faster than standard PEG for many cases. We need to expose this power via a standard `(re-match pattern string)` API.
  What to change:
    1.  **High-level API:** Register `re-match`, `re-find-all`, `re-split`, `re-replace` in the runtime.
    2.  **Pika Integration:** These functions should use the existing `omni_compile_pattern()` and `omni_pika_match()` under the hood.
    3.  **Advanced Pika Features:** Expose `find_all` which returns ALL non-overlapping matches anywhere in the string (Pika's unique strength).
  Implementation Details:
    ```c
    /* runtime/src/regex.c */
    Obj* prim_re_find_all(Obj* pattern_str, Obj* input_str) {
        /* 1. Compile pattern using Pika */
        /* 2. Execute pika_memo_get_non_overlapping_matches */
        /* 3. Return a list of matched strings */
    }
    ```
  How to verify: `(re-find-all \"[0-9]+\" \"abc123def456\")` => `(\"123\" \"456\")`.
  Acceptance:
    - Regex functions registered and working in runtime.
    - `re-find-all` correctly identifies multiple matches.
    - Handles Pika-specific grammars if passed as pattern.

- [TODO] Label: T-stdlib-string-utils
  Objective: Implement core string manipulation utilities (Split, Join, Replace, Case, Trim).
  Reference: docs/LANGUAGE_COMPLETENESS_REPORT.md
  Where: `runtime/src/string_utils.c`, `runtime/include/omni.h`
  What to change:
    1.  **Utilities:** Implement `string-split`, `string-join`, `string-replace`, `string-trim`, `string-upcase`, `string-lowcase`.
    2.  **Normalization:** Implement basic UTF-8 aware length and slicing if possible.
  How to verify: `(string-split \",\" \"a,b,c\")` => `(\"a\" \"b\" \"c\")`.
  Acceptance:
    - Comprehensive string library available.
    - All functions handle `nothing` and empty strings gracefully.

- [TODO] Label: T-stdlib-math-numerics
  Objective: Complete the math/numeric library with statistics and random number generation.
  Reference: docs/LANGUAGE_COMPLETENESS_REPORT.md
  Where: `runtime/src/math_ext.c`, `runtime/include/omni.h`
  What to change:
    1.  **Statistics:** Add `mean`, `median`, `stddev` for arrays/lists.
    2.  **Random:** Add `rand` (0..1), `rand-int`, `rand-seed!`.
    3.  **Constants:** Ensure `pi`, `e`, `inf`, `nan` are available.
  How to verify: `(mean [1 2 3 4 5])` => `2.0`.
  Acceptance:
    - Numeric library covers common statistical and random needs.
    - Functions work on both Arrays and Lists.

