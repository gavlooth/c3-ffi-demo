# Branch-Level Region Narrowing

This document describes **Branch-Level Region Narrowing**, a compile-time optimization that reduces RC (Reference Counting) overhead by keeping branch-local data out of RC-managed regions.

---

## 1. Problem Statement

Without narrowing, all objects within a function tend to be allocated into the same RC-managed region:

```text
┌─────────────────────────────────────────┐
│ Region R (RC-managed)                   │
│                                         │
│  if branch1:                            │
│    temp1, temp2, temp3  ──┐             │
│  else branch2:            │ ALL         │
│    temp4, temp5       ────┤ BULKED      │
│  end                      │ INTO R      │
│                           │             │
│  result = compute(...)  ◄─┘             │
│  return result  ; escapes               │
└─────────────────────────────────────────┘
```

**Consequences:**
- All temporaries (`temp1`-`temp5`) contribute to region RC overhead
- Memory is held until region destruction (even branch-local data)
- Transmigration cost increases (more data to potentially copy)
- Cache pressure increases

---

## 2. Solution: Branch-Level Narrowing

Analyze each branch independently. Non-escaping branches use **pure ASAP** (stack or scratch arena), avoiding the parent RC region entirely.

```text
┌─────────────────────────────────────────┐
│ Region R (RC-managed) ← NARROW          │
│                                         │
│  if branch1:                            │
│    ┌─────────────────────┐              │
│    │ Scratch/Stack       │ ← Pure ASAP  │
│    │ temp1, temp2, temp3 │   (freed at  │
│    └─────────────────────┘    branch    │
│  else branch2:                 exit)    │
│    ┌─────────────────────┐              │
│    │ Scratch/Stack       │ ← Pure ASAP  │
│    │ temp4, temp5        │              │
│    └─────────────────────┘              │
│  end                                    │
│                                         │
│  result ← only this in R                │
│  return result                          │
└─────────────────────────────────────────┘
```

**Result:** Region R contains only what truly escapes. Branch temporaries never touch RC.

---

## 3. Analysis Pipeline

The optimization combines **Escape Analysis** (the gate) with **Shape Analysis** (the optimizer):

```text
Branch Entry
    │
    ▼
┌─────────────────────────┐
│ Escape Analysis         │
│ "Does anything escape   │
│  this branch?"          │
└───────────┬─────────────┘
            │
    ┌───────┴───────┐
    │               │
    ▼               ▼
 ESCAPES         NON-ESCAPING
    │               │
    ▼               ▼
 Use parent     ┌─────────────────┐
 region         │ Shape Analysis  │
                │ "TREE/DAG/CYCLIC│
                └────────┬────────┘
                         │
            ┌────────────┼────────────┐
            ▼            ▼            ▼
          TREE          DAG        CYCLIC
            │            │            │
            ▼            ▼            ▼
      Stack alloc    Scratch      Scratch
      + free_tree    arena        arena
```

### 3.1 Escape Analysis (The Gate)

Determines whether branch data escapes to:
- The continuation after the branch (phi-node escape)
- A closure/lambda capturing branch-local variables
- A return statement within the branch
- A global/mutable reference

If **any** data escapes, that data must go to the parent region. Non-escaping data stays in pure ASAP.

### 3.2 Shape Analysis (The Optimizer)

For non-escaping branches, shape determines the **deallocation strategy**:

| Shape | Strategy | Mechanism |
|-------|----------|-----------|
| TREE | Stack + per-object `free_tree` | Maximum granularity, zero overhead |
| DAG | Scratch arena | Bulk free at branch exit, O(1) |
| CYCLIC | Scratch arena | Cycles don't matter for bulk free |

**Key Insight:** For non-escaping branches, even CYCLIC data is trivially handled - the scratch arena bulk-frees everything at branch exit, making cycle detection unnecessary.

---

## 4. Decision Matrix

| Branch Escape | Shape | Allocation | Deallocation | RC Overhead |
|---------------|-------|------------|--------------|-------------|
| Non-escaping | TREE | Stack | `free_tree` at branch exit | None |
| Non-escaping | DAG | Scratch arena | Bulk free at branch exit | None |
| Non-escaping | CYCLIC | Scratch arena | Bulk free at branch exit | None |
| Escaping | Any | Parent region | Region RC | Per-region |

---

## 5. Nested Narrowing (Recursive Analysis)

Branch-level narrowing applies recursively to nested control flow:

```text
Region R (escaping)
  └── Branch B1 (non-escaping) → Scratch
        └── Nested Branch B1.1 (non-escaping, TREE) → Stack
        └── Nested Branch B1.2 (non-escaping, DAG) → Scratch
  └── Branch B2 (escaping) → stays in R
        └── Nested Branch B2.1 (non-escaping) → Scratch
```

Each level independently decides whether to narrow. This is **not** nested regions - it's nested narrowing decisions. Each level avoids contributing to the parent's RC burden where possible.

---

## 6. Partial Escape Handling

When only some data escapes a branch:

```lisp
(if condition
  (let [x (make-obj)    ; Does NOT escape
        y x             ; DAG - y aliases x, does NOT escape
        z (transform x)] ; z ESCAPES via return
    z))
```

**Options:**

1. **Conservative (Simple):** Treat whole branch as escaping → parent region
2. **Precise (Optimal):** Only `z` goes to parent region; `x`/`y` stay in scratch

The precise approach requires tracking which specific bindings flow to escape points.

### Recommended Approach

Start with **conservative** for correctness, add **precise** as an optimization pass:

```text
Pass 1: Mark branch as escaping/non-escaping (conservative)
Pass 2: For escaping branches, identify minimal escaping set (precise)
```

---

## 7. Benefits

| Metric | Without Narrowing | With Narrowing |
|--------|-------------------|----------------|
| RC operations | O(all objects) | O(escaping objects only) |
| Region memory lifetime | Held until region death | Branch data freed at branch exit |
| Transmigration cost | More data to copy | Less data to copy |
| Cache pressure | Higher (larger working set) | Lower |
| Cycle handling | Requires SCC-RC/deferred-RC | Scratch arena handles trivially |

---

## 8. Implementation Strategy

### 8.1 Compiler Changes

1. **Scoped Escape Analysis:** Extend existing escape analysis to track escape at branch granularity, not just function granularity.

2. **Scoped Shape Analysis:** Compute shape within branch scope only (existing `analysis/shape.go` operates at function level).

3. **Allocation Routing:** Based on analysis, route allocations to:
   - Stack (TREE, non-escaping)
   - Scratch arena (DAG/CYCLIC, non-escaping)
   - Parent region (escaping)

4. **Deallocation Insertion:** Insert appropriate cleanup at branch exit:
   - `free_tree` calls for stack-allocated TREE data
   - `scratch_arena_destroy` for scratch arenas

### 8.2 Runtime Changes

Minimal - reuse existing mechanisms:
- Stack allocation: existing
- Scratch arena: existing arena infrastructure with scope-local lifetime
- Parent region: existing Region-RC

### 8.3 Files to Modify

| File | Change |
|------|--------|
| `csrc/analysis/analysis.c` | Add branch-scoped escape tracking |
| `csrc/analysis/shape.c` | Add branch-scoped shape computation |
| `csrc/codegen/codegen.c` | Allocation routing based on narrowing decision |
| `csrc/codegen/cleanup.c` | Branch-exit cleanup insertion |

---

## 9. Relation to Existing Architecture

### Integration with RC-G (Region-Based RC)

Branch-level narrowing is **complementary** to RC-G:
- RC-G handles escaping data efficiently (coarse-grained RC on regions)
- Branch narrowing reduces what *enters* RC-G regions

Together: RC regions are **tight** (minimal data), pure ASAP handles the rest.

### Integration with ASAP

This is an **extension** of ASAP philosophy:
- Traditional ASAP: "Free objects at last use"
- Branch narrowing: "Don't even allocate into RC regions if branch-local"

The narrowing decision is made at compile time, preserving ASAP's static nature.

---

## 10. Example

### Input

```lisp
(defn process [data]
  (let [result
        (if (empty? data)
          ;; Branch 1: Build temporary structure, return summary
          (let [temp (build-tree data)      ; TREE shape
                stats (compute-stats temp)] ; TREE shape
            (summarize stats))              ; Only this escapes
          ;; Branch 2: Direct transformation
          (transform data))]                ; Escapes directly
    result))
```

### Analysis

- **Branch 1:** `temp` and `stats` are TREE-shaped, non-escaping → Stack + `free_tree`
- **Branch 1:** `(summarize stats)` escapes → Parent region
- **Branch 2:** `(transform data)` escapes → Parent region

### Generated Code (Pseudocode)

```c
Value* process(Value* data) {
    Value* result;
    if (is_empty(data)) {
        // Branch 1: Stack allocation for non-escaping TREE data
        Value* temp = stack_alloc_tree(build_tree(data));
        Value* stats = stack_alloc_tree(compute_stats(temp));
        result = region_alloc(parent_region, summarize(stats));
        // Branch exit: free stack-allocated data
        free_tree(stats);
        free_tree(temp);
    } else {
        // Branch 2: Direct to parent region
        result = region_alloc(parent_region, transform(data));
    }
    return result;
}
```

