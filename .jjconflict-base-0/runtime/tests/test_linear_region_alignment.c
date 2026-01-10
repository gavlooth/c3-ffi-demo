/*
 * Test: Linear Region Alignment Bug (Non-Power-of-2 Alignments)
 *
 * Expected invariant: linear_region_alloc() should correctly align allocations
 * for any valid alignment value.
 *
 * Observed behavior: The alignment calculation at region.c:389 uses a bitwise
 * trick that only works for power-of-2 alignments:
 *   size_t aligned = (r->used + alignment - 1) & ~(alignment - 1);
 *
 * For non-power-of-2 alignments (e.g., 12, 24, 80), this produces incorrect results:
 * - alignment=12: ~(12-1) = ~11 = 0xFFFFFFF4 (aligns to 16, not 12)
 * - alignment=24: ~(24-1) = ~23 = 0xFFFFFFE8 (aligns to 24, but by accident)
 * - alignment=80: ~(80-1) = ~79 = 0xFFFFFFB0 (aligns to 80, but by accident)
 *
 * While some non-power-of-2 values happen to work (like 24, 80), they do so only
 * because they round to a different power-of-2 boundary. For 12, it incorrectly
 * rounds to 16.
 *
 * Architecture violation: Memory alignment contract - the function promises to
 * align to any valid alignment value but fails for non-power-of-2 values.
 */

#include "test_framework.h"
#include "../src/memory/region.h"
#include <stdint.h>

/* Test 1: Power-of-2 alignments work correctly */
void test_linear_region_power_of_2_alignment(void) {
    TEST("Power-of-2 alignments");

    LinearRegion* r = linear_region_new(1024);
    ASSERT_NOT_NULL(r);

    /* Allocate with alignment=1 (no alignment needed) */
    void* p1 = linear_region_alloc(r, 10, 1);
    ASSERT_NOT_NULL(p1);
    ASSERT_EQ((uintptr_t)p1 % 1, 0);

    /* Allocate with alignment=2 */
    void* p2 = linear_region_alloc(r, 10, 2);
    ASSERT_NOT_NULL(p2);
    ASSERT_EQ((uintptr_t)p2 % 2, 0);

    /* Allocate with alignment=4 */
    void* p3 = linear_region_alloc(r, 10, 4);
    ASSERT_NOT_NULL(p3);
    ASSERT_EQ((uintptr_t)p3 % 4, 0);

    /* Allocate with alignment=8 */
    void* p4 = linear_region_alloc(r, 10, 8);
    ASSERT_NOT_NULL(p4);
    ASSERT_EQ((uintptr_t)p4 % 8, 0);

    /* Allocate with alignment=16 */
    void* p5 = linear_region_alloc(r, 10, 16);
    ASSERT_NOT_NULL(p5);
    ASSERT_EQ((uintptr_t)p5 % 16, 0);

    /* Allocate with alignment=32 */
    void* p6 = linear_region_alloc(r, 10, 32);
    ASSERT_NOT_NULL(p6);
    ASSERT_EQ((uintptr_t)p6 % 32, 0);

    linear_region_free(r);
    PASS();
}

/* Test 2: Non-power-of-2 alignments fail */
void test_linear_region_non_power_of_2_alignment_bug(void) {
    TEST("Non-power-of-2 alignments (BUG DEMONSTRATION)");

    LinearRegion* r = linear_region_new(1024);
    ASSERT_NOT_NULL(r);

    /* alignment=3: Should align to 3, but aligns to 4 */
    void* p3 = linear_region_alloc(r, 10, 3);
    ASSERT_NOT_NULL(p3);
    uintptr_t addr3 = (uintptr_t)p3;
    if (addr3 % 3 != 0) {
        /* BUG: Address is not aligned to 3 as requested */
        FAIL("Address not aligned to 3");
    }

    /* alignment=12: Should align to 12, but aligns to 16 */
    r->used = 0; /* Reset for clean test */
    void* p12 = linear_region_alloc(r, 10, 12);
    ASSERT_NOT_NULL(p12);
    uintptr_t addr12 = (uintptr_t)p12;
    if (addr12 % 12 != 0) {
        /* BUG: Address is aligned to 16, not 12 */
        FAIL("Address not aligned to 12 (rounded to 16)");
    }

    /* alignment=24: Happens to work (rounds to 32, but 24 is a factor of 32... no wait) */
    /* Actually 24 is not a factor of 32. Let me recalculate:
     * ~(24-1) = ~23 = 0xFFFFFFE8
     * If used=0: (0 + 23) & 0xFFFFFFE8 = 23 & 0xFFFFFFE8 = 16 (incorrect!)
     * If used=10: (10 + 23) & 0xFFFFFFE8 = 33 & 0xFFFFFFE8 = 32 (incorrect!)
     */
    r->used = 0;
    void* p24 = linear_region_alloc(r, 10, 24);
    ASSERT_NOT_NULL(p24);
    uintptr_t addr24 = (uintptr_t)p24;
    if (addr24 % 24 != 0) {
        /* BUG: Address should be aligned to 24 */
        FAIL("Address not aligned to 24");
    }

    /* alignment=80: ~(80-1) = ~79 = 0xFFFFFFB0
     * If used=0: (0 + 79) & 0xFFFFFFB0 = 79 & 0xFFFFFFB0 = 64 (incorrect!)
     */
    r->used = 0;
    void* p80 = linear_region_alloc(r, 10, 80);
    ASSERT_NOT_NULL(p80);
    uintptr_t addr80 = (uintptr_t)p80;
    if (addr80 % 80 != 0) {
        /* BUG: Address should be aligned to 80 */
        FAIL("Address not aligned to 80");
    }

    linear_region_free(r);

    /* If we got here, all tests passed - but the bug still exists for some values */
    PASS();
}

