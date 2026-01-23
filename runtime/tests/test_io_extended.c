/*
 * test_io_extended.c - Extended tests for I/O operation primitives
 *
 * Tests for untested I/O functions in io.c:
 * - prim_read_lines - Read file as array of lines
 * - prim_write_lines - Write array of lines to file
 * - prim_file_exists_p - Check if path exists
 * - prim_file_p - Check if path is a regular file
 * - prim_directory_p - Check if path is a directory
 * - prim_path_join - Join path components
 *
 * These functions are core I/O operations that are currently untested.
 */

#include "test_framework.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

/* Helper to extract string value from Obj */
static const char* io_test_obj_to_cstr(Obj* obj) {
    if (!obj) return NULL;
    if (IS_IMMEDIATE(obj)) return NULL;
    if (obj->tag == TAG_STRING || obj->tag == TAG_SYM) {
        return obj->ptr ? (const char*)obj->ptr : "";
    }
    return NULL;
}

/* Helper to create a temporary file with content */
static char* create_test_file(const char* content) {
    char template[] = "/tmp/omnilisp_io_test_XXXXXX";
    int fd = mkstemp(template);
    if (fd == -1) return NULL;

    if (content) {
        size_t len = strlen(content);
        ssize_t written = write(fd, content, len);
        if (written != (ssize_t)len) {
            close(fd);
            unlink(template);
            return NULL;
        }
    }

    close(fd);
    char* path = strdup(template);
    return path;
}

/* Helper to create a temporary directory */
static char* create_test_directory(void) {
    char template[] = "/tmp/omnilisp_io_dir_XXXXXX";
    char* dir_path = mkdtemp(template);
    if (!dir_path) return NULL;
    return strdup(dir_path);
}

/* Helper to delete a test file */
static void delete_test_file(const char* path) {
    if (path) {
        unlink(path);
        free((void*)path);
    }
}

/* Helper to delete a test directory */
static void delete_test_directory(const char* path) {
    if (path) {
        rmdir(path);
        free((void*)path);
    }
}

/* Helper to get array length */
static int get_array_length(Obj* arr_obj) {
    if (!arr_obj || !IS_BOXED(arr_obj) || arr_obj->tag != TAG_ARRAY) {
        return 0;
    }
    Array* arr = (Array*)arr_obj->ptr;
    return arr ? arr->len : 0;
}

/* ========== prim_read_lines Tests ========== */

void test_io_read_lines_basic(void) {
    /* Test reading a file with multiple lines */
    char* path = create_test_file("Line 1\nLine 2\nLine 3\n");
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_read_lines(path_obj);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result) && result->tag == TAG_ARRAY);
    
    int len = get_array_length(result);
    ASSERT(len == 3);

    /* Verify each line */
    Array* arr = (Array*)result->ptr;
    const char* line1 = io_test_obj_to_cstr(arr->data[0]);
    const char* line2 = io_test_obj_to_cstr(arr->data[1]);
    const char* line3 = io_test_obj_to_cstr(arr->data[2]);
    
    ASSERT_NOT_NULL(line1);
    ASSERT_NOT_NULL(line2);
    ASSERT_NOT_NULL(line3);
    ASSERT_STR_EQ(line1, "Line 1");
    ASSERT_STR_EQ(line2, "Line 2");
    ASSERT_STR_EQ(line3, "Line 3");

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

