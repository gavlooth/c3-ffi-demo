/*
 * test_effect_check.c
 *
 * Tests for Effect Row Type Checking module.
 *
 * Purpose:
 *   Verify that the effect row infrastructure correctly tracks,
 *   merges, and computes effect sets for algebraic effect analysis.
 *
 * Why this matters:
 *   Effect row type checking enables compile-time verification that
 *   all effects are properly handled, preventing runtime errors
 *   from unhandled effects.
 *
 * Contract:
 *   - effect_row_new: Creates an empty effect row
 *   - effect_row_add: Adds an effect (idempotent)
 *   - effect_row_contains: Checks membership
 *   - effect_row_union: Merges two effect rows
 *   - effect_row_subset: Checks if all effects in A are in B
 *   - effect_row_difference: Computes effects in A but not B
 *   - effect_row_to_string: Converts to debug string
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../analysis/effect_check.h"

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

/* ========== Test: Effect Row Creation ========== */

TEST(test_effect_row_new_creates_empty) {
    EffectRow* row = effect_row_new();
    ASSERT(row != NULL);
    ASSERT(row->count == 0);
    ASSERT(row->is_open == false);
    effect_row_free(row);
}

TEST(test_effect_row_free_null_safe) {
    /* Should not crash when passed NULL */
    effect_row_free(NULL);
    /* If we reach here, it's safe */
}

/* ========== Test: Effect Row Add ========== */

TEST(test_effect_row_add_single) {
    EffectRow* row = effect_row_new();
    effect_row_add(row, "Fail");
    ASSERT(row->count == 1);
    ASSERT(strcmp(row->effects[0], "Fail") == 0);
    effect_row_free(row);
}

TEST(test_effect_row_add_multiple) {
    EffectRow* row = effect_row_new();
    effect_row_add(row, "Fail");
    effect_row_add(row, "State");
    effect_row_add(row, "IO");
    ASSERT(row->count == 3);
    effect_row_free(row);
}

TEST(test_effect_row_add_idempotent) {
    /* Adding the same effect twice should not duplicate */
    EffectRow* row = effect_row_new();
    effect_row_add(row, "Fail");
    effect_row_add(row, "Fail");
    ASSERT(row->count == 1);
    effect_row_free(row);
}

TEST(test_effect_row_add_null_safe) {
    EffectRow* row = effect_row_new();
    effect_row_add(NULL, "Fail");  /* Should not crash */
    effect_row_add(row, NULL);     /* Should not crash */
    ASSERT(row->count == 0);       /* No effect added */
    effect_row_free(row);
}

/* ========== Test: Effect Row Contains ========== */

TEST(test_effect_row_contains_present) {
    EffectRow* row = effect_row_new();
    effect_row_add(row, "Fail");
    effect_row_add(row, "State");
    ASSERT(effect_row_contains(row, "Fail") == true);
    ASSERT(effect_row_contains(row, "State") == true);
    effect_row_free(row);
}

TEST(test_effect_row_contains_absent) {
    EffectRow* row = effect_row_new();
    effect_row_add(row, "Fail");
    ASSERT(effect_row_contains(row, "State") == false);
    ASSERT(effect_row_contains(row, "IO") == false);
    effect_row_free(row);
}

TEST(test_effect_row_contains_empty) {
    EffectRow* row = effect_row_new();
    ASSERT(effect_row_contains(row, "Fail") == false);
    effect_row_free(row);
}

TEST(test_effect_row_contains_null_safe) {
    EffectRow* row = effect_row_new();
    effect_row_add(row, "Fail");
    ASSERT(effect_row_contains(NULL, "Fail") == false);
    ASSERT(effect_row_contains(row, NULL) == false);
    effect_row_free(row);
}

/* ========== Test: Effect Row Union ========== */

TEST(test_effect_row_union_both_non_empty) {
    EffectRow* a = effect_row_new();
    effect_row_add(a, "Fail");
    effect_row_add(a, "State");

    EffectRow* b = effect_row_new();
    effect_row_add(b, "IO");
    effect_row_add(b, "Async");

    EffectRow* result = effect_row_union(a, b);
    ASSERT(result != NULL);
    ASSERT(result->count == 4);
    ASSERT(effect_row_contains(result, "Fail") == true);
    ASSERT(effect_row_contains(result, "State") == true);
    ASSERT(effect_row_contains(result, "IO") == true);
    ASSERT(effect_row_contains(result, "Async") == true);

    effect_row_free(a);
    effect_row_free(b);
    effect_row_free(result);
}

