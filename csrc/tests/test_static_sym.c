/*
 * test_static_sym.c
 *
 * Tests for omni_analyze_static_symmetric function.
 *
 * Purpose:
 *   Verify that the Static Symmetric RC analysis correctly identifies
 *   SCCs that can be collected statically at compile time
 *   instead of falling back to runtime Symmetric RC.
 *
 * Why this matters:
 *   Static SCC collection is a critical optimization for the ASAP
 *   memory management system. It eliminates runtime overhead for
 *   cyclic data structures that don't escape their scope.
 *   Incorrect analysis leads to memory leaks or premature frees.
 *
 * Contract:
 *   - Variables with SHAPE_CYCLIC that die inside their SCC
 *   - And have ESCAPE_NONE (don't escape function)
 *   - Should be marked with is_static_scc = true
 *   - Variables that escape should NOT be marked as static
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

#include "../ast/ast.h"
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
    printf("\033[32mPASS\033[0m\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\033[31mFAIL\033[0m (line %d: %s)\n", __LINE__, #cond); \
        tests_run++; \
        return; \
    } \
} while(0)

/* Helper to create a simple symbol */
static OmniValue* mk_sym(const char* name) {
    return omni_new_sym(name);
}

/* Helper to create a cons cell */
static OmniValue* mk_cons(OmniValue* car, OmniValue* cdr) {
    return omni_new_cell(car, cdr);
}

/* Helper to create a list */
static OmniValue* mk_list2(OmniValue* a, OmniValue* b) {
    return mk_cons(a, mk_cons(b, omni_nil));
}

static OmniValue* mk_list3(OmniValue* a, OmniValue* b, OmniValue* c) {
    return mk_cons(a, mk_cons(b, mk_cons(c, omni_nil)));
}

/* ========== Helper: Create simple CFG ========== */

/*
 * Create a minimal CFG with one basic block for testing.
 * This represents a straight-line function body.
 */
static CFG* create_simple_cfg(AnalysisContext* ctx) {
    CFG* cfg = malloc(sizeof(CFG));
    if (!cfg) return NULL;

    cfg->node_capacity = 2;
    cfg->node_count = 2;
    cfg->nodes = malloc(sizeof(CFGNode*) * 2);
    if (!cfg->nodes) {
        free(cfg);
        return NULL;
    }

    /* Entry node */
    CFGNode* entry = malloc(sizeof(CFGNode));
    if (!entry) {
        free(cfg->nodes);
        free(cfg);
        return NULL;
    }
    memset(entry, 0, sizeof(CFGNode));
    entry->id = 0;
    entry->position_start = 0;
    entry->position_end = 10;
    entry->node_type = CFG_ENTRY;
    entry->scc_id = 0;  /* In an SCC */

    /* Exit node */
    CFGNode* exit_node = malloc(sizeof(CFGNode));
    if (!exit_node) {
        free(entry);
        free(cfg->nodes);
        free(cfg);
        return NULL;
    }
    memset(exit_node, 0, sizeof(CFGNode));
    exit_node->id = 1;
    exit_node->position_start = 10;
    exit_node->position_end = 20;
    exit_node->node_type = CFG_EXIT;
    exit_node->scc_id = -1;  /* Not in an SCC */

    cfg->nodes[0] = entry;
    cfg->nodes[1] = exit_node;
    cfg->entry = entry;
    cfg->exit = exit_node;

    return cfg;
}

/* Free test CFG */
static void free_test_cfg(CFG* cfg) {
    if (!cfg) return;
    for (size_t i = 0; i < cfg->node_count; i++) {
        free(cfg->nodes[i]);
    }
    free(cfg->nodes);
    free(cfg);
}

/* ========== Test: Cyclic variable with ESCAPE_NONE ========== */

