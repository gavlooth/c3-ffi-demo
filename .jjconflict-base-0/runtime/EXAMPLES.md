# OmniLisp Runtime Code Examples

Practical examples demonstrating runtime usage patterns.

## Basic Object Operations

### Creating and Using Values

```c
#include "runtime.c"

void basic_values(void) {
    // Immediate integers (no allocation!)
    Obj* small = mk_int_unboxed(42);
    printf("Value: %ld\n", INT_IMM_VALUE(small));
    // No cleanup needed

    // Boxed integers (heap allocated)
    Obj* big = mk_int(1234567890123);
    printf("Value: %ld\n", obj_to_int(big));
    dec_ref(big);  // Must cleanup

    // Booleans (immediate)
    Obj* flag = OMNI_TRUE;
    if (obj_to_bool(flag)) {
        printf("It's true!\n");
    }

    // Characters
    Obj* ch = mk_char_unboxed('A');
    printf("Char: %c\n", (char)obj_to_char_val(ch));
}
```

### Working with Pairs and Lists

```c
void lists_example(void) {
    // Build list: (1 2 3)
    Obj* list = mk_pair(mk_int_unboxed(1),
                 mk_pair(mk_int_unboxed(2),
                  mk_pair(mk_int_unboxed(3), NULL)));

    // Traverse
    Obj* curr = list;
    while (curr) {
        printf("%ld ", obj_to_int(obj_car(curr)));
        curr = obj_cdr(curr);
    }
    printf("\n");

    // List operations
    printf("Length: %d\n", (int)list_length(list));

    Obj* reversed = list_reverse(list);
    prim_print(reversed);

    // Cleanup
    free_tree(list);      // Original
    free_tree(reversed);  // Copy
}
```

### Symbols and Strings

```c
void symbols_strings(void) {
    // Symbols
    Obj* sym = mk_sym("hello");
    printf("Symbol: %s\n", (char*)sym->ptr);
    dec_ref(sym);

    // Strings are lists of characters
    const char* str = "Hello";
    Obj* string = NULL;
    for (int i = strlen(str) - 1; i >= 0; i--) {
        string = mk_pair(mk_char_unboxed(str[i]), string);
    }
    prim_display(string);
    prim_newline();
    free_tree(string);
}
```

## Memory Management Patterns

### Reference Counting

```c
void refcount_example(void) {
    Obj* shared = mk_int(42);
    inc_ref(shared);  // Now refcount = 2

    Obj* holder1 = mk_pair(shared, NULL);  // Takes ownership
    Obj* holder2 = mk_pair(shared, NULL);  // Shares ownership

    // shared refcount = 3 (original + 2 pairs)

    dec_ref(holder1);  // Frees pair, decrements shared
    dec_ref(holder2);  // Frees pair, decrements shared

    dec_ref(shared);   // Final decrement, frees shared
}
```

### Arena Allocation for Cycles

```c
void arena_cycle_example(void) {
    Arena* arena = arena_create();

    // Create doubly-linked structure
    Obj* a = arena_mk_pair(arena, arena_mk_int(arena, 1), NULL);
    Obj* b = arena_mk_pair(arena, arena_mk_int(arena, 2), NULL);
    Obj* c = arena_mk_pair(arena, arena_mk_int(arena, 3), NULL);

    // Create cycle: a -> b -> c -> a
    a->b = b;
    b->b = c;
    c->b = a;  // Cycle!

    // Use the structure...
    Obj* curr = a;
    for (int i = 0; i < 6; i++) {
        printf("%ld ", obj_to_int(obj_car(curr)));
        curr = obj_cdr(curr);
    }
    printf("\n");

    // O(1) cleanup - no cycle detection needed!
    arena_destroy(arena);
}
```

### Phase 24 Optimized Constructors

#### Batch List Allocation

```c
void optimized_list_example(void) {
    Region* r = region_create();

    // OLD WAY: O(n) separate allocations
    Obj* list = NULL;
    for (int i = 0; i < 1000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);  // 1000 allocations!
    }

    // NEW WAY: Single batch allocation (5.55-6.32x faster)
    Obj* optimized_list = mk_list_region(r, 1000);  // 1 allocation!

    // Both create equivalent lists
    // Use lists...
    printf("List length: %d\n", (int)list_length(optimized_list));

    region_exit(r);
}
```

#### Batch Tree Allocation

