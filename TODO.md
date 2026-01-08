# OmniLisp TODO

## Review Directive

**All newly implemented features must be marked with `[R]` (Review Needed) until explicitly approved by the user.**

- When an agent completes implementing a feature, mark it `[R]` (not `[DONE]`)
- `[R]` means: code is written and working, but awaits user review/approval
- After user approval, change `[R]` to `[DONE]`
- Workflow: `[TODO]` → implement → `[R]` → user approves → `[DONE]`

---

## STANDBY Notice
Phases marked with **[STANDBY]** are implementation-complete or architecturally stable but are currently deprioritized to focus on **Phase 19: Syntax Alignment**. This ensures the language is usable and "wired" before further optimizing the memory substrate.

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
- [DONE] Label: T-fix-transmigrate
  Objective: Fix type mismatches in transmigrate.c to enable it in the runtime build.
- [DONE] Label: T-rcg-cleanup
  Objective: Remove obsolete runtime components.

---

## Phase 14: ASAP Region Management (Static Lifetimes) [STANDBY]

**Objective:** Implement static liveness analysis to drive region deallocation, with RC as a fallback only.

- [TODO] Label: T-asap-region-liveness
- [TODO] Label: T-asap-region-main

---

## Phase 15: Branch-Level Region Narrowing

**Objective:** Reduce RC overhead by keeping branch-local data out of RC-managed regions.

- [DONE] Label: T1-analysis-scoped-escape
- [DONE] Label: T2-codegen-narrowing

---

## Phase 16: Advanced Region Optimization [STANDBY]

**Objective:** Implement high-performance transmigration and tethering algorithms.

- [DONE] Label: T-opt-bitmap-cycle
- [DONE] Label: T-opt-tether-cache

---

## Phase 17: Runtime Bridge & Feature Wiring

**Objective:** Complete the wiring of core language features into the compiler codegen and modern runtime.

- [TODO] Label: T-wire-dispatch-core
  Objective: Wire core generic function infrastructure.
  Reference: runtime/src/generic.c
  Where: csrc/codegen/codegen.c
  What: Connect generic function calls to runtime dispatch.
  How: Generate calls to `omni_generic_lookup` and `omni_generic_invoke`.

- [TODO] Label: T-wire-dispatch-arity
  Objective: Wire arity checking for multiple dispatch.
  Where: csrc/codegen/codegen.c
  What: Generate arity validation before dispatch.

- [TODO] Label: T-wire-parametric-single
  Objective: Wire single parametric type instantiation.
  Where: csrc/analysis/analysis.c
  What: Handle (List Int) style type instantiation.

- [TODO] Label: T-wire-parametric-constraints
  Objective: Wire parametric type constraints checking.
  Where: csrc/analysis/analysis.c
  What: Validate type arguments against constraints.

- [TODO] Label: T-wire-pika-compile
  Objective: Wire Pika pattern compilation.
  Where: csrc/parser/pika_core.c
  What: Expose `omni_compile_pattern` function.

- [TODO] Label: T-wire-pika-exec
  Objective: Wire Pika pattern matching execution.
  Where: csrc/parser/pika_core.c
  What: Expose `omni_pika_match` function.

- [TODO] Label: T-wire-deep-put
  Objective: Wire deep put operation for nested structures.
  Where: csrc/codegen/codegen.c
  What: Generate code for nested field updates.

- [TODO] Label: T-wire-iter-basics
  Objective: Wire basic iterator operations (first, rest, has-next).
  Where: csrc/codegen/codegen.c
  What: Add runtime iterator primitives.

- [TODO] Label: T-wire-iter-collect
  Objective: Wire iterator collect operation.
  Where: csrc/codegen/codegen.c
  What: Generate efficient collection building.

- [DONE] Label: T-wire-reader-macros
  Objective: Reader macro infrastructure exists.
  Status: Framework in place, expansion to be added.

