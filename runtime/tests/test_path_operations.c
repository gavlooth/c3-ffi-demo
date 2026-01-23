/*
 * test_path_operations.c - Tests for path operation primitives
 *
 * Tests path manipulation functions:
 * - prim_path_basename - Extract filename from path
 * - prim_path_dirname - Extract directory from path
 * - prim_path_extension - Extract file extension
 * - prim_path_join - Join path components
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

/* ========== prim_path_basename Tests ========== */

void test_path_basename_simple_filename(void) {
    /* Test simple filename without directory */
    Obj* path = mk_string("file.txt");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "file.txt");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_full_path(void) {
    /* Test full path with directories */
    Obj* path = mk_string("/home/user/file.txt");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "file.txt");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_nested_directories(void) {
    /* Test path with multiple nested directories */
    Obj* path = mk_string("/a/b/c/d/e/file");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "file");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_trailing_slash(void) {
    /* Test path with trailing slash (edge case) */
    Obj* path = mk_string("/home/user/dir/");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    /* strrchr on "/" returns "/", so last_slash+1 is empty string */
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_root_directory(void) {
    /* Test root directory */
    Obj* path = mk_string("/");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_no_slash(void) {
    /* Test path without any slash */
    Obj* path = mk_string("simple");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "simple");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_empty_string(void) {
    /* Test empty path */
    Obj* path = mk_string("");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_with_extension(void) {
    /* Test path with file extension */
    Obj* path = mk_string("/usr/local/bin/script.sh");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "script.sh");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_no_extension(void) {
    /* Test path without file extension */
    Obj* path = mk_string("/home/user/README");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "README");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_double_extension(void) {
    /* Test path with double extension */
    Obj* path = mk_string("/var/log/archive.tar.gz");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "archive.tar.gz");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_current_directory(void) {
    /* Test current directory notation */
    Obj* path = mk_string(".");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), ".");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_parent_directory(void) {
    /* Test parent directory notation */
    Obj* path = mk_string("..");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "..");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_hidden_file(void) {
    /* Test hidden file */
    Obj* path = mk_string("/home/user/.hidden");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), ".hidden");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_windows_style(void) {
    /* Test that backslash is treated as regular character (not path separator on Unix) */
    Obj* path = mk_string("C:\\Users\\file.txt");
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    /* On Unix, backslash is not a separator, so entire string is returned */
    ASSERT_STR_EQ(test_obj_to_cstr(result), "C:\\Users\\file.txt");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_invalid_type(void) {
    /* Test with non-string input (e.g., integer) */
    Obj* path = mk_int(42);
    Obj* result = prim_path_basename(path);

    ASSERT_NOT_NULL(result);
    /* Should return empty string for invalid type */
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(path);
    dec_ref(result);
    PASS();
}

void test_path_basename_null_input(void) {
    /* Test with NULL input */
    Obj* result = prim_path_basename(NULL);

    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(test_obj_to_cstr(result), "");

    dec_ref(result);
    PASS();
}

/* ========== Run All Path Operation Tests ========== */

void run_path_operations_tests(void) {
    TEST_SUITE("Path Operations (prim_path_basename)");

    /* Basic functionality tests */
    TEST_SECTION("Basic Functionality");
    RUN_TEST(test_path_basename_simple_filename);
    RUN_TEST(test_path_basename_full_path);
    RUN_TEST(test_path_basename_nested_directories);
    RUN_TEST(test_path_basename_with_extension);
    RUN_TEST(test_path_basename_no_extension);
    RUN_TEST(test_path_basename_double_extension);

    /* Edge cases */
    TEST_SECTION("Edge Cases");
    RUN_TEST(test_path_basename_trailing_slash);
    RUN_TEST(test_path_basename_root_directory);
    RUN_TEST(test_path_basename_no_slash);
    RUN_TEST(test_path_basename_empty_string);
    RUN_TEST(test_path_basename_current_directory);
    RUN_TEST(test_path_basename_parent_directory);
    RUN_TEST(test_path_basename_hidden_file);
    RUN_TEST(test_path_basename_windows_style);

    /* Error handling */
    TEST_SECTION("Error Handling");
    RUN_TEST(test_path_basename_invalid_type);
    RUN_TEST(test_path_basename_null_input);
}
