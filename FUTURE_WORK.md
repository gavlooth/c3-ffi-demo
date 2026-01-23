# Future Optimizations & Validation Testing

## Overview

This document outlines future work for OmniLisp's ASAP memory management system.
All items are optional enhancements - the core system is complete and functional.

---

## Recently Completed: Region Infrastructure (2026-01-04)

All 5 backlog items from ARCHITECTURE.md have been implemented with full test coverage:

| # | Feature | Tests | Implementation |
|---|---------|-------|----------------|
| 12 | Linear/Offset Regions for FFI | 17 | `runtime/src/memory/region_core.c` |
| 13 | Pluggable Region Backends (IRegion) | 17 | `runtime/src/memory/region_core.c` |
| 14 | Weak Ref Control Blocks | 21 | `runtime/src/memory/region_core.c` |
| 15 | Transmigration/Isolation | 17 | `runtime/src/memory/transmigrate.c` |
| 16 | External Handle Indexing | 27 | `runtime/src/memory/handle.c` |

**Total: 99 new tests**, all passing. See `runtime/tests/test_*.c` for details.

### Key APIs Added

**IRegion Vtable** (pluggable allocators):
- `iregion_new_arena()`, `iregion_new_linear()`, `iregion_new_offset()`, `iregion_new_pool()`
- `iregion_alloc()`, `iregion_free_one()`, `iregion_free_all()`
- `iregion_freeze()`, `iregion_clone()`, `iregion_serialize()`

**Weak Reference Control Blocks**:
- `weak_cb_new()`, `weak_cb_invalidate()`, `weak_cb_is_valid()`
- `weak_handle_new()`, `weak_handle_lock()`, `weak_handle_free()`
- `weak_table_new()`, `weak_table_register()`, `weak_table_invalidate()`

**Transmigration**:
- `transmigrate()`, `region_splice()`, `arena_promote()`
- `bitmap_test_and_set()`, `region_tether_start()`, `region_tether_end()`

**External Handles** (FFI + determinism):
- `handle_system_init()`, `handle_alloc_obj()`, `handle_free_obj()`
- `handle_from_obj()`, `handle_deref_obj()`, `handle_borrow_create()`

---

## Part 1: Future Optimizations

### O.1 Compile-Time RC Elimination (High Impact) ✅ INTEGRATED

**Source**: Lobster language, Perceus (PLDI 2021)

**Problem**: Many `region_retain`/`region_release` pairs are provably unnecessary.

**Status**: ✅ **INTEGRATED** (2026-01-18)

**Implementation**: `csrc/codegen/codegen.c` (Perceus Reuse & Lobster RC Elision section)
- `codegen_can_elide_inc()` / `codegen_can_elide_dec()` - query elision analysis
- `codegen_emit_dec_ref()` / `codegen_emit_inc_ref()` - emit with elision
- `emit_ownership_free()` - RC cases check elision before emitting dec_ref
- `omni_codegen_emit_cfg_frees()` - CFG-aware frees with elision

