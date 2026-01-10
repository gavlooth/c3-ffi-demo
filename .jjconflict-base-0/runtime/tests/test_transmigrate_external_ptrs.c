/*
 * Phase 34.1 - External Pointer Filtering Tests
 *
 * CTRR rule: transmigrate() should repair pointers *into the closing region*.
 * It must NOT clone boxed objects that are not owned by src_region, because:
 * - it's wasted work (performance regression)
 * - it can break identity-sensitive semantics (eq) if external objects are duplicated
 *
 * These tests lock the behavior down.
 */

#define _POSIX_C_SOURCE 200809L

#include "test_framework.h"

static void test_transmigrate_preserves_external_boxed_identity(void) {
    TEST("transmigrate preserves external boxed pointer identity");

    Region* ext = region_create();
    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(ext);
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Create a boxed symbol in an external region. */
    Obj* ext_sym = mk_sym_region(ext, "external");
    ASSERT_NOT_NULL(ext_sym);
    ASSERT_TRUE(IS_BOXED(ext_sym));
    ASSERT_EQ(ext_sym->tag, TAG_SYM);

    /* Create a local list in src region. */
    Obj* local_list = NULL;
    for (int i = 0; i < 10; i++) {
        local_list = mk_cell_region(src, mk_int_unboxed(i), local_list);
    }
    ASSERT_NOT_NULL(local_list);

    /* Root references ext_sym (external) and local_list (src-owned). */
    Obj* root = mk_cell_region(src, ext_sym, local_list);
    ASSERT_NOT_NULL(root);

    Obj* moved = (Obj*)transmigrate(root, src, dest);
    ASSERT_NOT_NULL(moved);
    ASSERT_EQ(moved->tag, TAG_PAIR);

    /* Critical assertion: external symbol pointer identity must be preserved. */
    ASSERT_TRUE(moved->a == ext_sym);

    /* Local list must still be a list, but should be a different allocation (copied). */
    ASSERT_NOT_NULL(moved->b);
    ASSERT_TRUE(moved->b != local_list);
    ASSERT_EQ(((Obj*)moved->b)->tag, TAG_PAIR);

    /* After src exits, moved must remain valid, and ext_sym must still be valid. */
    region_exit(src);

    ASSERT_EQ(moved->tag, TAG_PAIR);
    ASSERT_TRUE(moved->a == ext_sym);
    ASSERT_EQ(ext_sym->tag, TAG_SYM);

    region_exit(dest);
    region_exit(ext);
    PASS();
}

static void test_transmigrate_empty_src_is_noop_for_external_root(void) {
    TEST("transmigrate with empty src returns external root (no abort)");

    Region* ext = region_create();
    Region* src = region_create();   /* stays empty */
    Region* dest = region_create();
    ASSERT_NOT_NULL(ext);
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    Obj* ext_sym = mk_sym_region(ext, "external");
    ASSERT_NOT_NULL(ext_sym);
    ASSERT_TRUE(IS_BOXED(ext_sym));

    /* src has no allocations; transmigrate should be a no-op and must not abort. */
    Obj* result = (Obj*)transmigrate(ext_sym, src, dest);
    ASSERT_TRUE(result == ext_sym);

    region_exit(dest);
    region_exit(src);
    region_exit(ext);
    PASS();
}

void run_transmigrate_external_ptrs_tests(void) {
    TEST_SUITE("CTRR Transmigration External Ptr Filtering (Phase 34.1)");

    TEST_SECTION("Identity Preservation");
    RUN_TEST(test_transmigrate_preserves_external_boxed_identity);

    TEST_SECTION("Empty Src Region");
    RUN_TEST(test_transmigrate_empty_src_is_noop_for_external_root);
}

