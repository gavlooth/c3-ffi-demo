/*
 * test_restart.c - Tests for the Restart System
 *
 * Verifies:
 * - Restart context push/pop
 * - Restart registration and lookup
 * - Restart invocation with non-local return
 * - Nested restart contexts
 */

#include "../src/restart.h"
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
 * Test Helpers
 * ============================================================ */

static int action_called = 0;
static void* test_value = NULL;

static void* test_action(void* arg) {
    action_called = 1;
    test_value = arg;
    return arg;
}

static void* double_action(void* arg) {
    long val = (long)arg;
    return (void*)(val * 2);
}

/* ============================================================
 * Tests
 * ============================================================ */

TEST(restart_init) {
    restart_init();
    /* Should not crash, can call multiple times */
    restart_init();
    restart_init();
    ASSERT(1);
}

TEST(context_push_pop) {
    restart_init();

    /* No context initially (after cleanup from previous tests) */
    /* Push a context */
    RestartContext* ctx = restart_push_context();
    ASSERT(ctx != NULL);
    ASSERT(restart_current_context() == ctx);

    /* Pop the context */
    restart_pop_context();
    ASSERT(restart_current_context() == NULL);
}

TEST(restart_push_find) {
    restart_init();
    RestartContext* ctx = restart_push_context();
    ASSERT(ctx != NULL);

    /* Push a restart */
    Restart* r = restart_push("test-restart", "A test restart", test_action, NULL);
    ASSERT(r != NULL);
    ASSERT_STR_EQ(r->name, "test-restart");

    /* Find it */
    Restart* found = restart_find("test-restart");
    ASSERT(found != NULL);
    ASSERT_STR_EQ(found->name, "test-restart");

    /* Find nonexistent */
    Restart* notfound = restart_find("no-such-restart");
    ASSERT(notfound == NULL);

    restart_pop_context();
}

TEST(restart_pop_by_name) {
    restart_init();
    restart_push_context();

    restart_push("r1", NULL, NULL, NULL);
    restart_push("r2", NULL, NULL, NULL);

    ASSERT(restart_find("r1") != NULL);
    ASSERT(restart_find("r2") != NULL);

    /* Pop r1 */
    restart_pop("r1");
    ASSERT(restart_find("r1") == NULL);
    ASSERT(restart_find("r2") != NULL);

    restart_pop_context();
}

TEST(nested_contexts) {
    restart_init();

    /* Outer context */
    RestartContext* outer = restart_push_context();
    restart_push("outer-restart", "In outer", NULL, NULL);

    /* Inner context */
    RestartContext* inner = restart_push_context();
    restart_push("inner-restart", "In inner", NULL, NULL);

    /* Both should be findable */
    ASSERT(restart_find("outer-restart") != NULL);
    ASSERT(restart_find("inner-restart") != NULL);

    /* Current context should be inner */
    ASSERT(restart_current_context() == inner);

    /* Pop inner */
    restart_pop_context();

    /* Now only outer restart should be findable */
    ASSERT(restart_find("outer-restart") != NULL);
    ASSERT(restart_find("inner-restart") == NULL);
    ASSERT(restart_current_context() == outer);

    restart_pop_context();
}

TEST(restart_invoke_simple) {
    restart_init();
    action_called = 0;
    test_value = NULL;

    RestartContext* ctx = restart_push_context();
    ASSERT(ctx != NULL);

    if (setjmp(ctx->jump_buffer) == 0) {
        /* Normal path */
        restart_push("test", "Test restart", test_action, NULL);

        /* Invoke the restart */
        restart_invoke("test", (void*)42);

        /* Should not reach here */
        ASSERT(0);
    } else {
        /* Restart was invoked, we jumped here */
        ASSERT(action_called == 1);
        ASSERT(test_value == (void*)42);
    }

    restart_pop_context();
}

TEST(restart_invoke_with_result) {
    restart_init();

    void* result = NULL;

    RESTART_CASE_BEGIN()
        restart_push("double", "Double the value", double_action, NULL);

        /* Invoke and expect to jump */
        restart_invoke("double", (void*)21);

        /* Should not reach here */
        result = (void*)-1;

    RESTART_CASE_END(result)

    /* Result should be 42 (21 * 2) */
    ASSERT(result == (void*)42);
}

TEST(restart_use_value) {
    restart_init();

    void* result = NULL;

    RESTART_CASE_BEGIN()
        restart_push("use-value", "Use a specified value",
                     restart_action_use_value, NULL);

        /* Invoke with a value */
        restart_invoke("use-value", (void*)123);

        result = (void*)-1;

    RESTART_CASE_END(result)

    ASSERT(result == (void*)123);
}

TEST(restart_abort) {
    restart_init();

    void* result = (void*)999;

    RESTART_CASE_BEGIN()
        restart_push("abort", "Abort and return NULL",
                     restart_action_abort, NULL);

        restart_invoke("abort", NULL);

        result = (void*)-1;

    RESTART_CASE_END(result)

    ASSERT(result == NULL);
}

TEST(restart_list_all) {
    restart_init();

    /* Outer context with one restart */
    restart_push_context();
    restart_push("outer", "Outer restart", NULL, NULL);

    /* Inner context with two restarts */
    restart_push_context();
    restart_push("inner1", "First inner", NULL, NULL);
    restart_push("inner2", "Second inner", NULL, NULL);

    /* Get all restarts */
    Restart* all = restart_list_all();
    ASSERT(all != NULL);

    /* Count them - should be 3 */
    int count = 0;
    Restart* r = all;
    while (r) {
        count++;
        Restart* next = r->next;
        free(r);  /* Free the copy */
        r = next;
    }
    ASSERT(count == 3);

    restart_pop_context();
    restart_pop_context();
}

TEST(restart_print) {
    restart_init();

    restart_push_context();
    restart_push("abort", "Abort the operation", NULL, NULL);
    restart_push("retry", "Retry the operation", NULL, NULL);
    restart_push("use-value", "Use a different value", NULL, NULL);

    printf("\n    [Restart list output]:\n");
    restart_print_available();

    restart_pop_context();
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("=== Restart System Tests ===\n\n");

    RUN_TEST(restart_init);
    RUN_TEST(context_push_pop);
    RUN_TEST(restart_push_find);
    RUN_TEST(restart_pop_by_name);
    RUN_TEST(nested_contexts);
    RUN_TEST(restart_invoke_simple);
    RUN_TEST(restart_invoke_with_result);
    RUN_TEST(restart_use_value);
    RUN_TEST(restart_abort);
    RUN_TEST(restart_list_all);
    RUN_TEST(restart_print);

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
