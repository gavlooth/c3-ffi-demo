/*
 * Additional tests for Condition System
 *
 * Tests added for functions not covered in test_condition.c:
 * - make_memory_error
 * - make_undefined_function
 * - make_ffi_error
 *
 * These tests fill gaps in condition system coverage.
 */

#include "../src/condition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

TEST(make_memory_error) {
    condition_init();

    /* Create memory error with address */
    const char* test_message = "Use after free detected";
    void* test_address = (void*)0xDEADBEEF;
    
    Condition* cond = make_memory_error(test_message, test_address);
    
    ASSERT(cond != NULL);
    ASSERT(cond->type == COND_MEMORY_ERROR);
    
    /* Verify message slot */
    const char* msg = condition_get_message(cond);
    ASSERT_STR_EQ(msg, test_message);
    
    /* Verify address slot (value type, not string) */
    int is_string = 0;
    void* addr = condition_get_slot(cond, "address", &is_string);
    ASSERT(addr == test_address);
    ASSERT(is_string == 0);  /* address should be a value, not string */
    
    condition_free(cond);
}

TEST(make_undefined_function) {
    condition_init();

    /* Create undefined function error */
    const char* func_name = "nonexistent_func";
    Condition* cond = make_undefined_function(func_name);
    
    ASSERT(cond != NULL);
    ASSERT(cond->type == COND_UNDEFINED_FUNCTION);
    
    /* Verify message contains function name */
    const char* msg = condition_get_message(cond);
    ASSERT(msg != NULL);
    ASSERT(strstr(msg, "Undefined function") != NULL);
    ASSERT(strstr(msg, func_name) != NULL);
    
    /* Verify name slot */
    int is_string = 0;
    void* name_slot = condition_get_slot(cond, "name", &is_string);
    ASSERT(name_slot != NULL);
    ASSERT(is_string == 1);
    ASSERT_STR_EQ((const char*)name_slot, func_name);
    
    condition_free(cond);
}

TEST(make_ffi_error) {
    condition_init();

    /* Create FFI error with function name */
    const char* err_message = "Symbol not found";
    const char* func_name = "dlsym_test";
    
    Condition* cond = make_ffi_error(err_message, func_name);
    
    ASSERT(cond != NULL);
    ASSERT(cond->type == COND_FFI_ERROR);
    
    /* Verify message slot */
    const char* msg = condition_get_message(cond);
    ASSERT_STR_EQ(msg, err_message);
    
    /* Verify function slot */
    int is_string = 0;
    void* func_slot = condition_get_slot(cond, "function", &is_string);
    ASSERT(func_slot != NULL);
    ASSERT(is_string == 1);
    ASSERT_STR_EQ((const char*)func_slot, func_name);
    
    condition_free(cond);
}

TEST(make_ffi_error_null_function) {
    condition_init();

    /* Create FFI error without function name (should handle NULL) */
    const char* err_message = "Library not found";
    
    Condition* cond = make_ffi_error(err_message, NULL);
    
    ASSERT(cond != NULL);
    ASSERT(cond->type == COND_FFI_ERROR);
    
    /* Verify message slot */
    const char* msg = condition_get_message(cond);
    ASSERT_STR_EQ(msg, err_message);
    
    /* Function slot should be NULL */
    int is_string = 0;
    void* func_slot = condition_get_slot(cond, "function", &is_string);
    ASSERT(func_slot == NULL);
    
    condition_free(cond);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("=== Condition System Additional Tests ===\n\n");

    RUN_TEST(make_memory_error);
    RUN_TEST(make_undefined_function);
    RUN_TEST(make_ffi_error);
    RUN_TEST(make_ffi_error_null_function);

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
