/*@semantic
id: file::testing.c
kind: file
name: testing.c
summary: Built-in testing framework for OmniLisp runtime.
responsibility:
  - Provide test case registration and execution
  - Implement assertion primitives for equality, boolean, exception testing
  - Generate test reports with pass/fail summaries
inputs:
  - Test functions registered via prim_deftest
outputs:
  - Test results and reports
side_effects:
  - io (prints test results)
  - global_state (maintains test registry)
related_symbols:
  - omni.h
  - runtime.c (prim_eq)
tags:
  - testing
  - assertions
  - developer-tools
  - issue-27-p3
*/

#include "../include/omni.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

/* Forward declarations */
/* apply is prim_apply in piping.c */

/* ============================================================================
 * Test Registry Data Structures
 * ============================================================================ */

/*@semantic
id: struct::TestCase
kind: struct
name: TestCase
summary: Represents a single test case in the test registry.
responsibility:
  - Store test name, function, and result
  - Form linked list of tests
tags:
  - testing
  - registry
*/
typedef struct TestCase {
    const char* name;
    Obj* (*test_fn)(void);
    Obj* test_closure;  /* Alternative: closure-based test */
    bool passed;
    char error_msg[512];
    struct TestCase* next;
} TestCase;

/*@semantic
id: struct::TestStats
kind: struct
name: TestStats
summary: Aggregated statistics for test runs.
tags:
  - testing
  - statistics
*/
typedef struct {
    int total;
    int passed;
    int failed;
    int assertions;
    int assertions_passed;
    int assertions_failed;
} TestStats;

/*@semantic
id: global::g_test_registry
kind: global
name: g_test_registry
summary: Global linked list head for registered test cases.
tags:
  - testing
  - registry
*/
static TestCase* g_test_registry = NULL;
static TestCase* g_test_registry_tail = NULL;
static int g_test_count = 0;

static TestCase* g_current_test = NULL;
static TestStats g_test_stats = {0, 0, 0, 0, 0, 0};

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

static void reset_test_stats(void) {
    g_test_stats.total = 0;
    g_test_stats.passed = 0;
    g_test_stats.failed = 0;
    g_test_stats.assertions = 0;
    g_test_stats.assertions_passed = 0;
    g_test_stats.assertions_failed = 0;
}

static void mark_test_failed(const char* fmt, ...) {
    if (g_current_test && !g_current_test->passed) {
        return;
    }
    if (g_current_test) {
        g_current_test->passed = false;
        va_list args;
        va_start(args, fmt);
        vsnprintf(g_current_test->error_msg, sizeof(g_current_test->error_msg), fmt, args);
        va_end(args);
    }
}

/*@semantic
id: function::obj_to_string_safe
kind: function
name: obj_to_string_safe
summary: Convert object to string representation for error messages.
tags:
  - testing
  - internal
*/
static const char* obj_to_string_safe(Obj* obj) {
    static char buf[256];
    if (!obj) {
        return "nil";
    }
    if (IS_IMMEDIATE(obj)) {
        uintptr_t tag = GET_IMM_TAG(obj);
        if (tag == IMM_TAG_INT) {
            snprintf(buf, sizeof(buf), "%ld", INT_IMM_VALUE(obj));
            return buf;
        } else if (tag == IMM_TAG_BOOL) {
            return (obj == OMNI_TRUE) ? "true" : "false";
        } else if (tag == IMM_TAG_CHAR) {
            snprintf(buf, sizeof(buf), "'%c'", (char)CHAR_IMM_VALUE(obj));
            return buf;
        }
        return "<immediate>";
    }
    switch (obj->tag) {
        case TAG_FLOAT:
            snprintf(buf, sizeof(buf), "%g", obj->f);
            return buf;
        case TAG_STRING:
            snprintf(buf, sizeof(buf), "\"%.*s\"",
                     (int)(sizeof(buf) - 3), obj->ptr ? (char*)obj->ptr : "");
            return buf;
        case TAG_SYM:
            snprintf(buf, sizeof(buf), "%s", obj->ptr ? (char*)obj->ptr : "<symbol>");
            return buf;
        case TAG_KEYWORD:
            snprintf(buf, sizeof(buf), ":%s", obj->ptr ? (char*)obj->ptr : "");
            return buf;
        case TAG_NOTHING:
            return "nothing";
        case TAG_PAIR:
            return "<pair>";
        case TAG_ARRAY:
            snprintf(buf, sizeof(buf), "<array>");
            return buf;
        case TAG_DICT:
            return "<dict>";
        case TAG_SET:
            return "<set>";
        case TAG_CLOSURE:
            return "<closure>";
        case TAG_ERROR:
            snprintf(buf, sizeof(buf), "<error: %s>",
                     obj->ptr ? (char*)obj->ptr : "unknown");
            return buf;
        default:
            snprintf(buf, sizeof(buf), "<object tag=%d>", obj->tag);
            return buf;
    }
}

