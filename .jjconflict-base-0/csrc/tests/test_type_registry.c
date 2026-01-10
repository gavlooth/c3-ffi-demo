/*
 * Type Registry Tests
 *
 * Tests for deftype parsing, type registry population, and
 * automatic back-edge detection for weak references.
 */

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

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

static OmniValue* mk_list4(OmniValue* a, OmniValue* b, OmniValue* c, OmniValue* d) {
    return mk_cons(a, mk_cons(b, mk_cons(c, mk_cons(d, omni_nil))));
}

/* ========== Type Registry Creation ========== */

TEST(test_registry_new) {
    TypeRegistry* reg = omni_type_registry_new();
    ASSERT(reg != NULL);
    ASSERT(reg->types == NULL);
    ASSERT(reg->edges == NULL);
    ASSERT(reg->type_count == 0);
    omni_type_registry_free(reg);
}

TEST(test_register_simple_type) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (deftype Point (x int) (y int)) */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Point"),
        mk_list2(mk_sym("x"), mk_sym("int")),
        mk_list2(mk_sym("y"), mk_sym("int"))
    );

    TypeDef* type = omni_register_type(ctx, type_def);
    ASSERT(type != NULL);
    ASSERT(strcmp(type->name, "Point") == 0);
    ASSERT(type->field_count == 2);

    TypeField* x = omni_get_type_field(type, "x");
    ASSERT(x != NULL);
    ASSERT(strcmp(x->name, "x") == 0);
    ASSERT(x->strength == FIELD_STRONG);

    TypeField* y = omni_get_type_field(type, "y");
    ASSERT(y != NULL);
    ASSERT(strcmp(y->name, "y") == 0);
    ASSERT(y->strength == FIELD_STRONG);

    omni_analysis_free(ctx);
}

TEST(test_get_type) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Register a type */
    OmniValue* type_def = mk_list3(
        mk_sym("deftype"),
        mk_sym("Box"),
        mk_list2(mk_sym("value"), mk_sym("int"))
    );
    omni_register_type(ctx, type_def);

    /* Get it back */
    TypeDef* type = omni_get_type(ctx, "Box");
    ASSERT(type != NULL);
    ASSERT(strcmp(type->name, "Box") == 0);

    /* Non-existent type */
    TypeDef* missing = omni_get_type(ctx, "NotAType");
    ASSERT(missing == NULL);

    omni_analysis_free(ctx);
}

/* ========== Field Strength Detection ========== */

TEST(test_explicit_weak_field) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (deftype Node (value int) (parent Node :weak)) */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Node"),
        mk_list2(mk_sym("value"), mk_sym("int")),
        mk_list3(mk_sym("parent"), mk_sym("Node"), mk_sym(":weak"))
    );

    TypeDef* type = omni_register_type(ctx, type_def);
    ASSERT(type != NULL);

    /* value should be strong */
    ASSERT(omni_get_field_strength(ctx, "Node", "value") == FIELD_STRONG);

    /* parent should be weak (explicit) */
    ASSERT(omni_get_field_strength(ctx, "Node", "parent") == FIELD_WEAK);
    ASSERT(omni_is_weak_field(ctx, "Node", "parent") == true);

    omni_analysis_free(ctx);
}

TEST(test_auto_weak_by_name) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (deftype Node (value int) (prev Node)) */
    /* prev should be auto-detected as weak by naming convention */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Node"),
        mk_list2(mk_sym("value"), mk_sym("int")),
        mk_list2(mk_sym("prev"), mk_sym("Node"))
    );

    TypeDef* type = omni_register_type(ctx, type_def);
    ASSERT(type != NULL);

    /* prev should be auto-detected as weak */
    ASSERT(omni_is_weak_field(ctx, "Node", "prev") == true);

    omni_analysis_free(ctx);
}

