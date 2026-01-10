# SMR for Runtime Structures

**Status:** Normative specification for Safe Memory Reclamation (SMR) in OmniLisp runtime  
**Applies to:** `runtime/src/` (internal data structures only, NOT heap)  
**Last Updated:** 2026-01-10  
**Related:** `runtime/docs/REGION_THREADING_MODEL.md` (threading contract), `runtime/docs/REGION_RC_MODEL.md` (Region-RC for heap)

---

## 1) Purpose

Safe Memory Reclamation (SMR) is a technique for **concurrent data structures** that allows:

1. **Lock-free reads:** Readers don't block writers (or other readers)
2. **Safe memory reuse:** Nodes can be freed after all readers have seen them
3. **No stop-the-world:** No global pauses for reclamation

**Critical Distinction:**
- SMR here applies **only to internal runtime data structures** (metadata registries, intern tables, global module map)
- SMR is **NOT applied to the heap** (heap uses CTRR + Region-RC)

---

## 2) SMR Target Inventory

### 2.1 Candidate Internal Structures

The following runtime structures are candidates for SMR:

| Structure | Current Implementation | Read/Write Ratio | Expected Contention | SMR Benefit |
|-----------|----------------------|-------------------|-------------------|--------------|
| **Metadata Registry** | Global hash map | High read (90%), Low write (10%) | Low-Medium | Lock-free reads faster |
| **Intern Table** | Global hash map | Very High read (99%), Very Low write (1%) | Low | Lock-free reads significantly faster |
| **Global Module Map** | Global dict | Medium read (70%), Medium write (30%) | Medium | Lock-free reads faster |
| **Symbol Table** | Global hash map | High read (95%), Low write (5%) | Low | Lock-free reads faster |
| **Channel Queue** | Per-channel lock | High read (50%), High write (50%) | High | Lock-free significantly faster |
| **GC Worklist** (if ever added) | Global queue | Medium read (40%), Medium write (60%) | High | Lock-free faster |

### 2.2 Current Synchronization Mechanisms

**Before SMR:**
```c
// Global module map (current implementation)
pthread_mutex_t module_map_lock;

// Read operation (blocking)
pthread_mutex_lock(&module_map_lock);
Module* m = module_map_get(name);
pthread_mutex_unlock(&module_map_lock);

// Write operation (blocking)
pthread_mutex_lock(&module_map_lock);
module_map_set(name, m);
pthread_mutex_unlock(&module_map_lock);
```

**After SMR:**
```c
// Global module map (SMR implementation)
// Readers are lock-free (just read pointer)
Module* m = module_map_get_read_only(name);

// Writers are lock-free (use RCU-style update)
module_map_set_lock_free(name, m);
```

---

## 3) SMR Decision Matrix

### 3.1 Decision Criteria

For each structure, evaluate:

| Criterion | Question | Impact on Decision |
|-----------|-----------|-------------------|
| **Read/Write Ratio** | How often do reads vs. writes happen? | High read ratio → SMR beneficial |
| **Critical Path** | Is this on a hot code path? | Yes → SMR beneficial |
| **Contention** | Do multiple threads contend frequently? | Yes → SMR beneficial |
| **Node Lifetime** | How long do nodes live? | Short → SMR beneficial |
| **Implementation Complexity** | How hard to retrofit? | High → May defer SMR |

### 3.2 Decision Matrix

| Structure | Read/Write Ratio | Critical Path? | Contention? | Complexity | Recommendation |
|-----------|-------------------|----------------|--------------|-------------|----------------|
| Metadata Registry | 90/10 | Yes | Low-Medium | Medium | **QSBR** (Phase 1) |
| Intern Table | 99/1 | Yes | Low | Medium | **QSBR** (Phase 1) |
| Global Module Map | 70/30 | Yes | Medium | Low | **QSBR** (Phase 2) |
| Symbol Table | 95/5 | No | Low | Low | **Hazard Pointers** (Phase 2) |
| Channel Queue | 50/50 | Yes | High | High | **QSBR** (Phase 2) or **Lock** (keep as-is) |

