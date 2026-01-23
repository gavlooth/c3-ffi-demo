/*
 * test_ast_collections.c - Tests for AST Collection Operations
 *
 * Verifies:
 * - Set creation and element addition (Issue 24: Set data structure)
 * - Array creation and population
 * - Dict creation
 * - Tuple creation
 * - Type literal creation
 * - User type creation
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ast/ast.h"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %s: ", #name); \
    name(); \
    tests_run++; \
    if (tests_failed == tests_failed_pre) { \
        tests_passed++; \
        printf("\033[32mPASS\033[0m\n"); \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\033[31mFAIL\033[0m (line %d: %s)\n", __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* Helper to create a symbol */
static OmniValue* mk_sym(const char* name) {
    return omni_new_sym(name);
}

/* Helper to create an integer */
static OmniValue* mk_int(int64_t i) {
    return omni_new_int(i);
}

/* ========== Set Operation Tests (Issue 24) ========== */

TEST(test_set_creation) {
    OmniValue* set = omni_new_set();

    ASSERT(set != NULL);
    ASSERT(set->tag == OMNI_SET);
    ASSERT(set->set.len == 0);
    ASSERT(set->set.cap >= 8);
}

TEST(test_set_add_single_element) {
    OmniValue* set = omni_new_set();
    OmniValue* elem = mk_int(42);

    omni_set_add(set, elem);

    ASSERT(set->set.len == 1);
}

TEST(test_set_add_multiple_elements) {
    OmniValue* set = omni_new_set();

    omni_set_add(set, mk_int(1));
    omni_set_add(set, mk_int(2));
    omni_set_add(set, mk_int(3));

    ASSERT(set->set.len == 3);
}

TEST(test_set_add_duplicate_prevents_insertion) {
    OmniValue* set = omni_new_set();
    OmniValue* elem = mk_int(42);

    /* Add same element twice */
    omni_set_add(set, elem);
    size_t len_before = set->set.len;
    omni_set_add(set, elem);
    size_t len_after = set->set.len;

    /* Length should not change on duplicate */
    ASSERT(len_before == len_after);
    ASSERT(set->set.len == 1);
}

TEST(test_set_add_multiple_duplicates) {
    OmniValue* set = omni_new_set();
    OmniValue* elem = mk_int(42);

    /* Add same element 5 times */
    for (int i = 0; i < 5; i++) {
        omni_set_add(set, elem);
    }

    /* Should only have one element */
    ASSERT(set->set.len == 1);
}

TEST(test_set_add_with_growth) {
    OmniValue* set = omni_new_set();

    /* Add enough elements to trigger growth */
    for (int i = 0; i < 20; i++) {
        omni_set_add(set, mk_int(i));
    }

    ASSERT(set->set.len == 20);
    ASSERT(set->set.cap >= 20);
}

TEST(test_set_null_input_safety) {
    OmniValue* set = omni_new_set();

    /* Should not crash with NULL set */
    omni_set_add(NULL, mk_int(1));

    /* Should not crash with NULL element */
    omni_set_add(set, NULL);

    /* Set should remain unchanged */
    ASSERT(set->set.len == 0);
}

TEST(test_set_wrong_type_safety) {
    OmniValue* set = omni_new_set();
    OmniValue* not_a_set = mk_int(42);

    /* Should not crash when trying to add to non-set */
    omni_set_add(not_a_set, mk_int(1));

    /* Non-set should be unchanged */
    ASSERT(not_a_set->tag == OMNI_INT);
}

TEST(test_set_different_element_types) {
    OmniValue* set = omni_new_set();

    /* Add different types of elements */
    omni_set_add(set, mk_int(1));
    omni_set_add(set, mk_sym("foo"));
    omni_set_add(set, mk_int(2));

    /* All elements should be added */
    ASSERT(set->set.len == 3);
}

/* ========== Array Operation Tests ========== */

TEST(test_array_creation) {
    OmniValue* arr = omni_new_array(10);

    ASSERT(arr != NULL);
    ASSERT(arr->tag == OMNI_ARRAY);
    ASSERT(arr->array.len == 0);
    ASSERT(arr->array.cap == 10);
}

TEST(test_array_creation_default_capacity) {
    OmniValue* arr = omni_new_array(0);

    ASSERT(arr != NULL);
    ASSERT(arr->array.cap >= 8);  /* Default minimum */
}

TEST(test_array_from_elements) {
    OmniValue* elements[3];
    elements[0] = mk_int(1);
    elements[1] = mk_int(2);
    elements[2] = mk_int(3);

    OmniValue* arr = omni_new_array_from(elements, 3);

    ASSERT(arr != NULL);
    ASSERT(arr->array.len == 3);
    ASSERT(arr->array.cap >= 3);
}