---

## 11. Lifecycle-Based Region Partitioning

Beyond branch-level narrowing, escaping data can be further partitioned by **lifecycle class** rather than lumping all escaping data into a single parent region.

### 11.1 The Problem with Coarse Grouping

Without lifecycle partitioning:

```text
Branch with mixed escapes:
  ├─ temp1 (non-escaping) → scratch ✓
  ├─ temp2 (non-escaping) → scratch ✓
  ├─ result1 (escapes to return) ──┐
  ├─ result2 (escapes to closure A)│── ALL to parent region
  └─ result3 (escapes to closure B)┘
```

But these have **different lifetimes**:

| Data | Escapes To | Actual Lifetime |
|------|------------|-----------------|
| `result1` | return | caller's scope |
| `result2` | closure A | closure A's lifetime |
| `result3` | closure B | closure B's lifetime |

Lumping them together wastes memory when lifetimes diverge.

### 11.2 Lifecycle Classes

Define lifecycle classes based on escape target:

| Class | Escape Target | Lifetime | Region Strategy |
|-------|---------------|----------|-----------------|
| `LOCAL` | None | Branch/scope | Scratch arena (bulk free) |
| `CALLER` | Return value | Caller's scope | Caller-provided region |
| `CAPTURED` | Closure | Closure's lifetime | Closure's region |
| `GLOBAL` | Global/mutable ref | Program lifetime | Global region |

### 11.3 Partitioned Allocation

```text
Branch with lifecycle partitioning:
  ├─ LOCAL: temp1, temp2 → scratch
  ├─ CALLER: result1 → R_caller
  ├─ CAPTURED: result2, result3 → R_closure (shared if same closure)
  └─ GLOBAL: config → R_global
```

Each escaping value goes to the **minimal region that covers its lifetime**.

### 11.4 Cost Analysis: Region vs Transmigration

The key tradeoff:

| Strategy | Cost |
|----------|------|
| **More regions (lifecycle-aligned)** | N × region overhead |
| **Fewer regions (coarse)** | Transmigration O(graph size) later |

**Transmigration cost is proportional to data size. Region overhead can be made adaptive.**

```
For lifecycle group with n objects:

Fixed region overhead:     C (constant)
Transmigration cost:       O(n)

Crossover: when C < k*n, region wins
For n > ~50-100 objects, separate region is cheaper than transmigrating later.
```

### 11.5 Decision Framework

```text
For each identified lifecycle group G:

  estimated_size = static_estimate(G)
  crosses_boundary = escapes_to_different_lifecycle(G)

  if estimated_size > THRESHOLD and crosses_boundary:
    assign_own_region(G)    # cheaper than transmigrating later
  else:
    merge_with_parent(G)    # overhead not worth it
```

---

## 12. Adaptive Region Sizing

To reduce the penalty for small lifecycle groups, region overhead should scale with actual data size rather than being fixed.

### 12.1 Problem with Fixed Overhead

```text
Region overhead = C (constant, e.g., 64-128 bytes)

Small region (10 objects):   overhead/object = C/10   = high!
Large region (1000 objects): overhead/object = C/1000 = negligible
```

Fixed overhead penalizes small regions disproportionately, making the "should this get its own region?" decision sensitive to size estimates.

### 12.2 Adaptive Overhead Model

```text
Region overhead = f(n) where n = objects in region

Target: O(log n) or O(n) with small constant
```

With adaptive overhead:
```text
Small region (10 objects):   overhead ≈ small
Large region (1000 objects): overhead ≈ larger, but proportional
```

**Benefit:** Small lifecycle groups don't pay disproportionate overhead. The threshold for splitting becomes less critical.

### 12.3 Implementation: Region Size Classes

```text
Class       | Max Size | RCB Type    | Arena Type        | Overhead
------------|----------|-------------|-------------------|----------
TINY        | 256 B    | Inline      | Stack bump        | ~16 bytes
SMALL       | 4 KB     | RegionLite  | Single block      | ~32 bytes
MEDIUM      | 64 KB    | Region      | Linked blocks     | ~64 bytes
LARGE       | 1 MB+    | Region      | Geometric growth  | ~128 bytes
```

### 12.4 Lightweight RCB for Small Regions

