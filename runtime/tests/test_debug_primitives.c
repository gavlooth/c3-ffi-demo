/**
 * @file test_debug_primitives.c
 * @brief Tests for developer debugging primitives (Issue 27 P0)
 *
 * Tests prim_type_of and other debug primitives from runtime/src/debug.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/omni.h"

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

#define ASSERT_NOT_NULL(ptr) ASSERT((ptr) != NULL)

/* ============================================================
 * Helper to extract string from keyword
 * ============================================================ */
static const char* keyword_to_string(Obj* kw) {
    if (!kw || !IS_BOXED(kw) || kw->tag != TAG_KEYWORD) {
        return "<not-a-keyword>";
    }
    return (const char*)kw->ptr;
}

/* ============================================================
 * Test: prim_type_of for immediate types
 * ============================================================ */
TEST(type_of_immediate_int) {
    Obj* num = mk_int(42);
    Obj* type_obj = prim_type_of(num);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "int");
}

TEST(type_of_immediate_bool_true) {
    Obj* bool_val = mk_bool(true);
    Obj* type_obj = prim_type_of(bool_val);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "bool");
}

TEST(type_of_immediate_bool_false) {
    Obj* bool_val = mk_bool(false);
    Obj* type_obj = prim_type_of(bool_val);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "bool");
}

TEST(type_of_immediate_char) {
    Obj* char_val = mk_char('A');
    Obj* type_obj = prim_type_of(char_val);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "char");
}

TEST(type_of_nil) {
    Obj* type_obj = prim_type_of(NULL);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "nil");
}

TEST(type_of_nothing) {
    Obj* nothing_val = mk_nothing();
    Obj* type_obj = prim_type_of(nothing_val);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "nothing");
}

/* ============================================================
 * Test: prim_type_of for boxed types
 * ============================================================ */
TEST(type_of_string) {
    Obj* str = mk_string("hello world");
    Obj* type_obj = prim_type_of(str);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "string");
}

TEST(type_of_symbol) {
    Obj* sym = mk_sym("my-symbol");
    Obj* type_obj = prim_type_of(sym);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "symbol");
}

TEST(type_of_keyword) {
    Obj* kw = mk_keyword("my-keyword");
    Obj* type_obj = prim_type_of(kw);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "keyword");
}

TEST(type_of_pair) {
    Obj* pair = mk_pair(mk_int(1), mk_int(2));
    Obj* type_obj = prim_type_of(pair);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "pair");
}

TEST(type_of_empty_list) {
    /* Empty list is NULL, should return :nil */
    Obj* type_obj = prim_type_of(NULL);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "nil");
}

TEST(type_of_array) {
    /* Create simple array with 3 elements */
    Obj* arr = mk_array(3);
    arr->tag = TAG_ARRAY;
    array_set(arr, 0, mk_int(1));
    array_set(arr, 1, mk_int(2));
    array_set(arr, 2, mk_int(3));
    
    Obj* type_obj = prim_type_of(arr);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "array");
}

TEST(type_of_dict) {
    Obj* dict = mk_dict();
    
    Obj* type_obj = prim_type_of(dict);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "dict");
}

TEST(type_of_box) {
    Obj* boxed = mk_box(mk_int(42));
    
    Obj* type_obj = prim_type_of(boxed);
    
    ASSERT_NOT_NULL(type_obj);
    ASSERT(IS_BOXED(type_obj));
    ASSERT(type_obj->tag == TAG_KEYWORD);
    
    const char* type_str = keyword_to_string(type_obj);
    ASSERT_STR_EQ(type_str, "box");
}

/* ============================================================
 * Test: prim_type_of for function types
 * ============================================================ */
TEST(type_of_closure) {
    /* Skip closure test for now - complex API requiring proper closure creation */
    /* This test can be added later with proper closure construction */
    printf(" SKIPPED (closure creation API TBD)\n");
}

/* ============================================================
 * Main
 * ============================================================ */
int main(void) {
    printf("\n=== Debug Primitives Tests (Issue 27 P0) ===\n\n");
    
    /* Immediate types */
    RUN_TEST(type_of_immediate_int);
    RUN_TEST(type_of_immediate_bool_true);
    RUN_TEST(type_of_immediate_bool_false);
    RUN_TEST(type_of_immediate_char);
    RUN_TEST(type_of_nil);
    RUN_TEST(type_of_nothing);
    
    /* Boxed types */
    RUN_TEST(type_of_string);
    RUN_TEST(type_of_symbol);
    RUN_TEST(type_of_keyword);
    RUN_TEST(type_of_pair);
    RUN_TEST(type_of_empty_list);
    RUN_TEST(type_of_array);
    RUN_TEST(type_of_dict);
    RUN_TEST(type_of_box);
    
    /* Function types */
    RUN_TEST(type_of_closure);
    
    printf("\n=== Summary ===\n");
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}
