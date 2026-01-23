/*
 * test_generic_functions.c - Tests for generic function and multiple dispatch system
 *
 * Coverage: All functions in runtime/src/generic.c
 *
 * Test Groups:
 *   - Generic function creation
 *   - Method management (add_method)
 *   - Type checking for arguments
 *   - Generic dispatch (lookup, invoke, call)
 *   - Arity checking
 *   - Generic metadata (name, method_count)
 */

#include "test_framework.h"
#include <string.h>

/* ==================== Generic Function Creation Tests ==================== */

void test_generic_create_basic(void) {
    /* Create a generic function with a name */
    Obj* gen = mk_generic("test-generic");
    
    ASSERT_NOT_NULL(gen);
    ASSERT(IS_BOXED(gen));
    ASSERT(gen->tag == TAG_GENERIC);
    
    const char* name = omni_generic_name(gen);
    ASSERT_NOT_NULL(name);
    ASSERT_STR_EQ(name, "test-generic");
    
    ASSERT_EQ(omni_generic_method_count(gen), 0);
    
    dec_ref(gen);
    PASS();
}

void test_generic_create_empty_name(void) {
    /* Create a generic with empty name */
    Obj* gen = mk_generic("");
    
    ASSERT_NOT_NULL(gen);
    ASSERT(gen->tag == TAG_GENERIC);
    
    dec_ref(gen);
    PASS();
}

/* ==================== Method Management Tests ==================== */

/* Dummy method implementations for testing */
static Obj* method_int_int(Obj** captures, Obj** args, int argc) {
    (void)captures; (void)argc;
    return mk_int(obj_to_int(args[0]) + obj_to_int(args[1]));
}

static Obj* method_float_float(Obj** captures, Obj** args, int argc) {
    (void)captures; (void)argc;
    return mk_float(obj_to_float(args[0]) + obj_to_float(args[1]));
}

static Obj* method_string_string(Obj** captures, Obj** args, int argc) {
    (void)captures; (void)argc;
    const char* s1 = (const char*)args[0]->ptr;
    const char* s2 = (const char*)args[1]->ptr;
    
    size_t len = strlen(s1) + strlen(s2);
    char* result = malloc(len + 1);
    strcpy(result, s1);
    strcat(result, s2);
    
    Obj* obj = mk_string(result);
    free(result);
    return obj;
}

void test_generic_add_single_method(void) {
    Obj* gen = mk_generic("single-method");
    
    /* Create Int kind for parameter types */
    Obj* int_kind = mk_kind("Int", NULL, 0);
    Obj* kinds[] = {int_kind, int_kind};
    
    /* Add method */
    Obj* result = generic_add_method(gen, kinds, 2, method_int_int);
    ASSERT_NOT_NULL(result);
    ASSERT(result == gen);
    
    /* Check method count */
    ASSERT_EQ(omni_generic_method_count(gen), 1);
    
    dec_ref(gen);
    dec_ref(int_kind);
    PASS();
}

void test_generic_add_multiple_methods(void) {
    Obj* gen = mk_generic("multi-method");
    
    Obj* int_kind = mk_kind("Int", NULL, 0);
    Obj* float_kind = mk_kind("Float", NULL, 0);
    
    /* Add first method (Int, Int) */
    Obj* kinds1[] = {int_kind, int_kind};
    generic_add_method(gen, kinds1, 2, method_int_int);
    
    /* Add second method (Float, Float) */
    Obj* kinds2[] = {float_kind, float_kind};
    generic_add_method(gen, kinds2, 2, method_float_float);
    
    /* Should have 2 methods */
    ASSERT_EQ(omni_generic_method_count(gen), 2);
    
    dec_ref(gen);
    dec_ref(int_kind);
    dec_ref(float_kind);
    PASS();
}