```c
// Full RCB (current) - 64+ bytes
typedef struct Region {
    Arena* arena;
    atomic_int external_rc;
    atomic_int tether_count;
    bool scope_alive;
    RegionBitmap* bitmap;
    // ... more fields
} Region;

// Lightweight RCB - 16 bytes
typedef struct RegionLite {
    void* bump_ptr;            // inline bump allocator
    void* end_ptr;             // end of current block
    uint16_t external_rc;      // small counter (promotes if overflow)
    uint16_t flags;            // scope_alive, needs_promotion, etc.
} RegionLite;
```

### 12.5 Promotion Strategy

```text
Start with RegionLite (16 bytes)
         │
         ▼
If external_rc overflows OR needs bitmap OR needs multi-block:
         │
         ▼
Promote to full Region (64+ bytes)
```

Most small lifecycle groups never promote.

### 12.6 Arena Allocation Strategies

The compiler already performs escape analysis, shape analysis, and liveness analysis. We can extend this with **size analysis** to determine exact allocation sizes at compile time.

#### 12.6.1 Strategy Comparison

| Strategy | Metadata | Slack | When to Use |
|----------|----------|-------|-------------|
| **Static Sizing** | O(1) | O(1) | All allocations statically sized |
| **VM Reservation** | O(1) | O(page) | Dynamic size, OS support available |
| **Polynomial (√n)** | O(1) | O(√n) | Dynamic size, no VM support |
| **Geometric (2×)** | O(log n) | O(n) | Fallback only |

#### 12.6.2 Static Size Analysis (Recommended - Zero Slack)

Most allocations have statically known sizes:

| Pattern | Size | Static? |
|---------|------|---------|
| `(cons a b)` | sizeof(Cell) = 24B | ✓ Yes |
| `(make-point x y)` | sizeof(Point) = 16B | ✓ Yes |
| `(list 1 2 3)` | 3 × sizeof(Cell) = 72B | ✓ Yes |
| `(make-array 100)` | 100 × elem_size | ✓ Yes |
| `(map f xs)` | length(xs) × sizeof(Cell) | ✗ Dynamic |
| `(read-file path)` | file size | ✗ Dynamic |

**Algorithm:**

```text
For each lifecycle group G:
  static_size = 0
  dynamic_allocs = []

  for alloc in G.allocations:
    if alloc.size is statically known:
      static_size += alloc.size
    else:
      dynamic_allocs.append(alloc)

  if dynamic_allocs.empty():
    # Perfect case: exact allocation, zero slack
    emit: region = region_create_exact(static_size)
  else:
    # Hybrid: static portion exact, dynamic portion growable
    emit: region = region_create_hybrid(static_size, growth_strategy)
```

**Overhead for fully static groups:** O(1) metadata, O(1) slack (alignment only)

#### 12.6.3 Two-Phase Allocation (Dynamic with Zero Slack)

For dynamic allocations where size is computable but not statically known:

```text
Phase 1 (sizing pass):
  Walk the data, compute total size needed

Phase 2 (allocation pass):
  Allocate exact total
  Bump-allocate into it
```

Example - copying a list:
```lisp
(defn copy-list [xs]
  ;; Phase 1: count
  (let [n (length xs)]
    ;; Phase 2: allocate exact, fill
    (region-with-size (* n (sizeof Cell))
      (map identity xs))))
```

**Overhead:** O(1) metadata, O(1) slack, but 2× traversal cost

**When worthwhile:** When traversal is cheap relative to allocation (e.g., counting list length vs allocating cells).

#### 12.6.4 Virtual Memory Reservation (OS-Level Zero-Copy Growth)

For truly dynamic sizes on systems with virtual memory:

```c
// Reserve large virtual space (not backed by physical memory)
void* region = mmap(NULL, 1*MB, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

// Commit pages on demand as data is written
// (via SIGSEGV handler or explicit mprotect)
mprotect(region + committed, new_pages * 4096, PROT_READ|PROT_WRITE);
```

**Overhead:**
- Virtual space: free (not backed until touched)
- Physical slack: O(page_size) = 4KB max
- Growth: O(1) - no copying, just commit more pages

**Pros:** True O(1) growth with O(page) slack.
**Cons:** Requires OS support, page granularity (4KB minimum).

#### 12.6.5 Polynomial Growth (√n) - Fallback

When static sizing isn't possible and VM reservation isn't available:

```text
Block 1: b           = 256 B
Block 2: b · √n      ≈ 256 · √n
Block 3: b · n       ≈ 256 · n

For n = 64 KB: 2 blocks (256B, 64KB)
For n = 1 MB:  3 blocks (256B, 16KB, 1MB)
```

