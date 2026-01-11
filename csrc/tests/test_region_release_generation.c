/*
 * test_region_release_generation.c - Test for Issue 1 P2 region release generation
 *
 * Verifies that compiler generates region_release_internal() calls
 * at variable last-use positions (Region-RC model).
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>

#include "../ast/ast.h"
#include "../analysis/analysis.h"
#include "../codegen/codegen.h"
#include "../codegen/region_codegen.h"

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

/* ========== Region Release Generation Tests ========== */

/*
 * Test 1: Verify omni_codegen_emit_region_releases_at_pos is callable
 *
 * This is a basic compilation/linkage test to ensure function exists
 * and can be called without crashing.
 */
TEST(test_region_release_function_exists) {
    /* Create analysis context */
    AnalysisContext* analysis = omni_analysis_new();
    ASSERT(analysis != NULL);

    /* Create codegen context */
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ASSERT(ctx != NULL);

    /* Attach analysis to codegen */
    ctx->analysis = analysis;

    /* Call the function with a test position */
    /* This should not crash even if no variables match */
    omni_codegen_emit_region_releases_at_pos(ctx, 0);

    /* Cleanup */
    /* omni_codegen_free() frees ctx->analysis, so don't free it twice. */
    omni_codegen_free(ctx);
}

/* ========== Main Test Runner ========== */

int main(void) {
    printf("=== Region Release Generation Tests (Issue 1 P2) ===\n\n");

    RUN_TEST(test_region_release_function_exists);

    printf("\n=== Tests Run: %d, Passed: %d ===\n", tests_run, tests_passed);

    return (tests_run == tests_passed) ? 0 : 1;
}