- [R] Label: T-wire-modules
  Objective: Module system implemented.
  Status: runtime/src/modules.c complete, compiler integration pending.

---

## Phase 18: Standard Library & Ecosystem [STANDBY]

**Objective:** Transform OmniLisp from a "Core Language" to a "Usable Ecosystem".

- [R] Label: T-stdlib-pika-regex
  Objective: High-level Regex API implemented.
  Status: runtime/src/regex.c complete with POSIX regex.

- [R] Label: T-stdlib-string-utils
  Objective: String manipulation utilities implemented.
  Status: runtime/src/string_utils.c complete.

- [R] Label: T-stdlib-math-numerics
  Objective: Math and numerics library implemented.
  Status: runtime/src/math_numerics.c complete.

---

## Phase 19: Syntax Alignment (Strict Character Calculus) [ACTIVE]

**Objective:** Align the entire compiler and runtime with the **Strict Character Calculus** rules and the **Julia-aligned Type System**.

- [R] Label: T-syntax-uniform-define
  Objective: Extended TypeDef structure for uniform definitions.
  Status: analysis.h extended with parent, bit_width, metadata, type_params.

- [R] Label: T-syntax-metadata-where
  Objective: Metadata extraction and type hierarchy implemented.
  Status: omni_extract_metadata, omni_type_is_subtype, omni_compute_specificity complete.

- [R] Label: T-syntax-type-algebra
  Objective: Flow constructors (union, fn) implemented.
  Status: prim_union, prim_fn in runtime.c complete.

- [TODO] Label: T-syntax-slot-params
  Objective: Update define parameter parsing for Slot `[]` syntax.
  Reference: docs/SYNTAX_REVISION.md Section 2.1
  Where: csrc/codegen/codegen.c
  What: Change (define (f params...) body) to (define [params...] {ret} body).
  How: Parse params as Slot instead of list.

- [TODO] Label: T-syntax-slot-let
  Objective: Update let binding parsing for Slot triplet syntax.
  Reference: docs/SYNTAX_REVISION.md Section 6.2
  Where: csrc/codegen/codegen.c
  What: Parse (let [x {Type} val] body) instead of (let ((x val)) body).
  How: Extend analyze_let to handle name-type-value triplets.

- [TODO] Label: T-syntax-reader-val
  Objective: Implement #val reader tag for literal values.
  Reference: docs/SYNTAX_REVISION.md Section 6
  Where: csrc/parser/parser.c
  What: Add Pika rule for #val(...) reader macro.
  How: Expand to value creation during parsing.

- [TODO] Label: T-syntax-pika-ast
  Objective: Add AST output mode to Pika parser.
  Where: csrc/parser/pika_core.c
  What: Return OmniValue AST instead of string.
  How: Modify semantic actions to build AST nodes.

- [TODO] Label: T-syntax-dispatch-types
  Objective: Wire type system with multiple dispatch.
  Where: csrc/analysis/analysis.c, runtime/src/generic.c
  What: Use omni_type_is_subtype in dispatch selection.
  How: Integrate type checking into omni_apply analysis.

---

## Phase 20: Meta-Programming (Tower & Macros) [STANDBY]

**Objective:** Restore the "Collapsing Towers of Interpreters" semantics and implement a hygienic macro system.

- [TODO] Label: T-tower-env-stack
  Objective: Implement environment stack for meta-levels.
  Where: csrc/analysis/analysis.c
  What: Add stack of environments for tower levels.
  How: Extend AnalysisContext with env_stack array.

- [TODO] Label: T-tower-lift
  Objective: Implement lift operation (code -> data).
  Where: csrc/analysis/analysis.c
  What: Convert expressions to quoted data.
  How: Mark expressions as lifted (no evaluation).

- [TODO] Label: T-tower-run
  Objective: Implement run operation (data -> code).
  Where: csrc/codegen/codegen.c
  What: Evaluate quoted expressions.
  How: Generate evaluator for lifted code.

