/* test_stress_memory.c - Advanced Memory Stress Tests for OmniLisp Runtime
 *
 * This file contains comprehensive stress tests for the memory management system.
 * These tests go beyond basic functionality to test:
 * - Memory pressure scenarios
 * - Large object allocations
 * - Concurrent memory operations
 * - Free list stress
 * - Slot pool stress
 * - Continuation/fiber memory stress
 */

#include "test_framework.h"
#include <time.h>
#include <pthread.h>
#include <unistd.h>

/* ========== Memory Pressure Tests ========== */

void test_stress_oom_allocation_pattern(void) {
    /* Test allocation/deallocation pattern that might cause memory exhaustion */
    Obj* objs[10000];
    int allocated = 0;

    /* Allocate until we have 10000 objects */
    for (int i = 0; i < 10000; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
        allocated++;
    }

    /* Free every other one */
    for (int i = 0; i < 10000; i += 2) {
        dec_ref(objs[i]);
        objs[i] = NULL;
        allocated--;
    }

    /* Allocate more to fill gaps */
    for (int i = 0; i < 5000; i++) {
        Obj* x = mk_float((double)i);
        ASSERT_NOT_NULL(x);
        dec_ref(x);
    }

    /* Free remaining */
    for (int i = 1; i < 10000; i += 2) {
        if (objs[i]) {
            dec_ref(objs[i]);
            allocated--;
        }
    }
    (void)allocated;
    
    PASS();
}

void test_stress_large_list_construction(void) {
    /* Test building and freeing a very large list */
    Obj* list = NULL;
    const int count = 200000;

    for (int i = 0; i < count; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
        if ((i % 10000) == 0) {
            ASSERT_NOT_NULL(list);
        }
    }

    /* Verify length */
    int len = 0;
    Obj* current = list;
    while (current && current->tag == TAG_PAIR) {
        len++;
        current = current->b;
    }
    ASSERT_EQ(len, count);

    dec_ref(list);
    PASS();
}

void test_stress_many_small_allocations(void) {
    /* Test many small allocations in sequence */
    const int iterations = 50000;

    for (int i = 0; i < iterations; i++) {
        Obj* a = mk_int(i);
        ASSERT_NOT_NULL(a);

        Obj* b = mk_float((double)i * 1.5);
        ASSERT_NOT_NULL(b);

        Obj* c = mk_char(i % 256);
        ASSERT_NOT_NULL(c);

        dec_ref(c);
        dec_ref(b);
        dec_ref(a);
    }

    PASS();
}

void test_stress_nested_structures(void) {
    /* Test deeply nested structures */
    const int depth = 5000;

    /* Create a binary tree */
    Obj* tree = mk_int_unboxed(0);
    for (int i = 1; i < depth; i++) {
        Obj* left = mk_int_unboxed(i * 2);
        Obj* right = mk_int_unboxed(i * 2 + 1);
        tree = mk_pair(left, mk_pair(right, tree));
    }

    ASSERT_NOT_NULL(tree);
    dec_ref(tree);
    PASS();
}

void test_stress_interleaved_allocations(void) {
    /* Test interleaved allocations of different types */
    const int iterations = 20000;

    for (int i = 0; i < iterations; i++) {
        Obj* objs[10];

        /* Allocate different types */
        objs[0] = mk_int(i);
        objs[1] = mk_float((double)i);
        objs[2] = mk_char(i % 128);
        objs[3] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        objs[4] = mk_sym("test");
        objs[5] = mk_box(mk_int_unboxed(i));
        objs[6] = mk_int(i * 2);
        objs[7] = mk_pair(objs[0], NULL);
        objs[8] = mk_float((double)i * 2.0);
        objs[9] = mk_int_unboxed(i);

        /* Verify allocations succeeded */
        for (int j = 0; j < 10; j++) {
            if (j != 9) { /* mk_int_unboxed returns immediate values */
                ASSERT_NOT_NULL(objs[j]);
            }
        }

        /* Free in reverse order */
        for (int j = 9; j >= 0; j--) {
            if (j != 9) {
                dec_ref(objs[j]);
            }
        }
    }

    PASS();
}

/* ========== Reference Counting Stress Tests ========== */

