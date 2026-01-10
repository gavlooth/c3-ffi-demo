/*
 * Benchmark: Thread-Local RC Detection (T-opt-thread-local-rc-detect)
 *
 * This benchmark measures the performance improvement of using non-atomic
 * RC operations for thread-local regions compared to atomic operations.
 *
 * Expected speedup: 10-50x for thread-local RC operations
 */

#include "../src/memory/region_core.h"
#include "../include/omni.h"
#include <stdio.h>
#include <time.h>
#include <pthread.h>

#define ITERATIONS 10000000  // 10 million iterations

/* Helper: Get time in nanoseconds */
static uint64_t nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Benchmark 1: Thread-local RC (FAST PATH - non-atomic) */
static double bench_thread_local_rc(void) {
    Region* r = region_create();

    uint64_t start = nanos();

    for (int i = 0; i < ITERATIONS; i++) {
        region_retain_internal(r);
        region_release_internal(r);
    }

    uint64_t end = nanos();
    double ns_per_op = (double)(end - start) / (ITERATIONS * 2);

    region_exit(r);

    return ns_per_op;
}

/* Benchmark 2: Atomic RC (SLOW PATH - atomic operations) */
static double bench_atomic_rc(void) {
    Region* r = region_create();

    // Mark region as not thread-local to force atomic operations
    region_mark_external_ref(r);

    uint64_t start = nanos();

    for (int i = 0; i < ITERATIONS; i++) {
        region_retain_internal(r);
        region_release_internal(r);
    }

    uint64_t end = nanos();
    double ns_per_op = (double)(end - start) / (ITERATIONS * 2);

    region_exit(r);

    return ns_per_op;
}

/* Benchmark 3: Pure atomic operations (baseline) */
static double bench_pure_atomic(void) {
    int counter = 0;

    uint64_t start = nanos();

    for (int i = 0; i < ITERATIONS; i++) {
        __atomic_add_fetch(&counter, 1, __ATOMIC_SEQ_CST);
        __atomic_sub_fetch(&counter, 1, __ATOMIC_SEQ_CST);
    }

    uint64_t end = nanos();
    double ns_per_op = (double)(end - start) / (ITERATIONS * 2);

    return ns_per_op;
}

/* Benchmark 4: Pure non-atomic operations (upper bound) */
static double bench_pure_non_atomic(void) {
    volatile int counter = 0;  /* volatile to prevent optimization */

    uint64_t start = nanos();

    for (int i = 0; i < ITERATIONS; i++) {
        counter++;
        counter--;
    }

    uint64_t end = nanos();
    double ns_per_op = (double)(end - start) / (ITERATIONS * 2);

    return ns_per_op;
}

/* Entry point for bench_runner framework */
int main_thread_local_rc(int argc, char** argv) {
    (void)argc;  /* Unused */
    (void)argv;  /* Unused */
    printf("===========================================\n");
    printf("Thread-Local RC Detection Benchmark\n");
    printf("===========================================\n");
    printf("Iterations: %d (2 ops per iteration = %d total ops)\n\n",
           ITERATIONS, ITERATIONS * 2);

    // Run benchmarks
    printf("Running benchmarks...\n");

    double non_atomic_ns = bench_pure_non_atomic();
    printf("  [Baseline] Pure non-atomic:       %.2f ns/op\n", non_atomic_ns);

    double atomic_ns = bench_pure_atomic();
    printf("  [Baseline] Pure atomic:           %.2f ns/op (%.2fx slower than non-atomic)\n",
           atomic_ns, atomic_ns / non_atomic_ns);

    double thread_local_ns = bench_thread_local_rc();
    printf("  [NEW] Thread-local RC:            %.2f ns/op (%.2fx faster than atomic)\n",
           thread_local_ns, atomic_ns / thread_local_ns);

    double shared_ns = bench_atomic_rc();
    printf("  [SLOW] Shared/external RC:        %.2f ns/op (%.2fx slower than thread-local)\n",
           shared_ns, shared_ns / thread_local_ns);

    printf("\n===========================================\n");
    printf("Results Summary:\n");
    printf("===========================================\n");

    double speedup_vs_atomic = atomic_ns / thread_local_ns;
    double overhead_vs_pure = thread_local_ns / non_atomic_ns;

    printf("Speedup vs atomic operations: %.2fx\n", speedup_vs_atomic);
    printf("Overhead vs pure non-atomic: %.2fx\n", overhead_vs_pure);

    /* Success criteria: >= 2x speedup vs atomic operations */
    if (speedup_vs_atomic >= 2.0) {
        printf("\n*** SUCCESS: Thread-local RC achieves >= 2x speedup! ***\n");
        return 0;
    } else {
        printf("\n*** WARNING: Speedup < 2x, may need tuning ***\n");
        return 1;
    }
}