void test_io_read_lines_empty_file(void) {
    /* Test reading an empty file */
    char* path = create_test_file(NULL);
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_read_lines(path_obj);

    ASSERT_NOT_NULL(result);
    int len = get_array_length(result);
    ASSERT(len == 0);

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

void test_io_read_lines_single_line(void) {
    /* Test reading a file with a single line (no newline) */
    char* path = create_test_file("Single line");
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_read_lines(path_obj);

    ASSERT_NOT_NULL(result);
    int len = get_array_length(result);
    ASSERT(len == 1);

    Array* arr = (Array*)result->ptr;
    const char* line = io_test_obj_to_cstr(arr->data[0]);
    ASSERT_STR_EQ(line, "Single line");

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

void test_io_read_lines_with_trailing_newline(void) {
    /* Test reading a file that ends with a newline */
    char* path = create_test_file("Line 1\nLine 2\n");
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_read_lines(path_obj);

    ASSERT_NOT_NULL(result);
    int len = get_array_length(result);
    /* Should be 2 lines (not 3, the trailing newline doesn't create an empty line) */
    ASSERT(len == 2);

    Array* arr = (Array*)result->ptr;
    const char* line1 = io_test_obj_to_cstr(arr->data[0]);
    const char* line2 = io_test_obj_to_cstr(arr->data[1]);
    
    ASSERT_STR_EQ(line1, "Line 1");
    ASSERT_STR_EQ(line2, "Line 2");

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

void test_io_read_lines_blank_lines(void) {
    /* Test reading a file with blank lines */
    char* path = create_test_file("Line 1\n\nLine 3\n\n");
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_read_lines(path_obj);

    ASSERT_NOT_NULL(result);
    int len = get_array_length(result);
    /* Should be 4 lines (including 2 blank lines) */
    ASSERT(len == 4);

    Array* arr = (Array*)result->ptr;
    const char* line1 = io_test_obj_to_cstr(arr->data[0]);
    const char* line2 = io_test_obj_to_cstr(arr->data[1]);
    const char* line3 = io_test_obj_to_cstr(arr->data[2]);
    const char* line4 = io_test_obj_to_cstr(arr->data[3]);
    
    ASSERT_STR_EQ(line1, "Line 1");
    ASSERT_STR_EQ(line2, "");
    ASSERT_STR_EQ(line3, "Line 3");
    ASSERT_STR_EQ(line4, "");

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

/* ========== prim_write_lines Tests ========== */

void test_io_write_lines_basic(void) {
    /* Test writing array of lines to a file */
    char template[] = "/tmp/omnilisp_write_lines_XXXXXX";
    int fd = mkstemp(template);
    ASSERT(fd != -1);
    close(fd);

    /* Create array of lines */
    Obj* arr = mk_array(3);
    array_push(arr, mk_string("Line 1"));
    array_push(arr, mk_string("Line 2"));
    array_push(arr, mk_string("Line 3"));

    Obj* path_obj = mk_string(template);
    Obj* write_result = prim_write_lines(path_obj, arr);
    
    ASSERT_NOT_NULL(write_result);

    /* Verify file was written correctly */
    Obj* read_result = prim_read_lines(path_obj);
    ASSERT_NOT_NULL(read_result);
    int len = get_array_length(read_result);
    ASSERT(len == 3);

    Array* read_arr = (Array*)read_result->ptr;
    const char* line1 = io_test_obj_to_cstr(read_arr->data[0]);
    const char* line2 = io_test_obj_to_cstr(read_arr->data[1]);
    const char* line3 = io_test_obj_to_cstr(read_arr->data[2]);
    
    ASSERT_STR_EQ(line1, "Line 1");
    ASSERT_STR_EQ(line2, "Line 2");
    ASSERT_STR_EQ(line3, "Line 3");

    dec_ref(path_obj);
    dec_ref(arr);
    dec_ref(write_result);
    dec_ref(read_result);
    unlink(template);
    PASS();
}

void test_io_write_lines_empty_array(void) {
    /* Test writing an empty array */
    char template[] = "/tmp/omnilisp_write_lines_XXXXXX";
    int fd = mkstemp(template);
    ASSERT(fd != -1);
    close(fd);

    Obj* arr = mk_array(0);
    Obj* path_obj = mk_string(template);
    Obj* write_result = prim_write_lines(path_obj, arr);
    
    ASSERT_NOT_NULL(write_result);

    /* Verify file is empty */
    Obj* read_result = prim_read_lines(path_obj);
    ASSERT_NOT_NULL(read_result);
    int len = get_array_length(read_result);
    ASSERT(len == 0);

    dec_ref(path_obj);
    dec_ref(arr);
    dec_ref(write_result);
    dec_ref(read_result);
    unlink(template);
    PASS();
}

/* ========== prim_file_exists_p Tests ========== */

void test_io_file_exists_p_existing(void) {
    /* Test checking existence of an existing file */
    char* path = create_test_file("test content");
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_file_exists_p(path_obj);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result) && IS_BOOL(result));
    ASSERT(result == mk_bool(1));

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

void test_io_file_exists_p_nonexistent(void) {
    /* Test checking existence of a non-existent file */
    const char* path = "/tmp/omnilisp_nonexistent_file_xyz123.txt";

    Obj* path_obj = mk_string(path);
    Obj* result = prim_file_exists_p(path_obj);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result) && IS_BOOL(result));
    ASSERT(result == mk_bool(0));

    dec_ref(path_obj);
    dec_ref(result);
    PASS();
}

/* ========== prim_file_p Tests ========== */

void test_io_file_p_regular_file(void) {
    /* Test checking if path is a regular file */
    char* path = create_test_file("test content");
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_file_p(path_obj);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result) && IS_BOOL(result));
    ASSERT(result == mk_bool(1));

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

void test_io_file_p_directory(void) {
    /* Test checking if a directory is considered a file */
    char* dir_path = create_test_directory();
    ASSERT_NOT_NULL(dir_path);

    Obj* path_obj = mk_string(dir_path);
    Obj* result = prim_file_p(path_obj);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result) && IS_BOOL(result));
    /* Directory should NOT be considered a regular file */
    ASSERT(result == mk_bool(0));

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_directory(dir_path);
    PASS();
}

void test_io_file_p_nonexistent(void) {
    /* Test checking if non-existent path is a file */
    const char* path = "/tmp/omnilisp_nonexistent_xyz123";

    Obj* path_obj = mk_string(path);
    Obj* result = prim_file_p(path_obj);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result) && IS_BOOL(result));
    /* Non-existent path is NOT a file */
    ASSERT(result == mk_bool(0));

    dec_ref(path_obj);
    dec_ref(result);
    PASS();
}

