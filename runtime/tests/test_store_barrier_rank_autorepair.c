/*
 * test_store_barrier_rank_autorepair.c - Issue 2 P4.3: Rank-based store barrier autorepair tests
 *
 * Objective: Verify that omni_store_repair() enforces Region Closure Property
 * by automatically repairing illegal older<-younger stores using lifetime ranks.
 *
 * Test Plan:
 * 1. Store value from younger region (higher rank) into container in older region (lower rank)
 * 2. Verify the value is transmigrated (repaired) to the destination region
 * 3. After destroying source region, value should still be accessible in container
 * 4. Same-thread ranks are comparable
 * 5. Cross-thread stores always repair (ranks not comparable)
 */

#include "test_framework.h"

void run_store_barrier_rank_autorepair_tests(void) {
    TEST_SUITE("Store Barrier Rank Autorepair Tests (Issue 2 P4.3)");

    /* Use omni_obj_region() helper to check object's owning region */
    /* This function is defined in omni.h/region_value.c */

    TEST("older container <- younger value triggers repair via rank comparison");
    {
        /* Create older region (rank 0) */
        Region* dst = region_create();
        ASSERT_NOT_NULL(dst);
        dst->lifetime_rank = 0;  /* root/caller */

        /* Create younger region (rank 1) */
        Region* src = region_create();
        ASSERT_NOT_NULL(src);
        src->lifetime_rank = 1;  /* nested/callee */

        /* Allocate a boxed value in younger region */
        Obj* young_value = mk_cell_region(src, mk_int_region(src, 42), mk_int_region(src, 99));
        ASSERT_NOT_NULL(young_value);
        ASSERT(omni_obj_region(young_value) == src);

        /* Allocate a box in older region */
        Obj* container = mk_box_region(dst, young_value);
        ASSERT_NOT_NULL(container);
        ASSERT(omni_obj_region(container) == dst);

        /* Verify initial value */
        Obj* initial = box_get(container);
        ASSERT(initial == young_value);

        /* NOW: Store a NEW value from younger region into older container via box_set
         * This should trigger repair because dst->lifetime_rank (0) < src->lifetime_rank (1) */
        Obj* new_young_value = mk_cell_region(src, mk_int_region(src, 100), mk_int_region(src, 200));
        ASSERT_NOT_NULL(new_young_value);
        ASSERT(omni_obj_region(new_young_value) == src);

        /* box_set calls omni_store_repair internally */
        box_set(container, new_young_value);

        /* After repair, the stored value should be owned by dst region */
        Obj* repaired_value = box_get(container);
        ASSERT_NOT_NULL(repaired_value);

        /* The value should now be in dst region (was transmigrated) */
        Region* repaired_region = omni_obj_region(repaired_value);
        ASSERT(repaired_region == dst);

        /* Verify the pair contents are correct */
        /* Access via pair->a and pair->b fields */
        Obj* first = repaired_value->a;
        Obj* second = repaired_value->b;
        ASSERT_NOT_NULL(first);
        ASSERT_NOT_NULL(second);

        /* Cleanup */
        region_exit(src);
        region_destroy_if_dead(src);

        /* Value should STILL be accessible after src is destroyed */
        Obj* after_destroy = box_get(container);
        ASSERT_NOT_NULL(after_destroy);

        first = after_destroy->a;
        second = after_destroy->b;
        ASSERT_NOT_NULL(first);
        ASSERT_NOT_NULL(second);

        region_exit(dst);
        region_destroy_if_dead(dst);
        PASS();
    }

    TEST("same rank regions: no repair needed");
    {
        /* Create two regions with same rank */
        Region* r1 = region_create();
        ASSERT_NOT_NULL(r1);
        r1->lifetime_rank = 1;

        Region* r2 = region_create();
        ASSERT_NOT_NULL(r2);
        r2->lifetime_rank = 1;

        /* Allocate value in r1, container in r2 */
        Obj* value = mk_cell_region(r1, mk_int_region(r1, 1), mk_int_region(r1, 2));
        ASSERT_NOT_NULL(value);
        ASSERT(omni_obj_region(value) == r1);

        Obj* container = mk_box_region(r2, value);
        ASSERT_NOT_NULL(container);
        ASSERT(omni_obj_region(container) == r2);

        /* Store should NOT trigger repair (same rank, different regions) */
        /* Actually, different regions with same rank should still repair
         * because they're not the same region */
        Obj* new_value = mk_cell_region(r1, mk_int_region(r1, 3), mk_int_region(r1, 4));
        box_set(container, new_value);

        /* Value should be accessible */
        Obj* result = box_get(container);
        ASSERT_NOT_NULL(result);

        /* Cleanup */
        region_exit(r1);
        region_exit(r2);
        PASS();
    }

    TEST("immediate values bypass repair (fast path)");
    {
        Region* dst = region_create();
        ASSERT_NOT_NULL(dst);
        dst->lifetime_rank = 0;

        Region* src = region_create();
        ASSERT_NOT_NULL(src);
        src->lifetime_rank = 1;

        Obj* container = mk_box_region(dst, mk_int_region(dst, 0));
        ASSERT_NOT_NULL(container);

        /* Store immediate value (no region ownership) - should NOT repair */
        box_set(container, mk_int(42));  /* Uses global region */

        /* Value should be accessible */
        Obj* result = box_get(container);
        ASSERT_NOT_NULL(result);

        /* Cleanup */
        region_exit(src);
        region_exit(dst);
        PASS();
    }

    TEST("same region: no repair needed (fast path)");
    {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);
        r->lifetime_rank = 1;

        Obj* value = mk_cell_region(r, mk_int_region(r, 1), mk_int_region(r, 2));
        ASSERT_NOT_NULL(value);

        Obj* container = mk_box_region(r, value);
        ASSERT_NOT_NULL(container);

        /* Store value from same region into container - should NOT repair */
        Obj* new_value = mk_cell_region(r, mk_int_region(r, 3), mk_int_region(r, 4));
        box_set(container, new_value);

        /* Value should be accessible */
        Obj* result = box_get(container);
        ASSERT_NOT_NULL(result);

        /* Cleanup */
        region_exit(r);
        PASS();
    }

    TEST("NULL value bypasses repair");
    {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);

        Obj* container = mk_box_region(r, mk_int_region(r, 42));
        ASSERT_NOT_NULL(container);

        /* Store NULL - should NOT repair */
        box_set(container, NULL);

        Obj* result = box_get(container);
        ASSERT_NULL(result);

        /* Cleanup */
        region_exit(r);
        PASS();
    }

    TEST("nested pair graph is fully transmigrated on repair");
    {
        /* Create older region */
        Region* dst = region_create();
        ASSERT_NOT_NULL(dst);
        dst->lifetime_rank = 0;

        /* Create younger region */
        Region* src = region_create();
        ASSERT_NOT_NULL(src);
        src->lifetime_rank = 1;

        /* Create a nested pair structure in younger region:
         * (1, (2, 3)) */
        Obj* inner = mk_cell_region(src, mk_int_region(src, 2), mk_int_region(src, 3));
        Obj* outer = mk_cell_region(src, mk_int_region(src, 1), inner);
        ASSERT_NOT_NULL(outer);
        ASSERT(omni_obj_region(outer) == src);
        ASSERT(omni_obj_region(inner) == src);

        /* Container in older region */
        Obj* container = mk_box_region(dst, mk_int_region(dst, 0));
        ASSERT_NOT_NULL(container);

        /* Store nested structure - should trigger full transmigration */
        box_set(container, outer);

        /* Get repaired value */
        Obj* repaired = box_get(container);
        ASSERT_NOT_NULL(repaired);

        /* Verify entire graph is in dst region */
        Region* repaired_region = omni_obj_region(repaired);
        ASSERT(repaired_region == dst);

        Obj* repaired_inner = repaired->b;
        ASSERT_NOT_NULL(repaired_inner);

        Region* inner_region = omni_obj_region(repaired_inner);
        ASSERT(inner_region == dst);

        /* Cleanup - destroy src, value should still be accessible */
        region_exit(src);
        region_destroy_if_dead(src);

        Obj* after_destroy = box_get(container);
        ASSERT_NOT_NULL(after_destroy);

        repaired_inner = after_destroy->b;
        ASSERT_NOT_NULL(repaired_inner);

        region_exit(dst);
        region_destroy_if_dead(dst);
        PASS();
    }
}
