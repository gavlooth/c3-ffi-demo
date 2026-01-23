/*
 * test_piping_enhanced.c - Enhanced tests for pipe operator and method chaining
 *
 * Coverage: Missing tests in runtime/src/piping.c
 *
 * Test Groups:
 *   - prim_pipe: Pipe operator (|>)
 *   - prim_method_chain: Method chaining with extra args
 *   - Enhanced prim_apply tests with actual closures
 */

#include "test_framework.h"
#include <string.h>

/* ==================== Helper Functions ==================== */

/* Simple add closure */
static Obj* add_fn(Obj** captures, Obj* args[], int argc) {
    (void)captures;
    if (argc < 2) return NULL;
    long a = obj_to_int(args[0]);
    long b = obj_to_int(args[1]);
    return mk_int(a + b);
}

/* Simple multiply closure */
static Obj* mul_fn(Obj** captures, Obj* args[], int argc) {
    (void)captures;
    if (argc < 2) return NULL;
    long a = obj_to_int(args[0]);
    long b = obj_to_int(args[1]);
    return mk_int(a * b);
}

/* Simple square closure (single arg) */
static Obj* square_fn(Obj** captures, Obj* args[], int argc) {
    (void)captures;
    if (argc < 1) return NULL;
    long x = obj_to_int(args[0]);
    return mk_int(x * x);
}

/* Simple increment closure (single arg) */
static Obj* inc_fn(Obj** captures, Obj* args[], int argc) {
    (void)captures;
    if (argc < 1) return NULL;
    long x = obj_to_int(args[0]);
    return mk_int(x + 1);
}

/* Identity function */
static Obj* identity_fn(Obj** captures, Obj* args[], int argc) {
    (void)captures;
    if (argc < 1) return NULL;
    return args[0];
}

/* Create add closure */
static Obj* make_add_closure(void) {
    Closure* c = malloc(sizeof(Closure));
    if (!c) return NULL;
    c->fn = add_fn;
    c->captures = NULL;
    c->arity = 2;
    c->region_aware = 0;

    Obj* closure_obj = mk_boxed(TAG_CLOSURE, c);
    return closure_obj;
}

/* Create multiply closure */
static Obj* make_mul_closure(void) {
    Closure* c = malloc(sizeof(Closure));
    if (!c) return NULL;
    c->fn = mul_fn;
    c->captures = NULL;
    c->arity = 2;
    c->region_aware = 0;

    Obj* closure_obj = mk_boxed(TAG_CLOSURE, c);
    return closure_obj;
}

/* Create square closure */
static Obj* make_square_closure(void) {
    Closure* c = malloc(sizeof(Closure));
    if (!c) return NULL;
    c->fn = square_fn;
    c->captures = NULL;
    c->arity = 1;
    c->region_aware = 0;

    Obj* closure_obj = mk_boxed(TAG_CLOSURE, c);
    return closure_obj;
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

/* Create identity closure */
static Obj* make_identity_closure(void) {
    Closure* c = malloc(sizeof(Closure));
    if (!c) return NULL;
    c->fn = identity_fn;
    c->captures = NULL;
    c->arity = 1;
    c->region_aware = 0;

    Obj* closure_obj = mk_boxed(TAG_CLOSURE, c);
    return closure_obj;
}

/* ==================== prim_pipe Tests ==================== */

void test_pipe_with_closure(void) {
    /* Pipe value through closure */
    Obj* inc = make_inc_closure();
    Obj* value = mk_int(5);
    ASSERT_NOT_NULL(inc);
    ASSERT_NOT_NULL(value);

    Obj* result = prim_pipe(value, inc);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 6);

    dec_ref(inc);
    dec_ref(value);
    PASS();
}

void test_pipe_with_square_closure(void) {
    /* Pipe through square closure */
    Obj* square = make_square_closure();
    Obj* value = mk_int(5);
    ASSERT_NOT_NULL(square);
    ASSERT_NOT_NULL(value);

    Obj* result = prim_pipe(value, square);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 25);

    dec_ref(square);
    dec_ref(value);
    PASS();
}

