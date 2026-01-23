/*
 * test_string_utils.c - Comprehensive tests for string manipulation utilities
 *
 * Coverage: All functions in runtime/src/string_utils.c
 *
 * Test Groups:
 *   - String length
 *   - String splitting
 *   - String joining
 *   - String replacement
 *   - String trimming
 *   - String case conversion
 *   - String concatenation
 *   - String substring
 *   - String search (contains, index_of)
 *   - String comparison
 *   - String padding
 *   - String splitting (lines, words, chars)
 *   - String operations (reverse, repeat)
 */

#include "test_framework.h"
#include <string.h>

/* ==================== String Length Tests ==================== */

void test_string_length_normal(void) {
    Obj* s = mk_sym("hello");
    Obj* r = prim_string_length(s);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_int(r), 5);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_length_empty(void) {
    Obj* s = mk_sym("");
    Obj* r = prim_string_length(s);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_int(r), 0);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_length_unicode(void) {
    Obj* s = mk_sym("café");
    Obj* r = prim_string_length(s);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_int(r), 5);  /* UTF-8 bytes count */
    dec_ref(s); dec_ref(r);
    PASS();
}

/* ==================== String Split Tests ==================== */

void test_string_split_basic(void) {
    Obj* delim = mk_sym(",");
    Obj* s = mk_sym("a,b,c");
    Obj* r = prim_string_split(delim, s);
    ASSERT_NOT_NULL(r);
    ASSERT(obj_to_int(list_length(r)) == 3);
    dec_ref(delim); dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_split_empty_delim(void) {
    Obj* delim = mk_sym("");
    Obj* s = mk_sym("hello");
    Obj* r = prim_string_split(delim, s);
    /* Should return list with single string */
    ASSERT_NOT_NULL(r);
    dec_ref(delim); dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_split_no_match(void) {
    Obj* delim = mk_sym(",");
    Obj* s = mk_sym("hello world");
    Obj* r = prim_string_split(delim, s);
    /* Should return list with single string */
    ASSERT_NOT_NULL(r);
    dec_ref(delim); dec_ref(s); dec_ref(r);
    PASS();
}

/* ==================== String Join Tests ==================== */

void test_string_join_basic(void) {
    Obj* delim = mk_sym(",");
    Obj* list = mk_pair(mk_sym("a"), mk_pair(mk_sym("b"), mk_pair(mk_sym("c"), NULL)));
    Obj* r = prim_string_join(delim, list);
    ASSERT_NOT_NULL(r);
    const char* s = (const char*)r->ptr;
    ASSERT(strcmp(s, "a,b,c") == 0);
    dec_ref(delim); dec_ref(list); dec_ref(r);
    PASS();
}

void test_string_join_empty_list(void) {
    Obj* delim = mk_sym(",");
    Obj* list = NULL;
    Obj* r = prim_string_join(delim, list);
    ASSERT_NOT_NULL(r);
    const char* s = (const char*)r->ptr;
    ASSERT(strcmp(s, "") == 0);
    dec_ref(delim); dec_ref(r);
    PASS();
}

void test_string_join_empty_delim(void) {
    Obj* delim = mk_sym("");
    Obj* list = mk_pair(mk_sym("a"), mk_pair(mk_sym("b"), NULL));
    Obj* r = prim_string_join(delim, list);
    ASSERT_NOT_NULL(r);
    const char* s = (const char*)r->ptr;
    ASSERT(strcmp(s, "ab") == 0);
    dec_ref(delim); dec_ref(list); dec_ref(r);
    PASS();
}

/* ==================== String Replace Tests ==================== */

void test_string_replace_single(void) {
    Obj* old_s = mk_sym("hello");
    Obj* new_s = mk_sym("hi");
    Obj* s = mk_sym("hello world");
    Obj* r = prim_string_replace(old_s, new_s, s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hi world") == 0);
    dec_ref(old_s); dec_ref(new_s); dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_replace_all(void) {
    Obj* old_s = mk_sym("a");
    Obj* new_s = mk_sym("b");
    Obj* s = mk_sym("a a a");
    Obj* r = prim_string_replace(old_s, new_s, s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "b b b") == 0);
    dec_ref(old_s); dec_ref(new_s); dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_replace_no_match(void) {
    Obj* old_s = mk_sym("x");
    Obj* new_s = mk_sym("y");
    Obj* s = mk_sym("hello");
    Obj* r = prim_string_replace(old_s, new_s, s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hello") == 0);
    dec_ref(old_s); dec_ref(new_s); dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_replace_first(void) {
    Obj* old_s = mk_sym("a");
    Obj* new_s = mk_sym("b");
    Obj* s = mk_sym("a a a");
    Obj* r = prim_string_replace_first(old_s, new_s, s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "b a a") == 0);
    dec_ref(old_s); dec_ref(new_s); dec_ref(s); dec_ref(r);
    PASS();
}

/* ==================== String Trim Tests ==================== */

void test_string_trim_both(void) {
    Obj* s = mk_sym("  hello  ");
    Obj* r = prim_string_trim(s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hello") == 0);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_trim_left(void) {
    Obj* s = mk_sym("  hello");
    Obj* r = prim_string_trim_left(s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hello") == 0);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_trim_right(void) {
    Obj* s = mk_sym("hello  ");
    Obj* r = prim_string_trim_right(s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hello") == 0);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_trim_empty(void) {
    Obj* s = mk_sym("   ");
    Obj* r = prim_string_trim(s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "") == 0);
    dec_ref(s); dec_ref(r);
    PASS();
}

/* ==================== String Case Conversion Tests ==================== */

void test_string_upcase(void) {
    Obj* s = mk_sym("hello");
    Obj* r = prim_string_upcase(s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "HELLO") == 0);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_lowcase(void) {
    Obj* s = mk_sym("HELLO");
    Obj* r = prim_string_lowcase(s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hello") == 0);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_downcase(void) {
    Obj* s = mk_sym("HELLO");
    Obj* r = prim_string_downcase(s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hello") == 0);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_capitalize(void) {
    Obj* s = mk_sym("HELLO");
    Obj* r = prim_string_capitalize(s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "Hello") == 0);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_titlecase(void) {
    Obj* s = mk_sym("hello world");
    Obj* r = prim_string_titlecase(s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "Hello World") == 0);
    dec_ref(s); dec_ref(r);
    PASS();
}

/* ==================== String Concatenation Tests ==================== */

void test_string_concat_basic(void) {
    Obj* s1 = mk_sym("hello");
    Obj* s2 = mk_sym(" world");
    Obj* r = prim_string_concat(s1, s2);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hello world") == 0);
    dec_ref(s1); dec_ref(s2); dec_ref(r);
    PASS();
}

void test_string_concat_empty_left(void) {
    Obj* s1 = mk_sym("");
    Obj* s2 = mk_sym("hello");
    Obj* r = prim_string_concat(s1, s2);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hello") == 0);
    dec_ref(s1); dec_ref(s2); dec_ref(r);
    PASS();
}

void test_string_concat_empty_right(void) {
    Obj* s1 = mk_sym("hello");
    Obj* s2 = mk_sym("");
    Obj* r = prim_string_concat(s1, s2);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hello") == 0);
    dec_ref(s1); dec_ref(s2); dec_ref(r);
    PASS();
}

/* ==================== String Substring Tests ==================== */

void test_string_substr_positive(void) {
    Obj* s = mk_sym("hello world");
    Obj* start = mk_int(0);
    Obj* len = mk_int(5);
    Obj* r = prim_string_substr(s, start, len);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hello") == 0);
    dec_ref(s); dec_ref(start); dec_ref(len); dec_ref(r);
    PASS();
}

void test_string_substr_negative_start(void) {
    Obj* s = mk_sym("hello");
    Obj* start = mk_int(-2);
    Obj* len = mk_int(2);
    Obj* r = prim_string_substr(s, start, len);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "lo") == 0);
    dec_ref(s); dec_ref(start); dec_ref(len); dec_ref(r);
    PASS();
}

void test_string_substr_negative_length(void) {
    Obj* s = mk_sym("hello");
    Obj* start = mk_int(2);
    Obj* len = mk_int(-1);
    Obj* r = prim_string_substr(s, start, len);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "llo") == 0);
    dec_ref(s); dec_ref(start); dec_ref(len); dec_ref(r);
    PASS();
}

/* ==================== String Search Tests ==================== */

void test_string_contains_found(void) {
    Obj* s = mk_sym("hello world");
    Obj* sub = mk_sym("world");
    Obj* r = prim_string_contains(s, sub);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_bool(r), 1);
    dec_ref(s); dec_ref(sub); dec_ref(r);
    PASS();
}

void test_string_contains_not_found(void) {
    Obj* s = mk_sym("hello");
    Obj* sub = mk_sym("xyz");
    Obj* r = prim_string_contains(s, sub);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_bool(r), 0);
    dec_ref(s); dec_ref(sub); dec_ref(r);
    PASS();
}

void test_string_index_of_found(void) {
    Obj* s = mk_sym("hello world");
    Obj* sub = mk_sym("world");
    Obj* r = prim_string_index_of(s, sub);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_int(r), 6);
    dec_ref(s); dec_ref(sub); dec_ref(r);
    PASS();
}

void test_string_index_of_not_found(void) {
    Obj* s = mk_sym("hello");
    Obj* sub = mk_sym("xyz");
    Obj* r = prim_string_index_of(s, sub);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_int(r), -1);
    dec_ref(s); dec_ref(sub); dec_ref(r);
    PASS();
}

