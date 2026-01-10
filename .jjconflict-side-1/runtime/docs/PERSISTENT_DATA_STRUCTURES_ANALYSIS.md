# Functional Data Structures Analysis

**Date:** 2025-01-08
**Context:** Phase 24 Performance Optimization Complete - Discussion of Future Directions

## Summary

This document analyzes the need for **persistent/immutable data structures** in OmniLisp, particularly for:
- Functional programming patterns
- Tower of interpreters (metaprogramming)
- Domain-Specific Languages (DSLs)

## Conclusion

**Recommendation:** Defer persistent/immutable data structures until concrete need is demonstrated.

**Short-term approach:**
1. Add graph algorithms library (works with existing Dict/List)
2. Add simple mutable Graph type (for algorithm workloads)
3. Add snapshot/restore for Interpreter (using batched transmigration)

**Rationale:** Current Region-RC model with ASAP deallocation already provides scoped immutability. True persistent collections (structural sharing) are complex and should be added only when benchmarks show clear need.

---

## Problem Analysis

### What Users Actually Need

#### 1. Functional Purity vs. Practicality

**Common misconception:** Functional languages require 100% immutable data.

**Reality:** Most functional languages have both:
- **Mutable data** (for performance, convenience)
- **Immutable/persistent data** (for functional purity, concurrency)

**Examples:**
- **Clojure:** Has `defn` (pure) but also `atoms`, `refs`, `agents` (mutable state)
- **OCaml:** Has both mutable (`array`, `Hashtbl`) and immutable (`List`, `Map`) types
- **Haskell:** Has pure data but uses `STT` monad for localized mutation

**Insight:** Users need **choice**, not forced immutability.

#### 2. What "Immutable Graphs" Really Means

Most languages don't have "persistent graphs" as a primitive. Instead, they represent graphs using:

```clojure
;; Clojure: Graph as persistent map
(def graph {1 [2 3], 2 [4], 3 [4]})  ;; node -> neighbors

;; Update: O(log n), structural sharing
(def graph2 (assoc graph 1 [2 3 5]))
```

**Key realization:** You don't need a special "Graph" type. You need:
- **Persistent maps** (node → value/neighbors)
- **Persistent sets** (for neighbor lists)
- **Efficient structural sharing**

Trees are easy to make persistent. General graphs are **much harder**.

---

## Current OmniLisp Capabilities

### What We Already Have

✅ **Dict (hash map)** - Mutable but efficient
✅ **List (linked list)** - Mutable but functional-style
✅ **Region-RC memory model** - Scoped immutability
✅ **ASAP deallocation** - Automatic cleanup based on liveness
✅ **Batched transmigration** - Efficient state snapshots

### The Region Model Already Provides "Scoped Immutability"

```c
Region* r = region_create();

// Everything allocated in 'r' is effectively "immutable" during this scope
Obj* list = build_list_region(r, 1000);
Obj* tree = build_tree_region(r, 15);

// Pass 'list' to other functions - they can't deallocate individual elements
// Only the region owner decides when to free everything
region_exit(r);  // All gone at once
```

**This is already 80% of what users need from functional programming.**

---

## Three Approaches Compared

### Approach 1: Persistent/Immutable Collections (High Complexity)

**Implementation:** Hash Array Mapped Trie (HAMT) like Clojure

```c
typedef struct PersistentMap {
    TrieNode* root;
    int count;
    Region* region;
} PersistentMap;

// O(log n) update, structural sharing
PersistentMap* pmap_assoc(PersistentMap* m, Obj* key, Obj* value);
```

**Pros:**
- True functional semantics
- Structural sharing (log-time updates)
- Time-travel debugging (keep old versions)

**Cons:**
- **Very complex** to implement (path copying, trie nodes, rebalancing)
- **Memory overhead** (trie nodes, versioning)
- **Overkill** for graphs (trees are easier)
- **Incompatible** with Region-RC (requires RC or GC)

**Estimate:** 2000-5000 lines of complex code, 2-3 months work.

### Approach 2: Simple Mutable Graph Type (Low Complexity)

**Implementation:** Adjacency list with arrays