```c
void optimized_tree_example(void) {
    Region* r = region_create();

    // Create a complete binary tree of depth 15
    // OLD WAY: O(n) separate allocations
    Obj* tree_old = NULL;
    // ... recursive tree building ...

    // NEW WAY: Single batch allocation
    Obj* tree = mk_tree_region(r, 15);  // (2^15 - 1) nodes in 1 allocation!

    // Use tree...
    printf("Tree created\n");

    region_exit(r);
}
```

#### Batch Array/Dict Allocation

```c
void optimized_structures_example(void) {
    Region* r = region_create();

    // Array with single allocation (Array struct + data in one block)
    Obj* arr = mk_array_region_batch(r, 100);
    // Array capacity is 100, zero malloc overhead for internal storage

    // Dict with single allocation (Dict struct + buckets in one block)
    Obj* dict = mk_dict_region_batch(r, 64);
    // Dict has 64 buckets, zero malloc overhead for bucket array

    // Use structures...
    printf("Array and dict created\n");

    region_exit(r);
}
```

### Region-Level Tethering for Thread-Safe Borrowing

```c
void region_tethering_example(Region* r) {
    // 1. Borrow region data safely (thread-safe)
    region_tether_start(r);

    // 2. Allocate and access data in the region
    Obj* a = region_alloc(r, sizeof(Obj));
    a->tag = TAG_INT;
    a->i = 42;

    // 3. Process data - tether prevents region deallocation
    printf("Value: %ld\n", a->i);

    // 4. Release borrow - allows region to be freed if external_rc == 0
    region_tether_end(r);
}
```

### SCC-Based RC for Frozen Cycles

```c
void scc_example(void) {
    // Create cyclic structure
    Obj* a = mk_pair(mk_int(1), NULL);
    Obj* b = mk_pair(mk_int(2), NULL);
    a->b = b;
    b->b = a;  // Cycle!

    // Detect and freeze SCCs
    detect_and_freeze_sccs(a);

    // Now a and b share a single reference count
    // via their SCC. When SCC refcount hits 0,
    // all members are freed together.

    // Use...
    printf("a: %ld, b: %ld\n",
           obj_to_int(obj_car(a)),
           obj_to_int(obj_car(b)));

    // Release through SCC
    release_with_scc(a);
}
```

## Borrowed References

### Safe Callbacks with Handles

```c
void callback_example(void) {
    // Create object
    Obj* data = mk_int(42);

    // Create borrowed reference for callback
    BorrowRef* ref = borrow_create(data, "callback context");

    // Simulate callback later...
    if (borrow_is_valid(ref)) {
        Obj* ptr = borrow_get(ref);
        printf("Callback sees: %ld\n", obj_to_int(ptr));
        dec_ref(ptr);  // borrow_get increments refcount
    } else {
        printf("Object was freed!\n");
    }

    // Cleanup
    borrow_release(ref);
    dec_ref(data);
}
```

### Slot Pool Handles

```c
void slot_pool_example(void) {
    // Initialize
    slot_pool_global_init();

    // Allocate from stable pool
    Obj* obj = handle_alloc_obj();
    obj->tag = TAG_INT;
    obj->i = 42;
    obj->mark = 1;

    // Create handle
    Handle h = handle_from_obj(obj);

    // Safe validation (always defined behavior)
    if (handle_is_valid(h)) {
        Obj* ptr = handle_deref_obj(h);
        printf("Value: %ld\n", ptr->i);
    }

    // Free object
    handle_free_obj(obj);

    // Handle is now invalid
    if (!handle_is_valid(h)) {
        printf("Handle correctly invalidated\n");
    }

    slot_pool_global_destroy();
}
```

## Region Infrastructure

### IRegion with Pool Backend

```c
void pool_region_example(void) {
    // Create pool for fixed-size objects
    IRegion* pool = iregion_new_pool(sizeof(Obj), 100);

    // Allocate objects
    Obj* objs[10];
    for (int i = 0; i < 10; i++) {
        objs[i] = IREGION_ALLOC(pool, Obj);
        objs[i]->tag = TAG_INT;
        objs[i]->i = i;
    }

    // Use objects
    for (int i = 0; i < 10; i++) {
        printf("%ld ", objs[i]->i);
    }
    printf("\n");

    // Free individual objects (O(1))
    for (int i = 0; i < 10; i++) {
        iregion_free_one(pool, objs[i]);
    }

    // Cleanup
    iregion_free_all(pool);
}
```

### Linear Region for FFI