TEST(test_doubly_linked_list) {
    AnalysisContext* ctx = omni_analysis_new();

    /* (deftype DLLNode (value int) (next DLLNode) (prev DLLNode)) */
    OmniValue* type_def = mk_cons(
        mk_sym("deftype"),
        mk_cons(mk_sym("DLLNode"),
        mk_cons(mk_list2(mk_sym("value"), mk_sym("int")),
        mk_cons(mk_list2(mk_sym("next"), mk_sym("DLLNode")),
        mk_cons(mk_list2(mk_sym("prev"), mk_sym("DLLNode")),
        omni_nil))))
    );

    TypeDef* type = omni_register_type(ctx, type_def);
    ASSERT(type != NULL);
    ASSERT(type->field_count == 3);

    /* next should be strong */
    ASSERT(omni_get_field_strength(ctx, "DLLNode", "next") == FIELD_STRONG);

    /* prev should be weak (auto-detected) */
    ASSERT(omni_get_field_strength(ctx, "DLLNode", "prev") == FIELD_WEAK);

    omni_analysis_free(ctx);
}

TEST(test_field_strength_names) {
    ASSERT(strcmp(omni_field_strength_name(FIELD_STRONG), "strong") == 0);
    ASSERT(strcmp(omni_field_strength_name(FIELD_WEAK), "weak") == 0);
    ASSERT(strcmp(omni_field_strength_name(FIELD_BORROWED), "borrowed") == 0);
}

/* ========== Ownership Graph ========== */

TEST(test_ownership_graph_simple) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Register two types that reference each other */
    OmniValue* person = mk_list4(
        mk_sym("deftype"),
        mk_sym("Person"),
        mk_list2(mk_sym("name"), mk_sym("string")),
        mk_list2(mk_sym("company"), mk_sym("Company"))
    );

    OmniValue* company = mk_list4(
        mk_sym("deftype"),
        mk_sym("Company"),
        mk_list2(mk_sym("name"), mk_sym("string")),
        mk_list2(mk_sym("ceo"), mk_sym("Person"))
    );

    omni_register_type(ctx, person);
    omni_register_type(ctx, company);

    /* Build ownership graph */
    omni_build_ownership_graph(ctx);

    ASSERT(ctx->type_registry->graph_built == true);
    ASSERT(ctx->type_registry->edges != NULL);

    omni_analysis_free(ctx);
}

TEST(test_back_edge_analysis) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Self-referential type - naming convention determines weak */
    OmniValue* tree = mk_cons(
        mk_sym("deftype"),
        mk_cons(mk_sym("TreeNode"),
        mk_cons(mk_list2(mk_sym("value"), mk_sym("int")),
        mk_cons(mk_list2(mk_sym("left"), mk_sym("TreeNode")),
        mk_cons(mk_list2(mk_sym("parent"), mk_sym("TreeNode")),
        omni_nil))))
    );

    omni_register_type(ctx, tree);
    omni_analyze_back_edges(ctx);

    ASSERT(ctx->type_registry->back_edges_analyzed == true);

    /* parent should be weak (by naming convention) */
    ASSERT(omni_is_weak_field(ctx, "TreeNode", "parent") == true);

    /* left should be strong (not a back-edge name) */
    ASSERT(omni_is_weak_field(ctx, "TreeNode", "left") == false);

    omni_analysis_free(ctx);
}

/* ========== Shape Analysis Integration ========== */

TEST(test_shape_analysis_tree) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Binary tree - should be SHAPE_TREE */
    OmniValue* tree = mk_cons(
        mk_sym("deftype"),
        mk_cons(mk_sym("BinTree"),
        mk_cons(mk_list2(mk_sym("value"), mk_sym("int")),
        mk_cons(mk_list2(mk_sym("left"), mk_sym("BinTree")),
        mk_cons(mk_list2(mk_sym("right"), mk_sym("BinTree")),
        omni_nil))))
    );

    omni_register_type(ctx, tree);

    ShapeClass shape = omni_get_type_shape(ctx, "BinTree");
    /* Without back-edge, self-ref becomes DAG */
    ASSERT(shape == SHAPE_DAG || shape == SHAPE_TREE);

    omni_analysis_free(ctx);
}

