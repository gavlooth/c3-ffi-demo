/*
 * test_argument_type_compatibility.c
 *
 * Purpose:
 *   Tests for argument type compatibility checking in multiple dispatch.
 *   This function is critical for ensuring type safety in function calls
 *   and method selection during runtime dispatch.
 *
 * Why this file exists:
 *   omni_check_argument_type_compatibility is a core type checking function
 *   that verifies arguments match expected parameter types. It handles:
 *   - Literal type matching (Int, String, Bool)
 *   - Symbol/variable compatibility checking
 *   - Any type acceptance (wildcard)
 *   - Truthy/falsy integer handling for Bool parameters
 *   - Subtype relationships (foundation for future full type checking)
 *
 * Coverage Gaps Filled:
 *   - Literal type validation (integer, string, boolean)
 *   - Any type parameter acceptance
 *   - Integer as boolean compatibility (0/1)
 *   - Symbol handling with type annotations
 *   - Edge cases (null parameter type, null argument)
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../ast/ast.h"
#include "../analysis/analysis.h"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %s: ", #name); \
    name(); \
    tests_run++; \
    tests_passed++; \
    printf("\033[32mPASS\033[0m\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\033[31mFAIL\033[0m (line %d: %s)\n", __LINE__, #cond); \
        tests_run++; \
        return; \
    } \
} while(0)

/* ========== Helper Functions ========== */

/* Create an integer literal node */
static OmniValue* mk_int(int val) {
    return omni_new_int(val);
}

/* Create a string literal node */
static OmniValue* mk_string(const char* str) {
    return omni_new_string(str);
}

/* Create a symbol node */
static OmniValue* mk_sym(const char* name) {
    return omni_new_sym(name);
}

/* ========== Edge Cases ========== */

TEST(test_null_param_type) {
    /*
     * When param_type is NULL, the function accepts any type.
     * This is the "no type constraint = any type OK" behavior.
     */
    AnalysisContext* ctx = omni_analysis_new();
    OmniValue* arg = mk_int(42);

    bool result = omni_check_argument_type_compatibility(ctx, NULL, arg);
    ASSERT(result == true && "NULL param_type should accept any argument");

    omni_analysis_free(ctx);
}

TEST(test_null_argument) {
    /*
     * When arg_node is NULL, compatibility check should fail.
     * You can't pass NULL as an argument.
     */
    AnalysisContext* ctx = omni_analysis_new();

    bool result = omni_check_argument_type_compatibility(ctx, "Int", NULL);
    ASSERT(result == false && "NULL argument should be rejected");

    omni_analysis_free(ctx);
}

/* ========== Any Type Parameter ========== */

TEST(test_any_type_accepts_all) {
    /*
     * The "Any" type is a wildcard that accepts any argument type.
     * This is the universal supertype in OmniLisp's type hierarchy.
     */
    AnalysisContext* ctx = omni_analysis_new();

    /* Any accepts integer */
    OmniValue* int_arg = mk_int(42);
    ASSERT(omni_check_argument_type_compatibility(ctx, "Any", int_arg) == true);

    /* Any accepts string */
    OmniValue* str_arg = mk_string("hello");
    ASSERT(omni_check_argument_type_compatibility(ctx, "Any", str_arg) == true);

    /* Any accepts symbol */
    OmniValue* sym_arg = mk_sym("foo");
    ASSERT(omni_check_argument_type_compatibility(ctx, "Any", sym_arg) == true);

    omni_analysis_free(ctx);
}

/* ========== Integer Literal Matching ========== */

TEST(test_int_param_accepts_int_literal) {
    /*
     * Integer parameters should accept integer literal arguments.
     * This is exact type matching for literals.
     */
    AnalysisContext* ctx = omni_analysis_new();

    /* Positive integers */
    OmniValue* pos_int = mk_int(42);
    ASSERT(omni_check_argument_type_compatibility(ctx, "Int", pos_int) == true);

    /* Zero */
    OmniValue* zero = mk_int(0);
    ASSERT(omni_check_argument_type_compatibility(ctx, "Int", zero) == true);

    /* Negative integers */
    OmniValue* neg_int = mk_int(-123);
    ASSERT(omni_check_argument_type_compatibility(ctx, "Int", neg_int) == true);

    omni_analysis_free(ctx);
}

