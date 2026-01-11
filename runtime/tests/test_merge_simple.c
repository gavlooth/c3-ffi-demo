#include "test_framework.h"

void run_store_barrier_merge_tests(void) {
    TEST_SUITE("Store Barrier Merge Tests (Issue 2 P5)");

    TEST("merge permitted same thread");
    {
        Region* r1 = region_create();
        Region* r2 = region_create();
        ASSERT(region_merge_permitted(r1, r2) == true);
        region_exit(r1);
        region_destroy_if_dead(r1);
        region_exit(r2);
        region_destroy_if_dead(r2);
        PASS();
    }

    TEST("merge permitted cross thread");
    {
        Region* r1 = region_create();
        Region* r2 = region_create();
        r2->owner_thread = (pthread_t)0xDEADBEEF;
        ASSERT(region_merge_permitted(r1, r2) == false);
        region_exit(r1);
        region_destroy_if_dead(r1);
        region_exit(r2);
        region_destroy_if_dead(r2);
        PASS();
    }

    TEST("merge permitted inline allocs");
    {
        Region* r1 = region_create();
        Region* r2 = region_create();
        Obj* obj = mk_int_region(r1, 42);
        ASSERT(region_merge_permitted(r1, r2) == false);
        dec_ref(obj);
        region_exit(r1);
        region_destroy_if_dead(r1);
        region_exit(r2);
        region_destroy_if_dead(r2);
        PASS();
    }

    TEST("merge safe basic");
    {
        Region* src = region_create();
        Region* dst = region_create();
        char* data = (char*)region_alloc(src, 512);
        ASSERT(data != NULL);
        strcpy(data, "test data");
        int result = region_merge_safe(src, dst);
        ASSERT(result == 0);
        ASSERT(strcmp(data, "test data") == 0);
        region_exit(src);
        region_destroy_if_dead(src);
        region_exit(dst);
        region_destroy_if_dead(dst);
        PASS();
    }

    TEST("merge safe inline allocs fails");
    {
        Region* src = region_create();
        Region* dst = region_create();
        Obj* obj = mk_int_region(src, 42);
        int result = region_merge_safe(src, dst);
        ASSERT(result == -1);
        dec_ref(obj);
        region_exit(src);
        region_destroy_if_dead(src);
        region_exit(dst);
        region_destroy_if_dead(dst);
        PASS();
    }

    TEST("store barrier chooses transmigrate small");
    {
        Region* src = region_create();
        Region* dst = region_create();
        omni_region_set_lifetime_rank(dst, 0);
        omni_region_set_lifetime_rank(src, 1);
        Obj* small_value = mk_int_region(src, 42);
        Obj* container = mk_cell_region(dst, mk_int_region(dst, 0), mk_int_region(dst, 0));
        Obj* result = omni_store_repair(container, &container->a, small_value);
        ASSERT(result != NULL);
        /* Small region should transmigrate (copy), not merge */
        ASSERT(small_value != result);
        region_exit(src);
        region_destroy_if_dead(src);
        region_exit(dst);
        region_destroy_if_dead(dst);
        PASS();
    }

    TEST("store barrier checks merge threshold for large regions");
    {
        Region* src = region_create();
        Region* dst = region_create();
        omni_region_set_lifetime_rank(dst, 0);
        omni_region_set_lifetime_rank(src, 1);
        
        /* Allocate large nested pair in src (> threshold) */
        Obj* large_value = mk_cell_region(src, mk_int_region(src, 999), mk_int_region(src, 888));
        for (int i = 0; i < 100; i++) {
            large_value = mk_cell_region(src, large_value, mk_int_region(src, i));
        }
        
        /* Verify src is now large (> 4KB threshold) */
        ASSERT(src->bytes_allocated_total >= 4096);
        
        /* Create container in dst */
        Obj* container = mk_cell_region(dst, mk_int_region(dst, 0), mk_int_region(dst, 0));
        
        /* Store large_value into container (older <- younger): should check threshold */
        /* Even though src is large, merge not permitted due to inline allocs */
        /* So should fallback to transmigrate */
        Obj* result = omni_store_repair(container, &container->a, large_value);
        
        ASSERT(result != NULL);
        /* Since merge not permitted, should transmigrate (copy) */
        ASSERT(result != large_value);
        
        region_exit(src);
        region_destroy_if_dead(src);
        region_exit(dst);
        region_destroy_if_dead(dst);
        PASS();
    }

    TEST("merge threshold default");
    {
        size_t threshold = get_merge_threshold();
        ASSERT(threshold == 4096);
        PASS();
    }
}