TEST(test_shape_analysis_cyclic) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Doubly-linked list - should be SHAPE_CYCLIC */
    OmniValue* node = mk_cons(
        mk_sym("deftype"),
        mk_cons(mk_sym("DLL"),
        mk_cons(mk_list2(mk_sym("value"), mk_sym("int")),
        mk_cons(mk_list2(mk_sym("next"), mk_sym("DLL")),
        mk_cons(mk_list2(mk_sym("prev"), mk_sym("DLL")),
        omni_nil))))
    );

    omni_register_type(ctx, node);

    ShapeClass shape = omni_get_type_shape(ctx, "DLL");
    ASSERT(shape == SHAPE_CYCLIC);

    omni_analysis_free(ctx);
}

/* ========== Back-Edge Pattern Detection ========== */

TEST(test_back_edge_patterns) {
    /* Test the naming convention detection */
    ASSERT(omni_is_back_edge_pattern("parent") == true);
    ASSERT(omni_is_back_edge_pattern("prev") == true);
    ASSERT(omni_is_back_edge_pattern("back") == true);
    ASSERT(omni_is_back_edge_pattern("up") == true);
    ASSERT(omni_is_back_edge_pattern("owner") == true);
    ASSERT(omni_is_back_edge_pattern("container") == true);

    /* Non-back-edge names */
    ASSERT(omni_is_back_edge_pattern("next") == false);
    ASSERT(omni_is_back_edge_pattern("child") == false);
    ASSERT(omni_is_back_edge_pattern("data") == false);
    ASSERT(omni_is_back_edge_pattern("value") == false);
}

/* ========== Multiple Types ========== */

TEST(test_multiple_types) {
    AnalysisContext* ctx = omni_analysis_new();

    /* Register multiple types */
    OmniValue* point = mk_list4(
        mk_sym("deftype"),
        mk_sym("Point"),
        mk_list2(mk_sym("x"), mk_sym("int")),
        mk_list2(mk_sym("y"), mk_sym("int"))
    );

    OmniValue* rect = mk_cons(
        mk_sym("deftype"),
        mk_cons(mk_sym("Rect"),
        mk_cons(mk_list2(mk_sym("topLeft"), mk_sym("Point")),
        mk_cons(mk_list2(mk_sym("bottomRight"), mk_sym("Point")),
        omni_nil)))
    );

    omni_register_type(ctx, point);
    omni_register_type(ctx, rect);

    ASSERT(ctx->type_registry->type_count == 2);
    ASSERT(omni_get_type(ctx, "Point") != NULL);
    ASSERT(omni_get_type(ctx, "Rect") != NULL);

    omni_analysis_free(ctx);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Type Registry Tests ===\033[0m\n");

    printf("\n\033[33m--- Registry Creation ---\033[0m\n");
    RUN_TEST(test_registry_new);
    RUN_TEST(test_register_simple_type);
    RUN_TEST(test_get_type);

    printf("\n\033[33m--- Field Strength Detection ---\033[0m\n");
    RUN_TEST(test_explicit_weak_field);
    RUN_TEST(test_auto_weak_by_name);
    RUN_TEST(test_doubly_linked_list);
    RUN_TEST(test_field_strength_names);

    printf("\n\033[33m--- Ownership Graph ---\033[0m\n");
    RUN_TEST(test_ownership_graph_simple);
    RUN_TEST(test_back_edge_analysis);

    printf("\n\033[33m--- Shape Analysis Integration ---\033[0m\n");
    RUN_TEST(test_shape_analysis_tree);
    RUN_TEST(test_shape_analysis_cyclic);

    printf("\n\033[33m--- Back-Edge Pattern Detection ---\033[0m\n");
    RUN_TEST(test_back_edge_patterns);

    printf("\n\033[33m--- Multiple Types ---\033[0m\n");
    RUN_TEST(test_multiple_types);

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