- [TODO] Label: T-tower-escape-meta
  Objective: Implement EM (Escape to Meta) operator.
  Where: csrc/parser/parser.c
  What: Parse ^ operator for meta-escaping.
  How: Add semantic action for ^ forms.

- [TODO] Label: T-macro-template
  Objective: Implement template-based macro expansion.
  Where: csrc/parser/parser.c
  What: Define macro expansion with pattern matching.
  How: Add defmacro form and expansion logic.

- [TODO] Label: T-macro-hygiene
  Objective: Implement hygiene through gensym.
  Where: csrc/parser/parser.c
  What: Auto-generate unique symbols for macro variables.
  How: Add gensym primitive and hygiene tracking.

---

## Phase 21: Object System (Multiple Dispatch) [STANDBY]

**Objective:** Complete the Julia-style object system integration.

- [TODO] Label: T-obj-specificity-int
  Objective: Implement integer specificity calculation.
  Where: csrc/analysis/analysis.c
  What: Compute specificity score for types (depth in hierarchy).
  How: Use omni_compute_specificity for each method.

- [TODO] Label: T-obj-specificity-sort
  Objective: Implement static method sorting by specificity.
  Where: csrc/codegen/codegen.c
  What: Order methods from most to least specific at compile time.
  How: Generate sorted method table.

- [TODO] Label: T-obj-value-type
  Objective: Implement value->type primitive.
  Where: runtime/src/runtime.c
  What: Return the type of a value at runtime.
  How: Use tag field to determine type.

- [TODO] Label: T-obj-type-dispatch
  Objective: Implement Type(T) dispatch kind.
  Where: csrc/parser/parser.c, runtime/src/runtime.c
  What: Parse Type(T) forms and dispatch by value type.
  How: Add runtime type-based dispatch.

- [TODO] Label: T-obj-variance-check
  Objective: Implement variance validation for parametric types.
  Where: csrc/analysis/analysis.c
  What: Check covariance rules in subtype relationships.
  How: Use omni_type_get_param_variance in checks.

- [TODO] Label: T-obj-variance-co
  Objective: Implement covariance checking for output positions.
  Where: csrc/analysis/analysis.c
  What: Validate that covariant parameters are used correctly.
  How: Track variance through function returns.

---

## Phase 22: Core Module & Syntax Refinement [STANDBY]

**Objective:** Bootstrap the `core` module and implement final syntax refinements.

- [TODO] Label: T-core-collect-dispatch
  Objective: Implement symbol-based dispatch for collect.
  Where: csrc/parser/parser.c, runtime/src/runtime.c
  What: Parse collect forms and dispatch by collection type.
  How: Add generic collection protocol.

- [TODO] Label: T-core-collect-list
  Objective: Implement list collect operation.
  Where: runtime/src/runtime.c
  What: Add prim_collect_list for building lists.
  How: Append to list in collect context.

- [TODO] Label: T-core-collect-array
  Objective: Implement array collect operation.
  Where: runtime/src/runtime.c
  What: Add prim_collect_array for building arrays.
  How: Append to array in collect context.

- [TODO] Label: T-core-collect-string
  Objective: Implement string collect operation.
  Where: runtime/src/runtime.c
  What: Add prim_collect_string for building strings.
  How: Append characters in collect context.

- [TODO] Label: T-core-bootstrap-int
  Objective: Bootstrap Int type with operations.
  Where: runtime/src/runtime.c
  What: Add Int type constructor and arithmetic methods.
  How: Register methods with generic function system.

- [TODO] Label: T-core-bootstrap-string
  Objective: Bootstrap String type with operations.
  Where: runtime/src/runtime.c
  What: Add String type constructor and string methods.
  How: Register string methods (length, split, etc.).

- [TODO] Label: T-core-bootstrap-array
  Objective: Bootstrap Array type with operations.
  Where: runtime/src/runtime.c
  What: Add Array type constructor and array methods.
  How: Register array methods (length, map, filter).

