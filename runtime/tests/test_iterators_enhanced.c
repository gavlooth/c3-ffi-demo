/*
 * test_iterators_enhanced.c - Comprehensive tests for iterator and sequence operations
 *
 * Coverage: Functions in runtime/src/iterator.c that lack tests
 *
 * Test Groups:
 *   - prim_first: Get first element of sequence
 *   - prim_rest: Get rest of sequence
 *   - prim_iterate: Create lazy sequence from function
 *   - prim_iter_next: Get next value from iterator
 *   - prim_take: Take n elements from sequence
 *   - prim_collect: Collect elements into collection
 *   - prim_range: Create range iterator
 *   - Generator functions (make_generator, generator_next, generator_done)
 *   - Unified iterator functions (iter_next_unified, take_unified)
 */

#include "test_framework.h"
#include <string.h>

/* ==================== Helper Functions ==================== */

/* Simple increment closure for testing */
static Obj* inc_fn(Obj** captures, Obj* args[], int argc) {
    (void)captures; (void)argc;
    Obj* val = args[0];
    long n = obj_to_int(val);
    return mk_int(n + 1);
}

/* Double function for testing */
static Obj* double_fn(Obj** captures, Obj* args[], int argc) {
    (void)captures; (void)argc;
    Obj* val = args[0];
    long n = obj_to_int(val);
    return mk_int(n * 2);
}

/* Create increment closure */
static Obj* make_inc_closure(void) {
    Closure* c = malloc(sizeof(Closure));
    if (!c) return NULL;
    c->fn = inc_fn;
    c->captures = NULL;
    c->arity = 1;
    c->region_aware = 0;

    Obj* closure_obj = mk_boxed(TAG_CLOSURE, c);
    return closure_obj;
}

/* Create double closure */
static Obj* make_double_closure(void) {
    Closure* c = malloc(sizeof(Closure));
    if (!c) return NULL;
    c->fn = double_fn;
    c->captures = NULL;
    c->arity = 1;
    c->region_aware = 0;

    Obj* closure_obj = mk_boxed(TAG_CLOSURE, c);
    return closure_obj;
}

/* ==================== prim_first Tests ==================== */

void test_first_with_list(void) {
    /* Get first element of list */
    Obj* list = mk_pair(mk_int(1), mk_pair(mk_int(2), mk_pair(mk_int(3), NULL)));
    ASSERT_NOT_NULL(list);

    Obj* result = prim_first(list);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 1);

    dec_ref(list);
    PASS();
}

void test_first_with_single_element_list(void) {
    /* Get first element of single-element list */
    Obj* list = mk_pair(mk_int(42), NULL);
    ASSERT_NOT_NULL(list);

    Obj* result = prim_first(list);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 42);

    dec_ref(list);
    PASS();
}

void test_first_with_empty_list(void) {
    /* Get first element of empty list */
    Obj* result = prim_first(NULL);
    ASSERT_NULL(result);
    PASS();
}

void test_first_with_pair(void) {
    /* Get first element (car) of pair */
    Obj* pair = mk_pair(mk_int(10), mk_int(20));
    ASSERT_NOT_NULL(pair);

    Obj* result = prim_first(pair);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 10);

    dec_ref(pair);
    PASS();
}

/* ==================== prim_rest Tests ==================== */

void test_rest_with_list(void) {
    /* Get rest of list */
    Obj* list = mk_pair(mk_int(1), mk_pair(mk_int(2), mk_pair(mk_int(3), NULL)));
    ASSERT_NOT_NULL(list);

    Obj* result = prim_rest(list);
    ASSERT_NOT_NULL(result);

    /* Check that result has 2 elements */
    ASSERT_EQ(list_length(result), 2);
    ASSERT_EQ(obj_to_int(first(result)), 2);
    ASSERT_EQ(obj_to_int(second(result)), 3);

    dec_ref(list);
    PASS();
}

void test_rest_with_single_element_list(void) {
    /* Get rest of single-element list */
    Obj* list = mk_pair(mk_int(42), NULL);
    ASSERT_NOT_NULL(list);

    Obj* result = prim_rest(list);
    ASSERT_NULL(result);

    dec_ref(list);
    PASS();
}

void test_rest_with_empty_list(void) {
    /* Get rest of empty list */
    Obj* result = prim_rest(NULL);
    ASSERT_NULL(result);
    PASS();
}

void test_rest_with_pair(void) {
    /* Get rest (cdr) of pair */
    Obj* pair = mk_pair(mk_int(10), mk_int(20));
    ASSERT_NOT_NULL(pair);

    Obj* result = prim_rest(pair);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 20);

    dec_ref(pair);
    PASS();
}

/* ==================== prim_iterate Tests ==================== */

