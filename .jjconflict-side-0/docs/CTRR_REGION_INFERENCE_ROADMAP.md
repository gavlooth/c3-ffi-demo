# CTRR Region Inference Roadmap

**Status:** Compiler improvement roadmap for CTRR  
**Applies to:** `csrc/analysis/`, `csrc/codegen/`  
**Last Updated:** 2026-01-10  
**Related:** `docs/CTRR.md` (CTRR contract), `runtime/docs/REGION_RC_MODEL.md` (Region-RC model), `runtime/docs/REGION_ACCOUNTING.md` (accounting for heuristic tuning)

---

## 1) Problem Statement

### 1.1 Current Limitations

OmniLisp's current CTRR implementation has these limitations:

1. **Lexical region boundaries only:** Regions are created/destroyed at lexical scope boundaries (`{` and `}`), not at actual variable liveness points.
2. **Always allocate in current region:** Even when a variable provably never escapes, we allocate in a region (could be stack).
3. **Always transmigrate on escape:** When a value escapes a closing region, we always transmigrate (never pre-allocate in the destination).
4. **Monolithic regions:** Regions cannot be split or merged without copying entire contents.

### 1.2 Consequences

| Limitation | Impact |
|------------|--------|
| Lexical boundaries | Late reclamation (variables kept alive until scope end) |
| Always allocate in region | Unnecessary allocation overhead for non-escaping values |
| Always transmigrate | Copy overhead for escaping values |
| Monolithic regions | High transmigration cost for large graphs |

### 1.3 Research Inspiration

This roadmap draws from three key papers:

1. **Better Static Memory Management** (Aiken/Fähndrich/Levien, PLDI 1995)
   - Introduces **non-lexical region inference**
   - Regions can end at last-use, not scope-end
   - Enables earlier reclamation

2. **Region-Based Memory Management** (Tofte/Talbin, 1997)
   - Formalizes **region polymorphism**
   - Regions can be **split** and **merged**
   - Enables copy-free region transformations

3. **Spegion** (ECOOP 2025)
   - Introduces **interprocedural region inference**
   - Enables **allocate-into-outliving-region** optimization
   - Reduces transmigrate overhead

---

## 2) Roadmap Phases

### Phase 1: Non-Lexical Region Ends (Liveness-Driven Reclamation)

**Objective:** Free regions as early as possible (at last-use of all variables), not just at scope exit.

**Problem:**
```lisp
(let ((x 42)
      (y (pair 1 2))
      (z (pair 3 4)))
  (do-something-with x)
  (do-something-with y)
  ;; z is dead here but region still alive!
  (do-something-else))
;; Region reclaimed here (late!)
```

**Solution (After Phase 1):**
```lisp
(let ((x 42)
      (y (pair 1 2))
      (z (pair 3 4)))
  (do-something-with x)
  (do-something-with y)
  ;; z is last-used here → region_exit now!
  (region_exit)
  (do-something-else))
```

**Implementation:**

1. **Extend liveness analysis** to track last-use of each region
2. **Insert `region_exit()`** at last-use point (before scope exit)
3. **Verify:** No variables used after their region exits

**Files to modify:**
- `csrc/analysis/liveness.c` - Track region liveness
- `csrc/codegen/codegen.c` - Emit `region_exit()` at last-use

**Expected benefit:** 10-30% reduction in peak memory usage

---

### Phase 2: Stack Allocation for Non-Escaping Values

**Objective:** Allocate non-escaping values on the stack, bypassing region allocation entirely.

**Problem:**
```lisp
(let ((x (pair 1 2)))
  (do-something-with x))
;; x never escapes, but still allocated in region (overhead!)
```

**Solution (After Phase 2):**
```lisp
(let ((x (pair 1 2)))  ; x allocated on stack!
  (do-something-with x))
;; x freed automatically at stack unwind
```

**Implementation:**

1. **Escape analysis** already identifies `ESCAPE_TARGET_NONE`
2. **Code generation change:**
   - If escape = `ESCAPE_TARGET_NONE`: Stack allocate
   - Else: Region allocate