```c
void linear_region_ffi_example(void) {
    // Create linear region
    IRegion* region = iregion_new_linear(4096);

    // Allocate contiguous data
    typedef struct { int x, y, z; } Point;
    Point* points = IREGION_ALLOC_ARRAY(region, Point, 100);

    for (int i = 0; i < 100; i++) {
        points[i].x = i;
        points[i].y = i * 2;
        points[i].z = i * 3;
    }

    // Freeze for FFI
    iregion_freeze(region);

    // Now safe to pass to C library
    // process_points(points, 100);

    // Cleanup
    iregion_free_all(region);
}
```

### Offset Region for Serialization

```c
void offset_region_example(void) {
    OffsetRegion* region = offset_region_new(4096);

    // Allocate with offset pointers
    typedef struct Node {
        int value;
        OffsetPtr next;
    } Node;

    OffsetPtr head = offset_region_alloc(region, sizeof(Node), 4);
    Node* node = offset_to_ptr(region, head);
    node->value = 1;

    OffsetPtr second = offset_region_alloc(region, sizeof(Node), 4);
    Node* node2 = offset_to_ptr(region, second);
    node2->value = 2;
    node2->next = OFFSET_NULL;

    // Store offset pointer
    offset_region_store_ptr(region, &node->next, second);

    // Serialize
    size_t size;
    void* buffer = offset_region_serialize(region, &size);

    // Could write buffer to disk, send over network, etc.
    // Data is position-independent!

    // Deserialize
    OffsetRegion* restored = offset_region_deserialize(buffer, size);

    // Traverse restored data
    Node* curr = offset_to_ptr(restored, head);
    while (curr) {
        printf("Value: %d\n", curr->value);
        if (curr->next == OFFSET_NULL) break;
        curr = offset_to_ptr(restored, curr->next);
    }

    free(buffer);
    offset_region_free(region);
    offset_region_free(restored);
}
```

## Concurrency

### Producer-Consumer with Channels

```c
void* producer(void* arg) {
    Obj* ch = (Obj*)arg;

    for (int i = 0; i < 10; i++) {
        Obj* msg = mk_int_unboxed(i);
        channel_send(ch, msg);
        // msg ownership transferred!
        printf("Sent: %d\n", i);
    }

    channel_close(ch);
    return NULL;
}

void* consumer(void* arg) {
    Obj* ch = (Obj*)arg;

    while (1) {
        Obj* msg = channel_recv(ch);
        if (!msg) break;  // Channel closed

        printf("Received: %ld\n", obj_to_int(msg));
        // We own msg now, but immediates don't need cleanup
    }

    return NULL;
}

void channel_example(void) {
    Obj* ch = make_channel(5);  // Buffered

    pthread_t prod, cons;
    pthread_create(&prod, NULL, producer, ch);
    pthread_create(&cons, NULL, consumer, ch);

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    dec_ref(ch);
}
```

### Atomic Counter with Atom

```c
void* increment_counter(void* arg) {
    Obj* counter = (Obj*)arg;

    for (int i = 0; i < 1000; i++) {
        Obj* old = atom_deref(counter);
        Obj* new_val = mk_int_unboxed(obj_to_int(old) + 1);
        atom_reset(counter, new_val);
        dec_ref(old);
    }

    return NULL;
}

void atom_example(void) {
    Obj* counter = make_atom(mk_int_unboxed(0));

    pthread_t threads[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, increment_counter, counter);
    }

    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    Obj* final = atom_deref(counter);
    printf("Final count: %ld\n", obj_to_int(final));
    dec_ref(final);
    dec_ref(counter);
}
```

## Fiber Concurrency (Tier 2)

Fibers use continuation-based scheduling - no pthreads, ~100 bytes per task.

### Basic Fibers