### 3.3 Rationale per Structure

**Metadata Registry:**
- **Why QSBR:** Very high read ratio, low write ratio
- **QSBR Fit:** Good (many quiescent points between bytecode steps)
- **Priority:** Phase 1 (low-hanging fruit)

**Intern Table:**
- **Why QSBR:** Extremely high read ratio (99% reads), very low write ratio
- **QSBR Fit:** Excellent (reads dominate)
- **Priority:** Phase 1 (highest impact)

**Global Module Map:**
- **Why QSBR:** Moderate read ratio, critical path (module lookups)
- **QSBR Fit:** Good (module loading not too frequent)
- **Priority:** Phase 2 (good balance of benefit vs. complexity)

**Symbol Table:**
- **Why Hazard Pointers:** Symbol interning is less critical (not on hot path)
- **QSBR Fit:** Poor (less quiescent points during symbol interning)
- **Priority:** Phase 2 (if needed, otherwise keep simple lock)

**Channel Queue:**
- **Why Lock or QSBR:** High contention, 50/50 read/write
- **QSBR Fit:** Marginal (writes are frequent, QSBR benefit reduced)
- **Priority:** Phase 2 or defer (measure first, then decide)

---

## 4) QSBR (Quiescent State-Based Reclamation)

### 4.1 QSBR Concept

QSBR is a variant of **Read-Copy-Update (RCU)** for userspace:

1. **Readers:** Never block, just read pointers
2. **Writers:** Update data in-place (copy-on-write for complex structures)
3. **Quiescent Points:** Threads report "I'm at a safe point" (not holding any references)
4. **Reclamation:** When all threads report quiescent, freed nodes can be reclaimed

**Key Insight:**
- Memory is reclaimed **after all threads have passed a quiescent point**
- No need to track individual references per thread

### 4.2 QSBR vs. Alternatives

| Technique | Read Path | Write Path | Reclamation | Best For |
|-----------|-----------|-------------|-------------|-----------|
| **QSBR** | Lock-free (read pointer) | Copy-on-write | Global quiescent check | High read, low write |
| **Hazard Pointers** | Lock-free (per-thread list) | Lock-free | Per-node hazard check | Mixed read/write |
| **Publish-on-Ping** | Lock-free | Lock-free | Ping-based reclamation | Low contention |
| **Lock (pthread_mutex)** | Blocking | Blocking | Immediate | Low contention, simple |

### 4.3 QSBR Implementation Sketch

```c
// Global QSBR state
typedef struct {
    uint64_t global_epoch;        // Current global epoch (monotonic)
    pthread_mutex_t lock;          // Protects global_epoch
} QSBR_Global;

// Per-thread QSBR state
typedef struct {
    uint64_t local_epoch;        // Thread's last reported epoch
    bool in_critical_section;      // Is thread in critical section?
} QSBR_Thread;

// Thread reports quiescent point
void qsbr_quiescent(QSBR_Thread* t) {
    pthread_mutex_lock(&qsbr_global.lock);
    t->local_epoch = qsbr_global.global_epoch;
    pthread_mutex_unlock(&qsbr_global.lock);
}

// Writer advances epoch
void qsbr_advance_epoch(void) {
    pthread_mutex_lock(&qsbr_global.lock);
    qsbr_global.global_epoch++;
    pthread_mutex_unlock(&qsbr_global.lock);
}

// Reclaim nodes from epoch E
void qsbr_reclaim(uint64_t reclaim_epoch, FreeList* nodes) {
    // All threads have local_epoch >= reclaim_epoch?
    // If yes, safe to free all nodes in 'nodes'
}
```

---

## 5) Quiescent Points in OmniLisp

### 5.1 Where Are Quiescent Points?

In OmniLisp, **quiescent points** occur when a thread is not holding any references to SMR-protected data structures:

| Location | Quiescent Point? | Rationale |
|-----------|------------------|-----------|
| **End of bytecode step** | **YES** | Thread just finished executing instruction, no locks held |
| **End of tether window** | **YES** | Thread just exited borrow, no active references |
| **Start of blocking I/O** | **YES** | Thread about to block, won't touch SMR structures |
| **Return to event loop** | **YES** | Thread returning to scheduler, no active references |
| **Inside critical section** | **NO** | Thread holding lock or reference, NOT safe |
| **Inside long-running computation** | **NO** | Thread actively using SMR structures, NOT safe |

### 5.2 Interaction with Region Ownership

Quiescent points must respect **region ownership**:

**Rule 1:**
> A thread is NOT quiescent if it holds active references (tethers, external_rc) to regions that own SMR structures.

**Example:**
```lisp
;; Thread 1: Holds tether to region R (which owns intern table)
(let ((region (region-create))
      (table (intern-table region)))
  (region-tether-start region)
  ;; Thread 1 is NOT quiescent here (holds tether)
  (do-computation)
  (region-tether-end region))
;; Thread 1 is NOW quiescent (tether released)
```

**Rule 2:**
> Quiescent reporting must happen **after** `region_tether_end()` and **before** accessing any SMR structure.

### 5.3 Quiescent Point Implementation

```c
// In bytecode interpreter loop
void eval_bytecode(void) {
    for (;;) {
        // Execute one bytecode instruction
        bytecode = fetch_next();
        execute(bytecode);
        
        // After each instruction, report quiescent
        qsbr_quiescent(&thread_state->qsbr);
    }
}

// In tether operations
void region_tether_end(Region* r) {
    r->tether_count--;
    
    // Report quiescent after releasing tether
    qsbr_quiescent(&thread_state->qsbr);
}

// In blocking I/O
void do_blocking_io(void) {
    // Report quiescent before blocking
    qsbr_quiescent(&thread_state->qsbr);
    
    // Block on I/O
    io_wait();
    
    // Report quiescent after unblocking
    qsbr_quiescent(&thread_state->qsbr);
}
```

---

## 6) Concurrency Scenarios

### 6.1 Scenario 1: Reader-Writer on Intern Table

**Setup:**
- **Thread A:** Writer (adds new interned symbol "foo")
- **Thread B:** Reader (looks up "foo")

**Before SMR:**
```c
// Writer (Thread A) - BLOCKS Reader
pthread_mutex_lock(&intern_table_lock);
intern_table_add("foo");
pthread_mutex_unlock(&intern_table_lock);

// Reader (Thread B) - BLOCKED by Writer
pthread_mutex_lock(&intern_table_lock);  // Waits for Thread A
Symbol* s = intern_table_get("foo");
pthread_mutex_unlock(&intern_table_lock);
```

**After SMR (QSBR):**
```c
// Writer (Thread A) - LOCK-FREE
// Copy current intern table
InternTable* new_table = copy_intern_table(old_table);
// Add new symbol
intern_table_add(new_table, "foo");
// Publish new table (atomic pointer swap)
publish_intern_table(new_table);

// Reader (Thread B) - LOCK-FREE (no blocking)
Symbol* s = intern_table_get_read_only("foo");
// Just reads the table pointer, no lock!
```

**Reclamation:**
- Old table is added to epoch E's free list
- When all threads report quiescent for epoch E, old table freed

### 6.2 Scenario 2: Multiple Readers on Metadata Registry

**Setup:**
- **Threads A, B, C:** Readers (query metadata)
- **Thread D:** Writer (update metadata)

**Before SMR:**
```
Time | A | B | C | D
-----|---|---|---|---
0    |   |   |   | Acquire lock
1    | Block | Block | Block | Update metadata
2    | Block | Block | Block | Release lock
3    | Read | Read | Read |
```

**After SMR (QSBR):**
```
Time | A | B | C | D
-----|---|---|---|---
0    |   |   |   | Copy registry
1    | Read | Read | Read | Publish new registry
2    | Read | Read | Read | Add old to free list
3    | Quiescent | Quiescent | Quiescent |
4    |   |   |   | Reclaim old registry
```

**Result:** All readers proceed without blocking!

---

## 7) Benchmark Plan

### 7.1 Microbenchmark: Intern Table Lookup

**Objective:** Measure read performance improvement with QSBR

