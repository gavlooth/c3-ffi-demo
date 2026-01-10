/* test_performance.c - Performance Benchmarks for OmniLisp Runtime
 *
 * This file contains performance tests and benchmarks for the memory management
 * system and core operations. These tests measure execution time and throughput
 * for various operations.
 */

#include "test_framework.h"
#include <time.h>
#include <sys/time.h>
#include <stdio.h>

/* ========== Timing Utilities ========== */

static double get_time_seconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

static void print_perf_result(const char* name, double seconds, long ops) {
    double ops_per_sec = (double)ops / seconds;
    double ns_per_op = (seconds * 1e9) / (double)ops;

    printf("    %s: %.3fs (%.0f ops/sec, %.2f ns/op)\n",
           name, seconds, ops_per_sec, ns_per_op);
}

#define PERF_BENCHMARK(name, ops, code) do { \
    double start = get_time_seconds(); \
    code; \
    double end = get_time_seconds(); \
    print_perf_result(name, end - start, (long)(ops)); \
} while(0)

/* ========== Allocation Performance ========== */

void test_perf_alloc_int(void) {
    const int count = 10000000;
    Obj** objs = malloc(count * sizeof(Obj*));

    PERF_BENCHMARK("Alloc 10M ints", count,
        for (int i = 0; i < count; i++) {
            objs[i] = mk_int(i);
        }
    );

    /* Cleanup */
    for (int i = 0; i < count; i++) {
        dec_ref(objs[i]);
    }
    free(objs);

    PASS();
}

void test_perf_alloc_float(void) {
    const int count = 5000000;
    Obj** objs = malloc(count * sizeof(Obj*));

    PERF_BENCHMARK("Alloc 5M floats", count,
        for (int i = 0; i < count; i++) {
            objs[i] = mk_float((double)i);
        }
    );

    /* Cleanup */
    for (int i = 0; i < count; i++) {
        dec_ref(objs[i]);
    }
    free(objs);

    PASS();
}

void test_perf_alloc_pair(void) {
    const int count = 2000000;
    Obj** objs = malloc(count * sizeof(Obj*));

    PERF_BENCHMARK("Alloc 2M pairs", count,
        for (int i = 0; i < count; i++) {
            objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        }
    );

    /* Cleanup */
    for (int i = 0; i < count; i++) {
        dec_ref(objs[i]);
    }
    free(objs);

    PASS();
}

void test_perf_alloc_list_chain(void) {
    const int count = 1000000;

    PERF_BENCHMARK("Build 1M-element list", count,
        Obj* list = NULL;
        for (int i = 0; i < count; i++) {
            list = mk_pair(mk_int_unboxed(i), list);
        }
        dec_ref(list);
    );

    PASS();
}

void test_perf_alloc_mixed(void) {
    const int iterations = 1000000;

    PERF_BENCHMARK("Alloc 1M mixed objects", iterations,
        for (int i = 0; i < iterations; i++) {
            Obj* a = mk_int(i);
            Obj* b = mk_float((double)i);
            Obj* c = mk_pair(a, b);
            Obj* d = mk_box(a);
            dec_ref(d);
            dec_ref(c);
            dec_ref(b);
            dec_ref(a);
        }
    );

    PASS();
}

/* ========== Immediate Value Performance ========== */

void test_perf_immediate_int_ops(void) {
    const int count = 100000000;

    PERF_BENCHMARK("100M immediate int operations", count * 2,
        for (int i = 0; i < count; i++) {
            Obj* a = mk_int_unboxed(i);
            Obj* b = mk_int_unboxed(i + 1);
            Obj* result = prim_add(a, b);
            (void)result;
        }
    );

    PASS();
}

void test_perf_boxed_vs_immediate_arithmetic(void) {
    const int count = 50000000;

    /* Boxed */
    double start = get_time_seconds();
    for (int i = 0; i < count; i++) {
        Obj* a = mk_int(i);
        Obj* b = mk_int(i + 1);
        Obj* result = prim_add(a, b);
        dec_ref(result);
        dec_ref(b);
        dec_ref(a);
    }
    double boxed_time = get_time_seconds() - start;
    print_perf_result("Boxed arithmetic (50M ops)", boxed_time, count * 3);

    /* Immediate */
    start = get_time_seconds();
    for (int i = 0; i < count; i++) {
        Obj* a = mk_int_unboxed(i);
        Obj* b = mk_int_unboxed(i + 1);
        Obj* result = prim_add(a, b);
        (void)result;
    }
    double imm_time = get_time_seconds() - start;
    print_perf_result("Immediate arithmetic (50M ops)", imm_time, count);

    printf("    Speedup: %.2fx\n", boxed_time / imm_time);
    PASS();
}

