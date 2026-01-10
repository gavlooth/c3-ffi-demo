/*
 * test_effect.c - Tests for the Effect System
 *
 * Verifies:
 * - Effect type registration and lookup
 * - Handler stack management
 * - Recovery protocol creation
 * - Built-in effect types
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

TEST(effect_init) {
    effect_init();
    /* Should not crash, can call multiple times */
    effect_init();
    ASSERT(1);
}

TEST(builtin_effects_registered) {
    effect_init();

    ASSERT(EFFECT_FAIL != NULL);
    ASSERT(EFFECT_ASK != NULL);
    ASSERT(EFFECT_EMIT != NULL);
    ASSERT(EFFECT_STATE != NULL);
    ASSERT(EFFECT_YIELD != NULL);
    ASSERT(EFFECT_ASYNC != NULL);
    ASSERT(EFFECT_CHOICE != NULL);

    ASSERT_STR_EQ(EFFECT_FAIL->name, "Fail");
    ASSERT_STR_EQ(EFFECT_ASK->name, "Ask");
    ASSERT_STR_EQ(EFFECT_EMIT->name, "Emit");
}

TEST(effect_type_register) {
    effect_init();

    EffectType* my_effect = effect_type_register("MyEffect", NULL, NULL);
    ASSERT(my_effect != NULL);
    ASSERT_STR_EQ(my_effect->name, "MyEffect");
    ASSERT(my_effect->id > 0);
}

TEST(effect_type_find) {
    effect_init();

    EffectType* found = effect_type_find("Fail");
    ASSERT(found != NULL);
    ASSERT(found == EFFECT_FAIL);

    EffectType* not_found = effect_type_find("NonExistent");
    ASSERT(not_found == NULL);
}

TEST(effect_type_find_by_id) {
    effect_init();

    uint32_t fail_id = EFFECT_FAIL->id;
    EffectType* found = effect_type_find_by_id(fail_id);
    ASSERT(found == EFFECT_FAIL);

    EffectType* not_found = effect_type_find_by_id(99999);
    ASSERT(not_found == NULL);
}

TEST(recovery_protocol_create) {
    RecoveryProtocol* rp = recovery_protocol_create(
        "test-protocol",
        RECOVERY_ONE_SHOT,
        NULL, NULL,
        "A test recovery protocol"
    );

    ASSERT(rp != NULL);
    ASSERT_STR_EQ(rp->name, "test-protocol");
    ASSERT(rp->mode == RECOVERY_ONE_SHOT);
    ASSERT_STR_EQ(rp->description, "A test recovery protocol");

    recovery_protocol_free(rp);
}

TEST(recovery_modes) {
    effect_init();

    /* Fail effect should have ABORT mode */
    ASSERT(EFFECT_FAIL->recovery != NULL);
    ASSERT(EFFECT_FAIL->recovery->mode == RECOVERY_ABORT);

    /* Choice effect should have MULTI_SHOT mode */
    ASSERT(EFFECT_CHOICE->recovery != NULL);
    ASSERT(EFFECT_CHOICE->recovery->mode == RECOVERY_MULTI_SHOT);

    /* Ask effect should have ONE_SHOT mode */
    ASSERT(EFFECT_ASK->recovery != NULL);
    ASSERT(EFFECT_ASK->recovery->mode == RECOVERY_ONE_SHOT);
}

TEST(effect_create) {
    effect_init();

    Effect* eff = effect_create(EFFECT_ASK, NULL);
    ASSERT(eff != NULL);
    ASSERT(eff->type == EFFECT_ASK);
    ASSERT(eff->payload == NULL);

    effect_free(eff);
}

TEST(handler_push_pop) {
    effect_init();

    /* Initially no handler */
    ASSERT(handler_current() == NULL);

    /* Push a handler */
    Handler* h1 = handler_push(NULL, NULL, NULL);
    ASSERT(h1 != NULL);
    ASSERT(handler_current() == h1);
    ASSERT(h1->parent == NULL);

    /* Push another handler */
    Handler* h2 = handler_push(NULL, NULL, NULL);
    ASSERT(h2 != NULL);
    ASSERT(handler_current() == h2);
    ASSERT(h2->parent == h1);

    /* Pop handlers */
    handler_pop();
    ASSERT(handler_current() == h1);

    handler_pop();
    ASSERT(handler_current() == NULL);
}

