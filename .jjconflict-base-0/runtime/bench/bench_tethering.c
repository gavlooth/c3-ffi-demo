/*
 * bench_tethering.c - Tethering Benchmarks
 *
 * Benchmarks for region tethering (thread-safe borrowing).
 */

#include "rcg_bench_framework.h"


/* ========== Basic Tethering Overhead ========== */

void bench_tether_single(void) {
    const int count = g_config.size_large;
    BenchResult result = bench_start("tether_single", count);

    Region* r = region_create();
    for (int i = 0; i < count; i++) {
        region_tether_start(r);
        region_tether_end(r);
    }
    region_exit(r);

    bench_end(&result, count);
    bench_report(&result);
}

void bench_tether_nested(void) {
    const int depth = 100;
    const int iterations = g_config.size_small;
    BenchResult result = bench_start("tether_nested", depth * iterations);

    Region* r = region_create();
    for (int iter = 0; iter < iterations; iter++) {
        /* Nested tethers */
        for (int i = 0; i < depth; i++) {
            region_tether_start(r);
        }
        for (int i = 0; i < depth; i++) {
            region_tether_end(r);
        }
    }
    region_exit(r);

    bench_end(&result, depth * iterations);
    bench_report(&result);
}

/* ========== Tethering During Allocation ========== */

void bench_tether_with_allocation(void) {
    const int count = g_config.size_medium;
    BenchResult result = bench_start("tether_with_allocation", count);

    Region* r = region_create();
    region_tether_start(r);

    for (int i = 0; i < count; i++) {
        mk_int_region(r, i);
    }

    region_tether_end(r);
    region_exit(r);

    bench_end(&result, count);
    bench_report(&result);
}

/* ========== Multi-Threaded Tethering ========== */

static void* tether_thread_worker(void* arg) {
    Region* r = (Region*)arg;
    int count = 10000;

    for (int i = 0; i < count; i++) {
        region_tether_start(r);
        /* Simulate some work */
        volatile int x = i * i;
        (void)x;
        region_tether_end(r);
    }

    return NULL;
}

void bench_tether_concurrent(void) {
    const int num_threads = 4;
    const int ops_per_thread = 10000;
    BenchResult result = bench_start("tether_concurrent", num_threads * ops_per_thread);

    Region* r = region_create();
    pthread_t threads[num_threads];

    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, tether_thread_worker, r);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    region_exit(r);
    bench_end(&result, num_threads * ops_per_thread);
    bench_report(&result);
}

/* ========== Tethering Prevents Early Free ========== */

void bench_tether_prevents_free(void) {
    const int iterations = g_config.size_small;
    BenchResult result = bench_start("tether_prevents_free", iterations);

    for (int i = 0; i < iterations; i++) {
        Region* r = region_create();
        Obj* obj = mk_int_region(r, i);

        /* Tether keeps region alive even if scope would normally free it */
        region_tether_start(r);

        /* In real code, scope might end here but tether prevents cleanup */
        (void)obj;

        region_tether_end(r);
        region_exit(r);
    }

    bench_end(&result, iterations);
    bench_report(&result);
}

/* ========== Tether Cache Effectiveness ========== */

void bench_tether_cache_hit(void) {
    const int count = g_config.size_large;
    BenchResult result = bench_start("tether_cache_hit", count);

    Region* r = region_create();

    /* Same thread tethering multiple times */
    for (int i = 0; i < count; i++) {
        region_tether_start(r);
        region_tether_start(r);  /* Should hit cache */
        region_tether_end(r);
        region_tether_end(r);
    }

    region_exit(r);
    bench_end(&result, count);
    bench_report(&result);
}

/* ========== Run All Tethering Benchmarks ========== */

int main_tethering(int argc, char** argv) {
    bench_parse_args(argc, argv);
    csv_header();

    printf("=== RC-G Tethering Benchmarks ===\n");
    printf("Iterations: %d\n\n", g_config.iterations);

    BENCH_SECTION("Basic Tethering");
    RUN_BENCH(bench_tether_single);
    RUN_BENCH(bench_tether_nested);

    BENCH_SECTION("Tethering with Work");
    RUN_BENCH(bench_tether_with_allocation);

    BENCH_SECTION("Concurrency");
    RUN_BENCH(bench_tether_concurrent);

    BENCH_SECTION("Tethering Semantics");
    RUN_BENCH(bench_tether_prevents_free);
    RUN_BENCH(bench_tether_cache_hit);

    printf("\n=== All tethering benchmarks complete ===\n");
    return 0;
}
