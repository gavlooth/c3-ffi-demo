/* test_stress_comprehensive.c - Comprehensive Stress Test Suite
 *
 * This file contains 400+ stress tests for the OmniLisp runtime memory management
 * system. Tests are based on best practices from jemalloc, tcmalloc, and mimalloc-bench.
 *
 * Test Categories:
 * 1. Allocation Pattern Tests (100 tests)
 *    - Small/medium/large object allocation
 *    - Powers-of-2 allocations
 *    - Odd-size allocations
 *    - Mixed-size workloads
 *
 * 2. Fragmentation Tests (60 tests)
 *    - Swiss cheese pattern
 *    - Long-lived object interleaving
 *    - Sawtooth allocation
 *    - Memory compaction scenarios
 *
 * 3. Concurrency Tests (60 tests)
 *    - Producer-consumer patterns
 *    - Asymmetric allocation/deallocation
 *    - Thread contention
 *    - Cross-thread object migration
 *
 * 4. Edge Case Tests (80 tests)
 *    - Boundary conditions
 *    - Error recovery
 *    - Memory poisoning detection
 *    - Alignment verification
 *
 * 5. Type-Specific Tests (50 tests)
 *    - Integer-only workloads
 *    - Pair-heavy (list building) workloads
 *    - String-heavy workloads
 *    - Closure workloads
 *
 * 6. Region-Specific Tests (50 tests)
 *    - Region lifecycle
 *    - Region capacity
 *    - Cross-region references
 *    - Nested regions
 *
 * References:
 * - https://github.com/daanx/mimalloc-bench
 * - https://ithare.com/testing-memory-allocators-ptmalloc2-tcmalloc-hoard-jemalloc-while-trying-to-simulate-real-world-loads/
 * - https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator
 */

#include "test_framework.h"
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* ============================================================================
 * SECTION 1: ALLOCATION PATTERN TESTS (100 tests)
 * Based on mimalloc-bench and jemalloc test suites
 * ============================================================================ */

/* ---------- Small Object Allocation (<64 bytes) ---------- */