TEST(handler_clause_add) {
    effect_init();

    HandlerClause* clauses = NULL;
    clauses = handler_clause_add(clauses, EFFECT_ASK, NULL);
    ASSERT(clauses != NULL);
    ASSERT(clauses->effect_type == EFFECT_ASK);

    clauses = handler_clause_add(clauses, EFFECT_EMIT, NULL);
    ASSERT(clauses != NULL);
    ASSERT(clauses->effect_type == EFFECT_EMIT);
    ASSERT(clauses->next->effect_type == EFFECT_ASK);

    handler_clauses_free(clauses);
}

TEST(handler_find_clause) {
    effect_init();

    HandlerClause* clauses = NULL;
    clauses = handler_clause_add(clauses, EFFECT_ASK, NULL);
    clauses = handler_clause_add(clauses, EFFECT_EMIT, NULL);

    Handler* h = handler_push(clauses, NULL, NULL);

    HandlerClause* found = handler_find_clause(h, EFFECT_ASK);
    ASSERT(found != NULL);
    ASSERT(found->effect_type == EFFECT_ASK);

    found = handler_find_clause(h, EFFECT_EMIT);
    ASSERT(found != NULL);
    ASSERT(found->effect_type == EFFECT_EMIT);

    found = handler_find_clause(h, EFFECT_FAIL);
    ASSERT(found == NULL);

    handler_pop();
    handler_clauses_free(clauses);
}

TEST(handler_find) {
    effect_init();

    /* No handler initially */
    Handler* found = handler_find(EFFECT_ASK);
    ASSERT(found == NULL);

    /* Add a handler for ASK */
    HandlerClause* clauses = handler_clause_add(NULL, EFFECT_ASK, NULL);
    Handler* h = handler_push(clauses, NULL, NULL);

    found = handler_find(EFFECT_ASK);
    ASSERT(found == h);

    found = handler_find(EFFECT_EMIT);
    ASSERT(found == NULL);

    handler_pop();
    handler_clauses_free(clauses);
}

TEST(nested_handlers) {
    effect_init();

    /* Outer handler for EMIT */
    HandlerClause* outer_clauses = handler_clause_add(NULL, EFFECT_EMIT, NULL);
    Handler* h_outer = handler_push(outer_clauses, NULL, NULL);

    /* Inner handler for ASK */
    HandlerClause* inner_clauses = handler_clause_add(NULL, EFFECT_ASK, NULL);
    Handler* h_inner = handler_push(inner_clauses, NULL, NULL);

    /* Find should return innermost matching handler */
    Handler* found = handler_find(EFFECT_ASK);
    ASSERT(found == h_inner);

    found = handler_find(EFFECT_EMIT);
    ASSERT(found == h_outer);

    handler_pop();
    handler_pop();
    handler_clauses_free(inner_clauses);
    handler_clauses_free(outer_clauses);
}

TEST(effect_trace) {
    effect_init();

    ASSERT(effect_trace_is_enabled() == false);

    effect_trace_enable(true);
    ASSERT(effect_trace_is_enabled() == true);

    effect_trace_enable(false);
    ASSERT(effect_trace_is_enabled() == false);
}

TEST(effect_type_print) {
    effect_init();

    printf("\n    [Effect type output]:\n    ");
    effect_type_print(EFFECT_ASK);
    printf("    ");
    effect_type_print(EFFECT_FAIL);
    printf("    ");
    effect_type_print(EFFECT_CHOICE);
}

TEST(handler_stack_print) {
    effect_init();

    HandlerClause* clauses = handler_clause_add(NULL, EFFECT_ASK, NULL);
    clauses = handler_clause_add(clauses, EFFECT_EMIT, NULL);
    handler_push(clauses, NULL, NULL);

    printf("\n    [Handler stack output]:\n");
    handler_stack_print();

    handler_pop();
    handler_clauses_free(clauses);
}

