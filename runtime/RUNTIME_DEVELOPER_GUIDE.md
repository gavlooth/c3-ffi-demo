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

The OmniLisp runtime is a C99 + POSIX runtime implementing **ASAP (As Static As Possible)** memory management. Unlike garbage-collected runtimes, OmniLisp performs all deallocation decisions at compile time, inserting `free()` calls at optimal points.

### Key Principles

- **No stop-the-world GC**: All memory management is deterministic
- **No heap scanning**: O(1) or O(cycle_size) operations only
- **Compile-time analysis**: Shape analysis determines deallocation strategy
- **Tiered safety**: Different strategies for different data patterns

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
./run_tests        # Run all 446 tests
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

### ASAP Strategy Overview

OmniLisp uses a **Hybrid Strategy** where the compiler selects the optimal tool for the data shape:

```
┌─────────────────────────────────────────────────────────────────┐
│                    COMPILE-TIME ANALYSIS                         │
│  Shape Analysis ──► TREE / DAG / CYCLIC                         │
│  Escape Analysis ──► LOCAL / ESCAPING                           │
└─────────────────────────────────────────────────────────────────┘
                              │
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
       **TREE**             **DAG**            **CYCLIC**
   (Unique/Unshared)    (Shared/Acyclic)    (Back-Edges)
          │                   │                   │
          │                   │           ┌───────┴───────┐
          │                   │           ▼               ▼
          │                   │        **BROKEN**      **UNBROKEN**
          │                   │      (Weak Refs)     (Strong Cycle)
          │                   │           │               │
          │                   │           │        ┌──────┴──────┐
          │                   │           │        ▼             ▼
          │                   │           │     **LOCAL**    **ESCAPING**
          ▼                   ▼           ▼    (Scope-Bound) (Heap-Bound)
      Pure ASAP           Standard RC     RC     Arena Alloc   Component
     (free_tree)          (dec_ref)    (dec_ref) (destroy)     Tethering
```

### Strategy Hierarchy (Fastest to Most Capable)

1.  **Pure ASAP** (`free_tree`):
    *   For **Trees** (lists, records).
    *   **Zero overhead**. Static recursive free.
2.  **Standard RC** (`dec_ref`):
    *   For **DAGs** (shared data) or Cycles broken by **Weak Refs**.
    *   Standard atomic refcounting.
