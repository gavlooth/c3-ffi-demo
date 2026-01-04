# [LEGACY] Symmetric RC Enhancement Proposal

**STATUS: SUPERSEDED** by Component-Level Scope Tethering (v0.6.0).
The enhancements proposed here (Thread-Local Context, Slab Allocation, Inline Refs) have been fully implemented and integrated into the island-based unit model.

## Current State Analysis (2026-01-04)

The current Symmetric RC implementation (`runtime/src/memory/symmetric.c` and `runtime/src/runtime.c`) provides deterministic cycle collection but suffers from significant performance and scalability issues.

### 1. Memory Overhead
Each cycle-managed object requires a `SymObj` wrapper:
```c
struct SymObj {
    int external_rc;      // 4 bytes
    int internal_rc;      // 4 bytes
    SymObj** refs;        // 8 bytes (ptr to dynamic array)
    int ref_count;        // 4 bytes
    int ref_capacity;     // 4 bytes
    void* data;           // 8 bytes (payload)
    int freed;            // 4 bytes
    // Total: ~40 bytes per object + dynamic array overhead
};
```
This is larger than the `Obj` (32 bytes) it manages!

### 2. Allocation Pressure
- `sym_obj_new` calls `malloc` for every managed object.
- `sym_obj_add_ref` calls `realloc` when adding references.
- `sym_scope_own` calls `realloc` when adding objects to scope.
- Result: High fragmentation and allocator contention.

### 3. Thread Safety
The global context `SYM_CTX` is static and unsynchronized:
```c
static struct { ... } SYM_CTX = {NULL, NULL, 0, 0, 0, 0};
```
This makes Symmetric RC **unsafe for concurrent use**.

## Proposed Improvements

### Phase 1: Thread-Local Context (P0 - Safety)
Move `SYM_CTX` to thread-local storage (TLS) to allow concurrent Symmetric RC without locks.

```c
static __thread SymContext* _tls_sym_ctx = NULL;
```

### Phase 2: Slab/Pool Allocation (P1 - Performance)
Replace `malloc` with a slab allocator for `SymObj` nodes.
- Fixed-size `SymObj` allows perfect slab packing.
- O(1) allocation/free.
- drastic reduction in heap fragmentation.

### Phase 3: Inline Reference Storage (P1 - Memory)
Optimize `SymObj` to store small reference counts inline, avoiding `malloc` for the `refs` array in 90% of cases.

```c
struct SymObj {
    // ...
    union {
        SymObj* single_ref;      // Optimized for count=1
        SymObj* tiny_refs[3];    // Optimized for count<=3
        SymObj** dyn_refs;       // Fallback for N > 3
    };
};
```

### Phase 4: Integration with Obj (P2 - Architecture)
Instead of a wrapper `SymObj`, embed the Symmetric RC fields directly into `Obj` (or a `CyclicObj` variant) to eliminate the 40-byte overhead entirely.

## Implementation Plan

1. **Refactor**: Isolate `SYM_CTX` access behind a thread-aware getter.
2. **Optimize**: Implement `SymPool` for node allocation.
3. **Optimize**: Change `refs` to use inline storage for common cases.
4. **Benchmark**: Compare memory usage and throughput against baseline.
