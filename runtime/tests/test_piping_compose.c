/*
 * test_piping_compose.c - Tests for pipe operator and function composition
 *
 * Coverage: Functions in runtime/src/piping.c
 *
 * Test Groups:
 *   - Pipe operator (|>) - already tested in test_pipe_operator.lisp
 *   - Function composition (compose)
 *   - Dot field access (.field)
 *   - Method chaining
 *   - Flip operator
 *   - Partial application
 */

#include "test_framework.h"
#include <string.h>

/* ==================== Function Composition Tests ==================== */

void test_compose_basic(void) {
    /* Compose two functions */
    Obj* f = mk_pair(mk_int(1), mk_int(2));  /* Dummy function */
    Obj* g = mk_pair(mk_int(3), mk_int(4));  /* Dummy function */
    
    Obj* composed = prim_compose(f, g);
    
    ASSERT_NOT_NULL(composed);
    ASSERT(IS_BOXED(composed));
    ASSERT(composed->tag == TAG_PAIR);
    
    /* For now, compose returns a pair representation */
    Obj* result_a = composed->a;
    Obj* result_b = composed->b;
    
    ASSERT_NOT_NULL(result_a);
    ASSERT_NOT_NULL(result_b);
    
    dec_ref(f);
    dec_ref(g);
    dec_ref(composed);
    PASS();
}

void test_compose_many_single(void) {
    /* Compose many functions with single function */
    Obj* f = mk_pair(mk_int(1), NULL);
    Obj* functions = mk_pair(f, NULL);
    
    Obj* composed = prim_compose_many(functions);
    
    ASSERT_NOT_NULL(composed);
    ASSERT(IS_BOXED(composed));
    
    dec_ref(f);
    dec_ref(functions);
    dec_ref(composed);
    PASS();
}

void test_compose_many_multiple(void) {
    /* Compose chain of functions */
    Obj* f1 = mk_pair(mk_int(1), NULL);
    Obj* f2 = mk_pair(mk_int(2), NULL);
    Obj* f3 = mk_pair(mk_int(3), NULL);
    
    Obj* functions = mk_pair(f1, mk_pair(f2, mk_pair(f3, NULL)));
    Obj* composed = prim_compose_many(functions);
    
    ASSERT_NOT_NULL(composed);
    ASSERT(IS_BOXED(composed));
    
    dec_ref(f1);
    dec_ref(f2);
    dec_ref(f3);
    dec_ref(functions);
    dec_ref(composed);
    PASS();
}

void test_compose_many_empty(void) {
    /* Compose empty list */
    Obj* composed = prim_compose_many(NULL);
    
    /* Should return identity function (pair of NULLs) */
    ASSERT_NOT_NULL(composed);
    ASSERT(IS_BOXED(composed));
    ASSERT(composed->tag == TAG_PAIR);
    
    Obj* a = composed->a;
    Obj* b = composed->b;
    
    ASSERT_NULL(a);
    ASSERT_NULL(b);
    
    PASS();
}

/* ==================== Dot Field Access Tests ==================== */

void test_dot_field_dict_basic(void) {
    /* Access dict field */
    Obj* dict = mk_dict();
    Obj* key = mk_string("key1");
    Obj* value = mk_int(42);
    dict_set(dict, key, value);
    
    Obj* field = mk_string("key1");
    Obj* result = prim_dot_field(field, dict);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result));
    ASSERT_EQ(obj_to_int(result), 42);
    
    dec_ref(dict);
    dec_ref(key);
    dec_ref(value);
    dec_ref(field);
    dec_ref(result);
    PASS();
}

void test_dot_field_pair_car(void) {
    /* Access pair car field */
    Obj* pair = mk_pair(mk_int(100), mk_int(200));
    Obj* field = mk_string("car");
    
    Obj* result = prim_dot_field(field, pair);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result));
    ASSERT_EQ(obj_to_int(result), 100);
    
    dec_ref(pair);
    dec_ref(field);
    dec_ref(result);
    PASS();
}

void test_dot_field_pair_cdr(void) {
    /* Access pair cdr field */
    Obj* pair = mk_pair(mk_int(100), mk_int(200));
    Obj* field = mk_string("cdr");
    
    Obj* result = prim_dot_field(field, pair);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result));
    ASSERT_EQ(obj_to_int(result), 200);
    
    dec_ref(pair);
    dec_ref(field);
    dec_ref(result);
    PASS();
}

void test_dot_field_pair_a(void) {
    /* Access pair 'a' field (alias for car) */
    Obj* pair = mk_pair(mk_int(50), mk_int(75));
    Obj* field = mk_string("a");
    
    Obj* result = prim_dot_field(field, pair);
    
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 50);
    
    dec_ref(pair);
    dec_ref(field);
    dec_ref(result);
    PASS();
}