3. **Stack allocation API:**
   ```c
   // New API in runtime/src/memory/region_core.h
   void* stack_alloc_region(Region* r, size_t size);
   // Uses stack pointer, automatically reclaimed on unwind
   ```

**Files to modify:**
- `csrc/analysis/escape.c` - Already tracks `ESCAPE_TARGET_NONE`
- `csrc/codegen/codegen.c` - Emit stack alloc calls
- `runtime/src/memory/region_core.h` - Add `stack_alloc_region()`

**Expected benefit:** 20-40% reduction in allocation overhead

---

### Phase 3: Allocate-Into-Outliving-Region (Spegion-style)

**Objective:** When a value provably escapes into a known destination, allocate directly in the destination region, avoiding transmigrate.

**Problem:**
```lisp
;; Caller has long-lived region "caller_region"
(define (make-pair caller_region)
  (let ((region (region-create))
        (x (pair 1 2)))
    (region-exit region)
    x))  ;; x escapes → transmigrate needed!

;; Generated code:
Region* local = region_create();
Obj* x = mk_pair_region(local, 1, 2);
region_exit(local);
Obj* out = transmigrate(x, local, caller_region);  // OVERHEAD!
return out;
```

**Solution (After Phase 3):**
```lisp
;; Compiler infers x escapes into caller_region
;; Allocate directly in caller_region!

(define (make-pair caller_region)
  (let ((region (region-create))
        (x (pair 1 2)))  ;; x allocated in caller_region!
    (region-exit region)
    x))

;; Generated code:
Region* local = region_create();
Obj* x = mk_pair_region(caller_region, 1, 2);  // No transmigrate!
region_exit(local);
return x;
```

**Implementation:**

1. **Interprocedural escape analysis:**
   - Track which region a value escapes into
   - If destination is known and "outliving", use it for allocation
2. **Code generation:**
   - Replace `region_create()` for local region with `pass-through-region`
   - Allocate escaping values in destination region directly
3. **Verification:**
   - Ensure destination region outlives local region
   - Check for cycles in region graph

**Files to modify:**
- `csrc/analysis/escape.c` - Track escape destination
- `csrc/codegen/codegen.c` - Allocate in destination region

**Expected benefit:** 10-50% reduction in transmigrate overhead

---

### Phase 4: Splittable Regions

**Objective:** Support region splitting and merging without copying, enabling efficient transformations.

**Problem:**
```lisp
;; Two parts of a function need different lifetimes:
(let ((region (region-create))
      (x (pair 1 2))  ; Used early
      (y (pair 3 4)))  ; Used late
  (do-something-with x)
  ;; x is dead, but y keeps region alive
  (do-something-with-y))

;; Current: Can't split region → both x and y keep region alive
```

**Solution (After Phase 4):**
```lisp
(let ((region (region-create))
      (x (pair 1 2))
      (y (pair 3 4)))
  (do-something-with x)
  ;; SPLIT: Create new region for y only
  (let ((region2 (region-split region)))  ; x in region, y moves to region2
    (region-exit region)  ; x reclaimed!
    (do-something-with-y)))
```

**Implementation:**

1. **Region splitting API:**
   ```c
   // New API in runtime/src/memory/region_core.h
   Region* region_split(Region* src, size_t split_point);
   // Splits region at split_point, moves later objects to new region
   ```

2. **Compiler analysis:**
   - Identify points where region splitting is beneficial
   - Emit `region_split()` at those points
   - Track which objects live in which region after split

3. **Verification:**
   - Ensure no pointers cross split boundary
   - Verify both regions can be reclaimed independently

**Files to modify:**
- `runtime/src/memory/region_core.c` - Implement `region_split()`
- `csrc/analysis/split.c` - Identify split opportunities
- `csrc/codegen/codegen.c` - Emit split calls

**Expected benefit:** 5-20% reduction in peak memory (for split-friendly code)

---

### Phase 5: Auto-Repair Policy Integration

**Objective:** Use region accounting data (from Issue 2) to tune auto-repair decisions (transmigrate vs. retain).

**Problem:**
```lisp
;; Current: Fixed threshold (4 KB) for transmigrate vs. retain
;; Not optimal for all workloads!
```