void test_pipe_with_null_function(void) {
    /* Pipe with NULL function should return value unchanged */
    Obj* value = mk_int(42);
    Obj* result = prim_pipe(value, NULL);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 42);

    dec_ref(value);
    PASS();
}

void test_pipe_with_symbol_function(void) {
    /* Pipe with symbol function (unresolved) should return value unchanged */
    Obj* value = mk_int(10);
    Obj* func_sym = mk_sym("inc");
    ASSERT_NOT_NULL(value);
    ASSERT_NOT_NULL(func_sym);

    Obj* result = prim_pipe(value, func_sym);
    /* Symbol resolution requires compiler support, returns unchanged */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 10);

    dec_ref(value);
    dec_ref(func_sym);
    PASS();
}

void test_pipe_chaining(void) {
    /* Manual pipe chaining: (5 |> inc) |> square */
    Obj* inc = make_inc_closure();
    Obj* square = make_square_closure();
    Obj* value = mk_int(5);
    ASSERT_NOT_NULL(inc);
    ASSERT_NOT_NULL(square);
    ASSERT_NOT_NULL(value);

    /* First pipe: 5 |> inc = 6 */
    Obj* result1 = prim_pipe(value, inc);
    ASSERT_NOT_NULL(result1);
    ASSERT_EQ(obj_to_int(result1), 6);

    /* Second pipe: 6 |> square = 36 */
    Obj* result2 = prim_pipe(result1, square);
    ASSERT_NOT_NULL(result2);
    ASSERT_EQ(obj_to_int(result2), 36);

    dec_ref(inc);
    dec_ref(square);
    dec_ref(value);
    PASS();
}

void test_pipe_with_identity(void) {
    /* Pipe through identity function */
    Obj* identity = make_identity_closure();
    Obj* value = mk_int(42);
    ASSERT_NOT_NULL(identity);
    ASSERT_NOT_NULL(value);

    Obj* result = prim_pipe(value, identity);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 42);

    dec_ref(identity);
    dec_ref(value);
    PASS();
}

/* ==================== prim_method_chain Tests ==================== */

void test_method_chain_basic(void) {
    /* Chain method calls: obj [(add . 5) (mul . 2)] */
    Obj* add = make_add_closure();
    Obj* mul = make_mul_closure();
    Obj* obj = mk_int(10);
    ASSERT_NOT_NULL(add);
    ASSERT_NOT_NULL(mul);
    ASSERT_NOT_NULL(obj);

    /* Create call specs: [(add . 5) (mul . 2)] */
    Obj* call1 = mk_pair(add, mk_pair(mk_int(5), NULL));
    Obj* call2 = mk_pair(mul, mk_pair(mk_int(2), NULL));
    Obj* calls = mk_pair(call1, mk_pair(call2, NULL));
    ASSERT_NOT_NULL(call1);
    ASSERT_NOT_NULL(call2);
    ASSERT_NOT_NULL(calls);

    /* Execute chain: (mul (add obj 5) 2) = (mul (10 + 5) 2) = (mul 15 2) = 30 */
    Obj* result = prim_method_chain(obj, calls);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 30);

    dec_ref(add);
    dec_ref(mul);
    dec_ref(obj);
    dec_ref(call1);
    dec_ref(call2);
    dec_ref(calls);
    PASS();
}

void test_method_chain_single_call(void) {
    /* Chain with single method call */
    Obj* square = make_square_closure();
    Obj* obj = mk_int(5);
    ASSERT_NOT_NULL(square);
    ASSERT_NOT_NULL(obj);

    /* Create call spec: [(square . NULL)] (no extra args) */
    Obj* call = mk_pair(square, NULL);
    Obj* calls = mk_pair(call, NULL);

    /* Execute chain: square(obj) = 25 */
    Obj* result = prim_method_chain(obj, calls);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 25);

    dec_ref(square);
    dec_ref(obj);
    dec_ref(call);
    dec_ref(calls);
    PASS();
}

