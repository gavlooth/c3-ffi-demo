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

- [TODO] Label: T-wire-multiple-dispatch
- [TODO] Label: T-wire-parametric-subtyping
- [TODO] Label: T-wire-pika-api
- [TODO] Label: T-wire-deep-put
- [TODO] Label: T-wire-iter-ext
- [DONE] Label: T-wire-reader-macros
- [N/A] Label: T-wire-modules

---

## Phase 18: Standard Library & Ecosystem [STANDBY]

**Objective:** Transform OmniLisp from a "Core Language" to a "Usable Ecosystem".

- [TODO] Label: T-stdlib-pika-regex
- [TODO] Label: T-stdlib-string-utils
- [R] Label: T-stdlib-math-numerics

---

## Phase 19: Syntax Alignment (Strict Character Calculus) [ACTIVE]

**Objective:** Align the entire compiler and runtime with the **Strict Character Calculus** rules and the **Julia-aligned Type System**.

- [TODO] Label: T-syntax-uniform-define
  Objective: Refactor `analyze_define` to support the Uniform Definition syntax and `OMNI_TYPE_LIT`.
  Reference: docs/SYNTAX_REVISION.md
- [TODO] Label: T-syntax-metadata-where
  Objective: Implement Metadata (`^`) and Constraints (`^:where`) processing.
- [TODO] Label: T-syntax-slot-refactor
  Objective: Update `define` and `let` to use strict Slot `[]` for parameters and bindings.
- [TODO] Label: T-syntax-type-algebra
  Objective: Implement Flow Constructors for dynamic types (`union`, `fn`).
- [TODO] Label: T-syntax-val-tag
  Objective: Implement `#val` reader tag support.
- [TODO] Label: T-syntax-pika-dual-mode
  Objective: Implement dual-mode parsing (`parser->ast` and `parser->string`) in the Pika engine.
- [TODO] Label: T-syntax-wire-dispatch
  Objective: Wire the new Type System with the Multiple Dispatch engine.

---

## Phase 20: Meta-Programming (Tower & Macros) [STANDBY]

**Objective:** Restore the "Collapsing Towers of Interpreters" semantics and implement a hygienic macro system.

- [TODO] Label: T-tower-handlers
  Objective: Implement the Meta-Environment Handler System in the C analyzer.
- [TODO] Label: T-tower-lift-run
  Objective: Implement `lift`, `run`, and `EM` (Escape to Meta).
- [TODO] Label: T-macro-hygienic
  Objective: Implement a hygienic macro system with template-based expansion.

---

## Phase 21: Object System (Multiple Dispatch) [STANDBY]

**Objective:** Complete the Julia-style object system integration.

- [TODO] Label: T-obj-dispatch-logic
  Objective: Implement static specificity sorting for multiple dispatch.
- [TODO] Label: T-obj-value-type-types
  Objective: Implement Value-to-Type (`value->type(x)`) and Type-Dispatch (`Type(T)`) Kinds.
- [TODO] Label: T-obj-variance
  Objective: Implement Covariance and Invariance rules in the subtyping engine.

---

## Phase 22: Core Module & Syntax Refinement [STANDBY]

**Objective:** Bootstrap the `core` module and implement final syntax refinements.

- [TODO] Label: T-core-collect
  Objective: Implement unified `collect` with symbol dispatch.
- [TODO] Label: T-core-bootstrap
  Objective: Bootstrap the `core` module (Int, String, Array, List, collect).
- [R] Label: T-mod-isolation
  Objective: Implement the `module` and `import` system.
- [R] Label: T-syntax-piping
  Objective: Implement the Pipe operator `|>` and leading dot accessors `.field`.

---

## Phase 23: Advanced Control Flow (Continuations & Trampolines) [STANDBY]

**Objective:** Restore stack-safe mutual recursion and delimited continuations.

- [R] Label: T-ctrl-trampoline
  Objective: Implement `bounce` and `trampoline` for stack-safe mutual recursion.
- [R] Label: T-ctrl-delimited
  Objective: Implement `prompt` and `control` delimited continuations.

---



## Phase 24: Performance Optimization (Benchmark-Driven) [STANDBY]



**Objective:** Optimize RC-G memory model based on comprehensive benchmark results. **MANDATORY:** Every implementation in this phase must include a benchmark report comparing Before vs. After performance.



- [TODO] Label: T-opt-bitmap-cycle-detection

  Objective: Replace uthash with bitmap-based cycle detection in transmigrate.c.

  Verification: Benchmark report showing transmigration speedup (target: 10-100x).

- [TODO] Label: T-opt-region-splicing

  Objective: Implement O(1) region splicing for functional programming patterns.

  Verification: Benchmark report showing O(1) time for result-only regions.

- [TODO] Label: T-opt-region-pool

  Objective: Pool/reuse regions to reduce creation overhead.

  Verification: Benchmark report showing reduction in region lifecycle overhead.

- [TODO] Label: T-opt-inline-allocation

  Objective: Inline allocate small objects (< 64 bytes) in region.

  Verification: Benchmark report showing parity with raw malloc speed for small objects.

- [TODO] Label: T-opt-specialized-constructors

  Objective: Add specialized constructors for hot patterns (Lists, Trees).

  Verification: Benchmark report showing reduction in allocation count and time for sorting/traversal.

- [TODO] Label: T-opt-transmigrate-incremental

  Objective: Implement incremental transmigration for large graphs.

  Verification: Benchmark report for sparse access on large graphs.

- [TODO] Label: T-opt-batch-alloc

  Objective: Batch allocate multiple objects in a single call.

  Verification: Benchmark report showing speedup for complex object initialization.

- [TODO] Label: T-opt-hot-inline

  Objective: Inline hot paths in critical functions using `static inline` or attributes.

  Verification: Benchmark report showing instruction-count reduction in hot loops.
