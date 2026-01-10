/*
 * Type Codegen Tests
 *
 * Tests for generating C code from type definitions.
 * Verifies struct, constructor, accessor, mutator, and release generation.
 */

#define _POSIX_C_SOURCE 200809L

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

#define ASSERT_CONTAINS(haystack, needle) do { \
    if (!strstr((haystack), (needle))) { \
        printf("\033[31mFAIL\033[0m (line %d: expected '%s' in output)\n", __LINE__, needle); \
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

/* ========== Struct Generation ========== */

TEST(test_struct_generation) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register type: (deftype Point (x int) (y int)) */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Point"),
        mk_list2(mk_sym("x"), mk_sym("int")),
        mk_list2(mk_sym("y"), mk_sym("int"))
    );

    TypeDef* type = omni_register_type(analysis, type_def);
    ASSERT(type != NULL);

    /* Generate struct */
    omni_codegen_type_struct(ctx, type);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check struct definition */
    ASSERT_CONTAINS(output, "typedef struct Point");
    ASSERT_CONTAINS(output, "int tag");
    ASSERT_CONTAINS(output, "int rc");
    ASSERT_CONTAINS(output, "int gen");
    ASSERT_CONTAINS(output, "Obj* x");
    ASSERT_CONTAINS(output, "Obj* y");
    ASSERT_CONTAINS(output, "} Point");

    free(output);
    /* omni_codegen_free frees the analysis, so don't free it separately */
    omni_codegen_free(ctx);
}

TEST(test_struct_with_weak_field) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register type: (deftype Node (value int) (parent Node :weak)) */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Node"),
        mk_list2(mk_sym("value"), mk_sym("int")),
        mk_list3(mk_sym("parent"), mk_sym("Node"), mk_sym(":weak"))
    );

    TypeDef* type = omni_register_type(analysis, type_def);
    ASSERT(type != NULL);

    /* Generate struct */
    omni_codegen_type_struct(ctx, type);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check struct has weak field comment */
    ASSERT_CONTAINS(output, "typedef struct Node");
    ASSERT_CONTAINS(output, "parent");
    ASSERT_CONTAINS(output, "/* weak");

    free(output);
    omni_codegen_free(ctx);
}

/* ========== Constructor Generation ========== */

TEST(test_constructor_generation) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register type: (deftype Point (x int) (y int)) */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Point"),
        mk_list2(mk_sym("x"), mk_sym("int")),
        mk_list2(mk_sym("y"), mk_sym("int"))
    );

    TypeDef* type = omni_register_type(analysis, type_def);
    ASSERT(type != NULL);

    /* Generate constructor */
    omni_codegen_type_constructor(ctx, type);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check constructor function */
    ASSERT_CONTAINS(output, "Point* mk_Point");
    ASSERT_CONTAINS(output, "Obj* x");
    ASSERT_CONTAINS(output, "Obj* y");
    ASSERT_CONTAINS(output, "malloc(sizeof(Point))");
    ASSERT_CONTAINS(output, "obj->tag = TAG_Point");
    ASSERT_CONTAINS(output, "obj->rc = 1");
    ASSERT_CONTAINS(output, "inc_ref(x)");
    ASSERT_CONTAINS(output, "inc_ref(y)");

    free(output);
    omni_codegen_free(ctx);
}

TEST(test_constructor_weak_field) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register type with weak field */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Node"),
        mk_list2(mk_sym("value"), mk_sym("int")),
        mk_list3(mk_sym("parent"), mk_sym("Node"), mk_sym(":weak"))
    );

    TypeDef* type = omni_register_type(analysis, type_def);
    ASSERT(type != NULL);

    /* Generate constructor */
    omni_codegen_type_constructor(ctx, type);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check weak field comment (no inc_ref for weak fields) */
    ASSERT_CONTAINS(output, "/* weak");

    free(output);
    omni_codegen_free(ctx);
}

/* ========== Accessor Generation ========== */

TEST(test_accessor_generation) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register type */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Point"),
        mk_list2(mk_sym("x"), mk_sym("int")),
        mk_list2(mk_sym("y"), mk_sym("int"))
    );

    TypeDef* type = omni_register_type(analysis, type_def);
    ASSERT(type != NULL);

    /* Generate accessor for 'x' field */
    TypeField* x_field = omni_get_type_field(type, "x");
    ASSERT(x_field != NULL);

    omni_codegen_type_accessor(ctx, type, x_field);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check accessor function */
    ASSERT_CONTAINS(output, "Obj* Point_x");
    ASSERT_CONTAINS(output, "Point* obj");
    ASSERT_CONTAINS(output, "obj->x");

    free(output);
    omni_codegen_free(ctx);
}

/* ========== Mutator Generation ========== */