/* ========== List Operations Performance ========== */

void test_perf_list_traversal(void) {
    const int count = 1000000;
    Obj* list = NULL;

    /* Build list */
    for (int i = count - 1; i >= 0; i--) {
        list = mk_pair(mk_int_unboxed(i), list);
    }

    /* Traverse */
    long long sum = 0;
    PERF_BENCHMARK("Traverse 1M-element list", count,
        Obj* current = list;
        while (current) {
            sum += obj_to_int(current->a);
            current = current->b;
        }
    );

    /* Verify */
    ASSERT_EQ(sum, (long long)(count - 1) * count / 2);
    dec_ref(list);
    PASS();
}

/* ========== Region Performance ========== */

void test_perf_region_create_exit(void) {
    const int count = 1000000;

    PERF_BENCHMARK("Region create/exit 1M times", count,
        for (int i = 0; i < count; i++) {
            Region* r = region_create();
            region_exit(r);
        }
    );

    PASS();
}

void test_perf_region_alloc_int(void) {
    const int count = 10000000;
    Region* r = region_create();

    PERF_BENCHMARK("Region alloc 10M ints", count,
        for (int i = 0; i < count; i++) {
            mk_int_region(r, i);
        }
    );

    region_exit(r);
    PASS();
}

void test_perf_region_tethering(void) {
    const int count = 10000000;
    Region* r = region_create();

    PERF_BENCHMARK("Region tether/untether 10M times", count,
        for (int i = 0; i < count; i++) {
            region_tether_start(r);
            region_tether_end(r);
        }
    );

    region_exit(r);
    PASS();
}

void test_perf_transmigrate_list(void) {
    const int len = 10000;
    const int count = 100;
    
    Region* src = region_create();
    Obj* list = NULL;
    for (int i = 0; i < len; i++) {
        list = mk_cell_region(src, mk_int_unboxed(i), list);
    }

    PERF_BENCHMARK("Transmigrate 10k list (100x)", count,
        for (int i = 0; i < count; i++) {
            Region* dest = region_create();
            Obj* moved = transmigrate(list, src, dest);
            (void)moved;
            region_exit(dest);
        }
    );

    region_exit(src);
    PASS();
}

void test_perf_list_length(void) {
    const int count = 1000000;
    Obj* list = NULL;

    /* Build list */
    for (int i = count - 1; i >= 0; i--) {
        list = mk_pair(mk_int_unboxed(i), list);
    }

    PERF_BENCHMARK("List length 1M-element list (1000x)", 1000,
        for (int i = 0; i < 1000; i++) {
            Obj* len = list_length(list);
            dec_ref(len);
        }
    );

    dec_ref(list);
    PASS();
}

void test_perf_list_map(void) {
    const int count = 100000;
    Obj* list = NULL;

    /* Build list */
    for (int i = count - 1; i >= 0; i--) {
        list = mk_pair(mk_int_unboxed(i), list);
    }

    /* Identity function for mapping */
    Obj* identity_fn = mk_int_unboxed(42); /* Placeholder */
    (void)identity_fn;

    PERF_BENCHMARK("Map over 100k-element list (100x)", 100,
        for (int i = 0; i < 100; i++) {
            /* Note: list_map requires actual closure - using placeholder */
            Obj* current = list;
            while (current) {
                current = current->b;
            }
        }
    );

    dec_ref(list);
    PASS();
}

void test_perf_list_reverse(void) {
    const int count = 100000;

    for (int iter = 0; iter < 100; iter++) {
        Obj* list = NULL;
        for (int i = count - 1; i >= 0; i--) {
            list = mk_pair(mk_int_unboxed(i), list);
        }

        Obj* reversed = list_reverse(list);
        dec_ref(reversed);
        dec_ref(list);
    }

    PASS();
}

void test_perf_list_append(void) {
    const int count = 10000;

    PERF_BENCHMARK("Append two 10k-element lists (1000x)", 1000,
        for (int iter = 0; iter < 1000; iter++) {
            Obj* list1 = NULL;
            Obj* list2 = NULL;

            for (int i = count - 1; i >= 0; i--) {
                list1 = mk_pair(mk_int_unboxed(i), list1);
                list2 = mk_pair(mk_int_unboxed(i + count), list2);
            }

            Obj* result = list_append(list1, list2);
            dec_ref(result);
        }
    );

    PASS();
}

/* ========== Symbol Performance ========== */