**Elision Classes** (from `csrc/analysis/analysis.h`):
- `RC_ELIDE_BOTH` - Variable is unique or stack-allocated (skip both inc/dec)
- `RC_ELIDE_DEC` - Variable is arena/pool-allocated (bulk free, skip dec)
- `RC_ELIDE_INC` - Variable is borrowed (don't own it, skip inc)
- `RC_REQUIRED` - Must use RC (shared heap reference)

**Generated Code**:
```c
// Before (no elision)
dec_ref(x);

// After (elision enabled)
/* RC_ELIDED: dec_ref(x) - unique */
```

| Task | Description | Status |
|------|-------------|--------|
| O.1.1 | Extend uniqueness analysis to handle more patterns | ✅ Done |
| O.1.2 | Track borrowed references through function calls | ✅ Done |
| O.1.3 | Eliminate retain/release pairs for provably-unique paths | ✅ Done |
| O.1.4 | Add statistics: "X% of RC operations eliminated" | TODO |

**Expected Impact**: 30-50% reduction in RC operations for typical code.

---

### O.2 Active Reuse Transformation (High Impact) ✅ INTEGRATED

**Source**: Perceus FBIP (Functional But In-Place)

**Problem**: Reuse analysis exists but codegen doesn't actively transform code.

**Status**: ✅ **INTEGRATED** (2026-01-18)

**Implementation**: `csrc/codegen/codegen.c` (Perceus Reuse & Lobster RC Elision section)
- `codegen_get_reuse()` - query reuse candidates from analysis
- `codegen_emit_reuse_int()` - emit REUSE_OR_NEW_INT if candidate found
- `codegen_emit_reuse_float()` - emit REUSE_OR_NEW_FLOAT if candidate found
- `codegen_emit_reuse_cell()` - emit REUSE_OR_NEW_CELL if candidate found
- `codegen_int()` / `codegen_float()` - integrated with reuse check

**Generated Code**:
```c
// Before (fresh allocation)
Obj* y = mk_int(42);

// After (reusing x's memory)
Obj* y = REUSE_OR_NEW_INT(_local_region, x, 42);
```

The `REUSE_OR_NEW_*` macros (defined in runtime preamble) check at runtime if
the old object has rc==1, and if so, reuse its memory instead of allocating.

| Task | Description | Status |
|------|-------------|--------|
| O.2.1 | Integrate reuse analysis into `codegen_let` | ✅ Done |
| O.2.2 | Track allocation sizes at compile time | ✅ Done |
| O.2.3 | Generate `reuse_as_*` calls instead of `mk_*` | ✅ Done |
| O.2.4 | Handle partial reuse (larger block → smaller allocation) | TODO |

**Expected Impact**: Near-zero allocation for many functional patterns.

---

### O.3 DPS Code Generation (Medium Impact)

**Source**: Destination-Passing Style (FHPC 2017)

**Problem**: List-returning functions allocate intermediate results.

**Current State**: DPS runtime concepts exists but not auto-generated.

**Proposed Enhancement**:
```c
// Before: allocates result list
Value* result = map(f, xs);

// After: caller provides destination
Value* result;
map_into(&result, f, xs);  // No intermediate allocation
```

| Task | Description | Effort |
|------|-------------|--------|
| O.3.1 | Identify functions returning fresh allocations | Small |
| O.3.2 | Generate `_dps` variants for eligible functions | Large |
| O.3.3 | Transform call sites to use DPS when beneficial | Large |
| O.3.4 | Handle nested DPS (e.g., `map(f, filter(g, xs))`) | Large |

**Expected Impact**: Constant memory for pipelines like `map(f, filter(g, xs))`.

---

### O.4 Region Inference (Medium Impact)

**Source**: MLKit, Tofte-Talpin

**Problem**: Per-object static free has overhead; related objects could share lifetime.

**Proposed Enhancement**:
```c
// Before: individual frees
free_obj(a); free_obj(b); free_obj(c);

// After: region-based
Region* r = region_create();
a = region_alloc(r, ...);
b = region_alloc(r, ...);
c = region_alloc(r, ...);
region_exit(r);  // One operation frees all
```

| Task | Description | Effort |
|------|-------------|--------|
| O.4.1 | Infer region parameters for functions | Large |
| O.4.2 | Group allocations with same lifetime into regions | Large |
| O.4.3 | Generate region allocation/deallocation | Medium |
| O.4.4 | Optimize: merge small regions, inline single-object regions | Medium |

**Expected Impact**: Reduced free() overhead for related allocations.

---

### O.5 Interprocedural Ownership Propagation (Medium Impact)

**Source**: SOTER (ownership transfer inference)

**Problem**: Summaries exist but aren't used to optimize callers.

**Current State**: `summary_registry.c` has param/return ownership.

**Proposed Enhancement**:
```scheme
;; If we know `process` consumes its argument:
(let [x (alloc)]
  (process x)    ;; x consumed by callee
  ;; No free needed here - callee freed it
  )
```

| Task | Description | Effort |
|------|-------------|--------|
| O.5.1 | Query summaries in `codegen_let` cleanup | Medium |
| O.5.2 | Skip caller-side free for consumed parameters | Small |
| O.5.3 | Skip retain for parameters callee will borrow | Small |
| O.5.4 | Infer summaries for recursive functions | Large |

**Expected Impact**: Fewer redundant RC ops at call boundaries.

---

### O.6 Non-Lexical Lifetimes (Low Impact)

**Source**: Rust/Polonius

**Problem**: Variables freed at scope end, not at last use.

**Current State**: Liveness analysis exists in `csrc/analysis/liveness.c`.

**Proposed Enhancement**:
```scheme
(let [x (alloc)]
  (use x)        ;; Last use of x
  (long-computation)  ;; x could be freed here, not at scope end
  result)
```

| Task | Description | Effort |
|------|-------------|--------|
| O.6.1 | Use liveness info to free at last-use point | Medium |
| O.6.2 | Handle control flow (different free points per branch) | Large |
| O.6.3 | Ensure correctness with effects/exceptions | Medium |

**Expected Impact**: Earlier memory reclamation, reduced peak memory.

---

### O.7 Stack Allocation for Non-Escaping Values (Low Impact)

**Source**: Escape analysis literature

**Problem**: Non-escaping values still heap-allocated.

**Current State**: Escape analysis exists in `csrc/analysis/escape.c`.

**Proposed Enhancement**:
```c
// Before
Value* x = mk_int(42);  // Heap allocation
use(x);
region_release(r);

// After (proven non-escaping)
Value x_storage;
Value* x = init_int_stack(&x_storage, 42);  // Stack allocation
use(x);
// No free needed - automatic on scope exit
```

| Task | Description | Effort |
|------|-------------|--------|
| O.7.1 | Use escape analysis to identify stack candidates | Small |
| O.7.2 | Generate stack allocation for `ESCAPE_NONE` | Medium |
| O.7.3 | Handle aggregates (pairs, closures) on stack | Medium |

**Expected Impact**: Zero heap allocation for many local variables.

---

## Part 2: Validation Testing

### V.1 Memory Safety (Critical)

**Objective**: Prove generated C code has no memory errors.

| Test | Tool | Description |
|------|------|-------------|
| V.1.1 | Valgrind | `valgrind --leak-check=full --error-exitcode=1` |
| V.1.2 | ASan | Compile with `-fsanitize=address` |
| V.1.3 | MSan | Compile with `-fsanitize=memory` |
| V.1.4 | UBSan | Compile with `-fsanitize=undefined` |

**Test Cases**:
```scheme
;; Leak test: all allocations freed
(let [x (cons 1 2)]
  (car x))

;; Double-free test: no double frees
(let [x (cons 1 2)]
  (let [y x]  ;; Alias
    (car y)))

;; Use-after-free test: no dangling pointers
(let [x (cons 1 2)]
  (let [y (car x)]
    ;; x freed here
    y))  ;; y should still be valid
```

---

### V.2 Concurrency Safety (Critical)

**Objective**: Prove thread-safe code has no data races.

| Test | Tool | Description |
|------|------|-------------|
| V.2.1 | TSan | Compile with `-fsanitize=thread` |
| V.2.2 | Helgrind | Valgrind's thread error detector |

**Test Cases**:
```scheme
;; Ownership transfer: no race
(let [ch (chan 1)
      x (cons 1 2)]
  (spawn (lambda []
        (let [y (recv ch)]
          (display y))))
  (send ch x)
  nothing)

;; Shared variable: region tethering
(let [x (cons 1 2)]
  (spawn (lambda [] (display x)))
  (spawn (lambda [] (display x)))
  x)
```

---

### V.3 Correctness Testing (High Priority)

**Objective**: Compiled code produces same results as interpreter.

| Test | Description |
|------|-------------|
| V.3.1 | Run all 100+ existing tests |
| V.3.2 | Compare interpreter vs compiled output |
| V.3.3 | Property-based testing (QuickCheck-style) |

---

### V.4 Performance Benchmarks (Medium Priority)

**Objective**: Measure and track performance.

| Benchmark | Description |
|-----------|-------------|
| V.4.1 | Allocation rate (allocs/sec) |
| V.4.2 | Peak memory usage |
| V.4.3 | RC operation count |
| V.4.4 | Reuse hit rate |
| V.4.5 | Comparison vs manual memory management |

---

### V.5 Stress Testing (Medium Priority)

**Objective**: Find edge cases and resource limits.

| Test | Description |
|------|-------------|
| V.5.1 | Deep recursion (stack overflow handling) |
| V.5.2 | Large allocations (memory exhaustion) |
| V.5.3 | Many threads/fibers (resource limits) |
| V.5.4 | Complex cycles (region destruction stress) |
| V.5.5 | Long-running (memory stability over time) |

---

### V.6 Fuzzing (Low Priority)

**Objective**: Find crashes via random input.

| Test | Tool | Description |
|------|------|-------------|
| V.6.1 | AFL | Fuzz the parser |
| V.6.2 | AFL++ | Fuzz compiled programs |
| V.6.3 | libFuzzer | Fuzz runtime functions |

---

## Part 2: Language & Tooling Goodies (Non-Core)

### L.1 Language Server (LSP)
- Implementation in C or Rust for high performance
- Go-to-definition, find references, rename
- Hover docs and type info

---

## References

1. Perceus: Garbage Free Reference Counting with Reuse (PLDI 2021)
2. ASAP: As Static As Possible memory management (Proust 2017)
3. Destination-Passing Style for Efficient Memory Management (FHPC 2017)
4. Region-Based Memory Management (Tofte-Talpin 1997)
5. Lobster Memory Management (aardappel.github.io)
6. SOTER: Inferring Ownership Transfer (UIUC)
7. Rust Borrow Checker / Polonius