/* test_arena.c - Comprehensive arena allocator tests */
#include "test_framework.h"

/* ========== Arena Creation Tests ========== */

void test_arena_create(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    ASSERT_NOT_NULL(a);
    ASSERT_NULL(a->current);
    ASSERT_NULL(a->blocks);
    ASSERT_EQ(a->block_size, ARENA_BLOCK_SIZE);
    arena_destroy(a);
    PASS();
}

void test_arena_destroy_null(void) {
    arena_destroy(NULL);  /* Should not crash */
    PASS();
}

void test_arena_destroy_empty(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    arena_destroy(a);  /* Destroy empty arena */
    PASS();
}

/* ========== Arena Allocation Tests ========== */

void test_arena_mk_int(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    Obj* x = arena_mk_int(a, 42);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->tag, TAG_INT);
    ASSERT_EQ(x->i, 42);
    ASSERT_EQ(x->mark, -2);  /* Arena-allocated mark */
    arena_destroy(a);
    PASS();
}

void test_arena_mk_int_negative(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    Obj* x = arena_mk_int(a, -100);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->i, -100);
    arena_destroy(a);
    PASS();
}

void test_arena_mk_int_zero(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    Obj* x = arena_mk_int(a, 0);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->i, 0);
    arena_destroy(a);
    PASS();
}

void test_arena_mk_pair(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    Obj* car = arena_mk_int(a, 1);
    Obj* cdr = arena_mk_int(a, 2);
    Obj* p = arena_mk_pair(a, car, cdr);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->tag, TAG_PAIR);
    ASSERT_EQ(p->is_pair, 1);
    ASSERT_EQ(p->a, car);
    ASSERT_EQ(p->b, cdr);
    ASSERT_EQ(p->mark, -2);
    arena_destroy(a);
    PASS();
}

void test_arena_mk_pair_null_car(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    Obj* cdr = arena_mk_int(a, 1);
    Obj* p = arena_mk_pair(a, NULL, cdr);
    ASSERT_NOT_NULL(p);
    ASSERT_NULL(p->a);
    ASSERT_EQ(p->b, cdr);
    arena_destroy(a);
    PASS();
}

void test_arena_mk_pair_null_cdr(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    Obj* car = arena_mk_int(a, 1);
    Obj* p = arena_mk_pair(a, car, NULL);
    ASSERT_NOT_NULL(p);
    ASSERT_EQ(p->a, car);
    ASSERT_NULL(p->b);
    arena_destroy(a);
    PASS();
}

void test_arena_mk_pair_both_null(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    Obj* p = arena_mk_pair(a, NULL, NULL);
    ASSERT_NOT_NULL(p);
    ASSERT_NULL(p->a);
    ASSERT_NULL(p->b);
    arena_destroy(a);
    PASS();
}

/* ========== Arena Block Management Tests ========== */

void test_arena_multiple_allocations(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    Obj* objs[100];
    for (int i = 0; i < 100; i++) {
        objs[i] = arena_mk_int(a, i);
        ASSERT_NOT_NULL(objs[i]);
        ASSERT_EQ(objs[i]->i, i);
    }
    arena_destroy(a);
    PASS();
}

void test_arena_block_creation(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    /* Allocate enough to force multiple blocks */
    int count = (ARENA_BLOCK_SIZE / sizeof(Obj)) + 10;
    for (int i = 0; i < count; i++) {
        Obj* x = arena_mk_int(a, i);
        ASSERT_NOT_NULL(x);
    }
    /* Verify blocks were created */
    ASSERT_NOT_NULL(a->blocks);
    arena_destroy(a);
    PASS();
}

/* ========== Arena Reset Tests ========== */

void test_arena_reset_empty(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    arena_reset(a);  /* Should not crash */
    arena_destroy(a);
    PASS();
}

void test_arena_reset_with_data(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    for (int i = 0; i < 50; i++) {
        arena_mk_int(a, i);
    }
    arena_reset(a);
    /* After reset, should be able to allocate again */
    Obj* x = arena_mk_int(a, 999);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->i, 999);
    arena_destroy(a);
    PASS();
}