void test_stress_extreme_refcount(void) {
    /* Test very high reference counts */
    Obj* obj = mk_int(42);
    ASSERT_NOT_NULL(obj);

    const int count = 100000;
    for (int i = 0; i < count; i++) {
        inc_ref(obj);
    }

    ASSERT_EQ(obj->mark, count + 1);

    /* Decrement back to 1 */
    for (int i = 0; i < count; i++) {
        dec_ref(obj);
    }

    ASSERT_EQ(obj->mark, 1);
    dec_ref(obj);
    PASS();
}

void test_stress_shared_refcount_pattern(void) {
    /* Test many objects sharing a single object */
    Obj* shared = mk_int(-999);
    ASSERT_NOT_NULL(shared);

    const int count = 5000;
    Obj* containers[count];

    /* Create containers that all hold shared */
    for (int i = 0; i < count; i++) {
        containers[i] = mk_pair(shared, mk_int_unboxed(i));
        ASSERT_NOT_NULL(containers[i]);
    }

    /* shared should have count + 1 refs */
    ASSERT_EQ(shared->mark, count + 1);

    /* Free all containers */
    for (int i = 0; i < count; i++) {
        dec_ref(containers[i]);
    }

    /* shared should have 1 ref left */
    ASSERT_EQ(shared->mark, 1);
    dec_ref(shared);
    PASS();
}

void test_stress_refcount_cycles(void) {
    /* Test rapid reference count cycles */
    const int cycles = 10000;

    for (int i = 0; i < cycles; i++) {
        Obj* obj = mk_int(i);
        ASSERT_NOT_NULL(obj);

        /* Bump refcount up and down */
        for (int j = 0; j < 10; j++) {
            inc_ref(obj);
        }

        for (int j = 0; j < 10; j++) {
            dec_ref(obj);
        }

        ASSERT_EQ(obj->mark, 1);
        dec_ref(obj);
    }

    PASS();
}

/* ========== Free List Stress Tests ========== */

void test_stress_freelist_capacity(void) {
    /* Test free list with many objects */
    const int count = 20000;
    Obj* objs[count];

    /* Allocate */
    for (int i = 0; i < count; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }

    /* Free all */
    for (int i = 0; i < count; i++) {
        dec_ref(objs[i]);
    }

    /* Flush free list */
    flush_freelist();

    /* Allocate again - should reuse freed memory */
    for (int i = 0; i < count; i++) {
        objs[i] = mk_int(i + count);
        ASSERT_NOT_NULL(objs[i]);
    }

    /* Cleanup */
    for (int i = 0; i < count; i++) {
        dec_ref(objs[i]);
    }

    PASS();
}

void test_stress_freelist_interleaved(void) {
    /* Test interleaved alloc/free with freelist operations */
    const int count = 5000;
    Obj* objs[count * 2];

    /* Allocate batch 1 */
    for (int i = 0; i < count; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }

    /* Free odd indices */
    for (int i = 1; i < count; i += 2) {
        dec_ref(objs[i]);
    }

    /* Flush freelist */
    flush_freelist();

    /* Allocate batch 2 (should reuse memory) */
    for (int i = count; i < count * 2; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }

    /* Free remaining from batch 1 */
    for (int i = 0; i < count; i += 2) {
        dec_ref(objs[i]);
    }

    /* Free batch 2 */
    for (int i = count; i < count * 2; i++) {
        dec_ref(objs[i]);
    }

    flush_freelist();
    PASS();
}

/* ========== Deferred RC Stress Tests ========== */

void test_stress_deferred_large_batch(void) {
    /* Test deferred decrements with very large batches */
    const int count = 50000;
    Obj* objs[count];

    /* Allocate and increment refs */
    for (int i = 0; i < count; i++) {
        objs[i] = mk_int(i);
        inc_ref(objs[i]);
    }

    /* Defer all decrements */
    for (int i = 0; i < count; i++) {
        defer_decrement(objs[i]);
    }

    /* Process all deferred */
    flush_deferred();

    /* All should have refcount 1 now */
    for (int i = 0; i < count; i++) {
        ASSERT_EQ(objs[i]->mark, 1);
        dec_ref(objs[i]);
    }

    PASS();
}