void test_iterate_basic(void) {
    /* Create iterator from function and seed */
    Obj* inc = make_inc_closure();
    Obj* seed = mk_int(0);
    ASSERT_NOT_NULL(inc);
    ASSERT_NOT_NULL(seed);

    Obj* iter = prim_iterate(inc, seed);
    ASSERT_NOT_NULL(iter);
    ASSERT(IS_BOXED(iter));
    ASSERT(iter->tag == TAG_PAIR);

    /* Iterator should be represented as (seed . fn) pair */
    Obj* current = iter->a;
    Obj* fn = iter->b;

    ASSERT_EQ(obj_to_int(current), 0);
    ASSERT(fn == inc);

    dec_ref(iter);
    PASS();
}

void test_iterate_with_null_function(void) {
    /* Iterate with NULL function */
    Obj* seed = mk_int(0);
    Obj* iter = prim_iterate(NULL, seed);
    ASSERT_NULL(iter);

    dec_ref(seed);
    PASS();
}

void test_iterate_with_double_function(void) {
    /* Iterate with double function */
    Obj* dbl = make_double_closure();
    Obj* seed = mk_int(1);
    ASSERT_NOT_NULL(dbl);
    ASSERT_NOT_NULL(seed);

    Obj* iter = prim_iterate(dbl, seed);
    ASSERT_NOT_NULL(iter);

    /* Initial value should be seed */
    Obj* current = iter->a;
    ASSERT_EQ(obj_to_int(current), 1);

    dec_ref(iter);
    PASS();
}

/* ==================== prim_iter_next Tests ==================== */

void test_iter_next_basic(void) {
    /* Get next value from iterator */
    Obj* inc = make_inc_closure();
    Obj* seed = mk_int(0);
    Obj* iter = prim_iterate(inc, seed);
    ASSERT_NOT_NULL(iter);

    /* First call should return 0 and advance to 1 */
    Obj* result1 = prim_iter_next(iter);
    ASSERT_NOT_NULL(result1);
    ASSERT_EQ(obj_to_int(result1), 0);

    /* Iterator should be advanced */
    Obj* current1 = iter->a;
    ASSERT_EQ(obj_to_int(current1), 1);

    /* Second call should return 1 and advance to 2 */
    Obj* result2 = prim_iter_next(iter);
    ASSERT_NOT_NULL(result2);
    ASSERT_EQ(obj_to_int(result2), 1);

    /* Iterator should be advanced again */
    Obj* current2 = iter->a;
    ASSERT_EQ(obj_to_int(current2), 2);

    dec_ref(iter);
    PASS();
}

void test_iter_next_multiple_calls(void) {
    /* Multiple iter_next calls should advance correctly */
    Obj* inc = make_inc_closure();
    Obj* seed = mk_int(0);
    Obj* iter = prim_iterate(inc, seed);
    ASSERT_NOT_NULL(iter);

    /* Call iter_next 5 times */
    for (int i = 0; i < 5; i++) {
        Obj* result = prim_iter_next(iter);
        ASSERT_NOT_NULL(result);
        ASSERT_EQ(obj_to_int(result), i);
    }

    /* After 5 calls, iterator should be at 5 */
    Obj* current = iter->a;
    ASSERT_EQ(obj_to_int(current), 5);

    dec_ref(iter);
    PASS();
}

void test_iter_next_with_null_iterator(void) {
    /* iter_next with NULL iterator */
    Obj* result = prim_iter_next(NULL);
    ASSERT_NULL(result);
    PASS();
}

void test_iter_next_with_invalid_pair(void) {
    /* iter_next with non-iterator pair */
    Obj* pair = mk_pair(mk_int(1), mk_int(2));
    ASSERT_NOT_NULL(pair);

    Obj* result = prim_iter_next(pair);
    /* Should not crash, may return NULL or original value */
    ASSERT(result == NULL || obj_to_int(result) == 1);

    dec_ref(pair);
    PASS();
}

void test_iter_next_with_double_iterator(void) {
    /* iter_next with double function iterator */
    Obj* dbl = make_double_closure();
    Obj* seed = mk_int(1);
    Obj* iter = prim_iterate(dbl, seed);
    ASSERT_NOT_NULL(iter);

    /* First call should return 1 */
    Obj* result1 = prim_iter_next(iter);
    ASSERT_EQ(obj_to_int(result1), 1);

    /* Iterator should be doubled to 2 */
    Obj* current1 = iter->a;
    ASSERT_EQ(obj_to_int(current1), 2);

    /* Second call should return 2 */
    Obj* result2 = prim_iter_next(iter);
    ASSERT_EQ(obj_to_int(result2), 2);

    /* Iterator should be doubled to 4 */
    Obj* current2 = iter->a;
    ASSERT_EQ(obj_to_int(current2), 4);

    dec_ref(iter);
    PASS();
}

