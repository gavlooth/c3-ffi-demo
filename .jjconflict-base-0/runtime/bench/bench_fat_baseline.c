/*
 * bench_fat_baseline.c - Fat Pointer Baseline Benchmark Suite
 *
 * This benchmark suite establishes a performance baseline BEFORE implementing
 * fat pointers. We'll run the same benchmarks AFTER implementing fat pointers
 * to measure the actual impact.
 *
 * Usage:
 *   ./bench_runner fat_baseline --small 1000 --medium 100000 --verbose --csv
 */

#include "rcg_bench_framework.h"
#include "../src/memory/region_core.h"

/* Maximum size for inline storage (will be relevant with fat pointers) */
#define MAX_INLINE_SIZE 56

/* ========== Fat Pointer Statistics (Placeholder for now) ========== */

typedef struct {
    /* Allocation counts */
    long alloc_total;
    long alloc_small;     /* <= 64 bytes */
    long alloc_medium;    /* 65-256 bytes */
    long alloc_large;     /* > 256 bytes */

    /* Memory usage */
    size_t bytes_used;
    size_t bytes_inline;  /* Will be relevant with fat pointers */
    size_t bytes_heap;

    /* Performance metrics */
    long cache_hits;
    long cache_misses;

    const char* bench_name;
} FatStats;

static FatStats g_fat_stats = {0};

static inline void fat_stats_init(const char* name) {
    memset(&g_fat_stats, 0, sizeof(FatStats));
    g_fat_stats.bench_name = name;
}

static inline void fat_stats_report(void) {
    printf("\n      [Fat Stats: %s]\n", g_fat_stats.bench_name);
    printf("        Allocations: total=%ld small=%ld medium=%ld large=%ld\n",
           g_fat_stats.alloc_total,
           g_fat_stats.alloc_small,
           g_fat_stats.alloc_medium,
           g_fat_stats.alloc_large);
    printf("        Memory: used=%.2f MB heap=%.2f MB\n",
           g_fat_stats.bytes_used / (1024.0 * 1024.0),
           g_fat_stats.bytes_heap / (1024.0 * 1024.0));
    printf("        Cache: hits=%ld misses=%ld (%.2f%% hit rate)\n",
           g_fat_stats.cache_hits,
           g_fat_stats.cache_misses,
           g_fat_stats.cache_hits + g_fat_stats.cache_misses > 0
               ? 100.0 * g_fat_stats.cache_hits / (g_fat_stats.cache_hits + g_fat_stats.cache_misses)
               : 0.0);
}

/* ========== Baseline Benchmark: List Operations ========== */

static void benchmark_list_creation_small(void) {
    fat_stats_init("list_creation_small");

    BenchResult result = bench_start("List creation (small)", g_config.size_small);

    Region* r = region_create();
    MemStats mem_before = mem_stats_read();

    Obj* list = NULL;
    for (int i = 0; i < g_config.size_small; i++) {
        list = mk_cell_region(r, mk_int_region(r, i), list);
        g_fat_stats.alloc_total++;
        g_fat_stats.alloc_small++;
        g_fat_stats.bytes_used += sizeof(Obj) + sizeof(long);
    }

    MemStats mem_after = mem_stats_read();
    bench_end(&result, g_config.size_small);

    bench_report(&result);
    mem_stats_report("list_creation_small", &mem_before, &mem_after);
    fat_stats_report();

    region_exit(r);
}

static void benchmark_list_creation_medium(void) {
    fat_stats_init("list_creation_medium");

    BenchResult result = bench_start("List creation (medium)", g_config.size_medium);

    Region* r = region_create();
    MemStats mem_before = mem_stats_read();

    Obj* list = NULL;
    for (int i = 0; i < g_config.size_medium; i++) {
        list = mk_cell_region(r, mk_int_region(r, i), list);
        g_fat_stats.alloc_total++;
        g_fat_stats.alloc_small++;
    }

    MemStats mem_after = mem_stats_read();
    bench_end(&result, g_config.size_medium);

    bench_report(&result);
    mem_stats_report("list_creation_medium", &mem_before, &mem_after);
    fat_stats_report();

    region_exit(r);
}

static void benchmark_list_traversal(void) {
    fat_stats_init("list_traversal");

    /* Create list */
    Region* r = region_create();
    Obj* list = build_list_region(r, g_config.size_medium);

    BenchResult result = bench_start("List traversal", g_config.size_medium);

    /* Traverse list multiple times */
    long sum = 0;
    for (int iter = 0; iter < 100; iter++) {
        for (Obj* current = list; current != NULL; current = current->b) {
            if (current->a && current->a->tag == TAG_INT) {
                sum += obj_to_int(current->a);
                g_fat_stats.cache_hits++;
            }
        }
    }

    bench_end(&result, g_config.size_medium * 100);
    bench_report(&result);
    fat_stats_report();

    region_exit(r);
}