**Setup:**
```c
// Benchmark setup
InternTable* table = create_intern_table();
// Pre-populate with 10,000 symbols
for (int i = 0; i < 10000; i++) {
    intern_add(table, make_symbol(i));
}

// Benchmark function
void benchmark_lookup(InternTable* table) {
    uint64_t start = rdtsc();
    for (int i = 0; i < 1000000; i++) {
        intern_get(table, "symbol_5000");  // Hot cache
    }
    uint64_t end = rdtsc();
    report_time(end - start);
}
```

**Variants:**
1. **Baseline:** `pthread_mutex`-protected table
2. **QSBR:** Lock-free read table

**Metrics:**
- **Throughput:** Lookups per second
- **Latency:** Lookup time (P50, P95, P99)
- **Scalability:** Speedup as thread count increases (1, 2, 4, 8 threads)

**Expected Results:**
| Threads | Baseline (lookup/μs) | QSBR (lookup/μs) | Speedup |
|---------|----------------------|-------------------|---------|
| 1       | 100                  | 95                | 1.05x   |
| 2       | 250                  | 100               | 2.5x    |
| 4       | 600                  | 105               | 5.7x    |
| 8       | 1200                 | 110               | 10.9x   |

**Compile:**
```bash
gcc -std=c99 -pthread -O3 -march=native benchmark.c -o benchmark
```

**Run:**
```bash
./benchmark --threads 1 --mode baseline
./benchmark --threads 2 --mode baseline
./benchmark --threads 4 --mode baseline
./benchmark --threads 8 --mode baseline

./benchmark --threads 1 --mode qsbr
./benchmark --threads 2 --mode qsbr
./benchmark --threads 4 --mode qsbr
./benchmark --threads 8 --mode qsbr
```

### 7.2 Microbenchmark: Global Module Map

**Objective:** Measure read/write performance improvement with QSBR

**Setup:**
```c
// Benchmark setup
ModuleMap* map = create_module_map();
// Pre-populate with 100 modules
for (int i = 0; i < 100; i++) {
    module_add(map, make_module(i));
}

// Mixed read/write workload (70% reads, 30% writes)
void benchmark_mixed(ModuleMap* map) {
    for (int i = 0; i < 1000000; i++) {
        if (random() % 10 < 7) {
            module_get(map, "module_50");  // Read
        } else {
            module_add(map, make_module(i % 200));  // Write
        }
    }
}
```

**Variants:**
1. **Baseline:** `pthread_mutex`-protected map
2. **QSBR:** Lock-free read map

**Metrics:**
- **Throughput:** Operations per second
- **Read Latency:** Get operation time
- **Write Latency:** Add operation time

**Expected Results:**
| Threads | Baseline (ops/s) | QSBR (ops/s) | Speedup |
|---------|------------------|----------------|---------|
| 1       | 50,000           | 48,000         | 0.96x   |
| 2       | 40,000           | 70,000         | 1.75x   |
| 4       | 25,000           | 120,000        | 4.8x    |
| 8       | 15,000           | 200,000        | 13.3x   |

---

## 8) Implementation Phases

### Phase 1: QSBR for Metadata Registry + Intern Table

**Objective:** Implement QSBR for highest-benefit structures

**Tasks:**
1. Implement QSBR infrastructure (`qsbr.c`, `qsbr.h`)
2. Add `qsbr_quiescent()` calls at bytecode step end
3. Convert Metadata Registry to QSBR
4. Convert Intern Table to QSBR

**Files to modify:**
- Add: `runtime/src/smr/qsbr.c`, `runtime/src/smr/qsbr.h`
- Add: `runtime/src/smr/metadata_registry_qsbr.c`
- Modify: `runtime/src/intern/intern_table.c` (add QSBR variant)
- Modify: `runtime/src/bytecode/interpreter.c` (add quiescent calls)

**Expected benefit:** 5-10x speedup for multi-threaded workloads with high read ratios

### Phase 2: QSBR for Global Module Map + Alternatives Review

**Objective:** Extend QSBR to moderate-benefit structures, evaluate alternatives

