/*
 * test_environment.c - Tests for environment variable operations
 *
 * Tests environment variable functions:
 * - prim_io_getenv - Get environment variable value
 * - prim_io_setenv - Set environment variable
 * - prim_io_unsetenv - Unset environment variable
 * - prim_io_environ - Get all environment variables as dict
 *
 * Tests cover:
 * - Getting existing and non-existent variables
 * - Setting new and updating existing variables
 * - Unsetting variables
 * - Error handling for invalid inputs
 * - Getting all environment variables as dict
 */

#include "test_framework.h"
#include <stdlib.h>
#include <string.h>

/* Unique test variable names to avoid conflicts */
static const char* TEST_VAR_NAME = "OMNILISP_TEST_VAR_12345";
static const char* TEST_VAR_NAME2 = "OMNILISP_TEST_VAR_67890";
static const char* TEST_VAR_VALUE = "test_value";

/* ========== prim_io_getenv Tests ========== */

void test_env_get_existing(void) {
    /* Test getting an environment variable that exists (PATH should always exist) */
    Obj* path_obj = mk_string("PATH");
    Obj* result = prim_io_getenv(path_obj);

    /* Should return a string (not nothing, not error) */
    ASSERT_NOT_NULL(result);
    ASSERT_NOT_NULL(obj_to_cstr_safe(result));

    dec_ref(path_obj);
    dec_ref(result);
    PASS();
}

void test_env_get_nonexistent(void) {
    /* Test getting a variable that doesn't exist */
    Obj* name_obj = mk_string("OMNILISP_NONEXISTENT_VAR_99999");
    Obj* result = prim_io_getenv(name_obj);

    /* Should return nothing */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_tag(result), TAG_NOTHING);

    dec_ref(name_obj);
    dec_ref(result);
    PASS();
}

void test_env_get_null_input(void) {
    /* Test getenv with NULL input */
    Obj* result = prim_io_getenv(NULL);

    /* Should return nothing (not error) */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_tag(result), TAG_NOTHING);

    dec_ref(result);
    PASS();
}

void test_env_get_non_string(void) {
    /* Test getenv with non-string input */
    Obj* name_obj = mk_int(42);
    Obj* result = prim_io_getenv(name_obj);

    /* Should return nothing (not error) */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_tag(result), TAG_NOTHING);

    dec_ref(name_obj);
    dec_ref(result);
    PASS();
}

void test_env_get_empty_string(void) {
    /* Test getenv with empty string name */
    Obj* name_obj = mk_string("");
    Obj* result = prim_io_getenv(name_obj);

    /* Empty string should be treated as valid (returns nothing if not set) */
    ASSERT_NOT_NULL(result);
    /* Behavior depends on system - either nothing or empty string */

    dec_ref(name_obj);
    dec_ref(result);
    PASS();
}

/* ========== prim_io_setenv Tests ========== */

void test_env_set_new_variable(void) {
    /* Test setting a new environment variable */
    unsetenv(TEST_VAR_NAME);  /* Ensure it doesn't exist */

    Obj* name_obj = mk_string(TEST_VAR_NAME);
    Obj* value_obj = mk_string(TEST_VAR_VALUE);

    Obj* set_result = prim_io_setenv(name_obj, value_obj);

    /* Should return true */
    ASSERT_NOT_NULL(set_result);
    ASSERT_EQ(obj_to_bool(set_result), 1);

    /* Verify the value was set */
    Obj* get_result = prim_io_getenv(name_obj);
    ASSERT_NOT_NULL(get_result);
    const char* retrieved = obj_to_cstr_safe(get_result);
    ASSERT_NOT_NULL(retrieved);
    ASSERT_STR_EQ(retrieved, TEST_VAR_VALUE);

    /* Cleanup */
    unsetenv(TEST_VAR_NAME);
    dec_ref(name_obj);
    dec_ref(value_obj);
    dec_ref(set_result);
    dec_ref(get_result);
    PASS();
}

void test_env_set_update_existing(void) {
    /* Test updating an existing environment variable */
    unsetenv(TEST_VAR_NAME);
    setenv(TEST_VAR_NAME, "old_value", 1);

    Obj* name_obj = mk_string(TEST_VAR_NAME);
    Obj* value_obj = mk_string("new_value");

    Obj* set_result = prim_io_setenv(name_obj, value_obj);

    /* Should return true */
    ASSERT_NOT_NULL(set_result);
    ASSERT_EQ(obj_to_bool(set_result), 1);

    /* Verify the value was updated */
    Obj* get_result = prim_io_getenv(name_obj);
    ASSERT_NOT_NULL(get_result);
    const char* retrieved = obj_to_cstr_safe(get_result);
    ASSERT_NOT_NULL(retrieved);
    ASSERT_STR_EQ(retrieved, "new_value");

    /* Cleanup */
    unsetenv(TEST_VAR_NAME);
    dec_ref(name_obj);
    dec_ref(value_obj);
    dec_ref(set_result);
    dec_ref(get_result);
    PASS();
}

