/*
 * test_codegen_mangle.c
 *
 * Purpose:
 *   Unit tests for omni_codegen_mangle() function.
 *
 * Why this matters:
 *   The name mangler converts OmniLisp symbols (which can contain special
 *   characters like +, -, *, /, =, <, >, ?, !, etc.) into valid C
 *   identifiers. This is critical for code generation because C has strict
 *   rules about what characters can appear in identifiers.
 *
 *   If mangle() fails, generated C code will contain invalid identifiers
 *   that won't compile, even for correct OmniLisp source.
 *
 * Contract:
 *   - Alphanumeric characters pass through unchanged
 *   - Special operator symbols are replaced with underscore + mneumonic
 *   - All mangled names start with 'o_' prefix
 *   - Result is a valid C identifier (caller must free)
 */

#include "../codegen/codegen.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void assert_mangles_to(const char* name, const char* expected) {
    char* mangled = omni_codegen_mangle(name);
    if (strcmp(mangled, expected) != 0) {
        fprintf(stderr, "FAIL: Expected '%s' to mangle to '%s', got '%s'\n",
                name, expected, mangled);
        free(mangled);
        assert(0 && "mangling mismatch");
    }
    free(mangled);
}

static void test_mangle_simple_symbol(void) {
    /* Simple alphanumeric symbols should pass through unchanged */
    assert_mangles_to("foo", "o_foo");
    assert_mangles_to("bar", "o_bar");
    assert_mangles_to("myVar123", "o_myVar123");
}

static void test_mangle_addition(void) {
    /* + operator should mangle to _add */
    assert_mangles_to("+", "o__add");
    assert_mangles_to("add+", "o_add_add");
}

static void test_mangle_subtraction(void) {
    /* - operator should mangle to _sub */
    assert_mangles_to("-", "o__sub");
    assert_mangles_to("sub-", "o_sub_sub");
}

static void test_mangle_multiplication(void) {
    /* * operator should mangle to _mul */
    assert_mangles_to("*", "o__mul");
    assert_mangles_to("mul*", "o_mul_mul");
}

static void test_mangle_division(void) {
    /* / operator should mangle to _div */
    assert_mangles_to("/", "o__div");
    assert_mangles_to("div/", "o_div_div");
}

static void test_mangle_equality(void) {
    /* = operator should mangle to _eq */
    assert_mangles_to("=", "o__eq");
    assert_mangles_to("eq=", "o_eq_eq");
}

static void test_mangle_less_than(void) {
    /* < operator should mangle to _lt */
    assert_mangles_to("<", "o__lt");
    assert_mangles_to("lt<", "o_lt_lt");
}

static void test_mangle_greater_than(void) {
    /* > operator should mangle to _gt */
    assert_mangles_to(">", "o__gt");
    assert_mangles_to("gt>", "o_gt_gt");
}

static void test_mangle_question(void) {
    /* ? operator should mangle to _p */
    assert_mangles_to("?", "o__p");
    assert_mangles_to("pred?", "o_pred_p");
}

static void test_mangle_bang(void) {
    /* ! operator should mangle to _b */
    assert_mangles_to("!", "o__b");
    assert_mangles_to("set!", "o_set_b");
}

static void test_mangle_dot(void) {
    /* . should mangle to _d */
    assert_mangles_to(".", "o__d");
    assert_mangles_to("obj.field", "o_obj_dfield");
}

static void test_mangle_underscore(void) {
    /* _ should mangle to __ (double underscore) */
    assert_mangles_to("_", "o___");
    assert_mangles_to("_private", "o___private");
}

static void test_mangle_complex_symbols(void) {
    /* Symbols with multiple special characters */
    assert_mangles_to("+-*/=", "o__add_sub_mul_div_eq");
    assert_mangles_to("<=?", "o__lt_eq_p");
    assert_mangles_to("vector->list", "o_vector_sub_gtlist");
}

int main(void) {
    test_mangle_simple_symbol();
    test_mangle_addition();
    test_mangle_subtraction();
    test_mangle_multiplication();
    test_mangle_division();
    test_mangle_equality();
    test_mangle_less_than();
    test_mangle_greater_than();
    test_mangle_question();
    test_mangle_bang();
    test_mangle_dot();
    test_mangle_underscore();
    test_mangle_complex_symbols();

    printf("PASS: test_codegen_mangle\n");
    return 0;
}