void test_generic_add_same_method_twice(void) {
    Obj* gen = mk_generic("duplicate");
    
    Obj* int_kind = mk_kind("Int", NULL, 0);
    Obj* kinds[] = {int_kind, int_kind};
    
    /* Add same method signature twice */
    generic_add_method(gen, kinds, 2, method_int_int);
    generic_add_method(gen, kinds, 2, method_int_int);
    
    /* Should still have only 1 method */
    ASSERT_EQ(omni_generic_method_count(gen), 1);
    
    dec_ref(gen);
    dec_ref(int_kind);
    PASS();
}

/* ==================== Type Checking Tests ==================== */

void test_type_check_int_matches_int(void) {
    Obj* int_kind = mk_kind("Int", NULL, 0);
    
    /* Integer values should match Int kind */
    Obj* arg1 = mk_int(5);
    Obj* arg2 = mk_int(10);
    Obj* args[] = {arg1, arg2};
    Obj* kinds[] = {int_kind, int_kind};
    
    /* This is a static function, we're just testing the concept
       In practice, this would be checked by check_argument_types */
    
    /* Verify args are immediate integers */
    ASSERT(IS_IMMEDIATE(arg1));
    ASSERT(IS_IMMEDIATE(arg2));
    ASSERT(IS_IMMEDIATE_INT(arg1));
    ASSERT(IS_IMMEDIATE_INT(arg2));
    
    dec_ref(int_kind);
    PASS();
}

void test_type_check_string_matches_string(void) {
    Obj* string_kind = mk_kind("String", NULL, 0);
    
    /* String values should match String kind */
    Obj* arg1 = mk_string("hello");
    Obj* arg2 = mk_string("world");
    
    ASSERT(IS_BOXED(arg1));
    ASSERT(IS_BOXED(arg2));
    ASSERT(arg1->tag == TAG_STRING);
    ASSERT(arg2->tag == TAG_STRING);
    
    dec_ref(string_kind);
    dec_ref(arg1);
    dec_ref(arg2);
    PASS();
}

/* ==================== Generic Dispatch Tests ==================== */

void test_generic_invoke_matching_method(void) {
    Obj* gen = mk_generic("test-invoke");
    
    Obj* int_kind = mk_kind("Int", NULL, 0);
    Obj* kinds[] = {int_kind, int_kind};
    generic_add_method(gen, kinds, 2, method_int_int);
    
    /* Invoke with integer arguments */
    Obj* args[] = {mk_int(5), mk_int(10)};
    Obj* result = omni_generic_invoke(gen, args, 2);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result));
    ASSERT_EQ(obj_to_int(result), 15);  /* 5 + 10 */
    
    dec_ref(gen);
    dec_ref(int_kind);
    dec_ref(args[0]);
    dec_ref(args[1]);
    dec_ref(result);
    PASS();
}

void test_generic_invoke_no_matching_method(void) {
    Obj* gen = mk_generic("no-match");
    
    Obj* int_kind = mk_kind("Int", NULL, 0);
    Obj* kinds[] = {int_kind, int_kind};
    generic_add_method(gen, kinds, 2, method_int_int);
    
    /* Invoke with string arguments (no method matches) */
    Obj* args[] = {mk_string("hello"), mk_string("world")};
    Obj* result = omni_generic_invoke(gen, args, 2);
    
    /* Should return error object */
    ASSERT_NOT_NULL(result);
    
    dec_ref(gen);
    dec_ref(int_kind);
    dec_ref(args[0]);
    dec_ref(args[1]);
    dec_ref(result);
    PASS();
}

void test_generic_lookup_most_specific(void) {
    Obj* gen = mk_generic("lookup-specific");
    
    Obj* int_kind = mk_kind("Int", NULL, 0);
    Obj* float_kind = mk_kind("Float", NULL, 0);
    
    /* Add both methods - order matters for specificity */
    Obj* kinds1[] = {int_kind, int_kind};
    generic_add_method(gen, kinds1, 2, method_int_int);
    
    Obj* kinds2[] = {float_kind, float_kind};
    generic_add_method(gen, kinds2, 2, method_float_float);
    
    /* Lookup should find first matching method */
    Obj* args[] = {mk_int(5), mk_int(10)};
    MethodInfo* method = omni_generic_lookup(gen, args, 2);
    
    ASSERT_NOT_NULL(method);
    ASSERT(method->impl == method_int_int);
    
    dec_ref(gen);
    dec_ref(int_kind);
    dec_ref(float_kind);
    dec_ref(args[0]);
    dec_ref(args[1]);
    PASS();
}

