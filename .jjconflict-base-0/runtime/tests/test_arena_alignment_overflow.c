/*
 * Test: Arena Alignment Integer Overflow
 *
 * Expected invariant:
 *   arena_alloc() should either reject oversized allocations or handle them safely.
 *
 * Observed behavior:
 *   When arena_alloc() receives a size near SIZE_MAX, the alignment calculation
 *   at arena.c:22 causes integer overflow:
 *     size = (size + 7) & ~(size_t)7;
 *
 *   The addition (size + 7) can overflow when size is near SIZE_MAX, causing
 *   the aligned size to wrap to a small value. This leads to:
 *   - Buffer overflow when the allocated memory is too small
 *   - Potential security vulnerability
 *   - Undefined behavior
 *
 * Why this violates the architecture:
 *   The arena allocator is a critical memory management component. Integer
 *   overflow in size calculations breaks the fundamental memory safety invariant
 *   that allocated buffers must be at least as large as requested.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

/* Minimal arena definitions */
typedef struct ArenaBlock {
    char* memory;
    size_t size;
    size_t used;
    struct ArenaBlock* next;
} ArenaBlock;

typedef struct Arena {
    ArenaBlock* current;
    ArenaBlock* blocks;
    size_t block_size;
    void* externals;
} Arena;

/* Copy of the buggy alignment calculation from arena.c:22 */
static inline size_t buggy_align_size(size_t size) {
    return (size + 7) & ~(size_t)7;
}

/* Correct alignment calculation with overflow check */
static inline size_t correct_align_size(size_t size) {
    if (size > SIZE_MAX - 7) {
        return SIZE_MAX;  /* Or signal error */
    }
    return (size + 7) & ~(size_t)7;
}

int main(void) {
    printf("=== Arena Alignment Overflow Test ===\n\n");

    int test_failed = 0;

    /* Test 1: Normal sizes should work correctly */
    printf("Test 1: Normal sizes (no overflow)\n");
    {
        size_t sizes[] = {0, 1, 7, 8, 15, 16, 100, 1024, 4096};
        for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
            size_t size = sizes[i];
            size_t buggy = buggy_align_size(size);
            size_t correct = correct_align_size(size);

            printf("  size=%zu: buggy=%zu, correct=%zu", size, buggy, correct);
            if (buggy == correct) {
                printf(" OK\n");
            } else {
                printf(" MISMATCH!\n");
                test_failed = 1;
            }
        }
    }

    /* Test 2: Size near SIZE_MAX should cause overflow in buggy version */
    printf("\nTest 2: Size near SIZE_MAX (overflow detection)\n");
    {
        size_t dangerous_size = SIZE_MAX - 1;
        size_t buggy = buggy_align_size(dangerous_size);
        size_t correct = correct_align_size(dangerous_size);

        printf("  size=SIZE_MAX-1 (%zu)\n", dangerous_size);
        printf("  buggy result:  %zu\n", buggy);
        printf("  correct result: %zu\n", correct);

        /* The buggy version wraps to a small value due to overflow */
        if (buggy < dangerous_size) {
            printf("  BUG CONFIRMED: buggy align caused wraparound!\n");
            printf("  Requested %zu bytes but aligned to only %zu bytes\n",
                   dangerous_size, buggy);
            printf("  This would cause buffer overflow!\n");
            test_failed = 1;  /* Bug exists - test should fail to expose it */
        } else {
            printf("  No overflow detected (bug may be fixed)\n");
        }
    }

    /* Test 3: SIZE_MAX itself */
    printf("\nTest 3: SIZE_MAX\n");
    {
        size_t size = SIZE_MAX;
        size_t buggy = buggy_align_size(size);
        size_t correct = correct_align_size(size);

        printf("  size=SIZE_MAX (%zu)\n", size);
        printf("  buggy result:  %zu\n", buggy);
        printf("  correct result: %zu\n", correct);

        /* With SIZE_MAX, (SIZE_MAX + 7) wraps to 6 */
        if (buggy == 6) {
            printf("  BUG CONFIRMED: SIZE_MAX wraps to 6!\n");
            test_failed = 1;
        } else if (buggy < size) {
            printf("  BUG CONFIRMED: result smaller than input!\n");
            test_failed = 1;
        }
    }

    /* Test 4: Various boundary values */
    printf("\nTest 4: Boundary values\n");
    {
        struct {
            size_t input;
            const char* description;
        } tests[] = {
            {SIZE_MAX - 10, "SIZE_MAX - 10"},
            {SIZE_MAX - 7, "SIZE_MAX - 7"},
            {SIZE_MAX - 8, "SIZE_MAX - 8"},
            {SIZE_MAX / 2, "SIZE_MAX / 2"},
        };

        for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
            size_t input = tests[i].input;
            size_t buggy = buggy_align_size(input);
            printf("  %s: input=%zu, buggy=%zu", tests[i].description, input, buggy);

            if (buggy < input) {
                printf(" OVERFLOW!\n");
                test_failed = 1;
            } else {
                printf(" OK\n");
            }
        }
    }

    printf("\n=== Summary ===\n");
    if (test_failed) {
        printf("FAILED: Integer overflow bug detected in arena alignment\n");
        printf("Location: runtime/src/memory/arena.c:22\n");
        printf("Issue: (size + 7) can overflow for large sizes\n");
        printf("Fix: Add overflow check before alignment calculation\n");
        return 1;
    } else {
        printf("PASSED: No overflow detected (bug may be fixed)\n");
        return 0;
    }
}