**Tasks:**
1. Convert Global Module Map to QSBR
2. Evaluate Symbol Table (keep lock or use hazard pointers)
3. Evaluate Channel Queue (keep lock or use QSBR)
4. Document alternatives (hazard pointers, publish-on-ping) in appendix

**Files to modify:**
- Modify: `runtime/src/module/global_map.c`
- Modify: `runtime/src/symbol/symbol_table.c` (or keep as-is)
- Modify: `runtime/src/channel/channel.c` (or keep as-is)
- Update: `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` (append alternatives)

**Expected benefit:** 2-5x speedup for multi-threaded workloads

### Phase 3: Comprehensive Testing + Performance Validation

**Objective:** Verify correctness and measure performance gains

**Tasks:**
1. Add SMR-specific tests (memory reclamation, quiescent points)
2. Add concurrency stress tests (many threads, high contention)
3. Run benchmark suite (from Section 7)
4. Publish results and adjust design if needed

**Files to modify:**
- Add: `tests/test_smr_qsbr.omni`
- Add: `tests/test_smr_intern_table.omni`
- Add: `tests/bench_smr_scaling.omni`
- Update: `docs/BENCHMARK_RESULTS.md`

---

## 9) Alternatives (Appendix)

### 9.1 Hazard Pointers

**Concept:**
- Each thread maintains a "hazard pointer list" of nodes it's currently accessing
- Before freeing a node, check if any thread has it in hazard list
- If not safe to free, defer

**Pros:**
- Works without quiescent points (good for long-running computations)
- Per-node reclamation (fine-grained)

**Cons:**
- Per-thread overhead (maintain hazard list)
- Slower reads (must update hazard list)
- More complex reclamation logic

**When to Use:**
- Low read ratio
- Missing natural quiescent points
- Short node lifetimes

**In OmniLisp:**
- Use for **Symbol Table** (less critical, not on hot path)
- Keep as **alternative** if QSBR proves insufficient

### 9.2 Hyaline (2019)

**Concept:**
- Epoch-based reclamation like QSBR, but with **ping-based** quiescent detection
- Threads "ping" each other to check if safe to reclaim
- No global epoch counter

**Pros:**
- No global epoch counter (reduces contention)
- Decentralized quiescent detection

**Cons:**
- More complex (need ping protocol)
- Higher overhead for many threads

**When to Use:**
- Very high contention (many threads)
- Global epoch bottleneck

**In OmniLisp:**
- Consider if **Channel Queue** shows high contention
- Keep as **alternative** if QSBR epoch contention is high

### 9.3 Publish-on-Ping (2025)

**Concept:**
- Writer publishes new version via a "ping" token
- Readers respond to ping by releasing old references
- Reclamation happens after all readers respond

**Pros:**
- Low latency (readers don't block)
- Simple reclamation (ping-then-free)

**Cons:**
- Requires readers to respond to pings (complex coordination)
- Higher latency for writes (must wait for responses)

**When to Use:**
- Low contention
- Need simple reclamation protocol

**In OmniLisp:**
- Not recommended (complex coordination)
- Keep as **alternative** for research

---

## 10) "Done Means" for SMR Implementation

SMR is complete only when:

1. **QSBR infrastructure works:** All phases (register, quiescent, advance, reclaim) tested
2. **At least one structure converted:** Metadata Registry or Intern Table using QSBR
3. **All tests pass:** Memory reclamation tests, concurrency stress tests
4. **Performance measured:** Benchmark suite shows 5-10x speedup for multi-threaded workloads
5. **Threading model documented:** `REGION_THREADING_MODEL.md` defines quiescent points and ownership
6. **Alternatives documented:** Hazard pointers, Hyaline, publish-on-ping in appendix

---

## 11) References

- liburcu (Userspace RCU): https://liburcu.org/
- QSBR overview: https://lwn.net/Articles/573424/
- Hyaline (2019): https://arxiv.org/abs/1905.07903
- Publish on Ping (2025): https://arxiv.org/abs/2501.04250
- `runtime/docs/REGION_THREADING_MODEL.md` - Threading contract (to be written)