TEST(test_mutator_generation) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register type */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Point"),
        mk_list2(mk_sym("x"), mk_sym("int")),
        mk_list2(mk_sym("y"), mk_sym("int"))
    );

    TypeDef* type = omni_register_type(analysis, type_def);
    ASSERT(type != NULL);

    /* Generate mutator for 'x' field */
    TypeField* x_field = omni_get_type_field(type, "x");
    ASSERT(x_field != NULL);

    omni_codegen_type_mutator(ctx, type, x_field);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check mutator function */
    ASSERT_CONTAINS(output, "void set_Point_x");
    ASSERT_CONTAINS(output, "Point* obj");
    ASSERT_CONTAINS(output, "Obj* val");
    ASSERT_CONTAINS(output, "obj->x = val");

    free(output);
    omni_codegen_free(ctx);
}

TEST(test_mutator_weak_field) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register type with weak field */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Node"),
        mk_list2(mk_sym("value"), mk_sym("int")),
        mk_list3(mk_sym("parent"), mk_sym("Node"), mk_sym(":weak"))
    );

    TypeDef* type = omni_register_type(analysis, type_def);
    ASSERT(type != NULL);

    /* Generate mutator for 'parent' (weak) field */
    TypeField* parent_field = omni_get_type_field(type, "parent");
    ASSERT(parent_field != NULL);

    omni_codegen_type_mutator(ctx, type, parent_field);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check weak mutator uses SET_WEAK */
    ASSERT_CONTAINS(output, "SET_WEAK");

    free(output);
    omni_codegen_free(ctx);
}

/* ========== Release Function Generation ========== */

TEST(test_release_generation) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register type */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Point"),
        mk_list2(mk_sym("x"), mk_sym("int")),
        mk_list2(mk_sym("y"), mk_sym("int"))
    );

    TypeDef* type = omni_register_type(analysis, type_def);
    ASSERT(type != NULL);

    /* Generate release function */
    omni_codegen_type_release(ctx, type);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check release function */
    ASSERT_CONTAINS(output, "void release_Point");
    ASSERT_CONTAINS(output, "Point* obj");
    ASSERT_CONTAINS(output, "dec_ref(obj->x)");
    ASSERT_CONTAINS(output, "dec_ref(obj->y)");
    ASSERT_CONTAINS(output, "free(obj)");

    free(output);
    omni_codegen_free(ctx);
}

TEST(test_release_skips_weak_fields) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register type with one weak and one strong field */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Node"),
        mk_list2(mk_sym("value"), mk_sym("int")),
        mk_list3(mk_sym("parent"), mk_sym("Node"), mk_sym(":weak"))
    );

    TypeDef* type = omni_register_type(analysis, type_def);
    ASSERT(type != NULL);

    /* Generate release function */
    omni_codegen_type_release(ctx, type);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check release decrements value but skips parent */
    ASSERT_CONTAINS(output, "dec_ref(obj->value)");
    /* Should have a comment about skipping weak field */
    ASSERT_CONTAINS(output, "/* skip parent");

    free(output);
    omni_codegen_free(ctx);
}

/* ========== Full Type Generation ========== */

TEST(test_full_type_generation) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register type */
    OmniValue* type_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Point"),
        mk_list2(mk_sym("x"), mk_sym("int")),
        mk_list2(mk_sym("y"), mk_sym("int"))
    );

    TypeDef* type = omni_register_type(analysis, type_def);
    ASSERT(type != NULL);

    /* Generate full type code */
    omni_codegen_type_full(ctx, type);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check all parts are present */
    ASSERT_CONTAINS(output, "typedef struct Point");  /* Struct */
    ASSERT_CONTAINS(output, "Point* mk_Point");       /* Constructor */
    ASSERT_CONTAINS(output, "Obj* Point_x");          /* Accessor x */
    ASSERT_CONTAINS(output, "Obj* Point_y");          /* Accessor y */
    /* Mutators only generated for mutable fields */
    ASSERT_CONTAINS(output, "release_Point");         /* Release */

    free(output);
    omni_codegen_free(ctx);
}

/* ========== All Types Generation ========== */

TEST(test_all_types_generation) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register multiple types */
    OmniValue* point_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Point"),
        mk_list2(mk_sym("x"), mk_sym("int")),
        mk_list2(mk_sym("y"), mk_sym("int"))
    );

    OmniValue* rect_def = mk_list4(
        mk_sym("deftype"),
        mk_sym("Rect"),
        mk_list2(mk_sym("topLeft"), mk_sym("Point")),
        mk_list2(mk_sym("bottomRight"), mk_sym("Point"))
    );

    omni_register_type(analysis, point_def);
    omni_register_type(analysis, rect_def);

    /* Generate all types */
    omni_codegen_all_types(ctx);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check both types are present */
    ASSERT_CONTAINS(output, "typedef struct Point");
    ASSERT_CONTAINS(output, "typedef struct Rect");
    ASSERT_CONTAINS(output, "mk_Point");
    ASSERT_CONTAINS(output, "mk_Rect");

    free(output);
    omni_codegen_free(ctx);
}

