/*
 * test_effect_tracing.c - Tests for Effect Tracing
 *
 * Verifies:
 * - Trace enable/disable
 * - Trace recording
 * - Trace printing and to_string
 * - Trace clearing
 * - Mark handled functionality
 */

#include "../src/effect.h"
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

#define ASSERT_STR_CONTAINS(haystack, needle) do { \
    if (strstr((haystack), (needle)) == NULL) { \
        printf(" FAILED\n"); \
        printf("    Expected to contain: %s\n", (needle)); \
        printf("    Got: %s\n", (haystack)); \
        printf("    at %s:%d\n", __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ============================================================
 * Tests
 * ============================================================ */

TEST(trace_enable_disable) {
    effect_init();

    /* Initially disabled */
    ASSERT(!effect_trace_is_enabled());

    /* Enable */
    effect_trace_enable(true);
    ASSERT(effect_trace_is_enabled());

    /* Disable */
    effect_trace_enable(false);
    ASSERT(!effect_trace_is_enabled());
}

TEST(trace_clear) {
    effect_init();
    effect_trace_clear();

    /* After clear, last index should be -1 */
    ASSERT(effect_trace_last_index() == -1);
}

TEST(trace_record_basic) {
    effect_init();
    effect_trace_clear();
    effect_trace_enable(true);

    /* Create a test effect */
    Effect* eff = effect_create(EFFECT_FAIL, NULL);
    ASSERT(eff != NULL);

    /* Record it */
    effect_trace_record(eff);

    /* Should have one entry */
    ASSERT(effect_trace_last_index() == 0);

    effect_free(eff);
    effect_trace_enable(false);
}

TEST(trace_record_multiple) {
    effect_init();
    effect_trace_clear();
    effect_trace_enable(true);

    /* Record multiple effects */
    Effect* eff1 = effect_create(EFFECT_FAIL, NULL);
    Effect* eff2 = effect_create(EFFECT_ASK, NULL);
    Effect* eff3 = effect_create(EFFECT_EMIT, NULL);

    effect_trace_record(eff1);
    effect_trace_record(eff2);
    effect_trace_record(eff3);

    /* Should have three entries */
    ASSERT(effect_trace_last_index() == 2);

    effect_free(eff1);
    effect_free(eff2);
    effect_free(eff3);
    effect_trace_enable(false);
}

TEST(trace_disabled_no_record) {
    effect_init();
    effect_trace_clear();
    effect_trace_enable(false);  /* Disabled */

    Effect* eff = effect_create(EFFECT_FAIL, NULL);
    effect_trace_record(eff);

    /* Should not have recorded */
    ASSERT(effect_trace_last_index() == -1);

    effect_free(eff);
}

TEST(trace_to_string_empty) {
    effect_init();
    effect_trace_clear();

    char* str = effect_trace_to_string();
    ASSERT(str != NULL);
    ASSERT_STR_CONTAINS(str, "empty");
    free(str);
}

TEST(trace_to_string_with_entries) {
    effect_init();
    effect_trace_clear();
    effect_trace_enable(true);

    Effect* eff = effect_create(EFFECT_FAIL, NULL);
    effect_trace_record(eff);

    char* str = effect_trace_to_string();
    ASSERT(str != NULL);
    ASSERT_STR_CONTAINS(str, "Fail");
    ASSERT_STR_CONTAINS(str, "1 events");

    free(str);
    effect_free(eff);
    effect_trace_enable(false);
}

TEST(trace_mark_handled) {
    effect_init();
    effect_trace_clear();
    effect_trace_enable(true);

    Effect* eff = effect_create(EFFECT_ASK, NULL);
    effect_trace_record(eff);

    int idx = effect_trace_last_index();
    ASSERT(idx == 0);

    /* Mark as handled */
    effect_trace_mark_handled(idx);

    /* Verify in string output */
    char* str = effect_trace_to_string();
    ASSERT_STR_CONTAINS(str, "handled");

    free(str);
    effect_free(eff);
    effect_trace_enable(false);
}

TEST(trace_print_empty) {
    effect_init();
    effect_trace_clear();

    /* Should not crash */
    printf("\n    ");
    effect_trace_print();
}

TEST(trace_print_with_entries) {
    effect_init();
    effect_trace_clear();
    effect_trace_enable(true);

    Effect* eff = effect_create(EFFECT_EMIT, NULL);
    effect_trace_record(eff);

    /* Should not crash and show Emit */
    printf("\n    ");
    effect_trace_print();

    effect_free(eff);
    effect_trace_enable(false);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("Effect Tracing Tests\n");
    printf("====================\n");

    RUN_TEST(trace_enable_disable);
    RUN_TEST(trace_clear);
    RUN_TEST(trace_record_basic);
    RUN_TEST(trace_record_multiple);
    RUN_TEST(trace_disabled_no_record);
    RUN_TEST(trace_to_string_empty);
    RUN_TEST(trace_to_string_with_entries);
    RUN_TEST(trace_mark_handled);
    RUN_TEST(trace_print_empty);
    RUN_TEST(trace_print_with_entries);

    printf("\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    effect_trace_clear();

    return tests_failed > 0 ? 1 : 0;
}
