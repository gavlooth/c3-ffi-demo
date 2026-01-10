/*
 * bench_specialized.c - Specialized Constructors Benchmark
 *
 * Benchmarks the T-opt-specialized-constructors optimization.
 * Compares batch allocation (specialized) vs individual allocation.
 */

#include "rcg_bench_framework.h"

/* ========== List Construction Benchmarks ========== */

void bench_specialized_list_small(void) {
    const int n = g_config.size_small;  // 1000 elements
    BenchResult result = bench_start("specialized_list_small", n);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();
        Obj* list = mk_list_region(r, n);
        (void)list;
        region_exit(r);
    }

    bench_end(&result, n * g_config.iterations);
    bench_report(&result);
}

void bench_specialized_list_medium(void) {
    const int n = g_config.size_medium;  // 100000 elements
    BenchResult result = bench_start("specialized_list_medium", n);

    for (int i = 0; i < g_config.iterations / 10; i++) {
        Region* r = region_create();
        Obj* list = mk_list_region(r, n);
        (void)list;
        region_exit(r);
    }

    bench_end(&result, n * (g_config.iterations / 10));
    bench_report(&result);
}

/* ========== Comparison: Specialized vs Individual Allocation ========== */

void bench_specialized_vs_individual_list(void) {
    const int n = g_config.size_small;
    double specialized_time, individual_time;

    // Specialized (single allocation)
    double start = get_time_sec();
    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();
        Obj* list = mk_list_region(r, n);
        (void)list;
        region_exit(r);
    }
    specialized_time = get_time_sec() - start;

    // Individual (n allocations)
    start = get_time_sec();
    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();
        Obj* list = build_list_region(r, n);
        (void)list;
        region_exit(r);
    }
    individual_time = get_time_sec() - start;

    printf("    specialized_vs_individual_list:\n");
    printf("      Specialized (1 alloc):  %.6fs for %d iterations\n", specialized_time, g_config.iterations);
    printf("      Individual (n allocs):  %.6fs for %d iterations\n", individual_time, g_config.iterations);
    printf("      Speedup:               %.2fx\n", individual_time / specialized_time);
    printf("      Allocation reduction:  %dx fewer allocations\n", n);
}

/* ========== Tree Construction Benchmarks ========== */

void bench_specialized_tree_depth_15(void) {
    const int depth = 15;  // 2^16 - 1 = 65535 nodes
    BenchResult result = bench_start("specialized_tree_depth_15", depth);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();
        Obj* tree = mk_tree_region(r, depth);
        (void)tree;
        region_exit(r);
    }

    long nodes = (1L << (depth + 1)) - 1;
    bench_end(&result, nodes * g_config.iterations);
    bench_report(&result);
}

void bench_specialized_vs_individual_tree(void) {
    const int depth = 12;  // 2^13 - 1 = 8191 nodes (smaller for comparison)
    double specialized_time, individual_time;

    // Specialized (single allocation)
    double start = get_time_sec();
    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();
        Obj* tree = mk_tree_region(r, depth);
        (void)tree;
        region_exit(r);
    }
    specialized_time = get_time_sec() - start;

    // Individual (recursive allocations)
    start = get_time_sec();
    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();
        Obj* tree = build_tree_region(r, depth);
        (void)tree;
        region_exit(r);
    }
    individual_time = get_time_sec() - start;

    long nodes = (1L << (depth + 1)) - 1;

    printf("    specialized_vs_individual_tree:\n");
    printf("      Specialized (1 alloc):  %.6fs for %d iterations (%ld nodes)\n",
           specialized_time, g_config.iterations, nodes);
    printf("      Individual (n allocs):  %.6fs for %d iterations\n", individual_time, g_config.iterations);
    printf("      Speedup:               %.2fx\n", individual_time / specialized_time);
    printf("      Allocation reduction:  %ldx fewer allocations\n", nodes);
}

/* ========== Array-to-List Construction ========== */

void bench_specialized_list_from_array(void) {
    const int n = g_config.size_small;
    BenchResult result = bench_start("specialized_list_from_array", n);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        // Create array of values (using immediate integers)
        Obj** values = malloc(n * sizeof(Obj*));
        for (int j = 0; j < n; j++) {
            values[j] = (Obj*)((intptr_t)j << 3 | 0x1);  // Immediate integer
        }

        Obj* list = mk_list_from_array_region(r, values, n);
        (void)list;

        free(values);
        region_exit(r);
    }

    bench_end(&result, n * g_config.iterations);
    bench_report(&result);
}

/* ========== Allocation Count Comparison ========== */

void bench_specialized_allocation_count(void) {
    const int n = 1000;
    const int tree_depth = 10;

    printf("    specialized_allocation_count:\n");
    printf("      List construction (%d elements):\n", n);
    printf("        Individual: %d allocations (n cells + n integers)\n", 2 * n);
    printf("        Specialized: 1 allocation (single block)\n");
    printf("        Reduction:   %dx fewer allocations\n\n", 2 * n);

    long tree_nodes = (1L << (tree_depth + 1)) - 1;
    printf("      Tree construction (depth %d, %ld nodes):\n", tree_depth, tree_nodes);
    printf("        Individual: %ld allocations (recursive)\n", tree_nodes);
    printf("        Specialized: 1 allocation (single block)\n");
    printf("        Reduction:   %ldx fewer allocations\n", tree_nodes);
}

/* ========== Run All Specialized Constructor Benchmarks ========== */

int main_specialized(int argc, char** argv) {
    bench_parse_args(argc, argv);
    csv_header();

    printf("=== Specialized Constructors Benchmarks (T-opt-specialized-constructors) ===\n");
    printf("Iterations: %d\n\n", g_config.iterations);

    BENCH_SECTION("List Construction");
    RUN_BENCH(bench_specialized_list_small);
    RUN_BENCH(bench_specialized_list_medium);

    BENCH_SECTION("Comparison: Specialized vs Individual");
    RUN_BENCH(bench_specialized_vs_individual_list);

    BENCH_SECTION("Tree Construction");
    RUN_BENCH(bench_specialized_tree_depth_15);
    RUN_BENCH(bench_specialized_vs_individual_tree);

    BENCH_SECTION("Array-to-List Construction");
    RUN_BENCH(bench_specialized_list_from_array);

    BENCH_SECTION("Allocation Count Analysis");
    RUN_BENCH(bench_specialized_allocation_count);

    printf("\n=== All specialized constructor benchmarks complete ===\n");
    return 0;
}
