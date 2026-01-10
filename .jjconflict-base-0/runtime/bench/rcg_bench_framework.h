/*
 * rcg_bench_framework.h - RC-G (Region Control Block) Benchmark Framework
 *
 * This framework provides utilities for benchmarking the Region-based
 * Reference Counting memory management system.
 *
 * Key aspects of RC-G to benchmark:
 * - Region allocation (Arena bump pointer)
 * - Region reference counting (external_rc)
 * - Tethering (thread-safe borrowing)
 * - Transmigration (moving objects between regions)
 * - Multi-region interactions
 */

#ifndef RCG_BENCH_FRAMEWORK_H
#define RCG_BENCH_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#include <pthread.h>
#include <stdint.h>

/* Include the actual RC-G memory system */
#include "../src/memory/region_core.h"
#include "../src/memory/region_value.h"
#include "../include/omni.h"

/* ========== Timing Helper ========== */

static inline double get_time_sec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

/* ========== Benchmark Configuration ========== */

typedef struct {
    int size_small;     /* 1K operations */
    int size_medium;    /* 100K operations */
    int size_large;     /* 10M operations */
    int iterations;     /* Benchmark iterations */
    int verbose;        /* Verbose output */
    int csv_output;     /* Also output CSV format */
    int num_threads;    /* For concurrent benchmarks */
} BenchConfig;

/* Declare g_config as extern - defined in bench_runner.c */
extern BenchConfig g_config;

#define BENCH_DEFAULT_CONFIG { 1000, 100000, 10000000, 100, 0, 0, 4 }

/* ========== Timing Utilities ========== */

typedef struct {
    double wall_start;
    double cpu_start;
    double wall_time;
    double cpu_time;
    long operations;
    const char* name;
    int size;
} BenchResult;

static inline BenchResult bench_start(const char* name, int size) {
    BenchResult result = {0};
    result.name = name;
    result.size = size;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    result.wall_start = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;

    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    result.cpu_start = (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec / 1000000.0
                     + (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec / 1000000.0;

    return result;
}

static inline void bench_end(BenchResult* result, long operations) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    result->wall_time = (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0 - result->wall_start;

    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    double cpu_end = (double)usage.ru_utime.tv_sec + (double)usage.ru_utime.tv_usec / 1000000.0
                   + (double)usage.ru_stime.tv_sec + (double)usage.ru_stime.tv_usec / 1000000.0;
    result->cpu_time = cpu_end - result->cpu_start;

    result->operations = operations;
}

static inline void bench_report(const BenchResult* result) {
    double ops_per_sec = (result->operations / result->wall_time);
    double ns_per_op = (result->wall_time * 1e9) / result->operations;

    printf("    %-45s [%8d] %8.3fs (%10.0f ops/sec, %8.2f ns/op) [CPU: %.3fs]\n",
           result->name, result->size, result->wall_time, ops_per_sec, ns_per_op, result->cpu_time);

    if (g_config.csv_output) {
        printf("CSV: %s, %d, %ld, %.6f, %.6f, %.0f, %.2f\n",
               result->name, result->size, result->operations, result->wall_time, result->cpu_time,
               ops_per_sec, ns_per_op);
    }
}

/* ========== Memory Statistics (External) ========== */

typedef struct {
    size_t peak_rss;
    size_t current_rss;
    size_t vms;
} MemStats;

static inline MemStats mem_stats_read(void) {
    MemStats stats = {0};
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return stats;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            unsigned long rss;
            if (sscanf(line, "VmRSS: %lu kB", &rss) == 1) {
                stats.current_rss = rss * 1024;
            }
        } else if (strncmp(line, "VmPeak:", 7) == 0) {
            unsigned long peak;
            if (sscanf(line, "VmPeak: %lu kB", &peak) == 1) {
                stats.peak_rss = peak * 1024;
            }
        } else if (strncmp(line, "VmSize:", 7) == 0) {
            unsigned long vms;
            if (sscanf(line, "VmSize: %lu kB", &vms) == 1) {
                stats.vms = vms * 1024;
            }
        }
    }
    fclose(f);
    return stats;
}

static inline void mem_stats_report(const char* label, const MemStats* before, const MemStats* after) {
    if (g_config.verbose) {
        size_t delta_rss = after->current_rss - before->current_rss;
        printf("      MEM [%s]: RSS: %.2f MB (Δ%.2f MB), Peak: %.2f MB\n",
               label,
               after->current_rss / (1024.0 * 1024.0),
               delta_rss / (1024.0 * 1024.0),
               after->peak_rss / (1024.0 * 1024.0));
    }
}

/* ========== Region-Specific Utilities ========== */

/* Helper to build a list in a region */
static inline Obj* build_list_region(Region* r, int n) {
    Obj* list = mk_int_region(r, 0);  /* Sentinel */
    for (int i = 1; i < n; i++) {
        list = mk_cell_region(r, mk_int_region(r, i), list);
    }
    return list;
}

/* Helper to build a binary tree in a region */
static inline Obj* build_tree_region(Region* r, int depth) {
    if (depth <= 0) {
        return mk_int_region(r, 1);  /* Leaf */
    }
    Obj* left = build_tree_region(r, depth - 1);
    Obj* right = build_tree_region(r, depth - 1);
    return mk_cell_region(r, left, right);
}

/* Helper to build a wide tree in a region */
static inline Obj* build_wide_tree_region(Region* r, int branching, int depth) {
    if (depth <= 0) {
        return mk_int_region(r, 1);
    }

    /* Build first child */
    Obj* current = mk_cell_region(r, mk_int_region(r, 0), NULL);

    /* Build remaining children and link them */
    Obj* first = current;
    for (int i = 1; i < branching; i++) {
        Obj* child = build_wide_tree_region(r, branching, depth - 1);
        current->b = mk_cell_region(r, child, NULL);
        current = (Obj*)current->b;
    }

    return first;
}

/* ========== Benchmark Sections ========== */

#define BENCH_SECTION(name) \
    do { \
        printf("\n=== %s ===\n", name); \
    } while(0)

#define BENCH_SUBSECTION(name) \
    do { \
        printf("\n--- %s ---\n", name); \
    } while(0)

/* ========== Benchmark Macros ========== */

#define RUN_BENCH(fn) \
    do { \
        if (g_config.verbose) printf("  Running: %s\n", #fn); \
        fn(); \
    } while(0)

/* ========== Command Line Parsing ========== */

static inline void bench_parse_args(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--small") == 0) {
            g_config.size_small = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--medium") == 0) {
            g_config.size_medium = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--large") == 0) {
            g_config.size_large = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--iterations") == 0) {
            g_config.iterations = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--verbose") == 0) {
            g_config.verbose = 1;
        } else if (strcmp(argv[i], "--csv") == 0) {
            g_config.csv_output = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --small N       Set small size (default: 1000)\n");
            printf("  --medium N      Set medium size (default: 100000)\n");
            printf("  --large N       Set large size (default: 10000000)\n");
            printf("  --iterations N  Set iterations (default: 100)\n");
            printf("  --verbose       Enable verbose output\n");
            printf("  --csv           Enable CSV output\n");
            printf("  --help          Show this message\n");
            exit(0);
        }
    }
}

/* ========== CSV Header ========== */

static inline void csv_header(void) {
    if (g_config.csv_output) {
        printf("benchmark,size,operations,wall_time,cpu_time,ops_per_sec,ns_per_op\n");
    }
}

#endif /* RCG_BENCH_FRAMEWORK_H */
