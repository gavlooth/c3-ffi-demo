/*
 * Test: Constraint grow_array Integer Overflow Bug
 *
 * Expected invariant: grow_array() should handle capacity growth safely without
 * integer overflow, even when capacity is near INT_MAX.
 *
 * Observed behavior: In constraint.c:12-19, grow_array() has:
 *   int new_cap = (*capacity == 0) ? 8 : (*capacity * 2);
 *
 * This calculation can overflow when *capacity > INT_MAX / 2:
 * - If *capacity = INT_MAX/2 + 1, then *capacity * 2 overflows to negative
 * - The negative value is then used in realloc(arr, new_cap * elem_size)
 * - This can cause undefined behavior or incorrect allocation sizes
 *
 * Architecture violation: Memory safety - lack of overflow protection allows
 * integer wraparound that can lead to incorrect memory allocation sizes.
 *
 * Root cause: Missing overflow check before doubling capacity
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <assert.h>

/* Simplified version of grow_array from constraint.c */
static void* grow_array_buggy(void* arr, int* capacity, size_t elem_size) {
    /* BUG: No overflow check before multiplication */
    int new_cap = (*capacity == 0) ? 8 : (*capacity * 2);
    void* new_arr = realloc(arr, new_cap * elem_size);
    if (new_arr) {
        *capacity = new_cap;
    }
    return new_arr;
}

/* Fixed version for comparison */
static void* grow_array_fixed(void* arr, int* capacity, size_t elem_size) {
    int new_cap;
    if (*capacity == 0) {
        new_cap = 8;
    } else {
        /* FIX: Check for overflow before doubling */
        if (*capacity > INT_MAX / 2) {
            return NULL;  /* Would overflow - cannot grow */
        }
        new_cap = *capacity * 2;
    }
    void* new_arr = realloc(arr, (size_t)new_cap * elem_size);
    if (new_arr) {
        *capacity = new_cap;
    }
    return new_arr;
}

/* Test case 1: Normal operation */
void test_grow_array_normal(void) {
    printf("Test 1: Normal growth\n");

    int capacity = 0;
    char* arr = NULL;

    /* First growth: 0 -> 8 */
    arr = grow_array_buggy(arr, &capacity, sizeof(int));
    assert(arr != NULL);
    assert(capacity == 8);
    printf("  0 -> 8: OK\n");

    /* Second growth: 8 -> 16 */
    arr = grow_array_buggy(arr, &capacity, sizeof(int));
    assert(arr != NULL);
    assert(capacity == 16);
    printf("  8 -> 16: OK\n");

    /* Third growth: 16 -> 32 */
    arr = grow_array_buggy(arr, &capacity, sizeof(int));
    assert(arr != NULL);
    assert(capacity == 32);
    printf("  16 -> 32: OK\n");

    free(arr);
    printf("  PASS\n\n");
}

/* Test case 2: Overflow when capacity > INT_MAX / 2 */
void test_grow_array_overflow_bug(void) {
    printf("Test 2: Integer overflow when capacity > INT_MAX / 2\n");

    int capacity = INT_MAX / 2 + 1;  /* 1073741824 on 32-bit, larger on 64-bit */
    char* arr = malloc(8);  /* Start with small allocation */

    printf("  Initial capacity: %d (0x%x)\n", capacity, capacity);

    /* Try to grow - this should fail due to overflow, but buggy version
     * will attempt the multiplication */
    int capacity_before = capacity;
    char* result = grow_array_buggy(arr, &capacity, 1);

    printf("  Capacity after growth attempt: %d (0x%x)\n", capacity, capacity);

    /* The buggy version: new_cap = capacity * 2 overflows to negative */
    if (capacity < 0) {
        printf("  *** BUG CONFIRMED ***\n");
        printf("  Capacity overflowed to negative: %d\n", capacity);
        printf("  This would be passed to realloc() as size!\n");

        /* If realloc is called with negative value cast to size_t,
         * it becomes a huge positive number */
        size_t bad_size = (size_t)capacity * 1;
        printf("  Actual size passed to realloc: %zu\n", bad_size);

        if (result == NULL) {
            printf("  realloc() correctly failed (likely due to huge size)\n");
        } else {
            printf("  WARNING: realloc() succeeded with incorrect size!\n");
            free(result);
        }
    } else {
        printf("  No overflow detected (capacity fits in int)\n");
        if (result) free(result);
    }

    free(arr);
    printf("\n");
}