/*@semantic
id: function::pattern_match
kind: function
name: pattern_match
summary: Simple glob-style pattern matching for test filtering.
tags:
  - testing
  - internal
*/
static bool pattern_match(const char* pattern, const char* str) {
    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            if (!*pattern) return true;
            while (*str) {
                if (pattern_match(pattern, str)) return true;
                str++;
            }
            return false;
        }
        if (*pattern != *str) return false;
        pattern++;
        str++;
    }
    while (*pattern == '*') pattern++;
    return *pattern == '\0' && *str == '\0';
}

/* ============================================================================
 * Test Registration
 * ============================================================================ */

/*@semantic
id: function::prim_deftest
kind: function
name: prim_deftest
summary: Register a test case with name and test closure.
tags:
  - testing
  - registration
*/
Obj* prim_deftest(Obj* name_obj, Obj* test_fn) {
    const char* name = NULL;

    if (!name_obj) {
        return mk_error("deftest: name required");
    }

    if (IS_IMMEDIATE(name_obj)) {
        return mk_error("deftest: name must be string or symbol");
    }

    if (name_obj->tag == TAG_STRING) {
        name = (const char*)name_obj->ptr;
    } else if (name_obj->tag == TAG_SYM) {
        name = (const char*)name_obj->ptr;
    } else {
        return mk_error("deftest: name must be string or symbol");
    }

    if (!test_fn || IS_IMMEDIATE(test_fn) ||
        (test_fn->tag != TAG_CLOSURE && test_fn->tag != TAG_GENERIC)) {
        return mk_error("deftest: test must be a function");
    }

    TestCase* tc = malloc(sizeof(TestCase));
    if (!tc) {
        return mk_error("deftest: allocation failed");
    }

    tc->name = strdup(name);
    tc->test_fn = NULL;
    tc->test_closure = test_fn;
    tc->passed = true;
    tc->error_msg[0] = '\0';
    tc->next = NULL;

    if (!g_test_registry) {
        g_test_registry = tc;
        g_test_registry_tail = tc;
    } else {
        g_test_registry_tail->next = tc;
        g_test_registry_tail = tc;
    }
    g_test_count++;

    return name_obj;
}

/*@semantic
id: function::prim_clear_tests
kind: function
name: prim_clear_tests
summary: Clear all registered tests from the registry.
tags:
  - testing
  - registration
*/
Obj* prim_clear_tests(void) {
    TestCase* tc = g_test_registry;
    while (tc) {
        TestCase* next = tc->next;
        free((void*)tc->name);
        free(tc);
        tc = next;
    }
    g_test_registry = NULL;
    g_test_registry_tail = NULL;
    g_test_count = 0;
    return mk_nothing();
}

/* ============================================================================
 * Assertions
 * ============================================================================ */

/*@semantic
id: function::prim_assert_eq
kind: function
name: prim_assert_eq
summary: Assert that two values are equal.
tags:
  - testing
  - assertions
*/
Obj* prim_assert_eq(Obj* expected, Obj* actual) {
    g_test_stats.assertions++;

    Obj* result = prim_eq(expected, actual);
    bool equal = (result == OMNI_TRUE);

    if (equal) {
        g_test_stats.assertions_passed++;
        return mk_bool(true);
    } else {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-eq failed: expected %s, got %s",
                        obj_to_string_safe(expected),
                        obj_to_string_safe(actual));
        return mk_error("assertion failed");
    }
}