/* ========== Baseline Benchmark: Tree Operations ========== */

/* Helper function for tree traversal (static to avoid nested function) */
static void traverse_tree(Obj* node, long* sum, FatStats* stats) {
    if (!node) return;

    /* Check tag FIRST to avoid recursing on invalid fields */
    if (node->tag == TAG_INT) {
        *sum += obj_to_int(node);
        stats->cache_hits++;
    } else if (node->tag == TAG_PAIR) {
        /* Only recurse on cell nodes (internal tree nodes) */
        traverse_tree(node->a, sum, stats);
        traverse_tree(node->b, sum, stats);
    }
}

static void benchmark_tree_creation(void) {
    fat_stats_init("tree_creation");

    BenchResult result = bench_start("Tree creation (depth=15)", 1);

    Region* r = region_create();
    MemStats mem_before = mem_stats_read();

    Obj* tree = build_tree_region(r, 15);
    /* Count nodes: 2^16 - 1 = 65535 nodes for depth 15 */
    int node_count = 65535;
    g_fat_stats.alloc_total = node_count;
    g_fat_stats.alloc_small = node_count;
    g_fat_stats.bytes_used = node_count * sizeof(Obj);

    MemStats mem_after = mem_stats_read();
    bench_end(&result, node_count);

    bench_report(&result);
    mem_stats_report("tree_creation", &mem_before, &mem_after);
    fat_stats_report();

    region_exit(r);
}

static void benchmark_tree_traversal(void) {
    fat_stats_init("tree_traversal");

    Region* r = region_create();
    Obj* tree = build_tree_region(r, 12);  /* Smaller for traversal */

    BenchResult result = bench_start("Tree traversal (depth=12)", 1);

    /* Traverse tree (post-order) */
    long sum = 0;
    for (int iter = 0; iter < 100; iter++) {
        traverse_tree(tree, &sum, &g_fat_stats);
    }

    int node_count = 8191;  /* 2^13 - 1 for depth 12 */
    bench_end(&result, node_count * 100);
    bench_report(&result);
    fat_stats_report();

    region_exit(r);
}

/* ========== Baseline Benchmark: Array Operations ========== */

static void benchmark_array_creation(void) {
    fat_stats_init("array_creation");

    BenchResult result = bench_start("Array creation", g_config.size_small);

    Region* r = region_create();
    MemStats mem_before = mem_stats_read();

    /* Create arrays of varying sizes */
    for (int i = 0; i < g_config.size_small / 100; i++) {
        int size = (i % 10) + 1;  /* 1-10 elements */
        Obj* arr = mk_array_region_batch(r, size);
        g_fat_stats.alloc_total++;
        g_fat_stats.alloc_small += (size <= 8) ? 1 : 0;
        g_fat_stats.alloc_medium += (size > 8) ? 1 : 0;
        /* Approximate size calculation */
        g_fat_stats.bytes_used += sizeof(Obj) + (size * sizeof(Obj*));
    }

    MemStats mem_after = mem_stats_read();
    bench_end(&result, g_config.size_small / 100);

    bench_report(&result);
    mem_stats_report("array_creation", &mem_before, &mem_after);
    fat_stats_report();

    region_exit(r);
}

/* ========== Baseline Benchmark: Object Size Distribution ========== */

