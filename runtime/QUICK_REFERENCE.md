# OmniLisp Runtime Quick Reference

## Object Tags

```c
// Core types
TAG_INT TAG_FLOAT TAG_CHAR TAG_PAIR TAG_SYM TAG_BOX TAG_CLOSURE TAG_ERROR

// OS Thread concurrency (Tier 1 - pthreads)
TAG_CHANNEL TAG_ATOM TAG_THREAD

// Fiber concurrency (Tier 2 - continuations)
TAG_FIBER_CHANNEL TAG_GENERATOR TAG_PROMISE TAG_FIBER TAG_CONTINUATION
```

## Constructors

```c
// Heap allocation
mk_int(long)  mk_float(double)  mk_char(long)  mk_pair(Obj*, Obj*)
mk_sym(const char*)  mk_box(Obj*)  mk_error(const char*)

// Immediate (no allocation)
mk_int_unboxed(long)  mk_bool(int)  mk_char_unboxed(long)
OMNI_TRUE  OMNI_FALSE

// Stack pool (short-lived)
mk_int_stack(long)  mk_float_stack(double)  mk_char_stack(long)

// Arena (cyclic structures)
arena_mk_int(Arena*, long)  arena_mk_pair(Arena*, Obj*, Obj*)
```

## Immediate Value Macros

```c
IS_IMMEDIATE(p)       // Is tagged pointer?
IS_IMMEDIATE_INT(p)   // Is immediate int?
IS_IMMEDIATE_CHAR(p)  // Is immediate char?
IS_IMMEDIATE_BOOL(p)  // Is immediate bool?

MAKE_INT_IMM(n)       // Create immediate int
INT_IMM_VALUE(p)      // Extract immediate int value
```

## Accessors

```c
obj_to_int(Obj*)      // Extract integer (boxed or immediate)
obj_to_bool(Obj*)     // Extract boolean
obj_to_char_val(Obj*) // Extract character
obj_tag(Obj*)         // Get tag (handles immediates)

obj_car(Obj*)  obj_cdr(Obj*)  // Pair accessors
box_get(Obj*)  box_set(Obj*, Obj*)  // Box accessors
```

## Reference Counting

```c
inc_ref(Obj*)         // Increment ref count
dec_ref(Obj*)         // Decrement, free if zero
free_obj(Obj*)        // Add to free list
free_tree(Obj*)       // Recursive free
free_unique(Obj*)     // Direct free (skip RC check)
flush_freelist()      // Process pending frees
```

## Memory Strategies by Shape

| Shape | Strategy | Function |
|-------|----------|----------|
| Tree | ASAP | `free_tree()` |
| DAG | RC | `dec_ref()` |
| Frozen cycle | SCC | `release_with_scc()` |
| Unbroken cycle | SymRC | `sym_exit_scope()` |
| Scoped cyclic | Arena | `arena_destroy()` |

## Arena Allocator

```c
Arena* arena_create(void);
void* arena_alloc(Arena*, size_t);
void arena_reset(Arena*);
void arena_destroy(Arena*);
void arena_register_external(Arena*, void*, void(*)(void*));
```

## IRegion Interface

```c
// Create regions
iregion_new_arena(size_t)
iregion_new_linear(size_t)
iregion_new_offset(size_t)
iregion_new_pool(size_t obj_size, size_t count)

// Operations
iregion_alloc(IRegion*, size_t size, size_t align)
iregion_free_one(IRegion*, void*)
iregion_free_all(IRegion*)
iregion_freeze(IRegion*)
iregion_is_frozen(IRegion*)
iregion_remaining(IRegion*)
iregion_clone(IRegion*)
iregion_serialize(IRegion*, size_t*)
iregion_stats(IRegion*, size_t*, size_t*, size_t*)

// Convenience
IREGION_ALLOC(r, type)
IREGION_ALLOC_ARRAY(r, type, count)
```

## Handles and Borrowed References

