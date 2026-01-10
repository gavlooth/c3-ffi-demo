# OmniLisp Implementation Roadmap

## Complete Step-by-Step Plan

This document provides actionable steps to implement all future optimizations and validation testing for the pure C99 + POSIX implementation.

---

# Phase 1: Critical Validation

## Step 1.1: Valgrind Integration

**Goal**: Verify no memory leaks in generated C code.

### 1.1.1 Create test harness
```bash
# Create directory for validation scripts
mkdir -p tests/validation
```

### 1.1.2 Create comprehensive test cases
- Implement C programs that exercise all memory shapes (TREE, DAG, CYCLIC).
- Automate Valgrind execution across the test suite.

---

## Step 1.2: AddressSanitizer Integration

**Goal**: Detect buffer overflows, use-after-free, double-free.

### 1.2.1 Configure Build
- Update `Makefile` to support `make asan` which adds `-fsanitize=address`.

---

## Step 1.3: ThreadSanitizer Integration

**Goal**: Detect data races in concurrent code.

### 1.3.1 Configure Build
- Update `Makefile` to support `make tsan` which adds `-fsanitize=thread`.

---

## Step 1.4: Correctness Testing

**Goal**: Compiled code produces same results as interpreter.

---

# Phase 2: RC Elimination

## Step 2.1: Enhance Uniqueness Analysis

**Goal**: Identify more cases where region RC can be skipped.

### 2.1.1 Add uniqueness propagation
- Update `csrc/analysis/uniqueness.c` to track unique ownership through function calls.

### 2.1.2 Add borrowed reference tracking
- Distinguish between owning and non-owning pointers in the analysis pass.

### 2.1.3 Generate optimized RC operations
- Update `csrc/codegen/codegen.c` to elide `region_retain`/`region_release` for unique variables.

---

# Phase 3: Active Reuse

## Step 3.1: Integrate Reuse into Codegen

**Goal**: Transform `free(x); y = alloc()` → `y = reuse(x)`.

---

# Phase 4: Interprocedural Ownership

## Step 4.1: Use Summaries at Call Sites

**Goal**: Optimize RC based on callee ownership behavior.

---

# Phase 5: DPS Code Generation

## Step 5.1: Identify DPS Candidates

**Goal**: Find functions that return fresh allocations.

---

# Phase 6: Region Inference

**Goal**: Group allocations with same lifetime into static regions.

---

# Phase 7: Performance Benchmarks

**Goal**: Measure and track performance using C-based benchmarking tools.

---

# Phase 8: Stress Testing

**Goal**: Find edge cases and resource limits (recursion, large allocations, concurrency).

---

# Phase 9: Memory Architecture Enhancements ✅ ALL COMPLETE (2026-01-04)

Implementation lives in `runtime/src/memory/` with 99 tests.

- [x] 9.1 Linear/Offset Regions for Serialization & FFI
- [x] 9.2 Pluggable Region Backends (IRegion)
- [x] 9.3 Weak Ref Control Blocks (Merge‑Friendly)
- [x] 9.4 Transmigration / Isolation on Region Escape
- [x] 9.5 External Handle Indexing (FFI + Determinism)

---

# Phase 10: Component-Level Scope Tethering (Island Reclamation) ✅ COMPLETE (2026-01-04)

All phases of the component-level tethering system have been **implemented and verified**.

**Total: 454 functional tests passed in `runtime/tests/`**