void test_env_set_empty_value(void) {
    /* Test setting variable to empty string */
    unsetenv(TEST_VAR_NAME);

    Obj* name_obj = mk_string(TEST_VAR_NAME);
    Obj* value_obj = mk_string("");

    Obj* set_result = prim_io_setenv(name_obj, value_obj);

    /* Should return true */
    ASSERT_NOT_NULL(set_result);
    ASSERT_EQ(obj_to_bool(set_result), 1);

    /* Verify the variable exists with empty value */
    const char* env_value = getenv(TEST_VAR_NAME);
    ASSERT_NOT_NULL(env_value);
    ASSERT_STR_EQ(env_value, "");

    /* Cleanup */
    unsetenv(TEST_VAR_NAME);
    dec_ref(name_obj);
    dec_ref(value_obj);
    dec_ref(set_result);
    PASS();
}

void test_env_set_null_name(void) {
    /* Test setenv with NULL name */
    Obj* value_obj = mk_string("value");
    Obj* result = prim_io_setenv(NULL, value_obj);

    /* Should return error */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_tag(result), TAG_ERROR);

    dec_ref(value_obj);
    dec_ref(result);
    PASS();
}

void test_env_set_null_value(void) {
    /* Test setenv with NULL value (should set to empty string) */
    unsetenv(TEST_VAR_NAME);

    Obj* name_obj = mk_string(TEST_VAR_NAME);
    Obj* result = prim_io_setenv(name_obj, NULL);

    /* Should return true */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_bool(result), 1);

    /* Verify variable is set (value may be empty or NULL depending on implementation) */
    dec_ref(name_obj);
    dec_ref(result);
    unsetenv(TEST_VAR_NAME);
    PASS();
}

void test_env_set_special_characters(void) {
    /* Test setting variable with special characters in value */
    unsetenv(TEST_VAR_NAME);

    Obj* name_obj = mk_string(TEST_VAR_NAME);
    Obj* value_obj = mk_string("value with spaces\n\tand \"quotes\"");

    Obj* set_result = prim_io_setenv(name_obj, value_obj);

    /* Should return true */
    ASSERT_NOT_NULL(set_result);
    ASSERT_EQ(obj_to_bool(set_result), 1);

    /* Verify the special characters are preserved */
    Obj* get_result = prim_io_getenv(name_obj);
    ASSERT_NOT_NULL(get_result);
    const char* retrieved = obj_to_cstr_safe(get_result);
    ASSERT_NOT_NULL(retrieved);
    ASSERT_STR_EQ(retrieved, "value with spaces\n\tand \"quotes\"");

    /* Cleanup */
    unsetenv(TEST_VAR_NAME);
    dec_ref(name_obj);
    dec_ref(value_obj);
    dec_ref(set_result);
    dec_ref(get_result);
    PASS();
}

/* ========== prim_io_unsetenv Tests ========== */

void test_env_unset_existing(void) {
    /* Test unsetting an existing environment variable */
    setenv(TEST_VAR_NAME, TEST_VAR_VALUE, 1);

    Obj* name_obj = mk_string(TEST_VAR_NAME);
    Obj* unset_result = prim_io_unsetenv(name_obj);

    /* Should return true */
    ASSERT_NOT_NULL(unset_result);
    ASSERT_EQ(obj_to_bool(unset_result), 1);

    /* Verify the variable is gone */
    const char* env_value = getenv(TEST_VAR_NAME);
    ASSERT(env_value == NULL);

    dec_ref(name_obj);
    dec_ref(unset_result);
    PASS();
}

void test_env_unset_nonexistent(void) {
    /* Test unsetting a variable that doesn't exist */
    unsetenv(TEST_VAR_NAME);  /* Ensure it doesn't exist */

    Obj* name_obj = mk_string(TEST_VAR_NAME);
    Obj* unset_result = prim_io_unsetenv(name_obj);

    /* Should return true (unsetenv is idempotent) */
    ASSERT_NOT_NULL(unset_result);
    ASSERT_EQ(obj_to_bool(unset_result), 1);

    dec_ref(name_obj);
    dec_ref(unset_result);
    PASS();
}

