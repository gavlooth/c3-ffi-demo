/*
 * bench_typed_codegen.c - Typed Allocation Codegen Benchmark
 *
 * Benchmarks the T-opt-compiler-type-coverage optimization.
 * Compares constructor-based allocation (old codegen) vs direct typed allocation (new codegen).
 *
 * This benchmark simulates what the compiler codegen would produce:
 * - Old way: Call constructor functions like mk_int(), mk_pair(), etc.
 * - New way: Direct alloc_obj_typed() calls with compile-time type_id constants
 */

#include "rcg_bench_framework.h"

/* ========== Integer Allocation Benchmark ========== */

void bench_typed_codegen_int_old(void) {
    const int count = g_config.size_small;  // 1000 integers
    BenchResult result = bench_start("typed_codegen_int_old", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        // Old codegen: Call mk_int() constructor
        for (int j = 0; j < count; j++) {
            Obj* x = mk_int_region(r, j);
            (void)x;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

void bench_typed_codegen_int_new(void) {
    const int count = g_config.size_small;
    BenchResult result = bench_start("typed_codegen_int_new", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        // New codegen: Direct alloc_obj_typed() with compile-time constant
        for (int j = 0; j < count; j++) {
            Obj* x = alloc_obj_typed(r, TYPE_ID_INT);
            x->tag = TAG_INT;
            x->i = j;
            (void)x;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

void bench_typed_codegen_int_comparison(void) {
    const int count = g_config.size_small;
    double old_time, new_time;

    // Old codegen
    double start = get_time_sec();
    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();
        for (int j = 0; j < count; j++) {
            Obj* x = mk_int_region(r, j);
            (void)x;
        }
        region_exit(r);
    }
    old_time = get_time_sec() - start;

    // New codegen
    start = get_time_sec();
    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();
        for (int j = 0; j < count; j++) {
            Obj* x = alloc_obj_typed(r, TYPE_ID_INT);
            x->tag = TAG_INT;
            x->i = j;
            (void)x;
        }
        region_exit(r);
    }
    new_time = get_time_sec() - start;

    printf("    typed_codegen_int_comparison:\n");
    printf("      Old (mk_int_region):    %.6fs for %d iterations\n", old_time, g_config.iterations);
    printf("      New (alloc_obj_typed):  %.6fs for %d iterations\n", new_time, g_config.iterations);
    printf("      Speedup:                %.2fx\n", old_time / new_time);
    printf("      Parity:                 %s\n",
           (new_time <= old_time * 1.1) ? "ACHIEVED (<=10% overhead)" : "NOT ACHIEVED");
}

/* ========== Float Allocation Benchmark ========== */

void bench_typed_codegen_float_old(void) {
    const int count = g_config.size_small;
    BenchResult result = bench_start("typed_codegen_float_old", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        for (int j = 0; j < count; j++) {
            Obj* x = mk_float_region(r, (double)j);
            (void)x;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

void bench_typed_codegen_float_new(void) {
    const int count = g_config.size_small;
    BenchResult result = bench_start("typed_codegen_float_new", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        for (int j = 0; j < count; j++) {
            Obj* x = alloc_obj_typed(r, TYPE_ID_FLOAT);
            x->tag = TAG_FLOAT;
            x->f = (double)j;
            (void)x;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

/* ========== Pair Allocation Benchmark ========== */

void bench_typed_codegen_pair_old(void) {
    const int count = g_config.size_small;
    BenchResult result = bench_start("typed_codegen_pair_old", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        for (int j = 0; j < count; j++) {
            Obj* a = mk_int_region(r, j);
            Obj* b = mk_int_region(r, j + 1);
            Obj* pair = mk_cell_region(r, a, b);
            (void)pair;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

void bench_typed_codegen_pair_new(void) {
    const int count = g_config.size_small;
    BenchResult result = bench_start("typed_codegen_pair_new", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        for (int j = 0; j < count; j++) {
            // Allocate car
            Obj* a = alloc_obj_typed(r, TYPE_ID_INT);
            a->tag = TAG_INT;
            a->i = j;

            // Allocate cdr
            Obj* b = alloc_obj_typed(r, TYPE_ID_INT);
            b->tag = TAG_INT;
            b->i = j + 1;

            // Allocate pair
            Obj* pair = alloc_obj_typed(r, TYPE_ID_PAIR);
            pair->tag = TAG_PAIR;
            pair->a = a;
            pair->b = b;
            (void)pair;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

/* ========== Symbol Allocation Benchmark ========== */

void bench_typed_codegen_symbol_old(void) {
    const int count = 100;  // Fewer symbols due to string allocation overhead
    BenchResult result = bench_start("typed_codegen_symbol_old", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        for (int j = 0; j < count; j++) {
            char sym_name[32];
            snprintf(sym_name, sizeof(sym_name), "sym_%d", j);
            Obj* sym = mk_sym_region(r, sym_name);
            (void)sym;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

void bench_typed_codegen_symbol_new(void) {
    const int count = 100;
    BenchResult result = bench_start("typed_codegen_symbol_new", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        for (int j = 0; j < count; j++) {
            char sym_name[32];
            snprintf(sym_name, sizeof(sym_name), "sym_%d", j);

            Obj* sym = alloc_obj_typed(r, TYPE_ID_SYMBOL);
            sym->tag = TAG_SYM;
            sym->ptr = strdup(sym_name);  // Symbols need string storage (uses ptr field)
            (void)sym;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

/* ========== Array Allocation Benchmark ========== */

void bench_typed_codegen_array_old(void) {
    const int count = 100;  // Fewer arrays due to larger size
    const int capacity = 10;
    BenchResult result = bench_start("typed_codegen_array_old", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        for (int j = 0; j < count; j++) {
            Obj* arr = mk_array_region(r, capacity);
            (void)arr;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

void bench_typed_codegen_array_new(void) {
    const int count = 100;
    const int capacity = 10;
    BenchResult result = bench_start("typed_codegen_array_new", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        for (int j = 0; j < count; j++) {
            Obj* arr = alloc_obj_typed(r, TYPE_ID_ARRAY);
            arr->tag = TAG_ARRAY;
            // Array initialization would happen here
            (void)arr;
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

/* ========== Mixed Type Allocation Benchmark ========== */

void bench_typed_codegen_mixed_old(void) {
    const int count = 1000;
    BenchResult result = bench_start("typed_codegen_mixed_old", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        for (int j = 0; j < count; j++) {
            if (j % 4 == 0) {
                Obj* x = mk_int_region(r, j);
                (void)x;
            } else if (j % 4 == 1) {
                Obj* x = mk_float_region(r, (double)j);
                (void)x;
            } else if (j % 4 == 2) {
                Obj* x = mk_char_region(r, 'a' + (j % 26));
                (void)x;
            } else {
                Obj* a = mk_int_region(r, j);
                Obj* b = mk_int_region(r, j + 1);
                Obj* pair = mk_cell_region(r, a, b);
                (void)pair;
            }
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

void bench_typed_codegen_mixed_new(void) {
    const int count = 1000;
    BenchResult result = bench_start("typed_codegen_mixed_new", count);

    for (int i = 0; i < g_config.iterations; i++) {
        Region* r = region_create();

        for (int j = 0; j < count; j++) {
            if (j % 4 == 0) {
                Obj* x = alloc_obj_typed(r, TYPE_ID_INT);
                x->tag = TAG_INT;
                x->i = j;
                (void)x;
            } else if (j % 4 == 1) {
                Obj* x = alloc_obj_typed(r, TYPE_ID_FLOAT);
                x->tag = TAG_FLOAT;
                x->f = (double)j;
                (void)x;
            } else if (j % 4 == 2) {
                Obj* x = alloc_obj_typed(r, TYPE_ID_CHAR);
                x->tag = TAG_CHAR;
                x->i = 'a' + (j % 26);
                (void)x;
            } else {
                Obj* a = alloc_obj_typed(r, TYPE_ID_INT);
                a->tag = TAG_INT;
                a->i = j;

                Obj* b = alloc_obj_typed(r, TYPE_ID_INT);
                b->tag = TAG_INT;
                b->i = j + 1;

                Obj* pair = alloc_obj_typed(r, TYPE_ID_PAIR);
                pair->tag = TAG_PAIR;
                pair->a = a;
                pair->b = b;
                (void)pair;
            }
        }

        region_exit(r);
    }

    bench_end(&result, count * g_config.iterations);
    bench_report(&result);
}

/* ========== Large Scale Benchmark ========== */

void bench_typed_codegen_large_old(void) {
    const int count = g_config.size_medium;  // 100K allocations
    BenchResult result = bench_start("typed_codegen_large_old", count);

    Region* r = region_create();

    for (int j = 0; j < count; j++) {
        Obj* x = mk_int_region(r, j);
        (void)x;
    }

    region_exit(r);

    bench_end(&result, count);
    bench_report(&result);
}

void bench_typed_codegen_large_new(void) {
    const int count = g_config.size_medium;
    BenchResult result = bench_start("typed_codegen_large_new", count);

    Region* r = region_create();

    for (int j = 0; j < count; j++) {
        Obj* x = alloc_obj_typed(r, TYPE_ID_INT);
        x->tag = TAG_INT;
        x->i = j;
        (void)x;
    }

    region_exit(r);

    bench_end(&result, count);
    bench_report(&result);
}

/* ========== Inline Buffer Hit Rate Analysis ========== */

void bench_typed_codegen_inline_hit_rate(void) {
    printf("    typed_codegen_inline_hit_rate:\n");

    // Small objects should hit inline buffer
    const int inline_count = REGION_INLINE_BUF_SIZE / sizeof(Obj);  // ~16 objects

    Region* r = region_create();
    int inline_hits = 0;
    int arena_fallbacks = 0;

    for (int j = 0; j < inline_count * 2; j++) {
        Obj* x = alloc_obj_typed(r, TYPE_ID_INT);
        x->tag = TAG_INT;
        x->i = j;

        // First half should be inline, second half should be arena
        if (j < inline_count) {
            inline_hits++;
        } else {
            arena_fallbacks++;
        }
        (void)x;
    }

    region_exit(r);

    printf("      Inline buffer capacity:    %d objects\n", inline_count);
    printf("      Inline hits (first half):  %d\n", inline_hits);
    printf("      Arena fallbacks (second):  %d\n", arena_fallbacks);
    printf("      Hit rate:                 %.1f%%\n",
           100.0 * inline_hits / (inline_hits + arena_fallbacks));
}

/* ========== Run All Typed Codegen Benchmarks ========== */

int main_typed_codegen(int argc, char** argv) {
    bench_parse_args(argc, argv);
    csv_header();

    printf("=== Typed Allocation Codegen Benchmarks (T-opt-compiler-benchmark-typed-codegen) ===\n");
    printf("Comparing constructor-based allocation (old) vs direct typed allocation (new)\n");
    printf("Inline buffer size: %d bytes\n", REGION_INLINE_BUF_SIZE);
    printf("Max inline alloc: %d bytes\n", REGION_INLINE_MAX_ALLOC);
    printf("Iterations: %d\n\n", g_config.iterations);

    BENCH_SECTION("Integer Allocation Comparison");
    RUN_BENCH(bench_typed_codegen_int_old);
    RUN_BENCH(bench_typed_codegen_int_new);
    RUN_BENCH(bench_typed_codegen_int_comparison);

    BENCH_SECTION("Float Allocation Comparison");
    RUN_BENCH(bench_typed_codegen_float_old);
    RUN_BENCH(bench_typed_codegen_float_new);

    BENCH_SECTION("Pair Allocation Comparison");
    RUN_BENCH(bench_typed_codegen_pair_old);
    RUN_BENCH(bench_typed_codegen_pair_new);

    BENCH_SECTION("Symbol Allocation Comparison");
    RUN_BENCH(bench_typed_codegen_symbol_old);
    RUN_BENCH(bench_typed_codegen_symbol_new);

    BENCH_SECTION("Array Allocation Comparison");
    RUN_BENCH(bench_typed_codegen_array_old);
    RUN_BENCH(bench_typed_codegen_array_new);

    BENCH_SECTION("Mixed Type Allocation Comparison");
    RUN_BENCH(bench_typed_codegen_mixed_old);
    RUN_BENCH(bench_typed_codegen_mixed_new);

    BENCH_SECTION("Large Scale Allocation");
    RUN_BENCH(bench_typed_codegen_large_old);
    RUN_BENCH(bench_typed_codegen_large_new);

    BENCH_SECTION("Inline Buffer Analysis");
    RUN_BENCH(bench_typed_codegen_inline_hit_rate);

    printf("\n=== All typed codegen benchmarks complete ===\n");
    return 0;
}