**Solution (After Phase 5):**
```lisp
;; Use region accounting to adapt threshold:
;; - If many transmigrates fail (copy too expensive), increase threshold
;; - If many retains cause retention cliffs, decrease threshold
```

**Implementation:**

1. **Runtime feedback loop:**
   - Track `escape_repair_count` per region (already implemented in Issue 2)
   - Track `transmigrate_success_rate` and `retain_retention_cost`
   - Adjust `REPAIR_TRANSMIGRATE_THRESHOLD_BYTES` dynamically

2. **Compiler integration:**
   - Pass heuristic tuning data from runtime to compiler
   - Emit threshold-adjusted auto-repair decisions

3. **Verification:**
   - Measure performance impact of adaptive threshold
   - Ensure no oscillations in threshold value

**Files to modify:**
- `runtime/src/memory/region_core.c` - Track repair statistics
- `csrc/codegen/codegen.c` - Use adaptive threshold

**Expected benefit:** 5-15% reduction in auto-repair overhead

---

## 3) Verification Plan

### 3.1 Pseudo-Program 1: Non-Lexical Region Ends

**Source:**
```lisp
(define (test-non-lexical)
  (let ((x 42)
        (y (pair 1 2))
        (z (pair 3 4)))
    (do-something-with x)
    (do-something-with y)
    (do-something-else)))
```

**Expected Region Plan (After Phase 1):**
```
region = region_create()
x = 42 (immediate, no allocation)
y = mk_pair_region(region, 1, 2)
z = mk_pair_region(region, 3, 4)
do_something_with(x)
do_something_with(y)
region_exit(region)  ; ← Early exit! z is dead here
do_something_else()
```

**Verification:**
- Region reclaims before `do-something-else()`
- No use-after-free for any variable

---

### 3.2 Pseudo-Program 2: Allocate-Into-Outliving-Region

**Source:**
```lisp
(define (make-value caller-region)
  (let ((region (region-create))
        (x (pair 1 2)))
    (region-exit region)
    x))

(define (test-allocate-into-outliving)
  (let ((caller-region (region-create))
        (v (make-value caller-region)))
    (use-value v)
    (region-exit caller-region)))
```

**Expected Region Plan (After Phase 3):**
```
;; In make-value:
Region* local = region_create()
Obj* x = mk_pair_region(caller_region, 1, 2)  ; ← Allocate in caller_region directly!
region_exit(local)
return x

;; In test-allocate-into-outliving:
Region* caller = region_create()
Obj* v = make_value(caller)
use_value(v)
region_exit(caller)
```

**Verification:**
- No `transmigrate()` call emitted
- `x` allocated directly in `caller-region`
- `local` region reclaimed immediately

---

### 3.3 Pseudo-Program 3: Splittable Regions

**Source:**
```lisp
(define (test-split)
  (let ((region (region-create))
        (x (pair 1 2))
        (y (pair 3 4))
        (z (pair 5 6)))
    (do-something-with x)
    (do-something-with-y)
    (do-something-with-z)))
```

**Expected Region Plan (After Phase 4):**
```
region = region_create()
x = mk_pair_region(region, 1, 2)
y = mk_pair_region(region, 3, 4)
z = mk_pair_region(region, 5, 6)
do_something_with(x)

;; SPLIT: Move y, z to new region
Region* region2 = region_split(region, after=y)
region_exit(region)  ; ← x reclaimed!

do_something_with_y()
do_something_with_z()
region_exit(region2)  ; ← y, z reclaimed!
```

**Verification:**
- `region_split()` called at split point
- No pointers cross split boundary
- Both regions reclaimed independently

---

## 4) Interaction with Mutation Auto-Repair (Issue 2)

### 4.1 Mutation Barriers Still Required

Even after all phases, **mutation auto-repair** (from Issue 2) is still required:

```lisp
;; Even with allocate-into-outliving-region, mutations can cause problems:
(let ((older (region-create))
      (container (dict)))
  (region-exit older))
  (f older container))

(define (f older-region container)
  (let ((region (region-create))
        (x (pair 1 2)))
    (region-exit region)
    (dict-set! container "key" x)))  ;; Still requires auto-repair!
```