/* ========== Shape-Aware Codegen ========== */

TEST(test_shape_helpers_generation) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Generate shape helpers */
    omni_codegen_shape_helpers(ctx);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check arena type and functions */
    ASSERT_CONTAINS(output, "ArenaBlock");
    ASSERT_CONTAINS(output, "Arena");
    ASSERT_CONTAINS(output, "arena_create");
    ASSERT_CONTAINS(output, "arena_alloc");
    ASSERT_CONTAINS(output, "arena_destroy");

    free(output);
    omni_codegen_free(ctx);
}

TEST(test_shape_aware_tree_free) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register a tree-shaped type: (deftype BinTree (value int) (left BinTree) (right BinTree)) */
    OmniValue* tree_def = mk_cons(
        mk_sym("deftype"),
        mk_cons(mk_sym("BinTree"),
        mk_cons(mk_list2(mk_sym("value"), mk_sym("int")),
        mk_cons(mk_list2(mk_sym("left"), mk_sym("BinTree")),
        mk_cons(mk_list2(mk_sym("right"), mk_sym("BinTree")),
        omni_nil))))
    );

    TypeDef* type = omni_register_type(analysis, tree_def);
    ASSERT(type != NULL);

    /* Mark as SHAPE_TREE for testing */
    type->shape = SHAPE_TREE;

    /* Generate shape-aware free */
    omni_codegen_shape_aware_free(ctx, type);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check tree-recursive free */
    ASSERT_CONTAINS(output, "free_tree_BinTree");
    ASSERT_CONTAINS(output, "TREE");

    free(output);
    omni_codegen_free(ctx);
}

TEST(test_shape_aware_cyclic) {
    AnalysisContext* analysis = omni_analysis_new();
    CodeGenContext* ctx = omni_codegen_new_buffer();
    ctx->analysis = analysis;

    /* Register a cyclic type: (deftype DLL (value int) (next DLL) (prev DLL)) */
    OmniValue* dll_def = mk_cons(
        mk_sym("deftype"),
        mk_cons(mk_sym("DLL"),
        mk_cons(mk_list2(mk_sym("value"), mk_sym("int")),
        mk_cons(mk_list2(mk_sym("next"), mk_sym("DLL")),
        mk_cons(mk_list2(mk_sym("prev"), mk_sym("DLL")),
        omni_nil))))
    );

    TypeDef* type = omni_register_type(analysis, dll_def);
    ASSERT(type != NULL);

    /* Build graph and analyze back-edges to detect cycles */
    omni_build_ownership_graph(analysis);
    omni_analyze_back_edges(analysis);

    /* Should be marked CYCLIC due to prev field */
    ASSERT(type->shape == SHAPE_CYCLIC);

    /* Generate shape-aware free */
    omni_codegen_shape_aware_free(ctx, type);

    char* output = omni_codegen_get_output(ctx);
    ASSERT(output != NULL);

    /* Check cyclic handling */
    ASSERT_CONTAINS(output, "CYCLIC");

    free(output);
    omni_codegen_free(ctx);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Type Codegen Tests ===\033[0m\n");

    printf("\n\033[33m--- Struct Generation ---\033[0m\n");
    RUN_TEST(test_struct_generation);
    RUN_TEST(test_struct_with_weak_field);

    printf("\n\033[33m--- Constructor Generation ---\033[0m\n");
    RUN_TEST(test_constructor_generation);
    RUN_TEST(test_constructor_weak_field);

    printf("\n\033[33m--- Accessor Generation ---\033[0m\n");
    RUN_TEST(test_accessor_generation);

    printf("\n\033[33m--- Mutator Generation ---\033[0m\n");
    RUN_TEST(test_mutator_generation);
    RUN_TEST(test_mutator_weak_field);

    printf("\n\033[33m--- Release Generation ---\033[0m\n");
    RUN_TEST(test_release_generation);
    RUN_TEST(test_release_skips_weak_fields);

    printf("\n\033[33m--- Full Type Generation ---\033[0m\n");
    RUN_TEST(test_full_type_generation);
    RUN_TEST(test_all_types_generation);

    printf("\n\033[33m--- Shape-Aware Codegen ---\033[0m\n");
    RUN_TEST(test_shape_helpers_generation);
    RUN_TEST(test_shape_aware_tree_free);
    RUN_TEST(test_shape_aware_cyclic);

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
