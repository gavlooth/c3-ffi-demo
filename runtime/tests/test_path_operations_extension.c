/*
 * test_path_operations_extension.c - Tests for prim_path_extension
 *
 * Tests the file extension extraction function:
 * - prim_path_extension - Extract file extension from path
 */

#include "test_framework.h"

/* Helper to extract string value from Obj */
static const char* test_obj_to_cstr(Obj* obj) {
    if (!obj) return NULL;
    if (IS_IMMEDIATE(obj)) return NULL;
    if (obj->tag == TAG_STRING || obj->tag == TAG_SYM) {
        return obj->ptr ? (const char*)obj->ptr : "";
    }
    return NULL;
}

/* ========== prim_path_extension Tests ========== */

void test_path_extension_simple_filename(void) {
    /* Test simple filename with extension */
    Obj* path = mk_string("file.txt");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), ".txt");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_full_path(void) {
    /* Test full path with extension */
    Obj* path = mk_string("/home/user/file.txt");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), ".txt");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_no_extension(void) {
    /* Test filename without extension */
    Obj* path = mk_string("README");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_hidden_file(void) {
    /* Test hidden file (starts with dot, no extension) */
    Obj* path = mk_string(".hidden");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_hidden_file_with_extension(void) {
    /* Test hidden file that also has extension */
    Obj* path = mk_string(".config.json");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    /* Dot after the initial dot is the extension separator */
    ASSERT_STR_EQ(test_obj_to_cstr(result), ".json");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_multiple_extensions(void) {
    /* Test file with multiple extensions */
    Obj* path = mk_string("archive.tar.gz");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    /* Should return the last extension only */
    ASSERT_STR_EQ(test_obj_to_cstr(result), ".gz");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_nested_path(void) {
    /* Test nested directory path with extension */
    Obj* path = mk_string("/a/b/c/d/file.txt");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), ".txt");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_trailing_slash(void) {
    /* Test directory with trailing slash (no extension) */
    Obj* path = mk_string("/home/user/dir/");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    /* Basename is empty, so no extension */
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_no_slash(void) {
    /* Test path without any slash */
    Obj* path = mk_string("file.txt");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), ".txt");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_empty_string(void) {
    /* Test empty path */
    Obj* path = mk_string("");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_dot_in_directory(void) {
    /* Test path with dot in directory name */
    Obj* path = mk_string("/home.user/file");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_double_extension(void) {
    /* Test path with double extension */
    Obj* path = mk_string("/var/log/archive.tar.gz");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), ".gz");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_current_directory(void) {
    /* Test current directory notation */
    Obj* path = mk_string(".");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    /* Dot is first character (basename), so no extension */
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_parent_directory(void) {
    /* Test parent directory notation */
    Obj* path = mk_string("..");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_only_extension(void) {
    /* Test filename that is just an extension */
    Obj* path = mk_string(".gitignore");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    /* No characters after dot in basename */
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_long_extension(void) {
    /* Test file with long extension */
    Obj* path = mk_string("document.tex");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), ".tex");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_invalid_type(void) {
    /* Test with non-string input (e.g., integer) */
    Obj* path = mk_int(42);
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    /* Should return empty string for invalid type */
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_extension_null_input(void) {
    /* Test with NULL input */
    Obj* result = prim_path_extension(NULL);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(result);
    PASS();
}

void test_path_extension_case_sensitive(void) {
    /* Test that extension is case-sensitive */
    Obj* path = mk_string("FILE.TXT");
    Obj* result = prim_path_extension(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), ".TXT");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

/* ========== Run All Path Extension Tests ========== */

void run_path_extension_tests(void) {
    TEST_SUITE("Path Operations (prim_path_extension)");

    /* Basic functionality tests */
    TEST_SECTION("Basic Functionality");
    RUN_TEST(test_path_extension_simple_filename);
    RUN_TEST(test_path_extension_full_path);
    RUN_TEST(test_path_extension_nested_path);
    RUN_TEST(test_path_extension_long_extension);
    RUN_TEST(test_path_extension_case_sensitive);

    /* Edge cases - No extension */
    TEST_SECTION("Edge Cases - No Extension");
    RUN_TEST(test_path_extension_no_extension);
    RUN_TEST(test_path_extension_hidden_file);
    RUN_TEST(test_path_extension_current_directory);
    RUN_TEST(test_path_extension_parent_directory);
    RUN_TEST(test_path_extension_only_extension);
    RUN_TEST(test_path_extension_dot_in_directory);
    RUN_TEST(test_path_extension_trailing_slash);

    /* Edge cases - Multiple/Complex extensions */
    TEST_SECTION("Edge Cases - Complex Extensions");
    RUN_TEST(test_path_extension_hidden_file_with_extension);
    RUN_TEST(test_path_extension_multiple_extensions);
    RUN_TEST(test_path_extension_double_extension);

    /* Boundary cases */
    TEST_SECTION("Boundary Cases");
    RUN_TEST(test_path_extension_empty_string);
    RUN_TEST(test_path_extension_no_slash);

    /* Error handling */
    TEST_SECTION("Error Handling");
    RUN_TEST(test_path_extension_invalid_type);
    RUN_TEST(test_path_extension_null_input);
}