### 4.2 Auto-Repair Decision Uses Region Accounting

**Before Phase 5:**
```c
// Fixed threshold (from Issue 2)
if (src_bytes <= 4096) {
    return REPAIR_TRANSMIGRATE;
} else {
    return REPAIR_RETAIN;
}
```

**After Phase 5:**
```c
// Adaptive threshold using region accounting
size_t threshold = adaptive_threshold(src_region);
if (src_bytes <= threshold) {
    return REPAIR_TRANSMIGRATE;
} else {
    return REPAIR_RETAIN;
}

// adaptive_threshold() implementation:
size_t adaptive_threshold(Region* src) {
    // If many transmigrates fail, increase threshold
    if (src->transmigrate_failure_rate > 0.3) {
        return src->current_threshold * 1.5;
    }
    // If many retains cause retention cliffs, decrease threshold
    if (src->retain_retention_cost > 1000) {
        return src->current_threshold * 0.5;
    }
    return src->current_threshold;
}
```

---

## 5) Implementation Priority

| Phase | Benefit | Complexity | Priority |
|-------|---------|------------|----------|
| 1: Non-lexical region ends | 10-30% memory reduction | Medium | P1 |
| 2: Stack allocation | 20-40% alloc reduction | Medium | P1 |
| 3: Allocate-into-outliving-region | 10-50% transmigrate reduction | High | P0 |
| 4: Splittable regions | 5-20% memory reduction | Very High | P2 |
| 5: Auto-repair policy integration | 5-15% repair reduction | Medium | P1 |

**Recommended order:** Phase 3 → Phase 1 → Phase 2 → Phase 5 → Phase 4

**Rationale:**
- **Phase 3 first:** Biggest win for transmigrate overhead (common in realistic code)
- **Phase 1 and 2 next:** Medium effort, good wins for memory and allocation
- **Phase 5:** Builds on Phase 1-4 for adaptive tuning
- **Phase 4 last:** High complexity, moderate benefit, only for split-friendly workloads

---

## 6) Testing Strategy

### 6.1 Unit Tests per Phase

**Phase 1:**
- Test `region_exit()` emitted at last-use
- Test no use-after-free after early region exit

**Phase 2:**
- Test stack allocation for non-escaping values
- Test region allocation for escaping values

**Phase 3:**
- Test direct allocation in destination region
- Test no transmigrate when destination known

**Phase 4:**
- Test `region_split()` creates valid regions
- Test no cross-boundary pointers

**Phase 5:**
- Test adaptive threshold adjusts based on statistics
- Test no oscillation in threshold value

### 6.2 Regression Tests

Add 3 pseudo-programs (from Section 3) to test suite:
- `test_non_lexical_region_ends.omni`
- `test_allocate_into_outliving_region.omni`
- `test_splittable_regions.omni`

### 6.3 Performance Benchmarks

For each phase, run:
- `bench_memory_usage` - Measure peak memory
- `bench_allocation_time` - Measure allocation overhead
- `bench_transmigrate_cost` - Measure transmigrate count/time
- `bench_retention_cliffs` - Measure retention events

Compare before/after to quantify benefit.

---

## 7) "Done Means" for Each Phase

### Phase 1: Non-Lexical Region Ends

1. Liveness analysis tracks last-use per region
2. `region_exit()` emitted at last-use (before scope exit)
3. All tests pass (no use-after-free)
4. Performance: 10-30% reduction in peak memory

### Phase 2: Stack Allocation

1. Non-escaping values allocated on stack
2. Escaping values allocated in regions
3. New API `stack_alloc_region()` implemented
4. Performance: 20-40% reduction in alloc overhead

### Phase 3: Allocate-Into-Outliving-Region

1. Interprocedural escape analysis tracks destination
2. Direct allocation in destination region when provable
3. No transmigrate when destination known
4. Performance: 10-50% reduction in transmigrate overhead

### Phase 4: Splittable Regions

1. New API `region_split()` implemented
2. Compiler identifies split opportunities
3. No cross-boundary pointers
4. Performance: 5-20% reduction in peak memory

