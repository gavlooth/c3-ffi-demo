/*
 * test_condition.c - Tests for the Condition System
 *
 * Verifies:
 * - Condition type registration and lookup
 * - Condition creation and slot access
 * - Condition rendering
 * - Type hierarchy (subtype checking)
 */

#include "../src/condition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Testing %s...", #name); \
    fflush(stdout); \
    test_##name(); \
    printf(" PASSED\n"); \
    tests_passed++; \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf(" FAILED\n"); \
        printf("    Assertion failed: %s\n", #cond); \
        printf("    at %s:%d\n", __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf(" FAILED\n"); \
        printf("    Expected: %s\n", (b)); \
        printf("    Got: %s\n", (a)); \
        printf("    at %s:%d\n", __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================
 * Tests
 * ============================================================ */

TEST(condition_init) {
    /* Init should register all base types */
    condition_init();

    ASSERT(COND_CONDITION != NULL);
    ASSERT(COND_ERROR != NULL);
    ASSERT(COND_TYPE_ERROR != NULL);
    ASSERT(COND_WARNING != NULL);
    ASSERT(COND_MEMORY_ERROR != NULL);
    ASSERT(COND_FFI_ERROR != NULL);
}

TEST(type_lookup) {
    condition_init();

    ConditionType* err = condition_type_lookup(":error");
    ASSERT(err != NULL);
    ASSERT(err == COND_ERROR);

    ConditionType* type_err = condition_type_lookup(":type-error");
    ASSERT(type_err != NULL);
    ASSERT(type_err == COND_TYPE_ERROR);

    ConditionType* nonexistent = condition_type_lookup(":nonexistent");
    ASSERT(nonexistent == NULL);
}

TEST(type_hierarchy) {
    condition_init();

    /* :type-error is a subtype of :error */
    ASSERT(condition_type_is_subtype(COND_TYPE_ERROR, COND_ERROR));

    /* :error is a subtype of :condition */
    ASSERT(condition_type_is_subtype(COND_ERROR, COND_CONDITION));

    /* :type-error is a subtype of :condition (transitive) */
    ASSERT(condition_type_is_subtype(COND_TYPE_ERROR, COND_CONDITION));

    /* :warning is NOT a subtype of :error */
    ASSERT(!condition_type_is_subtype(COND_WARNING, COND_ERROR));

    /* :division-by-zero -> :arithmetic-error -> :error -> :condition */
    ASSERT(condition_type_is_subtype(COND_DIVISION_BY_ZERO, COND_ARITHMETIC_ERROR));
    ASSERT(condition_type_is_subtype(COND_DIVISION_BY_ZERO, COND_ERROR));
    ASSERT(condition_type_is_subtype(COND_DIVISION_BY_ZERO, COND_CONDITION));
}

TEST(condition_create) {
    condition_init();

    Condition* cond = condition_create(COND_ERROR);
    ASSERT(cond != NULL);
    ASSERT(cond->type == COND_ERROR);
    ASSERT(cond->slots == NULL);

    condition_free(cond);
}

TEST(condition_with_message) {
    condition_init();

    Condition* cond = condition_create_with_message(COND_ERROR, "Something went wrong");
    ASSERT(cond != NULL);

    const char* msg = condition_get_message(cond);
    ASSERT(msg != NULL);
    ASSERT_STR_EQ(msg, "Something went wrong");

    condition_free(cond);
}

TEST(condition_slots) {
    condition_init();

    Condition* cond = condition_create(COND_TYPE_ERROR);
    ASSERT(cond != NULL);

    condition_set_slot_string(cond, "expected", "integer");
    condition_set_slot_string(cond, "got", "string");

    int is_string = 0;
    const char* expected = (const char*)condition_get_slot(cond, "expected", &is_string);
    ASSERT(expected != NULL);
    ASSERT(is_string == 1);
    ASSERT_STR_EQ(expected, "integer");

    const char* got = (const char*)condition_get_slot(cond, "got", &is_string);
    ASSERT(got != NULL);
    ASSERT_STR_EQ(got, "string");

    condition_free(cond);
}

TEST(condition_location) {
    condition_init();

    Condition* cond = condition_create_with_message(COND_ERROR, "Test error");
    condition_set_location(cond, "test.omni", 42, 10);

    ASSERT(cond->source_file != NULL);
    ASSERT_STR_EQ(cond->source_file, "test.omni");
    ASSERT(cond->source_line == 42);
    ASSERT(cond->source_col == 10);

    condition_free(cond);
}

TEST(condition_to_string) {
    condition_init();

    Condition* cond = condition_create_with_message(COND_ERROR, "Test error");
    char* str = condition_to_string(cond);

    ASSERT(str != NULL);
    ASSERT(strstr(str, ":error") != NULL);
    ASSERT(strstr(str, "Test error") != NULL);

    free(str);
    condition_free(cond);
}

TEST(condition_to_string_with_location) {
    condition_init();

    Condition* cond = condition_create_with_message(COND_ERROR, "Test error");
    condition_set_location(cond, "test.omni", 42, 10);

    char* str = condition_to_string(cond);

    ASSERT(str != NULL);
    ASSERT(strstr(str, "test.omni") != NULL);
    ASSERT(strstr(str, "42") != NULL);

    free(str);
    condition_free(cond);
}

TEST(make_error) {
    condition_init();

    Condition* cond = make_error("Something bad happened");
    ASSERT(cond != NULL);
    ASSERT(cond->type == COND_ERROR);
    ASSERT_STR_EQ(condition_get_message(cond), "Something bad happened");

    condition_free(cond);
}

TEST(make_type_error) {
    condition_init();

    Condition* cond = make_type_error("integer", "string", NULL);
    ASSERT(cond != NULL);
    ASSERT(cond->type == COND_TYPE_ERROR);

    int is_string = 0;
    const char* expected = (const char*)condition_get_slot(cond, "expected", &is_string);
    ASSERT_STR_EQ(expected, "integer");

    const char* got = (const char*)condition_get_slot(cond, "got", &is_string);
    ASSERT_STR_EQ(got, "string");

    condition_free(cond);
}

TEST(make_unbound_variable) {
    condition_init();

    Condition* cond = make_unbound_variable("foo");
    ASSERT(cond != NULL);
    ASSERT(cond->type == COND_UNBOUND_VARIABLE);

    const char* msg = condition_get_message(cond);
    ASSERT(strstr(msg, "foo") != NULL);

    condition_free(cond);
}

TEST(make_division_by_zero) {
    condition_init();

    Condition* cond = make_division_by_zero();
    ASSERT(cond != NULL);
    ASSERT(cond->type == COND_DIVISION_BY_ZERO);
    ASSERT(condition_type_is_subtype(cond->type, COND_ARITHMETIC_ERROR));

    condition_free(cond);
}

TEST(condition_cause_chain) {
    condition_init();

    Condition* cause = make_error("Root cause");
    Condition* cond = make_error("Wrapper error");
    condition_set_cause(cond, cause);

    ASSERT(cond->cause == cause);
    ASSERT_STR_EQ(condition_get_message(cond->cause), "Root cause");

    /* Print full to verify cause chain rendering */
    printf("\n    [Cause chain output]:\n    ");
    condition_print_full(cond);

    condition_free(cond);
    condition_free(cause);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("=== Condition System Tests ===\n\n");

    RUN_TEST(condition_init);
    RUN_TEST(type_lookup);
    RUN_TEST(type_hierarchy);
    RUN_TEST(condition_create);
    RUN_TEST(condition_with_message);
    RUN_TEST(condition_slots);
    RUN_TEST(condition_location);
    RUN_TEST(condition_to_string);
    RUN_TEST(condition_to_string_with_location);
    RUN_TEST(make_error);
    RUN_TEST(make_type_error);
    RUN_TEST(make_unbound_variable);
    RUN_TEST(make_division_by_zero);
    RUN_TEST(condition_cause_chain);

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
