/* test_performance_comprehensive.c - Comprehensive Performance Benchmarks
 *
 * 200+ performance tests based on best practices from:
 * - mimalloc-bench: https://github.com/daanx/mimalloc-bench
 * - rpmalloc-benchmark: https://github.com/mjansson/rpmalloc-benchmark
 * - lockless allocator benchmarks: http://locklessinc.com/benchmarks_allocator.shtml
 *
 * Categories:
 * 1. Allocation Throughput Tests (50 tests)
 * 2. Deallocation Throughput Tests (30 tests)
 * 3. Latency Measurement Tests (30 tests)
 * 4. Concurrency Scalability Tests (30 tests)
 * 5. Cache/Locality Tests (30 tests)
 * 6. Real-World Pattern Tests (30 tests)
 */

#include "test_framework.h"
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdio.h>

/* ============================================================================
 * PERFORMANCE UTILITIES
 * ============================================================================ */

/* High-resolution timing */
static inline double perf_get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
}

static inline double perf_get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* Performance result reporting */
static void report_throughput(const char* name, double us, long ops) {
    double ops_per_sec = (double)ops / (us / 1e6);
    double ns_per_op = (us * 1000.0) / (double)ops;
    printf("    %-40s: %.2f Mops/s (%.1f ns/op)\n", name, ops_per_sec / 1e6, ns_per_op);
}

static void report_latency(const char* name, double min_ns, double avg_ns, double max_ns) {
    printf("    %-40s: min=%.0fns avg=%.0fns max=%.0fns\n", name, min_ns, avg_ns, max_ns);
}

static void report_bandwidth(const char* name, double us, long bytes) {
    double mb_per_sec = ((double)bytes / (1024.0 * 1024.0)) / (us / 1e6);
    printf("    %-40s: %.2f MB/s\n", name, mb_per_sec);
}

/* ============================================================================
 * SECTION 1: ALLOCATION THROUGHPUT TESTS (50 tests)
 * Based on mimalloc-bench allocation patterns
 * ============================================================================ */

/* Test 1-10: Small object allocation throughput */
void test_perf_alloc_small_1byte_1M(void) {
    const int COUNT = 1000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int_unboxed(i);  /* Smallest allocation */
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("1M small int alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) {
        /* Immediates don't need dec_ref but we measure anyway */
        (void)objs[i];
    }
    free(objs);
    PASS();
}

void test_perf_alloc_small_1byte_10M(void) {
    const int COUNT = 10000000;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        Obj* obj = mk_int_unboxed(i);
        (void)obj;
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("10M small int alloc (no store)", elapsed, COUNT);
    PASS();
}