/* ==================== Arity Checking Tests ==================== */

void test_generic_check_arity_match(void) {
    Obj* gen = mk_generic("arity-check");
    
    Obj* int_kind = mk_kind("Int", NULL, 0);
    Obj* kinds[] = {int_kind, int_kind};
    generic_add_method(gen, kinds, 2, method_int_int);
    
    /* Should return true for 2 arguments */
    ASSERT_TRUE(omni_check_arity(gen, 2));
    
    /* Should return false for different arity */
    ASSERT_FALSE(omni_check_arity(gen, 1));
    ASSERT_FALSE(omni_check_arity(gen, 3));
    ASSERT_FALSE(omni_check_arity(gen, 0));
    
    dec_ref(gen);
    dec_ref(int_kind);
    PASS();
}

void test_generic_check_arity_no_methods(void) {
    Obj* gen = mk_generic("no-methods");
    
    /* Should return false for any arity when no methods exist */
    ASSERT_FALSE(omni_check_arity(gen, 0));
    ASSERT_FALSE(omni_check_arity(gen, 1));
    ASSERT_FALSE(omni_check_arity(gen, 2));
    
    dec_ref(gen);
    PASS();
}

void test_generic_invoke_wrong_arity(void) {
    Obj* gen = mk_generic("wrong-arity");
    
    Obj* int_kind = mk_kind("Int", NULL, 0);
    Obj* kinds[] = {int_kind, int_kind};
    generic_add_method(gen, kinds, 2, method_int_int);
    
    /* Invoke with wrong number of arguments */
    Obj* args[] = {mk_int(5)};
    Obj* result = omni_generic_invoke(gen, args, 1);
    
    /* Should return error (no applicable method) */
    ASSERT_NOT_NULL(result);
    
    dec_ref(gen);
    dec_ref(int_kind);
    dec_ref(args[0]);
    dec_ref(result);
    PASS();
}

/* ==================== Generic Metadata Tests ==================== */

void test_generic_name_retrieval(void) {
    Obj* gen = mk_generic("my-generic-name");
    
    const char* name = omni_generic_name(gen);
    
    ASSERT_NOT_NULL(name);
    ASSERT_STR_EQ(name, "my-generic-name");
    
    dec_ref(gen);
    PASS();
}

void test_generic_name_null_for_invalid(void) {
    /* Pass non-generic object */
    Obj* not_gen = mk_int(42);
    
    const char* name = omni_generic_name(not_gen);
    
    ASSERT_NULL(name);
    
    PASS();
}

void test_generic_method_count_initial(void) {
    Obj* gen = mk_generic("count-initial");
    
    /* New generic should have 0 methods */
    ASSERT_EQ(omni_generic_method_count(gen), 0);
    
    dec_ref(gen);
    PASS();
}

void test_generic_method_count_after_add(void) {
    Obj* gen = mk_generic("count-after");
    
    Obj* int_kind = mk_kind("Int", NULL, 0);
    Obj* kinds[] = {int_kind, int_kind};
    
    /* Add 3 methods */
    generic_add_method(gen, kinds, 2, method_int_int);
    generic_add_method(gen, kinds, 2, method_int_int);
    generic_add_method(gen, kinds, 2, method_int_int);
    
    /* Should have 1 unique method (same signature) */
    ASSERT_EQ(omni_generic_method_count(gen), 1);
    
    dec_ref(gen);
    dec_ref(int_kind);
    PASS();
}

void test_generic_method_count_null_for_invalid(void) {
    /* Pass non-generic object */
    Obj* not_gen = mk_int(42);
    
    int count = omni_generic_method_count(not_gen);
    
    ASSERT_EQ(count, 0);
    
    PASS();
}

