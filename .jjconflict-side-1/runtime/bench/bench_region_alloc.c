/*
 * bench_region_alloc.c - Region Allocation Benchmarks
 *
 * Benchmarks for Region allocation (Arena bump pointer allocator) performance.
 */

#include "rcg_bench_framework.h"


/* ========== Basic Region Allocation ========== */

void bench_region_alloc_small(void) {
    const int count = g_config.size_small;
    BenchResult result = bench_start("region_alloc_small", count);

    Region* r = region_create();
    for (int i = 0; i < count; i++) {
        mk_int_region(r, i);
    }
    region_exit(r);

    bench_end(&result, count);
    bench_report(&result);
}

void bench_region_alloc_medium(void) {
    const int count = g_config.size_medium;
    BenchResult result = bench_start("region_alloc_medium", count);

    Region* r = region_create();
    for (int i = 0; i < count; i++) {
        mk_int_region(r, i);
    }
    region_exit(r);

    bench_end(&result, count);
    bench_report(&result);
}

void bench_region_alloc_large(void) {
    const int count = g_config.size_large;
    BenchResult result = bench_start("region_alloc_large", count);

    Region* r = region_create();
    for (int i = 0; i < count; i++) {
        mk_int_region(r, i);
    }
    region_exit(r);

    bench_end(&result, count);
    bench_report(&result);
}

/* ========== Region vs Heap Comparison ========== */

void bench_heap_alloc_int(void) {
    const int count = g_config.size_medium;
    BenchResult result = bench_start("heap_alloc_int (comparison)", count);

    for (int i = 0; i < count; i++) {
        Obj* o = mk_int(i);
        (void)o;
    }

    bench_end(&result, count);
    bench_report(&result);
}

/* ========== Region Allocation with Complex Types ========== */

void bench_region_alloc_pairs(void) {
    const int count = g_config.size_medium / 2;
    BenchResult result = bench_start("region_alloc_pairs", count);

    Region* r = region_create();
    for (int i = 0; i < count; i++) {
        mk_cell_region(r, mk_int_region(r, i), mk_int_region(r, i + 1));
    }
    region_exit(r);

    bench_end(&result, count);
    bench_report(&result);
}

void bench_region_alloc_lists(void) {
    const int count = g_config.size_small;
    BenchResult result = bench_start("region_alloc_lists", count);

    Region* r = region_create();
    /* Build a list of n elements */
    build_list_region(r, count);
    region_exit(r);

    bench_end(&result, count);
    bench_report(&result);
}

void bench_region_alloc_trees(void) {
    const int depth = 15;  /* 2^15 - 1 nodes */
    BenchResult result = bench_start("region_alloc_binary_tree", depth);

    Region* r = region_create();
    build_tree_region(r, depth);
    region_exit(r);

    /* Calculate approximate node count: 2^depth - 1 */
    long nodes = (1L << depth) - 1;
    bench_end(&result, nodes);
    bench_report(&result);
}

/* ========== Region Reset Performance ========== */

void bench_region_reset(void) {
    const int cycles = g_config.size_small;
    const int per_cycle = g_config.size_small;
    BenchResult result = bench_start("region_reset_cycles", cycles * per_cycle);

    Region* r = region_create();
    for (int i = 0; i < cycles; i++) {
        /* Allocate many objects */
        for (int j = 0; j < per_cycle; j++) {
            mk_int_region(r, j);
        }
        /* Note: Region doesn't have reset(), exit() destroys it */
        /* For this benchmark, we recreate the region */
        region_exit(r);
        r = region_create();
    }
    region_exit(r);

    bench_end(&result, cycles * per_cycle);
    bench_report(&result);
}

/* ========== Many Small Regions ========== */

void bench_many_small_regions(void) {
    const int count = g_config.size_small;
    BenchResult result = bench_start("many_small_regions", count);

    Region* regions[count];
    for (int i = 0; i < count; i++) {
        regions[i] = region_create();
        /* Allocate a small object */
        mk_int_region(regions[i], i);
    }

    /* Clean up */
    for (int i = 0; i < count; i++) {
        region_exit(regions[i]);
    }

    bench_end(&result, count);
    bench_report(&result);
}

/* ========== Region vs Heap for Complex Structures ========== */

void bench_region_vs_heap_pairs(void) {
    const int count = g_config.size_medium;
    double region_time, heap_time;

    /* Region version */
    MemStats before = mem_stats_read();
    double start = get_time_sec();
    Region* r = region_create();
    for (int i = 0; i < count; i++) {
        mk_cell_region(r, mk_int_region(r, i), mk_int_region(r, i + 1));
    }
    region_exit(r);
    region_time = get_time_sec() - start;
    MemStats after = mem_stats_read();

    printf("    region_vs_heap_pairs:\n");
    printf("      Region: %.6fs for %d pairs\n", region_time, count);
    mem_stats_report("region", &before, &after);

    /* Heap version */
    before = mem_stats_read();
    start = get_time_sec();
    for (int i = 0; i < count; i++) {
        Obj* pair = mk_pair(mk_int(i), mk_int(i + 1));
        dec_ref(pair);
    }
    heap_time = get_time_sec() - start;
    after = mem_stats_read();

    printf("      Heap:   %.6fs for %d pairs\n", heap_time, count);
    mem_stats_report("heap", &before, &after);
    printf("      Speedup: %.2fx\n", heap_time / region_time);
}

/* ========== Run All Allocation Benchmarks ========== */

int main_region_alloc(int argc, char** argv) {
    bench_parse_args(argc, argv);
    csv_header();

    printf("=== RC-G Region Allocation Benchmarks ===\n");
    printf("Small: %d, Medium: %d, Large: %d, Iterations: %d\n\n",
           g_config.size_small, g_config.size_medium, g_config.size_large, g_config.iterations);

    BENCH_SECTION("Basic Allocation");
    RUN_BENCH(bench_region_alloc_small);
    RUN_BENCH(bench_region_alloc_medium);
    RUN_BENCH(bench_region_alloc_large);

    BENCH_SECTION("Comparison with Heap");
    RUN_BENCH(bench_heap_alloc_int);

    BENCH_SECTION("Complex Types");
    RUN_BENCH(bench_region_alloc_pairs);
    RUN_BENCH(bench_region_alloc_lists);
    RUN_BENCH(bench_region_alloc_trees);

    BENCH_SECTION("Region Lifecycle");
    RUN_BENCH(bench_region_reset);
    RUN_BENCH(bench_many_small_regions);

    BENCH_SECTION("Region vs Heap Comparison");
    RUN_BENCH(bench_region_vs_heap_pairs);

    printf("\n=== All allocation benchmarks complete ===\n");
    return 0;
}