static void benchmark_size_distribution(void) {
    fat_stats_init("size_distribution");

    printf("\n    [Size Distribution Analysis]\n");

    Region* r = region_create();

    /* Small objects (<= 64 bytes) */
    int small_count = 0;
    for (int i = 0; i < 1000; i++) {
        Obj* small = mk_cell_region(r, mk_int_region(r, i), NULL);
        if (sizeof(Obj) <= 64) small_count++;
        g_fat_stats.alloc_small++;
    }

    /* Medium objects (65-256 bytes) */
    int medium_count = 0;
    for (int i = 0; i < 1000; i++) {
        Obj* medium = mk_array_region_batch(r, 10);
        /* Approximate medium object size check */
        size_t med_size = sizeof(Obj) + (10 * sizeof(Obj*));
        if (med_size > 64 && med_size <= 256) medium_count++;
        g_fat_stats.alloc_medium++;
    }

    /* Large objects (> 256 bytes) */
    int large_count = 0;
    for (int i = 0; i < 100; i++) {
        Obj* large = mk_array_region_batch(r, 100);
        /* Approximate large object size check */
        size_t large_size = sizeof(Obj) + (100 * sizeof(Obj*));
        if (large_size > 256) large_count++;
        g_fat_stats.alloc_large++;
    }

    printf("      Small objects (<=64B):  %d\n", small_count);
    printf("      Medium objects (65-256B): %d\n", medium_count);
    printf("      Large objects (>256B):   %d\n", large_count);

    /* Calculate potential savings with fat pointers */
    size_t current_memory = (small_count + medium_count + large_count) * sizeof(void*);
    size_t fat_pointer_memory = small_count * MAX_INLINE_SIZE;
    size_t potential_savings = current_memory - fat_pointer_memory;

    printf("      Current memory: %.2f KB\n", current_memory / 1024.0);
    printf("      Fat pointer memory: %.2f KB\n", fat_pointer_memory / 1024.0);
    printf("      Potential savings: %.2f KB (%.1f%%)\n",
           potential_savings / 1024.0,
           100.0 * potential_savings / current_memory);

    g_fat_stats.alloc_total = small_count + medium_count + large_count;
    g_fat_stats.bytes_used = current_memory;
    g_fat_stats.bytes_inline = fat_pointer_memory;

    region_exit(r);
}

/* ========== Baseline Benchmark: Memory Pressure ========== */

static void benchmark_memory_pressure(void) {
    fat_stats_init("memory_pressure");

    BenchResult result = bench_start("Memory pressure", g_config.size_medium);

    Region* r = region_create();
    MemStats mem_before = mem_stats_read();

    /* Create and destroy many objects */
    for (int i = 0; i < g_config.size_medium; i++) {
        Obj* obj = mk_cell_region(r, mk_int_region(r, i), NULL);
        /* No explicit free - rely on region cleanup */
        g_fat_stats.alloc_total++;
        g_fat_stats.alloc_small++;
    }

    MemStats mem_after = mem_stats_read();
    bench_end(&result, g_config.size_medium);

    bench_report(&result);
    mem_stats_report("memory_pressure", &mem_before, &mem_after);
    fat_stats_report();

    region_exit(r);
}

/* ========== Baseline Benchmark: Cache Behavior ========== */

static void benchmark_cache_behavior(void) {
    fat_stats_init("cache_behavior");

    printf("\n    [Cache Behavior Analysis]\n");

    Region* r = region_create();

    /* Create linked list (poor cache locality) */
    Obj* list = build_list_region(r, g_config.size_small);

    BenchResult result = bench_start("Cache test (list)", g_config.size_small);

    /* Access list sequentially */
    volatile long sum = 0;
    for (int i = 0; i < 100; i++) {
        for (Obj* current = list; current != NULL; current = current->b) {
            if (current->a && current->a->tag == TAG_INT) {
                sum += obj_to_int(current->a);
                g_fat_stats.cache_hits++;  /* Assume hits for baseline */
            }
        }
    }

    bench_end(&result, g_config.size_small * 100);
    bench_report(&result);

    printf("      Sequential access: %ld cache hits (baseline)\n", g_fat_stats.cache_hits);

    region_exit(r);
}

/* ========== Main Entry Point ========== */

int main_fat_baseline(int argc, char** argv) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  Fat Pointer Baseline Benchmark Suite                       ║\n");
    printf("║  Establish performance baseline BEFORE fat pointers         ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* Parse arguments */
    bench_parse_args(argc, argv);
    csv_header();

    /* Run baseline benchmarks */
    BENCH_SECTION("List Operations Baseline");
    RUN_BENCH(benchmark_list_creation_small);
    RUN_BENCH(benchmark_list_creation_medium);
    RUN_BENCH(benchmark_list_traversal);

    BENCH_SECTION("Tree Operations Baseline");
    RUN_BENCH(benchmark_tree_creation);
    RUN_BENCH(benchmark_tree_traversal);

    BENCH_SECTION("Array Operations Baseline");
    RUN_BENCH(benchmark_array_creation);

    BENCH_SECTION("Size Distribution Analysis");
    RUN_BENCH(benchmark_size_distribution);

    BENCH_SECTION("Memory Pressure Baseline");
    RUN_BENCH(benchmark_memory_pressure);

    BENCH_SECTION("Cache Behavior Baseline");
    RUN_BENCH(benchmark_cache_behavior);

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  Baseline Complete                                           ║\n");
    printf("║  Save this output for comparison with fat pointer results   ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    return 0;
}