void test_string_last_index_of(void) {
    Obj* s = mk_sym("a b a b a");
    Obj* sub = mk_sym("a");
    Obj* r = prim_string_last_index_of(s, sub);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_int(r), 8);
    dec_ref(s); dec_ref(sub); dec_ref(r);
    PASS();
}

/* ==================== String Comparison Tests ==================== */

void test_string_equals_true(void) {
    Obj* s1 = mk_sym("hello");
    Obj* s2 = mk_sym("hello");
    Obj* r = prim_string_equals(s1, s2);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_bool(r), 1);
    dec_ref(s1); dec_ref(s2); dec_ref(r);
    PASS();
}

void test_string_equals_false(void) {
    Obj* s1 = mk_sym("hello");
    Obj* s2 = mk_sym("world");
    Obj* r = prim_string_equals(s1, s2);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_bool(r), 0);
    dec_ref(s1); dec_ref(s2); dec_ref(r);
    PASS();
}

void test_string_compare_less(void) {
    Obj* s1 = mk_sym("a");
    Obj* s2 = mk_sym("b");
    Obj* r = prim_string_compare(s1, s2);
    ASSERT_NOT_NULL(r);
    ASSERT(obj_to_int(r) < 0);
    dec_ref(s1); dec_ref(s2); dec_ref(r);
    PASS();
}

