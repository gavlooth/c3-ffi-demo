/*
 * test_region_accounting.c - Test for Issue 2 P3 region accounting counters
 *
 * Verification plan:
 * 1. Allocate known sizes; assert `bytes_allocated_total` matches sum of requested sizes (aligned)
 * 2. Force arena growth; assert `chunk_count` increments
 * 3. Force inline allocations; assert `inline_buf_used_bytes` tracks peak offset
 */

#include "test_framework.h"

void run_region_accounting_tests(void) {
    TEST_SUITE("region_accounting");

    TEST("bytes_allocated_total tracks allocations");
    {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);
        
        size_t initial_bytes = r->bytes_allocated_total;
        ASSERT_EQ(initial_bytes, 0);

        /* Allocate one object */
        /* mk_int_region calls alloc_obj_region -> alloc_obj_typed -> region_alloc_typed */
        Obj* obj1 = mk_int_region(r, 1);
        ASSERT_NOT_NULL(obj1);
        
        /* Check that bytes increased by at least sizeof(Obj) */
        size_t after_alloc1 = r->bytes_allocated_total;
        ASSERT(after_alloc1 >= sizeof(Obj));
        
        /* Allocate another */
        Obj* obj2 = mk_int_region(r, 2);
        ASSERT_NOT_NULL(obj2);
        
        size_t after_alloc2 = r->bytes_allocated_total;
        ASSERT(after_alloc2 >= after_alloc1 + sizeof(Obj));
        
        /* Cleanup */
        region_exit(r);
        region_destroy_if_dead(r);
        PASS();
    }

    TEST("chunk_count increments on arena growth");
    {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);
        
        size_t initial_chunks = r->chunk_count;
        
        /* Force allocation larger than typical inline buffer/default chunk */
        /* Allocate 1MB buffer */
        size_t large_size = 1024 * 1024;
        void* ptr = region_alloc(r, large_size);
        ASSERT_NOT_NULL(ptr);
        
        /* Should have allocated a new chunk */
        ASSERT(r->chunk_count > initial_chunks);

        /* Cleanup */
        region_exit(r);
        region_destroy_if_dead(r);
        PASS();
    }

    TEST("inline_buf_used_bytes tracks inline usage");
    {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);
        
        /* Assuming new region starts with empty inline buffer */
        size_t initial_inline = r->inline_buf_used_bytes;
        ASSERT_EQ(initial_inline, 0);

        /* Allocate small object - should go to inline buffer if enabled */
        /* Note: This depends on region configuration. Assuming defaults allow inline. */
        Obj* obj1 = mk_int_region(r, 1);
        ASSERT_NOT_NULL(obj1);
        
        /* If inline buffer is used, usage should increase */
        /* If inline buffer is NOT used (e.g. disabled in build), this assertion might be tricky. */
        /* But we can check if it's > 0 OR if bytes_allocated_total increased. */
        /* The requirement is that IF inline buf is used, this tracks it. */
        
        /* Buffer is always present in struct, assume inline enabled for small allocs */
        ASSERT(r->inline_buf_used_bytes > 0);
        ASSERT(r->inline_buf_used_bytes >= sizeof(Obj));

        /* Cleanup */
        region_exit(r);
        region_destroy_if_dead(r);
        PASS();
    }

    TEST("bytes_allocated_peak tracks maximum allocation");
    {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);

        ASSERT_EQ(r->bytes_allocated_peak, 0);

        /* Initial allocation */
        Obj* obj1 = mk_int_region(r, 1);
        ASSERT_NOT_NULL(obj1);
        size_t peak1 = r->bytes_allocated_peak;
        ASSERT(peak1 > 0);
        ASSERT_EQ(peak1, r->bytes_allocated_total);

        /* Allocate more */
        Obj* obj2 = mk_int_region(r, 2);
        ASSERT_NOT_NULL(obj2);
        size_t peak2 = r->bytes_allocated_peak;
        ASSERT(peak2 > peak1);
        ASSERT_EQ(peak2, r->bytes_allocated_total);

        /* Cleanup */
        region_exit(r);
        region_destroy_if_dead(r);
        PASS();
    }

    TEST("counters reset on region_reset");
    {
        Region* r = region_create();
        ASSERT_NOT_NULL(r);

        /* Allocate to increment counters */
        Obj* obj1 = mk_int_region(r, 1);
        ASSERT_NOT_NULL(obj1);
        /* Force chunk alloc too */
        void* ptr = region_alloc(r, 1024*1024);
        ASSERT_NOT_NULL(ptr);
        
        ASSERT(r->bytes_allocated_total > 0);
        ASSERT(r->chunk_count > 0);

        /* Reset region */
        region_reset(r);

        /* Counters should be zero */
        ASSERT_EQ(r->bytes_allocated_total, 0);
        ASSERT_EQ(r->bytes_allocated_peak, 0);
        ASSERT_EQ(r->chunk_count, 0);
        ASSERT_EQ(r->inline_buf_used_bytes, 0);

        /* Allocate again */
        Obj* obj2 = mk_int_region(r, 2);
        ASSERT_NOT_NULL(obj2);

        /* Counters should track new allocations */
        ASSERT(r->bytes_allocated_total > 0);

        /* Cleanup */
        region_exit(r);
        region_destroy_if_dead(r);
        PASS();
    }
}