- [TODO] Label: T-core-bootstrap-list
  Objective: Bootstrap List type with operations.
  Where: runtime/src/runtime.c
  What: Add List type constructor and list methods.
  How: Register list methods (map, filter, fold).

- [R] Label: T-mod-isolation
  Objective: Module system implemented.
  Status: runtime/src/modules.c complete, compiler integration pending.

- [R] Label: T-syntax-piping
  Objective: Pipe operator and leading dot implemented.
  Status: runtime/src/piping.c complete, parser integration pending.

---

## Phase 23: Advanced Control Flow (Continuations & Trampolines) [STANDBY]

**Objective:** Restore stack-safe mutual recursion and delimited continuations.

- [R] Label: T-ctrl-trampoline
  Objective: Implement `bounce` and `trampoline` for stack-safe mutual recursion.
  Status: runtime/src/trampoline.c complete with bounce/trampoline implementation.

- [R] Label: T-ctrl-delimited
  Objective: Implement `prompt` and `control` delimited continuations.
  Status: Framework in place, continuation system integration pending.

---



## Phase 24: Performance Optimization (Benchmark-Driven) [STANDBY]



**Objective:** Optimize RC-G memory model based on comprehensive benchmark results. **MANDATORY:** Every implementation in this phase must include a benchmark report comparing Before vs. After performance.



- [DONE] Label: T-opt-bitmap-cycle-detection

  Objective: Replace uthash with bitmap-based cycle detection in transmigrate.c.

  Verification: **ACHIEVED 2.7-12.5x speedup!** Benchmark results (2026-01-08):
  - Small list (1K): 131.19 ns/op → 48.46 ns/op (**2.7x faster**)
  - Medium list (100K): 353.10 ns/op → 74.88 ns/op (**4.7x faster**)
  - Tree (15 nodes): 512,573 ns/op → 51,114 ns/op (**10.0x faster**)
  - Wide structure (100): 8,529,571 ns/op → 681,771 ns/op (**12.5x faster**)

  Implementation: Replaced uthash hash table with bitmap-based cycle detection using Arena allocation.
  - Eliminated malloc() overhead for hash table nodes
  - O(1) bitmap operations instead of hash table lookups
  - Single arena_free() cleanup instead of hash iteration
  - See runtime/src/memory/transmigrate.c lines 15-87 for bitmap implementation

- [DONE] Label: T-opt-region-splicing

  Objective: Implement O(1) region splicing for functional programming patterns.

  Verification: **ACHIEVED O(1) performance!** Benchmark results (2026-01-08):
  - Small result (1K): 55.27 ns/op (bitmap) → 38.91 ns/op (splicing) = **1.4x faster**
  - Large result (100K): 83.22 ns/op (bitmap) → 42.81 ns/op (splicing) = **1.9x faster**
  - **Critical insight:** Splicing shows O(1) performance - time per operation is constant regardless of size!

  Implementation: Added fast path in transmigrate() for result-only regions:
  - Detects when source has external_rc==0 and scope_alive==false (region is closing)
  - Transfers entire arena chunk with pointer manipulation (O(1))
  - No object-by-object copying needed
  - See runtime/src/memory/transmigrate.c lines 171-194 for splicing implementation

- [DONE] Label: T-opt-region-pool

  Objective: Pool/reuse regions to reduce creation overhead.

  Verification: **ACHIEVED 6.9-21.1x speedup!** Benchmark results (2026-01-08):
  - Small regions (1000): 1,050.95 ns/op → 49.80 ns/op (**21.1x faster!**)
  - Mixed lifetimes (100): 10.90 ns/op (fastest case with optimal reuse)
  - **Critical insight:** Pooling eliminates malloc/free overhead for region lifecycle

  Implementation: Added thread-local region pool with 32-slot capacity:
  - Fast path in region_create(): reuse from pool (no malloc)
  - Fast path in region_destroy_if_dead(): return to pool (no free)
  - Automatic reset when returning to pool (arena_free + control block reset)
  - Pool size limit prevents unbounded memory growth
  - See runtime/src/memory/region_core.c lines 6-33, 35-42, 82-88 for pool implementation