void test_string_compare_equal(void) {
    Obj* s1 = mk_sym("a");
    Obj* s2 = mk_sym("a");
    Obj* r = prim_string_compare(s1, s2);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_int(r), 0);
    dec_ref(s1); dec_ref(s2); dec_ref(r);
    PASS();
}

void test_string_compare_greater(void) {
    Obj* s1 = mk_sym("b");
    Obj* s2 = mk_sym("a");
    Obj* r = prim_string_compare(s1, s2);
    ASSERT_NOT_NULL(r);
    ASSERT(obj_to_int(r) > 0);
    dec_ref(s1); dec_ref(s2); dec_ref(r);
    PASS();
}

/* ==================== String Starts/Ends Tests ==================== */

void test_string_starts_with_true(void) {
    Obj* s = mk_sym("hello world");
    Obj* prefix = mk_sym("hello");
    Obj* r = prim_string_starts_with(s, prefix);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_bool(r), 1);
    dec_ref(s); dec_ref(prefix); dec_ref(r);
    PASS();
}

void test_string_starts_with_false(void) {
    Obj* s = mk_sym("hello world");
    Obj* prefix = mk_sym("world");
    Obj* r = prim_string_starts_with(s, prefix);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_bool(r), 0);
    dec_ref(s); dec_ref(prefix); dec_ref(r);
    PASS();
}

void test_string_ends_with_true(void) {
    Obj* s = mk_sym("hello world");
    Obj* suffix = mk_sym("world");
    Obj* r = prim_string_ends_with(s, suffix);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_bool(r), 1);
    dec_ref(s); dec_ref(suffix); dec_ref(r);
    PASS();
}

void test_string_ends_with_false(void) {
    Obj* s = mk_sym("hello world");
    Obj* suffix = mk_sym("hello");
    Obj* r = prim_string_ends_with(s, suffix);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(obj_to_bool(r), 0);
    dec_ref(s); dec_ref(suffix); dec_ref(r);
    PASS();
}

/* ==================== String Padding Tests ==================== */

void test_string_pad_left(void) {
    Obj* s = mk_sym("hi");
    Obj* width = mk_int(5);
    Obj* pad = mk_sym(" ");
    Obj* r = prim_string_pad_left(s, width, pad);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "   hi") == 0);
    ASSERT_EQ(obj_to_int(prim_string_length(r)),5);
    dec_ref(s); dec_ref(width); dec_ref(pad); dec_ref(r);
    PASS();
}

void test_string_pad_right(void) {
    Obj* s = mk_sym("hi");
    Obj* width = mk_int(5);
    Obj* pad = mk_sym(" ");
    Obj* r = prim_string_pad_right(s, width, pad);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hi   ") == 0);
    ASSERT_EQ(obj_to_int(prim_string_length(r)),5);
    dec_ref(s); dec_ref(width); dec_ref(pad); dec_ref(r);
    PASS();
}

void test_string_center(void) {
    Obj* s = mk_sym("hi");
    Obj* width = mk_int(6);
    Obj* pad = mk_sym(" ");
    Obj* r = prim_string_center(s, width, pad);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "  hi  ") == 0);
    ASSERT_EQ(obj_to_int(prim_string_length(r)),6);
    dec_ref(s); dec_ref(width); dec_ref(pad); dec_ref(r);
    PASS();
}