TEST(test_array_push_single_element) {
    /* Test pushing to empty array */
    OmniValue* arr = omni_new_array(10);

    omni_array_push(arr, mk_int(42));

    ASSERT(arr->array.len == 1);
    ASSERT(arr->array.cap == 10);  /* No growth needed */
}

TEST(test_array_push_multiple_elements) {
    /* Test multiple consecutive pushes */
    OmniValue* arr = omni_new_array(5);

    omni_array_push(arr, mk_int(1));
    omni_array_push(arr, mk_int(2));
    omni_array_push(arr, mk_int(3));

    ASSERT(arr->array.len == 3);
    ASSERT(arr->array.cap == 5);  /* No growth yet */
}

TEST(test_array_push_triggers_growth) {
    /* Test that array grows when capacity is exceeded */
    OmniValue* arr = omni_new_array(2);

    size_t initial_cap = arr->array.cap;

    /* Fill to capacity */
    omni_array_push(arr, mk_int(1));
    omni_array_push(arr, mk_int(2));

    ASSERT(arr->array.len == 2);
    ASSERT(arr->array.cap == initial_cap);

    /* This push should trigger growth */
    omni_array_push(arr, mk_int(3));

    ASSERT(arr->array.len == 3);
    ASSERT(arr->array.cap == initial_cap * 2);  /* Capacity doubled */
}

TEST(test_array_push_multiple_growth_cycles) {
    /* Test multiple growth cycles */
    OmniValue* arr = omni_new_array(2);

    /* First growth: 2 -> 4 */
    omni_array_push(arr, mk_int(1));
    omni_array_push(arr, mk_int(2));
    omni_array_push(arr, mk_int(3));

    ASSERT(arr->array.cap == 4);

    /* Fill to capacity again */
    omni_array_push(arr, mk_int(4));

    /* Second growth: 4 -> 8 */
    omni_array_push(arr, mk_int(5));

    ASSERT(arr->array.len == 5);
    ASSERT(arr->array.cap == 8);
}

TEST(test_array_push_different_types) {
    /* Test pushing different value types */
    OmniValue* arr = omni_new_array(10);

    omni_array_push(arr, mk_int(42));
    omni_array_push(arr, mk_sym("foo"));
    omni_array_push(arr, omni_new_string("hello"));

    ASSERT(arr->array.len == 3);
    ASSERT(arr->array.data[0]->tag == OMNI_INT);
    ASSERT(arr->array.data[1]->tag == OMNI_SYM);
    ASSERT(arr->array.data[2]->tag == OMNI_STRING);
}

TEST(test_array_push_null_array_safety) {
    /* Test safety with NULL array input */
    OmniValue* arr = omni_new_array(10);

    /* Should not crash */
    omni_array_push(NULL, mk_int(1));

    /* Original array should be unchanged */
    ASSERT(arr->array.len == 0);
}

TEST(test_array_push_null_element_safety) {
    /* Test safety with NULL element */
    OmniValue* arr = omni_new_array(10);

    /* Pushing NULL should be allowed (array can hold NULL values) */
    omni_array_push(arr, NULL);

    ASSERT(arr->array.len == 1);
    ASSERT(arr->array.data[0] == NULL);
}

TEST(test_array_push_wrong_type_safety) {
    /* Test safety when array parameter is wrong type */
    OmniValue* not_an_array = mk_int(42);
    size_t initial_len = not_an_array->array.len;  /* Will read garbage, but we only check it doesn't change */

    /* Should not crash */
    omni_array_push(not_an_array, mk_int(1));

    /* Not an array should still be an integer */
    ASSERT(not_an_array->tag == OMNI_INT);
}

TEST(test_array_push_preserves_existing_elements) {
    /* Test that growth preserves existing elements */
    OmniValue* arr = omni_new_array(2);

    omni_array_push(arr, mk_int(1));
    omni_array_push(arr, mk_int(2));

    /* Trigger growth */
    omni_array_push(arr, mk_int(3));

    /* First two elements should still be accessible */
    ASSERT(arr->array.data[0]->int_val == 1);
    ASSERT(arr->array.data[1]->int_val == 2);
    ASSERT(arr->array.data[2]->int_val == 3);
}

/* ========== Dict Operation Tests ========== */

TEST(test_dict_creation) {
    OmniValue* dict = omni_new_dict();

    ASSERT(dict != NULL);
    ASSERT(dict->tag == OMNI_DICT);
    ASSERT(dict->dict.len == 0);
    ASSERT(dict->dict.cap >= 8);
}

/* ========== Tuple Operation Tests ========== */

TEST(test_tuple_creation) {
    OmniValue* elements[3];
    elements[0] = mk_int(1);
    elements[1] = mk_int(2);
    elements[2] = mk_int(3);

    OmniValue* tuple = omni_new_tuple(elements, 3);

    ASSERT(tuple != NULL);
    ASSERT(tuple->tag == OMNI_TUPLE);
    ASSERT(tuple->tuple.len == 3);
}