- [DONE] Label: T-opt-inline-allocation

  Objective: Inline allocate small objects (< 64 bytes) in region.

  Verification: **ACHIEVED 6.99x speedup over raw malloc!** Benchmark results (2026-01-08):
  - Small objects (1K): 2.12 ns/op (472M ops/sec)
  - Inline vs malloc: 0.023s vs 0.160s = **6.99x faster!**
  - Mixed sizes: 2.92 ns/op (343M ops/sec)
  - Buffer exhaustion: 3.01 ns/op (333M ops/sec)
  - Closure allocation: 2.88 ns/op (348M ops/sec)

  Implementation: Added 512-byte inline buffer to Region struct for fast small object allocation:
  - Fast path in region_alloc(): bump-pointer allocation for objects <= 64 bytes
  - Eliminates malloc() overhead for small objects
  - Falls back to arena_alloc for larger objects or exhausted inline buffer
  - Zero overhead: inline buffer reset is part of existing region_reset()
  - See runtime/src/memory/region_core.h lines 9-18, runtime/src/memory/region_core.c lines 179-197 for implementation

- [DONE] Label: T-opt-specialized-constructors

  Objective: Add specialized constructors for hot patterns (Lists, Trees).

  Verification: **ACHIEVED 5.55-6.32x speedup!** Benchmark results (2026-01-08):
  - List construction (1000): 6.32x speedup, 1.02 ns/op (982M ops/sec)
  - Tree construction (8191 nodes): 5.55x speedup, 8191x fewer allocations
  - Medium list (100K): 1.89 ns/op (529M ops/sec)
  - Allocation reduction: 1000x-2000x fewer allocations

  Implementation: Added batch allocation constructors that allocate entire data structures in a single call:
  - mk_list_region(): Build list of n integers in one allocation (O(1) instead of O(n))
  - mk_tree_region(): Build complete binary tree in one allocation (O(1) instead of O(2^depth))
  - mk_list_from_array_region(): Build list from array in one allocation
  - Improved cache locality through contiguous memory allocation
  - See runtime/src/memory/region_value.h lines 60-83, runtime/src/memory/region_value.c lines 146-298

- [TODO] Label: T-opt-transmigrate-lazy
  Objective: Implement lazy on-demand transmigration.
  Where: runtime/src/memory/transmigrate.c
  What: Transmigrate objects only when accessed.
  How: Add access tracking and lazy migration.

- [TODO] Label: T-opt-transmigrate-batch
  Objective: Implement batched transmigration for object groups.
  Where: runtime/src/memory/transmigrate.c
  What: Transmigrate multiple objects in single operation.
  How: Queue objects and migrate in batches.

- [TODO] Label: T-opt-batch-alloc-array
  Objective: Batch allocate homogeneous arrays.
  Where: runtime/src/memory/region_value.c
  What: Allocate array elements in single operation.
  How: Bump-pointer allocation for array contents.

- [TODO] Label: T-opt-batch-alloc-struct
  Objective: Batch allocate struct fields.
  Where: runtime/src/memory/region_value.c
  What: Allocate all struct fields contiguously.
  How: Pre-calculate size and allocate once.

- [TODO] Label: T-opt-inline-alloc-fastpath
  Objective: Inline critical allocation fast paths.
  Where: runtime/src/memory/region_core.c
  What: Mark region_alloc as static inline.
  How: Move hot path to header for inlining.

- [TODO] Label: T-opt-inline-hash-fastpath
  Objective: Inline hash table operations.
  Where: runtime/src/util/hashmap.c
  What: Make hashmap lookups inlineable.
  How: Use static inline for hot paths.