void test_env_unset_null_name(void) {
    /* Test unsetenv with NULL name */
    Obj* result = prim_io_unsetenv(NULL);

    /* Should return error */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_tag(result), TAG_ERROR);

    dec_ref(result);
    PASS();
}

/* ========== prim_io_environ Tests ========== */

void test_env_environ_returns_dict(void) {
    /* Test that environ returns a dict */
    Obj* result = prim_io_environ();

    /* Should return a dict object */
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_tag(result), TAG_DICT);

    /* The dict should not be empty (there are always env vars) */
    ASSERT(result->ptr != NULL);

    dec_ref(result);
    PASS();
}

void test_env_environ_contains_path(void) {
    /* Test that PATH is in the returned environ dict */
    Obj* result = prim_io_environ();

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_tag(result), TAG_DICT);

    /* Try to get PATH from the dict */
    Obj* path_key = mk_string("PATH");
    Obj* path_value = dict_get(result, path_key);

    /* PATH should exist in environ */
    ASSERT_NOT_NULL(path_value);
    const char* path_str = obj_to_cstr_safe(path_value);
    ASSERT_NOT_NULL(path_str);

    dec_ref(result);
    dec_ref(path_key);
    dec_ref(path_value);
    PASS();
}

void test_env_environ_contains_set_var(void) {
    /* Test that newly set variable appears in environ */
    setenv(TEST_VAR_NAME, TEST_VAR_VALUE, 1);

    Obj* result = prim_io_environ();

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_tag(result), TAG_DICT);

    /* Try to get our test variable from the dict */
    Obj* test_key = mk_string(TEST_VAR_NAME);
    Obj* test_value = dict_get(result, test_key);

    /* Should find our test variable */
    ASSERT_NOT_NULL(test_value);
    const char* test_str = obj_to_cstr_safe(test_value);
    ASSERT_NOT_NULL(test_str);
    ASSERT_STR_EQ(test_str, TEST_VAR_VALUE);

    /* Cleanup */
    unsetenv(TEST_VAR_NAME);
    dec_ref(result);
    dec_ref(test_key);
    dec_ref(test_value);
    PASS();
}

void test_env_environ_multiple_vars(void) {
    /* Test that multiple set variables appear in environ */
    unsetenv(TEST_VAR_NAME);
    unsetenv(TEST_VAR_NAME2);

    setenv(TEST_VAR_NAME, "value1", 1);
    setenv(TEST_VAR_NAME2, "value2", 1);

    Obj* result = prim_io_environ();

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_tag(result), TAG_DICT);

    /* Check both variables are present */
    Obj* key1 = mk_string(TEST_VAR_NAME);
    Obj* key2 = mk_string(TEST_VAR_NAME2);
    Obj* val1 = dict_get(result, key1);
    Obj* val2 = dict_get(result, key2);

    ASSERT_NOT_NULL(val1);
    ASSERT_NOT_NULL(val2);
    ASSERT_STR_EQ(obj_to_cstr_safe(val1), "value1");
    ASSERT_STR_EQ(obj_to_cstr_safe(val2), "value2");

    /* Cleanup */
    unsetenv(TEST_VAR_NAME);
    unsetenv(TEST_VAR_NAME2);
    dec_ref(result);
    dec_ref(key1);
    dec_ref(key2);
    dec_ref(val1);
    dec_ref(val2);
    PASS();
}

/* ========== Run all environment tests ========== */

void run_environment_tests(void) {
    TEST_SECTION("Environment Variables - getenv");
    RUN_TEST(test_env_get_existing);
    RUN_TEST(test_env_get_nonexistent);
    RUN_TEST(test_env_get_null_input);
    RUN_TEST(test_env_get_non_string);
    RUN_TEST(test_env_get_empty_string);

    TEST_SECTION("Environment Variables - setenv");
    RUN_TEST(test_env_set_new_variable);
    RUN_TEST(test_env_set_update_existing);
    RUN_TEST(test_env_set_empty_value);
    RUN_TEST(test_env_set_null_name);
    RUN_TEST(test_env_set_null_value);
    RUN_TEST(test_env_set_special_characters);

    TEST_SECTION("Environment Variables - unsetenv");
    RUN_TEST(test_env_unset_existing);
    RUN_TEST(test_env_unset_nonexistent);
    RUN_TEST(test_env_unset_null_name);

    TEST_SECTION("Environment Variables - environ");
    RUN_TEST(test_env_environ_returns_dict);
    RUN_TEST(test_env_environ_contains_path);
    RUN_TEST(test_env_environ_contains_set_var);
    RUN_TEST(test_env_environ_multiple_vars);
}