TEST(test_effect_row_union_overlapping) {
    EffectRow* a = effect_row_new();
    effect_row_add(a, "Fail");
    effect_row_add(a, "State");

    EffectRow* b = effect_row_new();
    effect_row_add(b, "State");  /* Overlaps */
    effect_row_add(b, "IO");

    EffectRow* result = effect_row_union(a, b);
    ASSERT(result != NULL);
    ASSERT(result->count == 3);  /* No duplicate State */
    ASSERT(effect_row_contains(result, "Fail") == true);
    ASSERT(effect_row_contains(result, "State") == true);
    ASSERT(effect_row_contains(result, "IO") == true);

    effect_row_free(a);
    effect_row_free(b);
    effect_row_free(result);
}

TEST(test_effect_row_union_with_null) {
    EffectRow* a = effect_row_new();
    effect_row_add(a, "Fail");

    EffectRow* result1 = effect_row_union(a, NULL);
    ASSERT(result1 != NULL);
    ASSERT(result1->count == 1);
    ASSERT(effect_row_contains(result1, "Fail") == true);

    EffectRow* result2 = effect_row_union(NULL, a);
    ASSERT(result2 != NULL);
    ASSERT(result2->count == 1);
    ASSERT(effect_row_contains(result2, "Fail") == true);

    effect_row_free(a);
    effect_row_free(result1);
    effect_row_free(result2);
}

TEST(test_effect_row_union_open_rows) {
    EffectRow* a = effect_row_new();
    effect_row_add(a, "Fail");
    a->is_open = true;

    EffectRow* b = effect_row_new();
    effect_row_add(b, "IO");

    EffectRow* result = effect_row_union(a, b);
    ASSERT(result->is_open == true);  /* Open propagates */

    effect_row_free(a);
    effect_row_free(b);
    effect_row_free(result);
}

/* ========== Test: Effect Row Subset ========== */

TEST(test_effect_row_subset_true) {
    EffectRow* a = effect_row_new();
    effect_row_add(a, "Fail");

    EffectRow* b = effect_row_new();
    effect_row_add(b, "Fail");
    effect_row_add(b, "State");

    ASSERT(effect_row_subset(a, b) == true);

    effect_row_free(a);
    effect_row_free(b);
}

TEST(test_effect_row_subset_false) {
    EffectRow* a = effect_row_new();
    effect_row_add(a, "Fail");
    effect_row_add(a, "IO");

    EffectRow* b = effect_row_new();
    effect_row_add(b, "Fail");

    ASSERT(effect_row_subset(a, b) == false);

    effect_row_free(a);
    effect_row_free(b);
}

TEST(test_effect_row_subset_empty_is_subset) {
    EffectRow* a = effect_row_new();  /* Empty */
    EffectRow* b = effect_row_new();
    effect_row_add(b, "Fail");

    ASSERT(effect_row_subset(a, b) == true);
    ASSERT(effect_row_subset(NULL, b) == true);

    effect_row_free(a);
    effect_row_free(b);
}

TEST(test_effect_row_subset_equal_sets) {
    EffectRow* a = effect_row_new();
    effect_row_add(a, "Fail");
    effect_row_add(a, "State");

    EffectRow* b = effect_row_new();
    effect_row_add(b, "Fail");
    effect_row_add(b, "State");

    ASSERT(effect_row_subset(a, b) == true);
    ASSERT(effect_row_subset(b, a) == true);

    effect_row_free(a);
    effect_row_free(b);
}

/* ========== Test: Effect Row Difference ========== */

TEST(test_effect_row_difference_disjoint) {
    EffectRow* a = effect_row_new();
    effect_row_add(a, "Fail");
    effect_row_add(a, "State");

    EffectRow* b = effect_row_new();
    effect_row_add(b, "IO");

    EffectRow* result = effect_row_difference(a, b);
    ASSERT(result != NULL);
    ASSERT(result->count == 2);
    ASSERT(effect_row_contains(result, "Fail") == true);
    ASSERT(effect_row_contains(result, "State") == true);

    effect_row_free(a);
    effect_row_free(b);
    effect_row_free(result);
}