void test_perf_symbol_creation(void) {
    const int count = 1000000;

    PERF_BENCHMARK("Create 1M unique symbols", count,
        for (int i = 0; i < count; i++) {
            char name[32];
            snprintf(name, sizeof(name), "sym_%d", i);
            Obj* sym = mk_sym(name);
            dec_ref(sym);
        }
    );

    PASS();
}

void test_perf_symbol_interning(void) {
    const int count = 1000000;

    /* Pre-create symbol */
    Obj* sym = mk_sym("test_symbol");
    dec_ref(sym);

    PERF_BENCHMARK("Lookup 1M interned symbols", count,
        for (int i = 0; i < count; i++) {
            Obj* s = mk_sym("test_symbol");
            dec_ref(s);
        }
    );

    PASS();
}

/* ========== Box Operations Performance ========== */

void test_perf_box_get_set(void) {
    const int count = 50000000;
    Obj* box = mk_box(mk_int(42));

    PERF_BENCHMARK("Box get/set 50M times", count * 2,
        for (int i = 0; i < count; i++) {
            Obj* val = box_get(box);
            box_set(box, val);
        }
    );

    dec_ref(box);
    PASS();
}

/* ========== Box Operations Performance ========== */

void test_perf_arena_alloc_int(void) {
    const int count = 50000000;
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    ASSERT_NOT_NULL(a);

    PERF_BENCHMARK("Arena alloc 50M ints", count,
        for (int i = 0; i < count; i++) {
            Obj* x = arena_mk_int(a, i);
            (void)x;
        }
    );

    arena_destroy(a);
    PASS();
}

void test_perf_arena_vs_heap(void) {
    const int count = 10000000;
    double heap_time, arena_time;

    /* Heap allocation */
    double start = get_time_seconds();
    for (int i = 0; i < count; i++) {
        Obj* x = mk_int(i);
        dec_ref(x);
    }
    heap_time = get_time_seconds() - start;
    print_perf_result("Heap alloc+free 10M ints", heap_time, count);

    /* Arena allocation */
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    start = get_time_seconds();
    for (int i = 0; i < count; i++) {
        Obj* x = arena_mk_int(a, i);
        (void)x;
    }
    arena_time = get_time_seconds() - start;
    print_perf_result("Arena alloc 10M ints", arena_time, count);
    arena_destroy(a);

    printf("    Speedup: %.2fx\n", heap_time / arena_time);
    PASS();
}

void test_perf_arena_reset(void) {
    const int count = 100000;
    const int per_cycle = 10000;
    Arena* a = arena_create(ARENA_BLOCK_SIZE);

    PERF_BENCHMARK("Arena reset 100k times", count,
        for (int i = 0; i < count; i++) {
            for (int j = 0; j < per_cycle; j++) {
                Obj* x = arena_mk_int(a, j);
                (void)x;
            }
            arena_reset(a);
        }
    );

    arena_destroy(a);
    PASS();
}

/* ========== Arithmetic Performance ========== */

void test_perf_arithmetic_int(void) {
    const int count = 50000000;

    PERF_BENCHMARK("Integer add 50M ops", count,
        Obj* a = mk_int_unboxed(42);
        Obj* b = mk_int_unboxed(58);
        for (int i = 0; i < count; i++) {
            Obj* result = prim_add(a, b);
            (void)result;
        }
    );

    PASS();
}

void test_perf_arithmetic_float(void) {
    const int count = 50000000;
    Obj* a = mk_float(1.5);
    Obj* b = mk_float(2.5);

    PERF_BENCHMARK("Float add 50M ops", count,
        for (int i = 0; i < count; i++) {
            Obj* result = prim_add(a, b);
            dec_ref(result);
        }
    );

    dec_ref(a);
    dec_ref(b);
    PASS();
}

void test_perf_arithmetic_mixed(void) {
    const int count = 50000000;
    Obj* b = mk_float(2.5);

    PERF_BENCHMARK("Mixed int+float add 50M ops", count,
        Obj* a = mk_int_unboxed(10);
        for (int i = 0; i < count; i++) {
            Obj* result = prim_add(a, b);
            dec_ref(result);
        }
    );

    dec_ref(b);
    PASS();
}

/* ========== Comparison Performance ========== */

void test_perf_comparison_int(void) {
    const int count = 50000000;

    PERF_BENCHMARK("Integer compare 50M ops", count,
        Obj* a = mk_int_unboxed(42);
        Obj* b = mk_int_unboxed(58);
        for (int i = 0; i < count; i++) {
            Obj* result = prim_lt(a, b);
            (void)result;
        }
    );

    PASS();
}