/*@semantic
id: function::prim_assert_true
kind: function
name: prim_assert_true
summary: Assert that a value is truthy.
tags:
  - testing
  - assertions
*/
Obj* prim_assert_true(Obj* value) {
    g_test_stats.assertions++;

    bool truthy = false;
    if (!value) {
        truthy = false;
    } else if (IS_IMMEDIATE(value)) {
        uintptr_t tag = GET_IMM_TAG(value);
        if (tag == IMM_TAG_BOOL) {
            truthy = (value == OMNI_TRUE);
        } else {
            truthy = true;  /* integers, chars are truthy */
        }
    } else {
        truthy = (value->tag != TAG_NOTHING);
    }

    if (truthy) {
        g_test_stats.assertions_passed++;
        return mk_bool(true);
    } else {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-true failed: got %s", obj_to_string_safe(value));
        return mk_error("assertion failed");
    }
}

/*@semantic
id: function::prim_assert_false
kind: function
name: prim_assert_false
summary: Assert that a value is falsy.
tags:
  - testing
  - assertions
*/
Obj* prim_assert_false(Obj* value) {
    g_test_stats.assertions++;

    bool falsy = false;
    if (!value) {
        falsy = true;
    } else if (IS_IMMEDIATE(value)) {
        uintptr_t tag = GET_IMM_TAG(value);
        if (tag == IMM_TAG_BOOL) {
            falsy = (value == OMNI_FALSE);
        } else {
            falsy = false;  /* integers, chars are truthy */
        }
    } else {
        falsy = (value->tag == TAG_NOTHING);
    }

    if (falsy) {
        g_test_stats.assertions_passed++;
        return mk_bool(true);
    } else {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-false failed: got %s (expected falsy)",
                        obj_to_string_safe(value));
        return mk_error("assertion failed");
    }
}

/*@semantic
id: function::prim_assert_near
kind: function
name: prim_assert_near
summary: Assert that two floats are approximately equal within epsilon.
tags:
  - testing
  - assertions
  - floating-point
*/
Obj* prim_assert_near(Obj* expected, Obj* actual, Obj* epsilon) {
    g_test_stats.assertions++;

    double exp_val, act_val, eps_val;

    /* Extract expected */
    if (IS_IMMEDIATE_INT(expected)) {
        exp_val = (double)INT_IMM_VALUE(expected);
    } else if (!IS_IMMEDIATE(expected) && expected->tag == TAG_FLOAT) {
        exp_val = expected->f;
    } else {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-near: expected must be a number");
        return mk_error("assertion failed");
    }

    /* Extract actual */
    if (IS_IMMEDIATE_INT(actual)) {
        act_val = (double)INT_IMM_VALUE(actual);
    } else if (!IS_IMMEDIATE(actual) && actual->tag == TAG_FLOAT) {
        act_val = actual->f;
    } else {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-near: actual must be a number");
        return mk_error("assertion failed");
    }

    /* Extract epsilon (default 1e-9) */
    if (!epsilon) {
        eps_val = 1e-9;
    } else if (IS_IMMEDIATE_INT(epsilon)) {
        eps_val = (double)INT_IMM_VALUE(epsilon);
    } else if (!IS_IMMEDIATE(epsilon) && epsilon->tag == TAG_FLOAT) {
        eps_val = epsilon->f;
    } else if (!IS_IMMEDIATE(epsilon) && epsilon->tag == TAG_NOTHING) {
        eps_val = 1e-9;
    } else {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-near: epsilon must be a number");
        return mk_error("assertion failed");
    }

    double diff = fabs(exp_val - act_val);
    if (diff <= eps_val) {
        g_test_stats.assertions_passed++;
        return mk_bool(true);
    } else {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-near failed: expected %g, got %g (diff %g > epsilon %g)",
                        exp_val, act_val, diff, eps_val);
        return mk_error("assertion failed");
    }
}

/*@semantic
id: function::prim_assert_throws
kind: function
name: prim_assert_throws
summary: Assert that evaluating a thunk throws an error.
tags:
  - testing
  - assertions
  - exceptions
*/
Obj* prim_assert_throws(Obj* thunk) {
    g_test_stats.assertions++;

    if (!thunk || IS_IMMEDIATE(thunk) || thunk->tag != TAG_CLOSURE) {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-throws: argument must be a function");
        return mk_error("assertion failed");
    }

    /* Call the thunk and check if it returns an error */
    Obj* result = prim_apply(thunk, NULL);

    bool threw = false;
    if (!result) {
        threw = true;
    } else if (!IS_IMMEDIATE(result) && result->tag == TAG_ERROR) {
        threw = true;
    }

    if (threw) {
        g_test_stats.assertions_passed++;
        return mk_bool(true);
    } else {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-throws failed: expected error, got %s",
                        obj_to_string_safe(result));
        return mk_error("assertion failed");
    }
}