TEST(test_effect_row_difference_partial_overlap) {
    EffectRow* a = effect_row_new();
    effect_row_add(a, "Fail");
    effect_row_add(a, "State");
    effect_row_add(a, "IO");

    EffectRow* b = effect_row_new();
    effect_row_add(b, "State");
    effect_row_add(b, "IO");

    EffectRow* result = effect_row_difference(a, b);
    ASSERT(result != NULL);
    ASSERT(result->count == 1);
    ASSERT(effect_row_contains(result, "Fail") == true);
    ASSERT(effect_row_contains(result, "State") == false);
    ASSERT(effect_row_contains(result, "IO") == false);

    effect_row_free(a);
    effect_row_free(b);
    effect_row_free(result);
}

TEST(test_effect_row_difference_complete_overlap) {
    EffectRow* a = effect_row_new();
    effect_row_add(a, "Fail");

    EffectRow* b = effect_row_new();
    effect_row_add(b, "Fail");
    effect_row_add(b, "State");

    EffectRow* result = effect_row_difference(a, b);
    ASSERT(result != NULL);
    ASSERT(result->count == 0);

    effect_row_free(a);
    effect_row_free(b);
    effect_row_free(result);
}

TEST(test_effect_row_difference_with_null) {
    EffectRow* a = effect_row_new();
    effect_row_add(a, "Fail");

    EffectRow* result = effect_row_difference(a, NULL);
    ASSERT(result != NULL);
    ASSERT(result->count == 1);
    ASSERT(effect_row_contains(result, "Fail") == true);

    effect_row_free(a);
    effect_row_free(result);
}

/* ========== Test: Effect Row to String ========== */

TEST(test_effect_row_to_string_empty) {
    EffectRow* row = effect_row_new();
    char* str = effect_row_to_string(row);
    ASSERT(str != NULL);
    ASSERT(strcmp(str, "{}") == 0);
    free(str);
    effect_row_free(row);
}

TEST(test_effect_row_to_string_single) {
    EffectRow* row = effect_row_new();
    effect_row_add(row, "Fail");
    char* str = effect_row_to_string(row);
    ASSERT(str != NULL);
    ASSERT(strcmp(str, "{Fail}") == 0);
    free(str);
    effect_row_free(row);
}

TEST(test_effect_row_to_string_multiple) {
    EffectRow* row = effect_row_new();
    effect_row_add(row, "Fail");
    effect_row_add(row, "IO");
    char* str = effect_row_to_string(row);
    ASSERT(str != NULL);
    /* Order depends on insertion order */
    ASSERT(strcmp(str, "{Fail, IO}") == 0);
    free(str);
    effect_row_free(row);
}

TEST(test_effect_row_to_string_open) {
    EffectRow* row = effect_row_new();
    effect_row_add(row, "Fail");
    row->is_open = true;
    char* str = effect_row_to_string(row);
    ASSERT(str != NULL);
    ASSERT(strcmp(str, "{Fail, ...}") == 0);
    free(str);
    effect_row_free(row);
}

TEST(test_effect_row_to_string_null) {
    char* str = effect_row_to_string(NULL);
    ASSERT(str != NULL);
    ASSERT(strcmp(str, "{}") == 0);
    free(str);
}

/* ========== Test: Effect Context ========== */

TEST(test_effect_context_new) {
    EffectContext* ctx = effect_context_new(NULL);
    ASSERT(ctx != NULL);
    ASSERT(ctx->current_handler == NULL);
    ASSERT(ctx->required_effects == NULL);
    ASSERT(ctx->in_pure_context == false);
    ASSERT(ctx->handler_depth == 0);
    ASSERT(ctx->parent == NULL);
    effect_context_free(ctx);
}

TEST(test_effect_context_with_parent) {
    EffectContext* parent = effect_context_new(NULL);
    parent->handler_depth = 1;

    EffectContext* child = effect_context_new(parent);
    ASSERT(child != NULL);
    ASSERT(child->handler_depth == 1);  /* Inherited */
    ASSERT(child->parent == parent);

    effect_context_free(child);
    effect_context_free(parent);
}

TEST(test_effect_context_push_handler) {
    EffectContext* ctx = effect_context_new(NULL);

    EffectRow* handled = effect_row_new();
    effect_row_add(handled, "Fail");

    EffectContext* new_ctx = effect_context_push_handler(ctx, handled);
    ASSERT(new_ctx != NULL);
    ASSERT(new_ctx != ctx);  /* New context created */
    ASSERT(new_ctx->current_handler == handled);
    ASSERT(new_ctx->handler_depth == 1);
    ASSERT(new_ctx->parent == ctx);

    /* Cleanup - pop_handler frees the context */
    EffectContext* popped = effect_context_pop_handler(new_ctx);
    ASSERT(popped == ctx);
    effect_context_free(ctx);
}

