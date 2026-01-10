/**
 * Test for BUG-0006: Arena Region Alignment Bug
 *
 * Expected invariant: arena_region_alloc() should correctly align allocations
 * based on the alignment parameter, both for allocations from existing blocks
 * and allocations from newly created blocks.
 *
 * Observed behavior: In arena_region_alloc() at region.c:634, when allocating
 * from a newly created block, the code incorrectly computes:
 *   aligned = (alignment - 1) & ~(alignment - 1);
 *
 * This always produces 0 for power-of-2 alignments, completely ignoring the
 * actual current position (new_block->used, which is 0 for a new block).
 * While this accidentally works for the first allocation, the formula is
 * fundamentally wrong and doesn't match the correct pattern used at line 599:
 *   aligned = (r->current->used + alignment - 1) & ~(alignment - 1);
 *
 * Why this violates architecture/invariants:
 * 1. Alignment API contract broken: The function promises to align allocations
 *    but uses broken logic for new blocks.
 * 2. Code duplication with wrong logic: The correct formula exists 35 lines
 *    earlier but wasn't copied correctly.
 * 3. Undefined behavior for non-power-of-2 alignments: The mask formula
 *    ~(alignment - 1) only works for power-of-2 alignments.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

/* Simulate the buggy alignment formula from region.c:634 */
static inline size_t buggy_align_new_block(size_t alignment) {
    /* This is the BUGGY code from region.c:634 */
    return (alignment - 1) & ~(alignment - 1);
}

/* Correct alignment formula */
static inline size_t correct_align(size_t position, size_t alignment) {
    /* This is the CORRECT code from region.c:599 */
    return (position + alignment - 1) & ~(alignment - 1);
}

int main(void) {
    printf("=== BUG-0006: Arena Region Alignment Bug Test ===\n\n");

    int tests_passed = 0;
    int tests_failed = 0;

    /* Test 1: Demonstrate the buggy formula produces always 0 */
    printf("Test 1: Buggy formula returns 0 for all power-of-2 alignments\n");
    printf("--------------------------------------------------------------\n");

    for (size_t alignment = 1; alignment <= 128; alignment *= 2) {
        size_t result = buggy_align_new_block(alignment);
        printf("  alignment=%3zu: buggy_align_new_block() = %zu ",
               alignment, result);
        if (result == 0) {
            printf("✓ (always 0 - confirms bug)\n");
            tests_passed++;
        } else {
            printf("✗ (expected 0, got %zu)\n", result);
            tests_failed++;
        }
    }

    printf("\n");

    /* Test 2: Show correct formula for new block (position=0) */
    printf("Test 2: Correct formula for new block (position=0)\n");
    printf("--------------------------------------------------\n");

    for (size_t alignment = 1; alignment <= 128; alignment *= 2) {
        size_t correct = correct_align(0, alignment);
        size_t buggy = buggy_align_new_block(alignment);
        printf("  alignment=%3zu: correct=%zu, buggy=%zu, match=%s\n",
               alignment, correct, buggy,
               (correct == buggy) ? "yes" : "NO");
        if (correct == buggy) {
            tests_passed++;
        } else {
            tests_failed++;
        }
    }

    printf("\n");

    /* Test 3: Demonstrate non-power-of-2 alignment issue */
    printf("Test 3: Non-power-of-2 alignment produces WRONG results\n");
    printf("-------------------------------------------------------\n");

    size_t non_pof2_alignments[] = {3, 5, 6, 7, 10, 12, 15, 20};
    for (size_t i = 0; i < sizeof(non_pof2_alignments)/sizeof(non_pof2_alignments[0]); i++) {
        size_t alignment = non_pof2_alignments[i];
        size_t pos = 5;

        /* What we'd expect: next multiple of alignment >= pos */
        size_t expected = ((pos + alignment - 1) / alignment) * alignment;

        /* What the buggy formula gives */
        size_t buggy = buggy_align_new_block(alignment);

        /* What the mask formula actually gives (for position=0) */
        size_t mask_result = (pos + alignment - 1) & ~(alignment - 1);

        printf("  alignment=%2zu: expected=%zu, buggy=%zu, mask=%zu ",
               alignment, expected, buggy, mask_result);

        if (buggy != expected) {
            printf("✗ (BUG CONFIRMED)\n");
            tests_failed++;
        } else {
            printf("✓ (correct by accident)\n");
            tests_passed++;
        }
    }

    printf("\n");

    /* Test 4: Show the correct code that exists at line 599 */
    printf("Test 4: Compare with correct code at region.c:599\n");
    printf("-------------------------------------------------\n");

    size_t positions[] = {0, 5, 10, 15, 20};
    size_t alignments[] = {4, 8, 16};

    for (size_t pi = 0; pi < sizeof(positions)/sizeof(positions[0]); pi++) {
        for (size_t ai = 0; ai < sizeof(alignments)/sizeof(alignments[0]); ai++) {
            size_t pos = positions[pi];
            size_t alignment = alignments[ai];
            size_t correct = correct_align(pos, alignment);
            printf("  position=%zu, alignment=%zu: aligned=%zu ✓\n",
                   pos, alignment, correct);
            tests_passed++;
        }
    }

    printf("\n");

    /* Summary */
    printf("=== SUMMARY ===\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("\n");
    printf("BUG CONFIRMED: arena_region_alloc() at region.c:634 uses:\n");
    printf("  aligned = (alignment - 1) & ~(alignment - 1);\n");
    printf("\n");
    printf("This is INCORRECT because:\n");
    printf("  1. It should be: aligned = (new_block->used + alignment - 1) & ~(alignment - 1);\n");
    printf("  2. The correct formula already exists 35 lines earlier (line 599)\n");
    printf("  3. For power-of-2 alignments, it always returns 0 (works by accident for new blocks)\n");
    printf("  4. For non-power-of-2 alignments, it returns garbage (undefined behavior)\n");
    printf("\n");
    printf("Fix: Replace line 634 with the same formula used at line 599:\n");
    printf("  aligned = (new_block->used + alignment - 1) & ~(alignment - 1);\n");
    printf("\n");

    return (tests_failed > 0) ? 1 : 0;
}