TEST(test_static_scc_cyclic_non_escaping) {
    AnalysisContext* ctx = omni_analysis_new();
    CFG* cfg = create_simple_cfg(ctx);
    ASSERT(ctx != NULL);
    ASSERT(cfg != NULL);

    /* Create a cyclic variable that doesn't escape */
    OwnerInfo* owner = malloc(sizeof(OwnerInfo));
    ASSERT(owner != NULL);
    memset(owner, 0, sizeof(OwnerInfo));
    owner->name = strdup("cycle");
    owner->shape = SHAPE_CYCLIC;
    owner->must_free = true;
    owner->is_static_scc = false;  /* Will be set by analysis */
    owner->next = NULL;
    ctx->owner_info = owner;

    /* Create variable usage */
    VarUsage* usage = malloc(sizeof(VarUsage));
    ASSERT(usage != NULL);
    memset(usage, 0, sizeof(VarUsage));
    usage->name = strdup("cycle");
    usage->flags = VAR_USAGE_READ | VAR_USAGE_WRITE;
    usage->def_pos = 5;
    usage->last_use = 8;  /* Dies inside the SCC (node 0) */
    usage->next = NULL;
    ctx->var_usages = usage;

    /* Create escape info */
    EscapeInfo* escape = malloc(sizeof(EscapeInfo));
    ASSERT(escape != NULL);
    memset(escape, 0, sizeof(EscapeInfo));
    escape->name = strdup("cycle");
    escape->escape_class = ESCAPE_NONE;  /* Doesn't escape */
    escape->is_unique = true;
    escape->next = NULL;
    ctx->escape_info = escape;

    /* Run static symmetric analysis */
    omni_analyze_static_symmetric(ctx, cfg);

    /* Verify that is_static_scc is set correctly */
    OwnerInfo* result = omni_get_owner_info(ctx, "cycle");
    ASSERT(result != NULL);
    ASSERT(result->is_static_scc == true);
    ASSERT(result->must_free == true);

    /* Cleanup */
    free(usage->name);
    free(usage);
    free(escape->name);
    free(escape);
    free(owner->name);
    free(owner);
    free_test_cfg(cfg);
    omni_analysis_free(ctx);
}

/* ========== Test: Cyclic variable with ESCAPE_RETURN ========== */

TEST(test_static_scc_cyclic_escaping) {
    AnalysisContext* ctx = omni_analysis_new();
    CFG* cfg = create_simple_cfg(ctx);
    ASSERT(ctx != NULL);
    ASSERT(cfg != NULL);

    /* Create a cyclic variable that escapes via return */
    OwnerInfo* owner = malloc(sizeof(OwnerInfo));
    ASSERT(owner != NULL);
    memset(owner, 0, sizeof(OwnerInfo));
    owner->name = strdup("cycle_esc");
    owner->shape = SHAPE_CYCLIC;
    owner->must_free = true;
    owner->is_static_scc = false;
    owner->next = NULL;
    ctx->owner_info = owner;

    /* Create variable usage */
    VarUsage* usage = malloc(sizeof(VarUsage));
    ASSERT(usage != NULL);
    memset(usage, 0, sizeof(VarUsage));
    usage->name = strdup("cycle_esc");
    usage->flags = VAR_USAGE_READ | VAR_USAGE_WRITE | VAR_USAGE_RETURNED;
    usage->def_pos = 5;
    usage->last_use = 15;  /* Dies after returning (node 1) */
    usage->next = NULL;
    ctx->var_usages = usage;

    /* Create escape info */
    EscapeInfo* escape = malloc(sizeof(EscapeInfo));
    ASSERT(escape != NULL);
    memset(escape, 0, sizeof(EscapeInfo));
    escape->name = strdup("cycle_esc");
    escape->escape_class = ESCAPE_RETURN;  /* Escapes function */
    escape->is_unique = false;
    escape->next = NULL;
    ctx->escape_info = escape;

    /* Run static symmetric analysis */
    omni_analyze_static_symmetric(ctx, cfg);

    /* Verify that is_static_scc is NOT set (escapes, can't be static) */
    OwnerInfo* result = omni_get_owner_info(ctx, "cycle_esc");
    ASSERT(result != NULL);
    ASSERT(result->is_static_scc == false);
    /* must_free may still be true, but should use runtime RC */

    /* Cleanup */
    free(usage->name);
    free(usage);
    free(escape->name);
    free(escape);
    free(owner->name);
    free(owner);
    free_test_cfg(cfg);
    omni_analysis_free(ctx);
}