void test_arena_reset_null(void) {
    arena_reset(NULL);  /* Should not crash */
    PASS();
}

void test_arena_reset_multiple_times(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 20; i++) {
            arena_mk_int(a, i + round * 100);
        }
        arena_reset(a);
    }
    arena_destroy(a);
    PASS();
}

/* ========== Arena External Pointer Tests ========== */

static int cleanup_called = 0;
static void test_cleanup(void* ptr) {
    (void)ptr;
    cleanup_called++;
}

void test_arena_register_external(void) {
    cleanup_called = 0;
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    int dummy = 42;
    arena_register_external(a, &dummy, test_cleanup);
    arena_destroy(a);
    ASSERT_EQ(cleanup_called, 1);
    PASS();
}

void test_arena_register_external_null_arena(void) {
    cleanup_called = 0;
    int dummy = 42;
    arena_register_external(NULL, &dummy, test_cleanup);
    ASSERT_EQ(cleanup_called, 0);  /* Should not call cleanup */
    PASS();
}

void test_arena_register_external_null_ptr(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    arena_register_external(a, NULL, test_cleanup);  /* Should be ignored */
    arena_destroy(a);
    PASS();
}

void test_arena_multiple_externals(void) {
    cleanup_called = 0;
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    int d1 = 1, d2 = 2, d3 = 3;
    arena_register_external(a, &d1, test_cleanup);
    arena_register_external(a, &d2, test_cleanup);
    arena_register_external(a, &d3, test_cleanup);
    arena_destroy(a);
    ASSERT_EQ(cleanup_called, 3);
    PASS();
}

void test_arena_external_cleanup_on_reset(void) {
    cleanup_called = 0;
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    int dummy = 42;
    arena_register_external(a, &dummy, test_cleanup);
    arena_reset(a);
    ASSERT_EQ(cleanup_called, 1);
    arena_destroy(a);
    PASS();
}

/* ========== Arena List Building Tests ========== */

void test_arena_build_list(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    /* Build list: (1 2 3) */
    Obj* list = NULL;
    for (int i = 3; i >= 1; i--) {
        list = arena_mk_pair(a, arena_mk_int(a, i), list);
    }
    ASSERT_NOT_NULL(list);
    ASSERT_EQ(list->a->i, 1);
    ASSERT_EQ(list->b->a->i, 2);
    ASSERT_EQ(list->b->b->a->i, 3);
    arena_destroy(a);
    PASS();
}

void test_arena_nested_pairs(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    /* Build ((1 . 2) . (3 . 4)) */
    Obj* p1 = arena_mk_pair(a, arena_mk_int(a, 1), arena_mk_int(a, 2));
    Obj* p2 = arena_mk_pair(a, arena_mk_int(a, 3), arena_mk_int(a, 4));
    Obj* outer = arena_mk_pair(a, p1, p2);
    ASSERT_EQ(outer->a->a->i, 1);
    ASSERT_EQ(outer->a->b->i, 2);
    ASSERT_EQ(outer->b->a->i, 3);
    ASSERT_EQ(outer->b->b->i, 4);
    arena_destroy(a);
    PASS();
}

/* ========== Arena Stress Tests ========== */

void test_arena_stress_many_ints(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    for (int i = 0; i < 10000; i++) {
        Obj* x = arena_mk_int(a, i);
        ASSERT_NOT_NULL(x);
        ASSERT_EQ(x->i, i);
    }
    arena_destroy(a);
    PASS();
}

void test_arena_stress_many_pairs(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    for (int i = 0; i < 5000; i++) {
        Obj* p = arena_mk_pair(a, arena_mk_int(a, i), arena_mk_int(a, i+1));
        ASSERT_NOT_NULL(p);
    }
    arena_destroy(a);
    PASS();
}

