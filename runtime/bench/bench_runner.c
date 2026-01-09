/*
 * bench_runner.c - Main RC-G Benchmark Suite Runner
 *
 * Executes all RC-G (Region Control Block) benchmark suites.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rcg_bench_framework.h"

/* Define the global config */
BenchConfig g_config = BENCH_DEFAULT_CONFIG;

/* Benchmark suite function pointers */
typedef int (*bench_suite_fn)(int argc, char** argv);

/* Forward declarations for all benchmark suites */
extern int main_region_alloc(int argc, char** argv);
extern int main_transmigration(int argc, char** argv);
extern int main_tethering(int argc, char** argv);
extern int main_multiregion(int argc, char** argv);
extern int main_stress(int argc, char** argv);
extern int main_complex(int argc, char** argv);
extern int main_inline_alloc(int argc, char** argv);
extern int main_specialized(int argc, char** argv);
extern int main_batched_transmigrate(int argc, char** argv);
extern int main_fat_baseline(int argc, char** argv);
extern int main_typed_codegen(int argc, char** argv);
extern int main_thread_local_rc(int argc, char** argv);

typedef struct {
    const char* name;
    const char* description;
    bench_suite_fn run;
} BenchmarkSuite;

static BenchmarkSuite suites[] = {
    {
        "region_alloc",
        "Region allocation (Arena bump pointer) benchmarks",
        main_region_alloc
    },
    {
        "transmigration",
        "Transmigration (moving objects between regions) benchmarks",
        main_transmigration
    },
    {
        "tethering",
        "Tethering (thread-safe borrowing) benchmarks",
        main_tethering
    },
    {
        "multiregion",
        "Multi-region and inter-region pointer benchmarks",
        main_multiregion
    },
    {
        "stress",
        "Stress tests (deep nesting, memory pressure, etc.)",
        main_stress
    },
    {
        "complex",
        "Complex real-world workload benchmarks",
        main_complex
    },
    {
        "inline_alloc",
        "Inline allocation optimization (T-opt-inline-allocation)",
        main_inline_alloc
    },
    {
        "specialized",
        "Specialized constructors optimization (T-opt-specialized-constructors)",
        main_specialized
    },
    {
        "batched_transmigrate",
        "Batched transmigration optimization (T-opt-transmigrate-batch)",
        main_batched_transmigrate
    },
    {
        "fat_baseline",
        "Fat pointer baseline (establish BEFORE implementing fat pointers)",
        main_fat_baseline
    },
    {
        "typed_codegen",
        "Typed allocation codegen benchmark (T-opt-compiler-benchmark-typed-codegen)",
        main_typed_codegen
    },
    {
        "thread_local_rc",
        "Thread-local RC detection (T-opt-thread-local-rc-detect)",
        main_thread_local_rc
    },
    { NULL, NULL, NULL }
};

static void print_usage(const char* prog_name) {
    printf("Usage: %s [OPTIONS] [SUITE...]\n", prog_name);
    printf("\n");
    printf("RC-G (Region Control Block) Benchmark Suite Runner\n");
    printf("\n");
    printf("Options:\n");
    printf("  --help           Show this help message\n");
    printf("  --list           List all benchmark suites\n");
    printf("  --all            Run all benchmark suites (default)\n");
    printf("  --small N        Set small size (default: 1000)\n");
    printf("  --medium N       Set medium size (default: 100000)\n");
    printf("  --large N        Set large size (default: 10000000)\n");
    printf("  --iterations N   Set iterations (default: 100)\n");
    printf("  --verbose        Enable verbose output\n");
    printf("  --csv            Enable CSV output format\n");
    printf("\n");
    printf("Suites:\n");
    for (int i = 0; suites[i].name != NULL; i++) {
        printf("  %-15s  %s\n", suites[i].name, suites[i].description);
    }
    printf("\n");
    printf("Examples:\n");
    printf("  %s --all\n", prog_name);
    printf("  %s region_alloc transmigration\n", prog_name);
    printf("  %s --verbose --iterations 1000 stress\n", prog_name);
    printf("\n");
}

static void list_suites(void) {
    printf("Available benchmark suites:\n");
    printf("\n");
    for (int i = 0; suites[i].name != NULL; i++) {
        printf("  %-15s  %s\n", suites[i].name, suites[i].description);
    }
}

static int run_suite(BenchmarkSuite* suite, int argc, char** argv) {
    printf("\n");
    printf("========================================\n");
    printf("Running: %s\n", suite->name);
    printf("========================================\n");

    int result = suite->run(argc, argv);

    printf("\n");
    printf("Suite '%s' %s\n", suite->name, result == 0 ? "PASSED" : "FAILED");

    return result;
}

int main(int argc, char** argv) {
    /* Parse command line arguments */
    int run_all = 1;
    int verbose = 0;
    int csv_output = 0;

    /* Collect global options and suite names */
    char* suite_names[10];
    int suite_count = 0;

    /* Parse global options */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--list") == 0) {
            list_suites();
            return 0;
        } else if (strcmp(argv[i], "--all") == 0) {
            run_all = 1;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--csv") == 0) {
            csv_output = 1;
        } else if (strncmp(argv[i], "--", 2) == 0) {
            /* Pass through to suites */
            continue;
        } else {
            /* It's a suite name */
            run_all = 0;
            if (suite_count < 10) {
                suite_names[suite_count++] = argv[i];
            }
        }
    }

    /* Print header */
    printf("╔══════════════════════════════════════════╗\n");
    printf("║     RC-G Benchmark Suite Runner         ║\n");
    printf("║  (Region Control Block Memory Model)    ║\n");
    printf("╚══════════════════════════════════════════╝\n");

    /* Run requested suites */
    int total_result = 0;
    int suites_run = 0;

    if (run_all) {
        /* Run all suites */
        for (int i = 0; suites[i].name != NULL; i++) {
            int result = run_suite(&suites[i], argc, argv);
            if (result != 0) total_result = result;
            suites_run++;
        }
    } else {
        /* Run specific suites */
        for (int s = 0; s < suite_count; s++) {
            int found = 0;
            for (int i = 0; suites[i].name != NULL; i++) {
                if (strcmp(suite_names[s], suites[i].name) == 0) {
                    int result = run_suite(&suites[i], argc, argv);
                    if (result != 0) total_result = result;
                    suites_run++;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                fprintf(stderr, "Unknown benchmark suite: %s\n", suite_names[s]);
                total_result = 1;
            }
        }
    }

    /* Print summary */
    printf("\n");
    printf("========================================\n");
    printf("Benchmark Summary\n");
    printf("========================================\n");
    printf("Suites run: %d\n", suites_run);

    if (total_result == 0) {
        printf("Result: ALL PASSED\n");
    } else {
        printf("Result: SOME FAILED\n");
    }

    printf("========================================\n");

    return total_result;
}
