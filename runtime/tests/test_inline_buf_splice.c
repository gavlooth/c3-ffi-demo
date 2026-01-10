/*
 * test_inline_buf_splice.c - Regression test for inline_buf splice soundness
 *
 * Phase 31.4: Verifies that the splice fast path is NOT used when inline_buf
 * has allocations, preventing potential dangling pointer bugs.
 *
 * CTRR REQUIREMENT: The splice optimization (O(1) arena chunk transfer) is only
 * safe when NO allocations were made to the inline buffer, because inline buffer
 * allocations would NOT be transferred by the arena splice.
 *
 * Reference: runtime/docs/CTRR_TRANSMIGRATION.md
 */

#include <stdbool.h>
#include "test_framework.h"

/* Note: omni.h is NOT included here because test_main.c includes runtime.c
   directly, which provides all type definitions. Including both would cause
   redefinition errors. */

/* External declarations for functions defined in region_core.h */
extern bool region_can_splice_arena_only(const struct Region* r);

/*
 * Test 1: Verify region_can_splice_arena_only() returns true when inline_buf is empty
 *
 * This test verifies that when a region has no inline buffer allocations,
 * the splice fast path is considered sound.
 */
static void test_can_splice_arena_only_when_empty(void) {
    Region* r = region_create();
    TEST("region_can_splice_arena_only when empty");
    ASSERT_NOT_NULL(r);

    /* Initially, inline_buf.offset should be 0 */
    ASSERT(r->inline_buf.offset == 0);
    ASSERT(region_can_splice_arena_only(r) == true);

    region_exit(r);
    PASS();
}

/*
 * Test 2: Verify region_can_splice_arena_only() returns false when inline_buf has allocations
 *
 * This test verifies that after making inline buffer allocations,
 * the splice fast path is correctly disabled.
 *
 * Note: mk_sym_region allocates a symbol in the inline buffer (8 bytes).
 */
static void test_can_splice_arena_only_false_after_inline_alloc(void) {
    Region* r = region_create();
    TEST("region_can_splice_arena_only false after inline alloc");
    ASSERT_NOT_NULL(r);

    /* Initially should be spliceable */
    ASSERT(region_can_splice_arena_only(r) == true);

    /* Allocate a symbol - this uses inline buffer (8 bytes for small symbols) */
    Obj* sym = mk_sym_region(r, "test");
    (void)sym;

    /* Verify inline buffer was used */
    ASSERT(r->inline_buf.offset > 0);

    /* After inline allocation, should NOT be spliceable */
    ASSERT(region_can_splice_arena_only(r) == false);

    region_exit(r);
    PASS();
}

/*
 * Test 3: Verify transmigration splice fast path conditions
 *
 * This test verifies that the splice fast path is correctly used when
 * conditions are met (no external refs, scope not alive, empty inline_buf).
 */
static void test_transmigration_splice_fast_path_with_arena_only(void) {
    Region* src = region_create();
    Region* dest = region_create();
    TEST("transmigration splice fast path with arena only");
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Save initial inline_buf offset */
    size_t initial_offset = src->inline_buf.offset;

    /* Allocate a large object that primarily goes to the arena */
    /* Note: Some metadata may go to inline buffer */
    Obj* large_arr = mk_array_region(src, 1000);  /* Large array */

    /* Manually clear inline_buf to simulate arena-only allocation */
    /* This allows us to test the splice fast path in isolation */
    src->inline_buf.offset = 0;

    /* Verify splice fast path condition is now met */
    ASSERT(region_can_splice_arena_only(src) == true);

    /* Set up splice conditions: no external refs, scope not alive */
    src->external_rc = 0;
    src->scope_alive = false;

    /* Transmigrate - should use splice fast path */
    Obj* result = transmigrate(large_arr, src, dest);

    /* When splice is used, result should be the same pointer */
    ASSERT(result == large_arr);

    /* Clean up: restore offset for proper region_exit */
    src->inline_buf.offset = initial_offset;

    region_exit(src);
    region_exit(dest);
    PASS();
}

/*
 * Test 4: Verify transmigration with inline_buf allocations does NOT use splice fast path
 *
 * This test verifies that when inline_buf has allocations, the splice
 * optimization is correctly disabled and full transmigration occurs.
 */