TEST(resumption_create) {
    effect_init();

    /* Create a dummy continuation (NULL is ok for this test) */
    Resumption* r = resumption_create(NULL, EFFECT_ASK, NULL);
    ASSERT(r != NULL);
    ASSERT(r->effect_type == EFFECT_ASK);
    ASSERT(r->mode == RECOVERY_ONE_SHOT);
    ASSERT(r->used == false);
    ASSERT(r->refcount == 1);

    resumption_dec_ref(r);
}

TEST(resumption_one_shot_constraint) {
    effect_init();

    /* Create a one-shot resumption */
    Resumption* r = resumption_create(NULL, EFFECT_ASK, NULL);
    ASSERT(r->mode == RECOVERY_ONE_SHOT);
    ASSERT(r->used == false);

    /* Without a continuation, resumption is not valid for invoking */
    /* (resumption_is_valid requires r->cont != NULL) */
    ASSERT(resumption_is_valid(r) == false);  /* No cont means not invocable */

    /* Mark as used */
    r->used = true;

    /* Still invalid (no cont + used) */
    ASSERT(resumption_is_valid(r) == false);

    resumption_dec_ref(r);
}

TEST(resumption_multi_shot_clone) {
    effect_init();

    /* Create a multi-shot resumption */
    Resumption* r = resumption_create(NULL, EFFECT_CHOICE, NULL);
    ASSERT(r->mode == RECOVERY_MULTI_SHOT);

    /* Clone it */
    Resumption* r2 = resumption_clone(r);
    ASSERT(r2 != NULL);
    ASSERT(r2->mode == RECOVERY_MULTI_SHOT);
    ASSERT(r2->used == false);  /* Clone starts unused */

    resumption_dec_ref(r);
    resumption_dec_ref(r2);
}

TEST(resumption_abort_not_resumable) {
    effect_init();

    /* Create an abort-mode resumption */
    Resumption* r = resumption_create(NULL, EFFECT_FAIL, NULL);
    ASSERT(r->mode == RECOVERY_ABORT);

    /* Abort mode is never valid for resuming */
    ASSERT(resumption_is_valid(r) == false);

    resumption_dec_ref(r);
}

TEST(effect_perform_unhandled) {
    effect_init();

    /* Performing without a handler should fail gracefully */
    Obj* result = effect_perform(EFFECT_ASK, NULL);
    /* Should print error and return NULL */
    ASSERT(result == NULL);
}

TEST(resumption_discard) {
    effect_init();

    Resumption* r = resumption_create(NULL, EFFECT_ASK, NULL);
    ASSERT(r->used == false);

    /* Discard marks as used */
    r->refcount++;  /* Keep alive for test */
    resumption_discard(r);
    ASSERT(r->used == true);

    resumption_dec_ref(r);
}

/* ============================================================
 * Main
 * ============================================================ */

int main(void) {
    printf("=== Effect System Tests ===\n\n");

    RUN_TEST(effect_init);
    RUN_TEST(builtin_effects_registered);
    RUN_TEST(effect_type_register);
    RUN_TEST(effect_type_find);
    RUN_TEST(effect_type_find_by_id);
    RUN_TEST(recovery_protocol_create);
    RUN_TEST(recovery_modes);
    RUN_TEST(effect_create);
    RUN_TEST(handler_push_pop);
    RUN_TEST(handler_clause_add);
    RUN_TEST(handler_find_clause);
    RUN_TEST(handler_find);
    RUN_TEST(nested_handlers);
    RUN_TEST(effect_trace);
    RUN_TEST(effect_type_print);
    RUN_TEST(handler_stack_print);
    RUN_TEST(resumption_create);
    RUN_TEST(resumption_one_shot_constraint);
    RUN_TEST(resumption_multi_shot_clone);
    RUN_TEST(resumption_abort_not_resumable);
    RUN_TEST(effect_perform_unhandled);
    RUN_TEST(resumption_discard);

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