void test_arena_stress_long_list(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    Obj* list = NULL;
    for (int i = 0; i < 10000; i++) {
        list = arena_mk_pair(a, arena_mk_int(a, i), list);
    }
    /* Walk the list to verify */
    Obj* curr = list;
    int count = 0;
    while (curr) {
        count++;
        curr = curr->b;
    }
    ASSERT_EQ(count, 10000);
    arena_destroy(a);
    PASS();
}

void test_arena_stress_deep_nesting(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    Obj* deep = arena_mk_int(a, 0);
    for (int i = 1; i <= 1000; i++) {
        deep = arena_mk_pair(a, arena_mk_int(a, i), deep);
    }
    ASSERT_NOT_NULL(deep);
    arena_destroy(a);
    PASS();
}

void test_arena_stress_reset_cycle(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    for (int round = 0; round < 100; round++) {
        for (int i = 0; i < 100; i++) {
            arena_mk_int(a, i + round);
        }
        arena_reset(a);
    }
    arena_destroy(a);
    PASS();
}

/* ========== Arena Edge Cases ========== */

void test_arena_allocate_after_destroy_blocks(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    /* Force block creation */
    int count = (ARENA_BLOCK_SIZE / sizeof(Obj)) + 10;
    for (int i = 0; i < count; i++) {
        arena_mk_int(a, i);
    }
    /* Reset and allocate again */
    arena_reset(a);
    Obj* x = arena_mk_int(a, 42);
    ASSERT_NOT_NULL(x);
    ASSERT_EQ(x->i, 42);
    arena_destroy(a);
    PASS();
}

void test_arena_large_allocation(void) {
    Arena* a = arena_create(ARENA_BLOCK_SIZE);
    /* Allocate many objects to trigger multiple block allocations */
    for (int i = 0; i < 50000; i++) {
        Obj* x = arena_mk_int(a, i);
        ASSERT_NOT_NULL(x);
    }
    arena_destroy(a);
    PASS();
}

/* ========== Run All Arena Tests ========== */

void run_arena_tests(void) {
    TEST_SUITE("Arena Allocator");

    TEST_SECTION("Arena Creation");
    RUN_TEST(test_arena_create);
    RUN_TEST(test_arena_destroy_null);
    RUN_TEST(test_arena_destroy_empty);

    TEST_SECTION("Arena Allocation");
    RUN_TEST(test_arena_mk_int);
    RUN_TEST(test_arena_mk_int_negative);
    RUN_TEST(test_arena_mk_int_zero);
    RUN_TEST(test_arena_mk_pair);
    RUN_TEST(test_arena_mk_pair_null_car);
    RUN_TEST(test_arena_mk_pair_null_cdr);
    RUN_TEST(test_arena_mk_pair_both_null);

    TEST_SECTION("Block Management");
    RUN_TEST(test_arena_multiple_allocations);
    RUN_TEST(test_arena_block_creation);

    TEST_SECTION("Arena Reset");
    RUN_TEST(test_arena_reset_empty);
    RUN_TEST(test_arena_reset_with_data);
    RUN_TEST(test_arena_reset_null);
    RUN_TEST(test_arena_reset_multiple_times);

    TEST_SECTION("External Pointers");
    RUN_TEST(test_arena_register_external);
    RUN_TEST(test_arena_register_external_null_arena);
    RUN_TEST(test_arena_register_external_null_ptr);
    RUN_TEST(test_arena_multiple_externals);
    RUN_TEST(test_arena_external_cleanup_on_reset);

    TEST_SECTION("List Building");
    RUN_TEST(test_arena_build_list);
    RUN_TEST(test_arena_nested_pairs);

    TEST_SECTION("Stress Tests");
    RUN_TEST(test_arena_stress_many_ints);
    RUN_TEST(test_arena_stress_many_pairs);
    RUN_TEST(test_arena_stress_long_list);
    RUN_TEST(test_arena_stress_deep_nesting);
    RUN_TEST(test_arena_stress_reset_cycle);

    TEST_SECTION("Edge Cases");
    RUN_TEST(test_arena_allocate_after_destroy_blocks);
    RUN_TEST(test_arena_large_allocation);
}
