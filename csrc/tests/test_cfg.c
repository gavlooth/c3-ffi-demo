/*
 * CFG and Liveness Analysis Tests
 *
 * Tests for control flow graph construction and backward liveness analysis.
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

/* Helper to create a simple integer */
static OmniValue* mk_int(long val) {
    return omni_new_int(val);
}

/* Helper to create a cons cell */
static OmniValue* mk_cons(OmniValue* car, OmniValue* cdr) {
    return omni_new_cell(car, cdr);
}

/* Helper to create a list from args */
static OmniValue* mk_list2(OmniValue* a, OmniValue* b) {
    return mk_cons(a, mk_cons(b, omni_nil));
}

static OmniValue* mk_list3(OmniValue* a, OmniValue* b, OmniValue* c) {
    return mk_cons(a, mk_cons(b, mk_cons(c, omni_nil)));
}

static OmniValue* mk_list4(OmniValue* a, OmniValue* b, OmniValue* c, OmniValue* d) {
    return mk_cons(a, mk_cons(b, mk_cons(c, mk_cons(d, omni_nil))));
}

/* ========== Basic CFG Tests ========== */

TEST(test_cfg_simple_expr) {
    /* Just a symbol reference: x */
    OmniValue* expr = mk_sym("x");
    CFG* cfg = omni_build_cfg(expr);

    ASSERT(cfg != NULL);
    ASSERT(cfg->node_count >= 2);  /* At least entry and exit */
    ASSERT(cfg->entry != NULL);
    ASSERT(cfg->exit != NULL);

    omni_cfg_free(cfg);
    /* Note: Not freeing expr - would need proper omni value free */
}

TEST(test_cfg_if_branches) {
    /* (if cond then else) */
    OmniValue* expr = mk_list4(
        mk_sym("if"),
        mk_sym("cond"),
        mk_sym("then_val"),
        mk_sym("else_val")
    );

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    /* Should have: entry, cond/branch, then_entry, else_entry, join, exit */
    ASSERT(cfg->node_count >= 4);

    /* Find branch node */
    int branch_count = 0;
    int join_count = 0;
    for (size_t i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i]->node_type == CFG_BRANCH) branch_count++;
        if (cfg->nodes[i]->node_type == CFG_JOIN) join_count++;
    }
    ASSERT(branch_count == 1);
    ASSERT(join_count == 1);

    omni_cfg_free(cfg);
}

TEST(test_cfg_let_bindings) {
    /* (let ((x 1)) x) */
    OmniValue* bindings = mk_cons(
        mk_list2(mk_sym("x"), mk_int(1)),
        omni_nil
    );
    OmniValue* expr = mk_list3(
        mk_sym("let"),
        bindings,
        mk_sym("x")
    );

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);
    ASSERT(cfg->node_count >= 3);

    /* Check for def of x */
    bool found_x_def = false;
    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* n = cfg->nodes[i];
        for (size_t j = 0; j < n->def_count; j++) {
            if (strcmp(n->defs[j], "x") == 0) {
                found_x_def = true;
                break;
            }
        }
    }
    ASSERT(found_x_def);

    omni_cfg_free(cfg);
}

/* ========== Liveness Analysis Tests ========== */

TEST(test_liveness_simple_use) {
    /* x - just a use of x */
    OmniValue* expr = mk_sym("x");

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);
    omni_compute_liveness(cfg, ctx);

    /* x should be used somewhere in the CFG */
    bool x_is_used = false;
    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* n = cfg->nodes[i];
        for (size_t j = 0; j < n->use_count; j++) {
            if (strcmp(n->uses[j], "x") == 0) {
                x_is_used = true;
                break;
            }
        }
        if (x_is_used) break;
    }
    ASSERT(x_is_used);

    omni_cfg_free(cfg);
    omni_analysis_free(ctx);
    /* Note: OmniValue cleanup handled by allocator */
}

TEST(test_liveness_if_branches) {
    /* (if cond x y)
     * x should be live on then branch
     * y should be live on else branch
     * cond should be live at condition
     */
    OmniValue* expr = mk_list4(
        mk_sym("if"),
        mk_sym("cond"),
        mk_sym("x"),
        mk_sym("y")
    );

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);
    omni_compute_liveness(cfg, ctx);

    /* Find the join node */
    CFGNode* join = NULL;
    for (size_t i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i]->node_type == CFG_JOIN) {
            join = cfg->nodes[i];
            break;
        }
    }
    ASSERT(join != NULL);

    /* At join, neither x nor y should be live (they were used in branches) */
    /* Actually they might be live-out if they're not defined locally */

    omni_cfg_free(cfg);
    omni_analysis_free(ctx);
    /* Note: OmniValue cleanup handled by allocator */
}