void test_dot_field_pair_b(void) {
    /* Access pair 'b' field (alias for cdr) */
    Obj* pair = mk_pair(mk_int(50), mk_int(75));
    Obj* field = mk_string("b");
    
    Obj* result = prim_dot_field(field, pair);
    
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 75);
    
    dec_ref(pair);
    dec_ref(field);
    dec_ref(result);
    PASS();
}

void test_dot_field_array_length(void) {
    /* Access array length */
    Obj* arr = mk_array(10);
    array_push(arr, mk_int(1));
    array_push(arr, mk_int(2));
    array_push(arr, mk_int(3));
    
    Obj* field = mk_string("length");
    Obj* result = prim_dot_field(field, arr);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result));
    ASSERT_EQ(obj_to_int(result), 3);
    
    dec_ref(arr);
    dec_ref(field);
    dec_ref(result);
    PASS();
}

void test_dot_field_string_length(void) {
    /* Access string length */
    Obj* str = mk_string("hello");
    
    Obj* field = mk_string("length");
    Obj* result = prim_dot_field(field, str);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result));
    ASSERT_EQ(obj_to_int(result), 5);
    
    dec_ref(str);
    dec_ref(field);
    dec_ref(result);
    PASS();
}

void test_dot_field_invalid_field(void) {
    /* Access non-existent field */
    Obj* pair = mk_pair(mk_int(1), mk_int(2));
    Obj* field = mk_string("invalid");
    
    Obj* result = prim_dot_field(field, pair);
    
    /* Should return NULL for invalid field */
    ASSERT_NULL(result);
    
    dec_ref(pair);
    dec_ref(field);
    PASS();
}

/* ==================== Dot Field Chain Tests ==================== */

void test_dot_field_chain_dict(void) {
    /* Chain dict accesses (though simple dict only has one level) */
    Obj* dict = mk_dict();
    Obj* key1 = mk_string("key1");
    Obj* value1 = mk_int(42);
    dict_set(dict, key1, value1);
    
    /* Create field chain */
    Obj* field1 = mk_string("key1");
    Obj* field2 = mk_string("nonexistent");
    Obj* fields = mk_pair(field1, mk_pair(field2, NULL));
    
    Obj* result = prim_dot_field_chain(dict, fields);
    
    /* First access succeeds, second returns NULL */
    /* The chain stops when a field returns NULL */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 42);
    
    dec_ref(dict);
    dec_ref(key1);
    dec_ref(value1);
    dec_ref(field1);
    dec_ref(field2);
    dec_ref(fields);
    dec_ref(result);
    PASS();
}

/* ==================== Flip Operator Tests ==================== */

void test_flip_basic(void) {
    /* Flip a function */
    Obj* func = mk_pair(mk_int(1), mk_int(2));
    
    Obj* flipped = prim_flip(func);
    
    ASSERT_NOT_NULL(flipped);
    ASSERT(IS_BOXED(flipped));
    ASSERT(flipped->tag == TAG_PAIR);
    
    /* Flip returns a pair (func . "flipped") */
    Obj* a = flipped->a;
    Obj* b = flipped->b;
    
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT(b->tag == TAG_SYM);
    
    dec_ref(func);
    dec_ref(flipped);
    PASS();
}

void test_flip_null(void) {
    /* Flip NULL */
    Obj* flipped = prim_flip(NULL);
    ASSERT_NULL(flipped);
    PASS();
}

/* ==================== Apply Function Tests ==================== */

void test_apply_closure_single_arg(void) {
    /* Apply closure with single argument */
    /* Create a simple closure for testing */
    omni_ensure_global_region();
    Region* r = omni_get_global_region();
    
    /* For now, just test that apply doesn't crash with NULL closure */
    Obj* args = mk_pair(mk_int(5), NULL);
    Obj* result = prim_apply(NULL, args);
    
    /* NULL closure should return NULL */
    ASSERT_NULL(result);
    
    dec_ref(args);
    PASS();
}

void test_apply_closure_multiple_args(void) {
    /* Apply closure with multiple arguments */
    Obj* args = mk_pair(mk_int(1), mk_pair(mk_int(2), mk_pair(mk_int(3), NULL)));
    
    /* NULL closure should return NULL */
    Obj* result = prim_apply(NULL, args);
    ASSERT_NULL(result);
    
    dec_ref(args);
    PASS();
}

void test_apply_closure_no_args(void) {
    /* Apply closure with no arguments */
    Obj* result = prim_apply(NULL, NULL);
    ASSERT_NULL(result);
    PASS();
}

/* ==================== Partial Application Tests ==================== */

