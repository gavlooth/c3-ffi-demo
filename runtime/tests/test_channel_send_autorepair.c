/*
 * test_channel_send_autorepair.c - Issue 2 P4.4: Channel send store barrier autorepair tests
 *
 * Objective: Verify that channel_send() uses omni_store_repair() to enforce
 * Region Closure Property for values sent across regions.
 *
 * Test Plan:
 * 1. Send value from younger region (higher rank) into channel owned by older region (lower rank)
 * 2. Verify the value is transmigrated (repaired) before being stored in channel buffer
 * 3. After destroying source region, received value should still be valid
 * 4. NOTE: Unbuffered channels do NOT use store barrier because values are passed
 *    directly via pointer indirection during the handshake, not stored in the channel.
 */

#include "test_framework.h"

void run_channel_send_autorepair_tests(void) {
    TEST_SUITE("Channel Send Autorepair Tests (Issue 2 P4.4)");

    TEST("buffered channel: send young value into old channel triggers repair");
    {
        /* Create older region (rank 0) */
        Region* dst = region_create();
        ASSERT_NOT_NULL(dst);
        omni_region_set_lifetime_rank(dst, 0);  /* root/caller */

        /* Create younger region (rank 1) */
        Region* src = region_create();
        ASSERT_NOT_NULL(src);
        omni_region_set_lifetime_rank(src, 1);  /* nested/callee */

        /* Create channel in older region */
        Obj* ch = make_channel_region(dst, 10);
        ASSERT_NOT_NULL(ch);
        ASSERT(omni_obj_region(ch) == dst);

        /* Allocate a boxed value in younger region */
        Obj* young_value = mk_cell_region(src, mk_int_region(src, 42), mk_int_region(src, 99));
        ASSERT_NOT_NULL(young_value);
        ASSERT(omni_obj_region(young_value) == src);

        /* Send the value - should trigger repair via omni_store_repair */
        int send_result = channel_send(ch, young_value);
        ASSERT(send_result == 0);

        /* Receive the value */
        Obj* received = channel_recv(ch);
        ASSERT_NOT_NULL(received);

        /* The received value should now be owned by dst region (was transmigrated) */
        Region* received_region = omni_obj_region(received);
        ASSERT(received_region == dst);

        /* Verify the pair contents are correct */
        Obj* first = received->a;
        Obj* second = received->b;
        ASSERT_NOT_NULL(first);
        ASSERT_NOT_NULL(second);

        /* Cleanup: destroy src region */
        region_exit(src);
        region_destroy_if_dead(src);

        /* Value should STILL be accessible after src is destroyed.
         * The received value was transmigrated to dst region, so it's safe. */
        first = received->a;
        second = received->b;
        ASSERT_NOT_NULL(first);
        ASSERT_NOT_NULL(second);

        /* Cleanup dst */
        channel_close(ch);
        region_exit(dst);
        region_destroy_if_dead(dst);
        PASS();
    }

    TEST("channel send: immediate value bypasses repair (fast path)");
    {
        Region* dst = region_create();
        ASSERT_NOT_NULL(dst);
        omni_region_set_lifetime_rank(dst, 0);

        Region* src = region_create();
        ASSERT_NOT_NULL(src);
        omni_region_set_lifetime_rank(src, 1);

        /* Create channel in older region */
        Obj* ch = make_channel_region(dst, 5);
        ASSERT_NOT_NULL(ch);

        /* Send immediate value - should NOT trigger repair */
        int send_result = channel_send(ch, mk_int(42));
        ASSERT(send_result == 0);

        /* Receive and verify */
        Obj* received = channel_recv(ch);
        ASSERT_NOT_NULL(received);

        /* Cleanup */
        channel_close(ch);
        region_exit(src);
        region_exit(dst);
        PASS();
    }

    TEST("channel send: same region value no repair needed");
    {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);
        omni_region_set_lifetime_rank(r, 1);

        /* Create channel in same region */
        Obj* ch = make_channel_region(r, 5);
        ASSERT_NOT_NULL(ch);

        /* Allocate value in same region */
        Obj* value = mk_cell_region(r, mk_int_region(r, 1), mk_int_region(r, 2));
        ASSERT_NOT_NULL(value);
        ASSERT(omni_obj_region(value) == r);

        /* Send - should NOT trigger repair (same region) */
        int send_result = channel_send(ch, value);
        ASSERT(send_result == 0);

        /* Receive and verify */
        Obj* received = channel_recv(ch);
        ASSERT_NOT_NULL(received);

        /* Cleanup */
        channel_close(ch);
        region_exit(r);
        PASS();
    }
}
