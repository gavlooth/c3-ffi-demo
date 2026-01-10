/*
 * bench_batched_transmigrate.c - Batched Transmigration Benchmark
 *
 * Benchmarks the T-opt-transmigrate-batch optimization.
 * Compares standard transmigrate with batched/incremental transmigration.
 */

#include "rcg_bench_framework.h"
#include "../src/memory/transmigrate.h"

/* ========== Comparison: Standard vs Batched Transmigration ========== */

void bench_batched_vs_standard_list(void) {
    const int n = g_config.size_medium;  // 100K elements
    double standard_time, batched_time_10, batched_time_100, batched_time_1000;

    // Standard transmigrate
    double start = get_time_sec();
    for (int i = 0; i < g_config.iterations / 10; i++) {
        Region* src = region_create();
        Obj* list = build_list_region(src, n);
        Region* dest = region_create();
        Obj* moved = transmigrate(list, src, dest);
        (void)moved;
        region_exit(dest);
        region_exit(src);
    }
    standard_time = get_time_sec() - start;

    // Batched transmigrate (chunk size 10)
    start = get_time_sec();
    for (int i = 0; i < g_config.iterations / 10; i++) {
        Region* src = region_create();
        Obj* list = build_list_region(src, n);
        Region* dest = region_create();
        float progress = 0.0f;
        Obj* moved = transmigrate_incremental(list, src, dest, 10, &progress);
        (void)moved;
        region_exit(dest);
        region_exit(src);
    }
    batched_time_10 = get_time_sec() - start;

    // Batched transmigrate (chunk size 100)
    start = get_time_sec();
    for (int i = 0; i < g_config.iterations / 10; i++) {
        Region* src = region_create();
        Obj* list = build_list_region(src, n);
        Region* dest = region_create();
        float progress = 0.0f;
        Obj* moved = transmigrate_incremental(list, src, dest, 100, &progress);
        (void)moved;
        region_exit(dest);
        region_exit(src);
    }
    batched_time_100 = get_time_sec() - start;

    // Batched transmigrate (chunk size 1000)
    start = get_time_sec();
    for (int i = 0; i < g_config.iterations / 10; i++) {
        Region* src = region_create();
        Obj* list = build_list_region(src, n);
        Region* dest = region_create();
        float progress = 0.0f;
        Obj* moved = transmigrate_incremental(list, src, dest, 1000, &progress);
        (void)moved;
        region_exit(dest);
        region_exit(src);
    }
    batched_time_1000 = get_time_sec() - start;

    printf("    batched_vs_standard_list (%d elements):\n", n);
    printf("      Standard (no chunking):        %.6fs for %d iterations\n", standard_time, g_config.iterations / 10);
    printf("      Batched (chunk=10):            %.6fs for %d iterations (%.2fx)\n", batched_time_10, g_config.iterations / 10, standard_time / batched_time_10);
    printf("      Batched (chunk=100):           %.6fs for %d iterations (%.2fx)\n", batched_time_100, g_config.iterations / 10, standard_time / batched_time_100);
    printf("      Batched (chunk=1000):          %.6fs for %d iterations (%.2fx)\n", batched_time_1000, g_config.iterations / 10, standard_time / batched_time_1000);
}

void bench_batched_vs_standard_tree(void) {
    const int depth = 15;  // 65535 nodes
    double standard_time, batched_time_100, batched_time_1000;

    // Standard transmigrate
    double start = get_time_sec();
    for (int i = 0; i < g_config.iterations; i++) {
        Region* src = region_create();
        Obj* tree = build_tree_region(src, depth);
        Region* dest = region_create();
        Obj* moved = transmigrate(tree, src, dest);
        (void)moved;
        region_exit(dest);
        region_exit(src);
    }
    standard_time = get_time_sec() - start;

    // Batched transmigrate (chunk size 100)
    start = get_time_sec();
    for (int i = 0; i < g_config.iterations; i++) {
        Region* src = region_create();
        Obj* tree = build_tree_region(src, depth);
        Region* dest = region_create();
        float progress = 0.0f;
        Obj* moved = transmigrate_incremental(tree, src, dest, 100, &progress);
        (void)moved;
        region_exit(dest);
        region_exit(src);
    }
    batched_time_100 = get_time_sec() - start;

    // Batched transmigrate (chunk size 1000)
    start = get_time_sec();
    for (int i = 0; i < g_config.iterations; i++) {
        Region* src = region_create();
        Obj* tree = build_tree_region(src, depth);
        Region* dest = region_create();
        float progress = 0.0f;
        Obj* moved = transmigrate_incremental(tree, src, dest, 1000, &progress);
        (void)moved;
        region_exit(dest);
        region_exit(src);
    }
    batched_time_1000 = get_time_sec() - start;

    long nodes = (1L << (depth + 1)) - 1;

    printf("    batched_vs_standard_tree (depth %d, %ld nodes):\n", depth, nodes);
    printf("      Standard (no chunking):        %.6fs for %d iterations\n", standard_time, g_config.iterations);
    printf("      Batched (chunk=100):           %.6fs for %d iterations (%.2fx)\n", batched_time_100, g_config.iterations, standard_time / batched_time_100);
    printf("      Batched (chunk=1000):          %.6fs for %d iterations (%.2fx)\n", batched_time_1000, g_config.iterations, standard_time / batched_time_1000);
}

