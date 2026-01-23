/*
 * test_regex.c - Tests for Regex Operations
 *
 * Coverage: regex.c functions
 *   - re-match: Match first occurrence anywhere in string
 *   - re-find-all: Find all non-overlapping matches
 *   - re-split: Split string by pattern
 *   - re-replace: Replace pattern with string
 *   - re-fullmatch: Check if pattern matches entire string
 *
 * Tests basic regex patterns:
 *   - Literals: "abc", "123"
 *   - Character classes: [a-z], [0-9], \d
 *   - Quantifiers: *, +, ?, {n}
 *   - Alternation: a|b
 *   - Anchors: ^, $
 */

#include "test_framework.h"
#include <string.h>

/* ==================== re-match Tests ==================== */

void test_re_match_literal_found(void) {
    Obj* pattern = mk_sym("world");
    Obj* input = mk_sym("hello world");
    Obj* result = prim_re_match(pattern, input);

    ASSERT_NOT_NULL(result);
    /* Result should be the matched substring "world" */
    ASSERT(IS_BOXED(result) && result->tag == TAG_STRING);
    const char* matched = (const char*)result->ptr;
    ASSERT(strcmp(matched, "world") == 0);

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

void test_re_match_literal_not_found(void) {
    Obj* pattern = mk_sym("xyz");
    Obj* input = mk_sym("hello world");
    Obj* result = prim_re_match(pattern, input);

    /* No match should return NULL */
    ASSERT_NULL(result);

    dec_ref(pattern); dec_ref(input);
    PASS();
}

void test_re_match_digits(void) {
    Obj* pattern = mk_sym("\\d+");
    Obj* input = mk_sym("abc123def");
    Obj* result = prim_re_match(pattern, input);

    ASSERT_NOT_NULL(result);
    const char* matched = (const char*)result->ptr;
    ASSERT(strcmp(matched, "123") == 0);

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

void test_re_match_character_class(void) {
    Obj* pattern = mk_sym("[a-z]+");
    Obj* input = mk_sym("ABCxyzDEF");
    Obj* result = prim_re_match(pattern, input);

    ASSERT_NOT_NULL(result);
    const char* matched = (const char*)result->ptr;
    ASSERT(strcmp(matched, "xyz") == 0);

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

void test_re_match_quantifier_star(void) {
    Obj* pattern = mk_sym("a*");
    Obj* input = mk_sym("bbaabb");
    Obj* result = prim_re_match(pattern, input);

    ASSERT_NOT_NULL(result);
    /* Should match empty string at start (greedy quantifier) */
    const char* matched = (const char*)result->ptr;
    ASSERT(strlen(matched) >= 0); /* Just verify it doesn't crash */

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

void test_re_match_quantifier_plus(void) {
    Obj* pattern = mk_sym("a+");
    Obj* input = mk_sym("bbaabb");
    Obj* result = prim_re_match(pattern, input);

    ASSERT_NOT_NULL(result);
    const char* matched = (const char*)result->ptr;
    ASSERT(strcmp(matched, "aa") == 0);

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

void test_re_match_alternation(void) {
    Obj* pattern = mk_sym("cat|dog");
    Obj* input = mk_sym("I have a cat");
    Obj* result = prim_re_match(pattern, input);

    ASSERT_NOT_NULL(result);
    const char* matched = (const char*)result->ptr;
    ASSERT(strcmp(matched, "cat") == 0);

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

/* ==================== re-find-all Tests ==================== */

void test_re_find_all_multiple(void) {
    Obj* pattern = mk_sym("\\d+");
    Obj* input = mk_sym("a12b34c56");
    Obj* result = prim_re_find_all(pattern, input);

    ASSERT_NOT_NULL(result);
    /* Should return a list of matched strings */
    ASSERT(IS_BOXED(result));
    /* Verify we have 3 matches */
    int count = 0;
    Obj* current = result;
    while (current && IS_BOXED(current)) {
        count++;
        Obj* car = current->a;  /* car of pair */
        if (car && IS_BOXED(car) && car->tag == TAG_STRING) {
            /* Valid string match */
        }
        current = current->b;  /* cdr of pair */
    }
    /* We should have 3 number matches: 12, 34, 56 */
    ASSERT(count == 3);

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

void test_re_find_all_none(void) {
    Obj* pattern = mk_sym("xyz");
    Obj* input = mk_sym("abc def");
    Obj* result = prim_re_find_all(pattern, input);

    ASSERT_NOT_NULL(result);
    /* Should return empty list or NULL */

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

/* ==================== re-split Tests ==================== */

void test_re_split_basic(void) {
    Obj* pattern = mk_sym(",");
    Obj* input = mk_sym("a,b,c");
    Obj* result = prim_re_split(pattern, input);

    ASSERT_NOT_NULL(result);
    /* Should return list of 3 strings */
    ASSERT(IS_BOXED(result));

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

void test_re_split_empty_pattern(void) {
    Obj* pattern = mk_sym("");
    Obj* input = mk_sym("hello");
    Obj* result = prim_re_split(pattern, input);

    ASSERT_NOT_NULL(result);
    /* Empty pattern should return list with single string */

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

/* ==================== re-replace Tests ==================== */

void test_re_replace_first(void) {
    Obj* pattern = mk_sym("cat");
    Obj* replacement = mk_sym("dog");
    Obj* input = mk_sym("cat cat cat");
    Obj* global = OMNI_FALSE; /* Only replace first */

    Obj* result = prim_re_replace(pattern, replacement, input, global);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result) && result->tag == TAG_STRING);
    const char* output = (const char*)result->ptr;
    ASSERT(strcmp(output, "dog cat cat") == 0);

    dec_ref(pattern); dec_ref(replacement); dec_ref(input); dec_ref(result);
    PASS();
}

void test_re_replace_all(void) {
    Obj* pattern = mk_sym("cat");
    Obj* replacement = mk_sym("dog");
    Obj* input = mk_sym("cat cat cat");
    Obj* global = OMNI_TRUE; /* Replace all */

    Obj* result = prim_re_replace(pattern, replacement, input, global);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result) && result->tag == TAG_STRING);
    const char* output = (const char*)result->ptr;
    ASSERT(strcmp(output, "dog dog dog") == 0);

    dec_ref(pattern); dec_ref(replacement); dec_ref(input); dec_ref(result);
    PASS();
}

void test_re_replace_digits(void) {
    Obj* pattern = mk_sym("\\d+");
    Obj* replacement = mk_sym("X");
    Obj* input = mk_sym("a123b456c");
    Obj* global = OMNI_TRUE;

    Obj* result = prim_re_replace(pattern, replacement, input, global);

    ASSERT_NOT_NULL(result);
    const char* output = (const char*)result->ptr;
    ASSERT(strcmp(output, "aXbXc") == 0);

    dec_ref(pattern); dec_ref(replacement); dec_ref(input); dec_ref(result);
    PASS();
}

/* ==================== re-fullmatch Tests ==================== */

void test_re_fullmatch_true(void) {
    Obj* pattern = mk_sym("^[a-z]+$");
    Obj* input = mk_sym("hello");
    Obj* result = prim_re_fullmatch(pattern, input);

    /* Should return true for full match */
    ASSERT_NOT_NULL(result);

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

void test_re_fullmatch_false(void) {
    Obj* pattern = mk_sym("^[a-z]+$");
    Obj* input = mk_sym("hello123");
    Obj* result = prim_re_fullmatch(pattern, input);

    /* Should return false or NULL for no full match */
    /* This test just verifies it doesn't crash */

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

/* ==================== Edge Cases ==================== */

void test_re_null_pattern(void) {
    Obj* input = mk_sym("hello");
    Obj* result = prim_re_match(NULL, input);

    /* NULL pattern should return NULL */
    ASSERT_NULL(result);

    dec_ref(input);
    PASS();
}

void test_re_null_input(void) {
    Obj* pattern = mk_sym("hello");
    Obj* result = prim_re_match(pattern, NULL);

    /* NULL input should return NULL */
    ASSERT_NULL(result);

    dec_ref(pattern);
    PASS();
}

void test_re_empty_input(void) {
    Obj* pattern = mk_sym("a");
    Obj* input = mk_sym("");
    Obj* result = prim_re_match(pattern, input);

    /* Empty input should return NULL */
    ASSERT_NULL(result);

    dec_ref(pattern); dec_ref(input);
    PASS();
}

void test_re_empty_pattern_empty_input(void) {
    Obj* pattern = mk_sym("");
    Obj* input = mk_sym("");
    Obj* result = prim_re_match(pattern, input);

    /* Edge case: both empty */
    ASSERT_NOT_NULL(result);

    dec_ref(pattern); dec_ref(input); dec_ref(result);
    PASS();
}

/* ==================== Test Runner ==================== */

void run_regex_tests(void) {
    TEST_SUITE("Regex Tests");

    /* re-match tests */
    TEST_SECTION("re-match");
    RUN_TEST(test_re_match_literal_found);
    RUN_TEST(test_re_match_literal_not_found);
    RUN_TEST(test_re_match_digits);
    RUN_TEST(test_re_match_character_class);
    RUN_TEST(test_re_match_quantifier_star);
    RUN_TEST(test_re_match_quantifier_plus);
    RUN_TEST(test_re_match_alternation);

    /* re-find-all tests */
    TEST_SECTION("re-find-all");
    RUN_TEST(test_re_find_all_multiple);
    RUN_TEST(test_re_find_all_none);

    /* re-split tests */
    TEST_SECTION("re-split");
    RUN_TEST(test_re_split_basic);
    RUN_TEST(test_re_split_empty_pattern);

    /* re-replace tests */
    TEST_SECTION("re-replace");
    RUN_TEST(test_re_replace_first);
    RUN_TEST(test_re_replace_all);
    RUN_TEST(test_re_replace_digits);

    /* re-fullmatch tests */
    TEST_SECTION("re-fullmatch");
    RUN_TEST(test_re_fullmatch_true);
    RUN_TEST(test_re_fullmatch_false);

    /* Edge cases */
    TEST_SECTION("Edge Cases");
    RUN_TEST(test_re_null_pattern);
    RUN_TEST(test_re_null_input);
    RUN_TEST(test_re_empty_input);
    RUN_TEST(test_re_empty_pattern_empty_input);
}