void test_stress_deferred_coalescing(void) {
    /* Test that repeated defers coalesce properly */
    Obj* obj = mk_int(42);
    ASSERT_NOT_NULL(obj);

    inc_ref(obj);
    inc_ref(obj);

    const int iterations = 10000;

    /* Defer many times - should coalesce */
    for (int i = 0; i < iterations; i++) {
        defer_decrement(obj);
    }

    /* Process */
    flush_deferred();

    /* Should have decremented once */
    ASSERT_EQ(obj->mark, 2);

    dec_ref(obj);
    dec_ref(obj);
    PASS();
}

void test_stress_deferred_safe_points(void) {
    /* Test safe points with many pending decrements */
    const int count = 10000;
    Obj* objs[count];

    for (int i = 0; i < count; i++) {
        objs[i] = mk_int(i);
        inc_ref(objs[i]);
        defer_decrement(objs[i]);
    }

    /* Trigger many safe points - should process gradually */
    for (int i = 0; i < 1000; i++) {
        safe_point();
    }

    /* Finish processing */
    flush_deferred();

    /* Cleanup */
    for (int i = 0; i < count; i++) {
        dec_ref(objs[i]);
    }

    PASS();
}

/* ========== Arena Stress Tests ========== */

void test_stress_arena_large_blocks(void) {
    /* Test arena with many blocks */
    Arena* a = arena_create(1024);  /* Small block size to force growth */
    ASSERT_NOT_NULL(a);

    /* Allocate many objects */
    const int count = 100000;
    for (int i = 0; i < count; i++) {
        Obj* x = arena_mk_int(a, i);
        ASSERT_NOT_NULL(x);
    }

    /* Reset and reallocate */
    arena_reset(a);

    for (int i = 0; i < count; i++) {
        Obj* x = arena_mk_int(a, i + count);
        ASSERT_NOT_NULL(x);
    }

    arena_destroy(a);
    PASS();
}

void test_stress_arena_reset_cycles(void) {
    /* Test many arena reset cycles */
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    ASSERT_NOT_NULL(a);

    const int cycles = 10000;
    const int per_cycle = 100;

    for (int cycle = 0; cycle < cycles; cycle++) {
        for (int i = 0; i < per_cycle; i++) {
            Obj* x = arena_mk_int(a, cycle * per_cycle + i);
            ASSERT_NOT_NULL(x);
        }
        arena_reset(a);
    }

    arena_destroy(a);
    PASS();
}

void test_stress_multiple_arenas(void) {
    /* Test multiple arenas simultaneously */
    const int arena_count = 50;
    Arena* arenas[arena_count];

    for (int i = 0; i < arena_count; i++) {
        arenas[i] = arena_create(ARENA_BLOCK_SIZE);
        ASSERT_NOT_NULL(arenas[i]);
    }

    /* Allocate from each */
    for (int round = 0; round < 100; round++) {
        for (int i = 0; i < arena_count; i++) {
            Obj* x = arena_mk_int(arenas[i], round * arena_count + i);
            ASSERT_NOT_NULL(x);
        }
    }

    /* Destroy all */
    for (int i = 0; i < arena_count; i++) {
        arena_destroy(arenas[i]);
    }

    PASS();
}

/* ========== Slot Pool Stress Tests ========== */

void test_stress_slot_pool_alloc_free(void) {
    /* Test slot pool allocation/free cycles */
    const int cycles = 5000;
    const int per_cycle = 100;

    for (int cycle = 0; cycle < cycles; cycle++) {
        Obj* objs[per_cycle];

        /* Allocate */
        for (int i = 0; i < per_cycle; i++) {
            objs[i] = mk_int(cycle * per_cycle + i);
            ASSERT_NOT_NULL(objs[i]);
        }

        /* Free */
        for (int i = 0; i < per_cycle; i++) {
            dec_ref(objs[i]);
        }
    }

    PASS();
}

void test_stress_slot_pool_growth(void) {
    /* Test slot pool growth */
    Obj* objs[100000];

    /* Allocate many objects - should trigger pool growth */
    for (int i = 0; i < 100000; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }

    /* Free all */
    for (int i = 0; i < 100000; i++) {
        dec_ref(objs[i]);
    }

    PASS();
}

/* ========== Concurrent Memory Stress Tests ========== */

