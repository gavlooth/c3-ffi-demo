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

Analyze each branch independently. Non-escaping branches use **CTRR-local scratch**
(stack or scratch arena), avoiding the parent region entirely.

```text
┌─────────────────────────────────────────┐
│ Region R (RC-managed) ← NARROW          │
│                                         │
│  if branch1:                            │
│    ┌─────────────────────┐              │
│    │ Scratch/Stack       │ ← CTRR-local │
│    │ temp1, temp2, temp3 │   (freed at  │
│    └─────────────────────┘    branch    │
│  else branch2:                 exit)    │
│    ┌─────────────────────┐              │
│    │ Scratch/Stack       │ ← CTRR-local │
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

## 3. The Allocation Matrix

The decision of where to allocate an object is determined by its **Shape**, **Escape Scope**, and **Size**, filtered by the compilation mode (AOT vs JIT).

### 3.1 Decision Logic

| Shape | Escape Scope | Size (Heuristic) | **Allocation Strategy** |
| :--- | :--- | :--- | :--- |
| **TREE** | **Local** | Small (Static) | **Stack** (alloca) |
| **TREE/DAG** | **Local** | Any | **Scratch Arena** (CTRR-local) |
| **CYCLIC** | **Local** | Any | **Scratch Arena** (CTRR-local) |
| | | | |
| **TREE/DAG** | **Escaping** | Small (< 64B) | **Scratch -> Transmigrate** (Copy to parent) |
| **TREE/DAG** | **Escaping** | Large (> 64B) | **Detached** (Direct `malloc` + RC) |
| **CYCLIC** | **Escaping** | Any | **Parent Region** (Region-Resident) |

**Notes:**
1.  **Local Temporaries:** Never `malloc`. Use Stack (fastest) or Scratch (fast).
2.  **Small Escaping:** It is cheaper to allocate in Scratch and copy (Transmigrate) than to `malloc`.
3.  **Large Escaping:** Don't copy. If it's acyclic, make it Detached (Standard RC) to avoid fragmenting the region. If cyclic, it *must* go to the Region.

---

## 4. Optimization Levels: JIT vs AOT

The compiler applies different strategies based on the target use case.

### 4.1 JIT/REPL (Latency Optimized)
**Goal:** Minimize Compilation Time. Get code running immediately.
**Strategy:** "Coarse Narrowing"
1.  **Skip** detailed stack-promotion analysis for small branches.
2.  **Default to Scratch:** Treat almost all locals as **Scratch Arena**. It is O(1) alloc and O(1) free.
3.  **Loop Safety:** We *must* still create scopes for Loops to prevent memory explosion, but we don't need to optimize every `if/else`.

### 4.2 AOT (Throughput Optimized)
**Goal:** Maximize Runtime Performance. Minimize memory footprint.
**Strategy:** "Precise Narrowing"
1.  **Full Analysis:** Check every branch for `ESCAPE_NONE`.
2.  **Stack Promotion:** Aggressively promote `TREE` structures to the stack to reduce cache pressure.
3.  **Heuristics:** Apply the full Size/Shape matrix to choose between Transmigration and Detached allocation.

---

## 5. Analysis Pipeline

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

### 5.1 Escape Analysis (The Gate)

Determines whether branch data escapes to:
- The continuation after the branch (phi-node escape)
- A closure/lambda capturing branch-local variables
- A return statement within the branch
- A global/mutable reference

If **any** data escapes, that data must go to the parent region. Non-escaping data stays CTRR-local.

### 5.2 Shape Analysis (The Optimizer)

For non-escaping branches, shape determines the **deallocation strategy**:

| Shape | Strategy | Mechanism |
|-------|----------|-----------|
| TREE | Stack + per-object `free_tree` | Maximum granularity, zero overhead |
| DAG | Scratch arena | Bulk free at branch exit, O(1) |
| CYCLIC | Scratch arena | Cycles don't matter for bulk free |

**Key Insight:** For non-escaping branches, even CYCLIC data is trivially handled - the scratch arena bulk-frees everything at branch exit, making cycle detection unnecessary.

---

## 6. Implementation Strategy

### 6.1 Compiler Changes

1.  **Scoped Escape Analysis:** Extend existing escape analysis to track escape at branch granularity.
2.  **Scoped Shape Analysis:** Compute shape within branch scope only.
3.  **Allocation Routing:** Implement `omni_alloc_strategy(ctx, var)` using the Allocation Matrix.
4.  **Codegen:** Update `codegen.c` to emit code based on the strategy.

### 6.2 Files to Modify

| File | Change |
|------|--------|
| `csrc/analysis/analysis.c` | Add branch-scoped escape tracking |
| `csrc/analysis/shape.c` | Add branch-scoped shape computation |
| `csrc/codegen/codegen.c` | Allocation routing based on narrowing decision |
| `csrc/codegen/cleanup.c` | Branch-exit cleanup insertion |

---

## 7. Example

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

### Generated Code (AOT Mode - Precise)

```c
Value* process(Value* data) {
    Value* result;
    if (is_empty(data)) {
        // Branch 1: Stack allocation for non-escaping TREE data
        Value* temp = stack_alloc_tree(build_tree(data));
        Value* stats = stack_alloc_tree(compute_stats(temp));
        
        // Escape: result escapes, so allocate in PARENT region
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
