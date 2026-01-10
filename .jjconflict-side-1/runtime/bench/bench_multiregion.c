/*
 * bench_multiregion.c - Multi-Region Benchmarks
 *
 * Benchmarks for multi-region scenarios and inter-region pointers.
 */

#include "rcg_bench_framework.h"


/* ========== Many Independent Regions ========== */

void bench_many_independent_regions(void) {
    const int count = g_config.size_small;
    BenchResult result = bench_start("many_independent_regions", count);

    Region* regions[count];
    for (int i = 0; i < count; i++) {
        regions[i] = region_create();
        /* Allocate some data */
        for (int j = 0; j < 10; j++) {
            mk_int_region(regions[i], j);
        }
    }

    /* Cleanup */
    for (int i = 0; i < count; i++) {
        region_exit(regions[i]);
    }

    bench_end(&result, count);
    bench_report(&result);
}

/* ========== Region Dependency Chain ========== */

void bench_region_dependency_chain(void) {
    const int depth = g_config.size_small;
    BenchResult result = bench_start("region_dependency_chain", depth);

    Region* regions[depth];
    for (int i = 0; i < depth; i++) {
        regions[i] = region_create();
    }

    /* Create dependencies: r[i] holds reference to r[i+1] */
    for (int i = 0; i < depth - 1; i++) {
        /* Create an object in regions[i] that references regions[i+1] */
        Obj* obj = mk_int_region(regions[i], i);
        (void)obj;
        /* Note: In real code, this would be a RegionRef to regions[i+1] */
    }

    /* Cleanup */
    for (int i = 0; i < depth; i++) {
        region_exit(regions[i]);
    }

    bench_end(&result, depth);
    bench_report(&result);
}

/* ========== Region Hierarchy (Tree of Regions) ========== */

void bench_region_hierarchy(void) {
    const int branching = 10;
    const int depth = 5;
    BenchResult result = bench_start("region_hierarchy", branching * depth);

    Region* root = region_create();
    Region** current_level = malloc(branching * sizeof(Region*));

    /* Create hierarchy */
    for (int level = 0; level < depth; level++) {
        for (int i = 0; i < branching; i++) {
            current_level[i] = region_create();
            /* Allocate some data */
            mk_int_region(current_level[i], level * branching + i);
        }
        /* Next level would reference these */
    }

    /* Cleanup */
    for (int i = 0; i < branching; i++) {
        region_exit(current_level[i]);
    }
    region_exit(root);
    free(current_level);

    bench_end(&result, branching * depth);
    bench_report(&result);
}

/* ========== Inter-Region Object Sharing ========== */

void bench_inter_region_sharing(void) {
    const int num_regions = 100;
    const int shared_objects = g_config.size_small;
    BenchResult result = bench_start("inter_region_sharing", num_regions * shared_objects);

    Region* shared_region = region_create();
    Obj** shared_objs = malloc(shared_objects * sizeof(Obj*));

    /* Create shared objects */
    for (int i = 0; i < shared_objects; i++) {
        shared_objs[i] = mk_int_region(shared_region, i);
    }

    /* Multiple regions reference shared objects */
    for (int r = 0; r < num_regions; r++) {
        Region* region = region_create();
        /* Reference some shared objects */
        for (int i = 0; i < 10; i++) {
            Obj* local = mk_cell_region(region, shared_objs[i % shared_objects], NULL);
            (void)local;
        }
        region_exit(region);
    }

    free(shared_objs);
    region_exit(shared_region);
    bench_end(&result, num_regions * shared_objects);
    bench_report(&result);
}

/* ========== Region Splitting ========== */

void bench_region_splitting(void) {
    const int count = g_config.size_medium;
    BenchResult result = bench_start("region_splitting", count);

    Region* parent = region_create();
    Region* child1 = region_create();
    Region* child2 = region_create();

    /* Distribute allocations across regions */
    for (int i = 0; i < count; i++) {
        switch (i % 3) {
            case 0:
                mk_int_region(parent, i);
                break;
            case 1:
                mk_int_region(child1, i);
                break;
            case 2:
                mk_int_region(child2, i);
                break;
        }
    }

    region_exit(child2);
    region_exit(child1);
    region_exit(parent);

    bench_end(&result, count);
    bench_report(&result);
}

/* ========== Cross-Region Function Calls ========== */

void bench_cross_region_calls(void) {
    const int iterations = g_config.size_small;
    BenchResult result = bench_start("cross_region_calls", iterations);

    Region* r1 = region_create();
    Region* r2 = region_create();

    /* Simulate function calls that create/destroy regions */
    for (int i = 0; i < iterations; i++) {
        Region* temp = region_create();
        Obj* obj = mk_int_region(temp, i);

        /* "Return" value to caller region */
        Obj* result_obj = mk_cell_region(r1, obj, NULL);
        (void)result_obj;

        region_exit(temp);
    }

    region_exit(r2);
    region_exit(r1);
    bench_end(&result, iterations);
    bench_report(&result);
}

/* ========== Scoped Region Lifetime ========== */

void bench_scoped_regions(void) {
    const int iterations = g_config.size_small;
    BenchResult result = bench_start("scoped_regions", iterations);

    for (int i = 0; i < iterations; i++) {
        Region* scope = region_create();
        /* Do work in scope */
        for (int j = 0; j < 10; j++) {
            mk_int_region(scope, j);
        }
        /* Scope ends */
        region_exit(scope);
    }

    bench_end(&result, iterations);
    bench_report(&result);
}

/* ========== Run All Multi-Region Benchmarks ========== */

int main_multiregion(int argc, char** argv) {
    bench_parse_args(argc, argv);
    csv_header();

    printf("=== RC-G Multi-Region Benchmarks ===\n");
    printf("Iterations: %d\n\n", g_config.iterations);

    BENCH_SECTION("Independent Regions");
    RUN_BENCH(bench_many_independent_regions);

    BENCH_SECTION("Region Dependencies");
    RUN_BENCH(bench_region_dependency_chain);
    RUN_BENCH(bench_region_hierarchy);

    BENCH_SECTION("Object Sharing");
    RUN_BENCH(bench_inter_region_sharing);

    BENCH_SECTION("Region Splitting");
    RUN_BENCH(bench_region_splitting);

    BENCH_SECTION("Cross-Region Operations");
    RUN_BENCH(bench_cross_region_calls);
    RUN_BENCH(bench_scoped_regions);

    printf("\n=== All multi-region benchmarks complete ===\n");
    return 0;
}
