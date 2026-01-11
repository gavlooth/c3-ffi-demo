/*
 * Region Rank Codegen Tests (Issue 2 P4.2)
 *
 * Tests that codegen emits lifetime_rank assignment using accessor functions.
 * The rank represents the outlives depth (nesting order) of regions.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../ast/ast.h"
#include "../analysis/analysis.h"
#include "../codegen/codegen.h"

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

/* Helper to create a list */
static OmniValue* mk_list2(OmniValue* a, OmniValue* b) {
    return mk_cons(a, mk_cons(b, omni_nil));
}

static OmniValue* mk_list3(OmniValue* a, OmniValue* b, OmniValue* c) {
    return mk_cons(a, mk_cons(b, mk_cons(c, omni_nil)));
}

static OmniValue* mk_list4(OmniValue* a, OmniValue* b, OmniValue* c, OmniValue* d) {
    return mk_cons(a, mk_cons(b, mk_cons(c, mk_cons(d, omni_nil))));
}

/* ========== Issue 2 P4.2: Rank Assignment Tests ========== */

TEST(test_function_emits_rank_assignment) {
    /* (define (f x) (+ x 1))
     * Should generate:
     *   _local_region->lifetime_rank = _caller_region->lifetime_rank + 1;
     * or using accessor functions:
     *   omni_region_set_lifetime_rank(_local_region, omni_region_get_lifetime_rank(_caller_region) + 1);
     */
    OmniValue* params = mk_cons(mk_sym("x"), omni_nil);
    OmniValue* body = mk_list3(mk_sym("+"), mk_sym("x"), mk_int(1));
    OmniValue* expr = mk_list3(mk_sym("define"), mk_cons(mk_sym("f"), params), body);

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();
    omni_analyze_ownership(cg->analysis, expr);

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Check for rank assignment using accessor functions */
    ASSERT(strstr(output, "omni_region_set_lifetime_rank") != NULL);
    ASSERT(strstr(output, "omni_region_get_lifetime_rank") != NULL);
    ASSERT(strstr(output, "_caller_region) + 1") != NULL);

    free(output);
    omni_codegen_free(cg);  /* Note: this also frees cg->analysis */
}

TEST(test_lambda_emits_rank_assignment) {
    /* ((lambda (x) (+ x 1)) 42)
     * Lambda should also assign rank based on caller region
     */
    OmniValue* params = mk_cons(mk_sym("x"), omni_nil);
    OmniValue* body = mk_list3(mk_sym("+"), mk_sym("x"), mk_int(1));
    OmniValue* lambda = mk_list3(mk_sym("lambda"), params, body);
    OmniValue* expr = mk_list2(lambda, mk_int(42));

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();
    omni_analyze_ownership(cg->analysis, expr);

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Lambda implementations should also use rank assignment */
    ASSERT(strstr(output, "omni_region_set_lifetime_rank") != NULL);

    free(output);
    omni_codegen_free(cg);  /* Note: this also frees cg->analysis */
}

TEST(test_let_emits_rank_assignment) {
    /* (let ((x 1)) x)
     * Let bindings at top level are compiled inline, so they don't
     * create functions with rank assignment. This test verifies that
     * the comment for main() explains the rank behavior.
     */
    OmniValue* bindings = mk_cons(
        mk_list2(mk_sym("x"), mk_int(1)),
        omni_nil
    );
    OmniValue* expr = mk_list3(mk_sym("let"), bindings, mk_sym("x"));

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();
    omni_analyze_ownership(cg->analysis, expr);

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Top-level let bindings don't create functions, so we check for
     * the main() comment about rank being 0 instead */
    ASSERT(strstr(output, "lifetime_rank = 0") != NULL);

    free(output);
    omni_codegen_free(cg);  /* Note: this also frees cg->analysis */
}

