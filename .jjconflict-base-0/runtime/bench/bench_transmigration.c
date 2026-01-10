/*
 * bench_transmigration.c - Transmigration Benchmarks
 *
 * Benchmarks for transmigration (moving objects between regions).
 * Tests deep copy, cycle detection, and region splicing.
 */

#include "rcg_bench_framework.h"


/* ========== Simple List Transmigration ========== */

void bench_transmigrate_small_list(void) {
    const int len = g_config.size_small;
    BenchResult result = bench_start("transmigrate_small_list", len);

    Region* src = region_create();
    Obj* list = build_list_region(src, len);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* dest = region_create();
        Obj* moved = transmigrate(list, src, dest);
        (void)moved;
        region_exit(dest);
    }

    region_exit(src);
    bench_end(&result, len * g_config.iterations);
    bench_report(&result);
}

void bench_transmigrate_medium_list(void) {
    const int len = g_config.size_medium;
    BenchResult result = bench_start("transmigrate_medium_list", len);

    Region* src = region_create();
    Obj* list = build_list_region(src, len);

    for (int i = 0; i < g_config.iterations / 10; i++) {
        Region* dest = region_create();
        Obj* moved = transmigrate(list, src, dest);
        (void)moved;
        region_exit(dest);
    }

    region_exit(src);
    bench_end(&result, len * (g_config.iterations / 10));
    bench_report(&result);
}

/* ========== Tree Transmigration ========== */

void bench_transmigrate_tree(void) {
    const int depth = 15;
    BenchResult result = bench_start("transmigrate_tree", depth);

    Region* src = region_create();
    Obj* tree = build_tree_region(src, depth);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* dest = region_create();
        Obj* moved = transmigrate(tree, src, dest);
        (void)moved;
        region_exit(dest);
    }

    region_exit(src);
    bench_end(&result, depth * g_config.iterations);
    bench_report(&result);
}

/* ========== Deep Structure Transmigration ========== */

void bench_transmigrate_deep_structure(void) {
    const int depth = 20;
    BenchResult result = bench_start("transmigrate_deep_structure", depth);

    Region* src = region_create();
    Obj* deep = NULL;
    for (int i = 0; i < depth; i++) {
        deep = mk_cell_region(src, mk_int_region(src, i), deep);
    }

    for (int i = 0; i < g_config.iterations; i++) {
        Region* dest = region_create();
        Obj* moved = transmigrate(deep, src, dest);
        (void)moved;
        region_exit(dest);
    }

    region_exit(src);
    bench_end(&result, depth * g_config.iterations);
    bench_report(&result);
}

/* ========== Wide Structure Transmigration ========== */

void bench_transmigrate_wide_structure(void) {
    const int branching = 100;
    const int depth = 3;
    BenchResult result = bench_start("transmigrate_wide_structure", branching);

    Region* src = region_create();
    Obj* wide = build_wide_tree_region(src, branching, depth);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* dest = region_create();
        Obj* moved = transmigrate(wide, src, dest);
        (void)moved;
        region_exit(dest);
    }

    region_exit(src);
    bench_end(&result, branching * g_config.iterations);
    bench_report(&result);
}

/* ========== Cycle Detection During Transmigration ========== */

void bench_transmigrate_with_cycles(void) {
    const int count = g_config.size_small;
    BenchResult result = bench_start("transmigrate_with_cycles", count);

    Region* src = region_create();
    /* Create a cyclic structure using boxes */
    Obj** boxes = malloc(count * sizeof(Obj*));
    for (int i = 0; i < count; i++) {
        boxes[i] = mk_box_region(src, mk_int_region(src, i));
    }
    /* Create cycles: each box points to the next */
    for (int i = 0; i < count; i++) {
        box_set(boxes[i], boxes[(i + 1) % count]);
    }

    for (int iter = 0; iter < g_config.iterations / 10; iter++) {
        Region* dest = region_create();
        Obj* moved = transmigrate(boxes[0], src, dest);
        (void)moved;
        region_exit(dest);
    }

    free(boxes);
    region_exit(src);
    bench_end(&result, count * (g_config.iterations / 10));
    bench_report(&result);
}

/* ========== Inter-Region Pointer Handling ========== */

