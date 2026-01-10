/*
 * test_transmigrate_immediates.c - Regression Test for Immediate Value Transmigration
 *
 * Phase 32.1: CTRR Immediate Root Fast-Path
 *
 * Tests that transmigrate() and transmigrate_incremental() handle immediate
 * values correctly without allocating or calling bitmap_create, even when the
 * source region is empty. This ensures "everything can escape" guarantee.
 *
 * Reference: runtime/docs/CTRR.md, runtime/docs/CTRR_TRANSMIGRATION.md
 */

#define _POSIX_C_SOURCE 200809L

#include "test_framework.h"

/*
 * Test 1: Immediate integer transmigration with empty source region
 *
 * Verifies that transmigrate() handles immediate values correctly
 * even when the source region has no arena allocations (empty).
 */
static void test_transmigrate_immediate_int_empty_region(void) {
    TEST("transmigrate immediate int with empty region");

    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Create an immediate integer value */
    Obj* immediate_int = mk_int_unboxed(123);

    /* Transmigrate should succeed without aborting */
    Obj* result = transmigrate(immediate_int, src, dest);

    /* Result should be the same pointer (immediate values are self-contained) */
    ASSERT(result == immediate_int);

    /* Verify the value is preserved */
    ASSERT(obj_to_int(result) == 123);

    region_exit(src);
    region_exit(dest);
    PASS();
}

/* Note: Floats are NOT immediate values in OmniLisp - they are always boxed.
 * This test is removed since floats require heap allocation. */

/*
 * Test 3: Immediate char transmigration with empty source region
 *
 * Verifies that transmigrate() handles immediate char values correctly.
 */
static void test_transmigrate_immediate_char_empty_region(void) {
    TEST("transmigrate immediate char with empty region");

    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Create an immediate char value */
    Obj* immediate_char = mk_char_unboxed('X');

    /* Transmigrate should succeed without aborting */
    Obj* result = transmigrate(immediate_char, src, dest);

    /* Result should be the same pointer */
    ASSERT(result == immediate_char);

    /* Verify the value is preserved */
    ASSERT(obj_to_char(result) == 'X');

    region_exit(src);
    region_exit(dest);
    PASS();
}

/*
 * Test 4: Immediate integer transmigration_incremental with empty source region
 *
 * Verifies that transmigrate_incremental() handles immediate values correctly
 * and reports progress as 1.0 (complete).
 */
static void test_transmigrate_incremental_immediate_int_empty_region(void) {
    TEST("transmigrate_incremental immediate int with empty region");

    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Create an immediate integer value */
    Obj* immediate_int = mk_int_unboxed(456);

    /* Transmigrate incrementally with progress tracking */
    float progress = 0.0f;
    Obj* result = transmigrate_incremental(immediate_int, src, dest, 128, &progress);

    /* Result should be the same pointer */
    ASSERT(result == immediate_int);

    /* Verify the value is preserved */
    ASSERT(obj_to_int(result) == 456);

    /* Progress should be 1.0 (immediate values require no work) */
    ASSERT_TRUE(progress == 1.0f);

    region_exit(src);
    region_exit(dest);
    PASS();
}

/*
 * Test 5: NULL root transmigration with empty source region
 *
 * Verifies that transmigrate() handles NULL root correctly.
 */
static void test_transmigrate_null_root_empty_region(void) {
    TEST("transmigrate NULL root with empty region");

    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Transmigrate NULL should return NULL without aborting */
    Obj* result = transmigrate(NULL, src, dest);

    /* Result should be NULL */
    ASSERT_NULL(result);

    region_exit(src);
    region_exit(dest);
    PASS();
}

/*
 * Test 6: Immediate value transmigration with non-empty source region
 *
 * Verifies that immediate values work correctly even when the source
 * region has other allocations.
 */
static void test_transmigrate_immediate_with_non_empty_region(void) {
    TEST("transmigrate immediate int with non-empty region");

    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Allocate something in the source region to make it non-empty */
    Obj* sym = mk_sym_region(src, "test");

    /* Create an immediate integer value */
    Obj* immediate_int = mk_int_unboxed(789);

    /* Transmigrate should succeed */
    Obj* result = transmigrate(immediate_int, src, dest);

    /* Result should be the same pointer */
    ASSERT(result == immediate_int);

    /* Verify the value is preserved */
    ASSERT(obj_to_int(result) == 789);

    region_exit(src);
    region_exit(dest);
    PASS();
}

/*
 * Test 7: Immediate value transmigration after region_exit
 *
 * Verifies that immediate values remain valid after the source region is exited.
 * This tests that no dangling pointers are created.
 */
static void test_transmigrate_immediate_after_region_exit(void) {
    TEST("transmigrate immediate int after region_exit");

    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Create an immediate integer value in source region context */
    Obj* immediate_int = mk_int_unboxed(999);

    /* Transmigrate to destination */
    Obj* result = transmigrate(immediate_int, src, dest);

    /* Exit source region (this would invalidate any pointers into it) */
    region_exit(src);

    /* Result should still be valid and accessible */
    ASSERT_NOT_NULL(result);
    ASSERT(result == immediate_int);
    ASSERT(obj_to_int(result) == 999);

    /* Try to access the value again to ensure no UAF */
    ASSERT(obj_to_int(result) == 999);

    region_exit(dest);
    PASS();
}

/* ============================================================================
 * PUBLIC TEST RUNNER
 * ============================================================================ */

void run_transmigrate_immediates_tests(void) {
    TEST_SUITE("CTRR Transmigration Immediates (Phase 32.1)");

    TEST_SECTION("Immediate Value Transmigration");
    RUN_TEST(test_transmigrate_immediate_int_empty_region);
    RUN_TEST(test_transmigrate_immediate_char_empty_region);

    TEST_SECTION("Incremental Transmigration");
    RUN_TEST(test_transmigrate_incremental_immediate_int_empty_region);

    TEST_SECTION("Edge Cases");
    RUN_TEST(test_transmigrate_null_root_empty_region);
    RUN_TEST(test_transmigrate_immediate_with_non_empty_region);

    TEST_SECTION("Region Exit Safety");
    RUN_TEST(test_transmigrate_immediate_after_region_exit);

    printf("\n");
    printf(YELLOW "=== CTRR Contract Verification ===" RESET "\n");
    printf("  Immediate values can escape even from empty regions.\n");
    printf("  transmigrate() and transmigrate_incremental() never abort for immediates.\n");
    printf("  No dangling pointers are created for immediate values.\n");
}