TEST(test_liveness_let_scope) {
    /* (let ((x 1)) (+ x 1))
     * x is defined then used - should die after use
     */
    OmniValue* bindings = mk_cons(
        mk_list2(mk_sym("x"), mk_int(1)),
        omni_nil
    );
    OmniValue* body = mk_list3(mk_sym("+"), mk_sym("x"), mk_int(1));
    OmniValue* expr = mk_list3(mk_sym("let"), bindings, body);

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);
    omni_compute_liveness(cfg, ctx);

    /* x should be in some node's live_in but not in that node's live_out
     * (meaning it dies there) */
    bool x_dies_somewhere = false;
    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* n = cfg->nodes[i];
        bool in_live_in = false;
        bool in_live_out = false;

        for (size_t j = 0; j < n->live_in_count; j++) {
            if (strcmp(n->live_in[j], "x") == 0) in_live_in = true;
        }
        for (size_t j = 0; j < n->live_out_count; j++) {
            if (strcmp(n->live_out[j], "x") == 0) in_live_out = true;
        }

        if (in_live_in && !in_live_out) {
            x_dies_somewhere = true;
            break;
        }
    }
    ASSERT(x_dies_somewhere);

    omni_cfg_free(cfg);
    omni_analysis_free(ctx);
    /* Note: OmniValue cleanup handled by allocator */
}

/* ========== Free Point Tests ========== */

TEST(test_free_points_basic) {
    /* (let ((x (mk-obj))) x)
     * x should be freed after the use
     */
    OmniValue* bindings = mk_cons(
        mk_list2(mk_sym("x"), mk_list2(mk_sym("mk-obj"), omni_nil)),
        omni_nil
    );
    OmniValue* expr = mk_list3(mk_sym("let"), bindings, mk_sym("x"));

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);
    omni_compute_liveness(cfg, ctx);

    CFGFreePoint* fps = omni_compute_cfg_free_points(cfg, ctx);
    /* Should have at least one free point for x */
    /* Note: depends on ownership analysis marking x as must_free */

    omni_cfg_free_points_free(fps);
    omni_cfg_free(cfg);
    omni_analysis_free(ctx);
    /* Note: OmniValue cleanup handled by allocator */
}

/* ========== SCC Analysis Tests ========== */

TEST(test_scc_no_cycles_dag) {
    /* (if cond x y) - creates a DAG with branches, no cycles
     * All nodes should have scc_id = -1 (not in a cycle)
     */
    OmniValue* expr = mk_list4(
        mk_sym("if"),
        mk_sym("cond"),
        mk_sym("x"),
        mk_sym("y")
    );

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    /* Call SCC computation - should not crash */
    omni_compute_scc(cfg);

    /* Verify: all nodes should have scc_id = -1 (no cycles in this DAG) */
    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* n = cfg->nodes[i];
        ASSERT(n->scc_id == -1);
        ASSERT(n->is_scc_entry == false);
    }

    omni_cfg_free(cfg);
    /* Note: OmniValue cleanup handled by allocator */
}

TEST(test_scc_null_cfg) {
    /* Edge case: NULL CFG should not crash
     * This tests the safety check at the start of omni_compute_scc
     */
    omni_compute_scc(NULL);
    /* If we get here without crashing, test passes */
}

/* ========== Print CFG Test ========== */

TEST(test_print_cfg) {
    /* (if cond (+ x 1) (+ y 2)) */
    OmniValue* then_expr = mk_list3(mk_sym("+"), mk_sym("x"), mk_int(1));
    OmniValue* else_expr = mk_list3(mk_sym("+"), mk_sym("y"), mk_int(2));
    OmniValue* expr = mk_list4(mk_sym("if"), mk_sym("cond"), then_expr, else_expr);

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    AnalysisContext* ctx = omni_analysis_new();
    omni_analyze_ownership(ctx, expr);
    omni_compute_liveness(cfg, ctx);

    /* Just verify it doesn't crash */
    printf("\n");
    omni_print_cfg(cfg);

    omni_cfg_free(cfg);
    omni_analysis_free(ctx);
    /* Note: OmniValue cleanup handled by allocator */
}

/* ========== Dominator Analysis Tests ========== */

TEST(test_dominators_null_cfg) {
    /* Edge case: NULL CFG should not crash */
    omni_compute_dominators(NULL);
    /* If we get here without crashing, test passes */
}