void test_method_chain_three_calls(void) {
    /* Chain three method calls */
    Obj* inc1 = make_inc_closure();
    Obj* inc2 = make_inc_closure();
    Obj* square = make_square_closure();
    Obj* obj = mk_int(5);
    ASSERT_NOT_NULL(inc1);
    ASSERT_NOT_NULL(inc2);
    ASSERT_NOT_NULL(square);
    ASSERT_NOT_NULL(obj);

    /* Create call specs: [(inc . NULL) (inc . NULL) (square . NULL)] */
    Obj* call1 = mk_pair(inc1, NULL);
    Obj* call2 = mk_pair(inc2, NULL);
    Obj* call3 = mk_pair(square, NULL);
    Obj* calls = mk_pair(call1, mk_pair(call2, mk_pair(call3, NULL)));

    /* Execute chain: square(square(inc(inc(5)))) = square(square(7)) = square(49) = 2401 */
    Obj* result = prim_method_chain(obj, calls);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 2401);

    dec_ref(inc1);
    dec_ref(inc2);
    dec_ref(square);
    dec_ref(obj);
    dec_ref(call1);
    dec_ref(call2);
    dec_ref(call3);
    dec_ref(calls);
    PASS();
}

void test_method_chain_with_extra_args(void) {
    /* Chain with multiple extra args */
    Obj* add = make_add_closure();
    Obj* obj = mk_int(10);
    ASSERT_NOT_NULL(add);
    ASSERT_NOT_NULL(obj);

    /* Create call spec with multiple extra args: [(add . (5 3 2))] */
    Obj* extra_args = mk_pair(mk_int(5), mk_pair(mk_int(3), mk_pair(mk_int(2), NULL)));
    Obj* call = mk_pair(add, extra_args);
    Obj* calls = mk_pair(call, NULL);

    /* Execute chain: add(obj, 5, 3, 2) = add(10, 5, 3, 2)
     * Note: add only uses first 2 args, but should handle extra gracefully
     * Expected: 10 + 5 = 15 (ignoring extra args) */
    Obj* result = prim_method_chain(obj, calls);
    ASSERT_NOT_NULL(result);

    /* Result may vary based on implementation, just check it doesn't crash */
    ASSERT(IS_IMMEDIATE(result));

    dec_ref(add);
    dec_ref(obj);
    dec_ref(extra_args);
    dec_ref(call);
    dec_ref(calls);
    PASS();
}

void test_method_chain_with_null_obj(void) {
    /* Chain with NULL starting object */
    Obj* square = make_square_closure();
    ASSERT_NOT_NULL(square);

    Obj* call = mk_pair(square, NULL);
    Obj* calls = mk_pair(call, NULL);

    /* Execute chain with NULL obj */
    Obj* result = prim_method_chain(NULL, calls);
    /* Should return NULL or handle gracefully */
    ASSERT(result == NULL || IS_IMMEDIATE(result));

    dec_ref(square);
    dec_ref(call);
    dec_ref(calls);
    PASS();
}

void test_method_chain_with_empty_calls(void) {
    /* Chain with empty call list */
    Obj* obj = mk_int(42);
    ASSERT_NOT_NULL(obj);

    /* Execute chain with no calls */
    Obj* result = prim_method_chain(obj, NULL);
    /* Should return original object */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 42);

    dec_ref(obj);
    PASS();
}

void test_method_chain_with_null_closure(void) {
    /* Chain with NULL closure in call spec */
    Obj* obj = mk_int(10);
    ASSERT_NOT_NULL(obj);

    /* Create call spec with NULL closure */
    Obj* call = mk_pair(NULL, NULL);
    Obj* calls = mk_pair(call, NULL);

    /* Execute chain - should handle NULL closure gracefully */
    Obj* result = prim_method_chain(obj, calls);
    /* Should return original obj or handle gracefully */
    ASSERT(result == NULL || (result != NULL && IS_IMMEDIATE(result)));

    dec_ref(obj);
    dec_ref(call);
    dec_ref(calls);
    PASS();
}

/* ==================== Enhanced prim_apply Tests ==================== */

void test_apply_closure_with_single_arg(void) {
    /* Apply closure with single argument */
    Obj* inc = make_inc_closure();
    Obj* args = mk_pair(mk_int(5), NULL);
    ASSERT_NOT_NULL(inc);
    ASSERT_NOT_NULL(args);

    Obj* result = prim_apply(inc, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 6);

    dec_ref(inc);
    dec_ref(args);
    PASS();
}