/* ========== Large Graph Benchmark ========== */

void bench_batched_large_graph(void) {
    const int depth = 18;  // ~262K nodes
    BenchResult result = bench_start("batched_large_graph", depth);

    for (int i = 0; i < g_config.iterations / 10; i++) {
        Region* src = region_create();
        Obj* tree = build_tree_region(src, depth);

        Region* dest = region_create();
        float progress = 0.0f;
        Obj* moved = transmigrate_incremental(tree, src, dest, 1000, &progress);
        (void)moved;
        (void)progress;

        region_exit(dest);
        region_exit(src);
    }

    long nodes = (1L << (depth + 1)) - 1;
    bench_end(&result, nodes * (g_config.iterations / 10));
    bench_report(&result);
}

/* ========== Progress Tracking Test ========== */

void bench_batched_progress_tracking(void) {
    const int n = g_config.size_small;
    printf("    batched_progress_tracking:\n");

    Region* src = region_create();
    Obj* list = build_list_region(src, n);

    Region* dest = region_create();
    float progress = 0.0f;
    Obj* moved = transmigrate_incremental(list, src, dest, 100, &progress);

    printf("      Transmigrated %d elements with chunk size 100\n", n);
    printf("      Final progress: %.2f\n", progress);

    (void)moved;
    region_exit(dest);
    region_exit(src);
}

/* ========== Chunk Size Analysis ========== */

void bench_batched_chunk_size_analysis(void) {
    const int n = 10000;
    size_t chunk_sizes[] = {1, 10, 50, 100, 500, 1000, 5000, 0};
    const char* labels[] = {"1", "10", "50", "100", "500", "1000", "5000", "std"};

    printf("    batched_chunk_size_analysis (%d elements):\n", n);

    for (int i = 0; i < 8; i++) {
        double start = get_time_sec();
        int iters = 100;

        for (int j = 0; j < iters; j++) {
            Region* src = region_create();
            Obj* list = build_list_region(src, n);
            Region* dest = region_create();

            if (chunk_sizes[i] == 0) {
                Obj* moved = transmigrate(list, src, dest);
                (void)moved;
            } else {
                float progress = 0.0f;
                Obj* moved = transmigrate_incremental(list, src, dest, chunk_sizes[i], &progress);
                (void)moved;
            }

            region_exit(dest);
            region_exit(src);
        }

        double elapsed = get_time_sec() - start;
        printf("      chunk=%-5s: %.6f for %d iterations (%.2f us/op)\n",
               labels[i], elapsed, iters, (elapsed * 1e6) / iters);
    }
}

/* ========== Run All Batched Transmigration Benchmarks ========== */

int main_batched_transmigrate(int argc, char** argv) {
    bench_parse_args(argc, argv);
    csv_header();

    printf("=== Batched Transmigration Benchmarks (T-opt-transmigrate-batch) ===\n");
    printf("Iterations: %d\n\n", g_config.iterations);

    BENCH_SECTION("Comparison: Standard vs Batched");
    RUN_BENCH(bench_batched_vs_standard_list);
    RUN_BENCH(bench_batched_vs_standard_tree);

    BENCH_SECTION("Large Graph Performance");
    RUN_BENCH(bench_batched_large_graph);

    BENCH_SECTION("Progress Tracking");
    RUN_BENCH(bench_batched_progress_tracking);

    BENCH_SECTION("Chunk Size Analysis");
    RUN_BENCH(bench_batched_chunk_size_analysis);

    printf("\n=== All batched transmigration benchmarks complete ===\n");
    return 0;
}