/* ========== Test: Non-cyclic variable (should be skipped) ========== */

TEST(test_static_scc_non_cyclic_skipped) {
    AnalysisContext* ctx = omni_analysis_new();
    CFG* cfg = create_simple_cfg(ctx);
    ASSERT(ctx != NULL);
    ASSERT(cfg != NULL);

    /* Create a non-cyclic variable */
    OwnerInfo* owner = malloc(sizeof(OwnerInfo));
    ASSERT(owner != NULL);
    memset(owner, 0, sizeof(OwnerInfo));
    owner->name = strdup("tree");
    owner->shape = SHAPE_TREE;  /* Not cyclic */
    owner->must_free = true;
    owner->is_static_scc = false;
    owner->next = NULL;
    ctx->owner_info = owner;

    /* Create variable usage */
    VarUsage* usage = malloc(sizeof(VarUsage));
    ASSERT(usage != NULL);
    memset(usage, 0, sizeof(VarUsage));
    usage->name = strdup("tree");
    usage->flags = VAR_USAGE_READ | VAR_USAGE_WRITE;
    usage->def_pos = 5;
    usage->last_use = 8;
    usage->next = NULL;
    ctx->var_usages = usage;

    /* Create escape info */
    EscapeInfo* escape = malloc(sizeof(EscapeInfo));
    ASSERT(escape != NULL);
    memset(escape, 0, sizeof(EscapeInfo));
    escape->name = strdup("tree");
    escape->escape_class = ESCAPE_NONE;
    escape->is_unique = true;
    escape->next = NULL;
    ctx->escape_info = escape;

    /* Run static symmetric analysis */
    omni_analyze_static_symmetric(ctx, cfg);

    /* Non-cyclic variables should NOT be marked as static SCC */
    OwnerInfo* result = omni_get_owner_info(ctx, "tree");
    ASSERT(result != NULL);
    ASSERT(result->is_static_scc == false);
    /* SHAPE_TREE is not affected by static symmetric analysis */

    /* Cleanup */
    free(usage->name);
    free(usage);
    free(escape->name);
    free(escape);
    free(owner->name);
    free(owner);
    free_test_cfg(cfg);
    omni_analysis_free(ctx);
}

/* ========== Test: Variable dies outside SCC ========== */

TEST(test_static_scc_dies_outside_scc) {
    AnalysisContext* ctx = omni_analysis_new();
    CFG* cfg = create_simple_cfg(ctx);
    ASSERT(ctx != NULL);
    ASSERT(cfg != NULL);

    /* Create a cyclic variable that dies outside its SCC */
    OwnerInfo* owner = malloc(sizeof(OwnerInfo));
    ASSERT(owner != NULL);
    memset(owner, 0, sizeof(OwnerInfo));
    owner->name = strdup("late_cycle");
    owner->shape = SHAPE_CYCLIC;
    owner->must_free = true;
    owner->is_static_scc = false;
    owner->next = NULL;
    ctx->owner_info = owner;

    /* Create variable usage */
    VarUsage* usage = malloc(sizeof(VarUsage));
    ASSERT(usage != NULL);
    memset(usage, 0, sizeof(VarUsage));
    usage->name = strdup("late_cycle");
    usage->flags = VAR_USAGE_READ | VAR_USAGE_WRITE;
    usage->def_pos = 12;  /* Defined after SCC entry */
    usage->last_use = 18;  /* Dies outside SCC (node 1) */
    usage->next = NULL;
    ctx->var_usages = usage;

    /* Create escape info */
    EscapeInfo* escape = malloc(sizeof(EscapeInfo));
    ASSERT(escape != NULL);
    memset(escape, 0, sizeof(EscapeInfo));
    escape->name = strdup("late_cycle");
    escape->escape_class = ESCAPE_NONE;
    escape->is_unique = true;
    escape->next = NULL;
    ctx->escape_info = escape;

    /* Run static symmetric analysis */
    omni_analyze_static_symmetric(ctx, cfg);

    /* Variable dies outside SCC, so should NOT be marked static */
    OwnerInfo* result = omni_get_owner_info(ctx, "late_cycle");
    ASSERT(result != NULL);
    ASSERT(result->is_static_scc == false);

    /* Cleanup */
    free(usage->name);
    free(usage);
    free(escape->name);
    free(escape);
    free(owner->name);
    free(owner);
    free_test_cfg(cfg);
    omni_analysis_free(ctx);
}