```c
// Slot pool allocation (sound validation)
handle_alloc_obj()                 // Allocate from pool
handle_free_obj(Obj*)              // Free to pool
handle_from_obj(Obj*)              // Create handle
handle_deref_obj(Handle)           // Safe dereference
handle_is_valid(Handle)            // O(1) validation

// Borrowed references
borrow_create(Obj*, const char*)   // Create borrow ref
borrow_is_valid(BorrowRef*)        // Check validity
borrow_get(BorrowRef*)             // Get obj (inc_ref'd)
borrow_release(BorrowRef*)         // Release ref
```

## Weak References

```c
// Control blocks
weak_cb_new(void*, void(*)(void*))
weak_cb_invalidate(WeakControlBlock*)
weak_cb_is_valid(WeakControlBlock*)
weak_cb_get_target(WeakControlBlock*)

// Handles
weak_handle_new(WeakControlBlock*)
weak_handle_clone(WeakHandle*)
weak_handle_free(WeakHandle*)
weak_handle_is_valid(WeakHandle*)
weak_handle_lock(WeakHandle*)      // Returns target or NULL

// Table
weak_table_global()
weak_table_register(table, target, destructor)
weak_table_invalidate(table, index)
weak_table_lock(table, index, generation)
```

## External Handles (FFI)

```c
typedef uint64_t ExternalHandle;  // 32-bit index + 32-bit gen

// Table
external_table_new(size_t capacity)
external_table_free(ExternalHandleTable*)
external_table_set_deterministic(table, bool)
external_table_global()

// Handle ops
external_handle_create(table, obj, destructor)
external_handle_release(table, handle)
external_handle_get(table, handle)
external_handle_is_valid(table, handle)

// FFI convenience
ffi_obj_to_handle(void*)           // Create handle
ffi_handle_to_obj(uint64_t)        // Get object
ffi_release_handle(uint64_t)       // Release
```

## Tier 1: OS Thread Concurrency (pthreads)

```c
// Channels (CSP) - use for cross-thread communication
make_channel(int capacity)         // 0 = unbuffered
channel_send(Obj* ch, Obj* val)    // Blocks, transfers ownership
channel_recv(Obj* ch)              // Blocks, receives ownership
channel_close(Obj* ch)

// Atoms - thread-safe state
make_atom(Obj* initial)
atom_deref(Obj* atom)              // Read (inc_ref'd)
atom_reset(Obj* atom, Obj* val)    // Set
atom_cas(Obj*, Obj* exp, Obj* new) // Compare-and-swap
atom_swap(Obj*, Obj*(*fn)(Obj*))   // Apply function

// OS Threads
spawn_goroutine(Obj* closure, Obj** captured, int count)
```

## Tier 2: Fiber Concurrency (continuations)

```c
// Scheduler (call once per OS thread)
fiber_scheduler_init()             // Initialize scheduler
fiber_scheduler_run()              // Run until all fibers complete
fiber_scheduler_step()             // Step one fiber (for event loops)
fiber_scheduler_idle()             // Check if no fibers pending
fiber_scheduler_shutdown()         // Cleanup

// Fiber Channels - lightweight, no pthread overhead
make_fiber_chan(int capacity)      // 0 = unbuffered (rendezvous)
fiber_send(Obj* ch, Obj* val)      // Parks fiber if needed
fiber_recv(Obj* ch)                // Parks fiber if needed
fiber_try_send(Obj* ch, Obj* val)  // Non-blocking, returns success
fiber_try_recv(Obj* ch, int* ok)   // Non-blocking
fiber_chan_close(Obj* ch)

// Fibers - 1M+ concurrent fibers
fiber_spawn_task(Obj* thunk)       // Spawn, returns fiber handle
spawn_async_task(Obj* thunk)       // Spawn, returns promise
fiber_yield_wrapper()              // Cooperative yield

// Generators (Iterators) - lazy sequences
make_gen(Obj* producer)            // Create from closure
gen_next(Obj* gen)                 // Get next value (resumes)
gen_done(Obj* gen)                 // Check if exhausted
gen_close(Obj* gen)                // Close early
generator_yield(Obj* value)        // Yield from producer (internal)

// Promises (Async/Await)
make_promise_val()                 // Create pending promise
promise_resolve_val(Obj* p, Obj* v) // Fulfill with value
promise_reject_val(Obj* p, Obj* e)  // Reject with error
promise_await_val(Obj* p)          // Park until settled
promise_settled(Obj* p)            // Check if resolved/rejected
```

