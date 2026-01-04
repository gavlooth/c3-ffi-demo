# Static Symmetric RC: Enhancing ASAP with Double Strategy

**STATUS: IMPLEMENTED** and integrated into Component-Level Scope Tethering (v0.6.0).

## The Core Concept

We can combine **ASAP (Static Liveness)** with **Symmetric RC (Double Strategy)** to achieve **Zero-Overhead Cycle Collection**.

### The Mapping
| Runtime Symmetric RC | Static Symmetric RC (Proposed) |
|----------------------|--------------------------------|
| **External RC** (Roots) | **Static Scope Liveness** (ASAP) |
| **Internal RC** (Cycles) | **Static Shape Analysis** |
| **Liveness Check** | **Compiler determines "Last Use"** |
| **Cycle Collection** | **Compiler inserts `free_scc()`** |

## The Strategy: "Dominator-Based Lifetime"

In the Double RC model, an object lives as long as it has **External Refs**. In ASAP, "External Refs" are just **local variables** (roots).

If the compiler can identify a **Dominating Root** for a cyclic structure (an SCC), we can treat the entire SCC as a single "virtual object" owned by that root.

### Logic Flow

1.  **Shape Analysis**: Identify a subgraph is cyclic (SCC).
2.  **Dominator Analysis**: Find the single local variable `R` that allows access to the SCC (the "Entry Point").
3.  **Internal Classification**: Mark all edges *within* the SCC as **Internal** (structurally weak).
4.  **ASAP Integration**:
    *   Track the liveness of `R` (the External Ref).
    *   When `R` hits its **Last Use** (statically determined):
        *   **Runtime Action**: Immediately free the entire SCC.
        *   **Optimization**: NO reference counting on internal edges.

## Example Transformation

### 1. Source Code
```scheme
(let ((node-a (create-node))
      (node-b (create-node)))
  (link-nodes! node-a node-b) ;; Creates cycle: A <-> B
  (process node-a))           ;; Last use of A (and B via A)
```

### 2. Runtime Symmetric RC (Current - Slow)
```c
SymObj* A = sym_alloc(...); // Malloc + 40 byte header
SymObj* B = sym_alloc(...); // Malloc + 40 byte header
sym_link(A, B); // Int RC++
sym_link(B, A); // Int RC++
// ...
sym_exit_scope(); // Decrement Ext RC, detect orphaned cycle, free
```

### 3. Static Symmetric RC (Proposed - Fast)
```c
// Standard Obj (32 bytes), NO wrappers, NO RC overhead
Obj* A = mk_node(...);
Obj* B = mk_node(...);

// Internal links are just pointers (no ref counting)
A->next = B;
B->next = A;

process(A);

// ASAP determines 'A' is the dominator and it dies here.
// Compiler emits a specialized destructor for the known shape:
free_cycle_AB(A, B); 
```

## Requirements

1.  **Closed Scope**: The cycle must not escape the scope of the dominator `R` (or it must be moved as a unit).
2.  **Shape Knowledge**: The compiler must know the structure of the cycle (e.g., "Pair A links to Pair B") OR use a generic `free_scc` traversal.

## Benefits

*   **Zero Overhead**: No `SymObj` wrappers, no `inc_ref/dec_ref` at runtime.
*   **Deterministic**: Memory is reclaimed exactly at the closing brace/last use.
*   **Simple**: Reuses existing ASAP liveness logic.