/* Test 3: Demonstrate the incorrect alignment calculation */
void test_linear_region_alignment_formula_bug(void) {
    TEST("Alignment formula demonstration");

    printf("\n    Demonstrating the alignment bug:\n");

    /* For power-of-2, the formula works: */
    size_t alignment = 16;
    size_t used = 10;
    size_t aligned_wrong = (used + alignment - 1) & ~(alignment - 1);
    /* (10 + 15) & ~15 = 25 & 0xFFFFFFF0 = 16 ✓ Correct! */

    printf("    Power-of-2 (alignment=16, used=10): %zu (correct)\n", aligned_wrong);

    /* For non-power-of-2, the formula fails: */
    alignment = 12;
    used = 10;
    aligned_wrong = (used + alignment - 1) & ~(alignment - 1);
    /* (10 + 11) & ~11 = 21 & 0xFFFFFFF4 = 16 ✗ Should be 12! */

    size_t aligned_correct = ((used + alignment - 1) / alignment) * alignment;
    /* ((10 + 11) / 12) * 12 = (21 / 12) * 12 = 1 * 12 = 12 ✓ Correct! */

    printf("    Non-power-of-2 (alignment=12, used=12): %zu (wrong, should be %zu)\n",
           aligned_wrong, aligned_correct);

    if (aligned_wrong != aligned_correct) {
        FAIL("Alignment formula produces incorrect result for non-power-of-2");
    }

    PASS();
}

/* Test 4: Edge case - alignment of 0 or 1 */
void test_linear_region_edge_case_alignments(void) {
    TEST("Edge case alignments (0, 1)");

    LinearRegion* r = linear_region_new(1024);
    ASSERT_NOT_NULL(r);

    /* alignment=0: This could cause division by zero in correct formula,
     * and ~(0-1) = ~(-1) = 0, so the buggy formula gives:
     * (used + 0) & 0 = 0, always allocating at offset 0 (wrong!) */
    void* p0 = linear_region_alloc(r, 10, 0);
    if (p0 != NULL) {
        /* The allocation succeeded - check if it's at the right place */
        printf("    Note: alignment=0 returned %p\n", p0);
    }

    /* alignment=1: Should work (any address is aligned to 1) */
    void* p1 = linear_region_alloc(r, 10, 1);
    ASSERT_NOT_NULL(p1);
    ASSERT_EQ((uintptr_t)p1 % 1, 0);

    linear_region_free(r);
    PASS();
}

void run_linear_region_alignment_tests(void) {
    TEST_SUITE("Linear Region Alignment Bug Tests");
    RUN_TEST(test_linear_region_power_of_2_alignment);
    RUN_TEST(test_linear_region_non_power_of_2_alignment_bug);
    RUN_TEST(test_linear_region_alignment_formula_bug);
    RUN_TEST(test_linear_region_edge_case_alignments);

    printf("\n    Summary:\n");
    printf("    The bug is in region.c:389:\n");
    printf("      size_t aligned = (r->used + alignment - 1) & ~(alignment - 1);\n");
    printf("    This only works for power-of-2 alignments.\n");
    printf("    Correct formula (works for any alignment):\n");
    printf("      size_t aligned = ((r->used + alignment - 1) / alignment) * alignment;\n");
}
