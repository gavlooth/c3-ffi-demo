# Symmetric RC: Unified Implementation Plan

**STATUS: ALL PHASES COMPLETE.** This system has evolved into **Component-Level Scope Tethering** (island unit model).

## Overview

This plan outlines the roadmap to upgrade the Symmetric RC system from a "heavy runtime fallback" to a "high-performance, thread-safe, and statically-optimized" cycle collector.

## Track A: Runtime Enhancements (Immediate Impact)

These changes modify `runtime/src/memory/symmetric.c` and `runtime/src/runtime.c`.

### Phase 1: Thread-Safety (P0)
**Goal:** Remove the global `SYM_CTX` and enable concurrent execution.
- [x] Move `SYM_CTX` to `static __thread` (TLS).
- [x] Update `sym_init()` to initialize TLS on thread start.
- [x] Update `sym_exit()` to cleanup TLS on thread exit.
- [x] **Verification:** Add multi-threaded test case in `runtime/tests/test_sym_concurrency.c`.

### Phase 2: Memory Optimization (P1)
**Goal:** Reduce the 40-byte overhead and malloc contention.
- [x] **Slab Allocator:** Implement `SymPool` for fixed-size `SymObj` allocation.
    - [x] 4KB blocks divided into `sizeof(SymObj)` chunks.
    - [x] Free list within chunks.
- [x] **Inline Refs:** Optimize `SymObj` struct.
    ```c
    struct SymObj {
        // ...
        union {
            SymObj* single;
            SymObj* tiny[3];
            SymObj** dynamic;
        };
        uint8_t ref_mode; // 0=single, 1=tiny, 2=dynamic
    };
    ```
- [x] **Verification:** Micro-benchmark memory usage with 1M objects.

### Phase 3: Embedding (P2 - Architecture)
**Goal:** Eliminate the wrapper entirely for specific types.
- [x] Define `CyclicObj` header that includes `SymObj` fields. (Implemented via optimized `SymObj`)
- [ ] Update runtime to detect `TAG_CYCLIC` and use embedded fields.

---

## Track B: Compiler Static Analysis (Static Strategy)

These changes modify `csrc/analysis/` to enable "Zero-Overhead Cycle Collection".

### Phase 4: Dominator Analysis (P1)
**Goal:** Determine which variable "owns" an SCC.
- [x] Implement `omni_compute_dominators(CFG* cfg)`.
- [x] Calculate Immediate Dominator (IDom) for each node.
- [x] Identify "Loop Headers" and "Entry Points" for cyclic regions.

### Phase 5: SCC Analysis (P1)
**Goal:** Statically identify cycles in the Control Flow Graph.
- [x] Implement Tarjan's or Kosaraju's algorithm on the CFG.
- [x] Output: `SCCInfo` struct identifying nodes in a cycle.

### Phase 6: Static Symmetric RC Pass (P2)
**Goal:** Replace runtime calls with static destructors.
- [x] **Pass Logic:**
    1. Identify SCC.
    2. Find Dominating Variable `V`.
    3. Check if SCC escapes `V`'s scope.
    4. If Local: Emit `free_scc_static(...)` at `V`'s last use.
    5. If Escaping: Fallback to Runtime Symmetric RC.
- [x] **Integration:** Added `omni_analyze_static_symmetric` to codegen flow.

---

## Roadmap

| Week | Track A (Runtime) | Track B (Compiler) |
|------|-------------------|--------------------|
| 1    | Thread-Local Context | Dominator Analysis |
| 2    | Slab Allocator | SCC Detection |
| 3    | Inline Refs | Static Pass Prototype |
| 4    | Optimization | Integration & Testing |

## Dependencies

- `csrc/analysis/analysis.h`: Needs `CFG` extensions.
- `runtime/src/memory/symmetric.c`: Needs major refactoring.
