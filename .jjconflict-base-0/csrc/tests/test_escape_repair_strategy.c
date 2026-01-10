/*
 * test_escape_repair_strategy.c - Test for Issue 1 P2 escape repair strategy enum
 *
 * Verifies that EscapeRepairStrategy enum is defined and accessible.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
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
    printf("PASS\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL (line %d): %s\n", __LINE__, #cond); \
        tests_run++; \
        return; \
    } \
} while(0)

/* ========== Escape Repair Strategy Tests ========== */

TEST(test_transmigrate_constant_defined) {
    /* Verify enum value is defined */
    int transmigrate_val = ESCAPE_REPAIR_TRANSMIGRATE;
    ASSERT(transmigrate_val == 0);
}

TEST(test_retain_constant_defined) {
    /* Verify enum value is defined */
    int retain_val = ESCAPE_REPAIR_RETAIN_REGION;
    ASSERT(retain_val == 1);
}

TEST(test_enum_values_distinct) {
    /* Verify enum values are distinct */
    ASSERT(ESCAPE_REPAIR_TRANSMIGRATE != ESCAPE_REPAIR_RETAIN_REGION);
}

/* ========== Test Runner ========== */

int main(void) {
    printf("Escape Repair Strategy Tests\n");
    printf("=================================\n");

    RUN_TEST(test_transmigrate_constant_defined);
    RUN_TEST(test_retain_constant_defined);
    RUN_TEST(test_enum_values_distinct);

    printf("\n");
    printf("Summary: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
