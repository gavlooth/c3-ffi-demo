/*
 * test_type_env.c
 *
 * Purpose:
 *   Unit tests for the type environment implementation (type_env.c).
 *   The type environment tracks concrete types for variables in lexical scopes,
 *   supporting parent chain lookup for nested scopes.
 *
 * Why this matters:
 *   Type environments are core to the type specialization system (Phase 27).
 *   The lookup operation must correctly implement lexical scoping: find variable
 *   in current scope first, then search parent scopes recursively.
 *
 * Contract:
 *   - Lookup finds variables in current scope
 *   - Lookup finds variables in parent scopes when not in current scope
 *   - Lookup returns NULL when variable doesn't exist in any scope
 *   - Multiple variables in scope are accessible via parent chain
 */

#include "../analysis/type_env.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------- Test Helpers ----------------------------- */

static void assert_type_is_primitive(ConcreteType* type, PrimitiveType prim, int bit_width) {
    assert(type);
    assert(type->kind == TYPE_KIND_PRIMITIVE);
    assert(type->primitive.prim == prim);
    assert(type->primitive.bit_width == bit_width);
}

/* ------------------------------- Test Cases ------------------------------ */

static void test_type_env_lookup_in_current_scope(void) {
    /*
     * Test that lookup finds a variable defined in the current scope.
     */
    TypeEnv* env = type_env_new(NULL);
    assert(env);

    /* Create and bind an Int64 type to variable "x" */
    ConcreteType* int_type = concrete_type_primitive(PRIMITIVE_INT64, 64);
    assert(int_type);

    type_env_bind(env, "x", int_type);

    /* Lookup should find "x" in current scope */
    ConcreteType* found = type_env_lookup(env, "x");
    assert(found);
    assert_type_is_primitive(found, PRIMITIVE_INT64, 64);

    /* Variable should not be found if it doesn't exist */
    ConcreteType* not_found = type_env_lookup(env, "y");
    assert(not_found == NULL);

    /* Cleanup */
    /* Note: type_env_free decrements ref_count on bound types, so we don't
     * need to manually call concrete_type_dec_ref for int_type. */
    type_env_free(env);
}

static void test_type_env_lookup_in_parent_scope(void) {
    /*
     * Test that lookup finds variables defined in parent scopes.
     * This tests lexical scoping behavior.
     */
    /* Create outer scope with variable "x" */
    TypeEnv* outer = type_env_new(NULL);
    assert(outer);

    ConcreteType* int_type = concrete_type_primitive(PRIMITIVE_INT64, 64);
    assert(int_type);

    type_env_bind(outer, "x", int_type);

    /* Create inner scope (child) with no bindings */
    TypeEnv* inner = type_env_push(outer);
    assert(inner);
    assert(inner->parent == outer);

    /* Lookup in inner scope should find "x" from parent */
    ConcreteType* found = type_env_lookup(inner, "x");
    assert(found);
    assert_type_is_primitive(found, PRIMITIVE_INT64, 64);

    /* Cleanup */
    type_env_free(inner);  /* Does NOT free outer */
    /* Note: int_type is bound to outer, so type_env_free(outer) will
     * decrement its ref_count. Don't call concrete_type_dec_ref here. */
    type_env_free(outer);
}

static void test_type_env_lookup_shadowing(void) {
    /*
     * Test that inner scope variables shadow outer scope variables.
     * Variable defined in inner scope should be found, not outer one.
     */
    /* Create outer scope with "x" as Int64 */
    TypeEnv* outer = type_env_new(NULL);
    assert(outer);

    ConcreteType* int_type = concrete_type_primitive(PRIMITIVE_INT64, 64);
    assert(int_type);
    type_env_bind(outer, "x", int_type);

    /* Create inner scope with "x" as Float64 (shadowing) */
    TypeEnv* inner = type_env_push(outer);
    assert(inner);

    ConcreteType* float_type = concrete_type_primitive(PRIMITIVE_FLOAT64, 64);
    assert(float_type);
    type_env_bind(inner, "x", float_type);

    /* Lookup in inner scope should find inner "x" (Float64), not outer (Int64) */
    ConcreteType* found = type_env_lookup(inner, "x");
    assert(found);
    assert_type_is_primitive(found, PRIMITIVE_FLOAT64, 64);

    /* Lookup in outer scope should still find outer "x" (Int64) */
    ConcreteType* outer_found = type_env_lookup(outer, "x");
    assert(outer_found);
    assert_type_is_primitive(outer_found, PRIMITIVE_INT64, 64);

    /* Cleanup */
    /* Each type is bound to exactly one environment (inner or outer),
     * so type_env_free will correctly decrement each type's ref_count. */
    type_env_free(inner);
    type_env_free(outer);
}

static void test_type_env_lookup_not_found(void) {
    /*
     * Test that lookup returns NULL for variables that don't exist
     * in any scope in the chain.
     */
    TypeEnv* env = type_env_new(NULL);
    assert(env);

    /* Empty environment - no variables defined */
    ConcreteType* not_found = type_env_lookup(env, "nonexistent");
    assert(not_found == NULL);

    /* Create nested scopes, still no variables */
    TypeEnv* inner = type_env_push(env);
    assert(inner);

    not_found = type_env_lookup(inner, "still_not_found");
    assert(not_found == NULL);

    /* Cleanup */
    type_env_free(inner);
    type_env_free(env);
}

static void test_type_env_lookup_multiple_bindings(void) {
    /*
     * Test lookup in a scope with multiple variable bindings.
     */
    TypeEnv* env = type_env_new(NULL);
    assert(env);

    /* Bind multiple variables */
    ConcreteType* int_type = concrete_type_primitive(PRIMITIVE_INT64, 64);
    ConcreteType* float_type = concrete_type_primitive(PRIMITIVE_FLOAT64, 64);
    ConcreteType* bool_type = concrete_type_primitive(PRIMITIVE_BOOL, 1);

    type_env_bind(env, "x", int_type);
    type_env_bind(env, "y", float_type);
    type_env_bind(env, "z", bool_type);

    /* Should find each variable */
    ConcreteType* found_x = type_env_lookup(env, "x");
    assert(found_x);
    assert_type_is_primitive(found_x, PRIMITIVE_INT64, 64);

    ConcreteType* found_y = type_env_lookup(env, "y");
    assert(found_y);
    assert_type_is_primitive(found_y, PRIMITIVE_FLOAT64, 64);

    ConcreteType* found_z = type_env_lookup(env, "z");
    assert(found_z);
    assert_type_is_primitive(found_z, PRIMITIVE_BOOL, 1);

    /* Cleanup */
    /* All three types are bound to the same environment, so
     * type_env_free will correctly decrement each type's ref_count. */
    type_env_free(env);
}

int main(void) {
    test_type_env_lookup_in_current_scope();
    test_type_env_lookup_in_parent_scope();
    test_type_env_lookup_shadowing();
    test_type_env_lookup_not_found();
    test_type_env_lookup_multiple_bindings();

    printf("PASS: test_type_env\n");
    return 0;
}