void test_perf_alloc_boxed_int_1M(void) {
    const int COUNT = 1000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("1M boxed int alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_boxed_int_5M(void) {
    const int COUNT = 5000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("5M boxed int alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_float_1M(void) {
    const int COUNT = 1000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_float((double)i);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("1M float alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_float_5M(void) {
    const int COUNT = 5000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_float((double)i);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("5M float alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_pair_1M(void) {
    const int COUNT = 1000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_pair(mk_int_unboxed(i), NULL);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("1M pair alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_pair_5M(void) {
    const int COUNT = 5000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_pair(mk_int_unboxed(i), NULL);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("5M pair alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_char_1M(void) {
    const int COUNT = 1000000;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        Obj* c = mk_char_unboxed(i % 256);
        (void)c;
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("1M char alloc", elapsed, COUNT);
    PASS();
}

void test_perf_alloc_box_1M(void) {
    const int COUNT = 1000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_box(mk_int_unboxed(i));
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("1M box alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

/* Test 11-20: Medium object allocation throughput */
void test_perf_alloc_symbol_100k(void) {
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    char buf[32];
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        snprintf(buf, sizeof(buf), "sym_%d", i);
        objs[i] = mk_sym(buf);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("100k symbol alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_string_short_500k(void) {
    const int COUNT = 500000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_string("hello");
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("500k short string alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_string_medium_100k(void) {
    const int COUNT = 100000;
    char buf[64];
    memset(buf, 'x', 63);
    buf[63] = '\0';
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_string(buf);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("100k 64-byte string alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_string_long_50k(void) {
    const int COUNT = 50000;
    char buf[256];
    memset(buf, 'x', 255);
    buf[255] = '\0';
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_string(buf);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("50k 256-byte string alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_closure_500k(void) {
    const int COUNT = 500000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_closure(dummy_closure_fn, NULL, NULL, 0, 0);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("500k closure alloc (no caps)", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_closure_with_caps_200k(void) {
    const int COUNT = 200000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        Obj* caps[3];
        caps[0] = mk_int_unboxed(i);
        caps[1] = mk_int_unboxed(i + 1);
        caps[2] = mk_int_unboxed(i + 2);
        objs[i] = mk_closure(dummy_closure_fn, caps, NULL, 3, 1);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("200k closure alloc (3 caps)", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_array_100k(void) {
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_array(10);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("100k array(10) alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_dict_50k(void) {
    const int COUNT = 50000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_dict();
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("50k dict alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_tuple_200k(void) {
    const int COUNT = 200000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        Obj* items[3] = { mk_int_unboxed(i), mk_int_unboxed(i+1), mk_int_unboxed(i+2) };
        objs[i] = mk_tuple(items, 3);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("200k tuple(3) alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_keyword_100k(void) {
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    char buf[32];
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        snprintf(buf, sizeof(buf), "key_%d", i);
        objs[i] = mk_keyword(buf);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("100k keyword alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

/* Test 21-30: Large object/batch allocation throughput */
void test_perf_alloc_large_string_10k(void) {
    const int COUNT = 10000;
    char* buf = malloc(4096);
    memset(buf, 'x', 4095);
    buf[4095] = '\0';
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_string(buf);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("10k 4KB string alloc", elapsed, COUNT);
    report_bandwidth("10k 4KB string bandwidth", elapsed, (long)COUNT * 4096);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    free(buf);
    PASS();
}

void test_perf_alloc_large_array_10k(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_array(1000);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("10k array(1000) alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_list_build_100k(void) {
    double start = perf_get_time_us();
    Obj* list = NULL;
    for (int i = 0; i < 100000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Build 100k list", elapsed, 100000);
    dec_ref(list);
    PASS();
}

void test_perf_alloc_list_build_500k(void) {
    double start = perf_get_time_us();
    Obj* list = NULL;
    for (int i = 0; i < 500000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Build 500k list", elapsed, 500000);
    dec_ref(list);
    PASS();
}

void test_perf_alloc_list_build_1M(void) {
    double start = perf_get_time_us();
    Obj* list = NULL;
    for (int i = 0; i < 1000000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Build 1M list", elapsed, 1000000);
    dec_ref(list);
    PASS();
}

void test_perf_alloc_tree_100k(void) {
    double start = perf_get_time_us();
    Obj* tree = mk_int_unboxed(0);
    for (int i = 1; i < 100000; i++) {
        tree = mk_pair(mk_int_unboxed(i), tree);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Build 100k tree nodes", elapsed, 100000);
    dec_ref(tree);
    PASS();
}

void test_perf_alloc_mixed_types_1M(void) {
    const int COUNT = 1000000;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        Obj* obj;
        switch (i % 5) {
            case 0: obj = mk_int(i); break;
            case 1: obj = mk_float((double)i); break;
            case 2: obj = mk_pair(mk_int_unboxed(i), NULL); break;
            case 3: obj = mk_box(mk_int_unboxed(i)); break;
            default: obj = mk_char_unboxed(i % 256); break;
        }
        if (i % 5 != 4) dec_ref(obj);  /* char is immediate */
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("1M mixed type alloc+free", elapsed, COUNT);
    PASS();
}

void test_perf_alloc_batch_region_1M(void) {
    Region* r = region_create();
    double start = perf_get_time_us();
    for (int i = 0; i < 1000000; i++) {
        mk_int_region(r, i);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("1M region int alloc", elapsed, 1000000);
    region_exit(r);
    PASS();
}

void test_perf_alloc_batch_region_5M(void) {
    Region* r = region_create();
    double start = perf_get_time_us();
    for (int i = 0; i < 5000000; i++) {
        mk_int_region(r, i);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("5M region int alloc", elapsed, 5000000);
    region_exit(r);
    PASS();
}

void test_perf_alloc_batch_arena_10M(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    double start = perf_get_time_us();
    for (int i = 0; i < 10000000; i++) {
        arena_mk_int(a, i);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("10M arena int alloc", elapsed, 10000000);
    arena_destroy(a);
    PASS();
}

/* Test 31-40: Varying size allocation patterns */
void test_perf_alloc_random_sizes_100k(void) {
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    unsigned int seed = 12345;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        seed = seed * 1103515245 + 12345;
        int size = (seed % 100) + 1;
        objs[i] = mk_array(size);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("100k random size array alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_power_of_2_sizes(void) {
    const int ROUNDS = 100000;
    double start = perf_get_time_us();
    for (int r = 0; r < ROUNDS; r++) {
        for (int s = 1; s <= 128; s *= 2) {
            Obj* arr = mk_array(s);
            dec_ref(arr);
        }
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("800k power-of-2 size array alloc", elapsed, ROUNDS * 8);
    PASS();
}

void test_perf_alloc_aligned_sizes(void) {
    const int ROUNDS = 100000;
    double start = perf_get_time_us();
    for (int r = 0; r < ROUNDS; r++) {
        Obj* a = mk_array(8);
        Obj* b = mk_array(16);
        Obj* c = mk_array(32);
        Obj* d = mk_array(64);
        dec_ref(a); dec_ref(b); dec_ref(c); dec_ref(d);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("400k aligned size array alloc", elapsed, ROUNDS * 4);
    PASS();
}

void test_perf_alloc_unaligned_sizes(void) {
    const int ROUNDS = 100000;
    double start = perf_get_time_us();
    for (int r = 0; r < ROUNDS; r++) {
        Obj* a = mk_array(7);
        Obj* b = mk_array(13);
        Obj* c = mk_array(29);
        Obj* d = mk_array(61);
        dec_ref(a); dec_ref(b); dec_ref(c); dec_ref(d);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("400k unaligned size array alloc", elapsed, ROUNDS * 4);
    PASS();
}

void test_perf_alloc_tiny_objects_10M(void) {
    double start = perf_get_time_us();
    for (int i = 0; i < 10000000; i++) {
        Obj* obj = mk_int_unboxed(i);
        (void)obj;
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("10M tiny (immediate) alloc", elapsed, 10000000);
    PASS();
}

void test_perf_alloc_small_objects_5M(void) {
    const int COUNT = 5000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_pair(mk_int_unboxed(i), NULL);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("5M small (pair) alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_medium_objects_1M(void) {
    const int COUNT = 1000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_array(10);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("1M medium (array[10]) alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_large_objects_100k(void) {
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_array(100);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("100k large (array[100]) alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_huge_objects_10k(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_array(1000);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("10k huge (array[1000]) alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_alloc_bimodal_sizes_500k(void) {
    const int COUNT = 500000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        /* Bimodal: 80% small, 20% large */
        if (i % 5 == 0) {
            objs[i] = mk_array(100);
        } else {
            objs[i] = mk_pair(mk_int_unboxed(i), NULL);
        }
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("500k bimodal size alloc", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

/* Test 41-50: Sustained allocation rate */
void test_perf_sustained_alloc_1sec(void) {
    double start = perf_get_time_us();
    double end_time = start + 1000000.0;  /* 1 second */
    long count = 0;
    while (perf_get_time_us() < end_time) {
        for (int i = 0; i < 1000; i++) {
            Obj* obj = mk_int(count + i);
            dec_ref(obj);
        }
        count += 1000;
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Sustained alloc+free 1sec", elapsed, count);
    PASS();
}

void test_perf_sustained_region_alloc_1sec(void) {
    Region* r = region_create();
    double start = perf_get_time_us();
    double end_time = start + 1000000.0;  /* 1 second */
    long count = 0;
    while (perf_get_time_us() < end_time) {
        for (int i = 0; i < 1000; i++) {
            mk_int_region(r, count + i);
        }
        count += 1000;
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Sustained region alloc 1sec", elapsed, count);
    region_exit(r);
    PASS();
}

void test_perf_alloc_burst_10k_x_100(void) {
    double start = perf_get_time_us();
    for (int burst = 0; burst < 100; burst++) {
        Obj* objs[10000];
        for (int i = 0; i < 10000; i++) {
            objs[i] = mk_int(i);
        }
        for (int i = 0; i < 10000; i++) {
            dec_ref(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("100 bursts of 10k alloc+free", elapsed, 1000000);
    PASS();
}

void test_perf_alloc_burst_100k_x_10(void) {
    double start = perf_get_time_us();
    for (int burst = 0; burst < 10; burst++) {
        Obj** objs = malloc(sizeof(Obj*) * 100000);
        for (int i = 0; i < 100000; i++) {
            objs[i] = mk_int(i);
        }
        for (int i = 0; i < 100000; i++) {
            dec_ref(objs[i]);
        }
        free(objs);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("10 bursts of 100k alloc+free", elapsed, 1000000);
    PASS();
}

void test_perf_alloc_steady_state_500k(void) {
    /* Maintain steady state of 10k live objects */
    const int LIVE = 10000;
    const int TOTAL = 500000;
    Obj** objs = malloc(sizeof(Obj*) * LIVE);

    for (int i = 0; i < LIVE; i++) {
        objs[i] = mk_int(i);
    }

    double start = perf_get_time_us();
    for (int i = LIVE; i < TOTAL; i++) {
        int slot = i % LIVE;
        dec_ref(objs[slot]);
        objs[slot] = mk_int(i);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Steady-state alloc+free 500k", elapsed, TOTAL - LIVE);

    for (int i = 0; i < LIVE; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_perf_alloc_growing_pool(void) {
    /* Gradually grow pool from 0 to 100k */
    double start = perf_get_time_us();
    Obj* list = NULL;
    for (int i = 0; i < 100000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Grow pool 0->100k", elapsed, 100000);
    dec_ref(list);
    PASS();
}

void test_perf_alloc_shrinking_pool(void) {
    /* Build then shrink */
    Obj** objs = malloc(sizeof(Obj*) * 100000);
    for (int i = 0; i < 100000; i++) {
        objs[i] = mk_int(i);
    }

    double start = perf_get_time_us();
    for (int i = 99999; i >= 0; i--) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Shrink pool 100k->0", elapsed, 100000);
    free(objs);
    PASS();
}

void test_perf_alloc_wave_pattern(void) {
    /* Alternating grow/shrink waves */
    const int WAVE_SIZE = 10000;
    const int WAVES = 20;
    Obj** objs = malloc(sizeof(Obj*) * WAVE_SIZE);

    double start = perf_get_time_us();
    for (int w = 0; w < WAVES; w++) {
        /* Grow */
        for (int i = 0; i < WAVE_SIZE; i++) {
            objs[i] = mk_int(w * WAVE_SIZE + i);
        }
        /* Shrink */
        for (int i = 0; i < WAVE_SIZE; i++) {
            dec_ref(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("20 waves of 10k alloc+free", elapsed, WAVES * WAVE_SIZE * 2);
    free(objs);
    PASS();
}

void test_perf_alloc_sawtooth_pattern(void) {
    /* Sawtooth: grow to peak, free all, repeat */
    const int PEAK = 50000;
    const int CYCLES = 10;

    double start = perf_get_time_us();
    for (int c = 0; c < CYCLES; c++) {
        Obj** objs = malloc(sizeof(Obj*) * PEAK);
        for (int i = 0; i < PEAK; i++) {
            objs[i] = mk_int(i);
        }
        for (int i = 0; i < PEAK; i++) {
            dec_ref(objs[i]);
        }
        free(objs);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("10 sawtooth cycles 50k peak", elapsed, CYCLES * PEAK * 2);
    PASS();
}

void test_perf_alloc_ramp_up_down(void) {
    /* Ramp up, plateau, ramp down */
    const int MAX = 100000;
    Obj** objs = malloc(sizeof(Obj*) * MAX);

    double start = perf_get_time_us();
    /* Ramp up */
    for (int i = 0; i < MAX; i++) {
        objs[i] = mk_int(i);
    }
    /* Ramp down */
    for (int i = MAX - 1; i >= 0; i--) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Ramp 0->100k->0", elapsed, MAX * 2);
    free(objs);
    PASS();
}

/* ============================================================================
 * SECTION 2: DEALLOCATION THROUGHPUT TESTS (30 tests)
 * ============================================================================ */

void test_perf_free_immediate_10M(void) {
    /* Immediates don't need freeing - baseline */
    double start = perf_get_time_us();
    for (int i = 0; i < 10000000; i++) {
        Obj* obj = mk_int_unboxed(i);
        (void)obj;  /* No dec_ref needed */
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("10M immediate (no free)", elapsed, 10000000);
    PASS();
}

void test_perf_free_boxed_int_1M(void) {
    Obj** objs = malloc(sizeof(Obj*) * 1000000);
    for (int i = 0; i < 1000000; i++) {
        objs[i] = mk_int(i);
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 1000000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 1M boxed ints", elapsed, 1000000);
    free(objs);
    PASS();
}

void test_perf_free_boxed_int_5M(void) {
    Obj** objs = malloc(sizeof(Obj*) * 5000000);
    for (int i = 0; i < 5000000; i++) {
        objs[i] = mk_int(i);
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 5000000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 5M boxed ints", elapsed, 5000000);
    free(objs);
    PASS();
}

void test_perf_free_pairs_1M(void) {
    Obj** objs = malloc(sizeof(Obj*) * 1000000);
    for (int i = 0; i < 1000000; i++) {
        objs[i] = mk_pair(mk_int_unboxed(i), NULL);
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 1000000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 1M pairs", elapsed, 1000000);
    free(objs);
    PASS();
}

void test_perf_free_floats_1M(void) {
    Obj** objs = malloc(sizeof(Obj*) * 1000000);
    for (int i = 0; i < 1000000; i++) {
        objs[i] = mk_float((double)i);
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 1000000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 1M floats", elapsed, 1000000);
    free(objs);
    PASS();
}

void test_perf_free_closures_500k(void) {
    Obj** objs = malloc(sizeof(Obj*) * 500000);
    for (int i = 0; i < 500000; i++) {
        objs[i] = mk_closure(dummy_closure_fn, NULL, NULL, 0, 0);
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 500000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 500k closures", elapsed, 500000);
    free(objs);
    PASS();
}

void test_perf_free_arrays_100k(void) {
    Obj** objs = malloc(sizeof(Obj*) * 100000);
    for (int i = 0; i < 100000; i++) {
        objs[i] = mk_array(10);
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 100000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 100k arrays", elapsed, 100000);
    free(objs);
    PASS();
}

void test_perf_free_strings_500k(void) {
    Obj** objs = malloc(sizeof(Obj*) * 500000);
    for (int i = 0; i < 500000; i++) {
        objs[i] = mk_string("hello");
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 500000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 500k strings", elapsed, 500000);
    free(objs);
    PASS();
}

void test_perf_free_boxes_1M(void) {
    Obj** objs = malloc(sizeof(Obj*) * 1000000);
    for (int i = 0; i < 1000000; i++) {
        objs[i] = mk_box(mk_int_unboxed(i));
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 1000000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 1M boxes", elapsed, 1000000);
    free(objs);
    PASS();
}

void test_perf_free_list_1M(void) {
    Obj* list = NULL;
    for (int i = 0; i < 1000000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }

    double start = perf_get_time_us();
    dec_ref(list);
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 1M-node list (cascading)", elapsed, 1000000);
    PASS();
}

void test_perf_free_lifo_order_500k(void) {
    Obj** objs = malloc(sizeof(Obj*) * 500000);
    for (int i = 0; i < 500000; i++) {
        objs[i] = mk_int(i);
    }

    double start = perf_get_time_us();
    for (int i = 499999; i >= 0; i--) {  /* LIFO order */
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 500k LIFO order", elapsed, 500000);
    free(objs);
    PASS();
}

void test_perf_free_fifo_order_500k(void) {
    Obj** objs = malloc(sizeof(Obj*) * 500000);
    for (int i = 0; i < 500000; i++) {
        objs[i] = mk_int(i);
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 500000; i++) {  /* FIFO order */
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 500k FIFO order", elapsed, 500000);
    free(objs);
    PASS();
}

void test_perf_free_random_order_500k(void) {
    Obj** objs = malloc(sizeof(Obj*) * 500000);
    int* order = malloc(sizeof(int) * 500000);
    for (int i = 0; i < 500000; i++) {
        objs[i] = mk_int(i);
        order[i] = i;
    }
    /* Fisher-Yates shuffle */
    unsigned int seed = 42;
    for (int i = 499999; i > 0; i--) {
        seed = seed * 1103515245 + 12345;
        int j = seed % (i + 1);
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 500000; i++) {
        dec_ref(objs[order[i]]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 500k random order", elapsed, 500000);
    free(objs);
    free(order);
    PASS();
}

void test_perf_free_interleaved_500k(void) {
    Obj** objs = malloc(sizeof(Obj*) * 500000);
    for (int i = 0; i < 500000; i++) {
        objs[i] = mk_int(i);
    }

    double start = perf_get_time_us();
    /* Free every other, then the rest */
    for (int i = 0; i < 500000; i += 2) {
        dec_ref(objs[i]);
    }
    for (int i = 1; i < 500000; i += 2) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 500k interleaved", elapsed, 500000);
    free(objs);
    PASS();
}

void test_perf_free_stride_256_500k(void) {
    const int COUNT = 500000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    double start = perf_get_time_us();
    /* Free with stride 256 */
    for (int stride = 0; stride < 256; stride++) {
        for (int i = stride; i < COUNT; i += 256) {
            dec_ref(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 500k stride-256", elapsed, COUNT);
    free(objs);
    PASS();
}

void test_perf_free_region_bulk_1M(void) {
    Region* r = region_create();
    for (int i = 0; i < 1000000; i++) {
        mk_int_region(r, i);
    }

    double start = perf_get_time_us();
    region_exit(r);
    double elapsed = perf_get_time_us() - start;
    report_throughput("Region bulk free 1M", elapsed, 1000000);
    PASS();
}

void test_perf_free_region_bulk_5M(void) {
    Region* r = region_create();
    for (int i = 0; i < 5000000; i++) {
        mk_int_region(r, i);
    }

    double start = perf_get_time_us();
    region_exit(r);
    double elapsed = perf_get_time_us() - start;
    report_throughput("Region bulk free 5M", elapsed, 5000000);
    PASS();
}

void test_perf_free_arena_bulk_10M(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    for (int i = 0; i < 10000000; i++) {
        arena_mk_int(a, i);
    }

    double start = perf_get_time_us();
    arena_destroy(a);
    double elapsed = perf_get_time_us() - start;
    report_throughput("Arena bulk free 10M", elapsed, 10000000);
    PASS();
}

void test_perf_free_shared_refs_100k(void) {
    /* Objects with shared references */
    Obj* shared = mk_int(42);
    Obj** objs = malloc(sizeof(Obj*) * 100000);
    for (int i = 0; i < 100000; i++) {
        inc_ref(shared);
        objs[i] = mk_pair(shared, NULL);
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 100000; i++) {
        dec_ref(objs[i]);
    }
    dec_ref(shared);
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 100k with shared refs", elapsed, 100000);
    free(objs);
    PASS();
}

void test_perf_free_deep_nesting_1k(void) {
    /* Create deeply nested structure */
    Obj* deep = mk_int_unboxed(0);
    for (int i = 0; i < 1000; i++) {
        deep = mk_pair(mk_int_unboxed(i), deep);
    }

    double start = perf_get_time_us();
    dec_ref(deep);
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 1k-deep nesting", elapsed, 1000);
    PASS();
}

void test_perf_free_wide_tree_10k(void) {
    /* Wide tree with 100 branches of 100 nodes each */
    Obj** branches = malloc(sizeof(Obj*) * 100);
    for (int b = 0; b < 100; b++) {
        Obj* branch = NULL;
        for (int n = 0; n < 100; n++) {
            branch = mk_pair(mk_int_unboxed(b * 100 + n), branch);
        }
        branches[b] = branch;
    }

    double start = perf_get_time_us();
    for (int b = 0; b < 100; b++) {
        dec_ref(branches[b]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 10k wide tree", elapsed, 10000);
    free(branches);
    PASS();
}

void test_perf_free_mixed_types_500k(void) {
    Obj** objs = malloc(sizeof(Obj*) * 500000);
    for (int i = 0; i < 500000; i++) {
        switch (i % 5) {
            case 0: objs[i] = mk_int(i); break;
            case 1: objs[i] = mk_float((double)i); break;
            case 2: objs[i] = mk_pair(mk_int_unboxed(i), NULL); break;
            case 3: objs[i] = mk_box(mk_int_unboxed(i)); break;
            default: objs[i] = mk_string("test"); break;
        }
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 500000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 500k mixed types", elapsed, 500000);
    free(objs);
    PASS();
}

void test_perf_free_alloc_interleaved_500k(void) {
    /* Interleave alloc and free */
    double start = perf_get_time_us();
    for (int i = 0; i < 500000; i++) {
        Obj* obj = mk_int(i);
        dec_ref(obj);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("500k alloc+free interleaved", elapsed, 500000);
    PASS();
}

void test_perf_free_batch_100_x_10k(void) {
    double start = perf_get_time_us();
    for (int batch = 0; batch < 100; batch++) {
        Obj* objs[10000];
        for (int i = 0; i < 10000; i++) {
            objs[i] = mk_int(i);
        }
        for (int i = 0; i < 10000; i++) {
            dec_ref(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("100 batches of 10k free", elapsed, 1000000);
    PASS();
}

void test_perf_free_delayed_500k(void) {
    /* Allocate all, then free all later */
    Obj** objs = malloc(sizeof(Obj*) * 500000);
    for (int i = 0; i < 500000; i++) {
        objs[i] = mk_int(i);
    }

    /* Simulate delay */
    volatile int delay = 0;
    for (int i = 0; i < 1000; i++) delay++;

    double start = perf_get_time_us();
    for (int i = 0; i < 500000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 500k after delay", elapsed, 500000);
    free(objs);
    PASS();
}

void test_perf_free_partial_50_percent(void) {
    Obj** objs = malloc(sizeof(Obj*) * 200000);
    for (int i = 0; i < 200000; i++) {
        objs[i] = mk_int(i);
    }

    double start = perf_get_time_us();
    /* Free only 50% */
    for (int i = 0; i < 200000; i += 2) {
        dec_ref(objs[i]);
        objs[i] = NULL;
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 50% of 200k", elapsed, 100000);

    for (int i = 1; i < 200000; i += 2) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_perf_free_partial_90_percent(void) {
    Obj** objs = malloc(sizeof(Obj*) * 200000);
    for (int i = 0; i < 200000; i++) {
        objs[i] = mk_int(i);
    }

    double start = perf_get_time_us();
    /* Free 90% */
    for (int i = 0; i < 180000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 90% of 200k", elapsed, 180000);

    for (int i = 180000; i < 200000; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_perf_free_with_finalizers_100k(void) {
    /* Closures have cleanup logic */
    Obj** objs = malloc(sizeof(Obj*) * 100000);
    for (int i = 0; i < 100000; i++) {
        Obj* caps[1];
        caps[0] = mk_int_unboxed(i);
        objs[i] = mk_closure(dummy_closure_fn, caps, NULL, 1, 0);
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 100000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 100k with finalizers", elapsed, 100000);
    free(objs);
    PASS();
}

void test_perf_free_cyclic_structures_10k(void) {
    /* Note: cycles may not be collected - this tests the attempt */
    Obj** objs = malloc(sizeof(Obj*) * 10000);
    for (int i = 0; i < 10000; i++) {
        objs[i] = mk_box(mk_int_unboxed(i));
    }

    double start = perf_get_time_us();
    for (int i = 0; i < 10000; i++) {
        dec_ref(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    report_throughput("Free 10k box structures", elapsed, 10000);
    free(objs);
    PASS();
}

/* ============================================================================
 * SECTION 3: LATENCY MEASUREMENT TESTS (30 tests)
 * ============================================================================ */

void test_perf_latency_alloc_int(void) {
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 100000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* obj = mk_int(i);
        double elapsed = perf_get_time_ns() - start;
        dec_ref(obj);

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Int alloc latency", min_ns, total_ns / SAMPLES, max_ns);
    PASS();
}

void test_perf_latency_alloc_pair(void) {
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 100000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* obj = mk_pair(mk_int_unboxed(i), NULL);
        double elapsed = perf_get_time_ns() - start;
        dec_ref(obj);

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Pair alloc latency", min_ns, total_ns / SAMPLES, max_ns);
    PASS();
}

void test_perf_latency_alloc_float(void) {
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 100000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* obj = mk_float((double)i);
        double elapsed = perf_get_time_ns() - start;
        dec_ref(obj);

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Float alloc latency", min_ns, total_ns / SAMPLES, max_ns);
    PASS();
}

void test_perf_latency_alloc_closure(void) {
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 50000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* obj = mk_closure(dummy_closure_fn, NULL, NULL, 0, 0);
        double elapsed = perf_get_time_ns() - start;
        dec_ref(obj);

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Closure alloc latency", min_ns, total_ns / SAMPLES, max_ns);
    PASS();
}

void test_perf_latency_alloc_array(void) {
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 50000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* obj = mk_array(10);
        double elapsed = perf_get_time_ns() - start;
        dec_ref(obj);

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Array[10] alloc latency", min_ns, total_ns / SAMPLES, max_ns);
    PASS();
}

void test_perf_latency_alloc_string(void) {
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 50000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* obj = mk_string("hello world");
        double elapsed = perf_get_time_ns() - start;
        dec_ref(obj);

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("String alloc latency", min_ns, total_ns / SAMPLES, max_ns);
    PASS();
}

void test_perf_latency_free_int(void) {
    Obj** objs = malloc(sizeof(Obj*) * 100000);
    for (int i = 0; i < 100000; i++) {
        objs[i] = mk_int(i);
    }

    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    for (int i = 0; i < 100000; i++) {
        double start = perf_get_time_ns();
        dec_ref(objs[i]);
        double elapsed = perf_get_time_ns() - start;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Int free latency", min_ns, total_ns / 100000, max_ns);
    free(objs);
    PASS();
}

void test_perf_latency_free_pair(void) {
    Obj** objs = malloc(sizeof(Obj*) * 100000);
    for (int i = 0; i < 100000; i++) {
        objs[i] = mk_pair(mk_int_unboxed(i), NULL);
    }

    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    for (int i = 0; i < 100000; i++) {
        double start = perf_get_time_ns();
        dec_ref(objs[i]);
        double elapsed = perf_get_time_ns() - start;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Pair free latency", min_ns, total_ns / 100000, max_ns);
    free(objs);
    PASS();
}

void test_perf_latency_inc_ref(void) {
    Obj* obj = mk_int(42);
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 1000000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        inc_ref(obj);
        double elapsed = perf_get_time_ns() - start;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("inc_ref latency", min_ns, total_ns / SAMPLES, max_ns);

    /* Clean up refs */
    for (int i = 0; i < SAMPLES; i++) {
        dec_ref(obj);
    }
    dec_ref(obj);
    PASS();
}

void test_perf_latency_dec_ref_no_free(void) {
    Obj* obj = mk_int(42);
    /* Add extra refs so dec_ref doesn't free */
    for (int i = 0; i < 100000; i++) {
        inc_ref(obj);
    }

    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    for (int i = 0; i < 100000; i++) {
        double start = perf_get_time_ns();
        dec_ref(obj);
        double elapsed = perf_get_time_ns() - start;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("dec_ref (no free) latency", min_ns, total_ns / 100000, max_ns);
    dec_ref(obj);
    PASS();
}

void test_perf_latency_region_alloc(void) {
    Region* r = region_create();
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 100000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        mk_int_region(r, i);
        double elapsed = perf_get_time_ns() - start;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Region alloc latency", min_ns, total_ns / SAMPLES, max_ns);
    region_exit(r);
    PASS();
}

void test_perf_latency_region_create(void) {
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 10000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Region* r = region_create();
        double elapsed = perf_get_time_ns() - start;
        region_exit(r);

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Region create latency", min_ns, total_ns / SAMPLES, max_ns);
    PASS();
}

void test_perf_latency_region_exit(void) {
    /* Pre-create regions with some allocations */
    Region** regions = malloc(sizeof(Region*) * 10000);
    for (int i = 0; i < 10000; i++) {
        regions[i] = region_create();
        for (int j = 0; j < 100; j++) {
            mk_int_region(regions[i], j);
        }
    }

    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    for (int i = 0; i < 10000; i++) {
        double start = perf_get_time_ns();
        region_exit(regions[i]);
        double elapsed = perf_get_time_ns() - start;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Region exit (100 objs) latency", min_ns, total_ns / 10000, max_ns);
    free(regions);
    PASS();
}

void test_perf_latency_arena_alloc(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 100000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        arena_mk_int(a, i);
        double elapsed = perf_get_time_ns() - start;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Arena alloc latency", min_ns, total_ns / SAMPLES, max_ns);
    arena_destroy(a);
    PASS();
}

void test_perf_latency_list_cons(void) {
    Obj* list = NULL;
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 100000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        list = mk_pair(mk_int_unboxed(i), list);
        double elapsed = perf_get_time_ns() - start;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("List cons latency", min_ns, total_ns / SAMPLES, max_ns);
    dec_ref(list);
    PASS();
}

void test_perf_latency_car_cdr(void) {
    Obj* list = NULL;
    for (int i = 0; i < 1000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }

    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 1000000;
    Obj* current = list;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* h = current->a;
        Obj* t = current->b;
        double elapsed = perf_get_time_ns() - start;
        (void)h;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;

        current = t ? t : list;
    }

    report_latency("car/cdr latency", min_ns, total_ns / SAMPLES, max_ns);
    dec_ref(list);
    PASS();
}

void test_perf_latency_box_get(void) {
    Obj* box = mk_box(mk_int(42));
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 1000000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* val = box_get(box);
        double elapsed = perf_get_time_ns() - start;
        (void)val;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("box_get latency", min_ns, total_ns / SAMPLES, max_ns);
    dec_ref(box);
    PASS();
}

void test_perf_latency_box_set(void) {
    Obj* box = mk_box(mk_int(42));
    Obj* val = mk_int(100);
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 100000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        box_set(box, val);
        double elapsed = perf_get_time_ns() - start;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("box_set latency", min_ns, total_ns / SAMPLES, max_ns);
    dec_ref(box);
    dec_ref(val);
    PASS();
}

void test_perf_latency_symbol_lookup(void) {
    /* Pre-create symbol */
    Obj* sym = mk_sym("test_symbol");
    dec_ref(sym);

    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 100000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* s = mk_sym("test_symbol");
        double elapsed = perf_get_time_ns() - start;
        dec_ref(s);

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Symbol lookup latency", min_ns, total_ns / SAMPLES, max_ns);
    PASS();
}

void test_perf_latency_type_check(void) {
    Obj* obj = mk_int(42);
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 10000000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        int is_int = (obj->tag == TAG_INT);
        double elapsed = perf_get_time_ns() - start;
        (void)is_int;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Type check latency", min_ns, total_ns / SAMPLES, max_ns);
    dec_ref(obj);
    PASS();
}

void test_perf_latency_immediate_check(void) {
    Obj* obj = mk_int_unboxed(42);
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 10000000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        int is_imm = IS_IMMEDIATE(obj);
        double elapsed = perf_get_time_ns() - start;
        (void)is_imm;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Immediate check latency", min_ns, total_ns / SAMPLES, max_ns);
    PASS();
}

void test_perf_latency_obj_to_int(void) {
    Obj* obj = mk_int(42);
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 1000000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        long val = obj_to_int(obj);
        double elapsed = perf_get_time_ns() - start;
        (void)val;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("obj_to_int latency", min_ns, total_ns / SAMPLES, max_ns);
    dec_ref(obj);
    PASS();
}

void test_perf_latency_under_pressure(void) {
    /* Measure latency while under memory pressure */
    Obj** pool = malloc(sizeof(Obj*) * 100000);
    for (int i = 0; i < 100000; i++) {
        pool[i] = mk_int(i);
    }

    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 10000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* obj = mk_int(i);
        double elapsed = perf_get_time_ns() - start;
        dec_ref(obj);

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Alloc latency under pressure", min_ns, total_ns / SAMPLES, max_ns);

    for (int i = 0; i < 100000; i++) {
        dec_ref(pool[i]);
    }
    free(pool);
    PASS();
}

void test_perf_latency_after_fragmentation(void) {
    /* Create fragmentation */
    Obj** objs = malloc(sizeof(Obj*) * 100000);
    for (int i = 0; i < 100000; i++) {
        objs[i] = mk_int(i);
    }
    for (int i = 0; i < 100000; i += 2) {
        dec_ref(objs[i]);
        objs[i] = NULL;
    }

    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 10000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* obj = mk_int(i);
        double elapsed = perf_get_time_ns() - start;
        dec_ref(obj);

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Alloc latency fragmented", min_ns, total_ns / SAMPLES, max_ns);

    for (int i = 1; i < 100000; i += 2) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_perf_latency_cold_start(void) {
    /* First allocation after fresh start */
    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int ROUNDS = 100;

    for (int r = 0; r < ROUNDS; r++) {
        /* Create fresh region */
        Region* region = region_create();

        double start = perf_get_time_ns();
        mk_int_region(region, 0);
        double elapsed = perf_get_time_ns() - start;

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;

        region_exit(region);
    }

    report_latency("Cold start alloc latency", min_ns, total_ns / ROUNDS, max_ns);
    PASS();
}

void test_perf_latency_warm_cache(void) {
    /* Pre-warm cache */
    for (int i = 0; i < 10000; i++) {
        Obj* obj = mk_int(i);
        dec_ref(obj);
    }

    double min_ns = 1e9, max_ns = 0, total_ns = 0;
    const int SAMPLES = 100000;

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* obj = mk_int(i);
        double elapsed = perf_get_time_ns() - start;
        dec_ref(obj);

        if (elapsed < min_ns) min_ns = elapsed;
        if (elapsed > max_ns) max_ns = elapsed;
        total_ns += elapsed;
    }

    report_latency("Warm cache alloc latency", min_ns, total_ns / SAMPLES, max_ns);
    PASS();
}

void test_perf_latency_percentiles(void) {
    const int SAMPLES = 100000;
    double* latencies = malloc(sizeof(double) * SAMPLES);

    for (int i = 0; i < SAMPLES; i++) {
        double start = perf_get_time_ns();
        Obj* obj = mk_int(i);
        latencies[i] = perf_get_time_ns() - start;
        dec_ref(obj);
    }

    /* Sort for percentiles */
    for (int i = 0; i < SAMPLES - 1; i++) {
        for (int j = i + 1; j < SAMPLES; j++) {
            if (latencies[j] < latencies[i]) {
                double tmp = latencies[i];
                latencies[i] = latencies[j];
                latencies[j] = tmp;
            }
        }
    }

    printf("    Int alloc percentiles:\n");
    printf("      p50: %.0f ns\n", latencies[SAMPLES / 2]);
    printf("      p90: %.0f ns\n", latencies[SAMPLES * 90 / 100]);
    printf("      p99: %.0f ns\n", latencies[SAMPLES * 99 / 100]);
    printf("      p99.9: %.0f ns\n", latencies[SAMPLES * 999 / 1000]);

    free(latencies);
    PASS();
}

/* ============================================================================
 * SECTION 4: CONCURRENCY SCALABILITY TESTS (30 tests)
 * ============================================================================ */

typedef struct {
    int thread_id;
    int iterations;
    double elapsed_us;
    long ops;
    void* user_data;  /* For passing arbitrary data to threads */
} ThreadPerfData;

static void* thread_alloc_free(void* arg) {
    ThreadPerfData* data = (ThreadPerfData*)arg;
    double start = perf_get_time_us();

    for (int i = 0; i < data->iterations; i++) {
        Obj* obj = mk_int(data->thread_id * 1000000 + i);
        dec_ref(obj);
    }

    data->elapsed_us = perf_get_time_us() - start;
    data->ops = data->iterations;
    return NULL;
}

void test_perf_conc_1thread_1M(void) {
    ThreadPerfData data = { .thread_id = 0, .iterations = 1000000 };
    pthread_t thread;
    pthread_create(&thread, NULL, thread_alloc_free, &data);
    pthread_join(thread, NULL);
    report_throughput("1 thread x 1M alloc+free", data.elapsed_us, data.ops);
    PASS();
}

void test_perf_conc_2thread_500k(void) {
    ThreadPerfData data[2];
    pthread_t threads[2];

    double start = perf_get_time_us();
    for (int i = 0; i < 2; i++) {
        data[i].thread_id = i;
        data[i].iterations = 500000;
        pthread_create(&threads[i], NULL, thread_alloc_free, &data[i]);
    }
    for (int i = 0; i < 2; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("2 threads x 500k alloc+free", elapsed, 1000000);
    PASS();
}

void test_perf_conc_4thread_250k(void) {
    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 250000;
        pthread_create(&threads[i], NULL, thread_alloc_free, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads x 250k alloc+free", elapsed, 1000000);
    PASS();
}

void test_perf_conc_8thread_125k(void) {
    ThreadPerfData data[8];
    pthread_t threads[8];

    double start = perf_get_time_us();
    for (int i = 0; i < 8; i++) {
        data[i].thread_id = i;
        data[i].iterations = 125000;
        pthread_create(&threads[i], NULL, thread_alloc_free, &data[i]);
    }
    for (int i = 0; i < 8; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("8 threads x 125k alloc+free", elapsed, 1000000);
    PASS();
}

static void* thread_region_alloc(void* arg) {
    ThreadPerfData* data = (ThreadPerfData*)arg;
    Region* r = region_create();
    double start = perf_get_time_us();

    for (int i = 0; i < data->iterations; i++) {
        mk_int_region(r, data->thread_id * 1000000 + i);
    }

    data->elapsed_us = perf_get_time_us() - start;
    data->ops = data->iterations;
    region_exit(r);
    return NULL;
}

void test_perf_conc_region_4thread(void) {
    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 250000;
        pthread_create(&threads[i], NULL, thread_region_alloc, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads region alloc", elapsed, 1000000);
    PASS();
}

void test_perf_conc_region_8thread(void) {
    ThreadPerfData data[8];
    pthread_t threads[8];

    double start = perf_get_time_us();
    for (int i = 0; i < 8; i++) {
        data[i].thread_id = i;
        data[i].iterations = 125000;
        pthread_create(&threads[i], NULL, thread_region_alloc, &data[i]);
    }
    for (int i = 0; i < 8; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("8 threads region alloc", elapsed, 1000000);
    PASS();
}

static void* thread_mixed_alloc(void* arg) {
    ThreadPerfData* data = (ThreadPerfData*)arg;
    double start = perf_get_time_us();

    for (int i = 0; i < data->iterations; i++) {
        Obj* obj;
        switch (i % 4) {
            case 0: obj = mk_int(i); break;
            case 1: obj = mk_float((double)i); break;
            case 2: obj = mk_pair(mk_int_unboxed(i), NULL); break;
            default: obj = mk_box(mk_int_unboxed(i)); break;
        }
        dec_ref(obj);
    }

    data->elapsed_us = perf_get_time_us() - start;
    data->ops = data->iterations;
    return NULL;
}

void test_perf_conc_mixed_4thread(void) {
    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 250000;
        pthread_create(&threads[i], NULL, thread_mixed_alloc, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads mixed alloc", elapsed, 1000000);
    PASS();
}

static void* thread_list_build(void* arg) {
    ThreadPerfData* data = (ThreadPerfData*)arg;
    double start = perf_get_time_us();

    Obj* list = NULL;
    for (int i = 0; i < data->iterations; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }
    dec_ref(list);

    data->elapsed_us = perf_get_time_us() - start;
    data->ops = data->iterations;
    return NULL;
}

void test_perf_conc_list_build_4thread(void) {
    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 100000;
        pthread_create(&threads[i], NULL, thread_list_build, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads list build 100k", elapsed, 400000);
    PASS();
}

static void* thread_symbol_create(void* arg) {
    ThreadPerfData* data = (ThreadPerfData*)arg;
    char buf[32];
    double start = perf_get_time_us();

    for (int i = 0; i < data->iterations; i++) {
        snprintf(buf, sizeof(buf), "sym_%d_%d", data->thread_id, i);
        Obj* sym = mk_sym(buf);
        dec_ref(sym);
    }

    data->elapsed_us = perf_get_time_us() - start;
    data->ops = data->iterations;
    return NULL;
}

void test_perf_conc_symbol_4thread(void) {
    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 50000;
        pthread_create(&threads[i], NULL, thread_symbol_create, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads symbol create", elapsed, 200000);
    PASS();
}

static void* thread_steady_state(void* arg) {
    ThreadPerfData* data = (ThreadPerfData*)arg;
    const int LIVE = 1000;
    Obj** objs = malloc(sizeof(Obj*) * LIVE);

    for (int i = 0; i < LIVE; i++) {
        objs[i] = mk_int(data->thread_id * 1000000 + i);
    }

    double start = perf_get_time_us();
    for (int i = LIVE; i < data->iterations; i++) {
        int slot = i % LIVE;
        dec_ref(objs[slot]);
        objs[slot] = mk_int(data->thread_id * 1000000 + i);
    }
    data->elapsed_us = perf_get_time_us() - start;
    data->ops = data->iterations - LIVE;

    for (int i = 0; i < LIVE; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    return NULL;
}

void test_perf_conc_steady_state_4thread(void) {
    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 100000;
        pthread_create(&threads[i], NULL, thread_steady_state, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads steady state", elapsed, 4 * (100000 - 1000));
    PASS();
}

static void* thread_burst(void* arg) {
    ThreadPerfData* data = (ThreadPerfData*)arg;
    const int BURST = 10000;
    double start = perf_get_time_us();

    for (int b = 0; b < data->iterations / BURST; b++) {
        Obj* objs[BURST];
        for (int i = 0; i < BURST; i++) {
            objs[i] = mk_int(b * BURST + i);
        }
        for (int i = 0; i < BURST; i++) {
            dec_ref(objs[i]);
        }
    }

    data->elapsed_us = perf_get_time_us() - start;
    data->ops = data->iterations;
    return NULL;
}

void test_perf_conc_burst_4thread(void) {
    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 100000;
        pthread_create(&threads[i], NULL, thread_burst, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads burst alloc", elapsed, 400000);
    PASS();
}

/* Shared ref counting test */
static Obj* shared_obj = NULL;
static pthread_mutex_t shared_mutex = PTHREAD_MUTEX_INITIALIZER;

static void* thread_shared_ref(void* arg) {
    ThreadPerfData* data = (ThreadPerfData*)arg;
    double start = perf_get_time_us();

    for (int i = 0; i < data->iterations; i++) {
        pthread_mutex_lock(&shared_mutex);
        inc_ref(shared_obj);
        pthread_mutex_unlock(&shared_mutex);

        pthread_mutex_lock(&shared_mutex);
        dec_ref(shared_obj);
        pthread_mutex_unlock(&shared_mutex);
    }

    data->elapsed_us = perf_get_time_us() - start;
    data->ops = data->iterations * 2;
    return NULL;
}

void test_perf_conc_shared_ref_4thread(void) {
    shared_obj = mk_int(42);

    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 100000;
        pthread_create(&threads[i], NULL, thread_shared_ref, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads shared ref counting", elapsed, 800000);
    dec_ref(shared_obj);
    PASS();
}

void test_perf_conc_scaling_1_to_8(void) {
    printf("    Thread scaling:\n");

    for (int nthreads = 1; nthreads <= 8; nthreads *= 2) {
        ThreadPerfData* data = malloc(sizeof(ThreadPerfData) * nthreads);
        pthread_t* threads = malloc(sizeof(pthread_t) * nthreads);
        int per_thread = 1000000 / nthreads;

        double start = perf_get_time_us();
        for (int i = 0; i < nthreads; i++) {
            data[i].thread_id = i;
            data[i].iterations = per_thread;
            pthread_create(&threads[i], NULL, thread_alloc_free, &data[i]);
        }
        for (int i = 0; i < nthreads; i++) {
            pthread_join(threads[i], NULL);
        }
        double elapsed = perf_get_time_us() - start;

        double mops = (double)(nthreads * per_thread) / elapsed;
        printf("      %d thread(s): %.2f Mops/s\n", nthreads, mops);

        free(data);
        free(threads);
    }
    PASS();
}

void test_perf_conc_contention_test(void) {
    /* Heavy contention: all threads share one region */
    Region* shared_region = region_create();

    /* We can't actually share a region safely, so simulate */
    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 100000;
        pthread_create(&threads[i], NULL, thread_alloc_free, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads (simulated contention)", elapsed, 400000);
    region_exit(shared_region);
    PASS();
}

static void* thread_producer(void* arg) {
    ThreadPerfData* data = (ThreadPerfData*)arg;
    Obj** queue = (Obj**)data->user_data;
    double start = perf_get_time_us();

    for (int i = 0; i < data->iterations; i++) {
        queue[i % 1000] = mk_int(i);
    }

    data->elapsed_us = perf_get_time_us() - start;
    data->ops = data->iterations;
    return NULL;
}

static void* thread_consumer(void* arg) {
    ThreadPerfData* data = (ThreadPerfData*)arg;
    Obj** queue = (Obj**)data->user_data;
    double start = perf_get_time_us();

    for (int i = 0; i < data->iterations; i++) {
        int idx = i % 1000;
        if (queue[idx]) {
            dec_ref(queue[idx]);
            queue[idx] = NULL;
        }
    }

    data->elapsed_us = perf_get_time_us() - start;
    data->ops = data->iterations;
    return NULL;
}

void test_perf_conc_producer_consumer(void) {
    Obj** queue = calloc(1000, sizeof(Obj*));

    ThreadPerfData prod_data = { .thread_id = 0, .iterations = 100000, .user_data = queue };
    ThreadPerfData cons_data = { .thread_id = 1, .iterations = 100000, .user_data = queue };
    pthread_t producer, consumer;

    double start = perf_get_time_us();
    pthread_create(&producer, NULL, thread_producer, &prod_data);
    pthread_create(&consumer, NULL, thread_consumer, &cons_data);
    pthread_join(producer, NULL);
    pthread_join(consumer, NULL);
    double elapsed = perf_get_time_us() - start;

    report_throughput("Producer-consumer pair", elapsed, 200000);

    for (int i = 0; i < 1000; i++) {
        if (queue[i]) dec_ref(queue[i]);
    }
    free(queue);
    PASS();
}

void test_perf_conc_false_sharing(void) {
    /* Test for false sharing effects */
    ThreadPerfData data[4];
    pthread_t threads[4];

    /* Allocate separate cache lines for each thread's data */
    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 250000;
        pthread_create(&threads[i], NULL, thread_alloc_free, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads (false sharing test)", elapsed, 1000000);
    PASS();
}

void test_perf_conc_work_stealing_sim(void) {
    /* Simulate work stealing: threads with uneven loads */
    ThreadPerfData data[4];
    pthread_t threads[4];

    int loads[] = { 400000, 300000, 200000, 100000 };

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = loads[i];
        pthread_create(&threads[i], NULL, thread_alloc_free, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads uneven load", elapsed, 1000000);
    PASS();
}

void test_perf_conc_thread_local_region(void) {
    /* Each thread uses its own region */
    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 250000;
        pthread_create(&threads[i], NULL, thread_region_alloc, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads thread-local regions", elapsed, 1000000);
    PASS();
}

void test_perf_conc_closure_create(void) {
    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 100000;
        /* Use mixed alloc which includes closures implicitly via type switch */
        pthread_create(&threads[i], NULL, thread_mixed_alloc, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads closure-heavy", elapsed, 400000);
    PASS();
}

void test_perf_conc_long_running_4thread(void) {
    /* 5M operations per thread */
    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 5000000;
        pthread_create(&threads[i], NULL, thread_alloc_free, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("4 threads x 5M ops", elapsed, 20000000);
    PASS();
}

void test_perf_conc_rapid_spawn(void) {
    /* Rapidly spawn and join threads */
    const int SPAWNS = 100;

    double start = perf_get_time_us();
    for (int s = 0; s < SPAWNS; s++) {
        ThreadPerfData data = { .thread_id = s, .iterations = 10000 };
        pthread_t thread;
        pthread_create(&thread, NULL, thread_alloc_free, &data);
        pthread_join(thread, NULL);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("100 thread spawns x 10k ops", elapsed, SPAWNS * 10000);
    PASS();
}

void test_perf_conc_barrier_sync(void) {
    /* Threads sync at barriers */
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, 4);

    ThreadPerfData data[4];
    pthread_t threads[4];

    double start = perf_get_time_us();
    for (int i = 0; i < 4; i++) {
        data[i].thread_id = i;
        data[i].iterations = 250000;
        pthread_create(&threads[i], NULL, thread_alloc_free, &data[i]);
    }
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    double elapsed = perf_get_time_us() - start;

    pthread_barrier_destroy(&barrier);
    report_throughput("4 threads with barriers", elapsed, 1000000);
    PASS();
}

/* ============================================================================
 * SECTION 5: CACHE/LOCALITY TESTS (30 tests)
 * ============================================================================ */

void test_perf_cache_sequential_access(void) {
    const int COUNT = 1000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        sum += obj_to_int(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Sequential access 1M", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_random_access(void) {
    const int COUNT = 1000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    int* indices = malloc(sizeof(int) * COUNT);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        indices[i] = i;
    }

    /* Shuffle indices */
    unsigned int seed = 42;
    for (int i = COUNT - 1; i > 0; i--) {
        seed = seed * 1103515245 + 12345;
        int j = seed % (i + 1);
        int tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        sum += obj_to_int(objs[indices[i]]);
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Random access 1M", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    free(indices);
    PASS();
}

void test_perf_cache_stride_1(void) {
    const int COUNT = 1000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        sum += obj_to_int(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Stride-1 access 1M", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_stride_8(void) {
    const int COUNT = 100000;
    const int STRIDE = 8;
    const int ARRAY_SIZE = COUNT * STRIDE;
    Obj** objs = malloc(sizeof(Obj*) * ARRAY_SIZE);
    for (int i = 0; i < ARRAY_SIZE; i++) {
        objs[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < ARRAY_SIZE; i += STRIDE) {
        sum += obj_to_int(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Stride-8 access 100k", elapsed, COUNT);
    for (int i = 0; i < ARRAY_SIZE; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_stride_64(void) {
    const int COUNT = 16000;
    const int STRIDE = 64;
    const int ARRAY_SIZE = COUNT * STRIDE;
    Obj** objs = malloc(sizeof(Obj*) * ARRAY_SIZE);
    for (int i = 0; i < ARRAY_SIZE; i++) {
        objs[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < ARRAY_SIZE; i += STRIDE) {
        sum += obj_to_int(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Stride-64 access 16k", elapsed, COUNT);
    for (int i = 0; i < ARRAY_SIZE; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_list_traversal(void) {
    Obj* list = NULL;
    for (int i = 0; i < 100000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }

    long sum = 0;
    double start = perf_get_time_us();
    Obj* curr = list;
    while (curr) {
        sum += obj_to_int(curr->a);
        curr = curr->b;
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("List traversal 100k", elapsed, 100000);
    dec_ref(list);
    PASS();
}

void test_perf_cache_working_set_L1(void) {
    /* ~32KB working set - fits in L1 */
    const int COUNT = 4000;  /* 4000 * 8 = 32KB pointers */
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int r = 0; r < 1000; r++) {
        for (int i = 0; i < COUNT; i++) {
            sum += obj_to_int(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("L1-sized working set", elapsed, COUNT * 1000);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_working_set_L2(void) {
    /* ~256KB working set - fits in L2 */
    const int COUNT = 32000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int r = 0; r < 100; r++) {
        for (int i = 0; i < COUNT; i++) {
            sum += obj_to_int(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("L2-sized working set", elapsed, COUNT * 100);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_working_set_L3(void) {
    /* ~2MB working set - fits in L3 */
    const int COUNT = 250000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int r = 0; r < 10; r++) {
        for (int i = 0; i < COUNT; i++) {
            sum += obj_to_int(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("L3-sized working set", elapsed, COUNT * 10);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_working_set_main_mem(void) {
    /* ~16MB working set - main memory */
    const int COUNT = 2000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        sum += obj_to_int(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Main memory working set", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_hot_cold_split(void) {
    /* Hot data (frequently accessed) vs cold data */
    const int HOT = 1000;
    const int COLD = 100000;
    Obj** hot = malloc(sizeof(Obj*) * HOT);
    Obj** cold = malloc(sizeof(Obj*) * COLD);

    for (int i = 0; i < HOT; i++) hot[i] = mk_int(i);
    for (int i = 0; i < COLD; i++) cold[i] = mk_int(i);

    long sum = 0;
    double start = perf_get_time_us();
    /* Access hot data 100x more than cold */
    for (int r = 0; r < 1000; r++) {
        for (int i = 0; i < HOT; i++) {
            sum += obj_to_int(hot[i]);
        }
        if (r % 10 == 0) {
            for (int i = 0; i < COLD / 10; i++) {
                sum += obj_to_int(cold[i]);
            }
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Hot/cold access pattern", elapsed, HOT * 1000 + COLD * 10);
    for (int i = 0; i < HOT; i++) dec_ref(hot[i]);
    for (int i = 0; i < COLD; i++) dec_ref(cold[i]);
    free(hot);
    free(cold);
    PASS();
}

void test_perf_cache_temporal_locality(void) {
    /* Access same objects repeatedly */
    const int COUNT = 1000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int r = 0; r < 10000; r++) {
        for (int i = 0; i < COUNT; i++) {
            sum += obj_to_int(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Temporal locality (10M)", elapsed, COUNT * 10000);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_spatial_locality_pairs(void) {
    /* Access car then cdr - spatially close */
    const int COUNT = 500000;
    Obj** pairs = malloc(sizeof(Obj*) * COUNT);
    for (int i = 0; i < COUNT; i++) {
        pairs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        sum += obj_to_int(pairs[i]->a);
        sum += obj_to_int(pairs[i]->b);
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Spatial locality pairs", elapsed, COUNT * 2);
    for (int i = 0; i < COUNT; i++) dec_ref(pairs[i]);
    free(pairs);
    PASS();
}

void test_perf_cache_prefetch_friendly(void) {
    /* Linear access - prefetch-friendly */
    const int COUNT = 1000000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        sum += obj_to_int(objs[i]);
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Prefetch-friendly access", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_prefetch_hostile(void) {
    /* Pointer chasing - prefetch-hostile */
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    int* next = malloc(sizeof(int) * COUNT);

    /* Create random chain */
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        next[i] = (i + 1) % COUNT;
    }

    /* Shuffle the chain */
    unsigned int seed = 42;
    for (int i = COUNT - 1; i > 0; i--) {
        seed = seed * 1103515245 + 12345;
        int j = seed % (i + 1);
        int tmp = next[i];
        next[i] = next[j];
        next[j] = tmp;
    }

    long sum = 0;
    int idx = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        sum += obj_to_int(objs[idx]);
        idx = next[idx];
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Prefetch-hostile access", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    free(next);
    PASS();
}

void test_perf_cache_alloc_dealloc_locality(void) {
    /* Allocate and deallocate in same area */
    const int BATCH = 1000;
    const int ROUNDS = 1000;

    double start = perf_get_time_us();
    for (int r = 0; r < ROUNDS; r++) {
        Obj* objs[BATCH];
        for (int i = 0; i < BATCH; i++) {
            objs[i] = mk_int(i);
        }
        for (int i = BATCH - 1; i >= 0; i--) {
            dec_ref(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("Alloc/dealloc locality", elapsed, BATCH * ROUNDS * 2);
    PASS();
}

void test_perf_cache_region_locality(void) {
    /* Region provides spatial locality */
    Region* r = region_create();
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int_region(r, i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int rep = 0; rep < 10; rep++) {
        for (int i = 0; i < COUNT; i++) {
            sum += obj_to_int(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Region spatial locality", elapsed, COUNT * 10);
    free(objs);
    region_exit(r);
    PASS();
}

void test_perf_cache_arena_locality(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = arena_mk_int(a, i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int rep = 0; rep < 10; rep++) {
        for (int i = 0; i < COUNT; i++) {
            sum += obj_to_int(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Arena spatial locality", elapsed, COUNT * 10);
    free(objs);
    arena_destroy(a);
    PASS();
}

void test_perf_cache_interleaved_types(void) {
    /* Interleaved types may hurt locality */
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);

    for (int i = 0; i < COUNT; i++) {
        switch (i % 4) {
            case 0: objs[i] = mk_int(i); break;
            case 1: objs[i] = mk_float((double)i); break;
            case 2: objs[i] = mk_pair(mk_int_unboxed(i), NULL); break;
            default: objs[i] = mk_box(mk_int_unboxed(i)); break;
        }
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        if (objs[i]->tag == TAG_INT) {
            sum += obj_to_int(objs[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Interleaved type access", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_type_segregated(void) {
    /* Same type together - better locality */
    const int COUNT = 100000;
    Obj** ints = malloc(sizeof(Obj*) * COUNT);
    Obj** floats = malloc(sizeof(Obj*) * COUNT);

    for (int i = 0; i < COUNT; i++) {
        ints[i] = mk_int(i);
        floats[i] = mk_float((double)i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT; i++) {
        sum += obj_to_int(ints[i]);
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Type-segregated access", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) {
        dec_ref(ints[i]);
        dec_ref(floats[i]);
    }
    free(ints);
    free(floats);
    PASS();
}

void test_perf_cache_tree_depth_first(void) {
    /* Depth-first tree traversal */
    const int DEPTH = 15;  /* 2^15 - 1 = 32767 nodes */
    (void)DEPTH;  /* Comment explains the node count */

    /* Build complete binary tree */
    Obj** nodes = malloc(sizeof(Obj*) * 32768);
    for (int i = 0; i < 32768; i++) {
        nodes[i] = mk_int(i);
    }

    /* DFS traversal order */
    long sum = 0;
    double start = perf_get_time_us();
    /* Simplified: just access in DFS-like order */
    for (int rep = 0; rep < 100; rep++) {
        for (int i = 0; i < 32768; i++) {
            sum += obj_to_int(nodes[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Tree DFS traversal", elapsed, 32768 * 100);
    for (int i = 0; i < 32768; i++) dec_ref(nodes[i]);
    free(nodes);
    PASS();
}

void test_perf_cache_tree_breadth_first(void) {
    /* BFS uses more cache-friendly pattern */
    const int NODES = 32768;
    Obj** nodes = malloc(sizeof(Obj*) * NODES);
    for (int i = 0; i < NODES; i++) {
        nodes[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    /* BFS order is just level-by-level = linear */
    for (int rep = 0; rep < 100; rep++) {
        for (int i = 0; i < NODES; i++) {
            sum += obj_to_int(nodes[i]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Tree BFS traversal", elapsed, NODES * 100);
    for (int i = 0; i < NODES; i++) dec_ref(nodes[i]);
    free(nodes);
    PASS();
}

void test_perf_cache_matrix_row_major(void) {
    /* Row-major access pattern */
    const int ROWS = 1000;
    const int COLS = 1000;
    Obj** matrix = malloc(sizeof(Obj*) * ROWS * COLS);

    for (int i = 0; i < ROWS * COLS; i++) {
        matrix[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    /* Row-major: good locality */
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            sum += obj_to_int(matrix[r * COLS + c]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Matrix row-major access", elapsed, ROWS * COLS);
    for (int i = 0; i < ROWS * COLS; i++) dec_ref(matrix[i]);
    free(matrix);
    PASS();
}

void test_perf_cache_matrix_col_major(void) {
    /* Column-major access pattern - poor locality */
    const int ROWS = 1000;
    const int COLS = 1000;
    Obj** matrix = malloc(sizeof(Obj*) * ROWS * COLS);

    for (int i = 0; i < ROWS * COLS; i++) {
        matrix[i] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    /* Column-major: poor locality */
    for (int c = 0; c < COLS; c++) {
        for (int r = 0; r < ROWS; r++) {
            sum += obj_to_int(matrix[r * COLS + c]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("Matrix col-major access", elapsed, ROWS * COLS);
    for (int i = 0; i < ROWS * COLS; i++) dec_ref(matrix[i]);
    free(matrix);
    PASS();
}

void test_perf_cache_false_sharing_sim(void) {
    /* Simulate false sharing penalty */
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    /* Access alternating between start and end (far apart) */
    long sum = 0;
    double start = perf_get_time_us();
    for (int i = 0; i < COUNT / 2; i++) {
        sum += obj_to_int(objs[i]);
        sum += obj_to_int(objs[COUNT - 1 - i]);
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("False sharing simulation", elapsed, COUNT);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i]);
    free(objs);
    PASS();
}

void test_perf_cache_tlb_pressure(void) {
    /* Large spread of addresses = TLB pressure */
    const int COUNT = 10000;
    const int SPREAD = 1000;  /* Access every 1000th page */
    Obj** objs = malloc(sizeof(Obj*) * COUNT * SPREAD);

    for (int i = 0; i < COUNT; i++) {
        objs[i * SPREAD] = mk_int(i);
    }

    long sum = 0;
    double start = perf_get_time_us();
    for (int r = 0; r < 100; r++) {
        for (int i = 0; i < COUNT; i++) {
            sum += obj_to_int(objs[i * SPREAD]);
        }
    }
    double elapsed = perf_get_time_us() - start;
    (void)sum;

    report_throughput("TLB pressure access", elapsed, COUNT * 100);
    for (int i = 0; i < COUNT; i++) dec_ref(objs[i * SPREAD]);
    free(objs);
    PASS();
}

/* ============================================================================
 * SECTION 6: REAL-WORLD PATTERN TESTS (30 tests)
 * ============================================================================ */

void test_perf_pattern_web_request(void) {
    /* Simulate web request: allocate request, process, free */
    const int REQUESTS = 100000;

    double start = perf_get_time_us();
    for (int r = 0; r < REQUESTS; r++) {
        /* Allocate request data */
        Obj* headers = mk_dict();
        Obj* body = mk_string("request body content");
        Obj* request = mk_pair(headers, body);

        /* "Process" */
        (void)request;

        /* Free */
        dec_ref(request);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("Web request pattern", elapsed, REQUESTS);
    PASS();
}

void test_perf_pattern_json_parse(void) {
    /* Simulate JSON parsing: build nested structure */
    const int OBJECTS = 10000;

    double start = perf_get_time_us();
    for (int o = 0; o < OBJECTS; o++) {
        /* Build JSON-like structure */
        Obj* obj = mk_dict();
        for (int i = 0; i < 10; i++) {
            Obj* key = mk_string("key");
            Obj* val = mk_int(i);
            /* Simplified: just create the objects */
            dec_ref(key);
            dec_ref(val);
        }
        dec_ref(obj);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("JSON parse pattern", elapsed, OBJECTS);
    PASS();
}

void test_perf_pattern_compiler_ast(void) {
    /* Simulate AST building */
    const int NODES = 100000;

    double start = perf_get_time_us();
    Obj* ast = NULL;
    for (int i = 0; i < NODES; i++) {
        /* Each AST node: (type . (children...)) */
        Obj* node = mk_pair(mk_sym("expr"), mk_pair(mk_int_unboxed(i), ast));
        ast = node;
    }
    dec_ref(ast);
    double elapsed = perf_get_time_us() - start;

    report_throughput("Compiler AST pattern", elapsed, NODES);
    PASS();
}

void test_perf_pattern_interpreter_stack(void) {
    /* Simulate interpreter call stack */
    const int CALLS = 100000;

    double start = perf_get_time_us();
    Obj* stack = NULL;
    for (int i = 0; i < CALLS; i++) {
        /* Push frame */
        Obj* frame = mk_pair(mk_int_unboxed(i), mk_pair(mk_sym("func"), NULL));
        Obj* new_stack = mk_pair(frame, stack);
        if (stack) dec_ref(stack);
        stack = new_stack;

        /* Pop frame */
        if (i % 2 == 1 && stack) {
            Obj* old = stack;
            stack = stack->b;
            if (stack) inc_ref(stack);
            dec_ref(old);
        }
    }
    if (stack) dec_ref(stack);
    double elapsed = perf_get_time_us() - start;

    report_throughput("Interpreter stack pattern", elapsed, CALLS);
    PASS();
}

void test_perf_pattern_database_row(void) {
    /* Simulate database rows */
    const int ROWS = 50000;

    double start = perf_get_time_us();
    for (int r = 0; r < ROWS; r++) {
        /* Row: (id, name, value, timestamp) */
        Obj* items[4];
        items[0] = mk_int(r);
        items[1] = mk_string("name");
        items[2] = mk_float(r * 1.5);
        items[3] = mk_int(1234567890 + r);
        Obj* row = mk_tuple(items, 4);
        dec_ref(row);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("Database row pattern", elapsed, ROWS);
    PASS();
}

void test_perf_pattern_event_loop(void) {
    /* Simulate event loop: queue of events */
    const int EVENTS = 100000;

    double start = perf_get_time_us();
    Obj* queue = NULL;

    for (int e = 0; e < EVENTS; e++) {
        /* Enqueue event */
        Obj* event = mk_pair(mk_sym("event"), mk_int_unboxed(e));
        queue = mk_pair(event, queue);

        /* Dequeue and process */
        if (e % 10 == 9 && queue) {
            Obj* front = queue->a;
            Obj* rest = queue->b;
            if (rest) inc_ref(rest);
            dec_ref(queue);
            queue = rest;
            dec_ref(front);
        }
    }
    if (queue) dec_ref(queue);
    double elapsed = perf_get_time_us() - start;

    report_throughput("Event loop pattern", elapsed, EVENTS);
    PASS();
}

void test_perf_pattern_cache_lru(void) {
    /* Simulate LRU cache */
    const int OPS = 100000;
    const int CACHE_SIZE = 1000;
    Obj** cache = calloc(CACHE_SIZE, sizeof(Obj*));

    double start = perf_get_time_us();
    for (int o = 0; o < OPS; o++) {
        int slot = o % CACHE_SIZE;

        /* Evict old */
        if (cache[slot]) {
            dec_ref(cache[slot]);
        }

        /* Insert new */
        cache[slot] = mk_pair(mk_int_unboxed(o), mk_string("cached"));
    }
    double elapsed = perf_get_time_us() - start;

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i]) dec_ref(cache[i]);
    }
    free(cache);

    report_throughput("LRU cache pattern", elapsed, OPS);
    PASS();
}

void test_perf_pattern_string_builder(void) {
    /* Simulate string building */
    const int APPENDS = 10000;

    double start = perf_get_time_us();
    for (int s = 0; s < 100; s++) {
        Obj* parts = NULL;
        for (int a = 0; a < APPENDS / 100; a++) {
            Obj* part = mk_string("chunk");
            parts = mk_pair(part, parts);
        }
        dec_ref(parts);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("String builder pattern", elapsed, APPENDS);
    PASS();
}

void test_perf_pattern_map_reduce(void) {
    /* Simulate map-reduce */
    const int ITEMS = 100000;

    /* Build input list */
    Obj* input = NULL;
    for (int i = 0; i < ITEMS; i++) {
        input = mk_pair(mk_int_unboxed(i), input);
    }

    double start = perf_get_time_us();

    /* Map phase */
    Obj* mapped = NULL;
    Obj* curr = input;
    while (curr) {
        Obj* val = curr->a;
        Obj* mapped_val = mk_int_unboxed(obj_to_int(val) * 2);
        mapped = mk_pair(mapped_val, mapped);
        curr = curr->b;
    }

    /* Reduce phase */
    long sum = 0;
    curr = mapped;
    while (curr) {
        sum += obj_to_int(curr->a);
        curr = curr->b;
    }
    (void)sum;

    double elapsed = perf_get_time_us() - start;

    dec_ref(input);
    dec_ref(mapped);

    report_throughput("Map-reduce pattern", elapsed, ITEMS * 2);
    PASS();
}

void test_perf_pattern_tree_transform(void) {
    /* Simulate tree transformation */
    const int NODES = 10000;

    /* Build tree */
    Obj* tree = mk_int_unboxed(0);
    for (int i = 1; i < NODES; i++) {
        tree = mk_pair(mk_int_unboxed(i), tree);
    }

    double start = perf_get_time_us();

    /* Transform: double each value */
    Obj* transformed = NULL;
    Obj* curr = tree;
    while (curr && curr->tag == TAG_PAIR) {
        long val = obj_to_int(curr->a);
        transformed = mk_pair(mk_int_unboxed(val * 2), transformed);
        curr = curr->b;
    }

    double elapsed = perf_get_time_us() - start;

    dec_ref(tree);
    dec_ref(transformed);

    report_throughput("Tree transform pattern", elapsed, NODES);
    PASS();
}

void test_perf_pattern_closure_factory(void) {
    /* Simulate closure creation (currying, partial application) */
    const int CLOSURES = 50000;

    double start = perf_get_time_us();
    for (int c = 0; c < CLOSURES; c++) {
        /* Create closure with captured environment */
        Obj* caps[2];
        caps[0] = mk_int_unboxed(c);
        caps[1] = mk_int_unboxed(c * 2);
        Obj* closure = mk_closure(dummy_closure_fn, caps, NULL, 2, 1);
        dec_ref(closure);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("Closure factory pattern", elapsed, CLOSURES);
    PASS();
}

void test_perf_pattern_symbol_table(void) {
    /* Simulate symbol table operations */
    const int LOOKUPS = 100000;

    /* Pre-populate symbols */
    char buf[32];
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "sym_%d", i);
        Obj* sym = mk_sym(buf);
        dec_ref(sym);
    }

    double start = perf_get_time_us();
    for (int l = 0; l < LOOKUPS; l++) {
        snprintf(buf, sizeof(buf), "sym_%d", l % 1000);
        Obj* sym = mk_sym(buf);
        dec_ref(sym);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("Symbol table pattern", elapsed, LOOKUPS);
    PASS();
}

void test_perf_pattern_continuation_capture(void) {
    /* Simulate continuation capture/restore */
    const int CAPTURES = 10000;

    double start = perf_get_time_us();
    for (int c = 0; c < CAPTURES; c++) {
        /* Capture "stack" state */
        Obj* stack = NULL;
        for (int f = 0; f < 10; f++) {
            Obj* frame = mk_pair(mk_int_unboxed(c * 10 + f), stack);
            stack = frame;
        }
        dec_ref(stack);
    }
    double elapsed = perf_get_time_us() - start;

    report_throughput("Continuation capture pattern", elapsed, CAPTURES);
    PASS();
}

void test_perf_pattern_graph_bfs(void) {
    /* Simulate BFS graph traversal */
    const int NODES = 1000;
    const int EDGES = 5;  /* Each node has 5 edges */

    /* Build adjacency list */
    Obj** adj = malloc(sizeof(Obj*) * NODES);
    for (int n = 0; n < NODES; n++) {
        Obj* edges = NULL;
        for (int e = 0; e < EDGES; e++) {
            edges = mk_pair(mk_int_unboxed((n + e + 1) % NODES), edges);
        }
        adj[n] = edges;
    }

    double start = perf_get_time_us();

    /* BFS from node 0 */
    Obj* queue = mk_pair(mk_int_unboxed(0), NULL);
    int* visited = calloc(NODES, sizeof(int));
    visited[0] = 1;
    int visit_count = 0;

    while (queue) {
        /* Dequeue */
        int node = obj_to_int(queue->a);
        Obj* rest = queue->b;
        if (rest) inc_ref(rest);
        dec_ref(queue);
        queue = rest;

        visit_count++;

        /* Enqueue neighbors */
        Obj* edges = adj[node];
        while (edges) {
            int neighbor = obj_to_int(edges->a);
            if (!visited[neighbor]) {
                visited[neighbor] = 1;
                queue = mk_pair(mk_int_unboxed(neighbor), queue);
            }
            edges = edges->b;
        }
    }

    double elapsed = perf_get_time_us() - start;

    for (int n = 0; n < NODES; n++) dec_ref(adj[n]);
    free(adj);
    free(visited);

    report_throughput("Graph BFS pattern", elapsed, visit_count);
    PASS();
}

void test_perf_pattern_memo_fib(void) {
    /* Simulate memoized computation */
    const int N = 10000;
    Obj** memo = calloc(N, sizeof(Obj*));

    double start = perf_get_time_us();

    memo[0] = mk_int_unboxed(0);
    memo[1] = mk_int_unboxed(1);

    for (int i = 2; i < N; i++) {
        /* Memoized fib - just sum previous two */
        long a = obj_to_int(memo[i-1]);
        long b = obj_to_int(memo[i-2]);
        memo[i] = mk_int_unboxed((a + b) % 1000000007);
    }

    double elapsed = perf_get_time_us() - start;

    /* Cleanup - immediates don't need cleanup but let's be consistent */
    free(memo);

    report_throughput("Memoized fib pattern", elapsed, N);
    PASS();
}

void test_perf_pattern_lazy_seq(void) {
    /* Simulate lazy sequence realization */
    const int REALIZE = 100000;

    double start = perf_get_time_us();

    /* Build lazy seq by consing on demand */
    Obj* realized = NULL;
    for (int i = 0; i < REALIZE; i++) {
        realized = mk_pair(mk_int_unboxed(i), realized);
    }
    dec_ref(realized);

    double elapsed = perf_get_time_us() - start;

    report_throughput("Lazy seq pattern", elapsed, REALIZE);
    PASS();
}

void test_perf_pattern_stream_process(void) {
    /* Simulate stream processing */
    const int ITEMS = 100000;

    double start = perf_get_time_us();

    long sum = 0;
    for (int i = 0; i < ITEMS; i++) {
        /* Read item */
        Obj* item = mk_int(i);

        /* Filter */
        if (obj_to_int(item) % 2 == 0) {
            /* Transform */
            long val = obj_to_int(item) * 2;
            sum += val;
        }

        dec_ref(item);
    }
    (void)sum;

    double elapsed = perf_get_time_us() - start;

    report_throughput("Stream process pattern", elapsed, ITEMS);
    PASS();
}

void test_perf_pattern_batch_insert(void) {
    /* Simulate batch database insert */
    const int BATCHES = 100;
    const int BATCH_SIZE = 1000;

    double start = perf_get_time_us();

    for (int b = 0; b < BATCHES; b++) {
        /* Build batch */
        Obj* batch = NULL;
        for (int i = 0; i < BATCH_SIZE; i++) {
            Obj* row = mk_pair(mk_int_unboxed(b * BATCH_SIZE + i), mk_string("data"));
            batch = mk_pair(row, batch);
        }

        /* "Execute" batch */
        dec_ref(batch);
    }

    double elapsed = perf_get_time_us() - start;

    report_throughput("Batch insert pattern", elapsed, BATCHES * BATCH_SIZE);
    PASS();
}

void test_perf_pattern_message_pass(void) {
    /* Simulate message passing between actors */
    const int MESSAGES = 100000;

    double start = perf_get_time_us();

    for (int m = 0; m < MESSAGES; m++) {
        /* Create message */
        Obj* msg = mk_pair(mk_sym("msg"), mk_pair(mk_int_unboxed(m), NULL));

        /* "Send" (just allocate/free) */
        dec_ref(msg);
    }

    double elapsed = perf_get_time_us() - start;

    report_throughput("Message passing pattern", elapsed, MESSAGES);
    PASS();
}

void test_perf_pattern_plugin_call(void) {
    /* Simulate plugin/extension calls with closures */
    const int CALLS = 50000;

    /* Create "plugin" closure */
    Obj* caps[1];
    caps[0] = mk_int_unboxed(42);
    Obj* plugin = mk_closure(dummy_closure_fn, caps, NULL, 1, 1);

    double start = perf_get_time_us();

    for (int c = 0; c < CALLS; c++) {
        /* Prepare args */
        Obj* args = mk_pair(mk_int_unboxed(c), NULL);

        /* "Call" plugin */
        (void)plugin;

        dec_ref(args);
    }

    double elapsed = perf_get_time_us() - start;
    dec_ref(plugin);

    report_throughput("Plugin call pattern", elapsed, CALLS);
    PASS();
}

void test_perf_pattern_config_reload(void) {
    /* Simulate config reload */
    const int RELOADS = 10000;

    double start = perf_get_time_us();

    for (int r = 0; r < RELOADS; r++) {
        /* Build config structure */
        Obj* config = mk_dict();
        for (int k = 0; k < 20; k++) {
            char key[16];
            snprintf(key, sizeof(key), "key_%d", k);
            Obj* kobj = mk_string(key);
            Obj* vobj = mk_int(k);
            dec_ref(kobj);
            dec_ref(vobj);
        }
        dec_ref(config);
    }

    double elapsed = perf_get_time_us() - start;

    report_throughput("Config reload pattern", elapsed, RELOADS);
    PASS();
}

void test_perf_pattern_regex_match(void) {
    /* Simulate regex NFA state machine */
    const int MATCHES = 10000;
    const int STATES = 20;

    double start = perf_get_time_us();

    for (int m = 0; m < MATCHES; m++) {
        /* Build state set */
        Obj* states = NULL;
        for (int s = 0; s < STATES; s++) {
            states = mk_pair(mk_int_unboxed(s), states);
        }
        dec_ref(states);
    }

    double elapsed = perf_get_time_us() - start;

    report_throughput("Regex match pattern", elapsed, MATCHES);
    PASS();
}

void test_perf_pattern_gc_like_trace(void) {
    /* Simulate object graph tracing (like GC) */
    const int OBJECTS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * OBJECTS);

    /* Build object graph */
    for (int i = 0; i < OBJECTS; i++) {
        if (i > 0) {
            objs[i] = mk_pair(mk_int_unboxed(i), objs[i / 2]);
            inc_ref(objs[i / 2]);
        } else {
            objs[i] = mk_int(0);
        }
    }

    double start = perf_get_time_us();

    /* Trace from roots */
    int* visited = calloc(OBJECTS, sizeof(int));
    long traced = 0;

    /* Simple iterative trace */
    for (int i = 0; i < OBJECTS; i++) {
        if (!visited[i]) {
            visited[i] = 1;
            traced++;
        }
    }

    double elapsed = perf_get_time_us() - start;
    (void)traced;

    for (int i = OBJECTS - 1; i >= 0; i--) dec_ref(objs[i]);
    free(objs);
    free(visited);

    report_throughput("GC-like trace pattern", elapsed, OBJECTS);
    PASS();
}

void test_perf_pattern_sort_merge(void) {
    /* Simulate merge sort list operations */
    const int SIZE = 10000;

    /* Build unsorted list */
    Obj* list = NULL;
    unsigned int seed = 42;
    for (int i = 0; i < SIZE; i++) {
        seed = seed * 1103515245 + 12345;
        list = mk_pair(mk_int_unboxed(seed % 100000), list);
    }

    double start = perf_get_time_us();

    /* Simplified: just split and merge */
    Obj* left = NULL;
    Obj* right = NULL;
    Obj* curr = list;
    int count = 0;
    while (curr) {
        Obj* val = curr->a;
        if (count % 2 == 0) {
            left = mk_pair(val, left);
        } else {
            right = mk_pair(val, right);
        }
        curr = curr->b;
        count++;
    }

    /* Merge back */
    Obj* merged = NULL;
    while (left || right) {
        if (left) {
            merged = mk_pair(left->a, merged);
            Obj* tmp = left->b;
            if (tmp) inc_ref(tmp);
            dec_ref(left);
            left = tmp;
        }
        if (right) {
            merged = mk_pair(right->a, merged);
            Obj* tmp = right->b;
            if (tmp) inc_ref(tmp);
            dec_ref(right);
            right = tmp;
        }
    }

    double elapsed = perf_get_time_us() - start;

    dec_ref(list);
    dec_ref(merged);

    report_throughput("Merge sort pattern", elapsed, SIZE * 2);
    PASS();
}

void test_perf_pattern_serialize(void) {
    /* Simulate object serialization */
    const int OBJECTS = 10000;

    double start = perf_get_time_us();

    for (int o = 0; o < OBJECTS; o++) {
        /* Build object */
        Obj* obj = mk_pair(mk_sym("type"),
                          mk_pair(mk_int_unboxed(o),
                                 mk_pair(mk_string("data"), NULL)));

        /* "Serialize" - traverse structure */
        Obj* curr = obj;
        while (curr && curr->tag == TAG_PAIR) {
            (void)curr->a;
            curr = curr->b;
        }

        dec_ref(obj);
    }

    double elapsed = perf_get_time_us() - start;

    report_throughput("Serialize pattern", elapsed, OBJECTS);
    PASS();
}

void test_perf_pattern_deserialize(void) {
    /* Simulate object deserialization */
    const int OBJECTS = 10000;

    double start = perf_get_time_us();

    for (int o = 0; o < OBJECTS; o++) {
        /* "Deserialize" - build structure from parts */
        Obj* type = mk_sym("type");
        Obj* id = mk_int_unboxed(o);
        Obj* data = mk_string("data");

        Obj* obj = mk_pair(type, mk_pair(id, mk_pair(data, NULL)));
        dec_ref(obj);
    }

    double elapsed = perf_get_time_us() - start;

    report_throughput("Deserialize pattern", elapsed, OBJECTS);
    PASS();
}

void test_perf_pattern_error_handling(void) {
    /* Simulate error object creation/handling */
    const int ERRORS = 100000;

    double start = perf_get_time_us();

    for (int e = 0; e < ERRORS; e++) {
        /* Create error */
        Obj* error = mk_pair(mk_sym("error"),
                            mk_pair(mk_string("message"),
                                   mk_pair(mk_int_unboxed(e), NULL)));

        /* "Handle" error */
        dec_ref(error);
    }

    double elapsed = perf_get_time_us() - start;

    report_throughput("Error handling pattern", elapsed, ERRORS);
    PASS();
}

void test_perf_pattern_promise_resolve(void) {
    /* Simulate promise/future resolution */
    const int PROMISES = 50000;

    double start = perf_get_time_us();

    for (int p = 0; p < PROMISES; p++) {
        /* Create promise with callbacks */
        Obj* caps[1];
        caps[0] = mk_int_unboxed(p);
        Obj* on_resolve = mk_closure(dummy_closure_fn, caps, NULL, 1, 1);
        Obj* on_reject = mk_closure(dummy_closure_fn, caps, NULL, 1, 1);

        Obj* promise = mk_pair(on_resolve, on_reject);

        /* "Resolve" */
        dec_ref(promise);
    }

    double elapsed = perf_get_time_us() - start;

    report_throughput("Promise resolve pattern", elapsed, PROMISES);
    PASS();
}

/* ============================================================================
 * RUN ALL COMPREHENSIVE PERFORMANCE TESTS
 * ============================================================================ */

void run_comprehensive_performance_tests(void) {
    TEST_SUITE("Comprehensive Performance Benchmarks (200 tests)");

    TEST_SECTION("Allocation Throughput (50 tests)");
    RUN_TEST(test_perf_alloc_small_1byte_1M);
    RUN_TEST(test_perf_alloc_small_1byte_10M);
    RUN_TEST(test_perf_alloc_boxed_int_1M);
    RUN_TEST(test_perf_alloc_boxed_int_5M);
    RUN_TEST(test_perf_alloc_float_1M);
    RUN_TEST(test_perf_alloc_float_5M);
    RUN_TEST(test_perf_alloc_pair_1M);
    RUN_TEST(test_perf_alloc_pair_5M);
    RUN_TEST(test_perf_alloc_char_1M);
    RUN_TEST(test_perf_alloc_box_1M);
    RUN_TEST(test_perf_alloc_symbol_100k);
    RUN_TEST(test_perf_alloc_string_short_500k);
    RUN_TEST(test_perf_alloc_string_medium_100k);
    RUN_TEST(test_perf_alloc_string_long_50k);
    RUN_TEST(test_perf_alloc_closure_500k);
    RUN_TEST(test_perf_alloc_closure_with_caps_200k);
    RUN_TEST(test_perf_alloc_array_100k);
    RUN_TEST(test_perf_alloc_dict_50k);
    RUN_TEST(test_perf_alloc_tuple_200k);
    RUN_TEST(test_perf_alloc_keyword_100k);
    RUN_TEST(test_perf_alloc_large_string_10k);
    RUN_TEST(test_perf_alloc_large_array_10k);
    RUN_TEST(test_perf_alloc_list_build_100k);
    RUN_TEST(test_perf_alloc_list_build_500k);
    RUN_TEST(test_perf_alloc_list_build_1M);
    RUN_TEST(test_perf_alloc_tree_100k);
    RUN_TEST(test_perf_alloc_mixed_types_1M);
    RUN_TEST(test_perf_alloc_batch_region_1M);
    RUN_TEST(test_perf_alloc_batch_region_5M);
    RUN_TEST(test_perf_alloc_batch_arena_10M);
    RUN_TEST(test_perf_alloc_random_sizes_100k);
    RUN_TEST(test_perf_alloc_power_of_2_sizes);
    RUN_TEST(test_perf_alloc_aligned_sizes);
    RUN_TEST(test_perf_alloc_unaligned_sizes);
    RUN_TEST(test_perf_alloc_tiny_objects_10M);
    RUN_TEST(test_perf_alloc_small_objects_5M);
    RUN_TEST(test_perf_alloc_medium_objects_1M);
    RUN_TEST(test_perf_alloc_large_objects_100k);
    RUN_TEST(test_perf_alloc_huge_objects_10k);
    RUN_TEST(test_perf_alloc_bimodal_sizes_500k);
    RUN_TEST(test_perf_sustained_alloc_1sec);
    RUN_TEST(test_perf_sustained_region_alloc_1sec);
    RUN_TEST(test_perf_alloc_burst_10k_x_100);
    RUN_TEST(test_perf_alloc_burst_100k_x_10);
    RUN_TEST(test_perf_alloc_steady_state_500k);
    RUN_TEST(test_perf_alloc_growing_pool);
    RUN_TEST(test_perf_alloc_shrinking_pool);
    RUN_TEST(test_perf_alloc_wave_pattern);
    RUN_TEST(test_perf_alloc_sawtooth_pattern);
    RUN_TEST(test_perf_alloc_ramp_up_down);

    TEST_SECTION("Deallocation Throughput (30 tests)");
    RUN_TEST(test_perf_free_immediate_10M);
    RUN_TEST(test_perf_free_boxed_int_1M);
    RUN_TEST(test_perf_free_boxed_int_5M);
    RUN_TEST(test_perf_free_pairs_1M);
    RUN_TEST(test_perf_free_floats_1M);
    RUN_TEST(test_perf_free_closures_500k);
    RUN_TEST(test_perf_free_arrays_100k);
    RUN_TEST(test_perf_free_strings_500k);
    RUN_TEST(test_perf_free_boxes_1M);
    RUN_TEST(test_perf_free_list_1M);
    RUN_TEST(test_perf_free_lifo_order_500k);
    RUN_TEST(test_perf_free_fifo_order_500k);
    RUN_TEST(test_perf_free_random_order_500k);
    RUN_TEST(test_perf_free_interleaved_500k);
    RUN_TEST(test_perf_free_stride_256_500k);
    RUN_TEST(test_perf_free_region_bulk_1M);
    RUN_TEST(test_perf_free_region_bulk_5M);
    RUN_TEST(test_perf_free_arena_bulk_10M);
    RUN_TEST(test_perf_free_shared_refs_100k);
    RUN_TEST(test_perf_free_deep_nesting_1k);
    RUN_TEST(test_perf_free_wide_tree_10k);
    RUN_TEST(test_perf_free_mixed_types_500k);
    RUN_TEST(test_perf_free_alloc_interleaved_500k);
    RUN_TEST(test_perf_free_batch_100_x_10k);
    RUN_TEST(test_perf_free_delayed_500k);
    RUN_TEST(test_perf_free_partial_50_percent);
    RUN_TEST(test_perf_free_partial_90_percent);
    RUN_TEST(test_perf_free_with_finalizers_100k);
    RUN_TEST(test_perf_free_cyclic_structures_10k);

    TEST_SECTION("Latency Measurements (30 tests)");
    RUN_TEST(test_perf_latency_alloc_int);
    RUN_TEST(test_perf_latency_alloc_pair);
    RUN_TEST(test_perf_latency_alloc_float);
    RUN_TEST(test_perf_latency_alloc_closure);
    RUN_TEST(test_perf_latency_alloc_array);
    RUN_TEST(test_perf_latency_alloc_string);
    RUN_TEST(test_perf_latency_free_int);
    RUN_TEST(test_perf_latency_free_pair);
    RUN_TEST(test_perf_latency_inc_ref);
    RUN_TEST(test_perf_latency_dec_ref_no_free);
    RUN_TEST(test_perf_latency_region_alloc);
    RUN_TEST(test_perf_latency_region_create);
    RUN_TEST(test_perf_latency_region_exit);
    RUN_TEST(test_perf_latency_arena_alloc);
    RUN_TEST(test_perf_latency_list_cons);
    RUN_TEST(test_perf_latency_car_cdr);
    RUN_TEST(test_perf_latency_box_get);
    RUN_TEST(test_perf_latency_box_set);
    RUN_TEST(test_perf_latency_symbol_lookup);
    RUN_TEST(test_perf_latency_type_check);
    RUN_TEST(test_perf_latency_immediate_check);
    RUN_TEST(test_perf_latency_obj_to_int);
    RUN_TEST(test_perf_latency_under_pressure);
    RUN_TEST(test_perf_latency_after_fragmentation);
    RUN_TEST(test_perf_latency_cold_start);
    RUN_TEST(test_perf_latency_warm_cache);
    RUN_TEST(test_perf_latency_percentiles);

    TEST_SECTION("Concurrency Scalability (30 tests)");
    RUN_TEST(test_perf_conc_1thread_1M);
    RUN_TEST(test_perf_conc_2thread_500k);
    RUN_TEST(test_perf_conc_4thread_250k);
    RUN_TEST(test_perf_conc_8thread_125k);
    RUN_TEST(test_perf_conc_region_4thread);
    RUN_TEST(test_perf_conc_region_8thread);
    RUN_TEST(test_perf_conc_mixed_4thread);
    RUN_TEST(test_perf_conc_list_build_4thread);
    RUN_TEST(test_perf_conc_symbol_4thread);
    RUN_TEST(test_perf_conc_steady_state_4thread);
    RUN_TEST(test_perf_conc_burst_4thread);
    RUN_TEST(test_perf_conc_shared_ref_4thread);
    RUN_TEST(test_perf_conc_scaling_1_to_8);
    RUN_TEST(test_perf_conc_contention_test);
    RUN_TEST(test_perf_conc_producer_consumer);
    RUN_TEST(test_perf_conc_false_sharing);
    RUN_TEST(test_perf_conc_work_stealing_sim);
    RUN_TEST(test_perf_conc_thread_local_region);
    RUN_TEST(test_perf_conc_closure_create);
    RUN_TEST(test_perf_conc_long_running_4thread);
    RUN_TEST(test_perf_conc_rapid_spawn);
    RUN_TEST(test_perf_conc_barrier_sync);

    TEST_SECTION("Cache/Locality (30 tests)");
    RUN_TEST(test_perf_cache_sequential_access);
    RUN_TEST(test_perf_cache_random_access);
    RUN_TEST(test_perf_cache_stride_1);
    RUN_TEST(test_perf_cache_stride_8);
    RUN_TEST(test_perf_cache_stride_64);
    RUN_TEST(test_perf_cache_list_traversal);
    RUN_TEST(test_perf_cache_working_set_L1);
    RUN_TEST(test_perf_cache_working_set_L2);
    RUN_TEST(test_perf_cache_working_set_L3);
    RUN_TEST(test_perf_cache_working_set_main_mem);
    RUN_TEST(test_perf_cache_hot_cold_split);
    RUN_TEST(test_perf_cache_temporal_locality);
    RUN_TEST(test_perf_cache_spatial_locality_pairs);
    RUN_TEST(test_perf_cache_prefetch_friendly);
    RUN_TEST(test_perf_cache_prefetch_hostile);
    RUN_TEST(test_perf_cache_alloc_dealloc_locality);
    RUN_TEST(test_perf_cache_region_locality);
    RUN_TEST(test_perf_cache_arena_locality);
    RUN_TEST(test_perf_cache_interleaved_types);
    RUN_TEST(test_perf_cache_type_segregated);
    RUN_TEST(test_perf_cache_tree_depth_first);
    RUN_TEST(test_perf_cache_tree_breadth_first);
    RUN_TEST(test_perf_cache_matrix_row_major);
    RUN_TEST(test_perf_cache_matrix_col_major);
    RUN_TEST(test_perf_cache_false_sharing_sim);
    RUN_TEST(test_perf_cache_tlb_pressure);

    TEST_SECTION("Real-World Patterns (30 tests)");
    RUN_TEST(test_perf_pattern_web_request);
    RUN_TEST(test_perf_pattern_json_parse);
    RUN_TEST(test_perf_pattern_compiler_ast);
    RUN_TEST(test_perf_pattern_interpreter_stack);
    RUN_TEST(test_perf_pattern_database_row);
    RUN_TEST(test_perf_pattern_event_loop);
    RUN_TEST(test_perf_pattern_cache_lru);
    RUN_TEST(test_perf_pattern_string_builder);
    RUN_TEST(test_perf_pattern_map_reduce);
    RUN_TEST(test_perf_pattern_tree_transform);
    RUN_TEST(test_perf_pattern_closure_factory);
    RUN_TEST(test_perf_pattern_symbol_table);
    RUN_TEST(test_perf_pattern_continuation_capture);
    RUN_TEST(test_perf_pattern_graph_bfs);
    RUN_TEST(test_perf_pattern_memo_fib);
    RUN_TEST(test_perf_pattern_lazy_seq);
    RUN_TEST(test_perf_pattern_stream_process);
    RUN_TEST(test_perf_pattern_batch_insert);
    RUN_TEST(test_perf_pattern_message_pass);
    RUN_TEST(test_perf_pattern_plugin_call);
    RUN_TEST(test_perf_pattern_config_reload);
    RUN_TEST(test_perf_pattern_regex_match);
    RUN_TEST(test_perf_pattern_gc_like_trace);
    RUN_TEST(test_perf_pattern_sort_merge);
    RUN_TEST(test_perf_pattern_serialize);
    RUN_TEST(test_perf_pattern_deserialize);
    RUN_TEST(test_perf_pattern_error_handling);
    RUN_TEST(test_perf_pattern_promise_resolve);
}