/* ==================== prim_take Tests ==================== */

void test_take_from_iterator(void) {
    /* Take n elements from iterator */
    Obj* inc = make_inc_closure();
    Obj* seed = mk_int(0);
    Obj* iter = prim_iterate(inc, seed);
    ASSERT_NOT_NULL(iter);

    /* Take 3 elements */
    Obj* result = prim_take(3, iter);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(list_length(result), 3);

    /* Check values */
    Obj* elem1 = first(result);
    Obj* elem2 = second(result);
    Obj* elem3 = third(result);

    ASSERT_EQ(obj_to_int(elem1), 0);
    ASSERT_EQ(obj_to_int(elem2), 1);
    ASSERT_EQ(obj_to_int(elem3), 2);

    /* Iterator should be advanced to 3 */
    Obj* current = iter->a;
    ASSERT_EQ(obj_to_int(current), 3);

    dec_ref(iter);
    PASS();
}

void test_take_from_list(void) {
    /* Take n elements from list */
    Obj* list = mk_pair(mk_int(1), mk_pair(mk_int(2), mk_pair(mk_int(3), mk_pair(mk_int(4), mk_pair(mk_int(5), NULL)))));
    ASSERT_NOT_NULL(list);

    /* Take 3 elements */
    Obj* result = prim_take(3, list);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(list_length(result), 3);

    /* Check values */
    ASSERT_EQ(obj_to_int(first(result)), 1);
    ASSERT_EQ(obj_to_int(second(result)), 2);
    ASSERT_EQ(obj_to_int(third(result)), 3);

    dec_ref(list);
    PASS();
}

void test_take_zero_elements(void) {
    /* Take 0 elements */
    Obj* list = mk_pair(mk_int(1), mk_pair(mk_int(2), NULL));
    Obj* result = prim_take(0, list);
    ASSERT_NULL(result);
    PASS();
}

void test_take_negative_elements(void) {
    /* Take negative elements */
    Obj* list = mk_pair(mk_int(1), mk_pair(mk_int(2), NULL));
    Obj* result = prim_take(-1, list);
    ASSERT_NULL(result);
    PASS();
}

void test_take_from_empty_list(void) {
    /* Take from empty list */
    Obj* result = prim_take(3, NULL);
    ASSERT_NULL(result);
    PASS();
}

void test_take_more_than_list(void) {
    /* Take more elements than list has */
    Obj* list = mk_pair(mk_int(1), mk_pair(mk_int(2), NULL));
    Obj* result = prim_take(10, list);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(list_length(result), 2);

    dec_ref(list);
    PASS();
}

/* ==================== prim_collect Tests ==================== */

void test_collect_to_array_default(void) {
    /* Collect to array (default) */
    Obj* list = mk_pair(mk_int(1), mk_pair(mk_int(2), mk_pair(mk_int(3), NULL)));

    /* Collect with no kind specified (should default to array) */
    Obj* result = prim_collect(list, NULL);
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_ARRAY);

    /* Check array contents */
    ASSERT_EQ(array_length(result), 3);

    dec_ref(list);
    PASS();
}

void test_collect_to_array_explicit(void) {
    /* Collect to array (explicit) */
    Obj* list = mk_pair(mk_int(10), mk_pair(mk_int(20), mk_pair(mk_int(30), NULL)));
    Obj* kind = mk_sym("array");

    Obj* result = prim_collect(list, kind);
    ASSERT_NOT_NULL(result);
    ASSERT(result->tag == TAG_ARRAY);
    ASSERT_EQ(array_length(result), 3);

    /* Check values */
    ASSERT_EQ(obj_to_int(array_get(result, 0)), 10);
    ASSERT_EQ(obj_to_int(array_get(result, 1)), 20);
    ASSERT_EQ(obj_to_int(array_get(result, 2)), 30);

    dec_ref(list);
    PASS();
}

void test_collect_to_list(void) {
    /* Collect to list */
    Obj* list = mk_pair(mk_int(1), mk_pair(mk_int(2), mk_pair(mk_int(3), NULL)));
    Obj* kind = mk_sym("list");

    Obj* result = prim_collect(list, kind);
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_PAIR);

    /* Should be a list with 3 elements */
    ASSERT_EQ(list_length(result), 3);

    dec_ref(list);
    PASS();
}