**Overhead:** O(1) metadata (2-3 blocks), O(√n) slack

#### 12.6.6 Recommended Hybrid Strategy

```text
┌─────────────────────────────────────────────────────────────────┐
│ Compile-Time Size Analysis                                      │
│                                                                 │
│ For lifecycle group G:                                          │
│   Classify each allocation as STATIC or DYNAMIC                 │
└─────────────────────────────┬───────────────────────────────────┘
                              │
              ┌───────────────┴───────────────┐
              │                               │
              ▼                               ▼
      ALL STATIC                        HAS DYNAMIC
              │                               │
              ▼                               ▼
┌─────────────────────────┐   ┌─────────────────────────────────┐
│ Exact Static Allocation │   │ Hybrid Allocation               │
│                         │   │                                 │
│ size = sum(alloc.size)  │   │ static_region = exact(static)   │
│ region = exact(size)    │   │ dynamic_region = choose:        │
│                         │   │   - VM reservation (preferred)  │
│ Overhead:               │   │   - Two-phase if traversal cheap│
│   Metadata: O(1)        │   │   - √n growth (fallback)        │
│   Slack: O(1)           │   │                                 │
└─────────────────────────┘   └─────────────────────────────────┘
```

**Expected outcome:** Most lifecycle groups are fully static → O(1) everything.

### 12.7 Revised Decision Framework

With adaptive overhead, size estimation becomes less critical:

```text
Old: "Is group > 50-100 objects? → own region"
     (Requires accurate size estimation)

New: "Does group have distinct lifecycle? → own region"
     (Overhead adapts to actual size automatically)
```

The compile-time decision simplifies to pure lifecycle analysis. Runtime sizing handles the rest.

### 12.8 Asymptotic Comparison

| Operation | Static Sizing | VM Reservation | Polynomial (√n) | Geometric (2×) |
|-----------|---------------|----------------|-----------------|----------------|
| Create region | O(1) | O(1) | O(1) | O(1) |
| Allocate | O(1) bump | O(1) bump | O(1) amortized | O(1) amortized |
| Destroy | O(1) | O(1) | O(1) | O(log n) |
| # Blocks | 1 | 1 | O(1) ~2-3 | O(log n) |
| Metadata | O(1) | O(1) | O(1) | O(log n) |
| Slack | **O(1)** | O(page) | O(√n) | O(n) |
| Requires | Size analysis | OS support | Size hint | Nothing |

**Recommendation hierarchy:**

1. **Static sizing** (O(1) everything) - when all allocations are statically sized
2. **VM reservation** (O(1) + O(page)) - when dynamic but OS supports virtual memory
3. **Two-phase** (O(1) + 2× traversal) - when size is computable at runtime cheaply
4. **Polynomial √n** (O(1) + O(√n)) - fallback for truly unpredictable sizes
5. **Geometric** (O(log n) + O(n)) - last resort only

**Expected:** Most OmniLisp code hits case 1 (static sizing).

---

## 13. Combined Strategy: Narrowing + Lifecycle + Adaptive

The three optimizations compose:

```text
┌─────────────────────────────────────────────────────────────────┐
│ 1. Branch-Level Narrowing                                       │
│    "Is this data local to the branch?"                          │
│    YES → scratch/stack (pure ASAP)                              │
│    NO  → continues to step 2                                    │
└─────────────────────────────┬───────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 2. Lifecycle Partitioning                                       │
│    "What lifecycle class does this escaping data belong to?"    │
│    CALLER / CAPTURED / GLOBAL → assign to appropriate region    │
└─────────────────────────────┬───────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│ 3. Adaptive Region Sizing                                       │
│    "How big is this lifecycle group?"                           │
│    Automatically selects TINY/SMALL/MEDIUM/LARGE region type    │
│    Overhead scales with actual data                             │
└─────────────────────────────────────────────────────────────────┘
```

**Result:**
- Non-escaping data: Pure ASAP, zero RC overhead
- Escaping data: Minimal region covering its lifecycle
- Region overhead: Proportional to actual data size

---

## 14. References

- **ASAP:** Proust, "As Static As Possible memory management" (2017)
- **Shape Analysis:** Ghiya & Hendren, "Is it a tree, a DAG, or a cyclic graph?" (POPL 1996)
- **Region Inference:** Tofte & Talpin, "Region-Based Memory Management" (1997)
- **MLKit:** Region polymorphism and inference
- **Geometric Growth:** Cormen et al., "Introduction to Algorithms" - amortized analysis
