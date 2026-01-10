# OmniLisp Runtime Roadmap

## Current Status

The OmniLisp runtime is a mature C99 + POSIX runtime with:

- **Core object model** with tagged pointers and immediate values
- **Multiple memory strategies**: ASAP, RC, SCC, Arena, Region-RC (RC-G)
- **Region-based memory**: IRegion interface with 5 allocator types
- **Two-tier concurrency**: OS threads (pthreads) + Fibers (continuations)
- **Generators/Iterators**: Lazy sequences via continuations
- **Promises/Async**: Async/await pattern support
- **FFI**: External handles, weak references, borrowed refs
- **Phase 24 Performance Optimizations**: 2.7x-21.1x speedups across 9 optimizations

---

## Completed Features

### Phase 1: Core Runtime (Complete)
- [x] Object model with tagged pointers
- [x] Immediate values (int, char, bool - no allocation)
- [x] Reference counting with free-list
- [x] IPGE generational references
- [x] Basic primitives (arithmetic, comparison, I/O)

### Phase 2: Memory Management (Complete)
- [x] ASAP deallocation (compiler-inserted frees)
- [x] Shape-aware strategies (Tree, DAG, Cyclic)
- [x] SCC detection (Tarjan's algorithm)
- [x] Arena allocator for scoped cycles
- [x] Region-RC (RC-G) model with tethering
- [x] Deferred RC for batched operations

### Phase 7: Performance Optimization (Complete - 2026-01-08)
- [x] **Phase 24 Optimizations** (9 implementations, 2.7x-21.1x speedups):
  - [x] T-opt-inline-allocation: 512-byte inline buffer (6.99x speedup)
  - [x] T-opt-specialized-constructors: Batch list/tree allocation (5.55-6.32x speedup)
  - [x] T-opt-transmigrate-batch: Chunked processing with bitmap cycle detection (2.7-12.5x speedup)
  - [x] T-opt-region-splicing: O(1) result-only region transfer (1.4-1.9x speedup)
  - [x] T-opt-region-pool: Thread-local region reuse (21.1x speedup)
  - [x] T-opt-batch-alloc-array: Single allocation for Array+data (3x fewer allocations)
  - [x] T-opt-inline-alloc-fastpath: Eliminated call overhead for region_alloc
  - [x] T-opt-inline-hash-fastpath: Eliminated call overhead for hashmap operations
  - [x] Bitmap-based cycle detection: 10-100x faster than uthash approach

### Phase 3: Region Infrastructure (Complete)
- [x] IRegion abstract interface
- [x] Arena region (bump allocation)
- [x] Linear region (LIFO)
- [x] Offset region (serializable)
- [x] Pool region (fixed-size objects)
- [x] Region freezing and cloning
- [x] Transmigration (deep copy across regions)

### Phase 4: Safety Infrastructure (Complete)
- [x] Borrowed references with invalidation
- [x] Weak references with control blocks
- [x] Handle system (slot pool)
- [x] External handles for FFI
- [x] Constraint refs (debug validation)

### Phase 5: OS Thread Concurrency (Complete)
- [x] pthread-based channels (CSP)
- [x] Atoms (thread-safe state)
- [x] spawn_goroutine for OS threads

### Phase 6: Fiber Concurrency (Complete - NEW)
- [x] CEK-style continuation frames
- [x] Delimited continuations (prompt/control)
- [x] Fiber scheduler
- [x] Continuation-based channels
- [x] Generators/iterators via continuations
- [x] Promises with async/await

---

## Upcoming Features

### Phase 8: Extended Concurrency
- [ ] Select (multi-channel wait)
- [ ] Timeouts with timer wheel
- [ ] Structured concurrency (nurseries)
- [ ] Cancellation tokens
- [ ] Async streams (async iterators)

### Phase 9: Debugging & Profiling
- [ ] Continuation stack traces
- [ ] Task profiling (time in each task)
- [ ] Memory profiling per region
- [ ] Deadlock detection for channels
- [ ] Reference cycle visualization

### Phase 10: Platform Extensions
- [ ] Windows IOCP integration
- [ ] Linux io_uring integration
- [ ] macOS kqueue integration
- [ ] WASM target support

### Future: Data Structures (Deferred)
- [ ] Graph algorithms library (recommended first step - see PERSISTENT_DATA_STRUCTURES_ANALYSIS.md)
- [ ] Simple mutable Graph type (if needed)
- [ ] Persistent/immutable collections (only if benchmarks show clear need)

### Phase 11: Platform Extensions
- [ ] Windows IOCP integration
- [ ] Linux io_uring integration
- [ ] macOS kqueue integration
- [ ] WASM target support

---

## Architecture Decisions

### Two-Tier Concurrency Model

```
┌─────────────────────────────────────────────────────────────┐
│  Tier 1: OS Threads (pthreads)                              │
│                                                             │
│  When to use:                                               │
│  - True parallelism (multi-core)                            │
│  - Blocking FFI/library calls                               │
│  - CPU-intensive computation                                │
│                                                             │
│  API: spawn_goroutine, make_channel, make_atom              │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  Tier 2: Fibers (continuations)                      │
│                                                             │
│  When to use:                                               │
│  - 1000s+ concurrent tasks                                  │
│  - Async I/O multiplexing                                   │
│  - Lazy sequences (generators)                              │
│  - Cooperative scheduling                                   │
│                                                             │
│  API: fiber_spawn_task, make_fiber_chan, make_gen           │
└─────────────────────────────────────────────────────────────┘
```

### Performance Targets

| Metric | Target | Current |
|--------|--------|---------|
| Task creation | <200ns | ~100ns |
| Context switch | <100ns | ~50ns |
| Memory per task | <200 bytes | ~100 bytes |
| Max concurrent tasks | 1M+ | 1M+ |
| Channel send/recv | <500ns | ~200ns |

---

## File Structure

```
runtime/
├── src/
│   ├── runtime.c              # Core runtime (3800+ LOC)
│   └── memory/
│       ├── slot_pool.c/h      # Sound handle allocation
│       ├── handle.c/h         # Handle system
│       ├── region.c/h         # IRegion infrastructure
│       └── continuation.c/h   # Fibers, generators, promises
├── include/
│   └── omnilisp.h               # Public API
├── tests/
│   ├── test_main.c            # 446 tests
│   └── ...
├── RUNTIME_DEVELOPER_GUIDE.md # Comprehensive guide
├── QUICK_REFERENCE.md         # API quick reference
├── EXAMPLES.md                # Code examples
└── ROADMAP.md                 # This file
```

---

## Contributing

1. All new features need tests in `tests/`
2. Update QUICK_REFERENCE.md for new APIs
3. Add examples to EXAMPLES.md
4. Compile with `-Wall -Wextra -Werror`
5. Run full test suite before PR

---

## References

- [ASAP Memory Management](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-908.pdf)
- [Perceus: Garbage Free RC](https://dl.acm.org/doi/10.1145/3453483.3454032)
- [Delimited Continuations](https://www.cs.indiana.edu/~dyb/pubs/monadicDC.pdf)
- [Vale Virtual Threads](https://verdagon.dev/blog/seamless-fearless-structured-concurrency)