TEST(test_int_param_rejects_non_int_literal) {
    /*
     * Integer parameters strictly reject non-integer literal types.
     * Type safety is enforced for literals through exact matching.
     */
    AnalysisContext* ctx = omni_analysis_new();

    /* String literal should NOT be accepted as Int */
    OmniValue* str_arg = mk_string("123");
    bool result = omni_check_argument_type_compatibility(ctx, "Int", str_arg);
    ASSERT(result == false && "String literal should be rejected for Int parameter");

    /* Symbol is accepted conservatively (no type info) */
    OmniValue* sym_arg = mk_sym("my-var");
    result = omni_check_argument_type_compatibility(ctx, "Int", sym_arg);
    ASSERT(result == true && "Symbol without type annotation is conservatively accepted");

    omni_analysis_free(ctx);
}

/* ========== String Literal Matching ========== */

TEST(test_string_param_accepts_string_literal) {
    /*
     * String parameters should accept string literal arguments.
     * Exact type matching for string literals.
     */
    AnalysisContext* ctx = omni_analysis_new();

    /* Regular string */
    OmniValue* str_arg = mk_string("hello world");
    ASSERT(omni_check_argument_type_compatibility(ctx, "String", str_arg) == true);

    /* Empty string */
    OmniValue* empty_str = mk_string("");
    ASSERT(omni_check_argument_type_compatibility(ctx, "String", empty_str) == true);

    omni_analysis_free(ctx);
}

/* ========== Boolean Type Handling ========== */

TEST(test_bool_param_accepts_boolean_symbols) {
    /*
     * Boolean parameters should accept 'true and 'false symbols.
     * These are the canonical boolean literals in OmniLisp.
     */
    AnalysisContext* ctx = omni_analysis_new();

    /* true symbol */
    OmniValue* true_sym = mk_sym("true");
    ASSERT(omni_check_argument_type_compatibility(ctx, "Bool", true_sym) == true);

    /* false symbol */
    OmniValue* false_sym = mk_sym("false");
    ASSERT(omni_check_argument_type_compatibility(ctx, "Bool", false_sym) == true);

    omni_analysis_free(ctx);
}

TEST(test_bool_param_accepts_truthy_integers) {
    /*
     * Integer 0 and 1 are accepted as boolean values.
     * 0 = false, 1 = true (truthy/falsy convention).
     * Other integers should be rejected.
     */
    AnalysisContext* ctx = omni_analysis_new();

    /* Integer 0 (falsy) */
    OmniValue* zero = mk_int(0);
    ASSERT(omni_check_argument_type_compatibility(ctx, "Bool", zero) == true);

    /* Integer 1 (truthy) */
    OmniValue* one = mk_int(1);
    ASSERT(omni_check_argument_type_compatibility(ctx, "Bool", one) == true);

    omni_analysis_free(ctx);
}

TEST(test_bool_param_rejects_other_integers) {
    /*
     * Integers other than 0 and 1 should be rejected for Bool parameters.
     * Enforces strict boolean convention.
     */
    AnalysisContext* ctx = omni_analysis_new();

    /* Integer 2 should not be accepted as Bool */
    OmniValue* two = mk_int(2);
    bool result = omni_check_argument_type_compatibility(ctx, "Bool", two);
    ASSERT(result == false && "Integer 2 should be rejected for Bool parameter");

    /* Integer -1 should not be accepted as Bool */
    OmniValue* neg_one = mk_int(-1);
    result = omni_check_argument_type_compatibility(ctx, "Bool", neg_one);
    ASSERT(result == false && "Integer -1 should be rejected for Bool parameter");

    /* Integer 42 should not be accepted as Bool */
    OmniValue* forty_two = mk_int(42);
    result = omni_check_argument_type_compatibility(ctx, "Bool", forty_two);
    ASSERT(result == false && "Integer 42 should be rejected for Bool parameter");

    omni_analysis_free(ctx);
}

/* ========== Symbol/Variable Handling ========== */