void test_collect_from_iterator(void) {
    /* Collect from iterator */
    Obj* inc = make_inc_closure();
    Obj* seed = mk_int(0);
    Obj* iter = prim_iterate(inc, seed);
    Obj* kind = mk_sym("array");

    /* Collect from iterator (limited by max_iters = 1000) */
    Obj* result = prim_collect(iter, kind);
    ASSERT_NOT_NULL(result);
    ASSERT(result->tag == TAG_ARRAY);

    /* Should have 1000 elements (max_iters limit) */
    ASSERT_EQ(array_length(result), 1000);

    /* Check first few values */
    ASSERT_EQ(obj_to_int(array_get(result, 0)), 0);
    ASSERT_EQ(obj_to_int(array_get(result, 1)), 1);
    ASSERT_EQ(obj_to_int(array_get(result, 2)), 2);

    dec_ref(iter);
    PASS();
}

void test_collect_empty_list(void) {
    /* Collect empty list */
    Obj* result = prim_collect(NULL, NULL);
    ASSERT_NOT_NULL(result);
    ASSERT(result->tag == TAG_ARRAY);
    ASSERT_EQ(array_length(result), 0);
    PASS();
}

/* ==================== prim_range Tests ==================== */

void test_range_positive(void) {
    /* Create range 0 to n-1 */
    Obj* result = prim_range(5);
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_PAIR);

    /* Should have 5 elements */
    ASSERT_EQ(list_length(result), 5);

    /* Check values */
    ASSERT_EQ(obj_to_int(first(result)), 0);
    ASSERT_EQ(obj_to_int(second(result)), 1);
    ASSERT_EQ(obj_to_int(third(result)), 2);

    /* Check last value */
    Obj* current = result;
    while (current && current->b) {
        current = current->b;
    }
    ASSERT_EQ(obj_to_int(current->a), 4);

    PASS();
}

void test_range_zero(void) {
    /* Range with zero */
    Obj* result = prim_range(0);
    ASSERT_NULL(result);
    PASS();
}

void test_range_negative(void) {
    /* Range with negative */
    Obj* result = prim_range(-5);
    ASSERT_NULL(result);
    PASS();
}

void test_range_one(void) {
    /* Range with one */
    Obj* result = prim_range(1);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(list_length(result), 1);
    ASSERT_EQ(obj_to_int(first(result)), 0);
    PASS();
}

void test_range_large(void) {
    /* Create large range */
    Obj* result = prim_range(100);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(list_length(result), 100);

    /* Check first and last */
    ASSERT_EQ(obj_to_int(first(result)), 0);

    Obj* current = result;
    while (current && current->b) {
        current = current->b;
    }
    ASSERT_EQ(obj_to_int(current->a), 99);

    PASS();
}

/* ==================== Test Suite Runner ==================== */

void run_iterator_enhanced_tests(void) {
    TEST_SUITE("Enhanced Iterator Tests");

    /* prim_first tests */
    TEST_SECTION("prim_first");
    RUN_TEST(test_first_with_list);
    RUN_TEST(test_first_with_single_element_list);
    RUN_TEST(test_first_with_empty_list);
    RUN_TEST(test_first_with_pair);

    /* prim_rest tests */
    TEST_SECTION("prim_rest");
    RUN_TEST(test_rest_with_list);
    RUN_TEST(test_rest_with_single_element_list);
    RUN_TEST(test_rest_with_empty_list);
    RUN_TEST(test_rest_with_pair);

    /* prim_iterate tests */
    TEST_SECTION("prim_iterate");
    RUN_TEST(test_iterate_basic);
    RUN_TEST(test_iterate_with_null_function);
    RUN_TEST(test_iterate_with_double_function);

    /* prim_iter_next tests */
    TEST_SECTION("prim_iter_next");
    RUN_TEST(test_iter_next_basic);
    RUN_TEST(test_iter_next_multiple_calls);
    RUN_TEST(test_iter_next_with_null_iterator);
    RUN_TEST(test_iter_next_with_invalid_pair);
    RUN_TEST(test_iter_next_with_double_iterator);

    /* prim_take tests */
    TEST_SECTION("prim_take");
    RUN_TEST(test_take_from_iterator);
    RUN_TEST(test_take_from_list);
    RUN_TEST(test_take_zero_elements);
    RUN_TEST(test_take_negative_elements);
    RUN_TEST(test_take_from_empty_list);
    RUN_TEST(test_take_more_than_list);

    /* prim_collect tests */
    TEST_SECTION("prim_collect");
    RUN_TEST(test_collect_to_array_default);
    RUN_TEST(test_collect_to_array_explicit);
    RUN_TEST(test_collect_to_list);
    RUN_TEST(test_collect_from_iterator);
    RUN_TEST(test_collect_empty_list);

    /* prim_range tests */
    TEST_SECTION("prim_range");
    RUN_TEST(test_range_positive);
    RUN_TEST(test_range_zero);
    RUN_TEST(test_range_negative);
    RUN_TEST(test_range_one);
    RUN_TEST(test_range_large);
}
