/*
 * test_io.c - Tests for I/O operation primitives
 *
 * Tests file I/O functions:
 * - prim_io_read_file - Read entire file contents
 *
 * Tests cover:
 * - Reading existing files with content
 * - Reading empty files
 * - Error handling for non-existent files
 * - Error handling for invalid input
 */

#include "test_framework.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

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
    char template[] = "/tmp/omnilisp_test_XXXXXX";
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

/* Helper to delete a test file */
static void delete_test_file(const char* path) {
    if (path) {
        unlink(path);
        free((void*)path);
    }
}

/* ========== prim_io_read_file Tests ========== */

void test_io_read_file_basic_content(void) {
    /* Test reading a file with basic text content */
    char* path = create_test_file("Hello, World!");
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_io_read_file(path_obj);

    ASSERT_NOT_NULL(result);
    const char* content = io_test_obj_to_cstr(result);
    ASSERT_NOT_NULL(content);
    ASSERT_STR_EQ(content, "Hello, World!");

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

void test_io_read_file_multiline_content(void) {
    /* Test reading a file with multiple lines */
    char* path = create_test_file("Line 1\nLine 2\nLine 3\n");
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_io_read_file(path_obj);

    ASSERT_NOT_NULL(result);
    const char* content = io_test_obj_to_cstr(result);
    ASSERT_NOT_NULL(content);
    ASSERT_STR_EQ(content, "Line 1\nLine 2\nLine 3\n");

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

void test_io_read_file_empty_file(void) {
    /* Test reading an empty file */
    char* path = create_test_file(NULL);
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_io_read_file(path_obj);

    ASSERT_NOT_NULL(result);
    const char* content = io_test_obj_to_cstr(result);
    ASSERT_NOT_NULL(content);
    ASSERT_STR_EQ(content, "");

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

void test_io_read_file_large_file(void) {
    /* Test reading a larger file (stress test) */
    char* large_content = malloc(10000);
    ASSERT_NOT_NULL(large_content);

    for (int i = 0; i < 9999; i++) {
        large_content[i] = 'A';
    }
    large_content[9999] = '\0';

    char* path = create_test_file(large_content);
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_io_read_file(path_obj);

    ASSERT_NOT_NULL(result);
    const char* content = io_test_obj_to_cstr(result);
    ASSERT_NOT_NULL(content);
    /* Verify content is preserved */
    ASSERT(content[0] == 'A');
    ASSERT(content[9998] == 'A');

    free(large_content);
    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

void test_io_read_file_non_existent(void) {
    /* Test error handling for non-existent file */
    const char* path = "/tmp/omnilisp_nonexistent_file_12345.txt";

    Obj* path_obj = mk_string(path);
    Obj* result = prim_io_read_file(path_obj);

    /* Should return error object */
    ASSERT_NOT_NULL(result);
    /* Error objects have TAG_ERROR or similar */

    dec_ref(path_obj);
    dec_ref(result);
    PASS();
}

void test_io_read_file_invalid_path(void) {
    /* Test error handling for invalid path (NULL input) */
    Obj* result = prim_io_read_file(NULL);

    /* Should return error object */
    ASSERT_NOT_NULL(result);
    dec_ref(result);
    PASS();
}

void test_io_read_file_non_string_path(void) {
    /* Test error handling for non-string path object */
    Obj* path_obj = mk_int(42);
    Obj* result = prim_io_read_file(path_obj);

    /* Should return error object */
    ASSERT_NOT_NULL(result);
    dec_ref(path_obj);
    dec_ref(result);
    PASS();
}

void test_io_read_file_special_characters(void) {
    /* Test reading file with special characters */
    char* path = create_test_file("Special: \t\n\r\"\\");
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_io_read_file(path_obj);

    ASSERT_NOT_NULL(result);
    const char* content = io_test_obj_to_cstr(result);
    ASSERT_NOT_NULL(content);
    ASSERT_STR_EQ(content, "Special: \t\n\r\"\\");
    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

void test_io_read_file_unicode_content(void) {
    /* Test reading file with UTF-8 content */
    char* path = create_test_file("Hello 世界 🌍");
    ASSERT_NOT_NULL(path);

    Obj* path_obj = mk_string(path);
    Obj* result = prim_io_read_file(path_obj);

    ASSERT_NOT_NULL(result);
    const char* content = io_test_obj_to_cstr(result);
    ASSERT_NOT_NULL(content);
    ASSERT_STR_EQ(content, "Hello 世界 🌍");

    dec_ref(path_obj);
    dec_ref(result);
    delete_test_file(path);
    PASS();
}

/* ========== prim_io_write_file Tests ========== */

void test_io_write_file_basic(void) {
    /* Test writing basic text content to a file */
    char template[] = "/tmp/omnilisp_write_test_XXXXXX";
    int fd = mkstemp(template);
    ASSERT(fd != -1);
    close(fd);

    Obj* path_obj = mk_string(template);
    Obj* content_obj = mk_string("Hello, World!");

    /* Write the file */
    Obj* write_result = prim_io_write_file(path_obj, content_obj);
    ASSERT_NOT_NULL(write_result);
    /* Write returns true on success */

    /* Verify the file was written correctly */
    Obj* read_result = prim_io_read_file(path_obj);
    ASSERT_NOT_NULL(read_result);
    const char* read_content = io_test_obj_to_cstr(read_result);
    ASSERT_NOT_NULL(read_content);
    ASSERT_STR_EQ(read_content, "Hello, World!");

    dec_ref(path_obj);
    dec_ref(content_obj);
    dec_ref(write_result);
    dec_ref(read_result);
    unlink(template);
    PASS();
}

void test_io_write_file_empty_content(void) {
    /* Test writing empty content to a file */
    char template[] = "/tmp/omnilisp_write_test_XXXXXX";
    int fd = mkstemp(template);
    ASSERT(fd != -1);
    close(fd);

    Obj* path_obj = mk_string(template);
    Obj* content_obj = mk_string("");

    Obj* write_result = prim_io_write_file(path_obj, content_obj);
    ASSERT_NOT_NULL(write_result);

    /* Verify the file is empty */
    Obj* read_result = prim_io_read_file(path_obj);
    ASSERT_NOT_NULL(read_result);
    const char* read_content = io_test_obj_to_cstr(read_result);
    ASSERT_NOT_NULL(read_content);
    ASSERT_STR_EQ(read_content, "");

    dec_ref(path_obj);
    dec_ref(content_obj);
    dec_ref(write_result);
    dec_ref(read_result);
    unlink(template);
    PASS();
}

void test_io_write_file_multiline(void) {
    /* Test writing multiline content to a file */
    char template[] = "/tmp/omnilisp_write_test_XXXXXX";
    int fd = mkstemp(template);
    ASSERT(fd != -1);
    close(fd);

    const char* multiline = "Line 1\nLine 2\nLine 3\n";
    Obj* path_obj = mk_string(template);
    Obj* content_obj = mk_string(multiline);

    Obj* write_result = prim_io_write_file(path_obj, content_obj);
    ASSERT_NOT_NULL(write_result);

    /* Verify multiline content is preserved */
    Obj* read_result = prim_io_read_file(path_obj);
    ASSERT_NOT_NULL(read_result);
    const char* read_content = io_test_obj_to_cstr(read_result);
    ASSERT_NOT_NULL(read_content);
    ASSERT_STR_EQ(read_content, multiline);

    dec_ref(path_obj);
    dec_ref(content_obj);
    dec_ref(write_result);
    dec_ref(read_result);
    unlink(template);
    PASS();
}

void test_io_write_file_overwrite(void) {
    /* Test that write_file overwrites existing content */
    char template[] = "/tmp/omnilisp_write_test_XXXXXX";
    int fd = mkstemp(template);
    ASSERT(fd != -1);
    write(fd, "Old content", 11);
    close(fd);

    Obj* path_obj = mk_string(template);
    Obj* content_obj = mk_string("New content");

    /* Write new content */
    Obj* write_result = prim_io_write_file(path_obj, content_obj);
    ASSERT_NOT_NULL(write_result);

    /* Verify old content is gone */
    Obj* read_result = prim_io_read_file(path_obj);
    ASSERT_NOT_NULL(read_result);
    const char* read_content = io_test_obj_to_cstr(read_result);
    ASSERT_NOT_NULL(read_content);
    ASSERT_STR_EQ(read_content, "New content");

    dec_ref(path_obj);
    dec_ref(content_obj);
    dec_ref(write_result);
    dec_ref(read_result);
    unlink(template);
    PASS();
}

void test_io_write_file_special_chars(void) {
    /* Test writing special characters */
    char template[] = "/tmp/omnilisp_write_test_XXXXXX";
    int fd = mkstemp(template);
    ASSERT(fd != -1);
    close(fd);

    const char* special = "Tab:\tNewline:\nQuote:\\Backslash:\\";
    Obj* path_obj = mk_string(template);
    Obj* content_obj = mk_string(special);

    Obj* write_result = prim_io_write_file(path_obj, content_obj);
    ASSERT_NOT_NULL(write_result);

    /* Verify special characters are preserved */
    Obj* read_result = prim_io_read_file(path_obj);
    ASSERT_NOT_NULL(read_result);
    const char* read_content = io_test_obj_to_cstr(read_result);
    ASSERT_NOT_NULL(read_content);
    ASSERT_STR_EQ(read_content, special);

    dec_ref(path_obj);
    dec_ref(content_obj);
    dec_ref(write_result);
    dec_ref(read_result);
    unlink(template);
    PASS();
}

/* ========== Run all I/O tests ========== */

void run_io_tests(void) {
    TEST_SECTION("I/O Operations - read-file");
    RUN_TEST(test_io_read_file_basic_content);
    RUN_TEST(test_io_read_file_multiline_content);
    RUN_TEST(test_io_read_file_empty_file);
    RUN_TEST(test_io_read_file_large_file);
    RUN_TEST(test_io_read_file_non_existent);
    RUN_TEST(test_io_read_file_invalid_path);
    RUN_TEST(test_io_read_file_non_string_path);
    RUN_TEST(test_io_read_file_special_characters);
    RUN_TEST(test_io_read_file_unicode_content);

    TEST_SECTION("I/O Operations - write-file");
    RUN_TEST(test_io_write_file_basic);
    RUN_TEST(test_io_write_file_empty_content);
    RUN_TEST(test_io_write_file_multiline);
    RUN_TEST(test_io_write_file_overwrite);
    RUN_TEST(test_io_write_file_special_chars);
}
