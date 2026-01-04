# OmniLisp Runtime Optimization Plan

## Executive Summary

The OmniLisp compiler has sophisticated analysis infrastructure that is **underutilized in code generation**. This plan addresses the gaps to achieve **2-5x performance improvement**.

## Current Performance Baseline

| Benchmark | vs Raw C | Target After Optimization |
|-----------|----------|---------------------------|
| Fibonacci | 23-35x slower | 5-10x slower |
| Sum loop | 10x slower | 3-5x slower |
| List creation | 7x slower | 2-3x slower |
| Memory/int | 6x more (48 vs 8 bytes) | 1x (unboxed) or 6x (boxed) |

## Optimization Items (Priority Order)

### Phase 1: Quick Wins (1-2 hours each)

#### 1.1 Liveness Analysis Integration [CRITICAL]
**Status**: Analysis exists (161 lines), ZERO usage in codegen
**Location**: `pkg/analysis/liveness.go`
**Impact**: O(n) earlier frees in loops

**Current behavior**:
```scheme
(let ((x (make-large-list)))
  (process x)         ; x last used here
  (do-more-stuff)     ; x still alive!
  (even-more))        ; x freed here at scope exit
```

**After fix**:
```scheme
(let ((x (make-large-list)))
  (process x)         ; x last used here
  (free x)            ; FREE IMMEDIATELY
  (do-more-stuff)
  (even-more))
```

**Changes needed**:
- [ ] Call `LivenessAnalyzer.Analyze()` in codegen
- [ ] Use `GetLastUse()` to emit free at last use point
- [ ] Modify let binding cleanup to use liveness info

#### 1.2 Stack Allocation for Non-Escaping Values [HIGH]
**Status**: `mk_int_stack()` exists in runtime, escape analysis exists, NOT CONNECTED
**Location**: `pkg/analysis/escape.go`, `pkg/codegen/codegen.go`
**Impact**: Eliminate heap allocation for ~80% of temporaries

**Current behavior**:
```c
Obj* x = mk_int(42);  // HEAP allocation, 48 bytes
// ... use x ...
dec_ref(x);           // HEAP free
```

**After fix**:
```c
Obj* x = mk_int_stack(42);  // STACK allocation, instant
// ... use x ...
// NO FREE NEEDED - stack cleanup automatic
```

**Changes needed**:
- [ ] Check `EscapeNone` in codegen before allocation
- [ ] Emit `mk_int_stack()` / `mk_float_stack()` for non-escaping
- [ ] Skip dec_ref for stack-allocated values
- [ ] Add `mk_float_stack()`, `mk_char_stack()` to runtime

#### 1.3 Complete RC Elision Verification [HIGH]
**Status**: Partial - borrowing fixed, but not all paths checked
**Impact**: 20-40% fewer RC operations

**Changes needed**:
- [ ] Audit all `inc_ref` emission points
- [ ] Audit all `dec_ref` emission points
- [ ] Ensure `ShouldEmitIncRef/DecRef` called everywhere
- [ ] Add statistics output for optimization rates

### Phase 2: Medium Effort (4-8 hours each)

#### 2.1 Ownership-Driven Code Generation [MEDIUM]
**Status**: `pkg/analysis/ownership.go` exists (372 lines), barely used
**Impact**: Semantic correctness + optimization

**Ownership classes**:
- `OwnerLocal`: Can free at scope exit
- `OwnerBorrowed`: Don't free, don't inc_ref
- `OwnerTransferred`: Don't free (callee owns)
- `OwnerShared`: Use RC
- `OwnerWeak`: Weak reference (cycles)

**Changes needed**:
- [ ] Run ownership analysis in codegen pipeline
- [ ] Use ownership to drive allocation strategy
- [ ] Use ownership for RC decision making

#### 2.2 Region-Based Bulk Deallocation [MEDIUM]
**Status**: `pkg/analysis/region.go` exists (151 lines), skeleton only
**Impact**: O(1) deallocation for related objects

**Concept**:
```c
Region* r = region_enter();
Obj* a = region_alloc(r, mk_int(1));
Obj* b = region_alloc(r, mk_int(2));
Obj* c = region_alloc(r, mk_pair(a, b));
// ... use a, b, c ...
region_exit(r);  // FREE ALL AT ONCE - O(1)
```

**Changes needed**:
- [ ] Implement region allocation in runtime
- [ ] Identify region boundaries in codegen
- [ ] Emit region_enter/exit at scope boundaries

#### 2.3 Interprocedural RC Propagation [MEDIUM]
**Status**: Function summaries exist, not fully propagated
**Impact**: Cross-function optimization

**Changes needed**:
- [ ] Propagate consumed/borrowed annotations across calls
- [ ] Inline RC decisions for known callees
- [ ] Build call graph for optimization

### Phase 3: Advanced (8+ hours)

#### 3.1 GenRef Integration for Borrowed References [LOW-MEDIUM]
**Status**: Complete implementation in runtime/memory, zero codegen use
**Impact**: O(1) use-after-free detection instead of RC

**Concept**: For borrowed references, use generational validation instead of RC:
```c
// Instead of:
inc_ref(borrowed_val);
use(borrowed_val);
dec_ref(borrowed_val);

// Use:
GenRef* ref = genref_from_obj(borrowed_val, "borrow");
Obj* val = genref_get(ref);  // O(1) validation
use(val);
genref_release(ref);  // No RC overhead
```

**Changes needed**:
- [ ] Add GenRef path for borrowed parameters
- [ ] Emit genref_get() for borrowed value access
- [ ] Benchmark to verify benefit

#### 3.2 Unboxed Primitives [HIGH EFFORT, HIGH IMPACT]
**Status**: Not implemented
**Impact**: 6x memory reduction, much better cache

**Concept**: Store primitives directly, not as Obj*:
```c
// Instead of:
Obj* x = mk_int(42);  // 48 bytes

// Use:
int64_t x = 42;  // 8 bytes, register-friendly
```

**Changes needed**:
- [ ] Type inference to identify primitive-only code
- [ ] Dual codegen paths (boxed vs unboxed)
- [ ] Conversion at boundaries

#### 3.3 SIMD/Vectorization for Numeric Code [FUTURE]
**Status**: Not planned
**Impact**: 4-8x for numeric workloads

### Phase 4: Dynamic Cycle Collection (Symmetric RC) -> Evolved to Component Tethering
- [x] Implement `SymComponent` unit of reclamation.
- [x] Implement thread-local slab pools for components.
- [x] Implement `sym_tether_begin/end` for zero-cost access.
- [x] Functional verification via 450+ tests.

## Implementation Order

```
Week 1: Quick Wins
├── Day 1: Liveness integration
├── Day 2: Stack allocation
└── Day 3: RC elision audit

Week 2: Medium Effort
├── Day 1-2: Ownership integration
├── Day 3-4: Region allocation
└── Day 5: Statistics & testing

Week 3: Advanced (optional)
├── GenRef integration
└── Unboxed primitives exploration
```

## Success Metrics

| Metric | Current | Target |
|--------|---------|--------|
| RC ops per function | 100% | <40% |
| Heap allocations | 100% | <30% |
| Benchmark vs C | 10-35x | 3-10x |
| Memory per int | 48 bytes | 8-48 bytes |

## Files to Modify

| File | Changes |
|------|---------|
| `pkg/codegen/codegen.go` | Main integration point |
| `pkg/codegen/runtime.go` | Add stack allocation functions |
| `pkg/analysis/liveness.go` | May need API adjustments |
| `runtime/src/runtime.c` | Add mk_float_stack, mk_char_stack |
| `runtime/include/omnilisp.h` | Export new functions |