```c
typedef struct Graph {
    Region* region;
    Obj** nodes;              // Array of node data
    int** adjacency_lists;     //邻接表
    int node_count;
    int capacity;
} Graph;

Graph* graph_create(Region* r, int expected_nodes);
void graph_add_node(Graph* g, Obj* data);
void graph_add_edge(Graph* g, int from, int to);
Obj* graph_get_node(Graph* g, int index);  // O(1)
```

**Pros:**
- Simple to implement (500-1000 lines)
- O(1) adjacency lookup
- Cache-friendly
- Works with Region-RC

**Cons:**
- Not functional (mutable)
- Fixed size (or expensive to resize)
- No structural sharing

**Estimate:** 500-1000 lines, 1-2 weeks work.

### Approach 3: Graph Algorithms Library (Lowest Complexity)

**Implementation:** Algorithms that work with existing Dict/List

```c
// Represent graph as Dict: node -> list of neighbors
// Obj* graph = make_dict();
// dict_set(graph, 'node1, list(2, 3, 4));

Obj* graph_bfs(Obj* adj_dict, Obj* start_key, VisitorFn visitor);
Obj* graph_dfs(Obj* adj_dict, Obj* start_key, VisitorFn visitor);
Obj* graph_topological_sort(Obj* adj_dict);
bool graph_has_cycle(Obj* adj_dict);
```

**Pros:**
- Zero new data structures
- Works with existing Dict
- Low risk (500-1000 lines)
- Users choose representation

**Cons:**
- Dict lookup is O(log n) vs O(1) for arrays
- Less ergonomic than dedicated Graph type

**Estimate:** 500-1000 lines, 1 week work.

---

## Tower of Interpreters Needs

### What Metaprogramming Actually Needs

For "Tower of Interpreters" and time-travel debugging, you need:

#### 1. Snapshot/Restore (Already Implemented!)

```c
typedef struct InterpreterState {
    Obj* env;           // Current environment
    Obj* code;          // Current code
    Region* region;      // All allocations
} InterpreterState;

// Snapshot: Copy entire state to new region (O(1) with splicing!)
InterpreterState* interp_snapshot(InterpreterState* current) {
    Region* snapshot_region = region_create();
    InterpreterState* snapshot = region_alloc(snapshot_region, sizeof(InterpreterState));

    // Transmigrate env and code (already optimized!)
    snapshot->env = transmigrate(current->env, current->region, snapshot_region);
    snapshot->code = transmigrate(current->code, current->region, snapshot_region);
    snapshot->region = snapshot_region;

    return snapshot;
}

// Restore: Just switch back to snapshot state
void interp_restore(InterpreterState* target, InterpreterState* snapshot) {
    target->env = snapshot->env;
    target->code = snapshot->code;
}
```

**This is already supported by:**
- ✅ Batched transmigration (chunk processing for large graphs)
- ✅ Region splicing (O(1) for result-only regions)
- ✅ Bitmap cycle detection (fast graph copying)

#### 2. Incremental Execution (Not Snapshots)

For DSLs that need backtracking:

```c
// Constraint solving with backtracking
bool solve(Obj* state) {
    if (is_solved(state)) return true;
    if (is_dead_end(state)) return false;

    for (each possible move) {
        Obj* new_state = apply_move(state, move);
        if (solve(new_state)) return true;
        // No explicit undo needed - regions handle cleanup!
    }

    return false;
}
```

**Region-RC already handles this:**
- Create region for each branch
- Region automatically freed when branch fails
- No explicit "undo" or "rollback" needed

---

## DSL Requirements Analysis

### Common DSL Patterns

#### 1. Query DSL (SQL-like)

```lisp
;; DSL for querying data
(select :from users :where (> age 18))
```

**Needs:**
- Immutable query plan (for optimization/caching)
- Not necessarily immutable data structures

**Solution:** Build query plan using existing Dict/List types.

#### 2. Logic Programming DSL (Prolog-like)

```lisp
;; Logic rules
(rule (ancestor X Y)
      (parent X Y))
      (ancestor X Z)
      (parent Z Y))
```

**Needs:**
- Backtracking search (Region-RC handles this!)
- Unification algorithm
- Not necessarily persistent graphs

**Solution:** Represent knowledge base as Dict, use recursive search with regions.

#### 3. Probabilistic DSL (Graphical Models)

```lisp
;; Bayesian network
(define model
  (node 'A (parents B C))
  (node 'B (parents))
  (node 'C (parents)))
```