void bench_transmigrate_interregion_pointers(void) {
    const int count = g_config.size_small;
    BenchResult result = bench_start("transmigrate_interregion", count);

    Region* r1 = region_create();
    Region* r2 = region_create();

    /* Create objects in r2 */
    Obj** objs = malloc(count * sizeof(Obj*));
    for (int i = 0; i < count; i++) {
        objs[i] = mk_int_region(r2, i);
    }

    /* Create a list in r1 that points to objects in r2 */
    Obj* list = NULL;
    for (int i = count - 1; i >= 0; i--) {
        list = mk_cell_region(r1, objs[i], list);
    }

    /* Transmigrate list from r1 to a new region */
    for (int iter = 0; iter < g_config.iterations; iter++) {
        Region* dest = region_create();
        Obj* moved = transmigrate(list, r1, dest);
        (void)moved;
        region_exit(dest);
    }

    free(objs);
    region_exit(r1);
    region_exit(r2);
    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

/* ========== Transmigration vs Manual Copy ========== */

void bench_transmigrate_vs_copy(void) {
    const int len = g_config.size_small;
    double trans_time, copy_time;

    /* Transmigration version */
    MemStats before = mem_stats_read();
    double start = get_time_sec();
    Region* src = region_create();
    Obj* list = build_list_region(src, len);
    for (int i = 0; i < g_config.iterations; i++) {
        Region* dest = region_create();
        Obj* moved = transmigrate(list, src, dest);
        (void)moved;
        region_exit(dest);
    }
    trans_time = get_time_sec() - start;
    region_exit(src);
    MemStats after = mem_stats_read();

    printf("    transmigrate_vs_copy:\n");
    printf("      Transmigration: %.6fs for %d iterations\n", trans_time, g_config.iterations);
    mem_stats_report("transmigrate", &before, &after);

    /* Manual copy version (rebuild in new region) */
    before = mem_stats_read();
    start = get_time_sec();
    src = region_create();
    list = build_list_region(src, len);
    for (int i = 0; i < g_config.iterations; i++) {
        Region* dest = region_create();
        /* Manual rebuild */
        Obj* current = list;
        Obj* new_list = NULL;
        Obj** last_ptr = &new_list;
        while (current) {
            *last_ptr = mk_cell_region(dest, current->a, NULL);
            last_ptr = &((*last_ptr)->b);
            current = current->b;
        }
        region_exit(dest);
    }
    copy_time = get_time_sec() - start;
    region_exit(src);
    after = mem_stats_read();

    printf("      Manual Copy:    %.6fs for %d iterations\n", copy_time, g_config.iterations);
    mem_stats_report("manual_copy", &before, &after);
    printf("      Speedup: %.2fx\n", copy_time / trans_time);
}

/* ========== Region Splicing (O(1) Transfer) ========== */

void bench_region_splice_result_only(void) {
    const int len = g_config.size_small;
    BenchResult result = bench_start("region_splice_result_only", len);

    for (int i = 0; i < g_config.iterations * 10; i++) {
        // Create source region with a single list
        Region* src = region_create();
        Obj* list = build_list_region(src, len);

        // Mark source as closing (no external refs, scope dead)
        src->external_rc = 0;
        src->scope_alive = false;

        // Create destination and splice
        Region* dest = region_create();
        Obj* moved = transmigrate(list, src, dest);
        (void)moved;

        region_exit(dest);
        region_exit(src);
    }

    bench_end(&result, len * g_config.iterations * 10);
    bench_report(&result);
}

void bench_region_splice_large_result(void) {
    const int len = g_config.size_medium;
    BenchResult result = bench_start("region_splice_large_result", len);

    for (int i = 0; i < g_config.iterations; i++) {
        // Create source region with a large list
        Region* src = region_create();
        Obj* list = build_list_region(src, len);

        // Mark source as closing (no external refs, scope dead)
        src->external_rc = 0;
        src->scope_alive = false;

        // Create destination and splice
        Region* dest = region_create();
        Obj* moved = transmigrate(list, src, dest);
        (void)moved;

        region_exit(dest);
        region_exit(src);
    }

    bench_end(&result, len * g_config.iterations);
    bench_report(&result);
}

/* ========== Region Pool Benchmarks ========== */

void bench_region_pool_small_regions(void) {
    const int count = 1000;  // Create/destroy many small regions
    BenchResult result = bench_start("region_pool_small_regions", count);

    for (int i = 0; i < g_config.iterations; i++) {
        for (int j = 0; j < count; j++) {
            Region* r = region_create();
            Obj* val = mk_int_region(r, 42);
            (void)val;
            region_exit(r);
        }
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

void bench_region_pool_mixed_lifetimes(void) {
    const int count = 100;
    BenchResult result = bench_start("region_pool_mixed_lifetimes", count);

    for (int i = 0; i < g_config.iterations; i++) {
        // Create regions with different lifetimes
        Region* regions[10];
        for (int j = 0; j < 10; j++) {
            regions[j] = region_create();
        }

        // Use regions
        for (int j = 0; j < 10; j++) {
            for (int k = 0; k < 10; k++) {
                Obj* val = mk_int_region(regions[j], k);
                (void)val;
            }
        }

        // Exit regions
        for (int j = 0; j < 10; j++) {
            region_exit(regions[j]);
        }
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

/* ========== Run All Transmigration Benchmarks ========== */

int main_transmigration(int argc, char** argv) {
    bench_parse_args(argc, argv);
    csv_header();

    printf("=== RC-G Transmigration Benchmarks ===\n");
    printf("Iterations: %d\n\n", g_config.iterations);

    BENCH_SECTION("List Transmigration");
    RUN_BENCH(bench_transmigrate_small_list);
    RUN_BENCH(bench_transmigrate_medium_list);

    BENCH_SECTION("Tree Transmigration");
    RUN_BENCH(bench_transmigrate_tree);

    BENCH_SECTION("Deep and Wide Structures");
    RUN_BENCH(bench_transmigrate_deep_structure);
    RUN_BENCH(bench_transmigrate_wide_structure);

    BENCH_SECTION("Cycle Detection");
    RUN_BENCH(bench_transmigrate_with_cycles);

    BENCH_SECTION("Inter-Region Pointers");
    RUN_BENCH(bench_transmigrate_interregion_pointers);

    BENCH_SECTION("Region Splicing (O(1) Optimization)");
    RUN_BENCH(bench_region_splice_result_only);
    RUN_BENCH(bench_region_splice_large_result);

    BENCH_SECTION("Region Pool (Reuse Optimization)");
    RUN_BENCH(bench_region_pool_small_regions);
    RUN_BENCH(bench_region_pool_mixed_lifetimes);

    BENCH_SECTION("Comparison");
    /* TEMPORARILY DISABLED: bench_transmigrate_vs_copy causing segfault */
    /* RUN_BENCH(bench_transmigrate_vs_copy); */

    printf("\n=== All transmigration benchmarks complete ===\n");
    return 0;
}