### Phase 5: Auto-Repair Policy Integration

1. Adaptive threshold uses region accounting
2. Threshold adjusts based on statistics
3. No oscillation in threshold
4. Performance: 5-15% reduction in repair overhead

---

## 8) Future Work

### 8.1 Interprocedural Region Inference

Extend Phase 3 to:
- Propagate region information across function boundaries
- Handle mutually recursive functions
- Support region polymorphism (Tofte/Talbin)

### 8.2 Automatic Region Splitting

Extend Phase 4 to:
- Automatically identify split opportunities
- Heuristics for when to split vs. not split
- Merge split regions when beneficial

### 8.3 Global Optimization

After all phases, add a global optimization pass to:
- Reorder operations to reduce region lifetimes
- Coalesce small regions into larger regions
 - Eliminate unnecessary `region_create()` / `region_exit()` pairs
---

## 9) Current Implementation Status (Issue 3 P1)

This section tracks what code generation currently emits for CTRR features.

### 9.1 Where `_local_region` is created and destroyed

**Status:** ✅ IMPLEMENTED

**Locations:**
- `csrc/codegen/region_codegen.c` - `omni_codegen_region_create()` emits region creation
- `csrc/codegen/codegen.c` - `omni_codegen_region_destroy()` emits region cleanup

**Generated Code Pattern:**
```c
struct Region* _local_region = region_create();
// ... local allocations use _local_region ...
region_exit(_local_region);  // Marks scope as inactive
region_destroy_if_dead(_local_region);  // Frees if no external refs
```

### 9.2 Where `transmigrate()` is emitted

**Status:** ✅ IMPLEMENTED (transmigrate only, no retain/release yet)

**Locations:**
- `csrc/codegen/region_codegen.c` - `omni_codegen_transmigrate_on_escape()` emits transmigrate calls
- `csrc/codegen/codegen.c` - `omni_codegen_transmigrate_return()` emits transmigrate at return

**Generated Code Pattern (returns):**
```c
Obj* result = mk_pair_region(_local_region, 1, 2);
return transmigrate(result, _local_region, _caller_region);
```

**Decision Point:** `omni_should_transmigrate()` decides based on escape class:
- ESCAPE_RETURN, ESCAPE_CLOSURE, ESCAPE_GLOBAL → transmigrate

**TODO:** Issue 1 P2 (retain/release insertion) will add RETAIN_REGION option

### 9.3 Where tethering is emitted

**Status:** ✅ IMPLEMENTED

**Locations:**
- `csrc/codegen/region_codegen.c` - `omni_codegen_tether_params()` emits tether_start/tether_end for RegionRef arguments
- `csrc/codegen/region_codegen.c` - `omni_codegen_tether_start()`, `omni_codegen_tether_end()` functions exist

**Generated Code Pattern:**
```c
// Function entry for RegionRef arguments
region_tether_start(arg_region);
// ... function body with ArgRegion ...
region_tether_end(arg_region);  // Release borrow
```

**Precision:** Currently conservative (all RegionRef parameters), not precise

### 9.4 Confirm that no `region_retain/region_release` insertion exists yet

**Status:** ✅ CORRECT (none implemented yet)

**Finding:**
- Searched entire codebase for `region_retain_internal()` and `region_release_internal()` calls in generated code
- **Result:** No retain/release insertion found in code generation

**Reason:** Issue 1 P2 will implement retain/release insertion as an alternative to transmigrate for large/escaping graphs.

---

## 10) References

- `docs/CTRR.md` - CTRR contract and Region Closure Property
- `runtime/docs/REGION_RC_MODEL.md` - Region-RC model and external pointers
- `runtime/docs/REGION_ACCOUNTING.md` - Region accounting for heuristic tuning
- *Better Static Memory Management* (Aiken/Fähndrich/Levien, PLDI 1995): https://digicoll.lib.berkeley.edu/record/139069
- *Region-Based Memory Management* (Tofte/Talbin, 1997): https://www.sciencedirect.com/science/article/pii/S0890540196926139
- *Spegion* (ECOOP 2025): https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2025.15
