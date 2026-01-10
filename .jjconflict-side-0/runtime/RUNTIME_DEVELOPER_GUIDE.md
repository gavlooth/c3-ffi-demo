# OmniLisp Runtime Developer Guide

A comprehensive guide for developers working with the OmniLisp C runtime. This document covers the object model, memory management strategies, region infrastructure, concurrency primitives, and FFI.

## Table of Contents

1. [Overview](#overview)
2. [Building and Testing](#building-and-testing)
3. [Core Object Model](#core-object-model)
4. [Memory Management](#memory-management)
5. [Region Infrastructure](#region-infrastructure)
6. [Borrowed References and Handles](#borrowed-references-and-handles)
7. [Concurrency Primitives](#concurrency-primitives) (Tier 1: OS Threads)
8. [Fiber Concurrency](#green-thread-concurrency-tier-2) (Tier 2: Continuations)
9. [FFI and External Handles](#ffi-and-external-handles)
10. [Best Practices](#best-practices)
11. [API Reference](#api-reference)

---

## Overview

The OmniLisp runtime targets **C99 + POSIX + extensions** and implements
**CTRR (Compile-Time Region Reclamation)**. Unlike garbage-collected runtimes,
OmniLisp does not perform heap-wide tracing. Instead, the compiler schedules
region lifetimes and inserts explicit runtime operations for **escapes**
(transmigration) and **borrows** (tethering).

### Key Principles

- **No stop-the-world GC**: All memory management is deterministic
- **No heap scanning**: no collector that searches for garbage globally
- **Compile-time scheduling**: regions, escape repair, and borrows are explicit
- **Local work only**: per-region / per-root operations, not global pauses

### Target Platform

```bash
# Compile with C99 + POSIX
gcc -std=c99 -pthread -o program program.c -L. -lomnilisp
clang -std=c99 -pthread -o program program.c -L. -lomnilisp
```

---

## Building and Testing

### Build the Runtime

```bash
cd runtime
make clean && make
```

### Run Tests

```bash
cd runtime/tests
make clean && make
./run_tests        # Run all tests
```

### Test Files

| File | Tests | Description |
|------|-------|-------------|
| `test_main.c` | 446 | Main test suite |
| `test_iregion.c` | 17 | IRegion vtable tests |
| `test_weak_control_blocks.c` | 21 | Weak reference tests |
| `test_transmigration.c` | 17 | Object graph migration |
| `test_external_handles.c` | 27 | FFI handle tests |

---

## Core Object Model

### Object Structure

All OmniLisp values are represented by the `Obj` structure:

```c
typedef struct Obj {
    Generation generation;  /* IPGE generation for memory safety */
    int mark;               /* Reference count or mark bit */
    int tag;                /* ObjTag identifying the type */
    int is_pair;            /* Quick check for pairs */
    int scc_id;             /* SCC identifier (-1 = none) */
    unsigned int scan_tag;  /* For Tarjan's algorithm */
    union {
        long i;             /* Integer/char value */
        double f;           /* Float value */
        struct { struct Obj *a, *b; };  /* Pair car/cdr */
        void* ptr;          /* Symbol string, closure, etc. */
    };
} Obj;
```

### Object Tags

```c
typedef enum {
    TAG_INT = 1,      /* Integer */
    TAG_FLOAT,        /* Floating point */
    TAG_CHAR,         /* Character */
    TAG_PAIR,         /* Cons cell */
    TAG_SYM,          /* Symbol */
    TAG_BOX,          /* Mutable box */
    TAG_CLOSURE,      /* Lambda/closure */
    TAG_CHANNEL,      /* CSP channel */
    TAG_ERROR,        /* Error value */
    TAG_ATOM,         /* Atomic reference */
    TAG_THREAD        /* Thread handle */
} ObjTag;

#define TAG_USER_BASE 1000  /* User-defined types start here */
```

### Tagged Pointers (Immediate Values)

Small values are encoded directly in the pointer, avoiding heap allocation:

```c
/* 3-bit tag scheme */
#define IMM_TAG_PTR   0x0ULL  /* Heap pointer (aligned) */
#define IMM_TAG_INT   0x1ULL  /* 61-bit signed integer */
#define IMM_TAG_CHAR  0x2ULL  /* 21-bit Unicode codepoint */
#define IMM_TAG_BOOL  0x3ULL  /* Boolean */

/* Check if value is immediate (not heap-allocated) */
#define IS_IMMEDIATE(p)      (GET_IMM_TAG(p) != IMM_TAG_PTR)
#define IS_IMMEDIATE_INT(p)  (GET_IMM_TAG(p) == IMM_TAG_INT)

/* Create immediate integer (no allocation!) */
#define MAKE_INT_IMM(n)      ((Obj*)(((uintptr_t)(n) << 3) | IMM_TAG_INT))
#define INT_IMM_VALUE(p)     ((long)((intptr_t)(p) >> 3))

/* Booleans */
#define OMNI_FALSE         ((Obj*)(((uintptr_t)0 << 3) | IMM_TAG_BOOL))
#define OMNI_TRUE          ((Obj*)(((uintptr_t)1 << 3) | IMM_TAG_BOOL))
```

### Constructors

```c
/* Heap-allocated constructors */
Obj* mk_int(long i);           /* Integer */
Obj* mk_float(double f);       /* Float */
Obj* mk_char(long c);          /* Character (may use immediate) */
Obj* mk_pair(Obj* a, Obj* b);  /* Pair (cons cell) */
Obj* mk_sym(const char* s);    /* Symbol */
Obj* mk_box(Obj* v);           /* Mutable box */
Obj* mk_error(const char* msg);/* Error */

/* Immediate constructors (no heap allocation) */
Obj* mk_int_unboxed(long i);   /* Immediate integer */
Obj* mk_bool(int b);           /* Immediate boolean */
Obj* mk_char_unboxed(long c);  /* Immediate character */

/* Stack-pool allocation (for short-lived values) */
Obj* mk_int_stack(long i);
Obj* mk_float_stack(double f);
Obj* mk_char_stack(long c);

/* Arena allocation (for cyclic structures) */
Obj* arena_mk_int(Arena* a, long i);
Obj* arena_mk_pair(Arena* a, Obj* car, Obj* cdr);
```

### Accessors

```c
/* Safe extraction - handles both boxed and immediate */
long obj_to_int(Obj* p);       /* Extract integer */
int obj_to_bool(Obj* p);       /* Extract boolean */
long obj_to_char_val(Obj* p);  /* Extract character */
int obj_tag(Obj* p);           /* Get tag (handles immediates) */

/* Pair accessors */
Obj* obj_car(Obj* p);          /* Get car of pair */
Obj* obj_cdr(Obj* p);          /* Get cdr of pair */

/* Box accessors */
Obj* box_get(Obj* b);          /* Get boxed value */
void box_set(Obj* b, Obj* v);  /* Set boxed value */

/* Type checks */
int is_int(Obj* p);            /* Is integer? */
int is_char(Obj* p);           /* Is character? */
int is_nil(Obj* x);            /* Is NULL? */
int is_nothing(Obj* x);        /* Is nothing? */
int is_stack_obj(Obj* x);      /* From stack pool? */
int is_truthy(Obj* x);         /* Truthy for conditionals? */
```

---

## Memory Management

> **Normative CTRR contract:** `docs/CTRR.md`  
> **Transmigration contract (detailed):** `runtime/docs/CTRR_TRANSMIGRATION.md`

The runtime implements **CTRR (Compile-Time Region Reclamation)**. CTRR is not a
garbage collector:

- There is no runtime “scan the heap and collect garbage” phase.
- Memory work is explicit and local: per-region, per-root transmigration, and
  per-borrow pinning.

### What the compiler relies on (the real contract)

1) **Region scope**

```c
Region* region_create(void);
void region_exit(Region* r);
```

2) **Escape repair**

```c
void* transmigrate(void* root, Region* src_region, Region* dest_region);
```

3) **Borrow windows (pinning)**

```c
void region_tether_start(Region* r);
void region_tether_end(Region* r);
```

4) **External region liveness (cross-scope / cross-thread reachability)**

```c
void region_retain_internal(Region* r);
void region_release_internal(Region* r);
```

### What makes a region reclaimable

The runtime may reclaim/reuse a region’s storage only when all of these are
true:

- the region has exited (`scope_alive == false`)
- there are no external owners (`external_rc == 0`)
- there are no active borrows (`tether_count == 0`)

### Clarifying legacy APIs (do not build new features on these)

The public header (`runtime/include/omni.h`) still exposes object-level helpers
like `inc_ref`, `dec_ref`, `free_tree`, `free_unique`, and `flush_freelist`.

In the current runtime implementation (`runtime/src/runtime.c`), these are not
the primary correctness mechanism for reclamation (some are stubs / do not free
objects).

New work should treat **regions + transmigration + tethering** as the required
foundation.

---

## Region Infrastructure

This section documents the concrete Region control block used by CTRR codegen.

**Primary implementation files (authoritative):**

- `runtime/src/memory/region_core.h` / `runtime/src/memory/region_core.c`
- `runtime/src/memory/region_value.c` (region-aware constructors)
- `runtime/src/memory/transmigrate.c` (escape repair)
- `runtime/src/memory/region_metadata.h` / `runtime/src/memory/region_metadata.c`

### Region control block (RCB)

At runtime, a `Region` owns:

- an arena allocator for bulk storage (`Arena arena`)
- an inline buffer fast path for tiny allocations
- escape/borrow state used to decide when it is safe to reclaim/reuse:
  - `external_rc` (strong references keeping the region alive)
  - `tether_count` (active borrows pinning the region)
  - `scope_alive` (compiler-scheduled scope state)

See the real struct definition in `runtime/src/memory/region_core.h`.

### Allocation (`region_alloc`)

`region_alloc(r, size)` is defined as `static inline` in
`runtime/src/memory/region_core.h` and is expected to be on the hot path:

- <= 64 bytes: allocate from the inline buffer (8-byte aligned)
- otherwise: allocate from the arena

### Retain/release (external liveness)

Regions that are referenced across scopes/threads must be retained:

```c
void region_retain_internal(Region* r);
void region_release_internal(Region* r);
```

### Tethering (borrow pinning)

Borrow windows pin regions:

```c
void region_tether_start(Region* r);
void region_tether_end(Region* r);
```

Implementation uses C “extensions” that are standard practice in GCC/Clang:

- `__atomic_*` builtins for atomics (C99-compatible)
- `__thread` TLS for per-thread tether caches and region pools

### RegionRef (fat pointer keepalive)

```c
typedef struct RegionRef {
    void*  ptr;
    Region* region;
} RegionRef;

void region_retain(RegionRef ref);
void region_release(RegionRef ref);
```

---

## Phase 24 Performance Optimizations

The following optimizations were completed in Phase 24 (2026-01-08), achieving 2.7x-21.1x speedups across 9 implementations.

### Inline Allocation Buffer

Small objects (< 64 bytes) are allocated from a 512-byte inline buffer in the Region struct, avoiding arena allocation overhead:

```c
// FAST PATH: Inline buffer for small objects (< 64 bytes)
#define REGION_INLINE_BUF_SIZE 512
#define REGION_INLINE_MAX_ALLOC 64

// Allocation automatically uses inline buffer when size <= 64 bytes
Obj* obj = region_alloc(r, sizeof(Obj));  // Uses inline buffer if available
```

**Performance:** 6.99x faster than raw malloc for small objects.

### Specialized Constructors

Batch-allocate entire data structures in a single call:

```c
// Batch list allocation
Obj* mk_list_region(Region* r, int n);  // Allocate n cons cells in one block

// Batch tree allocation
Obj* mk_tree_region(Region* r, int depth);  // Allocate complete tree

// Array with single allocation
Obj* mk_array_region_batch(Region* r, int capacity);  // Array + data in one block

// Dict with single allocation
Obj* mk_dict_region_batch(Region* r, int initial_buckets);  // Dict + buckets in one block
```

**Performance:** 5.55-6.32x speedup for list/tree construction. 3x fewer allocations for arrays/dicts.

### Bitmap-Based Cycle Detection

Replaced uthash with bitmap-based cycle detection for transmigration:

```c
// O(1) bitmap operations using Arena allocation
void* transmigrate(void* root, Region* src, Region* dest);

// Incremental (batched) transmigration for sparse access patterns
void* transmigrate_incremental(void* root, Region* src, Region* dest, int chunk_size);
```

**Performance:** 2.7-12.5x speedup for transmigration. 10-100x faster cycle detection vs uthash.

### Region Splicing

O(1) transfer of arena chunks for result-only regions:

```c
// Automatically detects when source has external_rc==0 and scope_alive==false
// Transfers entire arena chunk instead of copying
void* transmigrate(void* root, Region* src, Region* dest);
```

**Performance:** 1.4-1.9x speedup for functional programming patterns. O(1) regardless of size.

### Region Pool

Thread-local pool of reusable regions to avoid malloc/free overhead:

```c
// Automatically returns regions to pool when destroyed
// Pool size: 32 regions per thread
Region* r = region_create();  // Reuses pooled region if available
region_exit(r);  // Returns to pool instead of freeing
```

**Performance:** 21.1x speedup for small region creation.

### Inline Fastpaths

Critical hot path functions are marked `static inline` for zero call overhead:

```c
// region_alloc is now static inline in region_core.h
static inline void* region_alloc(Region* r, size_t size);

// hashmap_get and hashmap_contains are now static inline in hashmap.h
static inline void* hashmap_get(HashMap* map, void* key);
static inline int hashmap_contains(HashMap* map, void* key);
```

**Performance:** Eliminated function call overhead for hot allocation and hash operations.

---

## Borrowed References and Handles

### IPGE (In-Place Generational Evolution)

Each object has a generation ID that evolves on free:

```c
/* Compact mode: 16-bit generation (default) */
typedef uint16_t Generation;

/* Robust mode: 64-bit generation (compile with -DIPGE_ROBUST_MODE=1) */
typedef uint64_t Generation;

/* Generation evolves on each alloc/free */
static inline Generation ipge_evolve(Generation gen);
```

### BorrowRef (Legacy)

Heap-allocated borrowed reference:

```c
typedef struct BorrowRef {
    struct GenObj* target;       /* Legacy GenObj system */
    Generation remembered_gen;   /* Snapshot at borrow time */
    const char* source_desc;     /* Debug description */
    Obj* ipge_target;            /* IPGE: Direct Obj* */
    Handle handle;               /* Sound handle for pool objects */
} BorrowRef;

BorrowRef* borrow_create(Obj* obj, const char* source_desc);
int borrow_is_valid(BorrowRef* ref);
Obj* borrow_get(BorrowRef* ref);
void borrow_release(BorrowRef* ref);
void borrow_invalidate_obj(Obj* obj);
```

### Slot Pool (Sound Validation)

Slots are never freed to system allocator, making generation validation sound:

```c
/* Create pool */
SlotPool* slot_pool_create(size_t payload_size, size_t alignment,
                           size_t initial_slots);
void slot_pool_destroy(SlotPool* pool);

/* Allocate/free slots */
Slot* slot_pool_alloc(SlotPool* pool);
void slot_pool_free(SlotPool* pool, Slot* slot);

/* Handle operations */
Handle slot_pool_make_handle(SlotPool* pool, Slot* slot);

/* Fast O(1) validation */
static inline bool handle_is_valid(Handle h) {
    if (h == HANDLE_INVALID) return false;
    Slot* s = handle_to_slot(h);
    if (!s) return false;
    if (handle_get_tag(h) != s->tag8) return false;
    if (s->flags != SLOT_IN_USE) return false;
    return true;
}

/* Get payload with validation */
static inline void* handle_get_payload(Handle h);

/* Global pool */
SlotPool* slot_pool_global(void);
void slot_pool_global_init(void);
void slot_pool_global_destroy(void);
```

### Handle-Based Allocation

```c
/* Allocate Obj from stable slot pool */
Obj* handle_alloc_obj(void);
void handle_free_obj(Obj* obj);
bool handle_is_pool_obj(Obj* obj);

/* Create handle from Obj */
Handle handle_from_obj(Obj* obj);

/* Safe dereference (returns NULL if invalid) */
Obj* handle_deref_obj(Handle h);
```

### Weak Reference Control Blocks

O(1) invalidation for weak references:

```c
/* Control block operations */
WeakControlBlock* weak_cb_new(void* target, void (*destructor)(void*));
void weak_cb_inc_ref(WeakControlBlock* cb);
void weak_cb_dec_ref(WeakControlBlock* cb);
void weak_cb_invalidate(WeakControlBlock* cb);
bool weak_cb_is_valid(WeakControlBlock* cb);
void* weak_cb_get_target(WeakControlBlock* cb);

/* Weak handle operations */
WeakHandle* weak_handle_new(WeakControlBlock* cb);
WeakHandle* weak_handle_clone(WeakHandle* h);
void weak_handle_free(WeakHandle* h);
bool weak_handle_is_valid(WeakHandle* h);
void* weak_handle_lock(WeakHandle* h);  /* Returns target if valid */

/* Weak reference table */
WeakRefTable* weak_table_new(size_t capacity);
void weak_table_free(WeakRefTable* table);
uint32_t weak_table_register(WeakRefTable* table, void* target,
                             void (*destructor)(void*));
void weak_table_invalidate(WeakRefTable* table, uint32_t index);
WeakHandle* weak_table_get_handle(WeakRefTable* table, uint32_t index);
void* weak_table_lock(WeakRefTable* table, uint32_t index, uint64_t generation);

/* Global table singleton */
WeakRefTable* weak_table_global(void);
```

---

## Concurrency Primitives

### Channels (CSP-style)

Ownership transfer semantics:

```c
/* Create channel */
Obj* make_channel(int capacity);  /* 0 = unbuffered */

/* Send (TRANSFERS OWNERSHIP - caller must not use value after) */
int channel_send(Obj* ch, Obj* value);

/* Receive (RECEIVES OWNERSHIP - caller must free when done) */
Obj* channel_recv(Obj* ch);

/* Close channel */
void channel_close(Obj* ch);
```

Example:

```c
Obj* ch = make_channel(10);  /* Buffered channel */

// Producer
Obj* msg = mk_pair(mk_int(42), NULL);
channel_send(ch, msg);  // Ownership transferred!
// msg is now invalid - do NOT use or free

// Consumer
Obj* received = channel_recv(ch);  // We now own this
// ... use received ...
dec_ref(received);  // Our responsibility to free

channel_close(ch);
dec_ref(ch);
```

### Atoms (Atomic References)

Thread-safe mutable references:

```c
/* Create atom */
Obj* make_atom(Obj* initial);

/* Read current value */
Obj* atom_deref(Obj* atom);

/* Set new value */
Obj* atom_reset(Obj* atom, Obj* new_val);

/* Compare-and-swap */
bool atom_cas(Obj* atom, Obj* expected, Obj* new_val);

/* Swap with function */
Obj* atom_swap(Obj* atom, Obj* (*fn)(Obj*));
```

### Goroutines

Spawn lightweight threads:

```c
/* Spawn goroutine with closure */
void spawn_goroutine(Obj* closure, Obj** captured, int count);
```

Example:

```c
Obj* closure = mk_closure(my_worker_fn, captures, refs, cap_count, 0);
spawn_goroutine(closure, captures, cap_count);
// closure ownership transferred to goroutine
```

### Closures

```c
/* Create closure */
Obj* mk_closure(ClosureFn fn, Obj** captures, BorrowRef** refs,
                int count, int arity);

/* Call closure */
Obj* call_closure(Obj* clos, Obj** args, int arg_count);

/* Release closure internals */
void closure_release(Closure* c);

/* Validate captures are still valid */
int closure_validate(Closure* c);
```

---

## Fiber Concurrency (Tier 2)

The runtime provides two tiers of concurrency:
- **Tier 1 (above)**: OS threads via pthreads - for blocking FFI, CPU-bound work
- **Tier 2 (this section)**: Fibers via continuations - for massive concurrency

### Two-Tier Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Tier 1: OS Threads (pthreads)                              │
│  - make_channel, channel_send/recv                          │
│  - make_atom, atom_deref/reset/cas                          │
│  - spawn_goroutine                                          │
│  Use for: blocking FFI, CPU-bound, true parallelism         │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  Tier 2: Fibers (continuations)                      │
│  - make_fiber_chan, fiber_send/recv                         │
│  - make_gen, gen_next, generator_yield                      │
│  - make_promise_val, promise_await_val                      │
│  - fiber_spawn_task, spawn_async_task                       │
│  Use for: 1M+ tasks, async I/O, generators                  │
└─────────────────────────────────────────────────────────────┘
```

### Performance Comparison

| Metric | OS Thread | Fiber |
|--------|-----------|--------------|
| Task creation | ~10μs | ~100ns |
| Context switch | ~1μs | ~50ns |
| Memory per task | ~8KB minimum | ~100 bytes |
| Max tasks | ~10,000 | ~1,000,000+ |
| Blocking FFI | Native | Needs wrapper |

### Green Scheduler

Initialize once per OS thread:

```c
/* Initialize scheduler (call once per OS thread) */
void fiber_scheduler_init(void);

/* Run until all tasks complete */
void fiber_scheduler_run(void);

/* Step one task (for event loop integration) */
int fiber_scheduler_step(void);

/* Check if scheduler has pending tasks */
int fiber_scheduler_idle(void);

/* Cleanup */
void fiber_scheduler_shutdown(void);
```

### Green Channels

Lightweight channels without pthread overhead:

```c
/* Create green channel */
Obj* make_fiber_chan(int capacity);  /* 0 = rendezvous */

/* Send (parks task if no receiver) */
void fiber_send(Obj* ch, Obj* value);

/* Receive (parks task if no sender) */
Obj* fiber_recv(Obj* ch);

/* Non-blocking variants */
int fiber_try_send(Obj* ch, Obj* value);  /* Returns 1 on success */
Obj* fiber_try_recv(Obj* ch, int* ok);

/* Close channel */
void fiber_chan_close(Obj* ch);
```

### Green Tasks

Spawn lightweight cooperative tasks:

```c
/* Spawn fiber, returns task handle */
Obj* fiber_spawn_task(Obj* thunk);

/* Spawn and get completion promise */
Obj* spawn_async_task(Obj* thunk);

/* Cooperative yield (return to scheduler) */
void fiber_yield(void);
```

### Generators (Iterators)

Lazy sequences using continuations:

```c
/* Create generator from producer closure */
Obj* make_gen(Obj* producer);

/* Get next value (resumes generator) */
Obj* gen_next(Obj* gen);

/* Check if exhausted */
int gen_done(Obj* gen);

/* Close early */
void gen_close(Obj* gen);

/* Called from within producer to yield value */
void generator_yield(Obj* value);
```

Example generator:

```c
/* Producer: yields 0, 1, 4, 9, 16, ... */
Obj* squares_producer(Obj** args, int argc) {
    (void)args; (void)argc;
    for (int i = 0; i < 10; i++) {
        generator_yield(mk_int_unboxed(i * i));
    }
    return NULL;  /* Generator done */
}

void example(void) {
    fiber_scheduler_init();

    Obj* producer = mk_closure(squares_producer, NULL, NULL, 0, 0);
    Obj* gen = make_gen(producer);

    while (!gen_done(gen)) {
        Obj* val = gen_next(gen);
        printf("%ld ", obj_to_int(val));  /* 0 1 4 9 16 25 36 49 64 81 */
    }

    free_generator_obj(gen);
    fiber_scheduler_shutdown();
}
```

### Promises (Async/Await)

Async computation with promises:

```c
/* Create pending promise */
Obj* make_promise_val(void);

/* Resolve with value */
void promise_resolve_val(Obj* p, Obj* value);

/* Reject with error */
void promise_reject_val(Obj* p, Obj* error);

/* Await (parks current task until settled) */
Obj* promise_await_val(Obj* p);

/* Check if settled */
int promise_settled(Obj* p);
```

Example async/await:

```c
/* Async computation */
Obj* async_fetch(Obj** args, int argc) {
    /* Simulate async work */
    for (int i = 0; i < 5; i++) {
        fiber_yield();
    }
    return mk_int_unboxed(42);
}

void example(void) {
    fiber_scheduler_init();

    /* Spawn async task, get promise */
    Obj* thunk = mk_closure(async_fetch, NULL, NULL, 0, 0);
    Obj* promise = spawn_async_task(thunk);

    /* Run scheduler until promise resolves */
    while (!promise_settled(promise)) {
        fiber_scheduler_step();
    }

    /* Get result */
    Promise* p = (Promise*)promise->ptr;
    printf("Result: %ld\n", obj_to_int(p->value));

    free_promise_val(promise);
    fiber_scheduler_shutdown();
}
```

### Hybrid: OS Threads + Fibers

Run green schedulers on multiple OS threads:

```c
void* worker(void* arg) {
    int id = *(int*)arg;

    /* Each OS thread has its own green scheduler */
    fiber_scheduler_init();

    /* Spawn many fibers */
    for (int i = 0; i < 10000; i++) {
        fiber_spawn_task(my_task);
    }

    fiber_scheduler_run();
    fiber_scheduler_shutdown();
    return NULL;
}

void main(void) {
    pthread_t threads[4];
    int ids[4] = {0, 1, 2, 3};

    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, worker, &ids[i]);
    }

    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
}
```

---

## FFI and External Handles

### External Handle Table

Stable integer handles for FFI:

```c
typedef uint64_t ExternalHandle;  /* 32-bit index + 32-bit generation */

#define EXTERNAL_HANDLE_INVALID 0
#define EXTERNAL_HANDLE_INDEX(h) ((uint32_t)((h) & 0xFFFFFFFF))
#define EXTERNAL_HANDLE_GEN(h) ((uint32_t)(((h) >> 32) & 0xFFFFFFFF))

/* Table lifecycle */
ExternalHandleTable* external_table_new(size_t capacity);
void external_table_free(ExternalHandleTable* table);

/* Deterministic mode for replay/debugging */
void external_table_set_deterministic(ExternalHandleTable* table, bool on);

/* Handle operations */
ExternalHandle external_handle_create(ExternalHandleTable* table,
                                      void* object,
                                      void (*destructor)(void*));
void external_handle_release(ExternalHandleTable* table, ExternalHandle h);
void* external_handle_get(ExternalHandleTable* table, ExternalHandle h);
bool external_handle_is_valid(ExternalHandleTable* table, ExternalHandle h);

/* Bulk operations */
void external_table_clear(ExternalHandleTable* table);
size_t external_table_count(ExternalHandleTable* table);
void external_table_iterate(ExternalHandleTable* table,
                            void (*callback)(ExternalHandle, void*, void*),
                            void* user_data);

/* Global table singleton */
ExternalHandleTable* external_table_global(void);

/* FFI convenience functions */
uint64_t ffi_obj_to_handle(void* obj);
void* ffi_handle_to_obj(uint64_t handle);
void ffi_release_handle(uint64_t handle);
```

### Example: C Library Integration

```c
// Export OmniLisp object to C callback
void register_callback(void* context, void (*callback)(void*)) {
    ExternalHandle h = external_handle_create(
        external_table_global(),
        context,
        (void(*)(void*))dec_ref  // Destructor
    );
    // Store h instead of raw pointer
}

// In callback
void my_callback(uint64_t handle) {
    void* obj = ffi_handle_to_obj(handle);
    if (obj) {
        // Use object safely
    }
}

// Cleanup
ffi_release_handle(h);
```

### Transmigration

The CTRR escape repair operation is transmigration:

```c
void* transmigrate(void* root, Region* src_region, Region* dest_region);
```

Notes:

- This is **not** GC: it is an explicit operation on a single root.
- The compiler inserts calls at escape boundaries (return/capture/global store).
- The runtime may use O(1) splicing fast paths when safe; otherwise it performs a
  deep copy with cycle/sharing preservation.

Canonical reference: `runtime/docs/CTRR_TRANSMIGRATION.md`.

---

## Best Practices

### 1. Treat CTRR as the default

If you are unsure which memory “strategy” applies, assume CTRR and use regions:

| Scenario | What to do | Runtime primitive |
|---|---|---|
| Scope ends | make region reclaimable | `region_exit(r)` |
| Value must outlive its region | repair escape by copying/moving | `transmigrate(root, src, dst)` |
| Temporary cross-thread/cross-scope borrow | pin during the borrow window | `region_tether_start/end(r)` |
| Long-lived root needs keepalive | retain/release region | `region_retain/_internal`, `region_release/_internal` |

Avoid building new features on object-level `dec_ref/free_tree` behavior; the
region model is the core contract.

### 2. Use Immediates When Possible

```c
// Boxed integer (allocates an Obj in a region)
Obj* x = mk_int_region(r, 42);
// ... use x ...
// Lifetime is governed by the region, not per-object frees.

// Immediate integer (no allocation)
Obj* y = mk_int_unboxed(42);
// ... use y - no cleanup needed!
```

### 3. Prefer Stack Pool for Short-Lived Values

```c
// For temporary values that don't escape
Obj* temp = mk_int_stack(42);
// No cleanup needed for stack pool objects
```

### 4. Use Arenas for Complex Graphs

```c
void build_ast(void) {
    Arena* arena = arena_create();

    // Build complex tree with potential cycles
    Obj* ast = parse_with_arena(arena, source);

    // Process
    compile(ast);

    // Single O(1) cleanup
    arena_destroy(arena);
}
```

### 5. Validate Borrowed References

```c
BorrowRef* ref = borrow_create(obj, "callback context");

// Later...
if (borrow_is_valid(ref)) {
    Obj* ptr = borrow_get(ref);
    // Use ptr safely
    dec_ref(ptr);  // borrow_get increments ref count
} else {
    // Object was freed - handle gracefully
}

borrow_release(ref);
```

### 6. Use Handles for FFI

```c
// DON'T: Pass raw pointers to external code
void bad_register(void* raw_ptr);

// DO: Use stable handles
ExternalHandle h = external_handle_create(table, obj, destructor);
uint64_t handle_id = h;  // Safe to pass externally
```

### 7. Thread Safety with Channels

```c
// Ownership transfer is explicit
Obj* msg = create_message();
channel_send(ch, msg);
// msg is GONE - do not use!

Obj* received = channel_recv(ch);
// We now own received - must free when done
process(received);
dec_ref(received);
```

### 8. Region Hierarchy for Scope Safety

```c
// Inner regions can reference outer, not vice versa
region_enter();  // depth 1
    RegionObj* inner = region_alloc(data, NULL);

    region_enter();  // depth 2
        RegionObj* inner2 = region_alloc(data2, NULL);

        // OK: inner2 -> inner (depth 2 -> depth 1)
        region_create_ref(inner2, inner);

        // ERROR: inner -> inner2 would be scope violation
        // region_create_ref(inner, inner2);

    region_exit();  // inner2 invalidated

region_exit();  // inner invalidated
```

---

## API Reference

### Object Constructors

| Function | Description | Allocates |
|----------|-------------|-----------|
| `mk_int(long)` | Create integer | Heap |
| `mk_int_unboxed(long)` | Create immediate integer | None |
| `mk_int_stack(long)` | Create stack-pool integer | Stack pool |
| `mk_float(double)` | Create float | Heap |
| `mk_char(long)` | Create character | Maybe immediate |
| `mk_pair(Obj*, Obj*)` | Create pair | Heap |
| `mk_sym(const char*)` | Create symbol | Heap |
| `mk_box(Obj*)` | Create mutable box | Heap |
| `mk_error(const char*)` | Create error | Heap |
| `mk_closure(...)` | Create closure | Heap |
| `make_channel(int)` | Create channel | Heap |
| `make_atom(Obj*)` | Create atom | Heap |
| `arena_mk_int(Arena*, long)` | Create arena integer | Arena |
| `arena_mk_pair(Arena*, ...)` | Create arena pair | Arena |

### Memory Management

| Function | Description |
|----------|-------------|
| `inc_ref(Obj*)` | Increment reference count |
| `dec_ref(Obj*)` | Decrement, free if zero |
| `free_obj(Obj*)` | Add to free list |
| `free_tree(Obj*)` | Recursive free for trees |
| `free_unique(Obj*)` | Direct free (skip RC check) |
| `flush_freelist()` | Process pending frees |
| `safe_point()` | Check/process deferred work |

### Arena Operations

| Function | Description |
|----------|-------------|
| `arena_create()` | Create new arena |
| `arena_alloc(Arena*, size_t)` | Allocate from arena |
| `arena_reset(Arena*)` | Reset without freeing |
| `arena_destroy(Arena*)` | Free arena and contents |
| `arena_register_external(...)` | Register external pointer |

### Region Operations

| Function | Description |
|----------|-------------|
| `region_create()` | Create a new region (may reuse pooled region) |
| `region_exit(Region*)` | Mark region scope as exited (reclaimable when unpinned) |
| `region_tether_start(Region*)` | Pin region for a borrow window |
| `region_tether_end(Region*)` | Unpin region after borrow window |
| `region_retain(RegionRef)` | Keep region alive via fat pointer |
| `region_release(RegionRef)` | Release keepalive |
| `transmigrate(root, src, dst)` | Escape repair: move/copy graph to `dst` |
| `omni_get_global_region()` | Access the global compatibility region |

### Handle Operations

| Function | Description |
|----------|-------------|
| `handle_alloc_obj()` | Allocate from slot pool |
| `handle_free_obj(Obj*)` | Free to slot pool |
| `handle_from_obj(Obj*)` | Create handle |
| `handle_deref_obj(Handle)` | Safe dereference |
| `handle_is_valid(Handle)` | Check validity (inline) |
| `borrow_create(Obj*, const char*)` | Create borrow ref |
| `borrow_is_valid(BorrowRef*)` | Check validity |
| `borrow_get(BorrowRef*)` | Get object (inc_ref) |
| `borrow_release(BorrowRef*)` | Release borrow ref |

### Concurrency

| Function | Description |
|----------|-------------|
| `make_channel(int)` | Create channel |
| `channel_send(Obj*, Obj*)` | Send (transfers ownership) |
| `channel_recv(Obj*)` | Receive (takes ownership) |
| `channel_close(Obj*)` | Close channel |
| `make_atom(Obj*)` | Create atom |
| `atom_deref(Obj*)` | Read atom value |
| `atom_reset(Obj*, Obj*)` | Set atom value |
| `atom_cas(Obj*, Obj*, Obj*)` | Compare-and-swap |
| `spawn_thread(Obj*)` | Spawn OS thread |
| `thread_join(Obj*)` | Join OS thread |

### External Handles

| Function | Description |
|----------|-------------|
| `external_table_new(size_t)` | Create table |
| `external_table_free(...)` | Destroy table |
| `external_handle_create(...)` | Create handle |
| `external_handle_release(...)` | Release handle |
| `external_handle_get(...)` | Get object |
| `external_handle_is_valid(...)` | Check validity |
| `ffi_obj_to_handle(void*)` | Convert to handle |
| `ffi_handle_to_obj(uint64_t)` | Convert from handle |
| `ffi_release_handle(uint64_t)` | Release handle |

---

## Further Reading

- `docs/CTRR.md` - CTRR contract (project spec)
- `runtime/docs/CTRR_TRANSMIGRATION.md` - transmigration contract (runtime spec)
- [ASAP (paper term): As Static As Possible](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-908.pdf) - Proust, 2017
- [Perceus: Garbage Free Reference Counting](https://dl.acm.org/doi/10.1145/3453483.3454032) - PLDI 2021
- [Shape Analysis](https://www.semanticscholar.org/paper/Is-it-a-tree,-a-DAG,-or-a-cyclic-graph-Ghiya-Hendren/115be3be1d6df75ff4defe0d7810ca6e45402040) - Ghiya & Hendren, POPL 1996
- [Region-Based Memory Management](https://elsman.com/mlkit/) - Tofte & Talpin / MLKit
- [CactusRef](https://github.com/artichoke/cactusref) - Deterministic cycle collection
- [Lobster Memory Management](https://aardappel.github.io/lobster/memory_management.html) - Compile-time RC
