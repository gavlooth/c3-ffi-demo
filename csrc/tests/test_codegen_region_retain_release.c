/*
 * test_codegen_region_retain_release.c - Integration test for Issue 1 P2 retain/release codegen
 *
 * Verifies that full compiler generates correct retain/release patterns
 * based on RETAIN_REGION strategy selection.
 *
 * Strategy selection is controlled by OMNILISP_REPAIR_STRATEGY environment variable:
 * - "transmigrate": Default, copies values to target region
 * - "retain": Keeps values in source region, increments RC
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../compiler/compiler.h"

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

/* ========== Test Helpers ========== */

static Compiler* g_compiler = NULL;

static void compiler_setup(void) {
    CompilerOptions opts = {
        .runtime_path = "../runtime"
    };
    g_compiler = omni_compiler_new_with_options(&opts);
    ASSERT(g_compiler != NULL);
}

static void compiler_teardown(void) {
    if (g_compiler) {
        omni_compiler_free(g_compiler);
        g_compiler = NULL;
    }
}

/* ========== Full Compiler Integration Tests ========== */

/*
 * Test 1: TRANSMIGRATE strategy (default) emits transmigrate
 *
 * This tests the default behavior to ensure it doesn't regress.
 */
TEST(test_transmigrate_strategy_emits_transmigrate) {
    /*
     * Test program: simple return
     *   (define (test-transmigrate x)
     *     x)
     *
     * Expected generated C (default TRANSMIGRATE strategy):
     *   - transmigrate(x, _local_region, _caller_region); at return
     *   - No region_retain_internal() call
     */

    const char* source = "(define (test-transmigrate x) x)";

    /* Ensure no env var is set for this test */
    unsetenv("OMNILISP_REPAIR_STRATEGY");

    /* Compile to C */
    char* output = omni_compiler_compile_to_c(g_compiler, source);
    ASSERT(output != NULL);

    /* Check for transmigrate in generated code */
    ASSERT(strstr(output, "transmigrate(") != NULL);

    /* Check that retain_internal is NOT used */
    ASSERT(strstr(output, "region_retain_internal(") == NULL);

    free(output);
}

/*
 * Test 2: RETAIN_REGION strategy emits retain_internal
 *
 * This tests the RETAIN_REGION strategy (activated via env var).
 */
TEST(test_retain_region_emits_retain_at_return) {
    /*
     * Test program: simple return
     *   (define (test-retain x)
     *     x)
     *
     * Expected generated C (RETAIN_REGION strategy):
     *   - region_retain_internal(_local_region); at return
     *   - No transmigrate() call
     */

    const char* source = "(define (test-retain x) x)";

    /* Set strategy to RETAIN_REGION via env var */
    setenv("OMNILISP_REPAIR_STRATEGY", "retain", 1);

    /* Recompile to force strategy change */
    char* output = omni_compiler_compile_to_c(g_compiler, source);
    ASSERT(output != NULL);

    /* Check for retain_internal in generated code */
    ASSERT(strstr(output, "region_retain_internal(_local_region)") != NULL);

    /* Check that transmigrate is NOT used */
    ASSERT(strstr(output, "transmigrate(") == NULL);

    free(output);

    /* Reset to default strategy */
    unsetenv("OMNILISP_REPAIR_STRATEGY");
}

/*
 * Test 3: Region exit with retain strategy
 *
 * Tests that region_exit is properly emitted alongside retain.
 */
TEST(test_region_exit_emitted_with_retain) {
    /*
     * Test program: region with value returned
     *   (define (test-region-exit)
     *     (let ((region (region-create))
     *           (x (pair 1 2)))
     *       x))
     *
     * Expected generated C (RETAIN_REGION strategy):
     *   - region_create() call
     *   - region_retain_internal() call
     *   - region_exit() call at scope end
     */

    const char* source =
        "(define (test-region-exit)"
        "  (let ((region (region-create))"
        "        (x (pair 1 2)))"
        "    x))";

    /* Set strategy to RETAIN_REGION */
    setenv("OMNILISP_REPAIR_STRATEGY", "retain", 1);

    char* output = omni_compiler_compile_to_c(g_compiler, source);
    ASSERT(output != NULL);

    /* Check for region_create */
    ASSERT(strstr(output, "region_create()") != NULL);

    /* Check for region_retain_internal */
    ASSERT(strstr(output, "region_retain_internal(") != NULL);

    /* Check for region_exit */
    ASSERT(strstr(output, "region_exit(") != NULL);

    free(output);

    /* Reset strategy */
    unsetenv("OMNILISP_REPAIR_STRATEGY");
}

/*
 * Test 4: Strategy switching works correctly
 *
 * Tests that switching between TRANSMIGRATE and RETAIN_REGION
 * produces different output.
 */
TEST(test_strategy_switching_produces_different_output) {
    /*
     * Test program: simple function returning value
     *   (define (test-switch x)
     *     x)
     *
     * Expected: TRANSMIGRATE output differs from RETAIN_REGION output
     */

    const char* source = "(define (test-switch x) x)";

    /* Compile with TRANSMIGRATE (default) */
    unsetenv("OMNILISP_REPAIR_STRATEGY");
    char* transmigrate_output = omni_compiler_compile_to_c(g_compiler, source);
    ASSERT(transmigrate_output != NULL);

    /* Compile with RETAIN_REGION */
    setenv("OMNILISP_REPAIR_STRATEGY", "retain", 1);
    char* retain_output = omni_compiler_compile_to_c(g_compiler, source);
    ASSERT(retain_output != NULL);

    /* Outputs should differ */
    int outputs_differ = strcmp(transmigrate_output, retain_output) != 0;
    ASSERT(outputs_differ);

    /* TRANSMIGRATE output should contain "transmigrate" */
    ASSERT(strstr(transmigrate_output, "transmigrate(") != NULL);
    ASSERT(strstr(transmigrate_output, "region_retain_internal(") == NULL);

    /* RETAIN output should contain "region_retain_internal" */
    ASSERT(strstr(retain_output, "region_retain_internal(") != NULL);
    ASSERT(strstr(retain_output, "transmigrate(") == NULL);

    free(transmigrate_output);
    free(retain_output);
    unsetenv("OMNILISP_REPAIR_STRATEGY");
}

/* ========== Main Test Runner ========== */

int main(void) {
    printf("\n\033[33m=== Retain/Release Full Codegen Tests (Issue 1 P2) ===\033[0m\n");
    printf("\033[33m--- Default Codegen Behavior ---\033[0m\n");

    compiler_setup();

    RUN_TEST(test_transmigrate_strategy_emits_transmigrate);
    RUN_TEST(test_retain_region_emits_retain_at_return);
    RUN_TEST(test_region_exit_emitted_with_retain);
    RUN_TEST(test_strategy_switching_produces_different_output);

    compiler_teardown();

    printf("\n\033[33m=== Summary ===\033[0m\n");
    printf("  Total:  %d\n", tests_run);
    printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
    if (tests_passed < tests_run) {
        printf("  \033[31mFailed: %d\033[0m\n", tests_run - tests_passed);
    }

    return (tests_run == tests_passed) ? 0 : 1;
}
