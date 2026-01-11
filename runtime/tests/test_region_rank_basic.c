/*
 * test_region_rank_basic.c - Issue 2 P4.1: Basic lifetime_rank tests
 *
 * Objective: Verify that lifetime_rank field is properly initialized and reset.
 *
 * Test Plan:
 * 1. Create region, verify default rank is 0 (root/global)
 * 2. Reset region, verify rank resets to default
 * 3. Verify rank can be manually set (for codegen use)
 */

#include "test_framework.h"

void run_region_rank_basic_tests(void) {
    TEST_SUITE("Region Rank Basic Tests (Issue 2 P4.1)");

    TEST("region create has rank 0");
    {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);
        ASSERT(r->lifetime_rank == 0);
        region_exit(r);
        PASS();
    }

    TEST("region reset has rank 0");
    {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);

        /* Explicitly set a non-zero rank (simulating codegen assignment) */
        r->lifetime_rank = 5;

        /* Reset the region (simulating pool reuse) */
        region_reset(r);

        ASSERT(r->lifetime_rank == 0);
        region_exit(r);
        PASS();
    }

    TEST("region rank can be set manually");
    {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);

        /* Simulate codegen assignment: _local_region->lifetime_rank = _caller_region->lifetime_rank + 1 */
        r->lifetime_rank = 1;
        ASSERT(r->lifetime_rank == 1);

        r->lifetime_rank = 42;
        ASSERT(r->lifetime_rank == 42);

        region_reset(r);
        ASSERT(r->lifetime_rank == 0);

        region_exit(r);
        PASS();
    }

    TEST("multiple regions have independent ranks");
    {
        Region* r1 = region_create();
        Region* r2 = region_create();
        Region* r3 = region_create();

        ASSERT(r1->lifetime_rank == 0);
        ASSERT(r2->lifetime_rank == 0);
        ASSERT(r3->lifetime_rank == 0);

        /* Simulate nested regions: r1 (caller), r2 (local), r3 (nested) */
        r1->lifetime_rank = 0;  /* root/caller */
        r2->lifetime_rank = 1;  /* nested in r1 */
        r3->lifetime_rank = 2;  /* nested in r2 */

        ASSERT(r1->lifetime_rank == 0);
        ASSERT(r2->lifetime_rank == 1);
        ASSERT(r3->lifetime_rank == 2);

        region_exit(r1);
        region_exit(r2);
        region_exit(r3);
        PASS();
    }

    TEST("pooled region gets fresh rank");
    {
        Region* r1 = region_create();
        ASSERT_NOT_NULL(r1);

        /* Set a non-zero rank */
        r1->lifetime_rank = 7;
        ASSERT(r1->lifetime_rank == 7);

        /* Exit returns region to pool (if pool not full) */
        region_exit(r1);

        /* Create another region - might get the same one from pool */
        Region* r2 = region_create();
        ASSERT_NOT_NULL(r2);
        ASSERT(r2->lifetime_rank == 0);

        region_exit(r2);
        PASS();
    }
}