/*@semantic
id: function::prim_assert_not_eq
kind: function
name: prim_assert_not_eq
summary: Assert that two values are not equal.
tags:
  - testing
  - assertions
*/
Obj* prim_assert_not_eq(Obj* val1, Obj* val2) {
    g_test_stats.assertions++;

    Obj* result = prim_eq(val1, val2);
    bool equal = (result == OMNI_TRUE);

    if (!equal) {
        g_test_stats.assertions_passed++;
        return mk_bool(true);
    } else {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-not-eq failed: both values are %s",
                        obj_to_string_safe(val1));
        return mk_error("assertion failed");
    }
}

/*@semantic
id: function::prim_assert_nil
kind: function
name: prim_assert_nil
summary: Assert that a value is nil/nothing.
tags:
  - testing
  - assertions
*/
Obj* prim_assert_nil(Obj* value) {
    g_test_stats.assertions++;

    bool is_nil = (!value) ||
                  (!IS_IMMEDIATE(value) && value->tag == TAG_NOTHING);

    if (is_nil) {
        g_test_stats.assertions_passed++;
        return mk_bool(true);
    } else {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-nil failed: got %s", obj_to_string_safe(value));
        return mk_error("assertion failed");
    }
}

/*@semantic
id: function::prim_assert_not_nil
kind: function
name: prim_assert_not_nil
summary: Assert that a value is not nil/nothing.
tags:
  - testing
  - assertions
*/
Obj* prim_assert_not_nil(Obj* value) {
    g_test_stats.assertions++;

    bool is_nil = (!value) ||
                  (!IS_IMMEDIATE(value) && value->tag == TAG_NOTHING);

    if (!is_nil) {
        g_test_stats.assertions_passed++;
        return mk_bool(true);
    } else {
        g_test_stats.assertions_failed++;
        mark_test_failed("assert-not-nil failed: value is nil");
        return mk_error("assertion failed");
    }
}

/* ============================================================================
 * Test Execution
 * ============================================================================ */

static bool run_single_test(TestCase* tc) {
    g_current_test = tc;
    tc->passed = true;
    tc->error_msg[0] = '\0';

    printf("  Running: %s ... ", tc->name);
    fflush(stdout);

    if (tc->test_closure) {
        Obj* result = prim_apply(tc->test_closure, NULL);

        if (result && !IS_IMMEDIATE(result) && result->tag == TAG_ERROR) {
            if (tc->passed) {
                tc->passed = false;
                snprintf(tc->error_msg, sizeof(tc->error_msg),
                        "Test threw error: %s",
                        result->ptr ? (char*)result->ptr : "unknown");
            }
        }
    } else if (tc->test_fn) {
        tc->test_fn();
    }

    g_current_test = NULL;

    if (tc->passed) {
        printf("PASS\n");
        g_test_stats.passed++;
    } else {
        printf("FAIL\n");
        if (tc->error_msg[0]) {
            printf("    Error: %s\n", tc->error_msg);
        }
        g_test_stats.failed++;
    }

    return tc->passed;
}

/*@semantic
id: function::prim_run_tests
kind: function
name: prim_run_tests
summary: Run all registered tests and return results.
tags:
  - testing
  - execution
*/
Obj* prim_run_tests(void) {
    reset_test_stats();

    printf("\n=== Running Tests ===\n\n");

    TestCase* tc = g_test_registry;
    while (tc) {
        g_test_stats.total++;
        run_single_test(tc);
        tc = tc->next;
    }

    printf("\n=== Test Summary ===\n");
    printf("Total:  %d\n", g_test_stats.total);
    printf("Passed: %d\n", g_test_stats.passed);
    printf("Failed: %d\n", g_test_stats.failed);
    if (g_test_stats.assertions > 0) {
        printf("Assertions: %d passed, %d failed\n",
               g_test_stats.assertions_passed, g_test_stats.assertions_failed);
    }
    printf("\n");

    Obj* result = mk_dict();
    dict_set(result, mk_keyword("total"), mk_int(g_test_stats.total));
    dict_set(result, mk_keyword("passed"), mk_int(g_test_stats.passed));
    dict_set(result, mk_keyword("failed"), mk_int(g_test_stats.failed));
    dict_set(result, mk_keyword("assertions"), mk_int(g_test_stats.assertions));
    dict_set(result, mk_keyword("success"), mk_bool(g_test_stats.failed == 0));

    return result;
}

