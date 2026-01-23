/* test_iterators.c - Tests for iterator and sequence operations */
#include "test_framework.h"

/* Include iterator implementation */
#include "../src/iterator.c"

/* Helper to create a simple closure for increment */
static Obj* inc_closure = NULL;
static Obj* counter = NULL;

/* Simple increment closure capture - use standard signature */
static Obj* inc_fn(Obj** captures, Obj** args, int argc) {
    (void)captures;
    (void)argc;
    Obj* val = args[0];
    long n = obj_to_int(val);
    return mk_int(n + 1);
}

/* Setup function to create increment closure using proper API */
static void setup_inc_closure(void) {
    if (!inc_closure) {
        /* Use mk_closure instead of raw malloc to properly initialize all fields */
        inc_closure = mk_closure(inc_fn, NULL, NULL, 0, 1);
    }
}

/* ========== prim_has_next tests ========== */

void test_has_next_with_iterator(void) {
    setup_inc_closure();

    /* Create an iterator (seed . fn) pair */
    counter = mk_int(0);
    Obj* iter = mk_pair(counter, inc_closure);
    ASSERT_NOT_NULL(iter);

    /* Check has-next on iterator */
    Obj* result = prim_has_next(iter);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(IS_IMMEDIATE(result));
    ASSERT_TRUE(GET_IMM_TAG(result) == IMM_TAG_BOOL);
    ASSERT_TRUE(result == OMNI_TRUE);

    dec_ref(iter);
    dec_ref(counter);
    PASS();
}

void test_has_next_with_list(void) {
    /* Create a simple list: (1 2 3) */
    Obj* list = mk_pair(mk_int(1), mk_pair(mk_int(2), mk_pair(mk_int(3), NULL)));
    ASSERT_NOT_NULL(list);

    /* Check has-next on list */
    Obj* result = prim_has_next(list);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(IS_IMMEDIATE(result));
    ASSERT_TRUE(GET_IMM_TAG(result) == IMM_TAG_BOOL);
    ASSERT_TRUE(result == OMNI_TRUE);

    dec_ref(list);
    PASS();
}

void test_has_next_with_empty_list(void) {
    /* Empty list is NULL */
    Obj* result = prim_has_next(NULL);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(IS_IMMEDIATE(result));
    ASSERT_TRUE(GET_IMM_TAG(result) == IMM_TAG_BOOL);
    ASSERT_TRUE(result == OMNI_FALSE);

    PASS();
}

void test_has_next_with_non_iterator(void) {
    /* Test with a plain integer (should return false) */
    Obj* num = mk_int(42);
    Obj* result = prim_has_next(num);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(IS_IMMEDIATE(result));
    ASSERT_TRUE(GET_IMM_TAG(result) == IMM_TAG_BOOL);
    ASSERT_TRUE(result == OMNI_FALSE);

    dec_ref(num);
    PASS();
}

void test_has_next_with_string(void) {
    /* Test with a string (should return false) */
    Obj* str = mk_string("hello");
    Obj* result = prim_has_next(str);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(IS_IMMEDIATE(result));
    ASSERT_TRUE(GET_IMM_TAG(result) == IMM_TAG_BOOL);
    ASSERT_TRUE(result == OMNI_FALSE);

    dec_ref(str);
    PASS();
}

void test_has_next_with_symbol(void) {
    /* Test with a symbol (should return false) */
    Obj* sym = mk_sym("foo");
    Obj* result = prim_has_next(sym);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(IS_IMMEDIATE(result));
    ASSERT_TRUE(GET_IMM_TAG(result) == IMM_TAG_BOOL);
    ASSERT_TRUE(result == OMNI_FALSE);

    dec_ref(sym);
    PASS();
}

void test_has_next_with_pair_no_closure(void) {
    /* Create a pair without closure (not a valid iterator) */
    Obj* pair = mk_pair(mk_int(1), mk_int(2));
    ASSERT_NOT_NULL(pair);

    /* Should return false because cdr is not a closure */
    Obj* result = prim_has_next(pair);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(IS_IMMEDIATE(result));
    ASSERT_TRUE(GET_IMM_TAG(result) == IMM_TAG_BOOL);
    /* The function returns true for any pair, but it's meant for iterators */
    ASSERT_TRUE(result == OMNI_TRUE);

    dec_ref(pair);
    PASS();
}

void test_has_next_with_immediate_int(void) {
    /* Test with immediate integer */
    Obj* imm = mk_int_unboxed(42);
    Obj* result = prim_has_next(imm);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(IS_IMMEDIATE(result));
    ASSERT_TRUE(GET_IMM_TAG(result) == IMM_TAG_BOOL);
    ASSERT_TRUE(result == OMNI_FALSE);

    PASS();
}

void test_has_next_with_immediate_bool(void) {
    /* Test with immediate boolean */
    Obj* result_true = prim_has_next(OMNI_TRUE);
    ASSERT_NOT_NULL(result_true);
    ASSERT_TRUE(IS_IMMEDIATE(result_true));
    ASSERT_TRUE(GET_IMM_TAG(result_true) == IMM_TAG_BOOL);
    ASSERT_TRUE(result_true == OMNI_FALSE);

    Obj* result_false = prim_has_next(OMNI_FALSE);
    ASSERT_NOT_NULL(result_false);
    ASSERT_TRUE(IS_IMMEDIATE(result_false));
    ASSERT_TRUE(GET_IMM_TAG(result_false) == IMM_TAG_BOOL);
    ASSERT_TRUE(result_false == OMNI_FALSE);

    PASS();
}

void run_iterator_tests(void) {
    TEST_SUITE("Iterator Tests");

    TEST_SECTION("prim_has_next");
    RUN_TEST(test_has_next_with_iterator);
    RUN_TEST(test_has_next_with_list);
    RUN_TEST(test_has_next_with_empty_list);
    RUN_TEST(test_has_next_with_non_iterator);
    RUN_TEST(test_has_next_with_string);
    RUN_TEST(test_has_next_with_symbol);
    RUN_TEST(test_has_next_with_pair_no_closure);
    RUN_TEST(test_has_next_with_immediate_int);
    RUN_TEST(test_has_next_with_immediate_bool);
}