**Needs:**
- Graph representation
- Inference algorithms (belief propagation)
- Mutable during inference, immutable at end

**Solution:** Dedicated Graph type (mutable), then freeze result.

---

## Recommendations

### Short Term (Next 1-2 Months)

#### 1. Add Graph Algorithms Library ✅ RECOMMENDED

**Priority:** HIGH
**Risk:** LOW
**Effort:** 1 week

**What:**
```c
// File: runtime/src/graph.h

// Graph algorithms using existing Dict representation
typedef struct GraphResult {
    Obj* visited;      // List of visited nodes (order)
    Obj* distances;    // Dict mapping node -> distance
    Obj* parents;      // Dict for path reconstruction
} GraphResult;

// Breadth-First Search
GraphResult* graph_bfs(Region* r, Obj* adj_dict, Obj* start_key);

// Depth-First Search (iterative, avoids stack overflow)
GraphResult* graph_dfs(Region* r, Obj* adj_dict, Obj* start_key);

// Topological Sort (for DAGs)
Obj* graph_topological_sort(Region* r, Obj* adj_dict);

// Cycle Detection
bool graph_has_cycle(Obj* adj_dict);

// Shortest Path (Dijkstra)
GraphResult* graph_shortest_path(Region* r, Obj* adj_dict, Obj* start, Obj* end);
```

**Why:**
- Zero new data structures
- Works with existing Dict/List
- Users choose representation (array vs. adjacency list vs. matrix)
- Low risk, high value

#### 2. Add Simple Mutable Graph Type ✅ RECOMMENDED

**Priority:** MEDIUM
**Risk:** LOW
**Effort:** 1-2 weeks

**What:**
```c
// File: runtime/src/graph_struct.h

typedef struct Graph {
    Region* region;
    Obj** node_data;        // Array of per-node data
    int* adjacency;         // Adjacency matrix (node_count × node_count)
    int node_count;
    int capacity;
} Graph;

Graph* graph_create(Region* r, int expected_nodes);
int graph_add_node(Graph* g, Obj* data);           // Returns node index
bool graph_add_edge(Graph* g, int from, int to);
Obj* graph_get_node(Graph* g, int index);          // O(1)
bool graph_has_edge(Graph* g, int from, int to);    // O(1)
Graph* graph_subgraph(Graph* g, int* nodes, int count);
```

**Why:**
- O(1) edge lookup (cache-friendly)
- Simple to understand
- Works with Region-RC
- Good for algorithm workloads

#### 3. Add Interpreter Snapshot API ✅ RECOMMENDED

**Priority:** HIGH
**Risk:** LOW
**Effort:** 3-5 days

**What:**
```c
// File: runtime/src/interpreter.h

typedef struct InterpreterSnapshot {
    Region* region;
    Obj* env;
    Obj* code;
    Obj* stack;
    // ... other state
} InterpreterSnapshot;

InterpreterSnapshot* interp_snapshot(Interpreter* interp);
void interp_restore(Interpreter* interp, InterpreterSnapshot* snapshot);
void interp_free_snapshot(InterpreterSnapshot* snap);
```

**Why:**
- Enables time-travel debugging
- Supports backtracking for DSLs
- Uses existing optimized transmigrate
- Clean API for metaprogramming

---

### Medium Term (3-6 Months)

#### 4. Measure Real Workloads

**Before** implementing persistent collections:

1. **Create benchmark suite** for DSL workloads:
   - Graph traversal benchmarks
   - Interpreter snapshot overhead
   - Pattern matching performance

2. **Profile actual user code**:
   - What are the hot paths?
   - Where is mutation causing problems?
   - Are users hitting walls with current approach?

3. **Define success criteria**:
   - "Persistent maps are needed if:
     - Immutable map updates are 10x+ slower than mutable
     - Users request functional semantics
     - Concurrent access patterns emerge"

#### 5. Experimental Persistent Map (If Measurements Show Need)

**Priority:** LOW
**Risk:** HIGH
**Effort:** 2-3 months
**Prerequisite:** Measured need

**What:**
```c
// Experimental persistent hash trie map
typedef struct PMap {
    TrieNode* root;
    int count;
    int version;        // For structural sharing tracking
    Region* region;
} PMap;

// O(log n) update, returns new map
PMap* pmap_assoc(PMap* m, Obj* key, Obj* value);

// O(log n) lookup
Obj* pmap_get(PMap* m, Obj* key);

// Structural sharing with old map
void pmap_release(PMap* m);  // Decrement refs, free if 0
```