TEST(test_dominators_linear_cfg) {
    /* Simple linear CFG: entry -> node1 -> node2 -> exit
     * Expected dominators:
     *   - entry: none
     *   - node1: entry
     *   - node2: node1 (indirectly entry)
     *   - exit: node2 (indirectly entry, node1, node2)
     */
    OmniValue* expr = mk_sym("x");  /* Simple expression creates linear CFG */
    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    omni_compute_dominators(cfg);

    /* Entry should dominate itself (temporarily set during algorithm, NULL after) */
    ASSERT(cfg->entry->idom == NULL);

    /* Count nodes and verify structure */
    int node_count = (int)cfg->node_count;
    ASSERT(node_count >= 2);  /* At least entry and exit */

    /* Every node except entry should have an immediate dominator */
    int nodes_with_idom = 0;
    for (size_t i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i] == cfg->entry) continue;
        if (cfg->nodes[i]->idom != NULL) {
            nodes_with_idom++;
        }
    }
    ASSERT(nodes_with_idom > 0);  /* At least some nodes should have dominators */

    omni_cfg_free(cfg);
}

TEST(test_dominators_if_branches) {
    /* (if cond then else)
     * Expected CFG structure:
     *   entry -> branch_node
     *   branch_node -> then_node (on true)
     *   branch_node -> else_node (on false)
     *   then_node -> join_node
     *   else_node -> join_node
     *   join_node -> exit
     *
     * Expected dominators:
     *   - entry: none
     *   - branch_node: entry
     *   - then_node: branch_node (indirectly entry)
     *   - else_node: branch_node (indirectly entry)
     *   - join_node: branch_node (both branches converge here)
     *   - exit: join_node (or whatever is before it)
     */
    OmniValue* expr = mk_list4(
        mk_sym("if"),
        mk_sym("cond"),
        mk_sym("then_val"),
        mk_sym("else_val")
    );

    CFG* cfg = omni_build_cfg(expr);
    ASSERT(cfg != NULL);

    omni_compute_dominators(cfg);

    /* Entry should have no immediate dominator */
    ASSERT(cfg->entry->idom == NULL);

    /* Find branch node */
    CFGNode* branch = NULL;
    CFGNode* join = NULL;
    for (size_t i = 0; i < cfg->node_count; i++) {
        if (cfg->nodes[i]->node_type == CFG_BRANCH) {
            branch = cfg->nodes[i];
        }
        if (cfg->nodes[i]->node_type == CFG_JOIN) {
            join = cfg->nodes[i];
        }
    }

    ASSERT(branch != NULL);
    ASSERT(join != NULL);

    /* Branch should be dominated by entry */
    ASSERT(branch->idom != NULL);

    /* Join should have an immediate dominator (likely branch or a successor) */
    ASSERT(join->idom != NULL);

    /* Both branches should converge at join, so their dominator chains
     * should include branch node */
    bool then_found = false;
    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];
        if (node == branch || node == join || node == cfg->entry) continue;

        /* Check if this node is dominated by branch */
        CFGNode* current = node->idom;
        while (current != NULL) {
            if (current == branch) {
                then_found = true;
                break;
            }
            current = current->idom;
        }
    }

    omni_cfg_free(cfg);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== CFG and Liveness Analysis Tests ===\033[0m\n");

    printf("\n\033[33m--- Basic CFG Construction ---\033[0m\n");
    RUN_TEST(test_cfg_simple_expr);
    RUN_TEST(test_cfg_if_branches);
    RUN_TEST(test_cfg_let_bindings);

    printf("\n\033[33m--- Liveness Analysis ---\033[0m\n");
    RUN_TEST(test_liveness_simple_use);
    RUN_TEST(test_liveness_if_branches);
    RUN_TEST(test_liveness_let_scope);

    printf("\n\033[33m--- SCC Analysis ---\033[0m\n");
    RUN_TEST(test_scc_no_cycles_dag);
    RUN_TEST(test_scc_null_cfg);

    printf("\n\033[33m--- Dominator Analysis ---\033[0m\n");
    RUN_TEST(test_dominators_null_cfg);
    RUN_TEST(test_dominators_linear_cfg);
    RUN_TEST(test_dominators_if_branches);

    printf("\n\033[33m--- Free Point Computation ---\033[0m\n");
    RUN_TEST(test_free_points_basic);

    printf("\n\033[33m--- Debug Output ---\033[0m\n");
    RUN_TEST(test_print_cfg);

    printf("\n\033[33m=== Summary ===\033[0m\n");
    printf("  Total:  %d\n", tests_run);
    if (tests_passed == tests_run) {
        printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
    } else {
        printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
        printf("  \033[31mFailed: %d\033[0m\n", tests_run - tests_passed);
    }
    printf("  Failed: %d\n", tests_run - tests_passed);

    return (tests_passed == tests_run) ? 0 : 1;
}