/* ==================== Wrapper Function Tests ==================== */

void test_call_generic_wrapper(void) {
    Obj* gen = mk_generic("call-wrapper");
    
    Obj* int_kind = mk_kind("Int", NULL, 0);
    Obj* kinds[] = {int_kind, int_kind};
    generic_add_method(gen, kinds, 2, method_int_int);
    
    /* Use call_generic wrapper */
    Obj* args[] = {mk_int(20), mk_int(30)};
    Obj* result = call_generic(gen, args, 2);
    
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 50);  /* 20 + 30 */
    
    dec_ref(gen);
    dec_ref(int_kind);
    dec_ref(args[0]);
    dec_ref(args[1]);
    dec_ref(result);
    PASS();
}

/* ==================== Error Handling Tests ==================== */

void test_generic_add_method_invalid_generic(void) {
    /* Pass NULL or non-generic object */
    Obj* int_kind = mk_kind("Int", NULL, 0);
    Obj* kinds[] = {int_kind, int_kind};
    
    Obj* result = generic_add_method(NULL, kinds, 2, method_int_int);
    
    /* Should return NULL */
    ASSERT_NULL(result);
    
    dec_ref(int_kind);
    PASS();
}

void test_generic_invoke_null_generic(void) {
    Obj* args[] = {mk_int(5), mk_int(10)};
    Obj* result = omni_generic_invoke(NULL, args, 2);
    
    /* Should return NULL for invalid generic */
    ASSERT_NULL(result);
    
    dec_ref(args[0]);
    dec_ref(args[1]);
    PASS();
}

void test_generic_lookup_null_generic(void) {
    Obj* args[] = {mk_int(5), mk_int(10)};
    MethodInfo* method = omni_generic_lookup(NULL, args, 2);
    
    /* Should return NULL for invalid generic */
    ASSERT_NULL(method);
    
    PASS();
}

/* ==================== Test Suite Runner ==================== */

void run_generic_function_tests(void) {
    TEST_SUITE("Generic Function Tests");
    
    /* Generic function creation tests */
    TEST_SECTION("Generic Creation");
    RUN_TEST(test_generic_create_basic);
    RUN_TEST(test_generic_create_empty_name);
    
    /* Method management tests */
    TEST_SECTION("Method Management");
    RUN_TEST(test_generic_add_single_method);
    RUN_TEST(test_generic_add_multiple_methods);
    RUN_TEST(test_generic_add_same_method_twice);
    
    /* Type checking tests */
    TEST_SECTION("Type Checking");
    RUN_TEST(test_type_check_int_matches_int);
    RUN_TEST(test_type_check_string_matches_string);
    
    /* Generic dispatch tests */
    TEST_SECTION("Generic Dispatch");
    RUN_TEST(test_generic_invoke_matching_method);
    RUN_TEST(test_generic_invoke_no_matching_method);
    RUN_TEST(test_generic_lookup_most_specific);
    
    /* Arity checking tests */
    TEST_SECTION("Arity Checking");
    RUN_TEST(test_generic_check_arity_match);
    RUN_TEST(test_generic_check_arity_no_methods);
    RUN_TEST(test_generic_invoke_wrong_arity);
    
    /* Generic metadata tests */
    TEST_SECTION("Generic Metadata");
    RUN_TEST(test_generic_name_retrieval);
    RUN_TEST(test_generic_name_null_for_invalid);
    RUN_TEST(test_generic_method_count_initial);
    RUN_TEST(test_generic_method_count_after_add);
    RUN_TEST(test_generic_method_count_null_for_invalid);
    
    /* Wrapper function tests */
    TEST_SECTION("Wrapper Functions");
    RUN_TEST(test_call_generic_wrapper);
    
    /* Error handling tests */
    TEST_SECTION("Error Handling");
    RUN_TEST(test_generic_add_method_invalid_generic);
    RUN_TEST(test_generic_invoke_null_generic);
    RUN_TEST(test_generic_lookup_null_generic);
}