/* ========== Test: must_free false (should be skipped) ========== */

TEST(test_static_scc_must_free_false) {
    AnalysisContext* ctx = omni_analysis_new();
    CFG* cfg = create_simple_cfg(ctx);
    ASSERT(ctx != NULL);
    ASSERT(cfg != NULL);

    /* Create a cyclic variable that doesn't need freeing (borrowed) */
    OwnerInfo* owner = malloc(sizeof(OwnerInfo));
    ASSERT(owner != NULL);
    memset(owner, 0, sizeof(OwnerInfo));
    owner->name = strdup("borrowed_cycle");
    owner->shape = SHAPE_CYCLIC;
    owner->must_free = false;  /* Don't need to free (borrowed) */
    owner->is_static_scc = false;
    owner->next = NULL;
    ctx->owner_info = owner;

    /* Create variable usage */
    VarUsage* usage = malloc(sizeof(VarUsage));
    ASSERT(usage != NULL);
    memset(usage, 0, sizeof(VarUsage));
    usage->name = strdup("borrowed_cycle");
    usage->flags = VAR_USAGE_READ | VAR_USAGE_WRITE;
    usage->def_pos = 5;
    usage->last_use = 8;
    usage->next = NULL;
    ctx->var_usages = usage;

    /* Create escape info */
    EscapeInfo* escape = malloc(sizeof(EscapeInfo));
    ASSERT(escape != NULL);
    memset(escape, 0, sizeof(EscapeInfo));
    escape->name = strdup("borrowed_cycle");
    escape->escape_class = ESCAPE_NONE;
    escape->is_unique = true;
    escape->next = NULL;
    ctx->escape_info = escape;

    /* Run static symmetric analysis */
    omni_analyze_static_symmetric(ctx, cfg);

    /* must_free = false should skip analysis */
    OwnerInfo* result = omni_get_owner_info(ctx, "borrowed_cycle");
    ASSERT(result != NULL);
    ASSERT(result->is_static_scc == false);

    /* Cleanup */
    free(usage->name);
    free(usage);
    free(escape->name);
    free(escape);
    free(owner->name);
    free(owner);
    free_test_cfg(cfg);
    omni_analysis_free(ctx);
}

/* ========== Test: Null context handling ========== */

TEST(test_static_scc_null_context) {
    CFG* cfg = create_simple_cfg(NULL);
    ASSERT(cfg != NULL);

    /* Should not crash with NULL context */
    omni_analyze_static_symmetric(NULL, cfg);

    free_test_cfg(cfg);
}

/* ========== Test: Null CFG handling ========== */

TEST(test_static_scc_null_cfg) {
    AnalysisContext* ctx = omni_analysis_new();
    ASSERT(ctx != NULL);

    /* Should not crash with NULL CFG */
    omni_analyze_static_symmetric(ctx, NULL);

    omni_analysis_free(ctx);
}

/* ========== Main ========== */

int main(void) {
    printf("=== Static Symmetric RC Analysis Tests ===\n");

    RUN_TEST(test_static_scc_cyclic_non_escaping);
    RUN_TEST(test_static_scc_cyclic_escaping);
    RUN_TEST(test_static_scc_non_cyclic_skipped);
    RUN_TEST(test_static_scc_dies_outside_scc);
    RUN_TEST(test_static_scc_must_free_false);
    RUN_TEST(test_static_scc_null_context);
    RUN_TEST(test_static_scc_null_cfg);

    printf("\n=== Test Results ===\n");
    printf("Total: %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_run - tests_passed);

    if (tests_run == tests_passed) {
        printf("\nALL TESTS PASSED!\n");
        return 0;
    } else {
        printf("\nSOME TESTS FAILED!\n");
        return 1;
    }
}
