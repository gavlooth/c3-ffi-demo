/*
 * Phase 35 (P0) - Dense Forwarding Table Remap Mode
 *
 * This is a correctness test *and* a “mode selection” test.
 *
 * Why:
 * - The forwarding table remap is a performance optimization. Performance is
 *   difficult to assert in CI, so we instead assert that:
 *   1) the forwarding-table code path can be forced at compile time
 *   2) semantics remain correct under that forced mode
 *
 * How it works:
 * - `runtime/src/memory/transmigrate.c` exposes a debug accessor
 *   `omni_transmigrate_debug_used_forwarding_table()` when built with
 *   `-DOMNI_TRANSMIGRATE_DEBUG_REMAP_MODE`.
 * - The `runtime/tests` Makefile has a `test-fwd` target that compiles the test
 *   binary with:
 *     - `OMNI_REMAP_FORCE_FORWARDING`
 *     - `OMNI_TRANSMIGRATE_DEBUG_REMAP_MODE`
 *
 * Contract:
 * - In forced-forwarding mode, transmigrate must actually use the forwarding
 *   table (not silently fall back to the robin-hood hash remap).
 */

/* test_framework.h is provided by test_main.c */

#ifdef OMNI_REMAP_FORCE_FORWARDING
extern int omni_transmigrate_debug_used_forwarding_table(void);

static void test_transmigrate_uses_forwarding_table_when_forced(void) {
    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* A simple, representative graph: list of immediate ints. */
    Obj* list = NULL;
    for (int i = 0; i < 100; i++) {
        list = mk_cell_region(src, mk_int_unboxed(i), list);
    }
    ASSERT_NOT_NULL(list);

    Obj* moved = (Obj*)transmigrate(list, src, dest);
    ASSERT_NOT_NULL(moved);

    /* The whole point of this test: verify the remap strategy path. */
    ASSERT_TRUE(omni_transmigrate_debug_used_forwarding_table() != 0);

    region_exit(src);
    region_exit(dest);
}

void run_transmigrate_forwarding_table_tests(void) {
    TEST_SUITE("CTRR Transmigration Forwarding Table (Phase 35 P0)");

    TEST_SECTION("Forced Mode Selection");
    RUN_TEST(test_transmigrate_uses_forwarding_table_when_forced);
}
#else
/* If not forcing forwarding, do not reference the debug symbol. */
void run_transmigrate_forwarding_table_tests(void) { }
#endif