/* Thread data for concurrent tests */
typedef struct {
    int thread_id;
    int iterations;
    Obj** shared_objs;
    int shared_count;
    pthread_barrier_t* barrier;
} ThreadData;

static void* concurrent_alloc_free_thread(void* arg) {
    ThreadData* data = (ThreadData*)arg;

    /* Wait for all threads to be ready */
    pthread_barrier_wait(data->barrier);

    /* Allocate and free */
    for (int i = 0; i < data->iterations; i++) {
        Obj* obj = mk_int(data->thread_id * 100000 + i);
        if (obj) {
            dec_ref(obj);
        }
    }

    /* Access shared objects */
    pthread_barrier_wait(data->barrier);

    for (int i = 0; i < data->iterations; i++) {
        int idx = (data->thread_id * data->iterations + i) % data->shared_count;
        Obj* obj = data->shared_objs[idx];
        if (obj) {
            inc_ref(obj);
            /* Do something with obj */
            dec_ref(obj);
        }
    }

    return NULL;
}

void test_stress_concurrent_alloc_free(void) {
    const int num_threads = 10;
    const int iterations = 10000;
    const int shared_count = 1000;

    pthread_t threads[num_threads];
    ThreadData thread_data[num_threads];
    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, num_threads);

    /* Create shared objects */
    Obj* shared_objs[shared_count];
    for (int i = 0; i < shared_count; i++) {
        shared_objs[i] = mk_int(i);
    }

    /* Create threads */
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].iterations = iterations;
        thread_data[i].shared_objs = shared_objs;
        thread_data[i].shared_count = shared_count;
        thread_data[i].barrier = &barrier;
        pthread_create(&threads[i], NULL, concurrent_alloc_free_thread, &thread_data[i]);
    }

    /* Wait for completion */
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Cleanup shared objects */
    for (int i = 0; i < shared_count; i++) {
        dec_ref(shared_objs[i]);
    }

    pthread_barrier_destroy(&barrier);
    PASS();
}

/* ========== Memory Pattern Stress Tests ========== */

void test_stress_wave_pattern(void) {
    /* Test wave-like allocation/deallocation pattern */
    const int wave_size = 5000;
    const int waves = 20;

    for (int w = 0; w < waves; w++) {
        Obj* objs[wave_size];

        /* Allocate wave */
        for (int i = 0; i < wave_size; i++) {
            objs[i] = mk_int(w * wave_size + i);
            ASSERT_NOT_NULL(objs[i]);
        }

        /* Free wave gradually */
        for (int i = 0; i < wave_size / 2; i++) {
            dec_ref(objs[i]);
        }

        /* Allocate more */
        for (int i = 0; i < wave_size / 2; i++) {
            objs[i] = mk_int(w * wave_size * 2 + i);
            ASSERT_NOT_NULL(objs[i]);
        }

        /* Free all */
        for (int i = 0; i < wave_size; i++) {
            if (objs[i]) dec_ref(objs[i]);
        }
    }

    PASS();
}

void test_stress_random_pattern(void) {
    /* Test random allocation/deallocation pattern */
    const int total = 10000;
    const int pool_size = 1000;
    Obj* pool[pool_size];
    int pool_top = 0;

    /* Initialize pool to NULL */
    for (int i = 0; i < pool_size; i++) {
        pool[i] = NULL;
    }

    for (int op = 0; op < total; op++) {
        if (pool_top < pool_size && (rand() % 2) == 0) {
            /* Allocate */
            pool[pool_top++] = mk_int(op);
            ASSERT_NOT_NULL(pool[pool_top - 1]);
        } else if (pool_top > 0) {
            /* Free random object */
            int idx = rand() % pool_top;
            if (pool[idx]) {
                dec_ref(pool[idx]);
                pool[idx] = pool[--pool_top];
                pool[pool_top] = NULL;
            }
        }
    }

    /* Free remaining */
    for (int i = 0; i < pool_top; i++) {
        if (pool[i]) {
            dec_ref(pool[i]);
        }
    }

    PASS();
}

/* ========== Complex Structure Stress Tests ========== */