/* Test case 3: Demonstrate the overflow calculation */
void test_grow_array_overflow_calculation(void) {
    printf("Test 3: Overflow calculation demonstration\n");

    /* On a typical 32-bit int system: */
    int test_cap = INT_MAX / 2 + 1;  /* 1073741824 */
    printf("  INT_MAX = %d\n", INT_MAX);
    printf("  INT_MAX / 2 = %d\n", INT_MAX / 2);
    printf("  INT_MAX / 2 + 1 = %d (0x%x)\n", test_cap, test_cap);

    /* Buggy calculation: */
    int doubled_buggy = test_cap * 2;
    printf("  %d * 2 = %d (0x%x)\n", test_cap, doubled_buggy, doubled_buggy);

    if (doubled_buggy < 0) {
        printf("  *** OVERFLOW: Result is negative!\n");
        printf("  When cast to size_t: %zu\n", (size_t)doubled_buggy);
    }

    /* Fixed calculation: */
    int doubled_fixed;
    if (test_cap > INT_MAX / 2) {
        printf("  Fixed version: Would detect overflow and return NULL\n");
        doubled_fixed = -1;  /* Sentinel */
    } else {
        doubled_fixed = test_cap * 2;
    }

    printf("\n");
}

/* Test case 4: Comparison with fixed version */
void test_grow_array_fixed_comparison(void) {
    printf("Test 4: Fixed version handles overflow correctly\n");

    int capacity = INT_MAX / 2 + 1;
    char* arr = malloc(8);

    printf("  Testing with capacity = %d\n", capacity);

    /* Fixed version should detect overflow and return NULL */
    char* result = grow_array_fixed(arr, &capacity, 1);

    if (result == NULL) {
        printf("  *** FIXED VERSION CORRECTLY DETECTED OVERFLOW ***\n");
        printf("  grow_array_fixed() returned NULL\n");
        printf("  Capacity unchanged: %d\n", capacity);
    } else {
        printf("  Fixed version did not detect overflow (unexpected)\n");
        free(result);
    }

    free(arr);
    printf("\n");
}

/* Test case 5: Edge case - INT_MAX exactly */
void test_grow_array_edge_case_int_max(void) {
    printf("Test 5: Edge case - capacity at INT_MAX\n");

    int capacity = INT_MAX;
    char* arr = malloc(8);

    printf("  Testing with capacity = INT_MAX = %d\n", capacity);

    /* Buggy version */
    int capacity_buggy = INT_MAX;
    char* result_buggy = grow_array_buggy(arr, &capacity_buggy, 1);
    printf("  Buggy version: capacity became %d\n", capacity_buggy);

    /* Fixed version */
    int capacity_fixed = INT_MAX;
    char* arr2 = malloc(8);
    char* result_fixed = grow_array_fixed(arr2, &capacity_fixed, 1);
    printf("  Fixed version: %s\n", result_fixed == NULL ? "returned NULL (correct)" : "returned non-NULL");

    if (result_buggy) free(result_buggy);
    if (result_fixed) free(result_fixed);
    free(arr);
    free(arr2);

    printf("\n");
}

int main(void) {
    printf("=== Constraint grow_array Integer Overflow Bug Test ===\n\n");

    test_grow_array_normal();
    test_grow_array_overflow_bug();
    test_grow_array_overflow_calculation();
    test_grow_array_fixed_comparison();
    test_grow_array_edge_case_int_max();

    printf("=== Summary ===\n");
    printf("Bug location: constraint.c:12-19 (grow_array function)\n");
    printf("Severity: Medium - potential memory corruption, unlikely in normal use\n");
    printf("\nThe bug:\n");
    printf("  int new_cap = (*capacity == 0) ? 8 : (*capacity * 2);\n");
    printf("\nFix:\n");
    printf("  Add overflow check before multiplication:\n");
    printf("  if (*capacity > INT_MAX / 2) return NULL;\n");
    printf("  int new_cap = *capacity * 2;\n");
    printf("\nBetter fix:\n");
    printf("  Use size_t instead of int for capacity to avoid overflow entirely\n");

    return 0;
}
