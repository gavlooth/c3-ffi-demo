/*
 * Phase 34.2 - Array has_boxed_elems Flag Tests
 *
 * This is a targeted performance contract:
 * - Arrays that are known to contain only immediates should be trace-free.
 * - We implement this by tracking a monotonic "has_boxed_elems" flag in Array.
 *
 * These tests verify the flag is maintained and preserved across transmigration.
 */

#define _POSIX_C_SOURCE 200809L

#include "test_framework.h"

static void test_array_flag_false_for_immediate_only(void) {
    TEST("array has_boxed_elems stays false for immediates");

    Region* r = region_create();
    ASSERT_NOT_NULL(r);

    Obj* arr_obj = mk_array_region(r, 16);
    ASSERT_NOT_NULL(arr_obj);
    ASSERT_EQ(arr_obj->tag, TAG_ARRAY);

    Array* arr = (Array*)arr_obj->ptr;
    ASSERT_NOT_NULL(arr);
    ASSERT_FALSE(arr->has_boxed_elems);

    for (int i = 0; i < 16; i++) {
        array_push(arr_obj, mk_int_unboxed(i));
    }

    ASSERT_EQ(arr->len, 16);
    ASSERT_FALSE(arr->has_boxed_elems);

    region_exit(r);
    PASS();
}

static void test_array_flag_turns_true_when_boxed_pushed(void) {
    TEST("array has_boxed_elems turns true when boxed value inserted");

    Region* r = region_create();
    ASSERT_NOT_NULL(r);

    Obj* arr_obj = mk_array_region(r, 8);
    ASSERT_NOT_NULL(arr_obj);

    Array* arr = (Array*)arr_obj->ptr;
    ASSERT_NOT_NULL(arr);
    ASSERT_FALSE(arr->has_boxed_elems);

    array_push(arr_obj, mk_int_unboxed(1));
    ASSERT_FALSE(arr->has_boxed_elems);

    array_push(arr_obj, mk_sym_region(r, "x"));
    ASSERT_TRUE(arr->has_boxed_elems);

    region_exit(r);
    PASS();
}

static void test_array_flag_preserved_across_transmigrate(void) {
    TEST("array has_boxed_elems preserved across transmigrate");

    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Immediate-only array */
    Obj* imm_arr_obj = mk_array_region(src, 32);
    for (int i = 0; i < 32; i++) {
        array_push(imm_arr_obj, mk_int_unboxed(i));
    }
    Array* imm_arr = (Array*)imm_arr_obj->ptr;
    ASSERT_NOT_NULL(imm_arr);
    ASSERT_FALSE(imm_arr->has_boxed_elems);

    Obj* moved_imm_arr_obj = (Obj*)transmigrate(imm_arr_obj, src, dest);
    ASSERT_NOT_NULL(moved_imm_arr_obj);
    Array* moved_imm_arr = (Array*)moved_imm_arr_obj->ptr;
    ASSERT_NOT_NULL(moved_imm_arr);
    ASSERT_FALSE(moved_imm_arr->has_boxed_elems);
    ASSERT_EQ(array_length(moved_imm_arr_obj), 32);

    /* Mixed array */
    Obj* mixed_arr_obj = mk_array_region(src, 4);
    array_push(mixed_arr_obj, mk_int_unboxed(1));
    array_push(mixed_arr_obj, mk_sym_region(src, "y"));
    Array* mixed_arr = (Array*)mixed_arr_obj->ptr;
    ASSERT_NOT_NULL(mixed_arr);
    ASSERT_TRUE(mixed_arr->has_boxed_elems);

    Obj* moved_mixed_arr_obj = (Obj*)transmigrate(mixed_arr_obj, src, dest);
    ASSERT_NOT_NULL(moved_mixed_arr_obj);
    Array* moved_mixed_arr = (Array*)moved_mixed_arr_obj->ptr;
    ASSERT_NOT_NULL(moved_mixed_arr);
    ASSERT_TRUE(moved_mixed_arr->has_boxed_elems);
    ASSERT_EQ(array_length(moved_mixed_arr_obj), 2);

    region_exit(src);
    region_exit(dest);
    PASS();
}

void run_array_boxed_flag_tests(void) {
    TEST_SUITE("Array boxed flag (Phase 34.2)");

    TEST_SECTION("Flag Maintenance");
    RUN_TEST(test_array_flag_false_for_immediate_only);
    RUN_TEST(test_array_flag_turns_true_when_boxed_pushed);

    TEST_SECTION("Transmigration");
    RUN_TEST(test_array_flag_preserved_across_transmigrate);
}