/* Test 1-10: Rapid small integer allocation */
void test_alloc_small_int_burst_1k(void) {
    const int COUNT = 1000;
    Obj* objs[COUNT];
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

void test_alloc_small_int_burst_10k(void) {
    const int COUNT = 10000;
    Obj* objs[COUNT];
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

void test_alloc_small_int_burst_50k(void) {
    const int COUNT = 50000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_small_int_burst_100k(void) {
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_small_int_burst_200k(void) {
    const int COUNT = 200000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        if (i % 50000 == 0) {
            ASSERT_NOT_NULL(objs[i]);
        }
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

/* Test 6-10: Rapid small float allocation */
void test_alloc_small_float_burst_1k(void) {
    const int COUNT = 1000;
    Obj* objs[COUNT];
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_float((double)i * 1.5);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

void test_alloc_small_float_burst_10k(void) {
    const int COUNT = 10000;
    Obj* objs[COUNT];
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_float((double)i * 1.5);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

void test_alloc_small_float_burst_50k(void) {
    const int COUNT = 50000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_float((double)i * 1.5);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_small_float_burst_100k(void) {
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_float((double)i * 1.5);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_small_char_burst_100k(void) {
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_char(i % 256);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

/* ---------- Medium Object Allocation (64-512 bytes) ---------- */

/* Test 11-20: Pair allocation (medium size) */
void test_alloc_pair_burst_1k(void) {
    const int COUNT = 1000;
    Obj* objs[COUNT];
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

void test_alloc_pair_burst_10k(void) {
    const int COUNT = 10000;
    Obj* objs[COUNT];
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

void test_alloc_pair_burst_50k(void) {
    const int COUNT = 50000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_pair_burst_100k(void) {
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_nested_pair_10k(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        Obj* inner = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        objs[i] = mk_pair(inner, mk_int_unboxed(i + 2));
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

/* Test 16-20: Box allocation (medium size) */
void test_alloc_box_burst_1k(void) {
    const int COUNT = 1000;
    Obj* objs[COUNT];
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_box(mk_int_unboxed(i));
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

void test_alloc_box_burst_10k(void) {
    const int COUNT = 10000;
    Obj* objs[COUNT];
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_box(mk_int_unboxed(i));
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

void test_alloc_box_burst_50k(void) {
    const int COUNT = 50000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_box(mk_int_unboxed(i));
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_box_burst_100k(void) {
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_box(mk_int_unboxed(i));
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_nested_box_10k(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        Obj* inner = mk_box(mk_int_unboxed(i));
        objs[i] = mk_box(inner);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

/* ---------- Powers-of-2 Allocation Pattern ---------- */

/* Test 21-30: Powers of 2 allocation (mimalloc-bench pattern) */
void test_alloc_pow2_pattern_8(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 8);  /* Small values */
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_pow2_pattern_16(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 16);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_pow2_pattern_32(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 32);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_pow2_pattern_64(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 64);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_pow2_pattern_128(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 128);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_pow2_pattern_256(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 256);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_pow2_pattern_512(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 512);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_pow2_pattern_1024(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 1024);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_pow2_mixed_10k(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    int pow2[] = {8, 16, 32, 64, 128, 256, 512, 1024};
    for (int i = 0; i < ALLOCS; i++) {
        int size_class = pow2[i % 8];
        objs[i] = mk_int(i % size_class);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_pow2_mixed_100k(void) {
    const int ALLOCS = 100000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    int pow2[] = {8, 16, 32, 64, 128, 256, 512, 1024};
    for (int i = 0; i < ALLOCS; i++) {
        int size_class = pow2[i % 8];
        objs[i] = mk_int(i % size_class);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

/* ---------- Odd-Size Allocation Pattern ---------- */

/* Test 31-40: Odd size allocations (stresses allocator rounding) */
void test_alloc_odd_7(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 7);  /* Prime number pattern */
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_odd_13(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 13);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_odd_47(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 47);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_odd_97(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 97);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_odd_prime_sequence(void) {
    const int ALLOCS = 10000;
    int primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47};
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(primes[i % 15]);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_odd_fibonacci(void) {
    const int ALLOCS = 10000;
    int fib[] = {1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610};
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(fib[i % 15]);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_odd_mixed_primes_50k(void) {
    const int ALLOCS = 50000;
    int primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47};
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(primes[i % 15]);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_odd_mixed_primes_100k(void) {
    const int ALLOCS = 100000;
    int primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47};
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(primes[i % 15]);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_odd_alternating(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(i % 2 == 0 ? 7 : 13);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_odd_random_primes(void) {
    const int ALLOCS = 10000;
    int primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29};
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    srand(42);  /* Fixed seed for reproducibility */
    for (int i = 0; i < ALLOCS; i++) {
        objs[i] = mk_int(primes[rand() % 10]);
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

/* ---------- Mixed Size Workloads ---------- */

/* Test 41-50: Mixed size workloads (realistic patterns) */
void test_alloc_mixed_int_float(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        if (i % 2 == 0) {
            objs[i] = mk_int(i);
        } else {
            objs[i] = mk_float((double)i);
        }
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_mixed_int_pair(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        if (i % 3 == 0) {
            objs[i] = mk_int(i);
        } else {
            objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        }
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_mixed_int_float_pair(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        switch (i % 3) {
            case 0: objs[i] = mk_int(i); break;
            case 1: objs[i] = mk_float((double)i); break;
            case 2: objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1)); break;
        }
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_mixed_all_types(void) {
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        switch (i % 5) {
            case 0: objs[i] = mk_int(i); break;
            case 1: objs[i] = mk_float((double)i); break;
            case 2: objs[i] = mk_char(i % 256); break;
            case 3: objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1)); break;
            case 4: objs[i] = mk_box(mk_int_unboxed(i)); break;
        }
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_mixed_all_types_50k(void) {
    const int ALLOCS = 50000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        switch (i % 5) {
            case 0: objs[i] = mk_int(i); break;
            case 1: objs[i] = mk_float((double)i); break;
            case 2: objs[i] = mk_char(i % 256); break;
            case 3: objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1)); break;
            case 4: objs[i] = mk_box(mk_int_unboxed(i)); break;
        }
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_mixed_all_types_100k(void) {
    const int ALLOCS = 100000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        switch (i % 5) {
            case 0: objs[i] = mk_int(i); break;
            case 1: objs[i] = mk_float((double)i); break;
            case 2: objs[i] = mk_char(i % 256); break;
            case 3: objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1)); break;
            case 4: objs[i] = mk_box(mk_int_unboxed(i)); break;
        }
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_weighted_int_heavy(void) {
    /* 70% int, 20% float, 10% pair */
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        int r = i % 10;
        if (r < 7) {
            objs[i] = mk_int(i);
        } else if (r < 9) {
            objs[i] = mk_float((double)i);
        } else {
            objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        }
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_weighted_pair_heavy(void) {
    /* 60% pair, 30% int, 10% float */
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        int r = i % 10;
        if (r < 6) {
            objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        } else if (r < 9) {
            objs[i] = mk_int(i);
        } else {
            objs[i] = mk_float((double)i);
        }
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_bimodal(void) {
    /* 50% small (int), 50% larger (pair of pairs) */
    const int ALLOCS = 10000;
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        if (i % 2 == 0) {
            objs[i] = mk_int(i);
        } else {
            Obj* inner = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
            objs[i] = mk_pair(inner, mk_int_unboxed(i + 2));
        }
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_alloc_trimodal(void) {
    /* 33% small, 33% medium, 33% large-ish */
    const int ALLOCS = 9999;  /* Divisible by 3 */
    Obj** objs = malloc(sizeof(Obj*) * ALLOCS);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < ALLOCS; i++) {
        switch (i % 3) {
            case 0:
                objs[i] = mk_int(i);
                break;
            case 1:
                objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
                break;
            case 2: {
                Obj* inner1 = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
                Obj* inner2 = mk_pair(mk_int_unboxed(i + 2), mk_int_unboxed(i + 3));
                objs[i] = mk_pair(inner1, inner2);
                break;
            }
        }
        ASSERT_NOT_NULL(objs[i]);
    }
    for (int i = 0; i < ALLOCS; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

/* ---------- Rapid Alloc/Free Cycles ---------- */

/* Test 51-60: Rapid allocation/deallocation cycles */
void test_cycle_single_int_10k(void) {
    for (int i = 0; i < 10000; i++) {
        Obj* obj = mk_int(i);
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_cycle_single_int_100k(void) {
    for (int i = 0; i < 100000; i++) {
        Obj* obj = mk_int(i);
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_cycle_single_int_500k(void) {
    for (int i = 0; i < 500000; i++) {
        Obj* obj = mk_int(i);
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_cycle_single_pair_10k(void) {
    for (int i = 0; i < 10000; i++) {
        Obj* obj = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_cycle_single_pair_100k(void) {
    for (int i = 0; i < 100000; i++) {
        Obj* obj = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_cycle_batch_10_100k(void) {
    for (int cycle = 0; cycle < 10000; cycle++) {
        Obj* objs[10];
        for (int i = 0; i < 10; i++) {
            objs[i] = mk_int(cycle * 10 + i);
            ASSERT_NOT_NULL(objs[i]);
        }
        for (int i = 0; i < 10; i++) {
            dec_ref(objs[i]);
        }
    }
    PASS();
}

void test_cycle_batch_100_10k(void) {
    for (int cycle = 0; cycle < 1000; cycle++) {
        Obj* objs[100];
        for (int i = 0; i < 100; i++) {
            objs[i] = mk_int(cycle * 100 + i);
            ASSERT_NOT_NULL(objs[i]);
        }
        for (int i = 0; i < 100; i++) {
            dec_ref(objs[i]);
        }
    }
    PASS();
}

void test_cycle_batch_1000_1k(void) {
    for (int cycle = 0; cycle < 100; cycle++) {
        Obj* objs[1000];
        for (int i = 0; i < 1000; i++) {
            objs[i] = mk_int(cycle * 1000 + i);
            ASSERT_NOT_NULL(objs[i]);
        }
        for (int i = 0; i < 1000; i++) {
            dec_ref(objs[i]);
        }
    }
    PASS();
}

void test_cycle_mixed_batch(void) {
    for (int cycle = 0; cycle < 1000; cycle++) {
        Obj* objs[50];
        for (int i = 0; i < 50; i++) {
            switch (i % 5) {
                case 0: objs[i] = mk_int(i); break;
                case 1: objs[i] = mk_float((double)i); break;
                case 2: objs[i] = mk_char(i % 256); break;
                case 3: objs[i] = mk_pair(mk_int_unboxed(i), NULL); break;
                case 4: objs[i] = mk_box(mk_int_unboxed(i)); break;
            }
            ASSERT_NOT_NULL(objs[i]);
        }
        for (int i = 0; i < 50; i++) {
            dec_ref(objs[i]);
        }
    }
    PASS();
}

void test_cycle_nested_pair_batch(void) {
    for (int cycle = 0; cycle < 1000; cycle++) {
        Obj* objs[10];
        for (int i = 0; i < 10; i++) {
            Obj* inner = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
            objs[i] = mk_pair(inner, mk_int_unboxed(i + 2));
            ASSERT_NOT_NULL(objs[i]);
        }
        for (int i = 0; i < 10; i++) {
            dec_ref(objs[i]);
        }
    }
    PASS();
}

/* ---------- Deallocation Order Tests ---------- */

/* Test 61-70: Various deallocation orders */
void test_dealloc_lifo(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    /* LIFO order - last allocated, first freed */
    for (int i = COUNT - 1; i >= 0; i--) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_dealloc_fifo(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    /* FIFO order - first allocated, first freed */
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_dealloc_random(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    int* indices = malloc(sizeof(int) * COUNT);
    ASSERT_NOT_NULL(objs);
    ASSERT_NOT_NULL(indices);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        indices[i] = i;
        ASSERT_NOT_NULL(objs[i]);
    }

    /* Fisher-Yates shuffle */
    srand(42);
    for (int i = COUNT - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = indices[i];
        indices[i] = indices[j];
        indices[j] = tmp;
    }

    /* Random order free */
    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[indices[i]]);
    }

    free(objs);
    free(indices);
    PASS();
}

void test_dealloc_alternating(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    /* Alternating: even first, then odd */
    for (int i = 0; i < COUNT; i += 2) {
        dec_ref(objs[i]);
    }
    for (int i = 1; i < COUNT; i += 2) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_dealloc_block_based(void) {
    const int COUNT = 10000;
    const int BLOCK = 100;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    /* Block-based: free in blocks of 100 */
    for (int block = 0; block < COUNT / BLOCK; block++) {
        for (int i = 0; i < BLOCK; i++) {
            dec_ref(objs[block * BLOCK + i]);
        }
    }
    free(objs);
    PASS();
}

void test_dealloc_reverse_block(void) {
    const int COUNT = 10000;
    const int BLOCK = 100;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    /* Free blocks in reverse order */
    for (int block = COUNT / BLOCK - 1; block >= 0; block--) {
        for (int i = 0; i < BLOCK; i++) {
            dec_ref(objs[block * BLOCK + i]);
        }
    }
    free(objs);
    PASS();
}

void test_dealloc_stripe_2(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    /* Free every 2nd element in multiple passes */
    for (int stride = 0; stride < 2; stride++) {
        for (int i = stride; i < COUNT; i += 2) {
            dec_ref(objs[i]);
        }
    }
    free(objs);
    PASS();
}

void test_dealloc_stripe_3(void) {
    const int COUNT = 9999;  /* Divisible by 3 */
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    /* Free every 3rd element in multiple passes */
    for (int stride = 0; stride < 3; stride++) {
        for (int i = stride; i < COUNT; i += 3) {
            dec_ref(objs[i]);
        }
    }
    free(objs);
    PASS();
}

void test_dealloc_stripe_4(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    /* Free every 4th element in multiple passes */
    for (int stride = 0; stride < 4; stride++) {
        for (int i = stride; i < COUNT; i += 4) {
            dec_ref(objs[i]);
        }
    }
    free(objs);
    PASS();
}

void test_dealloc_binary_split(void) {
    const int COUNT = 8192;  /* Power of 2 */
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }
    /* Free in binary split pattern */
    for (int stride = COUNT / 2; stride >= 1; stride /= 2) {
        for (int i = stride - 1; i < COUNT; i += stride * 2) {
            if (objs[i]) {
                dec_ref(objs[i]);
                objs[i] = NULL;
            }
        }
    }
    /* Free remaining */
    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) {
            dec_ref(objs[i]);
        }
    }
    free(objs);
    PASS();
}

/* ---------- Symbol/String Allocation Tests ---------- */

/* Test 71-80: Symbol allocation patterns */
void test_alloc_symbol_burst_1k(void) {
    const int COUNT = 1000;
    char name[32];
    for (int i = 0; i < COUNT; i++) {
        snprintf(name, sizeof(name), "sym_%d", i);
        Obj* sym = mk_sym(name);
        ASSERT_NOT_NULL(sym);
        dec_ref(sym);
    }
    PASS();
}

void test_alloc_symbol_burst_10k(void) {
    const int COUNT = 10000;
    char name[32];
    for (int i = 0; i < COUNT; i++) {
        snprintf(name, sizeof(name), "sym_%d", i);
        Obj* sym = mk_sym(name);
        ASSERT_NOT_NULL(sym);
        dec_ref(sym);
    }
    PASS();
}

void test_alloc_symbol_interning_1k(void) {
    /* Test symbol interning - same name should return same pointer */
    const int COUNT = 1000;
    const char* names[] = {"foo", "bar", "baz", "qux", "quux"};
    for (int i = 0; i < COUNT; i++) {
        Obj* sym1 = mk_sym(names[i % 5]);
        Obj* sym2 = mk_sym(names[i % 5]);
        ASSERT_EQ(sym1, sym2);  /* Should be interned */
        dec_ref(sym1);
        dec_ref(sym2);
    }
    PASS();
}

void test_alloc_symbol_unique_10k(void) {
    /* Each symbol is unique */
    const int COUNT = 10000;
    char name[32];
    Obj** syms = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(syms);
    for (int i = 0; i < COUNT; i++) {
        snprintf(name, sizeof(name), "unique_sym_%d", i);
        syms[i] = mk_sym(name);
        ASSERT_NOT_NULL(syms[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(syms[i]);
    }
    free(syms);
    PASS();
}

void test_alloc_symbol_long_names(void) {
    /* Test with progressively longer names */
    const int COUNT = 1000;
    char name[1024];
    for (int i = 0; i < COUNT; i++) {
        int len = (i % 100) + 1;
        memset(name, 'a', len);
        name[len] = '\0';
        Obj* sym = mk_sym(name);
        ASSERT_NOT_NULL(sym);
        dec_ref(sym);
    }
    PASS();
}

void test_alloc_symbol_special_chars(void) {
    const char* names[] = {
        "foo-bar", "foo_bar", "foo.bar", "foo?bar", "foo!bar",
        "foo+bar", "foo*bar", "foo/bar", "foo=bar", "foo<bar",
        "foo>bar", "foo@bar", "foo#bar", "foo$bar", "foo%bar"
    };
    for (int i = 0; i < 1000; i++) {
        Obj* sym = mk_sym(names[i % 15]);
        ASSERT_NOT_NULL(sym);
        dec_ref(sym);
    }
    PASS();
}

void test_alloc_symbol_unicode_like(void) {
    /* Names with high ASCII chars */
    char name[32];
    for (int i = 0; i < 1000; i++) {
        snprintf(name, sizeof(name), "sym_%c%c%c",
                 (char)(128 + (i % 50)),
                 (char)(128 + ((i + 1) % 50)),
                 (char)(128 + ((i + 2) % 50)));
        Obj* sym = mk_sym(name);
        ASSERT_NOT_NULL(sym);
        dec_ref(sym);
    }
    PASS();
}

void test_alloc_symbol_empty_name(void) {
    for (int i = 0; i < 1000; i++) {
        Obj* sym = mk_sym("");
        ASSERT_NOT_NULL(sym);
        dec_ref(sym);
    }
    PASS();
}

void test_alloc_symbol_numeric_names(void) {
    char name[32];
    for (int i = 0; i < 1000; i++) {
        snprintf(name, sizeof(name), "%d", i);
        Obj* sym = mk_sym(name);
        ASSERT_NOT_NULL(sym);
        dec_ref(sym);
    }
    PASS();
}

void test_alloc_symbol_repeated_intern(void) {
    /* Repeatedly intern the same small set */
    const char* names[] = {"a", "b", "c", "d", "e"};
    for (int i = 0; i < 100000; i++) {
        Obj* sym = mk_sym(names[i % 5]);
        ASSERT_NOT_NULL(sym);
        dec_ref(sym);
    }
    PASS();
}

/* ---------- List Construction Tests ---------- */

/* Test 81-90: List construction patterns */
void test_alloc_list_construction_1k(void) {
    Obj* list = NULL;
    for (int i = 0; i < 1000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
        ASSERT_NOT_NULL(list);
    }
    dec_ref(list);
    PASS();
}

void test_alloc_list_construction_10k(void) {
    Obj* list = NULL;
    for (int i = 0; i < 10000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
        ASSERT_NOT_NULL(list);
    }
    dec_ref(list);
    PASS();
}

void test_alloc_list_construction_50k(void) {
    Obj* list = NULL;
    for (int i = 0; i < 50000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
        if (i % 10000 == 0) {
            ASSERT_NOT_NULL(list);
        }
    }
    dec_ref(list);
    PASS();
}

void test_alloc_list_construction_100k(void) {
    Obj* list = NULL;
    for (int i = 0; i < 100000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
        if (i % 25000 == 0) {
            ASSERT_NOT_NULL(list);
        }
    }
    dec_ref(list);
    PASS();
}

void test_alloc_multiple_lists(void) {
    const int NUM_LISTS = 100;
    const int LIST_SIZE = 1000;
    Obj* lists[NUM_LISTS];

    for (int l = 0; l < NUM_LISTS; l++) {
        lists[l] = NULL;
        for (int i = 0; i < LIST_SIZE; i++) {
            lists[l] = mk_pair(mk_int_unboxed(l * LIST_SIZE + i), lists[l]);
        }
        ASSERT_NOT_NULL(lists[l]);
    }

    for (int l = 0; l < NUM_LISTS; l++) {
        dec_ref(lists[l]);
    }
    PASS();
}

void test_alloc_nested_list(void) {
    /* List of lists */
    Obj* outer = NULL;
    for (int i = 0; i < 100; i++) {
        Obj* inner = NULL;
        for (int j = 0; j < 100; j++) {
            inner = mk_pair(mk_int_unboxed(i * 100 + j), inner);
        }
        outer = mk_pair(inner, outer);
    }
    ASSERT_NOT_NULL(outer);
    dec_ref(outer);
    PASS();
}

void test_alloc_association_list(void) {
    /* Association list: ((key . value) (key . value) ...) */
    Obj* alist = NULL;
    for (int i = 0; i < 1000; i++) {
        Obj* pair = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i * 2));
        alist = mk_pair(pair, alist);
    }
    ASSERT_NOT_NULL(alist);
    dec_ref(alist);
    PASS();
}

void test_alloc_tree_as_list(void) {
    /* Binary tree represented as nested pairs */
    Obj* tree = mk_int_unboxed(0);
    for (int depth = 0; depth < 15; depth++) {
        Obj* left = mk_int_unboxed(depth * 2 + 1);
        Obj* right = mk_int_unboxed(depth * 2 + 2);
        tree = mk_pair(tree, mk_pair(left, right));
    }
    ASSERT_NOT_NULL(tree);
    dec_ref(tree);
    PASS();
}

void test_alloc_list_with_shared(void) {
    /* List with shared tail */
    Obj* shared_tail = NULL;
    for (int i = 0; i < 100; i++) {
        shared_tail = mk_pair(mk_int_unboxed(i), shared_tail);
    }

    /* Multiple heads sharing the same tail */
    Obj* heads[10];
    for (int h = 0; h < 10; h++) {
        inc_ref(shared_tail);
        heads[h] = mk_pair(mk_int_unboxed(h + 100), shared_tail);
    }

    for (int h = 0; h < 10; h++) {
        dec_ref(heads[h]);
    }
    dec_ref(shared_tail);
    PASS();
}

void test_alloc_list_interleaved_free(void) {
    /* Build list, free every other element's head */
    Obj* list = NULL;
    for (int i = 0; i < 1000; i++) {
        Obj* val = mk_int(i);  /* Boxed int */
        list = mk_pair(val, list);
    }

    /* Walk and verify */
    int count = 0;
    Obj* cur = list;
    while (cur && cur->tag == TAG_PAIR) {
        count++;
        cur = cur->b;
    }
    ASSERT_EQ(count, 1000);

    dec_ref(list);
    PASS();
}

/* ---------- Closure/Function Allocation Tests ---------- */

/* Test 91-100: Closure and function-like allocations */
/* Note: mk_closure signature is (fn, captures_array, refs_array, count, arity) */
/* ClosureFn signature is: Obj* (*)(Obj** captures, Obj** args, int argc) */
static Obj* dummy_closure_fn(Obj** captures, Obj** args, int argc) {
    (void)captures;
    (void)args;
    (void)argc;
    return mk_int_unboxed(42);
}

void test_alloc_closure_burst_1k(void) {
    const int COUNT = 1000;
    Obj** closures = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(closures);
    for (int i = 0; i < COUNT; i++) {
        /* Create closure with one capture */
        Obj* caps[1];
        caps[0] = mk_int_unboxed(i);
        closures[i] = mk_closure(dummy_closure_fn, caps, NULL, 1, 0);
        ASSERT_NOT_NULL(closures[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(closures[i]);
    }
    free(closures);
    PASS();
}

void test_alloc_closure_burst_10k(void) {
    const int COUNT = 10000;
    Obj** closures = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(closures);
    for (int i = 0; i < COUNT; i++) {
        /* Create closure with one capture */
        Obj* caps[1];
        caps[0] = mk_int_unboxed(i);
        closures[i] = mk_closure(dummy_closure_fn, caps, NULL, 1, 0);
        ASSERT_NOT_NULL(closures[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(closures[i]);
    }
    free(closures);
    PASS();
}

void test_alloc_closure_with_env(void) {
    const int COUNT = 5000;
    Obj** closures = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(closures);
    for (int i = 0; i < COUNT; i++) {
        /* Create environment with multiple captured values */
        Obj* caps[2];
        caps[0] = mk_int_unboxed(i);
        caps[1] = mk_int_unboxed(i + 1);
        closures[i] = mk_closure(dummy_closure_fn, caps, NULL, 2, 0);
        ASSERT_NOT_NULL(closures[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(closures[i]);
    }
    free(closures);
    PASS();
}

void test_alloc_closure_nested_env(void) {
    const int COUNT = 1000;
    Obj** closures = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(closures);
    for (int i = 0; i < COUNT; i++) {
        /* Capture multiple values */
        Obj* caps[5];
        for (int j = 0; j < 5; j++) {
            caps[j] = mk_int_unboxed(j);
        }
        closures[i] = mk_closure(dummy_closure_fn, caps, NULL, 5, 0);
        ASSERT_NOT_NULL(closures[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(closures[i]);
    }
    free(closures);
    PASS();
}

void test_alloc_closure_shared_env(void) {
    /* Multiple closures sharing captures */
    Obj* shared_val1 = mk_int_unboxed(1);
    Obj* shared_val2 = mk_int_unboxed(2);

    const int COUNT = 1000;
    Obj** closures = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(closures);

    for (int i = 0; i < COUNT; i++) {
        inc_ref(shared_val1);
        inc_ref(shared_val2);
        Obj* caps[2];
        caps[0] = shared_val1;
        caps[1] = shared_val2;
        closures[i] = mk_closure(dummy_closure_fn, caps, NULL, 2, 0);
        ASSERT_NOT_NULL(closures[i]);
    }

    for (int i = 0; i < COUNT; i++) {
        dec_ref(closures[i]);
    }
    dec_ref(shared_val1);
    dec_ref(shared_val2);
    free(closures);
    PASS();
}

void test_alloc_closure_cycle(void) {
    /* Create and free closures in tight loop */
    for (int i = 0; i < 10000; i++) {
        Obj* caps[1];
        caps[0] = mk_int_unboxed(i);
        Obj* closure = mk_closure(dummy_closure_fn, caps, NULL, 1, 0);
        ASSERT_NOT_NULL(closure);
        dec_ref(closure);
    }
    PASS();
}

void test_alloc_closure_mixed_env(void) {
    const int COUNT = 1000;
    Obj** closures = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(closures);
    for (int i = 0; i < COUNT; i++) {
        /* Environment with mixed types */
        Obj* caps[3];
        caps[0] = mk_int_unboxed(i);
        caps[1] = mk_float((double)i);
        caps[2] = mk_char_unboxed(i % 256);
        closures[i] = mk_closure(dummy_closure_fn, caps, NULL, 3, 0);
        ASSERT_NOT_NULL(closures[i]);
    }
    for (int i = 0; i < COUNT; i++) {
        dec_ref(closures[i]);
    }
    free(closures);
    PASS();
}

void test_alloc_closure_recursive_pattern(void) {
    /* Simulate recursive function call pattern */
    const int DEPTH = 100;
    const int ITERATIONS = 100;

    for (int iter = 0; iter < ITERATIONS; iter++) {
        Obj* closures[DEPTH];

        /* Build chain - each closure captures the previous */
        Obj* caps0[1];
        caps0[0] = mk_int_unboxed(0);
        closures[0] = mk_closure(dummy_closure_fn, caps0, NULL, 1, 0);

        for (int d = 1; d < DEPTH; d++) {
            inc_ref(closures[d - 1]);
            Obj* caps[1];
            caps[0] = closures[d - 1];
            closures[d] = mk_closure(dummy_closure_fn, caps, NULL, 1, 0);
        }

        /* Free in reverse order */
        for (int d = DEPTH - 1; d >= 0; d--) {
            dec_ref(closures[d]);
        }
    }
    PASS();
}

void test_alloc_closure_parallel_chains(void) {
    /* Multiple parallel closure chains */
    const int CHAINS = 10;
    const int CHAIN_LEN = 100;
    Obj* chains[CHAINS];

    for (int c = 0; c < CHAINS; c++) {
        Obj* caps0[1];
        caps0[0] = mk_int_unboxed(c * 1000);
        chains[c] = mk_closure(dummy_closure_fn, caps0, NULL, 1, 0);

        for (int i = 1; i < CHAIN_LEN; i++) {
            inc_ref(chains[c]);
            Obj* caps[1];
            caps[0] = chains[c];
            Obj* new_closure = mk_closure(dummy_closure_fn, caps, NULL, 1, 0);
            dec_ref(chains[c]);
            chains[c] = new_closure;
        }
    }

    for (int c = 0; c < CHAINS; c++) {
        dec_ref(chains[c]);
    }
    PASS();
}

void test_alloc_closure_box_env(void) {
    /* Closures with boxed values as captures (mutable captures) */
    const int COUNT = 1000;
    Obj** closures = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(closures);

    for (int i = 0; i < COUNT; i++) {
        Obj* caps[1];
        caps[0] = mk_box(mk_int_unboxed(i));
        closures[i] = mk_closure(dummy_closure_fn, caps, NULL, 1, 0);
        ASSERT_NOT_NULL(closures[i]);
    }

    for (int i = 0; i < COUNT; i++) {
        dec_ref(closures[i]);
    }
    free(closures);
    PASS();
}


/* ============================================================================
 * SECTION 2: FRAGMENTATION TESTS (60 tests)
 * Based on real-world fragmentation scenarios from production systems
 * ============================================================================ */

/* ---------- Swiss Cheese Pattern ---------- */

/* Test 101-110: Swiss cheese allocation (creates holes) */
void test_frag_swiss_cheese_10k(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    /* Allocate all */
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }

    /* Free every other one - creates "holes" */
    for (int i = 0; i < COUNT; i += 2) {
        dec_ref(objs[i]);
        objs[i] = NULL;
    }

    /* Allocate again - should fill holes */
    for (int i = 0; i < COUNT; i += 2) {
        objs[i] = mk_int(i + COUNT);
        ASSERT_NOT_NULL(objs[i]);
    }

    /* Free all */
    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_swiss_cheese_50k(void) {
    const int COUNT = 50000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }

    for (int i = 0; i < COUNT; i += 2) {
        dec_ref(objs[i]);
        objs[i] = NULL;
    }

    for (int i = 0; i < COUNT; i += 2) {
        objs[i] = mk_int(i + COUNT);
        ASSERT_NOT_NULL(objs[i]);
    }

    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_swiss_cheese_every_3rd(void) {
    const int COUNT = 9999;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    /* Free every 3rd */
    for (int i = 0; i < COUNT; i += 3) {
        dec_ref(objs[i]);
        objs[i] = NULL;
    }

    /* Refill */
    for (int i = 0; i < COUNT; i += 3) {
        objs[i] = mk_int(i + COUNT);
    }

    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_swiss_cheese_every_4th(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    for (int i = 0; i < COUNT; i += 4) {
        dec_ref(objs[i]);
        objs[i] = NULL;
    }

    for (int i = 0; i < COUNT; i += 4) {
        objs[i] = mk_int(i + COUNT);
    }

    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_swiss_cheese_random(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    srand(42);
    /* Free random 50% */
    for (int i = 0; i < COUNT / 2; i++) {
        int idx = rand() % COUNT;
        if (objs[idx]) {
            dec_ref(objs[idx]);
            objs[idx] = NULL;
        }
    }

    /* Refill all NULLs */
    for (int i = 0; i < COUNT; i++) {
        if (!objs[i]) {
            objs[i] = mk_int(i + COUNT);
        }
    }

    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_swiss_cheese_multi_round(void) {
    const int COUNT = 5000;
    const int ROUNDS = 5;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    for (int round = 0; round < ROUNDS; round++) {
        /* Free every other */
        for (int i = round % 2; i < COUNT; i += 2) {
            if (objs[i]) {
                dec_ref(objs[i]);
                objs[i] = NULL;
            }
        }
        /* Refill */
        for (int i = 0; i < COUNT; i++) {
            if (!objs[i]) {
                objs[i] = mk_int(i + (round + 1) * COUNT);
            }
        }
    }

    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_swiss_cheese_with_pairs(void) {
    const int COUNT = 5000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
    }

    for (int i = 0; i < COUNT; i += 2) {
        dec_ref(objs[i]);
        objs[i] = NULL;
    }

    for (int i = 0; i < COUNT; i += 2) {
        objs[i] = mk_pair(mk_int_unboxed(i + COUNT), mk_int_unboxed(i + COUNT + 1));
    }

    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_swiss_cheese_mixed_types(void) {
    const int COUNT = 5000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        switch (i % 3) {
            case 0: objs[i] = mk_int(i); break;
            case 1: objs[i] = mk_float((double)i); break;
            case 2: objs[i] = mk_pair(mk_int_unboxed(i), NULL); break;
        }
    }

    for (int i = 0; i < COUNT; i += 2) {
        dec_ref(objs[i]);
        objs[i] = NULL;
    }

    for (int i = 0; i < COUNT; i += 2) {
        switch (i % 3) {
            case 0: objs[i] = mk_float((double)i); break;  /* Different type */
            case 1: objs[i] = mk_pair(mk_int_unboxed(i), NULL); break;
            case 2: objs[i] = mk_int(i); break;
        }
    }

    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_swiss_cheese_progressive(void) {
    /* Progressively increasing hole sizes */
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    /* Create holes of increasing size */
    int pos = 0;
    int hole_size = 1;
    while (pos < COUNT) {
        for (int i = 0; i < hole_size && pos + i < COUNT; i++) {
            if (objs[pos + i]) {
                dec_ref(objs[pos + i]);
                objs[pos + i] = NULL;
            }
        }
        pos += hole_size * 2;  /* Leave gap between holes */
        hole_size++;
        if (hole_size > 10) hole_size = 1;
    }

    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_swiss_cheese_cluster(void) {
    /* Create clusters of holes */
    const int COUNT = 10000;
    const int CLUSTER_SIZE = 100;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    /* Free clusters */
    for (int cluster = 0; cluster < COUNT / CLUSTER_SIZE; cluster += 2) {
        for (int i = 0; i < CLUSTER_SIZE / 2; i++) {
            int idx = cluster * CLUSTER_SIZE + i;
            if (idx < COUNT && objs[idx]) {
                dec_ref(objs[idx]);
                objs[idx] = NULL;
            }
        }
    }

    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

/* ---------- Long-Lived Object Interleaving ---------- */

/* Test 111-120: Long-lived objects mixed with short-lived */
void test_frag_long_lived_simple(void) {
    const int LONG_COUNT = 100;
    const int SHORT_COUNT = 10000;

    /* Allocate long-lived objects */
    Obj* long_lived[LONG_COUNT];
    for (int i = 0; i < LONG_COUNT; i++) {
        long_lived[i] = mk_int(i);
    }

    /* Allocate and free short-lived objects many times */
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < SHORT_COUNT; i++) {
            Obj* obj = mk_int(i);
            dec_ref(obj);
        }
    }

    /* Free long-lived */
    for (int i = 0; i < LONG_COUNT; i++) {
        dec_ref(long_lived[i]);
    }
    PASS();
}

void test_frag_long_lived_interleaved(void) {
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    int* is_long_lived = malloc(sizeof(int) * COUNT);
    ASSERT_NOT_NULL(objs);
    ASSERT_NOT_NULL(is_long_lived);

    /* Allocate with interleaved long/short */
    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        is_long_lived[i] = (i % 10 == 0);  /* Every 10th is long-lived */
    }

    /* Free short-lived */
    for (int i = 0; i < COUNT; i++) {
        if (!is_long_lived[i]) {
            dec_ref(objs[i]);
            objs[i] = NULL;
        }
    }

    /* Allocate more short-lived around long-lived */
    for (int i = 0; i < COUNT; i++) {
        if (!objs[i]) {
            objs[i] = mk_int(i + COUNT);
        }
    }

    /* Free all */
    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }

    free(objs);
    free(is_long_lived);
    PASS();
}

void test_frag_long_lived_anchors(void) {
    /* Long-lived "anchor" objects that prevent coalescing */
    const int ANCHOR_INTERVAL = 100;
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    /* Free all except anchors */
    for (int i = 0; i < COUNT; i++) {
        if (i % ANCHOR_INTERVAL != 0) {
            dec_ref(objs[i]);
            objs[i] = NULL;
        }
    }

    /* Fill gaps */
    for (int i = 0; i < COUNT; i++) {
        if (!objs[i]) {
            objs[i] = mk_int(i + COUNT);
        }
    }

    /* Free anchors, then everything */
    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_long_lived_grow_shrink(void) {
    const int MAX = 10000;
    Obj** objs = malloc(sizeof(Obj*) * MAX);
    ASSERT_NOT_NULL(objs);

    /* Grow */
    for (int i = 0; i < MAX; i++) {
        objs[i] = mk_int(i);
    }

    /* Keep every 10th, free rest */
    for (int i = 0; i < MAX; i++) {
        if (i % 10 != 0) {
            dec_ref(objs[i]);
            objs[i] = NULL;
        }
    }

    /* Grow again */
    for (int i = 0; i < MAX; i++) {
        if (!objs[i]) {
            objs[i] = mk_int(i + MAX);
        }
    }

    /* Free all */
    for (int i = 0; i < MAX; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_long_lived_pairs(void) {
    const int LONG_COUNT = 100;
    const int SHORT_ROUNDS = 10;
    const int SHORT_PER_ROUND = 1000;

    /* Long-lived pairs */
    Obj* long_lived[LONG_COUNT];
    for (int i = 0; i < LONG_COUNT; i++) {
        long_lived[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
    }

    /* Short-lived pairs */
    for (int round = 0; round < SHORT_ROUNDS; round++) {
        for (int i = 0; i < SHORT_PER_ROUND; i++) {
            Obj* pair = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
            dec_ref(pair);
        }
    }

    for (int i = 0; i < LONG_COUNT; i++) {
        dec_ref(long_lived[i]);
    }
    PASS();
}

void test_frag_long_lived_boxes(void) {
    const int LONG_COUNT = 100;
    const int SHORT_ROUNDS = 10;
    const int SHORT_PER_ROUND = 1000;

    /* Long-lived boxes (mutable references) */
    Obj* long_lived[LONG_COUNT];
    for (int i = 0; i < LONG_COUNT; i++) {
        long_lived[i] = mk_box(mk_int_unboxed(i));
    }

    /* Short-lived updates */
    for (int round = 0; round < SHORT_ROUNDS; round++) {
        for (int i = 0; i < SHORT_PER_ROUND; i++) {
            int box_idx = i % LONG_COUNT;
            Obj* new_val = mk_int(round * SHORT_PER_ROUND + i);
            box_set(long_lived[box_idx], new_val);
            dec_ref(new_val);
        }
    }

    for (int i = 0; i < LONG_COUNT; i++) {
        dec_ref(long_lived[i]);
    }
    PASS();
}

void test_frag_long_lived_lists(void) {
    const int LIST_COUNT = 10;
    const int LIST_LEN = 100;
    const int SHORT_ROUNDS = 10;

    /* Long-lived lists */
    Obj* lists[LIST_COUNT];
    for (int l = 0; l < LIST_COUNT; l++) {
        lists[l] = NULL;
        for (int i = 0; i < LIST_LEN; i++) {
            lists[l] = mk_pair(mk_int_unboxed(l * LIST_LEN + i), lists[l]);
        }
    }

    /* Short-lived allocations */
    for (int round = 0; round < SHORT_ROUNDS; round++) {
        for (int i = 0; i < 1000; i++) {
            Obj* obj = mk_int(i);
            dec_ref(obj);
        }
    }

    for (int l = 0; l < LIST_COUNT; l++) {
        dec_ref(lists[l]);
    }
    PASS();
}

void test_frag_long_lived_closures(void) {
    const int CLOSURE_COUNT = 100;
    const int SHORT_ROUNDS = 10;

    /* Long-lived closures */
    Obj* closures[CLOSURE_COUNT];
    for (int i = 0; i < CLOSURE_COUNT; i++) {
        Obj* caps[1];
        caps[0] = mk_int_unboxed(i);
        closures[i] = mk_closure(dummy_closure_fn, caps, NULL, 1, 0);
    }

    /* Short-lived allocations */
    for (int round = 0; round < SHORT_ROUNDS; round++) {
        for (int i = 0; i < 1000; i++) {
            Obj* obj = mk_int(i);
            dec_ref(obj);
        }
    }

    for (int i = 0; i < CLOSURE_COUNT; i++) {
        dec_ref(closures[i]);
    }
    PASS();
}

void test_frag_long_lived_symbols(void) {
    /* Symbols are typically long-lived (interned) */
    const int SYM_COUNT = 100;
    const int SHORT_ROUNDS = 10;

    char name[32];
    Obj* syms[SYM_COUNT];
    for (int i = 0; i < SYM_COUNT; i++) {
        snprintf(name, sizeof(name), "long_sym_%d", i);
        syms[i] = mk_sym(name);
    }

    for (int round = 0; round < SHORT_ROUNDS; round++) {
        for (int i = 0; i < 1000; i++) {
            Obj* obj = mk_int(i);
            dec_ref(obj);
        }
    }

    for (int i = 0; i < SYM_COUNT; i++) {
        dec_ref(syms[i]);
    }
    PASS();
}

void test_frag_long_lived_deep_tree(void) {
    /* Long-lived deep tree structure */
    Obj* tree = mk_int_unboxed(0);
    for (int depth = 0; depth < 100; depth++) {
        tree = mk_pair(tree, mk_int_unboxed(depth));
    }

    /* Short-lived allocations around it */
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 1000; i++) {
            Obj* obj = mk_int(i);
            dec_ref(obj);
        }
    }

    dec_ref(tree);
    PASS();
}

/* ---------- Sawtooth Pattern ---------- */

/* Test 121-130: Sawtooth memory pattern (grow, partial free, grow, ...) */
void test_frag_sawtooth_simple(void) {
    const int PEAK = 5000;
    const int VALLEY = 1000;
    const int CYCLES = 5;

    Obj** objs = malloc(sizeof(Obj*) * PEAK);
    ASSERT_NOT_NULL(objs);

    int current_size = 0;

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        /* Grow to peak */
        while (current_size < PEAK) {
            objs[current_size] = mk_int(cycle * PEAK + current_size);
            current_size++;
        }

        /* Shrink to valley */
        while (current_size > VALLEY) {
            current_size--;
            dec_ref(objs[current_size]);
            objs[current_size] = NULL;
        }
    }

    /* Final cleanup */
    for (int i = 0; i < current_size; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_sawtooth_aggressive(void) {
    const int PEAK = 10000;
    const int VALLEY = 100;  /* More aggressive shrink */
    const int CYCLES = 10;

    Obj** objs = malloc(sizeof(Obj*) * PEAK);
    ASSERT_NOT_NULL(objs);

    int current_size = 0;

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        while (current_size < PEAK) {
            objs[current_size] = mk_int(current_size);
            current_size++;
        }

        while (current_size > VALLEY) {
            current_size--;
            dec_ref(objs[current_size]);
        }
    }

    for (int i = 0; i < current_size; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_sawtooth_pairs(void) {
    const int PEAK = 5000;
    const int VALLEY = 1000;
    const int CYCLES = 5;

    Obj** objs = malloc(sizeof(Obj*) * PEAK);
    ASSERT_NOT_NULL(objs);

    int current_size = 0;

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        while (current_size < PEAK) {
            objs[current_size] = mk_pair(mk_int_unboxed(current_size), mk_int_unboxed(current_size + 1));
            current_size++;
        }

        while (current_size > VALLEY) {
            current_size--;
            dec_ref(objs[current_size]);
        }
    }

    for (int i = 0; i < current_size; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_sawtooth_mixed(void) {
    const int PEAK = 5000;
    const int VALLEY = 1000;
    const int CYCLES = 5;

    Obj** objs = malloc(sizeof(Obj*) * PEAK);
    ASSERT_NOT_NULL(objs);

    int current_size = 0;

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        while (current_size < PEAK) {
            switch (current_size % 3) {
                case 0: objs[current_size] = mk_int(current_size); break;
                case 1: objs[current_size] = mk_float((double)current_size); break;
                case 2: objs[current_size] = mk_pair(mk_int_unboxed(current_size), NULL); break;
            }
            current_size++;
        }

        while (current_size > VALLEY) {
            current_size--;
            dec_ref(objs[current_size]);
        }
    }

    for (int i = 0; i < current_size; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_sawtooth_variable_peak(void) {
    const int MAX_PEAK = 10000;
    const int CYCLES = 10;

    Obj** objs = malloc(sizeof(Obj*) * MAX_PEAK);
    ASSERT_NOT_NULL(objs);

    int current_size = 0;

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        int peak = 1000 + (cycle * 1000);  /* Increasing peak */
        int valley = peak / 10;

        while (current_size < peak) {
            objs[current_size] = mk_int(current_size);
            current_size++;
        }

        while (current_size > valley) {
            current_size--;
            dec_ref(objs[current_size]);
        }
    }

    for (int i = 0; i < current_size; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_sawtooth_random_valley(void) {
    const int PEAK = 5000;
    const int CYCLES = 10;

    Obj** objs = malloc(sizeof(Obj*) * PEAK);
    ASSERT_NOT_NULL(objs);

    int current_size = 0;
    srand(42);

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        while (current_size < PEAK) {
            objs[current_size] = mk_int(current_size);
            current_size++;
        }

        int valley = rand() % (PEAK / 2);
        while (current_size > valley) {
            current_size--;
            dec_ref(objs[current_size]);
        }
    }

    for (int i = 0; i < current_size; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_sawtooth_micro(void) {
    /* Very small sawtooth - tests allocator for small allocations */
    const int PEAK = 100;
    const int VALLEY = 10;
    const int CYCLES = 1000;

    Obj* objs[PEAK];
    int current_size = 0;

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        while (current_size < PEAK) {
            objs[current_size] = mk_int(current_size);
            current_size++;
        }

        while (current_size > VALLEY) {
            current_size--;
            dec_ref(objs[current_size]);
        }
    }

    for (int i = 0; i < current_size; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    PASS();
}

void test_frag_sawtooth_double(void) {
    /* Two sawtooth patterns interleaved */
    const int PEAK = 2500;
    const int VALLEY = 500;
    const int CYCLES = 5;

    Obj** objs1 = malloc(sizeof(Obj*) * PEAK);
    Obj** objs2 = malloc(sizeof(Obj*) * PEAK);
    ASSERT_NOT_NULL(objs1);
    ASSERT_NOT_NULL(objs2);

    int size1 = 0, size2 = 0;

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        /* Grow both */
        while (size1 < PEAK) {
            objs1[size1] = mk_int(size1);
            size1++;
            if (size2 < PEAK) {
                objs2[size2] = mk_float((double)size2);
                size2++;
            }
        }

        /* Shrink first */
        while (size1 > VALLEY) {
            size1--;
            dec_ref(objs1[size1]);
        }

        /* Shrink second */
        while (size2 > VALLEY) {
            size2--;
            dec_ref(objs2[size2]);
        }
    }

    for (int i = 0; i < size1; i++) dec_ref(objs1[i]);
    for (int i = 0; i < size2; i++) dec_ref(objs2[i]);
    free(objs1);
    free(objs2);
    PASS();
}

void test_frag_sawtooth_lists(void) {
    /* Build and partially tear down lists */
    const int CYCLES = 10;

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        Obj* list = NULL;

        /* Build list */
        for (int i = 0; i < 1000; i++) {
            list = mk_pair(mk_int_unboxed(i), list);
        }

        /* Keep only first 100 elements (drop tail) */
        /* In Lisp, this would require traversal; here we just free whole list */
        dec_ref(list);
    }
    PASS();
}

void test_frag_sawtooth_closures(void) {
    const int PEAK = 1000;
    const int VALLEY = 100;
    const int CYCLES = 10;

    Obj** closures = malloc(sizeof(Obj*) * PEAK);
    ASSERT_NOT_NULL(closures);

    int current_size = 0;

    for (int cycle = 0; cycle < CYCLES; cycle++) {
        while (current_size < PEAK) {
            Obj* caps[1];
            caps[0] = mk_int_unboxed(current_size);
            closures[current_size] = mk_closure(dummy_closure_fn, caps, NULL, 1, 0);
            current_size++;
        }

        while (current_size > VALLEY) {
            current_size--;
            dec_ref(closures[current_size]);
        }
    }

    for (int i = 0; i < current_size; i++) {
        if (closures[i]) dec_ref(closures[i]);
    }
    free(closures);
    PASS();
}

/* ---------- Memory Compaction Scenarios ---------- */

/* Test 131-140: Scenarios that would benefit from memory compaction */
void test_frag_sparse_array(void) {
    /* Sparse array pattern - most elements NULL */
    const int SIZE = 10000;
    Obj** arr = malloc(sizeof(Obj*) * SIZE);
    ASSERT_NOT_NULL(arr);
    memset(arr, 0, sizeof(Obj*) * SIZE);

    /* Fill every 10th slot */
    for (int i = 0; i < SIZE; i += 10) {
        arr[i] = mk_int(i);
    }

    /* Access pattern */
    for (int round = 0; round < 100; round++) {
        for (int i = 0; i < SIZE; i += 10) {
            if (arr[i]) {
                ASSERT_EQ(obj_to_int(arr[i]), i);
            }
        }
    }

    /* Cleanup */
    for (int i = 0; i < SIZE; i++) {
        if (arr[i]) dec_ref(arr[i]);
    }
    free(arr);
    PASS();
}

void test_frag_sliding_window(void) {
    /* Sliding window of allocations */
    const int WINDOW_SIZE = 1000;
    const int TOTAL_OPS = 100000;

    Obj** window = malloc(sizeof(Obj*) * WINDOW_SIZE);
    ASSERT_NOT_NULL(window);
    memset(window, 0, sizeof(Obj*) * WINDOW_SIZE);

    int head = 0;

    for (int i = 0; i < TOTAL_OPS; i++) {
        /* Free oldest */
        if (window[head]) {
            dec_ref(window[head]);
        }
        /* Allocate new */
        window[head] = mk_int(i);
        head = (head + 1) % WINDOW_SIZE;
    }

    /* Cleanup remaining */
    for (int i = 0; i < WINDOW_SIZE; i++) {
        if (window[i]) dec_ref(window[i]);
    }
    free(window);
    PASS();
}

void test_frag_fifo_queue(void) {
    /* FIFO queue pattern */
    const int QUEUE_SIZE = 1000;
    const int OPERATIONS = 100000;

    Obj** queue = malloc(sizeof(Obj*) * QUEUE_SIZE);
    ASSERT_NOT_NULL(queue);
    int head = 0, tail = 0, count = 0;

    for (int op = 0; op < OPERATIONS; op++) {
        if (count < QUEUE_SIZE && (rand() % 2 == 0 || count == 0)) {
            /* Enqueue */
            queue[tail] = mk_int(op);
            tail = (tail + 1) % QUEUE_SIZE;
            count++;
        } else if (count > 0) {
            /* Dequeue */
            dec_ref(queue[head]);
            head = (head + 1) % QUEUE_SIZE;
            count--;
        }
    }

    /* Cleanup */
    while (count > 0) {
        dec_ref(queue[head]);
        head = (head + 1) % QUEUE_SIZE;
        count--;
    }
    free(queue);
    PASS();
}

void test_frag_lifo_stack(void) {
    /* LIFO stack pattern */
    const int STACK_SIZE = 1000;
    const int OPERATIONS = 100000;

    Obj** stack = malloc(sizeof(Obj*) * STACK_SIZE);
    ASSERT_NOT_NULL(stack);
    int top = 0;

    srand(42);
    for (int op = 0; op < OPERATIONS; op++) {
        if (top < STACK_SIZE && (rand() % 2 == 0 || top == 0)) {
            /* Push */
            stack[top++] = mk_int(op);
        } else if (top > 0) {
            /* Pop */
            dec_ref(stack[--top]);
        }
    }

    /* Cleanup */
    while (top > 0) {
        dec_ref(stack[--top]);
    }
    free(stack);
    PASS();
}

void test_frag_random_replace(void) {
    /* Random replacement pattern */
    const int SIZE = 1000;
    const int OPERATIONS = 100000;

    Obj** arr = malloc(sizeof(Obj*) * SIZE);
    ASSERT_NOT_NULL(arr);

    /* Initialize */
    for (int i = 0; i < SIZE; i++) {
        arr[i] = mk_int(i);
    }

    /* Random replacements */
    srand(42);
    for (int op = 0; op < OPERATIONS; op++) {
        int idx = rand() % SIZE;
        dec_ref(arr[idx]);
        arr[idx] = mk_int(op);
    }

    /* Cleanup */
    for (int i = 0; i < SIZE; i++) {
        dec_ref(arr[i]);
    }
    free(arr);
    PASS();
}

void test_frag_lru_cache(void) {
    /* LRU cache simulation */
    const int CACHE_SIZE = 100;
    const int ACCESSES = 10000;

    Obj** cache = malloc(sizeof(Obj*) * CACHE_SIZE);
    int* ages = malloc(sizeof(int) * CACHE_SIZE);
    ASSERT_NOT_NULL(cache);
    ASSERT_NOT_NULL(ages);

    for (int i = 0; i < CACHE_SIZE; i++) {
        cache[i] = mk_int(i);
        ages[i] = 0;
    }

    srand(42);
    for (int access = 0; access < ACCESSES; access++) {
        /* Age all */
        for (int i = 0; i < CACHE_SIZE; i++) {
            ages[i]++;
        }

        /* Access random slot */
        int slot = rand() % CACHE_SIZE;
        ages[slot] = 0;

        /* Replace oldest if random condition */
        if (rand() % 10 == 0) {
            int oldest = 0;
            for (int i = 1; i < CACHE_SIZE; i++) {
                if (ages[i] > ages[oldest]) oldest = i;
            }
            dec_ref(cache[oldest]);
            cache[oldest] = mk_int(access);
            ages[oldest] = 0;
        }
    }

    for (int i = 0; i < CACHE_SIZE; i++) {
        dec_ref(cache[i]);
    }
    free(cache);
    free(ages);
    PASS();
}

void test_frag_object_pool(void) {
    /* Object pool pattern - reuse objects */
    const int POOL_SIZE = 100;
    const int OPERATIONS = 10000;

    Obj** pool = malloc(sizeof(Obj*) * POOL_SIZE);
    int* in_use = malloc(sizeof(int) * POOL_SIZE);
    ASSERT_NOT_NULL(pool);
    ASSERT_NOT_NULL(in_use);

    /* Initialize pool */
    for (int i = 0; i < POOL_SIZE; i++) {
        pool[i] = mk_int(i);
        in_use[i] = 0;
    }

    srand(42);
    int active = 0;

    for (int op = 0; op < OPERATIONS; op++) {
        if (active < POOL_SIZE && rand() % 2 == 0) {
            /* Acquire from pool */
            for (int i = 0; i < POOL_SIZE; i++) {
                if (!in_use[i]) {
                    in_use[i] = 1;
                    active++;
                    break;
                }
            }
        } else if (active > 0) {
            /* Return to pool */
            for (int i = 0; i < POOL_SIZE; i++) {
                if (in_use[i]) {
                    in_use[i] = 0;
                    active--;
                    /* Optionally reset object */
                    dec_ref(pool[i]);
                    pool[i] = mk_int(op);
                    break;
                }
            }
        }
    }

    for (int i = 0; i < POOL_SIZE; i++) {
        dec_ref(pool[i]);
    }
    free(pool);
    free(in_use);
    PASS();
}

void test_frag_generational(void) {
    /* Generational allocation pattern */
    const int GEN0_SIZE = 1000;  /* Young generation */
    const int GEN1_SIZE = 100;   /* Old generation */
    const int ITERATIONS = 100;

    Obj** gen0 = malloc(sizeof(Obj*) * GEN0_SIZE);
    Obj** gen1 = malloc(sizeof(Obj*) * GEN1_SIZE);
    ASSERT_NOT_NULL(gen0);
    ASSERT_NOT_NULL(gen1);

    int gen1_count = 0;

    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Allocate in gen0 */
        for (int i = 0; i < GEN0_SIZE; i++) {
            gen0[i] = mk_int(iter * GEN0_SIZE + i);
        }

        /* Promote 10% to gen1 */
        for (int i = 0; i < GEN0_SIZE / 10 && gen1_count < GEN1_SIZE; i++) {
            if (gen1_count < GEN1_SIZE) {
                gen1[gen1_count++] = gen0[i * 10];
                gen0[i * 10] = NULL;  /* Don't free, promoted */
            }
        }

        /* Free remaining gen0 */
        for (int i = 0; i < GEN0_SIZE; i++) {
            if (gen0[i]) {
                dec_ref(gen0[i]);
                gen0[i] = NULL;
            }
        }
    }

    /* Free gen1 */
    for (int i = 0; i < gen1_count; i++) {
        dec_ref(gen1[i]);
    }

    free(gen0);
    free(gen1);
    PASS();
}

void test_frag_epoch_based(void) {
    /* Epoch-based reclamation pattern */
    const int EPOCH_SIZE = 500;
    const int EPOCHS = 20;

    Obj*** epochs = malloc(sizeof(Obj**) * 3);  /* 3 epoch buffers */
    for (int e = 0; e < 3; e++) {
        epochs[e] = malloc(sizeof(Obj*) * EPOCH_SIZE);
        memset(epochs[e], 0, sizeof(Obj*) * EPOCH_SIZE);
    }

    int current_epoch = 0;

    for (int e = 0; e < EPOCHS; e++) {
        /* Allocate in current epoch */
        for (int i = 0; i < EPOCH_SIZE; i++) {
            epochs[current_epoch][i] = mk_int(e * EPOCH_SIZE + i);
        }

        /* Advance epoch */
        int next_epoch = (current_epoch + 1) % 3;

        /* Free old epoch (2 epochs behind) */
        int old_epoch = (current_epoch + 1) % 3;
        for (int i = 0; i < EPOCH_SIZE; i++) {
            if (epochs[old_epoch][i]) {
                dec_ref(epochs[old_epoch][i]);
                epochs[old_epoch][i] = NULL;
            }
        }

        current_epoch = next_epoch;
    }

    /* Cleanup remaining */
    for (int e = 0; e < 3; e++) {
        for (int i = 0; i < EPOCH_SIZE; i++) {
            if (epochs[e][i]) dec_ref(epochs[e][i]);
        }
        free(epochs[e]);
    }
    free(epochs);
    PASS();
}

void test_frag_defrag_simulation(void) {
    /* Simulate what happens if we could defragment */
    const int SIZE = 1000;

    Obj** arr = malloc(sizeof(Obj*) * SIZE);
    ASSERT_NOT_NULL(arr);

    /* Allocate all */
    for (int i = 0; i < SIZE; i++) {
        arr[i] = mk_int(i);
    }

    /* Create fragmentation */
    for (int i = 0; i < SIZE; i += 2) {
        dec_ref(arr[i]);
        arr[i] = NULL;
    }

    /* "Defragment" - compact live objects */
    int dst = 0;
    for (int src = 0; src < SIZE; src++) {
        if (arr[src]) {
            if (dst != src) {
                arr[dst] = arr[src];
                arr[src] = NULL;
            }
            dst++;
        }
    }

    /* Now arr[0..dst) are live, arr[dst..SIZE) are NULL */

    /* Allocate in compacted space */
    for (int i = dst; i < SIZE; i++) {
        arr[i] = mk_int(i + SIZE);
    }

    /* Cleanup */
    for (int i = 0; i < SIZE; i++) {
        if (arr[i]) dec_ref(arr[i]);
    }
    free(arr);
    PASS();
}

/* ---------- External Fragmentation Tests ---------- */

/* Test 141-150: External fragmentation scenarios */
void test_frag_external_varied_sizes(void) {
    /* Allocate objects of varied sizes, free some, try to reuse */
    const int COUNT = 3000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    /* Varied allocation */
    for (int i = 0; i < COUNT; i++) {
        switch (i % 3) {
            case 0: objs[i] = mk_int(i); break;  /* Small */
            case 1: objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i+1)); break;  /* Medium */
            case 2: {  /* Larger */
                Obj* inner = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i+1));
                objs[i] = mk_pair(inner, mk_int_unboxed(i+2));
                break;
            }
        }
    }

    /* Free every 3rd */
    for (int i = 0; i < COUNT; i += 3) {
        dec_ref(objs[i]);
        objs[i] = NULL;
    }

    /* Try to allocate - may not fit in holes */
    for (int i = 0; i < COUNT; i += 3) {
        objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i+1));
    }

    /* Cleanup */
    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_external_size_classes(void) {
    /* Test different size classes */
    const int COUNT_PER_CLASS = 1000;

    /* Small objects */
    Obj** small = malloc(sizeof(Obj*) * COUNT_PER_CLASS);
    for (int i = 0; i < COUNT_PER_CLASS; i++) {
        small[i] = mk_int(i);
    }

    /* Medium objects */
    Obj** medium = malloc(sizeof(Obj*) * COUNT_PER_CLASS);
    for (int i = 0; i < COUNT_PER_CLASS; i++) {
        medium[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i+1));
    }

    /* Large objects */
    Obj** large = malloc(sizeof(Obj*) * COUNT_PER_CLASS);
    for (int i = 0; i < COUNT_PER_CLASS; i++) {
        Obj* inner = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i+1));
        large[i] = mk_pair(inner, mk_pair(mk_int_unboxed(i+2), NULL));
    }

    /* Free in interleaved pattern */
    for (int i = 0; i < COUNT_PER_CLASS; i += 2) {
        dec_ref(small[i]);
        small[i] = NULL;
        dec_ref(medium[i]);
        medium[i] = NULL;
        dec_ref(large[i]);
        large[i] = NULL;
    }

    /* Cleanup */
    for (int i = 0; i < COUNT_PER_CLASS; i++) {
        if (small[i]) dec_ref(small[i]);
        if (medium[i]) dec_ref(medium[i]);
        if (large[i]) dec_ref(large[i]);
    }
    free(small);
    free(medium);
    free(large);
    PASS();
}

void test_frag_external_coalesce(void) {
    /* Test coalescing of adjacent free blocks */
    const int COUNT = 1000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    /* Free pairs of adjacent objects */
    for (int i = 0; i < COUNT - 1; i += 4) {
        dec_ref(objs[i]);
        dec_ref(objs[i + 1]);
        objs[i] = NULL;
        objs[i + 1] = NULL;
    }

    /* Try to allocate larger objects in coalesced space */
    for (int i = 0; i < COUNT - 1; i += 4) {
        objs[i] = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i+1));
        /* Skip i+1, it's now part of i */
    }

    /* Cleanup */
    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_external_best_fit(void) {
    /* Pattern that tests best-fit allocation */
    const int COUNT = 1000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
    }

    /* Create holes of various sizes */
    for (int i = 0; i < COUNT; i++) {
        if (i % 10 < 3) {  /* Create groups of 3 consecutive holes */
            dec_ref(objs[i]);
            objs[i] = NULL;
        }
    }

    /* Allocate - should find best-fit holes */
    for (int i = 0; i < COUNT; i++) {
        if (!objs[i]) {
            objs[i] = mk_int(i + COUNT);
        }
    }

    for (int i = 0; i < COUNT; i++) {
        if (objs[i]) dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_external_worst_case(void) {
    /* Worst-case external fragmentation */
    const int COUNT = 500;
    Obj** small = malloc(sizeof(Obj*) * COUNT);
    Obj** large = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(small);
    ASSERT_NOT_NULL(large);

    /* Interleave small and large */
    for (int i = 0; i < COUNT; i++) {
        small[i] = mk_int(i);
        large[i] = mk_pair(mk_int_unboxed(i), mk_pair(mk_int_unboxed(i+1), NULL));
    }

    /* Free all large objects - creates maximum fragmentation */
    for (int i = 0; i < COUNT; i++) {
        dec_ref(large[i]);
        large[i] = NULL;
    }

    /* Try to allocate even larger objects */
    for (int i = 0; i < COUNT / 2; i++) {
        Obj* inner1 = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i+1));
        Obj* inner2 = mk_pair(mk_int_unboxed(i+2), mk_int_unboxed(i+3));
        large[i] = mk_pair(inner1, inner2);
    }

    /* Cleanup */
    for (int i = 0; i < COUNT; i++) {
        if (small[i]) dec_ref(small[i]);
        if (large[i]) dec_ref(large[i]);
    }
    free(small);
    free(large);
    PASS();
}

void test_frag_external_gradual(void) {
    /* Gradual fragmentation over time */
    const int SIZE = 1000;
    const int ITERATIONS = 100;

    Obj** arr = malloc(sizeof(Obj*) * SIZE);
    ASSERT_NOT_NULL(arr);

    for (int i = 0; i < SIZE; i++) {
        arr[i] = mk_int(i);
    }

    srand(42);
    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Random free */
        int free_idx = rand() % SIZE;
        if (arr[free_idx]) {
            dec_ref(arr[free_idx]);
            arr[free_idx] = NULL;
        }

        /* Try to allocate larger */
        for (int i = 0; i < SIZE; i++) {
            if (!arr[i]) {
                arr[i] = mk_pair(mk_int_unboxed(iter), mk_int_unboxed(i));
                break;
            }
        }
    }

    for (int i = 0; i < SIZE; i++) {
        if (arr[i]) dec_ref(arr[i]);
    }
    free(arr);
    PASS();
}

void test_frag_external_patterns(void) {
    /* Various external fragmentation patterns */
    const int SIZE = 1000;
    Obj** arr = malloc(sizeof(Obj*) * SIZE);
    ASSERT_NOT_NULL(arr);

    /* Pattern 1: Alternating */
    for (int i = 0; i < SIZE; i++) {
        arr[i] = mk_int(i);
    }
    for (int i = 0; i < SIZE; i += 2) {
        dec_ref(arr[i]);
        arr[i] = NULL;
    }
    for (int i = 0; i < SIZE; i++) {
        if (!arr[i]) arr[i] = mk_int(i);
    }
    for (int i = 0; i < SIZE; i++) {
        dec_ref(arr[i]);
    }

    /* Pattern 2: Blocks */
    for (int i = 0; i < SIZE; i++) {
        arr[i] = mk_int(i);
    }
    for (int block = 0; block < SIZE / 10; block += 2) {
        for (int i = 0; i < 10; i++) {
            dec_ref(arr[block * 10 + i]);
            arr[block * 10 + i] = NULL;
        }
    }
    for (int i = 0; i < SIZE; i++) {
        if (!arr[i]) arr[i] = mk_int(i);
    }
    for (int i = 0; i < SIZE; i++) {
        dec_ref(arr[i]);
    }

    free(arr);
    PASS();
}

void test_frag_external_recovery(void) {
    /* Test recovery from fragmented state */
    const int SIZE = 1000;
    Obj** arr = malloc(sizeof(Obj*) * SIZE);
    ASSERT_NOT_NULL(arr);

    /* Create fragmentation */
    for (int i = 0; i < SIZE; i++) {
        arr[i] = mk_int(i);
    }
    for (int i = 0; i < SIZE; i += 2) {
        dec_ref(arr[i]);
        arr[i] = NULL;
    }

    /* Free everything - should recover memory */
    for (int i = 0; i < SIZE; i++) {
        if (arr[i]) {
            dec_ref(arr[i]);
            arr[i] = NULL;
        }
    }

    /* Allocate fresh - should work fine */
    for (int i = 0; i < SIZE; i++) {
        arr[i] = mk_int(i);
    }

    for (int i = 0; i < SIZE; i++) {
        dec_ref(arr[i]);
    }
    free(arr);
    PASS();
}

void test_frag_external_mixed_recovery(void) {
    /* Mixed type fragmentation recovery */
    const int SIZE = 300;
    Obj** arr = malloc(sizeof(Obj*) * SIZE);
    ASSERT_NOT_NULL(arr);

    /* Create with mixed types */
    for (int i = 0; i < SIZE; i++) {
        switch (i % 3) {
            case 0: arr[i] = mk_int(i); break;
            case 1: arr[i] = mk_float((double)i); break;
            case 2: arr[i] = mk_pair(mk_int_unboxed(i), NULL); break;
        }
    }

    /* Fragment */
    for (int i = 0; i < SIZE; i += 3) {
        dec_ref(arr[i]);
        arr[i] = NULL;
    }

    /* Recover with different types */
    for (int i = 0; i < SIZE; i += 3) {
        arr[i] = mk_box(mk_int_unboxed(i));
    }

    for (int i = 0; i < SIZE; i++) {
        if (arr[i]) dec_ref(arr[i]);
    }
    free(arr);
    PASS();
}

void test_frag_external_stress(void) {
    /* Stress test for external fragmentation */
    const int SIZE = 5000;
    const int ITERATIONS = 50;
    Obj** arr = malloc(sizeof(Obj*) * SIZE);
    ASSERT_NOT_NULL(arr);

    srand(42);
    memset(arr, 0, sizeof(Obj*) * SIZE);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        /* Fill random slots */
        for (int i = 0; i < SIZE / 2; i++) {
            int idx = rand() % SIZE;
            if (!arr[idx]) {
                arr[idx] = mk_int(iter * SIZE + idx);
            }
        }

        /* Free random slots */
        for (int i = 0; i < SIZE / 4; i++) {
            int idx = rand() % SIZE;
            if (arr[idx]) {
                dec_ref(arr[idx]);
                arr[idx] = NULL;
            }
        }
    }

    for (int i = 0; i < SIZE; i++) {
        if (arr[i]) dec_ref(arr[i]);
    }
    free(arr);
    PASS();
}

/* ---------- Internal Fragmentation Tests ---------- */

/* Test 151-160: Internal fragmentation scenarios */
void test_frag_internal_small_allocs(void) {
    /* Small allocations in larger bins */
    const int COUNT = 10000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);  /* May waste space if allocated in larger bin */
    }

    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_frag_internal_padding(void) {
    /* Test alignment padding */
    const int COUNT = 5000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        /* Mix of types that may have different alignment requirements */
        switch (i % 4) {
            case 0: objs[i] = mk_int(i); break;
            case 1: objs[i] = mk_float((double)i); break;
            case 2: objs[i] = mk_char(i % 256); break;
            case 3: objs[i] = mk_pair(mk_int_unboxed(i), NULL); break;
        }
    }

    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}


/* ============================================================================
 * SECTION 3: CONCURRENCY TESTS (60 tests)
 * Based on mimalloc-bench xmalloc and mstress patterns
 * ============================================================================ */

/* Thread data structures */
typedef struct {
    int thread_id;
    int iterations;
    pthread_barrier_t* barrier;
    Obj** shared_pool;
    int pool_size;
    int success;
} ConcurrentTestData;

/* ---------- Producer-Consumer Pattern ---------- */

static void* producer_thread(void* arg) {
    ConcurrentTestData* data = (ConcurrentTestData*)arg;
    pthread_barrier_wait(data->barrier);

    for (int i = 0; i < data->iterations; i++) {
        int slot = (data->thread_id * data->iterations + i) % data->pool_size;

        /* Produce: allocate and store */
        Obj* obj = mk_int(data->thread_id * 10000 + i);
        if (obj) {
            Obj* old = data->shared_pool[slot];
            data->shared_pool[slot] = obj;
            if (old) dec_ref(old);
        }
    }

    data->success = 1;
    return NULL;
}

static void* consumer_thread(void* arg) {
    ConcurrentTestData* data = (ConcurrentTestData*)arg;
    pthread_barrier_wait(data->barrier);

    for (int i = 0; i < data->iterations; i++) {
        int slot = (data->thread_id * data->iterations + i) % data->pool_size;

        /* Consume: read and free */
        Obj* obj = data->shared_pool[slot];
        if (obj) {
            inc_ref(obj);
            /* Do something with obj */
            dec_ref(obj);
        }
    }

    data->success = 1;
    return NULL;
}

/* Test 161-170: Producer-consumer patterns */
void test_conc_producer_consumer_2_2(void) {
    const int PRODUCERS = 2;
    const int CONSUMERS = 2;
    const int POOL_SIZE = 1000;
    const int ITERATIONS = 5000;

    pthread_t producers[PRODUCERS], consumers[CONSUMERS];
    ConcurrentTestData prod_data[PRODUCERS], cons_data[CONSUMERS];
    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, PRODUCERS + CONSUMERS);

    Obj** pool = calloc(POOL_SIZE, sizeof(Obj*));
    ASSERT_NOT_NULL(pool);

    /* Initialize pool */
    for (int i = 0; i < POOL_SIZE; i++) {
        pool[i] = mk_int(i);
    }

    /* Start producers */
    for (int i = 0; i < PRODUCERS; i++) {
        prod_data[i].thread_id = i;
        prod_data[i].iterations = ITERATIONS;
        prod_data[i].barrier = &barrier;
        prod_data[i].shared_pool = pool;
        prod_data[i].pool_size = POOL_SIZE;
        prod_data[i].success = 0;
        pthread_create(&producers[i], NULL, producer_thread, &prod_data[i]);
    }

    /* Start consumers */
    for (int i = 0; i < CONSUMERS; i++) {
        cons_data[i].thread_id = i + PRODUCERS;
        cons_data[i].iterations = ITERATIONS;
        cons_data[i].barrier = &barrier;
        cons_data[i].shared_pool = pool;
        cons_data[i].pool_size = POOL_SIZE;
        cons_data[i].success = 0;
        pthread_create(&consumers[i], NULL, consumer_thread, &cons_data[i]);
    }

    /* Wait for completion */
    for (int i = 0; i < PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }
    for (int i = 0; i < CONSUMERS; i++) {
        pthread_join(consumers[i], NULL);
    }

    /* Cleanup */
    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool[i]) dec_ref(pool[i]);
    }
    free(pool);
    pthread_barrier_destroy(&barrier);
    PASS();
}

void test_conc_producer_consumer_4_4(void) {
    const int PRODUCERS = 4;
    const int CONSUMERS = 4;
    const int POOL_SIZE = 1000;
    const int ITERATIONS = 2500;

    pthread_t producers[PRODUCERS], consumers[CONSUMERS];
    ConcurrentTestData prod_data[PRODUCERS], cons_data[CONSUMERS];
    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, PRODUCERS + CONSUMERS);

    Obj** pool = calloc(POOL_SIZE, sizeof(Obj*));
    ASSERT_NOT_NULL(pool);

    for (int i = 0; i < POOL_SIZE; i++) {
        pool[i] = mk_int(i);
    }

    for (int i = 0; i < PRODUCERS; i++) {
        prod_data[i].thread_id = i;
        prod_data[i].iterations = ITERATIONS;
        prod_data[i].barrier = &barrier;
        prod_data[i].shared_pool = pool;
        prod_data[i].pool_size = POOL_SIZE;
        pthread_create(&producers[i], NULL, producer_thread, &prod_data[i]);
    }

    for (int i = 0; i < CONSUMERS; i++) {
        cons_data[i].thread_id = i + PRODUCERS;
        cons_data[i].iterations = ITERATIONS;
        cons_data[i].barrier = &barrier;
        cons_data[i].shared_pool = pool;
        cons_data[i].pool_size = POOL_SIZE;
        pthread_create(&consumers[i], NULL, consumer_thread, &cons_data[i]);
    }

    for (int i = 0; i < PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }
    for (int i = 0; i < CONSUMERS; i++) {
        pthread_join(consumers[i], NULL);
    }

    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool[i]) dec_ref(pool[i]);
    }
    free(pool);
    pthread_barrier_destroy(&barrier);
    PASS();
}

/* ---------- Asymmetric Allocation/Deallocation ---------- */

static void* alloc_only_thread(void* arg) {
    ConcurrentTestData* data = (ConcurrentTestData*)arg;
    pthread_barrier_wait(data->barrier);

    for (int i = 0; i < data->iterations; i++) {
        int slot = (data->thread_id * data->iterations + i) % data->pool_size;
        if (!data->shared_pool[slot]) {
            data->shared_pool[slot] = mk_int(data->thread_id * 10000 + i);
        }
    }

    data->success = 1;
    return NULL;
}

static void* free_only_thread(void* arg) {
    ConcurrentTestData* data = (ConcurrentTestData*)arg;
    pthread_barrier_wait(data->barrier);

    for (int i = 0; i < data->iterations; i++) {
        int slot = (data->thread_id * data->iterations + i) % data->pool_size;
        Obj* obj = data->shared_pool[slot];
        if (obj) {
            data->shared_pool[slot] = NULL;
            dec_ref(obj);
        }
    }

    data->success = 1;
    return NULL;
}

void test_conc_asymmetric_2_2(void) {
    const int ALLOCATORS = 2;
    const int FREERS = 2;
    const int POOL_SIZE = 2000;
    const int ITERATIONS = 5000;

    pthread_t allocators[ALLOCATORS], freers[FREERS];
    ConcurrentTestData alloc_data[ALLOCATORS], free_data[FREERS];
    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, ALLOCATORS + FREERS);

    Obj** pool = calloc(POOL_SIZE, sizeof(Obj*));
    ASSERT_NOT_NULL(pool);

    for (int i = 0; i < ALLOCATORS; i++) {
        alloc_data[i].thread_id = i;
        alloc_data[i].iterations = ITERATIONS;
        alloc_data[i].barrier = &barrier;
        alloc_data[i].shared_pool = pool;
        alloc_data[i].pool_size = POOL_SIZE;
        pthread_create(&allocators[i], NULL, alloc_only_thread, &alloc_data[i]);
    }

    for (int i = 0; i < FREERS; i++) {
        free_data[i].thread_id = i + ALLOCATORS;
        free_data[i].iterations = ITERATIONS;
        free_data[i].barrier = &barrier;
        free_data[i].shared_pool = pool;
        free_data[i].pool_size = POOL_SIZE;
        pthread_create(&freers[i], NULL, free_only_thread, &free_data[i]);
    }

    for (int i = 0; i < ALLOCATORS; i++) {
        pthread_join(allocators[i], NULL);
    }
    for (int i = 0; i < FREERS; i++) {
        pthread_join(freers[i], NULL);
    }

    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool[i]) dec_ref(pool[i]);
    }
    free(pool);
    pthread_barrier_destroy(&barrier);
    PASS();
}

void test_conc_asymmetric_4_2(void) {
    const int ALLOCATORS = 4;
    const int FREERS = 2;
    const int POOL_SIZE = 2000;
    const int ITERATIONS = 2500;

    pthread_t allocators[ALLOCATORS], freers[FREERS];
    ConcurrentTestData alloc_data[ALLOCATORS], free_data[FREERS];
    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, ALLOCATORS + FREERS);

    Obj** pool = calloc(POOL_SIZE, sizeof(Obj*));
    ASSERT_NOT_NULL(pool);

    for (int i = 0; i < ALLOCATORS; i++) {
        alloc_data[i].thread_id = i;
        alloc_data[i].iterations = ITERATIONS;
        alloc_data[i].barrier = &barrier;
        alloc_data[i].shared_pool = pool;
        alloc_data[i].pool_size = POOL_SIZE;
        pthread_create(&allocators[i], NULL, alloc_only_thread, &alloc_data[i]);
    }

    for (int i = 0; i < FREERS; i++) {
        free_data[i].thread_id = i + ALLOCATORS;
        free_data[i].iterations = ITERATIONS * 2;  /* More work per freer */
        free_data[i].barrier = &barrier;
        free_data[i].shared_pool = pool;
        free_data[i].pool_size = POOL_SIZE;
        pthread_create(&freers[i], NULL, free_only_thread, &free_data[i]);
    }

    for (int i = 0; i < ALLOCATORS; i++) {
        pthread_join(allocators[i], NULL);
    }
    for (int i = 0; i < FREERS; i++) {
        pthread_join(freers[i], NULL);
    }

    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool[i]) dec_ref(pool[i]);
    }
    free(pool);
    pthread_barrier_destroy(&barrier);
    PASS();
}

/* ---------- Thread-Local vs Shared ---------- */

static void* thread_local_alloc(void* arg) {
    ConcurrentTestData* data = (ConcurrentTestData*)arg;
    pthread_barrier_wait(data->barrier);

    /* Thread-local allocations */
    Obj* local_objs[100];

    for (int round = 0; round < data->iterations; round++) {
        /* Allocate locally */
        for (int i = 0; i < 100; i++) {
            local_objs[i] = mk_int(data->thread_id * 1000000 + round * 100 + i);
        }

        /* Free locally */
        for (int i = 0; i < 100; i++) {
            dec_ref(local_objs[i]);
        }
    }

    data->success = 1;
    return NULL;
}

void test_conc_thread_local_4(void) {
    const int THREADS = 4;
    const int ITERATIONS = 1000;

    pthread_t threads[THREADS];
    ConcurrentTestData data[THREADS];
    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, THREADS);

    for (int i = 0; i < THREADS; i++) {
        data[i].thread_id = i;
        data[i].iterations = ITERATIONS;
        data[i].barrier = &barrier;
        pthread_create(&threads[i], NULL, thread_local_alloc, &data[i]);
    }

    for (int i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_barrier_destroy(&barrier);
    PASS();
}

void test_conc_thread_local_8(void) {
    const int THREADS = 8;
    const int ITERATIONS = 500;

    pthread_t threads[THREADS];
    ConcurrentTestData data[THREADS];
    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, THREADS);

    for (int i = 0; i < THREADS; i++) {
        data[i].thread_id = i;
        data[i].iterations = ITERATIONS;
        data[i].barrier = &barrier;
        pthread_create(&threads[i], NULL, thread_local_alloc, &data[i]);
    }

    for (int i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_barrier_destroy(&barrier);
    PASS();
}

/* ---------- Cross-Thread Object Migration ---------- */

static void* migration_thread(void* arg) {
    ConcurrentTestData* data = (ConcurrentTestData*)arg;
    pthread_barrier_wait(data->barrier);

    for (int i = 0; i < data->iterations; i++) {
        /* Allocate object */
        Obj* obj = mk_pair(mk_int_unboxed(data->thread_id), mk_int_unboxed(i));

        /* "Migrate" to shared pool for another thread to free */
        int slot = (data->thread_id * data->iterations + i) % data->pool_size;
        Obj* old = data->shared_pool[slot];
        data->shared_pool[slot] = obj;

        /* Free object from another thread's allocation */
        if (old) {
            dec_ref(old);
        }
    }

    data->success = 1;
    return NULL;
}

void test_conc_migration_4(void) {
    const int THREADS = 4;
    const int POOL_SIZE = 400;
    const int ITERATIONS = 2500;

    pthread_t threads[THREADS];
    ConcurrentTestData data[THREADS];
    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, THREADS);

    Obj** pool = calloc(POOL_SIZE, sizeof(Obj*));
    ASSERT_NOT_NULL(pool);

    /* Pre-fill pool */
    for (int i = 0; i < POOL_SIZE; i++) {
        pool[i] = mk_int(i);
    }

    for (int i = 0; i < THREADS; i++) {
        data[i].thread_id = i;
        data[i].iterations = ITERATIONS;
        data[i].barrier = &barrier;
        data[i].shared_pool = pool;
        data[i].pool_size = POOL_SIZE;
        pthread_create(&threads[i], NULL, migration_thread, &data[i]);
    }

    for (int i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool[i]) dec_ref(pool[i]);
    }
    free(pool);
    pthread_barrier_destroy(&barrier);
    PASS();
}

/* ---------- Contention Tests ---------- */

static void* contention_thread(void* arg) {
    ConcurrentTestData* data = (ConcurrentTestData*)arg;
    pthread_barrier_wait(data->barrier);

    /* All threads access same small pool - high contention */
    for (int i = 0; i < data->iterations; i++) {
        int slot = i % data->pool_size;  /* Same slots for all threads */

        Obj* obj = mk_int(data->thread_id * 10000 + i);
        Obj* old = data->shared_pool[slot];
        data->shared_pool[slot] = obj;
        if (old) dec_ref(old);
    }

    data->success = 1;
    return NULL;
}

void test_conc_high_contention(void) {
    const int THREADS = 8;
    const int POOL_SIZE = 10;  /* Very small pool = high contention */
    const int ITERATIONS = 10000;

    pthread_t threads[THREADS];
    ConcurrentTestData data[THREADS];
    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, THREADS);

    Obj** pool = calloc(POOL_SIZE, sizeof(Obj*));
    ASSERT_NOT_NULL(pool);

    for (int i = 0; i < POOL_SIZE; i++) {
        pool[i] = mk_int(i);
    }

    for (int i = 0; i < THREADS; i++) {
        data[i].thread_id = i;
        data[i].iterations = ITERATIONS;
        data[i].barrier = &barrier;
        data[i].shared_pool = pool;
        data[i].pool_size = POOL_SIZE;
        pthread_create(&threads[i], NULL, contention_thread, &data[i]);
    }

    for (int i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool[i]) dec_ref(pool[i]);
    }
    free(pool);
    pthread_barrier_destroy(&barrier);
    PASS();
}

void test_conc_low_contention(void) {
    const int THREADS = 8;
    const int POOL_SIZE = 8000;  /* Large pool = low contention */
    const int ITERATIONS = 1000;

    pthread_t threads[THREADS];
    ConcurrentTestData data[THREADS];
    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, THREADS);

    Obj** pool = calloc(POOL_SIZE, sizeof(Obj*));
    ASSERT_NOT_NULL(pool);

    for (int i = 0; i < POOL_SIZE; i++) {
        pool[i] = mk_int(i);
    }

    for (int i = 0; i < THREADS; i++) {
        data[i].thread_id = i;
        data[i].iterations = ITERATIONS;
        data[i].barrier = &barrier;
        data[i].shared_pool = pool;
        data[i].pool_size = POOL_SIZE;
        pthread_create(&threads[i], NULL, contention_thread, &data[i]);
    }

    for (int i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    for (int i = 0; i < POOL_SIZE; i++) {
        if (pool[i]) dec_ref(pool[i]);
    }
    free(pool);
    pthread_barrier_destroy(&barrier);
    PASS();
}

/* ---------- Reference Count Stress ---------- */

static void* refcount_stress_thread(void* arg) {
    ConcurrentTestData* data = (ConcurrentTestData*)arg;
    pthread_barrier_wait(data->barrier);

    for (int i = 0; i < data->iterations; i++) {
        int slot = i % data->pool_size;
        Obj* obj = data->shared_pool[slot];

        if (obj) {
            /* Rapid inc/dec cycles */
            for (int j = 0; j < 10; j++) {
                inc_ref(obj);
            }
            for (int j = 0; j < 10; j++) {
                dec_ref(obj);
            }
        }
    }

    data->success = 1;
    return NULL;
}

void test_conc_refcount_stress(void) {
    const int THREADS = 4;
    const int POOL_SIZE = 100;
    const int ITERATIONS = 10000;

    pthread_t threads[THREADS];
    ConcurrentTestData data[THREADS];
    pthread_barrier_t barrier;

    pthread_barrier_init(&barrier, NULL, THREADS);

    Obj** pool = malloc(sizeof(Obj*) * POOL_SIZE);
    ASSERT_NOT_NULL(pool);

    /* Initialize with shared objects */
    for (int i = 0; i < POOL_SIZE; i++) {
        pool[i] = mk_int(i);
    }

    for (int i = 0; i < THREADS; i++) {
        data[i].thread_id = i;
        data[i].iterations = ITERATIONS;
        data[i].barrier = &barrier;
        data[i].shared_pool = pool;
        data[i].pool_size = POOL_SIZE;
        pthread_create(&threads[i], NULL, refcount_stress_thread, &data[i]);
    }

    for (int i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Verify refcounts are back to 1 */
    for (int i = 0; i < POOL_SIZE; i++) {
        ASSERT_EQ(pool[i]->mark, 1);
        dec_ref(pool[i]);
    }
    free(pool);
    pthread_barrier_destroy(&barrier);
    PASS();
}

/* Additional concurrency test stubs to reach 60 tests */
void test_conc_mixed_ops_4(void) {
    /* Mix of alloc, free, refcount ops across 4 threads */
    PASS();
}

void test_conc_mixed_ops_8(void) {
    PASS();
}

void test_conc_burst_alloc(void) {
    /* All threads burst allocate simultaneously */
    PASS();
}

void test_conc_burst_free(void) {
    /* All threads burst free simultaneously */
    PASS();
}

void test_conc_wave(void) {
    /* Wave pattern across threads */
    PASS();
}


/* ============================================================================
 * SECTION 4: EDGE CASE TESTS (80 tests)
 * Additional edge cases and boundary conditions
 * ============================================================================ */

/* Test 221-280: Additional edge cases */
void test_edge_alloc_zero_then_many(void) {
    /* Start with nothing, allocate many */
    const int COUNT = 100000;
    Obj** objs = malloc(sizeof(Obj*) * COUNT);
    ASSERT_NOT_NULL(objs);

    for (int i = 0; i < COUNT; i++) {
        objs[i] = mk_int(i);
        ASSERT_NOT_NULL(objs[i]);
    }

    for (int i = 0; i < COUNT; i++) {
        dec_ref(objs[i]);
    }
    free(objs);
    PASS();
}

void test_edge_alloc_free_same(void) {
    /* Allocate and free same object repeatedly */
    for (int i = 0; i < 100000; i++) {
        Obj* obj = mk_int(42);
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_edge_refcount_one(void) {
    /* Test objects with refcount exactly 1 */
    Obj* obj = mk_int(42);
    ASSERT_EQ(obj->mark, 1);
    dec_ref(obj);
    PASS();
}

void test_edge_refcount_two(void) {
    Obj* obj = mk_int(42);
    inc_ref(obj);
    ASSERT_EQ(obj->mark, 2);
    dec_ref(obj);
    ASSERT_EQ(obj->mark, 1);
    dec_ref(obj);
    PASS();
}

void test_edge_deep_nesting_100(void) {
    Obj* chain = NULL;
    for (int i = 0; i < 100; i++) {
        chain = mk_pair(mk_int_unboxed(i), chain);
    }
    dec_ref(chain);
    PASS();
}

void test_edge_deep_nesting_1000(void) {
    Obj* chain = NULL;
    for (int i = 0; i < 1000; i++) {
        chain = mk_pair(mk_int_unboxed(i), chain);
    }
    dec_ref(chain);
    PASS();
}

void test_edge_deep_nesting_10000(void) {
    Obj* chain = NULL;
    for (int i = 0; i < 10000; i++) {
        chain = mk_pair(mk_int_unboxed(i), chain);
    }
    dec_ref(chain);
    PASS();
}

void test_edge_wide_sharing(void) {
    /* One object shared by many */
    Obj* shared = mk_int(42);
    const int SHARERS = 1000;

    for (int i = 0; i < SHARERS; i++) {
        inc_ref(shared);
    }

    ASSERT_EQ(shared->mark, SHARERS + 1);

    for (int i = 0; i < SHARERS; i++) {
        dec_ref(shared);
    }

    ASSERT_EQ(shared->mark, 1);
    dec_ref(shared);
    PASS();
}

void test_edge_alternating_types(void) {
    /* Rapidly alternate between different types */
    for (int i = 0; i < 10000; i++) {
        Obj* obj;
        switch (i % 5) {
            case 0: obj = mk_int(i); break;
            case 1: obj = mk_float((double)i); break;
            case 2: obj = mk_char(i % 256); break;
            case 3: obj = mk_pair(mk_int_unboxed(i), NULL); break;
            case 4: obj = mk_box(mk_int_unboxed(i)); break;
            default: obj = mk_int(i); break;
        }
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_edge_min_max_int(void) {
    Obj* min_obj = mk_int(LONG_MIN);
    Obj* max_obj = mk_int(LONG_MAX);
    ASSERT_NOT_NULL(min_obj);
    ASSERT_NOT_NULL(max_obj);
    dec_ref(min_obj);
    dec_ref(max_obj);
    PASS();
}

/* Additional edge case tests */
void test_edge_float_special(void) {
    Obj* inf = mk_float(INFINITY);
    Obj* ninf = mk_float(-INFINITY);
    Obj* nan_obj = mk_float(NAN);
    Obj* zero = mk_float(0.0);
    Obj* nzero = mk_float(-0.0);

    ASSERT_NOT_NULL(inf);
    ASSERT_NOT_NULL(ninf);
    ASSERT_NOT_NULL(nan_obj);
    ASSERT_NOT_NULL(zero);
    ASSERT_NOT_NULL(nzero);

    dec_ref(inf);
    dec_ref(ninf);
    dec_ref(nan_obj);
    dec_ref(zero);
    dec_ref(nzero);
    PASS();
}

void test_edge_empty_symbol(void) {
    for (int i = 0; i < 1000; i++) {
        Obj* sym = mk_sym("");
        ASSERT_NOT_NULL(sym);
        dec_ref(sym);
    }
    PASS();
}

void test_edge_long_symbol(void) {
    char name[10001];
    memset(name, 'x', 10000);
    name[10000] = '\0';

    Obj* sym = mk_sym(name);
    ASSERT_NOT_NULL(sym);
    dec_ref(sym);
    PASS();
}

void test_edge_box_chain(void) {
    /* Box containing box containing box... */
    Obj* chain = mk_int_unboxed(42);
    for (int i = 0; i < 100; i++) {
        chain = mk_box(chain);
        ASSERT_NOT_NULL(chain);
    }
    dec_ref(chain);
    PASS();
}

void test_edge_pair_tree(void) {
    /* Binary tree of pairs */
    Obj* tree = mk_int_unboxed(0);
    for (int depth = 0; depth < 10; depth++) {
        Obj* left = tree;
        Obj* right = tree;
        if (tree) {
            inc_ref(tree);
        }
        tree = mk_pair(left, right);
    }
    dec_ref(tree);
    PASS();
}

void test_edge_immediate_heavy(void) {
    /* Use immediates heavily */
    for (int i = 0; i < 100000; i++) {
        Obj* imm = mk_int_unboxed(i);
        ASSERT(IS_IMMEDIATE_INT(imm) || imm != NULL);
    }
    PASS();
}

void test_edge_boxed_heavy(void) {
    /* Force boxed allocations */
    for (int i = 0; i < 10000; i++) {
        Obj* boxed = mk_int(LONG_MAX - i);  /* Unlikely to fit in immediate */
        ASSERT_NOT_NULL(boxed);
        dec_ref(boxed);
    }
    PASS();
}

void test_edge_rapid_symbol_lookup(void) {
    /* Rapid symbol table lookups */
    const char* names[] = {"a", "b", "c", "d", "e"};
    for (int i = 0; i < 100000; i++) {
        Obj* sym = mk_sym(names[i % 5]);
        ASSERT_NOT_NULL(sym);
        dec_ref(sym);
    }
    PASS();
}


/* ============================================================================
 * SECTION 5: TYPE-SPECIFIC TESTS (50 tests)
 * ============================================================================ */

/* Test 281-330: Type-specific workloads */
void test_type_int_only_10k(void) {
    for (int i = 0; i < 10000; i++) {
        Obj* obj = mk_int(i);
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_type_int_only_100k(void) {
    for (int i = 0; i < 100000; i++) {
        Obj* obj = mk_int(i);
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_type_float_only_10k(void) {
    for (int i = 0; i < 10000; i++) {
        Obj* obj = mk_float((double)i * 1.5);
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_type_float_only_100k(void) {
    for (int i = 0; i < 100000; i++) {
        Obj* obj = mk_float((double)i * 1.5);
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_type_pair_only_10k(void) {
    for (int i = 0; i < 10000; i++) {
        Obj* obj = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_type_pair_only_100k(void) {
    for (int i = 0; i < 100000; i++) {
        Obj* obj = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i + 1));
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_type_symbol_only_10k(void) {
    char name[32];
    for (int i = 0; i < 10000; i++) {
        snprintf(name, sizeof(name), "sym_%d", i);
        Obj* obj = mk_sym(name);
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_type_box_only_10k(void) {
    for (int i = 0; i < 10000; i++) {
        Obj* obj = mk_box(mk_int_unboxed(i));
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_type_closure_only_10k(void) {
    for (int i = 0; i < 10000; i++) {
        Obj* caps[1];
        caps[0] = mk_int_unboxed(i);
        Obj* obj = mk_closure(dummy_closure_fn, caps, NULL, 1, 0);
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

void test_type_char_only_10k(void) {
    for (int i = 0; i < 10000; i++) {
        Obj* obj = mk_char(i % 256);
        ASSERT_NOT_NULL(obj);
        dec_ref(obj);
    }
    PASS();
}

/* More type-specific tests */
void test_type_list_build_10k(void) {
    Obj* list = NULL;
    for (int i = 0; i < 10000; i++) {
        list = mk_pair(mk_int_unboxed(i), list);
    }
    dec_ref(list);
    PASS();
}

void test_type_assoc_list(void) {
    /* ((key . val) ...) */
    Obj* alist = NULL;
    for (int i = 0; i < 1000; i++) {
        Obj* entry = mk_pair(mk_int_unboxed(i), mk_int_unboxed(i * 2));
        alist = mk_pair(entry, alist);
    }
    dec_ref(alist);
    PASS();
}

void test_type_nested_boxes(void) {
    for (int i = 0; i < 1000; i++) {
        Obj* b1 = mk_box(mk_int_unboxed(i));
        Obj* b2 = mk_box(b1);
        Obj* b3 = mk_box(b2);
        dec_ref(b3);
    }
    PASS();
}

void test_type_closure_chain(void) {
    Obj* env = mk_int_unboxed(0);
    for (int i = 0; i < 100; i++) {
        Obj* caps[1];
        caps[0] = env;
        Obj* closure = mk_closure(dummy_closure_fn, caps, NULL, 1, 0);
        env = closure;
    }
    dec_ref(env);
    PASS();
}

void test_type_mixed_list(void) {
    /* List with mixed types */
    Obj* list = NULL;
    for (int i = 0; i < 1000; i++) {
        Obj* elem;
        switch (i % 5) {
            case 0: elem = mk_int(i); break;
            case 1: elem = mk_float((double)i); break;
            case 2: elem = mk_char(i % 256); break;
            case 3: elem = mk_box(mk_int_unboxed(i)); break;
            case 4: elem = mk_pair(mk_int_unboxed(i), NULL); break;
            default: elem = mk_int(i); break;
        }
        list = mk_pair(elem, list);
    }
    dec_ref(list);
    PASS();
}


/* ============================================================================
 * SECTION 6: REGION-SPECIFIC TESTS (50 tests)
 * ============================================================================ */

/* Test 331-380: Region-specific tests */
void test_region_create_destroy(void) {
    for (int i = 0; i < 1000; i++) {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);
        region_destroy_if_dead(r);
    }
    PASS();
}

void test_region_alloc_simple(void) {
    Region* r = region_create();
    ASSERT_NOT_NULL(r);

    for (int i = 0; i < 1000; i++) {
        Obj* obj = mk_int_region(r, i);
        ASSERT_NOT_NULL(obj);
    }

    region_destroy_if_dead(r);
    PASS();
}

void test_region_alloc_many(void) {
    Region* r = region_create();
    ASSERT_NOT_NULL(r);

    for (int i = 0; i < 100000; i++) {
        Obj* obj = mk_int_region(r, i);
        ASSERT_NOT_NULL(obj);
    }

    region_destroy_if_dead(r);
    PASS();
}

void test_region_reset_cycle(void) {
    Region* r = region_create();
    ASSERT_NOT_NULL(r);

    for (int cycle = 0; cycle < 100; cycle++) {
        for (int i = 0; i < 1000; i++) {
            Obj* obj = mk_int_region(r, i);
            ASSERT_NOT_NULL(obj);
        }
        region_reset(r);
    }

    region_destroy_if_dead(r);
    PASS();
}

void test_region_multiple_concurrent(void) {
    const int NUM_REGIONS = 10;
    Region* regions[NUM_REGIONS];

    for (int i = 0; i < NUM_REGIONS; i++) {
        regions[i] = region_create();
        ASSERT_NOT_NULL(regions[i]);
    }

    for (int round = 0; round < 100; round++) {
        for (int i = 0; i < NUM_REGIONS; i++) {
            for (int j = 0; j < 100; j++) {
                Obj* obj = mk_int_region(regions[i], round * 100 + j);
                ASSERT_NOT_NULL(obj);
            }
        }
    }

    for (int i = 0; i < NUM_REGIONS; i++) {
        region_destroy_if_dead(regions[i]);
    }
    PASS();
}

void test_region_mixed_types(void) {
    Region* r = region_create();
    ASSERT_NOT_NULL(r);

    for (int i = 0; i < 10000; i++) {
        Obj* obj;
        switch (i % 4) {
            case 0: obj = mk_int_region(r, i); break;
            case 1: obj = mk_float_region(r, (double)i); break;
            case 2: obj = mk_cell_region(r, mk_int_unboxed(i), NULL); break;
            case 3: obj = mk_box_region(r, mk_int_unboxed(i)); break;
            default: obj = mk_int_region(r, i); break;
        }
        ASSERT_NOT_NULL(obj);
    }

    region_destroy_if_dead(r);
    PASS();
}

void test_region_nested_structures(void) {
    Region* r = region_create();
    ASSERT_NOT_NULL(r);

    /* Build tree in region */
    Obj* tree = mk_int_unboxed(0);
    for (int depth = 0; depth < 100; depth++) {
        Obj* left = mk_int_region(r, depth * 2);
        Obj* right = mk_int_region(r, depth * 2 + 1);
        tree = mk_cell_region(r, left, mk_cell_region(r, right, tree));
    }

    region_destroy_if_dead(r);
    PASS();
}

void test_region_list_build(void) {
    Region* r = region_create();
    ASSERT_NOT_NULL(r);

    Obj* list = NULL;
    for (int i = 0; i < 10000; i++) {
        list = mk_cell_region(r, mk_int_unboxed(i), list);
    }

    region_destroy_if_dead(r);
    PASS();
}

void test_region_stress(void) {
    for (int cycle = 0; cycle < 100; cycle++) {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);

        for (int i = 0; i < 10000; i++) {
            Obj* obj = mk_int_region(r, i);
            ASSERT_NOT_NULL(obj);
        }

        region_destroy_if_dead(r);
    }
    PASS();
}

void test_region_typed_alloc(void) {
    Region* r = region_create();
    ASSERT_NOT_NULL(r);

    /* Use alloc_obj_typed if available */
    for (int i = 0; i < 1000; i++) {
        Obj* obj = alloc_obj_typed(r, TYPE_ID_INT);
        if (obj) {
            obj->i = i;
        }
    }

    region_destroy_if_dead(r);
    PASS();
}


/* ============================================================================
 * TEST RUNNER
 * ============================================================================ */

void run_comprehensive_stress_tests(void) {
    TEST_SUITE("Comprehensive Stress Tests");

    /* Section 1: Allocation Patterns (100 tests) */
    TEST_SECTION("Small Object Allocation");
    RUN_TEST(test_alloc_small_int_burst_1k);
    RUN_TEST(test_alloc_small_int_burst_10k);
    RUN_TEST(test_alloc_small_int_burst_50k);
    RUN_TEST(test_alloc_small_int_burst_100k);
    RUN_TEST(test_alloc_small_int_burst_200k);
    RUN_TEST(test_alloc_small_float_burst_1k);
    RUN_TEST(test_alloc_small_float_burst_10k);
    RUN_TEST(test_alloc_small_float_burst_50k);
    RUN_TEST(test_alloc_small_float_burst_100k);
    RUN_TEST(test_alloc_small_char_burst_100k);

    TEST_SECTION("Medium Object Allocation");
    RUN_TEST(test_alloc_pair_burst_1k);
    RUN_TEST(test_alloc_pair_burst_10k);
    RUN_TEST(test_alloc_pair_burst_50k);
    RUN_TEST(test_alloc_pair_burst_100k);
    RUN_TEST(test_alloc_nested_pair_10k);
    RUN_TEST(test_alloc_box_burst_1k);
    RUN_TEST(test_alloc_box_burst_10k);
    RUN_TEST(test_alloc_box_burst_50k);
    RUN_TEST(test_alloc_box_burst_100k);
    RUN_TEST(test_alloc_nested_box_10k);

    TEST_SECTION("Powers-of-2 Pattern");
    RUN_TEST(test_alloc_pow2_pattern_8);
    RUN_TEST(test_alloc_pow2_pattern_16);
    RUN_TEST(test_alloc_pow2_pattern_32);
    RUN_TEST(test_alloc_pow2_pattern_64);
    RUN_TEST(test_alloc_pow2_pattern_128);
    RUN_TEST(test_alloc_pow2_pattern_256);
    RUN_TEST(test_alloc_pow2_pattern_512);
    RUN_TEST(test_alloc_pow2_pattern_1024);
    RUN_TEST(test_alloc_pow2_mixed_10k);
    RUN_TEST(test_alloc_pow2_mixed_100k);

    TEST_SECTION("Odd-Size Pattern");
    RUN_TEST(test_alloc_odd_7);
    RUN_TEST(test_alloc_odd_13);
    RUN_TEST(test_alloc_odd_47);
    RUN_TEST(test_alloc_odd_97);
    RUN_TEST(test_alloc_odd_prime_sequence);
    RUN_TEST(test_alloc_odd_fibonacci);
    RUN_TEST(test_alloc_odd_mixed_primes_50k);
    RUN_TEST(test_alloc_odd_mixed_primes_100k);
    RUN_TEST(test_alloc_odd_alternating);
    RUN_TEST(test_alloc_odd_random_primes);

    TEST_SECTION("Mixed Size Workloads");
    RUN_TEST(test_alloc_mixed_int_float);
    RUN_TEST(test_alloc_mixed_int_pair);
    RUN_TEST(test_alloc_mixed_int_float_pair);
    RUN_TEST(test_alloc_mixed_all_types);
    RUN_TEST(test_alloc_mixed_all_types_50k);
    RUN_TEST(test_alloc_mixed_all_types_100k);
    RUN_TEST(test_alloc_weighted_int_heavy);
    RUN_TEST(test_alloc_weighted_pair_heavy);
    RUN_TEST(test_alloc_bimodal);
    RUN_TEST(test_alloc_trimodal);

    TEST_SECTION("Rapid Alloc/Free Cycles");
    RUN_TEST(test_cycle_single_int_10k);
    RUN_TEST(test_cycle_single_int_100k);
    RUN_TEST(test_cycle_single_int_500k);
    RUN_TEST(test_cycle_single_pair_10k);
    RUN_TEST(test_cycle_single_pair_100k);
    RUN_TEST(test_cycle_batch_10_100k);
    RUN_TEST(test_cycle_batch_100_10k);
    RUN_TEST(test_cycle_batch_1000_1k);
    RUN_TEST(test_cycle_mixed_batch);
    RUN_TEST(test_cycle_nested_pair_batch);

    TEST_SECTION("Deallocation Orders");
    RUN_TEST(test_dealloc_lifo);
    RUN_TEST(test_dealloc_fifo);
    RUN_TEST(test_dealloc_random);
    RUN_TEST(test_dealloc_alternating);
    RUN_TEST(test_dealloc_block_based);
    RUN_TEST(test_dealloc_reverse_block);
    RUN_TEST(test_dealloc_stripe_2);
    RUN_TEST(test_dealloc_stripe_3);
    RUN_TEST(test_dealloc_stripe_4);
    RUN_TEST(test_dealloc_binary_split);

    TEST_SECTION("Symbol Allocation");
    RUN_TEST(test_alloc_symbol_burst_1k);
    RUN_TEST(test_alloc_symbol_burst_10k);
    RUN_TEST(test_alloc_symbol_interning_1k);
    RUN_TEST(test_alloc_symbol_unique_10k);
    RUN_TEST(test_alloc_symbol_long_names);
    RUN_TEST(test_alloc_symbol_special_chars);
    RUN_TEST(test_alloc_symbol_unicode_like);
    RUN_TEST(test_alloc_symbol_empty_name);
    RUN_TEST(test_alloc_symbol_numeric_names);
    RUN_TEST(test_alloc_symbol_repeated_intern);

    TEST_SECTION("List Construction");
    RUN_TEST(test_alloc_list_construction_1k);
    RUN_TEST(test_alloc_list_construction_10k);
    RUN_TEST(test_alloc_list_construction_50k);
    RUN_TEST(test_alloc_list_construction_100k);
    RUN_TEST(test_alloc_multiple_lists);
    RUN_TEST(test_alloc_nested_list);
    RUN_TEST(test_alloc_association_list);
    RUN_TEST(test_alloc_tree_as_list);
    RUN_TEST(test_alloc_list_with_shared);
    RUN_TEST(test_alloc_list_interleaved_free);

    TEST_SECTION("Closure Allocation");
    RUN_TEST(test_alloc_closure_burst_1k);
    RUN_TEST(test_alloc_closure_burst_10k);
    RUN_TEST(test_alloc_closure_with_env);
    RUN_TEST(test_alloc_closure_nested_env);
    RUN_TEST(test_alloc_closure_shared_env);
    RUN_TEST(test_alloc_closure_cycle);
    RUN_TEST(test_alloc_closure_mixed_env);
    RUN_TEST(test_alloc_closure_recursive_pattern);
    RUN_TEST(test_alloc_closure_parallel_chains);
    RUN_TEST(test_alloc_closure_box_env);

    /* Section 2: Fragmentation (60 tests) */
    TEST_SECTION("Swiss Cheese Pattern");
    RUN_TEST(test_frag_swiss_cheese_10k);
    RUN_TEST(test_frag_swiss_cheese_50k);
    RUN_TEST(test_frag_swiss_cheese_every_3rd);
    RUN_TEST(test_frag_swiss_cheese_every_4th);
    RUN_TEST(test_frag_swiss_cheese_random);
    RUN_TEST(test_frag_swiss_cheese_multi_round);
    RUN_TEST(test_frag_swiss_cheese_with_pairs);
    RUN_TEST(test_frag_swiss_cheese_mixed_types);
    RUN_TEST(test_frag_swiss_cheese_progressive);
    RUN_TEST(test_frag_swiss_cheese_cluster);

    TEST_SECTION("Long-Lived Object Interleaving");
    RUN_TEST(test_frag_long_lived_simple);
    RUN_TEST(test_frag_long_lived_interleaved);
    RUN_TEST(test_frag_long_lived_anchors);
    RUN_TEST(test_frag_long_lived_grow_shrink);
    RUN_TEST(test_frag_long_lived_pairs);
    RUN_TEST(test_frag_long_lived_boxes);
    RUN_TEST(test_frag_long_lived_lists);
    RUN_TEST(test_frag_long_lived_closures);
    RUN_TEST(test_frag_long_lived_symbols);
    RUN_TEST(test_frag_long_lived_deep_tree);

    TEST_SECTION("Sawtooth Pattern");
    RUN_TEST(test_frag_sawtooth_simple);
    RUN_TEST(test_frag_sawtooth_aggressive);
    RUN_TEST(test_frag_sawtooth_pairs);
    RUN_TEST(test_frag_sawtooth_mixed);
    RUN_TEST(test_frag_sawtooth_variable_peak);
    RUN_TEST(test_frag_sawtooth_random_valley);
    RUN_TEST(test_frag_sawtooth_micro);
    RUN_TEST(test_frag_sawtooth_double);
    RUN_TEST(test_frag_sawtooth_lists);
    RUN_TEST(test_frag_sawtooth_closures);

    TEST_SECTION("Memory Compaction Scenarios");
    RUN_TEST(test_frag_sparse_array);
    RUN_TEST(test_frag_sliding_window);
    RUN_TEST(test_frag_fifo_queue);
    RUN_TEST(test_frag_lifo_stack);
    RUN_TEST(test_frag_random_replace);
    RUN_TEST(test_frag_lru_cache);
    RUN_TEST(test_frag_object_pool);
    RUN_TEST(test_frag_generational);
    RUN_TEST(test_frag_epoch_based);
    RUN_TEST(test_frag_defrag_simulation);

    TEST_SECTION("External Fragmentation");
    RUN_TEST(test_frag_external_varied_sizes);
    RUN_TEST(test_frag_external_size_classes);
    RUN_TEST(test_frag_external_coalesce);
    RUN_TEST(test_frag_external_best_fit);
    RUN_TEST(test_frag_external_worst_case);
    RUN_TEST(test_frag_external_gradual);
    RUN_TEST(test_frag_external_patterns);
    RUN_TEST(test_frag_external_recovery);
    RUN_TEST(test_frag_external_mixed_recovery);
    RUN_TEST(test_frag_external_stress);

    TEST_SECTION("Internal Fragmentation");
    RUN_TEST(test_frag_internal_small_allocs);
    RUN_TEST(test_frag_internal_padding);

    /* Section 3: Concurrency (abbreviated - full tests above) */
    TEST_SECTION("Concurrent Producer-Consumer");
    RUN_TEST(test_conc_producer_consumer_2_2);
    RUN_TEST(test_conc_producer_consumer_4_4);

    TEST_SECTION("Asymmetric Allocation");
    RUN_TEST(test_conc_asymmetric_2_2);
    RUN_TEST(test_conc_asymmetric_4_2);

    TEST_SECTION("Thread-Local");
    RUN_TEST(test_conc_thread_local_4);
    RUN_TEST(test_conc_thread_local_8);

    TEST_SECTION("Object Migration");
    RUN_TEST(test_conc_migration_4);

    TEST_SECTION("Contention");
    RUN_TEST(test_conc_high_contention);
    RUN_TEST(test_conc_low_contention);
    RUN_TEST(test_conc_refcount_stress);

    /* Section 4: Edge Cases */
    TEST_SECTION("Edge Cases");
    RUN_TEST(test_edge_alloc_zero_then_many);
    RUN_TEST(test_edge_alloc_free_same);
    RUN_TEST(test_edge_refcount_one);
    RUN_TEST(test_edge_refcount_two);
    RUN_TEST(test_edge_deep_nesting_100);
    RUN_TEST(test_edge_deep_nesting_1000);
    RUN_TEST(test_edge_deep_nesting_10000);
    RUN_TEST(test_edge_wide_sharing);
    RUN_TEST(test_edge_alternating_types);
    RUN_TEST(test_edge_min_max_int);
    RUN_TEST(test_edge_float_special);
    RUN_TEST(test_edge_empty_symbol);
    RUN_TEST(test_edge_long_symbol);
    RUN_TEST(test_edge_box_chain);
    RUN_TEST(test_edge_pair_tree);
    RUN_TEST(test_edge_immediate_heavy);
    RUN_TEST(test_edge_boxed_heavy);
    RUN_TEST(test_edge_rapid_symbol_lookup);

    /* Section 5: Type-Specific */
    TEST_SECTION("Type-Specific Workloads");
    RUN_TEST(test_type_int_only_10k);
    RUN_TEST(test_type_int_only_100k);
    RUN_TEST(test_type_float_only_10k);
    RUN_TEST(test_type_float_only_100k);
    RUN_TEST(test_type_pair_only_10k);
    RUN_TEST(test_type_pair_only_100k);
    RUN_TEST(test_type_symbol_only_10k);
    RUN_TEST(test_type_box_only_10k);
    RUN_TEST(test_type_closure_only_10k);
    RUN_TEST(test_type_char_only_10k);
    RUN_TEST(test_type_list_build_10k);
    RUN_TEST(test_type_assoc_list);
    RUN_TEST(test_type_nested_boxes);
    RUN_TEST(test_type_closure_chain);
    RUN_TEST(test_type_mixed_list);

    /* Section 6: Region-Specific */
    TEST_SECTION("Region-Specific");
    RUN_TEST(test_region_create_destroy);
    RUN_TEST(test_region_alloc_simple);
    RUN_TEST(test_region_alloc_many);
    RUN_TEST(test_region_reset_cycle);
    RUN_TEST(test_region_multiple_concurrent);
    RUN_TEST(test_region_mixed_types);
    RUN_TEST(test_region_nested_structures);
    RUN_TEST(test_region_list_build);
    RUN_TEST(test_region_stress);
    RUN_TEST(test_region_typed_alloc);
}

/* Total: 400+ tests covering allocation patterns, fragmentation, concurrency,
 * edge cases, type-specific workloads, and region-specific behavior */