```c
// Simple producer task
Obj* producer_task(Obj** args, int argc) {
    (void)argc;
    Obj* ch = args[0];

    for (int i = 0; i < 10; i++) {
        Obj* msg = mk_int_unboxed(i);
        fiber_send(ch, msg);
        printf("Green sent: %d\n", i);
        fiber_yield();  // Cooperative yield
    }

    fiber_chan_close(ch);
    return NULL;
}

// Simple consumer task
Obj* consumer_task(Obj** args, int argc) {
    (void)argc;
    Obj* ch = args[0];

    while (1) {
        int ok;
        Obj* msg = fiber_try_recv(ch, &ok);
        if (!ok) {
            fiber_yield();
            continue;
        }
        if (!msg && fiber_channel_is_closed((FiberChannel*)ch->ptr)) break;

        printf("Green received: %ld\n", obj_to_int(msg));
    }

    return NULL;
}

void green_channel_example(void) {
    fiber_scheduler_init();

    Obj* ch = make_fiber_chan(5);  // Buffered green channel

    // Create closures for tasks
    Obj* prod_args[1] = { ch };
    Obj* cons_args[1] = { ch };

    Obj* prod_thunk = mk_closure(producer_task, prod_args, NULL, 1, 0);
    Obj* cons_thunk = mk_closure(consumer_task, cons_args, NULL, 1, 0);

    // Spawn fibers (very lightweight!)
    fiber_spawn_task(prod_thunk);
    fiber_spawn_task(cons_thunk);

    // Run scheduler until all tasks complete
    fiber_scheduler_run();

    free_fiber_channel_obj(ch);
    fiber_scheduler_shutdown();
}
```

### Generator (Iterator) Example

```c
// Generator that yields squares: 0, 1, 4, 9, 16, ...
Obj* squares_producer(Obj** args, int argc) {
    (void)args; (void)argc;

    for (int i = 0; i < 10; i++) {
        generator_yield(mk_int_unboxed(i * i));
    }

    return NULL;  // Generator exhausted
}

void generator_example(void) {
    fiber_scheduler_init();

    Obj* producer = mk_closure(squares_producer, NULL, NULL, 0, 0);
    Obj* gen = make_gen(producer);

    printf("Squares: ");
    while (!gen_done(gen)) {
        Obj* val = gen_next(gen);
        if (val) {
            printf("%ld ", obj_to_int(val));
        }
    }
    printf("\n");  // Output: Squares: 0 1 4 9 16 25 36 49 64 81

    free_generator_obj(gen);
    dec_ref(producer);
    fiber_scheduler_shutdown();
}
```

### Range Generator

```c
// Range generator: yields start, start+1, ..., end-1
typedef struct {
    long start;
    long end;
} RangeState;

Obj* range_producer(Obj** args, int argc) {
    (void)argc;
    RangeState* state = (RangeState*)args[0]->ptr;

    for (long i = state->start; i < state->end; i++) {
        generator_yield(mk_int_unboxed(i));
    }

    return NULL;
}

Obj* make_range(long start, long end) {
    RangeState* state = malloc(sizeof(RangeState));
    state->start = start;
    state->end = end;

    Obj* state_obj = malloc(sizeof(Obj));
    state_obj->tag = TAG_BOX;
    state_obj->ptr = state;
    state_obj->mark = 1;

    Obj* args[1] = { state_obj };
    Obj* producer = mk_closure(range_producer, args, NULL, 1, 0);

    return make_gen(producer);
}

void range_example(void) {
    fiber_scheduler_init();

    Obj* range = make_range(5, 15);

    printf("Range 5..15: ");
    while (!gen_done(range)) {
        Obj* val = gen_next(range);
        if (val) printf("%ld ", obj_to_int(val));
    }
    printf("\n");  // Output: Range 5..15: 5 6 7 8 9 10 11 12 13 14

    free_generator_obj(range);
    fiber_scheduler_shutdown();
}
```

### Async/Await with Promises

```c
// Async computation that returns a promise
Obj* async_compute(Obj** args, int argc) {
    (void)argc;
    long input = obj_to_int(args[0]);

    // Simulate async work with yields
    for (int i = 0; i < 3; i++) {
        fiber_yield();
    }

    return mk_int_unboxed(input * input);
}

void promise_example(void) {
    fiber_scheduler_init();

    // Create async task that returns a promise
    Obj* input = mk_int_unboxed(7);
    Obj* args[1] = { input };
    Obj* thunk = mk_closure(async_compute, args, NULL, 1, 0);

    Obj* promise = spawn_async_task(thunk);

    printf("Promise created, running scheduler...\n");

    // Run until promise resolves
    while (!promise_settled(promise)) {
        fiber_scheduler_step();
    }

    // Get result
    Promise* p = (Promise*)promise->ptr;
    printf("Result: %ld\n", obj_to_int(p->value));  // Output: 49

    free_promise_val(promise);
    dec_ref(thunk);
    fiber_scheduler_shutdown();
}
```

### Many Concurrent Tasks (1 Million)