void test_partial_basic(void) {
    /* Partially apply function */
    Obj* func = mk_pair(mk_int(1), mk_int(2));
    Obj* fixed_args = mk_pair(mk_int(10), mk_pair(mk_int(20), NULL));
    
    Obj* partial = prim_partial(func, fixed_args);
    
    ASSERT_NOT_NULL(partial);
    ASSERT(IS_BOXED(partial));
    ASSERT(partial->tag == TAG_PAIR);
    
    /* Partial returns (func . fixed_args) */
    Obj* a = partial->a;
    Obj* b = partial->b;
    
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    
    dec_ref(func);
    dec_ref(fixed_args);
    dec_ref(partial);
    PASS();
}

void test_partial_null_func(void) {
    /* Partial with NULL function */
    Obj* fixed_args = mk_pair(mk_int(10), NULL);
    Obj* partial = prim_partial(NULL, fixed_args);
    
    ASSERT_NULL(partial);
    
    dec_ref(fixed_args);
    PASS();
}

void test_partial_empty_args(void) {
    /* Partial with empty args */
    Obj* func = mk_pair(mk_int(1), NULL);
    Obj* partial = prim_partial(func, NULL);
    
    ASSERT_NOT_NULL(partial);
    
    dec_ref(func);
    dec_ref(partial);
    PASS();
}

/* ==================== Pipe Many Tests ==================== */

void test_pipe_many_empty(void) {
    /* Pipe with empty function list */
    Obj* value = mk_int(42);
    Obj* functions = NULL;
    
    Obj* result = prim_pipe_many(value, functions);
    
    /* Should return original value */
    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result));
    ASSERT_EQ(obj_to_int(result), 42);
    
    dec_ref(value);
    PASS();
}

void test_pipe_many_single(void) {
    /* Pipe through single function */
    Obj* value = mk_int(5);
    
    /* Create dummy function (just returns value) */
    Obj* func = mk_pair(mk_sym("identity"), mk_int(0));
    Obj* functions = mk_pair(func, NULL);
    
    Obj* result = prim_pipe_many(value, functions);
    
    /* Should return something (even if func is dummy) */
    /* This test mainly checks for no crashes */
    ASSERT_NOT_NULL(result);
    
    dec_ref(value);
    dec_ref(func);
    dec_ref(functions);
    dec_ref(result);
    PASS();
}

void test_pipe_many_multiple(void) {
    /* Pipe through multiple functions */
    Obj* value = mk_int(10);
    
    Obj* func1 = mk_pair(mk_sym("f1"), mk_int(0));
    Obj* func2 = mk_pair(mk_sym("f2"), mk_int(0));
    Obj* func3 = mk_pair(mk_sym("f3"), mk_int(0));
    
    Obj* functions = mk_pair(func1, mk_pair(func2, mk_pair(func3, NULL)));
    Obj* result = prim_pipe_many(value, functions);
    
    /* Should return something (even if funcs are dummy) */
    ASSERT_NOT_NULL(result);
    
    dec_ref(value);
    dec_ref(func1);
    dec_ref(func2);
    dec_ref(func3);
    dec_ref(functions);
    dec_ref(result);
    PASS();
}

/* ==================== Test Suite Runner ==================== */

void run_piping_compose_tests(void) {
    TEST_SUITE("Piping and Compose Tests");
    
    /* Function composition tests */
    TEST_SECTION("Function Composition");
    RUN_TEST(test_compose_basic);
    RUN_TEST(test_compose_many_single);
    RUN_TEST(test_compose_many_multiple);
    RUN_TEST(test_compose_many_empty);
    
    /* Dot field access tests */
    TEST_SECTION("Dot Field Access");
    RUN_TEST(test_dot_field_dict_basic);
    RUN_TEST(test_dot_field_pair_car);
    RUN_TEST(test_dot_field_pair_cdr);
    RUN_TEST(test_dot_field_pair_a);
    RUN_TEST(test_dot_field_pair_b);
    RUN_TEST(test_dot_field_array_length);
    RUN_TEST(test_dot_field_string_length);
    RUN_TEST(test_dot_field_invalid_field);
    
    /* Dot field chain tests */
    TEST_SECTION("Dot Field Chain");
    RUN_TEST(test_dot_field_chain_dict);
    
    /* Flip operator tests */
    TEST_SECTION("Flip Operator");
    RUN_TEST(test_flip_basic);
    RUN_TEST(test_flip_null);
    
    /* Apply function tests */
    TEST_SECTION("Apply Function");
    RUN_TEST(test_apply_closure_single_arg);
    RUN_TEST(test_apply_closure_multiple_args);
    RUN_TEST(test_apply_closure_no_args);
    
    /* Partial application tests */
    TEST_SECTION("Partial Application");
    RUN_TEST(test_partial_basic);
    RUN_TEST(test_partial_null_func);
    RUN_TEST(test_partial_empty_args);
    
    /* Pipe many tests */
    TEST_SECTION("Pipe Many");
    RUN_TEST(test_pipe_many_empty);
    RUN_TEST(test_pipe_many_single);
    RUN_TEST(test_pipe_many_multiple);
}