## Closures

```c
mk_closure(ClosureFn, Obj** caps, BorrowRef** refs, int count, int arity)
call_closure(Obj* clos, Obj** args, int arg_count)
closure_validate(Closure*)         // Check captures valid
closure_release(Closure*)
```

## Component Tethering (Cyclic Islands)
- `sym_component_new()`: Create unit of reclamation
- `sym_acquire_handle(c)`: Increment handle count (ASAP managed)
- `sym_release_handle(c)`: Decrement handle count
- `sym_tether_begin(c)`: Zero-cost scoped access
- `sym_tether_end(t)`: Release scope tether
- `sym_component_add_member(c, obj)`: Add object to island

## SCC-Based RC

```c
create_scc()
scc_add_member(SCC*, Obj*)
freeze_scc(SCC*)
find_scc(int id)
release_scc(SCC*)
detect_and_freeze_sccs(Obj* root)
```

## Deferred RC

```c
defer_decrement(Obj*)
process_deferred()
flush_deferred()
safe_point()
set_deferred_batch_size(int)
```

## Transmigration

```c
transmigration_new(IRegion* dest)
transmigration_free(TransmigrationContext*)
transmigrate(ctx, source, &err)
transmigration_lookup(ctx, source)
transmigration_record(ctx, source, dest)
transmigration_register_visitor(ctx, type_tag, visitor)

check_isolation(root, region)
isolation_result_free(IsolationResult*)
```

## Region-Bound Refs

```c
region_bound_ref_new(void*, IRegion*)
region_bound_ref_free(RegionBoundRef*)
region_bound_ref_deref(RegionBoundRef*)
region_bound_ref_is_valid(RegionBoundRef*)
```

## Primitives

```c
// Arithmetic
prim_add  prim_sub  prim_mul  prim_div  prim_mod  prim_abs

// Comparison
prim_lt  prim_gt  prim_le  prim_ge  prim_eq  prim_not

// Type predicates
prim_null  prim_pair  prim_int  prim_float  prim_char  prim_sym

// I/O
prim_display  prim_print  prim_newline

// Type conversion
char_to_int  int_to_char  int_to_float  float_to_int  prim_floor  prim_ceil

// Introspection
ctr_tag(Obj*)  ctr_arg(Obj*, Obj* idx)
```

## List Operations

```c
list_length(Obj*)
list_append(Obj*, Obj*)
list_reverse(Obj*)
list_map(Obj* fn, Obj* xs)
list_filter(Obj* fn, Obj* xs)
list_fold(Obj* fn, Obj* init, Obj* xs)
list_foldr(Obj* fn, Obj* init, Obj* xs)
```

## Compile Flags

```bash
# Standard build
gcc -std=c99 -pthread -o prog prog.c -lomnilisp

# Robust IPGE (64-bit generation)
gcc -std=c99 -pthread -DIPGE_ROBUST_MODE=1 -o prog prog.c -lomnilisp

# Debug constraint refs
gcc -std=c99 -pthread -DCONSTRAINT_DEBUG=1 -o prog prog.c -lomnilisp
```

## Two-Tier Concurrency: When to Use What

| Use Case | Tier | API |
|----------|------|-----|
| 1000s of concurrent tasks | Fiber | `fiber_spawn_task` |
| Blocking C library call | OS | `spawn_goroutine` |
| CPU-bound computation | OS | `spawn_goroutine` |
| Async I/O multiplexing | Fiber | `fiber_send/recv` |
| Lazy sequences | Fiber | `make_gen` |
| Cross-thread shared state | OS | `make_atom` |

## Performance: Tier 1 vs Tier 2

| Metric | OS Thread (Tier 1) | Fiber (Tier 2) |
|--------|-------------------|----------------------|
| Creation | ~10μs | ~100ns |
| Context switch | ~1μs | ~50ns |
| Memory/fiber | ~8KB min | ~100 bytes |
| Max fibers | ~10K | ~1M+ |
| Blocking FFI | Native | Needs wrapper |
