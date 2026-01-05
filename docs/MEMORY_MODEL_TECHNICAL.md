# OmniLisp Memory Model: A Technical Deep Dive

**Target Audience:** C/C++ Developers & Systems Engineers

OmniLisp eschews traditional Tracing Garbage Collection (Stop-the-World, Mark-and-Sweep) in favor of a deterministic, compile-time memory management strategy called **ASAP (As Static As Possible)**. This approach shifts the burden of object lifecycle management from the runtime to the compiler, resulting in predictable latency and reduced runtime overhead.

## 1. Core Philosophy: Deterministic & Static

The fundamental principle is that **object lifetime is a static property of the code**. The compiler runs an extensive analysis pipeline to determine exactly *where* an object is allocated, *who* owns it, and *when* it dies.

*   **No Global Heap Scan:** There is no background process walking the object graph.
*   **No Non-Deterministic Pauses:** `malloc` and `free` happen at predictable points.
*   **Scope-Based:** Memory management is tightly coupled with lexical scope (regions).

## 2. The Analysis Pipeline

The compiler (`csrc/analysis/`) performs distinct passes to classify every variable:

### 2.1 Liveness Analysis
*   **Goal:** Find the `last_use` position of every variable.
*   **Mechanism:** Backward dataflow analysis on the Control Flow Graph (CFG).
*   **Result:** A variable is considered "dead" immediately after its last read operation, allowing for early reclamation before the scope technically ends.

### 2.2 Escape Analysis
*   **Goal:** Determine if a value leaves its defining scope.
*   **Classifications:**
    *   `ESCAPE_NONE`: Local only. **Action:** Stack allocation (`alloca`) or local register. Zero GC cost.
    *   `ESCAPE_ARG`: Passed to a function but not retained. **Action:** Stack alloc + pass-by-reference.
    *   `ESCAPE_RETURN`/`ESCAPE_GLOBAL`: Leaves the scope. **Action:** Heap allocation (`malloc`).

### 2.3 Shape Analysis
*   **Goal:** Identify the structural topology of data.
*   **Classifications:**
    *   `SHAPE_TREE`: Acyclic, unique ownership. **Action:** Recursive `free_tree()`.
    *   `SHAPE_DAG`: Acyclic, shared ownership. **Action:** Reference Counting (`dec_ref`).
    *   `SHAPE_CYCLIC`: Potentially cyclic. **Action:** Cycle-aware reclamation (Arena or Static SCC).

## 3. Allocation & Deallocation Strategies

Based on the analysis, one of several strategies is baked into the generated C code:

### 3.1 Pure ASAP (Unique/Tree)
If an object is a `TREE` and has `ESCAPE_NONE` or unique ownership:
*   **Alloc:** Standard heap or stack.
*   **Free:** The compiler inserts a direct `free()` call at the `last_use` point.
*   **Overhead:** Zero. Equivalent to manual C memory management.

### 3.2 Stack Allocation
If `ESCAPE_NONE` is proven:
*   **Alloc:** The object is allocated on the C stack (or a "stack pool" for resizable types).
*   **Free:** Automatically reclaimed when the C stack frame pops.
*   **Overhead:** Negative (faster than malloc).

### 3.3 Reference Counting (DAGs)
If an object is shared (`SHAPE_DAG`) or escapes unpredictably:
*   **Mechanism:** Intrusive reference counting.
*   **Optimization (Region-Aware Elision):** If a reference is passed within a known region (scope) and guaranteed to outlive the call, `inc_ref`/`dec_ref` pairs are **elided**.
*   **Code:** `INC_REF_IF_NEEDED(obj)` / `DEC_REF_IF_NEEDED(obj)`.

### 3.4 Arena Allocation (Local Cycles)
If a structure is `SHAPE_CYCLIC` but `ESCAPE_NONE`:
*   **Alloc:** Objects are allocated from a linear "Arena" region.
*   **Free:** The entire arena is reset/freed in O(1) when the scope exits. Individual cycle edges are ignored.
*   **Benefit:** Handles complex local graphs without RC overhead or cycle detection.

### 3.5 Static Symmetric RC (Escaping Cycles)
For escaping cycles (e.g., a graph returned from a function):
*   **Dominator Analysis:** Identifies the "entry point" of a Strongly Connected Component (SCC).
*   **Component Tracking:** The runtime tracks the "Component" as a unit.
*   **Tethering:** `sym_tether_begin()` / `sym_tether_end()` create a scope where internal edges are weak. When the external handle count drops to zero, the whole component is dismantled.

## 4. Safety Mechanisms

### 4.1 Generational References (GenRef)
To prevent Use-After-Free (UAF) in a manual/deterministic system, OmniLisp uses "fat pointers":
```c
struct GenRef {
    uint32_t index;      // Index into a stable slot pool
    uint32_t generation; // Generation ID
};
```
*   **Slot Pool:** Memory slots are reused, but their "generation" counter increments on every free.
*   **Check:** Accessing a pointer checks `ref.generation == pool[ref.index].generation`. Mismatch = UAF error (safe crash), not Segfault/Corruption.

### 4.2 Region Hierarchy
The runtime maintains a stack of "Regions" (similar to stack frames).
*   **Invariant:** A pointer can only reference objects in the *same* or *older* (outer) regions.
*   **Validation:** Checked at compile time (where possible) or runtime (for dynamic regions). Prevents dangling pointers to popped stack frames.

## 5. Summary Mapping

| Scenario | Shape | Escape | Strategy | C Equivalent |
|:---|:---|:---|:---|:---|
| Local int/struct | Scalar | None | Stack | `int x;` |
| Unique List | Tree | Return | Pure ASAP | `malloc` + transfer ownership |
| Shared Config | DAG | Global | RefCount | `std::shared_ptr` |
| Local Graph | Cyclic | None | Arena | `std::pmr::monotonic_buffer_resource` |
| Complex Return | Cyclic | Return | Static Sym RC | Custom Graph Destructor |

## 6. Runtime Implementation (`runtime/src/runtime.c`)

The runtime provides the primitives used by the generated code:
*   `mk_int`, `mk_cons`: Object constructors.
*   `inc_ref`, `dec_ref`: Atomic/Non-atomic RC operations.
*   `region_enter`, `region_exit`: Scope management.
*   `arena_alloc`: Bump-pointer allocation.
*   `free_tree`: Recursive destructor for Tree shapes.

This model combines the performance of manual C memory management with the safety of modern smart pointers, all orchestrated by the compiler.