void test_stress_complex_graph(void) {
    /* Test complex object graph with sharing */
    const int node_count = 5000;

    /* Create shared leaf nodes */
    Obj* leaves[100];
    for (int i = 0; i < 100; i++) {
        leaves[i] = mk_int(i);
    }

    /* Create graph that references leaves */
    Obj* nodes[node_count];
    for (int i = 0; i < node_count; i++) {
        Obj* left = leaves[i % 100];
        inc_ref(left);
        Obj* right = leaves[(i + 50) % 100];
        inc_ref(right);
        nodes[i] = mk_pair(left, right);
    }

    /* Verify leaf refcounts are elevated */
    for (int i = 0; i < 100; i++) {
        ASSERT(leaves[i]->mark > 1);
    }

    /* Free all nodes */
    for (int i = 0; i < node_count; i++) {
        dec_ref(nodes[i]);
    }

    /* Verify leaves are back to refcount 1 */
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(leaves[i]->mark, 1);
        dec_ref(leaves[i]);
    }

    PASS();
}

void test_stress_deep_chain(void) {
    /* Test very deep reference chain */
    const int depth = 100000;

    Obj* chain = NULL;
    for (int i = 0; i < depth; i++) {
        chain = mk_pair(mk_int_unboxed(i), chain);
        if ((i % 10000) == 0) {
            ASSERT_NOT_NULL(chain);
        }
    }

    /* Walk chain */
    Obj* current = chain;
    int count = 0;
    while (current && current->tag == TAG_PAIR) {
        count++;
        current = current->b;
    }

    ASSERT_EQ(count, depth);
    dec_ref(chain);
    PASS();
}

/* ========== Box Stress Tests ========== */

void test_stress_many_boxes(void) {
    /* Test creating many boxes */
    const int count = 50000;
    Obj* boxes[count];

    for (int i = 0; i < count; i++) {
        boxes[i] = mk_box(mk_int_unboxed(i));
        ASSERT_NOT_NULL(boxes[i]);
    }

    /* Verify contents */
    for (int i = 0; i < count; i++) {
        Obj* val = box_get(boxes[i]);
        ASSERT_NOT_NULL(val);
        ASSERT_EQ(obj_to_int(val), i);
    }

    /* Free all */
    for (int i = 0; i < count; i++) {
        dec_ref(boxes[i]);
    }

    PASS();
}

void test_stress_box_updates(void) {
    /* Test many box updates */
    Obj* box = mk_box(mk_int(0));
    ASSERT_NOT_NULL(box);

    const int iterations = 100000;
    for (int i = 0; i < iterations; i++) {
        Obj* new_val = mk_int(i);
        box_set(box, new_val);
        Obj* current = box_get(box);
        ASSERT_EQ(obj_to_int(current), i);
        dec_ref(new_val);
    }

    dec_ref(box);
    PASS();
}

/* ========== Symbol Stress Tests ========== */

void test_stress_many_symbols(void) {
    /* Test creating many symbols */
    const int count = 10000;

    for (int i = 0; i < count; i++) {
        char name[32];
        snprintf(name, sizeof(name), "sym_%d", i);
        Obj* sym = mk_sym(name);
        ASSERT_NOT_NULL(sym);
        dec_ref(sym);
    }

    PASS();
}

void test_stress_symbol_interning(void) {
    /* Test symbol interning (same name = same object) */
    const char* names[] = {"foo", "bar", "baz", "qux", "test"};
    const int num_names = 5;
    const int iterations = 1000;

    for (int i = 0; i < iterations; i++) {
        Obj* syms1[num_names];
        Obj* syms2[num_names];

        /* Create symbols */
        for (int j = 0; j < num_names; j++) {
            syms1[j] = mk_sym(names[j]);
            syms2[j] = mk_sym(names[j]);
        }

        /* Verify interning */
        for (int j = 0; j < num_names; j++) {
            ASSERT_EQ(syms1[j], syms2[j]);
            dec_ref(syms1[j]);
            dec_ref(syms2[j]);
        }
    }

    PASS();
}

/* ========== List Operations Stress Tests ========== */

void test_stress_list_append_many(void) {
    /* Test list append with many elements */
    Obj* list1 = NULL;
    Obj* list2 = NULL;

    const int count = 10000;

    /* Build two lists */
    for (int i = 0; i < count; i++) {
        list1 = mk_pair(mk_int_unboxed(i), list1);
        list2 = mk_pair(mk_int_unboxed(i + count), list2);
    }

    /* Append them */
    Obj* result = list_append(list1, list2);
    ASSERT_NOT_NULL(result);

    /* Verify combined length */
    int len = 0;
    Obj* current = result;
    while (current && current->tag == TAG_PAIR) {
        len++;
        current = current->b;
    }

    ASSERT_EQ(len, count * 2);

    dec_ref(result);
    PASS();
}