**Implementation notes:**
- Use HAMT (Hash Array Mapped Trie) like Clojure
- Path copying for updates
- Region-RC integration: each trie node is reference counted
- Mark as "experimental" feature

---

### Long Term (6-12 Months)

#### 6. Persistent Graph (If Persistent Maps Successful)

**Priority:** VERY LOW
**Risk:** VERY HIGH
**Effort:** 4-6 months
**Prerequisite:** Persistent maps proven in production

**Approaches:**
1. **Functional graph library** (incremental updates)
2. **Immutable graph views** (read-only views of mutable graphs)
3. **Persistent adjacency** (combine persistent maps with graph algorithms)

**Why defer:**
- Much harder than persistent maps
- User needs unclear
- Can build on top of persistent maps if needed

---

## Implementation Priority

### Phase 1: Immediate (This Week)

1. ✅ **Graph algorithms library** - Start with BFS/DFS, cycle detection
2. ✅ **Interpreter snapshot API** - Simple wrapper around transmigrate

### Phase 2: Short-term (This Month)

3. ✅ **Simple mutable Graph type** - For algorithm workloads
4. ✅ **Benchmark suite** - Measure DSL/graph performance

### Phase 3: Medium-term (3-6 months)

5. ⏸️ **Measure real workloads** - Collect data on actual usage
6. ⏸️ **Evaluate persistent collections** - Based on measurements

### Phase 4: Long-term (6-12 months)

7. ❌ **Persistent map** - Only if measurements show need
8. ❌ **Persistent graph** - Only if persistent maps successful

---

## Technical Notes

### Why Persistent Graphs Are Hard

#### Problem 1: Cycles in Structural Sharing

```
A → B → C
↑   ↓
   ← D ←
```

To persistently add edge A→E:
1. Copy A (since it's modified)
2. A points to B and D (must they be copied too?)
3. B points to C (must C be copied?)
4. D points to B (cycle!)

**Result:** Almost entire graph must be copied, negating sharing benefit.

#### Problem 2: Path Copying Overhead

For trees, path copying is O(log n). For graphs:
- Each node has multiple parents
- Updating one edge requires copying all paths
- Can easily become O(n) instead of O(log n)

**Alternative:** Use persistent maps (already O(log n)), don't try to make graphs persistent.

#### Problem 3: Memory Model Mismatch

Region-RC is designed for:
- Scoped lifetimes (region_exit)
- Bulk deallocation (ASAP)

Persistent collections need:
- Per-object reference counting
- Individual deallocation
- Complex garbage collection

**Mismatch:** Fundamental conflict in design goals.

---

## References

### Functional Data Structures

- **Clojure:** Persistent data structures using HAMT
  - Rich Hickey, "Clojure's Persistent Data Structures"
  - https://clojure.org/reference/persistent_maps

- **Okasaki:** Purely Functional Data Structures
  - Chris Okasaki, "Purely Functional Data Structures"
  - PhD thesis, novel persistent queue, deque

- **Bacon:** Erlang/OTP in Action
  - Functional concurrency patterns
  - Actor model vs. shared state

### Graph Algorithms

- **Cormen:** Introduction to Algorithms
  - Chapters 22-25: Graph algorithms
  - BFS, DFS, shortest paths, MST

- **Sedgewick:** Algorithms in C++
  - Part 5: Graph algorithms
  - Practical implementations

---

## Conclusion

**Key insight:** OmniLisp's Region-RC model already provides 80% of the benefits of functional programming (scoped immutability, automatic cleanup). The remaining 20% (true persistent collections) should be added only when there's measured need.

**Recommended action:**
1. ✅ Implement graph algorithms library (works with existing types)
2. ✅ Add simple mutable Graph type (for algorithms)
3. ✅ Add interpreter snapshot API (metaprogramming)
4. ⏸️ Defer persistent collections until benchmarks prove necessity

**Philosophical stance:**
> "Perfection is achieved not when there is nothing more to add, but when there is nothing left to take away." - Antoine de Saint-Exupéry

We've achieved excellent performance with Phase 24 optimizations. Adding more complexity before it's needed would violate the YAGNI principle.
