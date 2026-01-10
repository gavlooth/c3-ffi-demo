/*
 * test_region_of_obj.c - Test for omni_obj_region() implementation
 *
 * This tests Issue 1 P1: region_of(obj) mechanism
 *
 * Verification plan:
 * 1. allocate in R1, assert omni_obj_region(obj) == R1
 * 2. allocate in R2, assert omni_obj_region(obj) == R2
 * 3. immediates return NULL
 */

#include "test_framework.h"

void run_region_of_obj_tests(void) {
    TEST_SUITE("region_of_obj");

    TEST("Basic region_of() functionality");
    {
        Region* R1 = region_create();
        Region* R2 = region_create();

        ASSERT_NOT_NULL(R1);
        ASSERT_NOT_NULL(R2);

        /* Test 1: Allocate object in R1, verify it returns R1 */
        Obj* obj1 = mk_int_region(R1, 42);
        Region* region1 = omni_obj_region(obj1);

        if (region1 != R1) {
            FAIL("Object allocated in R1 but omni_obj_region returned wrong region");
            goto cleanup;
        }
        PASS();

        TEST("Object in R2 returns R2");
        {
            Obj* obj2 = mk_int_region(R2, 100);
            Region* region2 = omni_obj_region(obj2);

            ASSERT(region2 == R2);
            PASS();
        }

        TEST("Immediate integer returns NULL");
        {
            Obj* imm_int = mk_int_unboxed(123);
            Region* region_imm_int = omni_obj_region(imm_int);

            ASSERT_NULL(region_imm_int);
            PASS();
        }

        TEST("Immediate boolean returns NULL");
        {
            Obj* imm_bool = mk_bool(1);
            Region* region_imm_bool = omni_obj_region(imm_bool);

            ASSERT_NULL(region_imm_bool);
            PASS();
        }

        TEST("Immediate character returns NULL");
        {
            Obj* imm_char = mk_char_unboxed('A');
            Region* region_imm_char = omni_obj_region(imm_char);

            ASSERT_NULL(region_imm_char);
            PASS();
        }

        TEST("NULL returns NULL");
        {
            Region* region_null = omni_obj_region(NULL);

            ASSERT_NULL(region_null);
            PASS();
        }

cleanup:
        region_exit(R1);
        region_exit(R2);
        region_destroy_if_dead(R1);
        region_destroy_if_dead(R2);
    }
}