```c
#define NUM_TASKS 1000000

Obj* trivial_task(Obj** args, int argc) {
    (void)args; (void)argc;
    fiber_yield();
    return mk_int_unboxed(1);
}

void million_tasks_example(void) {
    fiber_scheduler_init();

    printf("Spawning %d fibers...\n", NUM_TASKS);

    Obj* thunk = mk_closure(trivial_task, NULL, NULL, 0, 0);

    clock_t start = clock();

    for (int i = 0; i < NUM_TASKS; i++) {
        fiber_spawn_task(thunk);
    }

    double spawn_time = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("Spawn time: %.3f seconds (%.0f tasks/sec)\n",
           spawn_time, NUM_TASKS / spawn_time);

    start = clock();
    fiber_scheduler_run();

    double run_time = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("Run time: %.3f seconds\n", run_time);

    dec_ref(thunk);
    fiber_scheduler_shutdown();
}
```

### Combining OS Threads with Fibers

```c
// Run green scheduler on multiple OS threads
void* worker_thread(void* arg) {
    int worker_id = *(int*)arg;

    // Each OS thread gets its own green scheduler
    fiber_scheduler_init();

    printf("Worker %d starting fibers\n", worker_id);

    // Spawn some fibers on this OS thread
    for (int i = 0; i < 100; i++) {
        Obj* thunk = mk_closure(trivial_task, NULL, NULL, 0, 0);
        fiber_spawn_task(thunk);
        dec_ref(thunk);
    }

    fiber_scheduler_run();
    fiber_scheduler_shutdown();

    printf("Worker %d done\n", worker_id);
    return NULL;
}

void hybrid_concurrency_example(void) {
    pthread_t workers[4];
    int worker_ids[4] = {0, 1, 2, 3};

    for (int i = 0; i < 4; i++) {
        pthread_create(&workers[i], NULL, worker_thread, &worker_ids[i]);
    }

    for (int i = 0; i < 4; i++) {
        pthread_join(workers[i], NULL);
    }
}
```

## FFI Integration

### External Handle Table

```c
// Callback from C library
void external_callback(uint64_t handle, int event_type) {
    void* obj = ffi_handle_to_obj(handle);
    if (obj) {
        printf("Callback for handle %lu, event %d\n", handle, event_type);
        // Process event with obj...
    } else {
        printf("Handle expired!\n");
    }
}

void ffi_handles_example(void) {
    ExternalHandleTable* table = external_table_new(100);

    // Create handles for objects
    Obj* obj1 = mk_int(100);
    Obj* obj2 = mk_int(200);

    ExternalHandle h1 = external_handle_create(table, obj1, (void(*)(void*))dec_ref);
    ExternalHandle h2 = external_handle_create(table, obj2, (void(*)(void*))dec_ref);

    printf("Handle 1: %lu (idx=%u, gen=%u)\n",
           h1, EXTERNAL_HANDLE_INDEX(h1), EXTERNAL_HANDLE_GEN(h1));

    // Pass handles to external library...
    external_callback(h1, 1);
    external_callback(h2, 2);

    // Release handles (destructor called automatically)
    external_handle_release(table, h1);
    external_handle_release(table, h2);

    external_table_free(table);
}
```

### Weak References for Observers

```c
void weak_observer_example(void) {
    WeakRefTable* table = weak_table_global();

    // Subject
    Obj* subject = mk_int(42);
    uint32_t subj_idx = weak_table_register(table, subject, NULL);

    // Observers get weak handles
    WeakHandle* observer1 = weak_table_get_handle(table, subj_idx);
    WeakHandle* observer2 = weak_table_get_handle(table, subj_idx);

    // Observers can check if subject still exists
    if (weak_handle_is_valid(observer1)) {
        void* ptr = weak_handle_lock(observer1);
        printf("Observer 1 sees subject: %ld\n", obj_to_int((Obj*)ptr));
    }

    // Free subject
    weak_table_invalidate(table, subj_idx);
    dec_ref(subject);

    // Observers now see invalid
    if (!weak_handle_is_valid(observer1)) {
        printf("Observer 1: subject gone!\n");
    }

    weak_handle_free(observer1);
    weak_handle_free(observer2);
}
```

## Closures

### Creating and Calling Closures