TEST(test_symbol_argument_conservative_acceptance) {
    /*
     * Symbol arguments are currently accepted conservatively.
     * Full type inference is not yet implemented, so the
     * function returns true for all symbols.
     * TODO: This should be enhanced with proper subtype checking
     * once variable type tracking is implemented.
     */
    AnalysisContext* ctx = omni_analysis_new();

    /* Symbol with no type annotation */
    OmniValue* untyped_sym = mk_sym("my-var");
    ASSERT(omni_check_argument_type_compatibility(ctx, "Int", untyped_sym) == true);

    /* Symbol for boolean */
    OmniValue* bool_sym = mk_sym("flag");
    ASSERT(omni_check_argument_type_compatibility(ctx, "Bool", bool_sym) == true);

    /* Symbol for string */
    OmniValue* str_sym = mk_sym("name");
    ASSERT(omni_check_argument_type_compatibility(ctx, "String", str_sym) == true);

    omni_analysis_free(ctx);
}

/* ========== Unknown Type Parameters ========== */

TEST(test_unknown_param_type_conservative) {
    /*
     * For unknown type parameters, the function is conservative
     * and accepts the argument. This is a temporary measure
     * until full type inference and subtype checking is implemented.
     */
    AnalysisContext* ctx = omni_analysis_new();

    OmniValue* arg = mk_int(42);
    bool result = omni_check_argument_type_compatibility(ctx, "UnknownType", arg);
    /* Currently returns true (conservative) */
    ASSERT(result == true);

    omni_analysis_free(ctx);
}

/* ========== Multiple Type Categories ========== */

TEST(test_comprehensive_type_categories) {
    /*
     * Test all major type categories work correctly.
     * Ensures no regressions in type handling.
     */
    AnalysisContext* ctx = omni_analysis_new();

    /* Int -> Int: exact match */
    OmniValue* int_val = mk_int(10);
    ASSERT(omni_check_argument_type_compatibility(ctx, "Int", int_val) == true);

    /* String -> String: exact match */
    OmniValue* str_val = mk_string("test");
    ASSERT(omni_check_argument_type_compatibility(ctx, "String", str_val) == true);

    /* Bool -> Bool: symbol match */
    OmniValue* bool_val = mk_sym("true");
    ASSERT(omni_check_argument_type_compatibility(ctx, "Bool", bool_val) == true);

    /* Any -> anything: wildcard */
    ASSERT(omni_check_argument_type_compatibility(ctx, "Any", int_val) == true);
    ASSERT(omni_check_argument_type_compatibility(ctx, "Any", str_val) == true);
    ASSERT(omni_check_argument_type_compatibility(ctx, "Any", bool_val) == true);

    omni_analysis_free(ctx);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Argument Type Compatibility Tests ===\033[0m\n");

    printf("\n\033[33m--- Edge Cases ---\033[0m\n");
    RUN_TEST(test_null_param_type);
    RUN_TEST(test_null_argument);

    printf("\n\033[33m--- Any Type Parameter ---\033[0m\n");
    RUN_TEST(test_any_type_accepts_all);

    printf("\n\033[33m--- Integer Literal Matching ---\033[0m\n");
    RUN_TEST(test_int_param_accepts_int_literal);
    RUN_TEST(test_int_param_rejects_non_int_literal);

    printf("\n\033[33m--- String Literal Matching ---\033[0m\n");
    RUN_TEST(test_string_param_accepts_string_literal);

    printf("\n\033[33m--- Boolean Type Handling ---\033[0m\n");
    RUN_TEST(test_bool_param_accepts_boolean_symbols);
    RUN_TEST(test_bool_param_accepts_truthy_integers);
    RUN_TEST(test_bool_param_rejects_other_integers);

    printf("\n\033[33m--- Symbol/Variable Handling ---\033[0m\n");
    RUN_TEST(test_symbol_argument_conservative_acceptance);

    printf("\n\033[33m--- Unknown Type Parameters ---\033[0m\n");
    RUN_TEST(test_unknown_param_type_conservative);

    printf("\n\033[33m--- Comprehensive Type Categories ---\033[0m\n");
    RUN_TEST(test_comprehensive_type_categories);

    printf("\n\033[33m=== Summary ===\033[0m\n");
    printf("  Total:  %d\n", tests_run);
    if (tests_passed == tests_run) {
        printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
    } else {
        printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
        printf("  \033[31mFailed: %d\033[0m\n", tests_run - tests_passed);
    }

    return (tests_passed == tests_run) ? 0 : 1;
}