/* ==================== String Splitting Tests ==================== */

void test_string_lines_basic(void) {
    Obj* s = mk_sym("a\nb\nc");
    Obj* r = prim_string_lines(s);
    ASSERT_NOT_NULL(r);
    ASSERT(obj_to_int(list_length(r)) == 3);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_words_basic(void) {
    Obj* s = mk_sym("hello world test");
    Obj* r = prim_string_words(s);
    ASSERT_NOT_NULL(r);
    ASSERT(obj_to_int(list_length(r)) == 3);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_chars_basic(void) {
    Obj* s = mk_sym("abc");
    Obj* r = prim_string_chars(s);
    ASSERT_NOT_NULL(r);
    ASSERT(obj_to_int(list_length(r)) == 3);
    dec_ref(s); dec_ref(r);
    PASS();
}

/* ==================== String Operations Tests ==================== */

void test_string_reverse(void) {
    Obj* s = mk_sym("hello");
    Obj* r = prim_string_reverse(s);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "olleh") == 0);
    dec_ref(s); dec_ref(r);
    PASS();
}

void test_string_repeat(void) {
    Obj* s = mk_sym("hi");
    Obj* count = mk_int(3);
    Obj* r = prim_string_repeat(s, count);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "hihihi") == 0);
    dec_ref(s); dec_ref(count); dec_ref(r);
    PASS();
}

void test_string_repeat_zero(void) {
    Obj* s = mk_sym("hi");
    Obj* count = mk_int(0);
    Obj* r = prim_string_repeat(s, count);
    ASSERT_NOT_NULL(r);
    const char* result = (const char*)r->ptr;
    ASSERT(strcmp(result, "") == 0);
    dec_ref(s); dec_ref(count); dec_ref(r);
    PASS();
}

/* ==================== Test Runner ==================== */

void run_string_utils_tests(void) {
    TEST_SUITE("String Utils Tests");

    /* String Length */
    RUN_TEST(test_string_length_normal);
    RUN_TEST(test_string_length_empty);
    RUN_TEST(test_string_length_unicode);

    /* String Split */
    RUN_TEST(test_string_split_basic);
    RUN_TEST(test_string_split_empty_delim);
    RUN_TEST(test_string_split_no_match);

    /* String Join */
    RUN_TEST(test_string_join_basic);
    RUN_TEST(test_string_join_empty_list);
    RUN_TEST(test_string_join_empty_delim);

    /* String Replace */
    RUN_TEST(test_string_replace_single);
    RUN_TEST(test_string_replace_all);
    RUN_TEST(test_string_replace_no_match);
    RUN_TEST(test_string_replace_first);

    /* String Trim */
    RUN_TEST(test_string_trim_both);
    RUN_TEST(test_string_trim_left);
    RUN_TEST(test_string_trim_right);
    RUN_TEST(test_string_trim_empty);

    /* String Case Conversion */
    RUN_TEST(test_string_upcase);
    RUN_TEST(test_string_lowcase);
    RUN_TEST(test_string_downcase);
    RUN_TEST(test_string_capitalize);
    RUN_TEST(test_string_titlecase);

    /* String Concatenation */
    RUN_TEST(test_string_concat_basic);
    RUN_TEST(test_string_concat_empty_left);
    RUN_TEST(test_string_concat_empty_right);

    /* String Substring */
    RUN_TEST(test_string_substr_positive);
    RUN_TEST(test_string_substr_negative_start);
    RUN_TEST(test_string_substr_negative_length);

    /* String Search */
    RUN_TEST(test_string_contains_found);
    RUN_TEST(test_string_contains_not_found);
    RUN_TEST(test_string_index_of_found);
    RUN_TEST(test_string_index_of_not_found);
    RUN_TEST(test_string_last_index_of);

    /* String Comparison */
    RUN_TEST(test_string_equals_true);
    RUN_TEST(test_string_equals_false);
    RUN_TEST(test_string_compare_less);
    RUN_TEST(test_string_compare_equal);
    RUN_TEST(test_string_compare_greater);

    /* String Starts/Ends */
    RUN_TEST(test_string_starts_with_true);
    RUN_TEST(test_string_starts_with_false);
    RUN_TEST(test_string_ends_with_true);
    RUN_TEST(test_string_ends_with_false);

    /* String Padding */
    RUN_TEST(test_string_pad_left);
    RUN_TEST(test_string_pad_right);
    RUN_TEST(test_string_center);

    /* String Splitting */
    RUN_TEST(test_string_lines_basic);
    RUN_TEST(test_string_words_basic);
    RUN_TEST(test_string_chars_basic);

    /* String Operations */
    RUN_TEST(test_string_reverse);
    RUN_TEST(test_string_repeat);
    RUN_TEST(test_string_repeat_zero);
}