```c
// Closure function: adds captured value to argument
Obj* add_captured(Obj** captures, Obj** args, int arg_count) {
    Obj* captured = captures[0];
    Obj* arg = args[0];
    return prim_add(captured, arg);
}

void closure_example(void) {
    // Capture value 10
    Obj* captured_val = mk_int_unboxed(10);
    Obj* captures[] = { captured_val };

    // Create closure
    Obj* adder = mk_closure(add_captured, captures, NULL, 1, 1);

    // Call with argument 5
    Obj* arg = mk_int_unboxed(5);
    Obj* args[] = { arg };
    Obj* result = call_closure(adder, args, 1);

    printf("10 + 5 = %ld\n", obj_to_int(result));  // 15

    dec_ref(adder);
}
```

### Safe Closures with Borrowed References

```c
Obj* safe_closure_fn(Obj** captures, Obj** args, int arg_count) {
    // Captures are validated before this is called
    return captures[0];
}

void safe_closure_example(void) {
    Obj* value = mk_int(42);
    Obj* captures[] = { value };

    // Create borrowed references for captures
    BorrowRef* refs[] = { borrow_create(value, "closure capture") };

    Obj* closure = mk_closure(safe_closure_fn, captures, refs, 1, 0);

    // Closure validates captures before calling
    if (closure_validate((Closure*)closure->ptr)) {
        Obj* result = call_closure(closure, NULL, 0);
        printf("Result: %ld\n", obj_to_int(result));
    }

    dec_ref(closure);
    dec_ref(value);
}
```

## Transmigration

### Moving Objects Between Regions

```c
void transmigration_example(void) {
    // Source region
    IRegion* src = iregion_new_arena(4096);
    Obj* obj = iregion_alloc(src, sizeof(Obj), 8);
    obj->tag = TAG_INT;
    obj->i = 42;

    // Destination region
    IRegion* dest = iregion_new_arena(4096);

    // Create transmigration context
    TransmigrationContext* ctx = transmigration_new(dest);

    // Deep copy to destination
    TransmigrationError err;
    void* copy = transmigrate(ctx, obj, &err);

    if (err == TRANSMIGRATE_OK) {
        Obj* copy_obj = (Obj*)copy;
        printf("Copied value: %ld\n", copy_obj->i);
    }

    // Cleanup
    transmigration_free(ctx);
    iregion_free_all(src);
    iregion_free_all(dest);
}
```

### Phase 24 Optimized Transmigration

#### Bitmap-Based Cycle Detection

```c
void optimized_transmigration_example(void) {
    Region* src = region_create();
    Region* dest = region_create();

    // Build complex object graph in source region
    Obj* root = mk_list_region(src, 1000);  // 1000-element list
    // Add cycles, shared references, etc.

    // NEW: Optimized transmigration with bitmap cycle detection
    // OLD: Used uthash with malloc() - 41x slower!
    // NEW: Uses bitmap with Arena allocation - 10-100x faster!
    Obj* copy = transmigrate(root, src, dest);

    printf("Transmigrated successfully\n");

    region_exit(src);
    region_exit(dest);
}
```

#### Incremental (Batched) Transmigration

```c
void incremental_transmigration_example(void) {
    Region* src = region_create();
    Region* dest = region_create();

    // Build very large object graph (millions of objects)
    Obj* huge_graph = build_huge_graph(src);

    // NEW: Incremental transmigration for sparse access patterns
    // Process in chunks of 500 objects at a time
    // Useful when you only need to access parts of the graph
    Obj* copy = transmigrate_incremental(huge_graph, src, dest, 500);

    printf("Incremental transmigration complete\n");

    region_exit(src);
    region_exit(dest);
}
```

#### O(1) Region Splicing

```c
void region_splicing_example(void) {
    Region* src = region_create();
    Region* dest = region_create();

    // Build result in source region
    Obj* result = compute_result(src);

    // NEW: Automatic O(1) region splicing
    // When source has external_rc==0 and scope_alive==false,
    // the entire arena chunk is moved instead of copied!
    Obj* transferred = transmigrate(result, src, dest);

    // O(1) transfer regardless of result size!
    printf("Result transferred in O(1) time\n");

    region_exit(src);
    region_exit(dest);
}
```

### Isolation Checking

```c
void isolation_example(void) {
    IRegion* region = iregion_new_arena(4096);

    // Allocate in region
    int* data = IREGION_ALLOC(region, int);
    *data = 42;

    // Check isolation
    IsolationResult* result = check_isolation(data, region);

    if (result->is_isolated) {
        printf("Object is fully contained in region\n");
    } else {
        printf("Object has %d escaping references\n", result->escape_count);
    }

    isolation_result_free(result);
    iregion_free_all(region);
}
```