/*@semantic
id: function::prim_run_tests_matching
kind: function
name: prim_run_tests_matching
summary: Run tests matching a pattern and return results.
tags:
  - testing
  - execution
  - filtering
*/
Obj* prim_run_tests_matching(Obj* pattern_obj) {
    if (!pattern_obj || IS_IMMEDIATE(pattern_obj) ||
        pattern_obj->tag != TAG_STRING) {
        return mk_error("run-tests-matching: pattern must be a string");
    }

    const char* pattern = (const char*)pattern_obj->ptr;
    reset_test_stats();

    printf("\n=== Running Tests Matching '%s' ===\n\n", pattern);

    TestCase* tc = g_test_registry;
    int matched = 0;
    while (tc) {
        if (pattern_match(pattern, tc->name)) {
            matched++;
            g_test_stats.total++;
            run_single_test(tc);
        }
        tc = tc->next;
    }

    if (matched == 0) {
        printf("No tests matched pattern '%s'\n", pattern);
    }

    printf("\n=== Test Summary ===\n");
    printf("Matched: %d\n", matched);
    printf("Passed:  %d\n", g_test_stats.passed);
    printf("Failed:  %d\n", g_test_stats.failed);
    printf("\n");

    Obj* result = mk_dict();
    dict_set(result, mk_keyword("matched"), mk_int(matched));
    dict_set(result, mk_keyword("passed"), mk_int(g_test_stats.passed));
    dict_set(result, mk_keyword("failed"), mk_int(g_test_stats.failed));
    dict_set(result, mk_keyword("success"), mk_bool(g_test_stats.failed == 0));

    return result;
}

/*@semantic
id: function::prim_test_report
kind: function
name: prim_test_report
summary: Generate detailed test report as a dict.
tags:
  - testing
  - reporting
*/
Obj* prim_test_report(void) {
    Obj* report = mk_dict();

    dict_set(report, mk_keyword("registered"), mk_int(g_test_count));
    dict_set(report, mk_keyword("last-run-total"), mk_int(g_test_stats.total));
    dict_set(report, mk_keyword("last-run-passed"), mk_int(g_test_stats.passed));
    dict_set(report, mk_keyword("last-run-failed"), mk_int(g_test_stats.failed));
    dict_set(report, mk_keyword("assertions-total"), mk_int(g_test_stats.assertions));
    dict_set(report, mk_keyword("assertions-passed"), mk_int(g_test_stats.assertions_passed));
    dict_set(report, mk_keyword("assertions-failed"), mk_int(g_test_stats.assertions_failed));

    /* List of test names */
    Obj* names = mk_array(g_test_count > 0 ? g_test_count : 1);
    TestCase* tc = g_test_registry;
    int i = 0;
    while (tc && i < g_test_count) {
        array_push(names, mk_string(tc->name));
        tc = tc->next;
        i++;
    }
    dict_set(report, mk_keyword("test-names"), names);

    /* List of failed tests with errors */
    Obj* failures = mk_array(g_test_stats.failed > 0 ? g_test_stats.failed : 1);
    tc = g_test_registry;
    while (tc) {
        if (!tc->passed && tc->error_msg[0]) {
            Obj* failure = mk_dict();
            dict_set(failure, mk_keyword("name"), mk_string(tc->name));
            dict_set(failure, mk_keyword("error"), mk_string(tc->error_msg));
            array_push(failures, failure);
        }
        tc = tc->next;
    }
    dict_set(report, mk_keyword("failures"), failures);

    return report;
}

/*@semantic
id: function::prim_list_tests
kind: function
name: prim_list_tests
summary: List all registered test names.
tags:
  - testing
  - introspection
*/
Obj* prim_list_tests(void) {
    Obj* names = mk_array(g_test_count > 0 ? g_test_count : 1);
    TestCase* tc = g_test_registry;
    while (tc) {
        array_push(names, mk_string(tc->name));
        tc = tc->next;
    }
    return names;
}

/*@semantic
id: function::prim_test_count
kind: function
name: prim_test_count
summary: Return the number of registered tests.
tags:
  - testing
  - introspection
*/
Obj* prim_test_count(void) {
    return mk_int(g_test_count);
}