void test_stress_list_reverse_large(void) {
    /* Test reversing a large list */
    Obj* list = NULL;
    const int count = 50000;

    /* Build list */
    for (int i = 0; i < count; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }

    /* Reverse */
    Obj* reversed = list_reverse(list);
    ASSERT_NOT_NULL(reversed);

    /* Verify first element is 0 */
    Obj* first = obj_car(reversed);
    ASSERT_EQ(obj_to_int(first), 0);
    dec_ref(first);

    dec_ref(list);
    dec_ref(reversed);
    PASS();
}

/* ========== Integration Stress Test ========== */

void test_stress_mixed_operations(void) {
    /* Test mixing all operations */
    const int iterations = 5000;

    for (int i = 0; i < iterations; i++) {
        /* Allocate various types */
        Obj* a = mk_int(i);
        Obj* b = mk_float((double)i);
        Obj* c = mk_pair(a, b);
        Obj* d = mk_box(a);
        Obj* e = mk_sym("test");

        /* Reference count manipulations */
        inc_ref(a);
        inc_ref(c);

        /* Defer decrement */
        defer_decrement(a);

        /* Free */
        dec_ref(e);
        dec_ref(d);
        dec_ref(c);
        dec_ref(b);
        dec_ref(a);  /* This brings a back to 1 */
        dec_ref(a);  /* This frees a */
    }

    /* Process deferred */
    flush_deferred();

    flush_freelist();
    PASS();
}

/* ========== Run All Memory Stress Tests ========== */

void run_stress_memory_tests(void) {
    TEST_SUITE("Advanced Memory Stress Tests");

    TEST_SECTION("Memory Pressure");
    RUN_TEST(test_stress_oom_allocation_pattern);
    RUN_TEST(test_stress_large_list_construction);
    RUN_TEST(test_stress_many_small_allocations);
    RUN_TEST(test_stress_nested_structures);
    RUN_TEST(test_stress_interleaved_allocations);

    TEST_SECTION("Reference Counting");
    RUN_TEST(test_stress_extreme_refcount);
    RUN_TEST(test_stress_shared_refcount_pattern);
    RUN_TEST(test_stress_refcount_cycles);

    TEST_SECTION("Free List");
    RUN_TEST(test_stress_freelist_capacity);
    RUN_TEST(test_stress_freelist_interleaved);

    TEST_SECTION("Deferred RC");
    RUN_TEST(test_stress_deferred_large_batch);
    RUN_TEST(test_stress_deferred_coalescing);
    RUN_TEST(test_stress_deferred_safe_points);

    TEST_SECTION("Arena");
    RUN_TEST(test_stress_arena_large_blocks);
    RUN_TEST(test_stress_arena_reset_cycles);
    RUN_TEST(test_stress_multiple_arenas);

    TEST_SECTION("Slot Pool");
    RUN_TEST(test_stress_slot_pool_alloc_free);
    RUN_TEST(test_stress_slot_pool_growth);

    TEST_SECTION("Concurrent");
    RUN_TEST(test_stress_concurrent_alloc_free);

    TEST_SECTION("Memory Patterns");
    RUN_TEST(test_stress_wave_pattern);
    RUN_TEST(test_stress_random_pattern);

    TEST_SECTION("Complex Structures");
    RUN_TEST(test_stress_complex_graph);
    RUN_TEST(test_stress_deep_chain);

    TEST_SECTION("Boxes");
    RUN_TEST(test_stress_many_boxes);
    RUN_TEST(test_stress_box_updates);

    TEST_SECTION("Symbols");
    RUN_TEST(test_stress_many_symbols);
    RUN_TEST(test_stress_symbol_interning);

    TEST_SECTION("Lists");
    RUN_TEST(test_stress_list_append_many);
    RUN_TEST(test_stress_list_reverse_large);

    TEST_SECTION("Integration");
    RUN_TEST(test_stress_mixed_operations);
}
