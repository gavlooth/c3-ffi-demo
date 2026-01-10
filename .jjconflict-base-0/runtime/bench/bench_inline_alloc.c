/*
 * bench_inline_alloc.c - Inline Allocation Benchmark
 *
 * Benchmarks the T-opt-inline-allocation optimization.
 * Compares small object allocation speed with raw malloc.
 */

#include "rcg_bench_framework.h"

/* ========== Small Object Allocation Benchmark ========== */

void bench_inline_alloc_small_objects(void) {
    const int count = g_config.size_small;  // 1000 small objects
    BenchResult result = bench_start("inline_alloc_small_objects", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        // Allocate many small objects (Obj is ~32 bytes, < 64 byte threshold)
        for (int j = 0; j < count; j++) {
            Obj* o = region_alloc(r, sizeof(Obj));
            o->tag = TAG_INT;
            o->i = j;
            (void)o;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

/* ========== Comparison with Raw Malloc ========== */

void bench_inline_alloc_vs_malloc(void) {
    const int count = g_config.size_small;
    double inline_time, malloc_time;

    // Inline allocation version
    double start = get_time_sec();
    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();
        for (int j = 0; j < count; j++) {
            Obj* o = region_alloc(r, sizeof(Obj));
            o->tag = TAG_INT;
            o->i = j;
            (void)o;
        }
        region_exit(r);
    }
    inline_time = get_time_sec() - start;

    // Raw malloc version (for comparison)
    start = get_time_sec();
    for (int i = 0; i < g_config.iterations; i++) {
        Obj** objs = malloc(count * sizeof(Obj*));
        for (int j = 0; j < count; j++) {
            objs[j] = malloc(sizeof(Obj));
            objs[j]->tag = TAG_INT;
            objs[j]->i = j;
        }
        // Free all
        for (int j = 0; j < count; j++) {
            free(objs[j]);
        }
        free(objs);
    }
    malloc_time = get_time_sec() - start;

    printf("    inline_alloc_vs_malloc:\n");
    printf("      Inline (region_alloc): %.6fs for %d iterations\n", inline_time, g_config.iterations);
    printf("      Raw malloc:            %.6fs for %d iterations\n", malloc_time, g_config.iterations);
    printf("      Speedup:               %.2fx\n", malloc_time / inline_time);
    printf("      Parity:                %s\n",
           (inline_time <= malloc_time * 1.1) ? "ACHIEVED (<10% overhead)" : "NOT ACHIEVED");
}

/* ========== Mixed Size Allocation ========== */

void bench_inline_alloc_mixed_sizes(void) {
    const int count = 1000;
    BenchResult result = bench_start("inline_alloc_mixed_sizes", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        // Mix of small and medium allocations
        for (int j = 0; j < count; j++) {
            if (j % 3 == 0) {
                // Small: 32 bytes (uses inline buffer)
                void* p = region_alloc(r, 32);
                (void)p;
            } else if (j % 3 == 1) {
                // Medium: 64 bytes (uses inline buffer, threshold)
                void* p = region_alloc(r, 64);
                (void)p;
            } else {
                // Large: 128 bytes (uses arena, fallback)
                void* p = region_alloc(r, 128);
                (void)p;
            }
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

/* ========== Inline Buffer Exhaustion Test ========== */

void bench_inline_alloc_exhaustion(void) {
    const int obj_size = 32;
    const int inline_buf_size = REGION_INLINE_BUF_SIZE;
    const int count = (inline_buf_size / obj_size) * 2;  // Allocate 2x buffer capacity
    BenchResult result = bench_start("inline_alloc_exhaustion", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        // First half fits in inline buffer, second half falls back to arena
        for (int j = 0; j < count; j++) {
            void* p = region_alloc(r, obj_size);
            (void)p;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

/* ========== Closure Allocation (Common Case) ========== */

void bench_inline_alloc_closures(void) {
    const int count = 100;
    BenchResult result = bench_start("inline_alloc_closures", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        // Allocate closures (32 bytes, fits in inline buffer)
        for (int j = 0; j < count; j++) {
            Closure* c = region_alloc(r, sizeof(Closure));
            c->fn = NULL;
            c->captures = NULL;
            c->capture_count = 0;
            c->arity = 1;
            c->name = "test";
            (void)c;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

/* ========== Run All Inline Allocation Benchmarks ========== */

int main_inline_alloc(int argc, char** argv) {
    bench_parse_args(argc, argv);
    csv_header();

    printf("=== Inline Allocation Benchmarks (T-opt-inline-allocation) ===\n");
    printf("Inline buffer size: %d bytes\n", REGION_INLINE_BUF_SIZE);
    printf("Max inline alloc: %d bytes\n", REGION_INLINE_MAX_ALLOC);
    printf("Iterations: %d\n\n", g_config.iterations);

    BENCH_SECTION("Small Object Allocation");
    RUN_BENCH(bench_inline_alloc_small_objects);

    BENCH_SECTION("Comparison with Raw Malloc");
    RUN_BENCH(bench_inline_alloc_vs_malloc);

    BENCH_SECTION("Mixed Size Allocation");
    RUN_BENCH(bench_inline_alloc_mixed_sizes);

    BENCH_SECTION("Inline Buffer Exhaustion");
    RUN_BENCH(bench_inline_alloc_exhaustion);

    BENCH_SECTION("Closure Allocation");
    RUN_BENCH(bench_inline_alloc_closures);

    printf("\n=== All inline allocation benchmarks complete ===\n");
    return 0;
}