/* ========== prim_directory_p Tests ========== */

void test_io_directory_p_existing(void) {
    /* Test checking if path is a directory */
    char* dir_path = create_test_directory();
    ASSERT_NOT_NULL(dir_path);

    Obj* path_obj = mk_string(dir_path);
    Obj* result = prim_directory_p(path_obj);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result) && IS_BOOL(result));
    ASSERT(result == mk_bool(1));

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_directory(dir_path);
    PASS();
}

void test_io_directory_p_file(void) {
    /* Test checking if a file is considered a directory */
    char* path = create_test_file("test content");
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_directory_p(path_obj);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_IMMEDIATE(result) && IS_BOOL(result));
    /* File should NOT be considered a directory */
    ASSERT(result == mk_bool(0));

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

/* ========== prim_path_join Tests ========== */

void test_io_path_join_basic(void) {
    /* Test basic path joining */
    Obj* arr = mk_array(2);
    array_push(arr, mk_string("/home"));
    array_push(arr, mk_string("user"));
    
    Obj* result = prim_path_join(arr);
    ASSERT_NOT_NULL(result);
    
    const char* path = io_test_obj_to_cstr(result);
    ASSERT_NOT_NULL(path);
    ASSERT_STR_EQ(path, "/home/user");

    dec_ref(arr);
    dec_ref(result);
    PASS();
}

void test_io_path_join_multiple_parts(void) {
    /* Test joining multiple path parts */
    Obj* arr = mk_array(4);
    array_push(arr, mk_string("/var"));
    array_push(arr, mk_string("lib"));
    array_push(arr, mk_string("omnilisp"));
    array_push(arr, mk_string("module.ol"));
    
    Obj* result = prim_path_join(arr);
    ASSERT_NOT_NULL(result);
    
    const char* path = io_test_obj_to_cstr(result);
    ASSERT_NOT_NULL(path);
    ASSERT_STR_EQ(path, "/var/lib/omnilisp/module.ol");

    dec_ref(arr);
    dec_ref(result);
    PASS();
}

void test_io_path_join_empty_parts(void) {
    /* Test joining with empty parts */
    Obj* arr = mk_array(2);
    array_push(arr, mk_string("/home"));
    array_push(arr, mk_string(""));
    
    Obj* result = prim_path_join(arr);
    ASSERT_NOT_NULL(result);
    
    const char* path = io_test_obj_to_cstr(result);
    ASSERT_NOT_NULL(path);
    /* Empty parts should be skipped */
    ASSERT_STR_EQ(path, "/home");

    dec_ref(arr);
    dec_ref(result);
    PASS();
}

void test_io_path_join_trailing_slash(void) {
    /* Test joining when first part has trailing slash */
    Obj* arr = mk_array(2);
    array_push(arr, mk_string("/home/"));
    array_push(arr, mk_string("user"));
    
    Obj* result = prim_path_join(arr);
    ASSERT_NOT_NULL(result);
    
    const char* path = io_test_obj_to_cstr(result);
    ASSERT_NOT_NULL(path);
    /* Should not have double slash */
    ASSERT_STR_EQ(path, "/home/user");

    dec_ref(arr);
    dec_ref(result);
    PASS();
}

/* ========== Run all extended I/O tests ========== */

void run_io_extended_tests(void) {
    TEST_SECTION("I/O Operations - read-lines");
    RUN_TEST(test_io_read_lines_basic);
    RUN_TEST(test_io_read_lines_empty_file);
    RUN_TEST(test_io_read_lines_single_line);
    RUN_TEST(test_io_read_lines_with_trailing_newline);
    RUN_TEST(test_io_read_lines_blank_lines);

    TEST_SECTION("I/O Operations - write-lines");
    RUN_TEST(test_io_write_lines_basic);
    RUN_TEST(test_io_write_lines_empty_array);

    TEST_SECTION("I/O Operations - file-exists?");
    RUN_TEST(test_io_file_exists_p_existing);
    RUN_TEST(test_io_file_exists_p_nonexistent);

    TEST_SECTION("I/O Operations - file?");
    RUN_TEST(test_io_file_p_regular_file);
    RUN_TEST(test_io_file_p_directory);
    RUN_TEST(test_io_file_p_nonexistent);

    TEST_SECTION("I/O Operations - directory?");
    RUN_TEST(test_io_directory_p_existing);
    RUN_TEST(test_io_directory_p_file);

    TEST_SECTION("I/O Operations - path-join");
    RUN_TEST(test_io_path_join_basic);
    RUN_TEST(test_io_path_join_multiple_parts);
    RUN_TEST(test_io_path_join_empty_parts);
    RUN_TEST(test_io_path_join_trailing_slash);
}