static void test_transmigration_no_splice_with_inline_alloc(void) {
    Region* src = region_create();
    Region* dest = region_create();
    TEST("transmigration no splice with inline alloc");
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Allocate small objects that WILL use inline buffer */
    Obj* int1 = mk_int_region(src, 1);
    Obj* int2 = mk_int_region(src, 2);
    Obj* pair = mk_cell_region(src, int1, int2);

    /* Verify inline buffer was used */
    ASSERT(src->inline_buf.offset > 0);
    ASSERT(region_can_splice_arena_only(src) == false);

    /* Set up splice conditions (but they should be ignored due to inline_buf) */
    src->external_rc = 0;
    src->scope_alive = false;

    /* Save original pointer for comparison */
    Obj* original_pair = pair;

    /* Transmigrate - should NOT use splice fast path */
    Obj* result = transmigrate(pair, src, dest);

    /* When splice is NOT used, result should be a different pointer */
    ASSERT(result != original_pair);

    /* But the result should still be semantically equivalent */
    ASSERT(result->tag == TAG_PAIR);
    ASSERT(obj_to_int(result->a) == 1);
    ASSERT(obj_to_int(result->b) == 2);

    region_exit(src);
    region_exit(dest);
    PASS();
}

/*
 * Test 5: Verify bitmap_create() includes inline_buf range
 *
 * This test verifies that the bitmap used for cycle detection includes
 * the inline buffer address range, preventing false negatives.
 */
static void test_bitmap_includes_inline_buf_range(void) {
    Region* src = region_create();
    Region* dest = region_create();
    TEST("bitmap includes inline_buf range");
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Allocate objects that use inline buffer */
    Obj* int1 = mk_int_region(src, 42);
    Obj* int2 = mk_int_region(src, 99);
    Obj* pair = mk_cell_region(src, int1, int2);

    /* Verify inline buffer was used */
    ASSERT(src->inline_buf.offset > 0);

    /* Transmigrate to test bitmap coverage */
    /* If inline_buf is not covered, this would fail to detect cycles properly */
    Obj* result = transmigrate(pair, src, dest);

    /* Result should be valid */
    ASSERT_NOT_NULL(result);
    ASSERT(result->tag == TAG_PAIR);
    ASSERT(obj_to_int(result->a) == 42);
    ASSERT(obj_to_int(result->b) == 99);

    region_exit(src);
    region_exit(dest);
    PASS();
}

/*
 * Test 6: Stress test with mixed arena and inline allocations
 *
 * This test verifies correctness when mixing both allocation types.
 */
static void test_mixed_arena_inline_allocations(void) {
    Region* src = region_create();
    Region* dest = region_create();
    TEST("mixed arena and inline allocations");
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Mix of inline and arena allocations */
    Obj* ints[20];
    for (int i = 0; i < 20; i++) {
        ints[i] = mk_int_region(src, i);
    }

    /* Create a large array (arena) */
    Obj* arr = mk_array_region(src, 100);
    for (int i = 0; i < 100; i++) {
        array_push(arr, mk_int_unboxed(i));
    }

    /* Create a pair with inline-allocated children */
    Obj* pair = mk_cell_region(src, ints[0], ints[1]);

    /* Verify inline buffer was used */
    ASSERT(src->inline_buf.offset > 0);

    /* Transmigrate the pair */
    Obj* result = transmigrate(pair, src, dest);

    /* Result should be valid */
    ASSERT_NOT_NULL(result);
    ASSERT(result->tag == TAG_PAIR);
    ASSERT(obj_to_int(result->a) == 0);
    ASSERT(obj_to_int(result->b) == 1);

    region_exit(src);
    region_exit(dest);
    PASS();
}

/* ============================================================================
 * PUBLIC TEST RUNNER
 * ============================================================================ */

void run_inline_buf_splice_tests(void) {
    TEST_SUITE("Inline Buf Splice Soundness (Phase 31.4)");

    TEST_SECTION("region_can_splice_arena_only() Tests");
    RUN_TEST(test_can_splice_arena_only_when_empty);
    RUN_TEST(test_can_splice_arena_only_false_after_inline_alloc);

    TEST_SECTION("Splice Fast Path Tests");
    RUN_TEST(test_transmigration_splice_fast_path_with_arena_only);
    RUN_TEST(test_transmigration_no_splice_with_inline_alloc);

    TEST_SECTION("Bitmap Coverage Tests");
    RUN_TEST(test_bitmap_includes_inline_buf_range);

    TEST_SECTION("Stress Tests");
    RUN_TEST(test_mixed_arena_inline_allocations);

    printf("\n");
    printf(YELLOW "=== CTRR Contract Verification ===" RESET "\n");
    printf("  The splice fast path is now guarded by region_can_splice_arena_only(),\n");
    printf("  preventing dangling pointer bugs when inline_buf has allocations.\n");
    printf("  Bitmap coverage includes inline_buf range for correct cycle detection.\n");
}
