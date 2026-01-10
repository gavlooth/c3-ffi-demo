/*
 * bench_stress.c - RC-G Stress Tests
 *
 * Stress tests for the Region Control Block memory management system.
 */

#include "rcg_bench_framework.h"


/* ========== Deep Nesting Stress ========== */

void bench_stress_deep_nesting(void) {
    const int depth = g_config.size_medium;
    BenchResult result = bench_start("stress_deep_nesting", depth);

    Region* r = region_create();
    Obj* nested = NULL;

    /* Build a very deep structure */
    for (int i = 0; i < depth; i++) {
        nested = mk_cell_region(r, mk_int_region(r, i), nested);
    }

    (void)nested;
    region_exit(r);
    bench_end(&result, depth);
    bench_report(&result);
}

/* ========== Wide Structure Stress ========== */

void bench_stress_wide_structure(void) {
    const int width = g_config.size_small;
    BenchResult result = bench_start("stress_wide_structure", width);

    Region* r = region_create();
    Obj* wide = NULL;
    Obj** last = &wide;

    /* Build a very wide structure (many siblings) */
    for (int i = 0; i < width; i++) {
        *last = mk_cell_region(r, mk_int_region(r, i), NULL);
        last = &((*last)->b);
    }

    (void)wide;
    region_exit(r);
    bench_end(&result, width);
    bench_report(&result);
}

/* ========== Memory Pressure Waves ========== */

void bench_stress_memory_waves(void) {
    const int waves = g_config.size_small;
    const int per_wave = g_config.size_small;
    BenchResult result = bench_start("stress_memory_waves", waves * per_wave);

    for (int w = 0; w < waves; w++) {
        Region* r = region_create();

        /* Allocate a wave of objects */
        for (int i = 0; i < per_wave; i++) {
            mk_int_region(r, i);
        }

        /* Free the wave */
        region_exit(r);
    }

    bench_end(&result, waves * per_wave);
    bench_report(&result);
}

/* ========== Many Tiny Regions ========== */

void bench_stress_many_tiny_regions(void) {
    const int count = g_config.size_medium;
    BenchResult result = bench_start("stress_many_tiny_regions", count);

    Region** regions = malloc(count * sizeof(Region*));

    /* Create many tiny regions */
    for (int i = 0; i < count; i++) {
        regions[i] = region_create();
        mk_int_region(regions[i], i);  /* Single object */
    }

    /* Cleanup */
    for (int i = 0; i < count; i++) {
        region_exit(regions[i]);
    }
    free(regions);

    bench_end(&result, count);
    bench_report(&result);
}

/* ========== Mixed Allocation Pattern ========== */

void bench_stress_mixed_pattern(void) {
    const int iterations = g_config.size_small;
    BenchResult result = bench_start("stress_mixed_pattern", iterations);

    for (int iter = 0; iter < iterations; iter++) {
        Region* r = region_create();

        /* Mix of different allocation types */
        for (int i = 0; i < 100; i++) {
            /* Integers */
            mk_int_region(r, i);

            /* Floats */
            mk_float_region(r, (double)i);

            /* Pairs */
            mk_cell_region(r, mk_int_region(r, i), mk_int_region(r, i + 1));

            /* Boxes */
            mk_box_region(r, mk_int_region(r, i));
        }

        region_exit(r);
    }

    bench_end(&result, iterations);
    bench_report(&result);
}

/* ========== Alternating Create/Destroy ========== */

void bench_stress_alternating_lifetimes(void) {
    const int cycles = g_config.size_small;
    BenchResult result = bench_start("stress_alternating_lifetimes", cycles);

    Region* r1 = region_create();
    Region* r2 = NULL;

    for (int i = 0; i < cycles; i++) {
        if (i % 2 == 0) {
            if (r2) region_exit(r2);
            r2 = region_create();
            mk_int_region(r2, i);
        } else {
            mk_int_region(r1, i);
        }
    }

    region_exit(r1);
    if (r2) region_exit(r2);

    bench_end(&result, cycles);
    bench_report(&result);
}

/* ========== Region Lifecycle Stress ========== */

void bench_stress_region_lifecycle(void) {
    const int iterations = g_config.size_medium;
    BenchResult result = bench_start("stress_region_lifecycle", iterations);

    for (int i = 0; i < iterations; i++) {
        Region* r = region_create();

        /* Allocate some objects */
        for (int j = 0; j < 10; j++) {
            mk_int_region(r, j);
        }

        /* Tether and untether */
        region_tether_start(r);
        region_tether_end(r);

        /* Destroy */
        region_exit(r);
    }

    bench_end(&result, iterations);
    bench_report(&result);
}

/* ========== Transmigration Stress ========== */

void bench_stress_transmigration(void) {
    const int iterations = g_config.size_small;
    const int list_size = 100;
    BenchResult result = bench_start("stress_transmigration", iterations * list_size);

    for (int iter = 0; iter < iterations; iter++) {
        Region* src = region_create();
        Obj* list = build_list_region(src, list_size);

        /* Transmigrate multiple times */
        for (int i = 0; i < 10; i++) {
            Region* dest = region_create();
            list = transmigrate(list, src, dest);
            region_exit(src);
            src = dest;
        }

        region_exit(src);
    }

    bench_end(&result, iterations * list_size);
    bench_report(&result);
}

/* ========== Run All Stress Tests ========== */

int main_stress(int argc, char** argv) {
    bench_parse_args(argc, argv);
    csv_header();

    printf("=== RC-G Stress Tests ===\n");
    printf("Iterations: %d\n\n", g_config.iterations);

    BENCH_SECTION("Deep Nesting");
    RUN_BENCH(bench_stress_deep_nesting);

    BENCH_SECTION("Wide Structures");
    RUN_BENCH(bench_stress_wide_structure);

    BENCH_SECTION("Memory Pressure");
    RUN_BENCH(bench_stress_memory_waves);
    RUN_BENCH(bench_stress_many_tiny_regions);

    BENCH_SECTION("Mixed Patterns");
    RUN_BENCH(bench_stress_mixed_pattern);
    RUN_BENCH(bench_stress_alternating_lifetimes);

    BENCH_SECTION("Lifecycle Stress");
    RUN_BENCH(bench_stress_region_lifecycle);

    BENCH_SECTION("Transmigration Stress");
    RUN_BENCH(bench_stress_transmigration);

    printf("\n=== All stress tests complete ===\n");
    return 0;
}