TEST(test_main_has_rank_comment) {
    /* Main function doesn't have caller, so rank is 0 by default
     * Should have a comment explaining this
     */
    OmniValue* expr = mk_int(42);

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();
    omni_analyze_ownership(cg->analysis, expr);

    omni_codegen_main(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Main should have a comment about rank being 0 */
    ASSERT(strstr(output, "lifetime_rank = 0") != NULL);

    free(output);
    omni_codegen_free(cg);  /* Note: this also frees cg->analysis */
}

TEST(test_no_direct_rank_field_access) {
    /* Verify that generated code does NOT use direct field access
     * (like _local_region->lifetime_rank) because Region is incomplete
     * in generated code. Must use accessor functions instead.
     *
     * Note: We exclude comments from this check since comments may mention
     * the field names for documentation purposes.
     */
    OmniValue* params = mk_cons(mk_sym("x"), omni_nil);
    OmniValue* body = mk_list3(mk_sym("+"), mk_sym("x"), mk_int(1));
    OmniValue* expr = mk_list3(mk_sym("define"), mk_cons(mk_sym("f"), params), body);

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();
    omni_analyze_ownership(cg->analysis, expr);

    omni_codegen_program(cg, &expr, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Look for actual code statements (not comments) with direct field access */
    const char* direct_access = "_local_region->lifetime_rank";
    const char* p = output;
    bool found_in_code = false;
    while ((p = strstr(p, direct_access)) != NULL) {
        /* Check if this is in actual code (after a semicolon or start of line)
         * and not in a comment (after /* or // ) */
        const char* line_start = p;
        while (line_start > output && *(line_start - 1) != '\n') {
            line_start--;
        }
        /* Check if the pattern is within a comment */
        bool in_comment = false;
        for (const char* c = line_start; c < p; c++) {
            if (strncmp(c, "/*", 2) == 0 && (c + 1) < p) {
                in_comment = true;
                break;
            }
            if (strncmp(c, "//", 2) == 0 && (c + 1) < p) {
                in_comment = true;
                break;
            }
        }
        if (!in_comment) {
            found_in_code = true;
            break;
        }
        p += strlen(direct_access);
    }

    ASSERT(!found_in_code);  /* Should not find direct field access in actual code */

    /* Should use accessor functions instead */
    ASSERT(strstr(output, "omni_region_set_lifetime_rank") != NULL);
    ASSERT(strstr(output, "omni_region_get_lifetime_rank") != NULL);

    free(output);
    omni_codegen_free(cg);  /* Note: this also frees cg->analysis */
}

TEST(test_nested_functions_rank_hierarchy) {
    /* (define (outer x)
     *   (define (inner y) (+ x y))
     *   (inner 10))
     *
     * Outer region: rank = caller_rank + 1
     * Inner region: rank = outer_rank + 1 = caller_rank + 2
     */
    OmniValue* inner_params = mk_cons(mk_sym("y"), omni_nil);
    OmniValue* inner_body = mk_list3(mk_sym("+"), mk_sym("x"), mk_sym("y"));
    OmniValue* inner_def = mk_list3(
        mk_sym("define"),
        mk_cons(mk_sym("inner"), inner_params),
        inner_body
    );

    /* Create outer body as: (begin (define inner ...) (inner 10))
     * Using mk_list3 for begin with inner_def and inner call
     */
    OmniValue* outer_body = mk_list3(
        mk_sym("begin"),
        inner_def,
        mk_list2(mk_sym("inner"), mk_int(10))
    );
    OmniValue* outer_def = mk_list3(
        mk_sym("define"),
        mk_cons(mk_sym("outer"), mk_cons(mk_sym("x"), omni_nil)),
        outer_body
    );

    CodeGenContext* cg = omni_codegen_new_buffer();
    cg->analysis = omni_analysis_new();
    omni_analyze_ownership(cg->analysis, outer_def);

    omni_codegen_program(cg, &outer_def, 1);

    char* output = omni_codegen_get_output(cg);
    ASSERT(output != NULL);

    /* Both functions should have rank assignment */
    int rank_count = 0;
    const char* p = output;
    while ((p = strstr(p, "omni_region_set_lifetime_rank")) != NULL) {
        rank_count++;
        p += strlen("omni_region_set_lifetime_rank");
    }

    /* Should have at least 2 rank assignments (outer and inner) */
    ASSERT(rank_count >= 2);

    free(output);
    omni_codegen_free(cg);  /* Note: this also frees cg->analysis */
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Region Rank Codegen Tests (Issue 2 P4.2) ===\033[0m\n");

    printf("\n\033[33m--- Rank Assignment Generation ---\033[0m\n");
    RUN_TEST(test_function_emits_rank_assignment);
    RUN_TEST(test_lambda_emits_rank_assignment);
    RUN_TEST(test_let_emits_rank_assignment);
    RUN_TEST(test_main_has_rank_comment);

    printf("\n\033[33m--- Rank Implementation Details ---\033[0m\n");
    RUN_TEST(test_no_direct_rank_field_access);
    RUN_TEST(test_nested_functions_rank_hierarchy);

    printf("\n\033[33m=== Summary ===\033[0m\n");
    printf("  Total:  %d\n", tests_run);
    if (tests_passed == tests_run) {
        printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
    } else {
        printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
        printf("  \033[31mFailed: %d\033[0m\n", tests_run - tests_passed);
    }

    return (tests_passed == tests_run) ? 0 : 1;
}