/* ========== Type Literal Tests ========== */

TEST(test_type_literal_creation) {
    OmniValue* type_lit = omni_new_type_lit("Int", NULL, 0);

    ASSERT(type_lit != NULL);
    ASSERT(type_lit->tag == OMNI_TYPE_LIT);
    ASSERT(type_lit->type_lit.param_count == 0);
}

TEST(test_type_literal_with_params) {
    OmniValue* params[2];
    params[0] = mk_sym("T");
    params[1] = mk_sym("U");

    OmniValue* type_lit = omni_new_type_lit("Pair", params, 2);

    ASSERT(type_lit != NULL);
    ASSERT(type_lit->tag == OMNI_TYPE_LIT);
    ASSERT(type_lit->type_lit.param_count == 2);
}

/* ========== User Type Tests ========== */

TEST(test_user_type_creation) {
    OmniField fields[2];
    fields[0].name = (char*)"x";
    fields[0].value = mk_int(10);
    fields[1].name = (char*)"y";
    fields[1].value = mk_int(20);

    OmniValue* user_type = omni_new_user_type("Point", fields, 2);

    ASSERT(user_type != NULL);
    ASSERT(user_type->tag == OMNI_USER_TYPE);
    ASSERT(user_type->user_type.field_count == 2);
}

/* ========== Main ========== */

int main(void) {
    printf("\n=== AST Collection Operations Tests ===\n\n");

    printf("=== Set Operations (Issue 24) ===\n");
    int tests_failed_pre;
    tests_failed_pre = tests_failed; RUN_TEST(test_set_creation);
    tests_failed_pre = tests_failed; RUN_TEST(test_set_add_single_element);
    tests_failed_pre = tests_failed; RUN_TEST(test_set_add_multiple_elements);
    tests_failed_pre = tests_failed; RUN_TEST(test_set_add_duplicate_prevents_insertion);
    tests_failed_pre = tests_failed; RUN_TEST(test_set_add_multiple_duplicates);
    tests_failed_pre = tests_failed; RUN_TEST(test_set_add_with_growth);
    tests_failed_pre = tests_failed; RUN_TEST(test_set_null_input_safety);
    tests_failed_pre = tests_failed; RUN_TEST(test_set_wrong_type_safety);
    tests_failed_pre = tests_failed; RUN_TEST(test_set_different_element_types);

    printf("\n=== Array Operations ===\n");
    tests_failed_pre = tests_failed; RUN_TEST(test_array_creation);
    tests_failed_pre = tests_failed; RUN_TEST(test_array_creation_default_capacity);
    tests_failed_pre = tests_failed; RUN_TEST(test_array_from_elements);
    tests_failed_pre = tests_failed; RUN_TEST(test_array_push_single_element);
    tests_failed_pre = tests_failed; RUN_TEST(test_array_push_multiple_elements);
    tests_failed_pre = tests_failed; RUN_TEST(test_array_push_triggers_growth);
    tests_failed_pre = tests_failed; RUN_TEST(test_array_push_multiple_growth_cycles);
    tests_failed_pre = tests_failed; RUN_TEST(test_array_push_different_types);
    tests_failed_pre = tests_failed; RUN_TEST(test_array_push_null_array_safety);
    tests_failed_pre = tests_failed; RUN_TEST(test_array_push_null_element_safety);
    tests_failed_pre = tests_failed; RUN_TEST(test_array_push_wrong_type_safety);
    tests_failed_pre = tests_failed; RUN_TEST(test_array_push_preserves_existing_elements);

    printf("\n=== Dict Operations ===\n");
    tests_failed_pre = tests_failed; RUN_TEST(test_dict_creation);

    printf("\n=== Tuple Operations ===\n");
    tests_failed_pre = tests_failed; RUN_TEST(test_tuple_creation);

    printf("\n=== Type Literal Operations ===\n");
    tests_failed_pre = tests_failed; RUN_TEST(test_type_literal_creation);
    tests_failed_pre = tests_failed; RUN_TEST(test_type_literal_with_params);

    printf("\n=== User Type Operations ===\n");
    tests_failed_pre = tests_failed; RUN_TEST(test_user_type_creation);

    printf("\n=== Summary ===\n");
    printf("  Total:   %d\n", tests_run);
    printf("  Passed:  \033[32m%d\033[0m\n", tests_passed);
    if (tests_failed > 0) {
        printf("  Failed:  \033[31m%d\033[0m\n", tests_failed);
    } else {
        printf("  Failed:  %d\n", tests_failed);
    }
    printf("\n");

    return tests_failed > 0 ? 1 : 0;
}