TEST(test_effect_context_nested_handlers) {
    EffectContext* ctx = effect_context_new(NULL);

    EffectRow* h1 = effect_row_new();
    effect_row_add(h1, "Fail");

    EffectRow* h2 = effect_row_new();
    effect_row_add(h2, "State");

    EffectContext* ctx1 = effect_context_push_handler(ctx, h1);
    EffectContext* ctx2 = effect_context_push_handler(ctx1, h2);

    ASSERT(ctx2->handler_depth == 2);
    ASSERT(ctx1->handler_depth == 1);

    /* Pop inner handler */
    EffectContext* after_pop = effect_context_pop_handler(ctx2);
    ASSERT(after_pop == ctx1);

    /* Pop outer handler */
    after_pop = effect_context_pop_handler(ctx1);
    ASSERT(after_pop == ctx);

    effect_context_free(ctx);
}

/* ========== Test: Built-in Effect Names ========== */

TEST(test_builtin_effect_names_defined) {
    /* Verify built-in effect names are defined */
    ASSERT(strcmp(EFFECT_NAME_FAIL, "Fail") == 0);
    ASSERT(strcmp(EFFECT_NAME_ASK, "Ask") == 0);
    ASSERT(strcmp(EFFECT_NAME_EMIT, "Emit") == 0);
    ASSERT(strcmp(EFFECT_NAME_STATE, "State") == 0);
    ASSERT(strcmp(EFFECT_NAME_YIELD, "Yield") == 0);
    ASSERT(strcmp(EFFECT_NAME_ASYNC, "Async") == 0);
    ASSERT(strcmp(EFFECT_NAME_CHOICE, "Choice") == 0);
    ASSERT(strcmp(EFFECT_NAME_IO, "IO") == 0);
}

/* ========== Main Test Runner ========== */

int main(void) {
    printf("Running Effect Check Tests...\n");
    printf("\n");

    printf("=== Effect Row Creation ===\n");
    RUN_TEST(test_effect_row_new_creates_empty);
    RUN_TEST(test_effect_row_free_null_safe);

    printf("\n=== Effect Row Add ===\n");
    RUN_TEST(test_effect_row_add_single);
    RUN_TEST(test_effect_row_add_multiple);
    RUN_TEST(test_effect_row_add_idempotent);
    RUN_TEST(test_effect_row_add_null_safe);

    printf("\n=== Effect Row Contains ===\n");
    RUN_TEST(test_effect_row_contains_present);
    RUN_TEST(test_effect_row_contains_absent);
    RUN_TEST(test_effect_row_contains_empty);
    RUN_TEST(test_effect_row_contains_null_safe);

    printf("\n=== Effect Row Union ===\n");
    RUN_TEST(test_effect_row_union_both_non_empty);
    RUN_TEST(test_effect_row_union_overlapping);
    RUN_TEST(test_effect_row_union_with_null);
    RUN_TEST(test_effect_row_union_open_rows);

    printf("\n=== Effect Row Subset ===\n");
    RUN_TEST(test_effect_row_subset_true);
    RUN_TEST(test_effect_row_subset_false);
    RUN_TEST(test_effect_row_subset_empty_is_subset);
    RUN_TEST(test_effect_row_subset_equal_sets);

    printf("\n=== Effect Row Difference ===\n");
    RUN_TEST(test_effect_row_difference_disjoint);
    RUN_TEST(test_effect_row_difference_partial_overlap);
    RUN_TEST(test_effect_row_difference_complete_overlap);
    RUN_TEST(test_effect_row_difference_with_null);

    printf("\n=== Effect Row to String ===\n");
    RUN_TEST(test_effect_row_to_string_empty);
    RUN_TEST(test_effect_row_to_string_single);
    RUN_TEST(test_effect_row_to_string_multiple);
    RUN_TEST(test_effect_row_to_string_open);
    RUN_TEST(test_effect_row_to_string_null);

    printf("\n=== Effect Context ===\n");
    RUN_TEST(test_effect_context_new);
    RUN_TEST(test_effect_context_with_parent);
    RUN_TEST(test_effect_context_push_handler);
    RUN_TEST(test_effect_context_nested_handlers);

    printf("\n=== Built-in Effect Names ===\n");
    RUN_TEST(test_builtin_effect_names_defined);

    printf("\n");
    printf("======================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    printf("======================================\n");

    return (tests_run == tests_passed) ? 0 : 1;
}