3.  **Arena Allocation** (`arena_destroy`):
    *   For **Local Cyclic** data (doesn't escape function).
    *   O(1) bulk free.
4.  **Component Tethering** (`sym_release_handle`):
    *   For **Escaping Cyclic** data (complex graphs).
    *   Treats SCC as a unit. Zero-cost access via `sym_tether`.

### Reference Counting

```c
/* Standard reference counting */
void inc_ref(Obj* x);    /* Increment reference count */
void dec_ref(Obj* x);    /* Decrement, free if zero */

/* Direct free for proven-unique references (Lobster-style) */
void free_unique(Obj* x);  /* Skip RC check, free immediately */

/* Tree deallocation (recursive free) */
void free_tree(Obj* x);    /* Free tree structure immediately */

/* Deferred/SCC release */
void deferred_release(Obj* x);  /* Add to free list */
void release_with_scc(Obj* obj);/* Handle SCC membership */
```

### Deferred Reference Counting

For complex structures, decrements can be deferred to safe points:

```c
/* Defer a decrement operation */
void defer_decrement(Obj* obj);

/* Process up to batch_size decrements (bounded work) */
void process_deferred(void);

/* Flush all pending decrements */
void flush_deferred(void);

/* Check if processing needed */
int should_process_deferred(void);

/* Call at function boundaries */
void safe_point(void);

/* Configure batch size */
void set_deferred_batch_size(int size);
```

### Free List

```c
/* Add to dynamic free list */
void free_obj(Obj* x);

/* Process all pending frees */
void flush_freelist(void);
```

### Arena Allocator

For cyclic structures that don't escape their scope:

```c
/* Create arena with default block size */
Arena* arena_create(void);

/* Allocate within arena (8-byte aligned) */
void* arena_alloc(Arena* a, size_t size);

/* Register external pointer for cleanup */
void arena_register_external(Arena* a, void* ptr, void (*cleanup)(void*));

/* Reset arena for reuse (doesn't free memory) */
void arena_reset(Arena* a);

/* Destroy arena and all allocations */
void arena_destroy(Arena* a);
```

Example usage:

```c
void process_graph(void) {
    Arena* arena = arena_create();

    // All allocations from arena
    Obj* node1 = arena_mk_pair(arena, arena_mk_int(arena, 1), NULL);
    Obj* node2 = arena_mk_pair(arena, arena_mk_int(arena, 2), node1);
    node1->b = node2;  // Cycle! No problem with arenas.

    // Use the graph...
    analyze(node1);

    // O(1) bulk deallocation
    arena_destroy(arena);
}
```

### SCC-Based Reference Counting

For frozen (immutable) cyclic structures:

```c
/* Create an SCC (Strongly Connected Component) */
SCC* create_scc(void);

/* Add object to SCC */
void scc_add_member(SCC* scc, Obj* obj);

/* Freeze SCC (mark as immutable) */
void freeze_scc(SCC* scc);

/* Find SCC by ID */
SCC* find_scc(int id);

/* Release SCC (decrements ref count, frees if zero) */
void release_scc(SCC* scc);

/* Detect and freeze SCCs starting from root */
void detect_and_freeze_sccs(Obj* root);
```

### Component-Level Scope Tethering

For unbroken cycles with O(1) collection and zero-cost tethered access:

```c
/* Component Lifecycle */
SymComponent* sym_component_new(void);
void sym_component_add_member(SymComponent* c, SymObj* obj);

/* Boundary Operations (ASAP called) */
void sym_acquire_handle(SymComponent* c);
void sym_release_handle(SymComponent* c);

/* Scope Tethering (Zero-cost access) */
SymTetherToken sym_tether_begin(SymComponent* c);
void sym_tether_end(SymTetherToken token);

/* Get underlying Obj* */
Obj* sym_get_data(SymObj* obj);
```

Key insight: Cyclic islands (SCCs) are treated as single units ("Components").

```c
void example(void) {
    SymComponent* c = sym_component_new();
    sym_acquire_handle(c); // ASAP manages this handle

    SymObj* a = sym_alloc(mk_int(1));
    SymObj* b = sym_alloc(mk_int(2));
    sym_component_add_member(c, a);
    sym_component_add_member(c, b);
    
    // Create internal cycle
    a->refs[0] = b;
    b->refs[0] = a;

    {
        // Zero-cost access block
        SymTetherToken t = sym_tether_begin(c);
        process(a); 
        process(b);
        sym_tether_end(t);
    }

    sym_release_handle(c);  // Both a and b freed immediately
}
```

---

## Region Infrastructure

The region system provides Vale/Ada/SPARK-style scope hierarchy validation.

### IRegion Interface

Pluggable region backends via vtable:

```c
typedef enum {
    REGION_KIND_ARENA,   /* Bulk alloc/free */
    REGION_KIND_LINEAR,  /* Bump allocator */
    REGION_KIND_OFFSET,  /* Position-independent */
    REGION_KIND_SCOPED,  /* Scope hierarchy */
    REGION_KIND_POOL,    /* Fixed-size objects */
    REGION_KIND_CUSTOM   /* User-defined */
} RegionKind;

/* Create regions */
IRegion* iregion_new_arena(size_t initial_size);
IRegion* iregion_new_linear(size_t size);
IRegion* iregion_new_offset(size_t size);
IRegion* iregion_new_pool(size_t object_size, size_t count);

/* Core operations */
void* iregion_alloc(IRegion* r, size_t size, size_t alignment);
void iregion_free_one(IRegion* r, void* ptr);  /* May be no-op */
void iregion_free_all(IRegion* r);             /* Bulk deallocation */

/* State */
void iregion_freeze(IRegion* r);       /* No more allocations */
bool iregion_is_frozen(IRegion* r);
size_t iregion_remaining(IRegion* r);

/* Advanced */
IRegion* iregion_clone(IRegion* r);    /* Deep copy */
void* iregion_serialize(IRegion* r, size_t* out_size);
void iregion_stats(IRegion* r, size_t* alloc_count,
                   size_t* bytes_used, size_t* bytes_total);

/* Convenience macros */
#define IREGION_ALLOC(r, type) \
    ((type*)iregion_alloc((r), sizeof(type), _Alignof(type)))

#define IREGION_ALLOC_ARRAY(r, type, count) \
    ((type*)iregion_alloc((r), sizeof(type) * (count), _Alignof(type)))
```

### Linear Regions

For FFI buffers and contiguous memory:

```c
LinearRegion* linear_region_new(size_t size);
void* linear_region_alloc(LinearRegion* r, size_t size, size_t alignment);
size_t linear_region_remaining(LinearRegion* r);
void linear_region_freeze(LinearRegion* r);  /* Ready for FFI */
void linear_region_free(LinearRegion* r);
```

### Offset Regions

For serialization and position-independent data:

```c
typedef int32_t OffsetPtr;  /* Relative pointer */
#define OFFSET_NULL 0

OffsetRegion* offset_region_new(size_t size);
OffsetPtr offset_region_alloc(OffsetRegion* r, size_t size, size_t alignment);
void offset_region_store_ptr(OffsetRegion* r, OffsetPtr* dest, OffsetPtr target);
void* offset_to_ptr(OffsetRegion* r, OffsetPtr offset);
OffsetPtr ptr_to_offset(OffsetRegion* r, void* ptr);

/* Serialization */
void* offset_region_serialize(OffsetRegion* r, size_t* out_size);
OffsetRegion* offset_region_deserialize(void* data, size_t size);
void offset_region_free(OffsetRegion* r);
```

### Pool Regions

For fixed-size objects with O(1) alloc/free:

```c
PoolRegion* pool_region_new(size_t object_size, size_t count);
void* pool_region_alloc(PoolRegion* r);
void pool_region_free_one(PoolRegion* r, void* ptr);
void pool_region_reset(PoolRegion* r);
void pool_region_free(PoolRegion* r);
```

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

Deep copy object graphs between regions:

```c
/* Create transmigration context */
TransmigrationContext* transmigration_new(IRegion* dest);
void transmigration_free(TransmigrationContext* ctx);

/* Register custom type visitors */
void transmigration_register_visitor(TransmigrationContext* ctx,
                                     int type_tag,
                                     TransmigrationVisitor visitor);

/* Core transmigration (deep copy) */
void* transmigrate(TransmigrationContext* ctx, void* source,
                   TransmigrationError* err);

/* Lookup already-copied object (for cycle handling) */
void* transmigration_lookup(TransmigrationContext* ctx, void* source);

/* Record source -> dest mapping */
void transmigration_record(TransmigrationContext* ctx, void* source, void* dest);
```

### Isolation Checking

Verify object graph is contained in single region:

```c
typedef struct IsolationResult {
    bool is_isolated;
    int escape_count;
    void** escaping_refs;
    int escaping_capacity;
} IsolationResult;

IsolationResult* check_isolation(void* root, IRegion* expected_region);
void isolation_result_free(IsolationResult* result);
```

### Region-Bound References

References that carry region context:

```c
typedef struct RegionBoundRef {
    void* target;
    IRegion* region;
    uint64_t region_epoch;  /* Staleness detection */
} RegionBoundRef;

RegionBoundRef* region_bound_ref_new(void* target, IRegion* region);
void region_bound_ref_free(RegionBoundRef* ref);
void* region_bound_ref_deref(RegionBoundRef* ref);  /* NULL if region frozen */
bool region_bound_ref_is_valid(RegionBoundRef* ref);
```

---

## Best Practices

### 1. Choose the Right Memory Strategy

| Data Shape | Strategy | Function |
|------------|----------|----------|
| Tree (no sharing) | ASAP immediate free | `free_tree()` |
| DAG (sharing, no cycles) | Reference counting | `dec_ref()` |
| Frozen cycles | SCC-based RC | `release_with_scc()` |
| Unbroken cycles | Component Tethering | `sym_release_handle()` |
| Scoped cyclic | Arena allocation | `arena_destroy()` |

### 2. Use Immediates When Possible

```c
// BAD: Heap allocation for small integer
Obj* x = mk_int(42);
// ... use x ...
dec_ref(x);

// GOOD: No allocation, no cleanup
Obj* x = mk_int_unboxed(42);
// ... use x - no cleanup needed!
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
| `iregion_new_arena(size_t)` | Create arena region |
| `iregion_new_linear(size_t)` | Create linear region |
| `iregion_new_offset(size_t)` | Create offset region |
| `iregion_new_pool(size_t, size_t)` | Create pool region |
| `iregion_alloc(...)` | Allocate from region |
| `iregion_free_one(...)` | Free single allocation |
| `iregion_free_all(...)` | Free entire region |
| `iregion_freeze(...)` | Freeze region |
| `iregion_clone(...)` | Deep copy region |
| `iregion_serialize(...)` | Serialize to buffer |

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
| `spawn_goroutine(...)` | Spawn thread |

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

- [ASAP: As Static As Possible](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-908.pdf) - Proust, 2017
- [Perceus: Garbage Free Reference Counting](https://dl.acm.org/doi/10.1145/3453483.3454032) - PLDI 2021
- [Shape Analysis](https://www.semanticscholar.org/paper/Is-it-a-tree,-a-DAG,-or-a-cyclic-graph-Ghiya-Hendren/115be3be1d6df75ff4defe0d7810ca6e45402040) - Ghiya & Hendren, POPL 1996
- [Region-Based Memory Management](https://elsman.com/mlkit/) - Tofte & Talpin / MLKit
- [CactusRef](https://github.com/artichoke/cactusref) - Deterministic cycle collection
- [Lobster Memory Management](https://aardappel.github.io/lobster/memory_management.html) - Compile-time RC