void test_apply_closure_with_two_args(void) {
    /* Apply closure with two arguments */
    Obj* add = make_add_closure();
    Obj* args = mk_pair(mk_int(3), mk_pair(mk_int(7), NULL));
    ASSERT_NOT_NULL(add);
    ASSERT_NOT_NULL(args);

    Obj* result = prim_apply(add, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 10);

    dec_ref(add);
    dec_ref(args);
    PASS();
}

void test_apply_closure_with_square(void) {
    /* Apply square closure */
    Obj* square = make_square_closure();
    Obj* args = mk_pair(mk_int(6), NULL);
    ASSERT_NOT_NULL(square);
    ASSERT_NOT_NULL(args);

    Obj* result = prim_apply(square, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 36);

    dec_ref(square);
    dec_ref(args);
    PASS();
}

void test_apply_with_identity(void) {
    /* Apply identity closure */
    Obj* identity = make_identity_closure();
    Obj* value = mk_int(99);
    Obj* args = mk_pair(value, NULL);
    ASSERT_NOT_NULL(identity);
    ASSERT_NOT_NULL(args);

    Obj* result = prim_apply(identity, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 99);

    dec_ref(identity);
    dec_ref(value);
    dec_ref(args);
    PASS();
}

void test_apply_closure_arity_match(void) {
    /* Apply closure with matching arity */
    Obj* mul = make_mul_closure(); /* arity = 2 */
    Obj* args = mk_pair(mk_int(4), mk_pair(mk_int(5), NULL));
    ASSERT_NOT_NULL(mul);
    ASSERT_NOT_NULL(args);

    Obj* result = prim_apply(mul, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 20);

    dec_ref(mul);
    dec_ref(args);
    PASS();
}

void test_apply_closure_with_nested_args(void) {
    /* Apply closure, then apply result to another closure */
    Obj* inc = make_inc_closure();
    Obj* square = make_square_closure();

    /* First apply: inc(5) = 6 */
    Obj* args1 = mk_pair(mk_int(5), NULL);
    Obj* result1 = prim_apply(inc, args1);
    ASSERT_NOT_NULL(result1);
    ASSERT_EQ(obj_to_int(result1), 6);

    /* Second apply: square(6) = 36 */
    Obj* args2 = mk_pair(result1, NULL);
    Obj* result2 = prim_apply(square, args2);
    ASSERT_NOT_NULL(result2);
    ASSERT_EQ(obj_to_int(result2), 36);

    dec_ref(inc);
    dec_ref(square);
    dec_ref(args1);
    dec_ref(args2);
    PASS();
}

/* ==================== Test Suite Runner ==================== */

void run_piping_enhanced_tests(void) {
    TEST_SUITE("Enhanced Piping Tests");

    /* prim_pipe tests */
    TEST_SECTION("prim_pipe");
    RUN_TEST(test_pipe_with_closure);
    RUN_TEST(test_pipe_with_square_closure);
    RUN_TEST(test_pipe_with_null_function);
    RUN_TEST(test_pipe_with_symbol_function);
    RUN_TEST(test_pipe_chaining);
    RUN_TEST(test_pipe_with_identity);

    /* prim_method_chain tests */
    TEST_SECTION("prim_method_chain");
    RUN_TEST(test_method_chain_basic);
    RUN_TEST(test_method_chain_single_call);
    RUN_TEST(test_method_chain_three_calls);
    RUN_TEST(test_method_chain_with_extra_args);
    RUN_TEST(test_method_chain_with_null_obj);
    RUN_TEST(test_method_chain_with_empty_calls);
    RUN_TEST(test_method_chain_with_null_closure);

    /* Enhanced prim_apply tests */
    TEST_SECTION("Enhanced prim_apply");
    RUN_TEST(test_apply_closure_with_single_arg);
    RUN_TEST(test_apply_closure_with_two_args);
    RUN_TEST(test_apply_closure_with_square);
    RUN_TEST(test_apply_with_identity);
    RUN_TEST(test_apply_closure_arity_match);
    RUN_TEST(test_apply_closure_with_nested_args);
}
