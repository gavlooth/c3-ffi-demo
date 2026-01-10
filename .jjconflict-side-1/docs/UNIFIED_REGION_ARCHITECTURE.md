# Unified "Region-Hybrid" Architecture

## 1. Core Philosophy: "The Best of Both"

This architecture blends **Region-Based Memory** (for cycles, bulk data, and safety) with **Standard Reference Counting** (for flexible, granular sharing), eliminating the weaknesses of each.

### The Golden Rule
> **"Any data structure that MIGHT form a cycle MUST be allocated within a Region. Standard Reference Counting is reserved strictly for Directed Acyclic Graphs (DAGs)."**

### Why this works:
1.  **Solves the Cycle Problem:** Since all cycles are contained in Regions, we *never* need a runtime Cycle Detector (Tarjan's) or a Garbage Collector. When the Region dies, the cycle dies.
2.  **Solves the Fragmentation Problem:** Long-lived, independent objects (like User Sessions or Configuration) can use Standard RC ("Detached" mode), so they don't trap megabytes of dead arena memory.
3.  **Solves the Transmigration Cost:** We only Copy (Transmigrate) small values. Large values are either Shared (Region-Ref) or Detached (Standard-Ref).

## 2. Memory Semantics: Three Object States

The compiler/runtime supports three distinct allocation strategies:

### A. Region-Resident (The "Cell")
*   **Location:** Inside an `Arena` or `Pool` block of a Region.
*   **Semantics:** Zero-overhead allocation.
*   **Lifecycle:** Dies when the Region dies.
*   **Use Case:** Local scratchpad data, complex cyclic graphs (DOM trees, game entities), temporary lists.

### B. Region-Owner (The "Shell")
*   **Location:** Allocated on the Heap, but *controls* a Region.
*   **Semantics:** Has a `RegionControlBlock`.
*   **Lifecycle:** Refcounted (Region-RC). When RC=0, it destroys the underlying Region.
*   **Use Case:** The "Handle" to a cyclic graph. Passing this passes the whole graph.

### C. Detached (The "Singleton")
*   **Location:** Standard Heap (`malloc`).
*   **Semantics:** Standard Atomic RC (`count`).
*   **Constraint:** **MUST BE ACYCLIC.** The compiler forbids fields that could form back-edges unless they are `Weak`.
*   **Use Case:** shared strings, config objects, independent singletons, "return values" that don't need a whole region.

## 3. Pointer & Reference Semantics

### 3.1 Strong References
1.  **`RegionRef` (Fat Pointer):** `{ Obj* ptr, RCB* region }`. Holds the *Region* alive. Used for sharing type **A** and **B**.
2.  **`DetachedRef` (Slim Pointer):** `{ Obj* ptr }`. Holds the *Object* alive via standard RC. Used for type **C**.

### 3.2 Tethers (Borrows)
*   **Mechanism:** Works on *both* types.
    *   For **RegionRef**: Increments `region->tether_count`.
    *   For **DetachedRef**: Standard `inc_ref` (or specialized `tether_bit` if optimizing).
*   **Benefit:** Uniform API for safe concurrent access.

## 4. Compiler Strategy: The "Escape Heuristic"

The compiler chooses the allocation strategy based on **Shape**, **Escape**, and **Size**:

| Scenario | Decision | Rationale |
| :--- | :--- | :--- |
| **Local Temporary** | **Region-Resident** | Fastest alloc/free. Zero overhead. |
| **Cyclic Graph** (Escapes) | **Region-Owner** | Regions handle cycles automatically. Bulk free is efficient. |
| **Small Value** (Escapes) | **Transmigrate (Copy)** | Cheaper to copy 64 bytes than to promote a 4KB page. |
| **Independent Object** (Escapes) | **Detached (Std RC)** | Avoids fragmentation. e.g., A long-lived String or Request object. |

## 5. Transitioning (The "Mix")

### 5.1 Promotion (Early Detachment)
If the compiler sees:
```clojure
(let [x (create-huge-config)] 
  (return x))
```
It realizes `x` is large, independent, and escaping. Instead of allocating `x` in the local Arena (which would require Transmigration/Copying), it allocates `x` as **Detached** immediately.

### 5.2 Transmigration (Late Detachment)
If `x` was created in an Arena (optimistically) but then a *small piece* of it escapes:
```clojure
(let [graph (build-huge-graph)]
  (return graph.summary_stats)) ; Just a small struct
```
The runtime performs **Transmigration**: It deep-copies `summary_stats` into the Caller's Region (or as a Detached object), allowing the huge `graph` Region to die cleanly.

## 6. Summary

This Unified Architecture gives you:
1.  **Speed of Regions** (for local/cyclic data).
2.  **Flexibility of RC** (for shared/linear data).
3.  **Safety of No-GC** (Cycles are Region-bound).
4.  **Efficiency** (Transmigration prevents leaks/fragmentation).

It is strictly better than using *only* RC (too slow/cycles) or *only* Regions (too rigid/fragmented).