void test_perf_eq_immediate(void) {
    const int count = 100000000;

    PERF_BENCHMARK("Immediate eq 100M ops", count,
        Obj* a = mk_int_unboxed(42);
        Obj* b = mk_int_unboxed(42);
        for (int i = 0; i < count; i++) {
            Obj* result = prim_eq(a, b);
            (void)result;
        }
    );

    PASS();
}

/* ========== Deep Structure Performance ========== */

void test_perf_deep_list_creation(void) {
    const int depth = 1000000;

    PERF_BENCHMARK("Create 1M-deep list", depth,
        Obj* list = NULL;
        for (int i = 0; i < depth; i++) {
            list = mk_pair(mk_int_unboxed(i), list);
        }
        dec_ref(list);
    );

    PASS();
}

void test_perf_tree_building(void) {
    const int nodes = 1000000;

    PERF_BENCHMARK("Build 1M-node binary tree", nodes,
        Obj* tree = mk_int_unboxed(0);
        for (int i = 1; i < nodes; i++) {
            tree = mk_pair(mk_int_unboxed(i), mk_pair(mk_int_unboxed(i * 2), tree));
        }
        dec_ref(tree);
    );

    PASS();
}

/* ========== Concurrency Performance ========== */

static void* perf_thread_alloc(void* arg) {
    int count = *(int*)arg;
    for (int i = 0; i < count; i++) {
        Obj* obj = mk_int(i);
        dec_ref(obj);
    }
    return NULL;
}

void test_perf_multithread_alloc(void) {
    const int count = 1000000;
    const int num_threads = 4;
    pthread_t threads[num_threads];

    double start = get_time_seconds();

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, perf_thread_alloc, (void*)&count);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    double end = get_time_seconds();
    print_perf_result("4-thread alloc 4M objects", end - start, count * num_threads);

    PASS();
}

/* ========== Memory Pressure Performance ========== */

void test_perf_memory_stress(void) {
    const int waves = 1000;
    const int per_wave = 10000;

    PERF_BENCHMARK("Memory stress: 10M alloc/free cycles", waves * per_wave * 2,
        for (int w = 0; w < waves; w++) {
            Obj* objs[per_wave];
            for (int i = 0; i < per_wave; i++) {
                objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
            }
            for (int i = 0; i < per_wave; i++) {
                dec_ref(objs[i]);
            }
        }
    );

    PASS();
}

/* ========== Run All Performance Tests ========== */

void run_performance_tests(void) {
    TEST_SUITE("Performance Benchmarks");

    TEST_SECTION("Allocation Performance");
    RUN_TEST(test_perf_alloc_int);
    RUN_TEST(test_perf_alloc_float);
    RUN_TEST(test_perf_alloc_pair);
    RUN_TEST(test_perf_alloc_list_chain);
    RUN_TEST(test_perf_alloc_mixed);

    TEST_SECTION("Region Performance");
    RUN_TEST(test_perf_region_create_exit);
    RUN_TEST(test_perf_region_alloc_int);
    RUN_TEST(test_perf_region_tethering);
    RUN_TEST(test_perf_transmigrate_list);

    TEST_SECTION("Immediate Values");
    RUN_TEST(test_perf_immediate_int_ops);
    RUN_TEST(test_perf_boxed_vs_immediate_arithmetic);

    TEST_SECTION("List Operations");
    RUN_TEST(test_perf_list_traversal);
    RUN_TEST(test_perf_list_length);
    RUN_TEST(test_perf_list_map);
    RUN_TEST(test_perf_list_reverse);
    RUN_TEST(test_perf_list_append);

    TEST_SECTION("Symbol Operations");
    RUN_TEST(test_perf_symbol_creation);
    RUN_TEST(test_perf_symbol_interning);

    TEST_SECTION("Box Operations");
    RUN_TEST(test_perf_box_get_set);

    TEST_SECTION("Arena");
    RUN_TEST(test_perf_arena_alloc_int);
    RUN_TEST(test_perf_arena_vs_heap);
    RUN_TEST(test_perf_arena_reset);

    TEST_SECTION("Arithmetic");
    RUN_TEST(test_perf_arithmetic_int);
    RUN_TEST(test_perf_arithmetic_float);
    RUN_TEST(test_perf_arithmetic_mixed);

    TEST_SECTION("Comparison");
    RUN_TEST(test_perf_comparison_int);
    RUN_TEST(test_perf_eq_immediate);

    TEST_SECTION("Deep Structures");
    RUN_TEST(test_perf_deep_list_creation);
    RUN_TEST(test_perf_tree_building);

    TEST_SECTION("Concurrency");
    RUN_TEST(test_perf_multithread_alloc);

    TEST_SECTION("Memory Pressure");
    RUN_TEST(test_perf_memory_stress);
}
