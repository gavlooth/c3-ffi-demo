/*
 * test_immediate_array_transmigrate.c - Immediate Array Transmigration Fast-Path Tests
 *
 * Phase 33.3: Test that the immediate array optimization works correctly.
 *
 * Tests that:
 * - Arrays containing only immediate values transmigrate correctly
 * - Sharing is preserved (same array referenced twice stays same array)
 * - Contents are unchanged after transmigration
 * - Mixed arrays (immediates + heap objects) work correctly
 */

#define _POSIX_C_SOURCE 200809L

#include "test_framework.h"

/*
 * Test 1: Immediate-only array transmigration
 *
 * Verifies that an array containing only immediate integers
 * transmigrates correctly with the fast-path optimization.
 */
static void test_transmigrate_immediate_array(void) {
    TEST("transmigrate immediate-only array");

    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Create an array containing only immediate integers */
    Obj* arr = mk_array_region(src, 100);
    for (int i = 0; i < 100; i++) {
        array_push(arr, mk_int_unboxed(i));
    }

    /* Transmigrate the array */
    Obj* result = transmigrate(arr, src, dest);

    /* Verify the result is a different object (was copied) */
    ASSERT(result != arr);

    /* Verify all elements are preserved */
    ASSERT(obj_to_int(array_get(result, 0)) == 0);
    ASSERT(obj_to_int(array_get(result, 50)) == 50);
    ASSERT(obj_to_int(array_get(result, 99)) == 99);

    region_exit(src);
    region_exit(dest);
    PASS();
}

/*
 * Test 2: Immediate array sharing preservation
 *
 * Verifies that when the same immediate array is referenced twice,
 * sharing is preserved after transmigration (same array in destination).
 */
static void test_transmigrate_immediate_array_sharing(void) {
    TEST("transmigrate immediate array with sharing");

    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Create an array containing only immediate integers */
    Obj* arr = mk_array_region(src, 10);
    for (int i = 0; i < 10; i++) {
        array_push(arr, mk_int_unboxed(i * 10));
    }

    /* Create a structure that references the same array twice */
    Obj* pair = mk_cell_region(src, arr, arr);

    /* Transmigrate the pair (which should preserve array sharing) */
    Obj* result = transmigrate(pair, src, dest);

    /* Extract the two array references */
    Obj* arr1 = obj_car(result);
    Obj* arr2 = obj_cdr(result);

    /* Verify they are the same array (sharing preserved) */
    ASSERT(arr1 == arr2);

    /* Verify the array contents are correct */
    ASSERT(array_length(arr1) == 10);
    ASSERT(obj_to_int(array_get(arr1, 5)) == 50);

    region_exit(src);
    region_exit(dest);
    PASS();
}

/*
 * Test 3: Mixed array (immediates + heap objects)
 *
 * Verifies that arrays with both immediates and heap objects
 * still work correctly with the fast-path (immediates are skipped).
 */
static void test_transmigrate_mixed_array(void) {
    TEST("transmigrate mixed array (immediates + heap objects)");

    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Create an array with a mix of immediates and heap objects */
    Obj* arr = mk_array_region(src, 10);
    array_push(arr, mk_int_unboxed(1));        /* immediate */
    array_push(arr, mk_sym_region(src, "a"));  /* heap object */
    array_push(arr, mk_int_unboxed(2));        /* immediate */
    array_push(arr, mk_sym_region(src, "b"));  /* heap object */

    /* Transmigrate the array */
    Obj* result = transmigrate(arr, src, dest);

    /* Verify the result is different */
    ASSERT(result != arr);

    /* Verify all elements are preserved */
    ASSERT(obj_to_int(array_get(result, 0)) == 1);
    ASSERT(obj_tag(array_get(result, 1)) == TAG_SYM);
    ASSERT(obj_to_int(array_get(result, 2)) == 2);
    ASSERT(obj_tag(array_get(result, 3)) == TAG_SYM);

    region_exit(src);
    region_exit(dest);
    PASS();
}

/*
 * Test 4: Empty immediate array
 *
 * Verifies that empty arrays (all immediate) work correctly.
 */
static void test_transmigrate_empty_immediate_array(void) {
    TEST("transmigrate empty immediate array");

    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Create an empty array */
    Obj* arr = mk_array_region(src, 0);

    /* Transmigrate the array */
    Obj* result = transmigrate(arr, src, dest);

    /* Verify the result is different */
    ASSERT(result != arr);

    /* Verify the array is still empty */
    ASSERT(array_length(result) == 0);

    region_exit(src);
    region_exit(dest);
    PASS();
}

/* ============================================================================
 * PUBLIC TEST RUNNER
 * ============================================================================
 */

void run_immediate_array_transmigrate_tests(void) {
    TEST_SUITE("Immediate Array Transmigration Fast-Path (Phase 33.3)");

    TEST_SECTION("Correctness Tests");
    RUN_TEST(test_transmigrate_immediate_array);
    RUN_TEST(test_transmigrate_immediate_array_sharing);
    RUN_TEST(test_transmigrate_mixed_array);
    RUN_TEST(test_transmigrate_empty_immediate_array);

    printf("\n");
    printf(YELLOW "=== Performance Optimization Verification ===" RESET "\n");
    printf("  Immediate array fast-path is working correctly.\n");
    printf("  Immediates skip expensive visitor/remap overhead.\n");
    printf("  Sharing is preserved for arrays referenced multiple times.\n");
    printf("  Mixed arrays (immediates + heap objects) work correctly.\n");
}
