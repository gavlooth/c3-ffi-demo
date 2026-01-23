/*
 * test_pika_charset.c - Test charset pattern parsing including escaped caret
 *
 * Tests Issue P4-1: Escaped caret in character classes
 * See: TODO.md Issue 29 P4
 *
 * Uses pika_meta_parse to create grammars from pattern strings, then tests
 * matching behavior.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include pika headers */
#include "../../csrc/parser/pika_c/pika.h"

/* Colors for output */
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define RESET   "\033[0m"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/*
 * Helper to test if a pattern matches an input string
 * Returns: 1 if match found, 0 if no match, -1 on error
 */
static int test_pattern_match(const char* pattern, const char* input, int* match_len) {
    /* Build a simple grammar: root <- <pattern>; */
    char grammar_buf[512];
    snprintf(grammar_buf, sizeof(grammar_buf), "root <- %s;", pattern);

    char* error = NULL;
    PikaGrammar* g = pika_meta_parse(grammar_buf, &error);
    if (!g) {
        if (error) {
            fprintf(stderr, "Grammar parse error: %s\n", error);
            free(error);
        }
        return -1;
    }

    PikaMemoTable* memo = pika_grammar_parse(g, input);
    if (!memo) {
        pika_grammar_free(g);
        return -1;
    }

    /* Get matches for the root rule */
    size_t count = 0;
    PikaMatch** matches = pika_memo_get_all_matches_for_rule(memo, "root", &count);

    int found = 0;
    if (count > 0 && matches) {
        found = 1;
        if (match_len) {
            *match_len = pika_match_len(matches[0]);
        }
        free(matches);
    }

    pika_memo_free(memo);
    pika_grammar_free(g);
    return found;
}

/* Test: escaped caret [\^] should match ^ character */
static void test_escaped_caret(void) {
    printf("  escaped caret [\\\\^]+: ");
    fflush(stdout);

    int len = 0;
    /* Pattern [\\^]+ should match one or more ^ characters */
    /* In meta-grammar, we need to escape once for the grammar parser */
    int result = test_pattern_match("[\\^]+", "^^^", &len);

    if (result == -1) {
        printf(RED "FAIL" RESET " - grammar parse error\n");
        tests_failed++;
        return;
    }

    if (result == 0) {
        printf(RED "FAIL" RESET " - pattern should match '^^^'\n");
        tests_failed++;
        return;
    }

    if (len != 3) {
        printf(RED "FAIL" RESET " - expected match length 3, got %d\n", len);
        tests_failed++;
        return;
    }

    printf(GREEN "PASS" RESET "\n");
    tests_passed++;
}

/* Test: escaped caret in mixed charset */
static void test_escaped_caret_mixed(void) {
    printf("  mixed charset [a\\^b]+: ");
    fflush(stdout);

    int len = 0;
    int result = test_pattern_match("[a\\^b]+", "a^b^a", &len);

    if (result == -1) {
        printf(RED "FAIL" RESET " - grammar parse error\n");
        tests_failed++;
        return;
    }

    if (result == 0) {
        printf(RED "FAIL" RESET " - pattern should match 'a^b^a'\n");
        tests_failed++;
        return;
    }

    if (len != 5) {
        printf(RED "FAIL" RESET " - expected match length 5, got %d\n", len);
        tests_failed++;
        return;
    }

    printf(GREEN "PASS" RESET "\n");
    tests_passed++;
}

/* Test: negated charset [^abc] */
static void test_negated_charset(void) {
    printf("  negated charset [^abc]+: ");
    fflush(stdout);

    int len = 0;
    int result = test_pattern_match("[^abc]+", "xyz", &len);

    if (result == -1) {
        printf(RED "FAIL" RESET " - grammar parse error\n");
        tests_failed++;
        return;
    }

    if (result == 0) {
        printf(RED "FAIL" RESET " - pattern should match 'xyz'\n");
        tests_failed++;
        return;
    }

    if (len != 3) {
        printf(RED "FAIL" RESET " - expected match length 3, got %d\n", len);
        tests_failed++;
        return;
    }

    printf(GREEN "PASS" RESET "\n");
    tests_passed++;
}

/* Test: negated charset should not match excluded chars */
static void test_negated_charset_no_match(void) {
    printf("  negated charset [^abc]+ vs 'abc': ");
    fflush(stdout);

    int len = 0;
    int result = test_pattern_match("[^abc]+", "abc", &len);

    if (result == -1) {
        printf(RED "FAIL" RESET " - grammar parse error\n");
        tests_failed++;
        return;
    }

    /* Should not match at position 0 since 'a' is excluded */
    if (result == 1 && len > 0) {
        printf(RED "FAIL" RESET " - pattern should NOT match 'abc' at start\n");
        tests_failed++;
        return;
    }

    printf(GREEN "PASS" RESET "\n");
    tests_passed++;
}

/* Test: simple character class [abc]+ */
static void test_simple_charset(void) {
    printf("  simple charset [abc]+: ");
    fflush(stdout);

    int len = 0;
    int result = test_pattern_match("[abc]+", "cabbage", &len);

    if (result == -1) {
        printf(RED "FAIL" RESET " - grammar parse error\n");
        tests_failed++;
        return;
    }

    if (result == 0) {
        printf(RED "FAIL" RESET " - pattern should match start of 'cabbage'\n");
        tests_failed++;
        return;
    }

    /* Should match 'cabba' (5 chars) but stop at 'g' */
    if (len != 5) {
        printf(RED "FAIL" RESET " - expected match length 5, got %d\n", len);
        tests_failed++;
        return;
    }

    printf(GREEN "PASS" RESET "\n");
    tests_passed++;
}

/* Test: range [a-z]+ */
static void test_range_charset(void) {
    printf("  range charset [a-z]+: ");
    fflush(stdout);

    int len = 0;
    int result = test_pattern_match("[a-z]+", "hello123", &len);

    if (result == -1) {
        printf(RED "FAIL" RESET " - grammar parse error\n");
        tests_failed++;
        return;
    }

    if (result == 0) {
        printf(RED "FAIL" RESET " - pattern should match 'hello'\n");
        tests_failed++;
        return;
    }

    if (len != 5) {
        printf(RED "FAIL" RESET " - expected match length 5, got %d\n", len);
        tests_failed++;
        return;
    }

    printf(GREEN "PASS" RESET "\n");
    tests_passed++;
}

/* Test: escaped hyphen [a\-c]+ - literal hyphen, not range */
static void test_escaped_hyphen(void) {
    printf("  escaped hyphen [a\\-c]+: ");
    fflush(stdout);

    int len = 0;
    int result = test_pattern_match("[a\\-c]+", "a-c-a", &len);

    if (result == -1) {
        printf(RED "FAIL" RESET " - grammar parse error\n");
        tests_failed++;
        return;
    }

    if (result == 0) {
        printf(RED "FAIL" RESET " - pattern should match 'a-c-a'\n");
        tests_failed++;
        return;
    }

    if (len != 5) {
        printf(RED "FAIL" RESET " - expected match length 5, got %d\n", len);
        tests_failed++;
        return;
    }

    printf(GREEN "PASS" RESET "\n");
    tests_passed++;
}

/* Test: escaped hyphen should NOT match 'b' (it's not a range) */
static void test_escaped_hyphen_no_b(void) {
    printf("  escaped hyphen [a\\-c]+ vs 'b': ");
    fflush(stdout);

    int len = 0;
    int result = test_pattern_match("[a\\-c]+", "b", &len);

    if (result == -1) {
        printf(RED "FAIL" RESET " - grammar parse error\n");
        tests_failed++;
        return;
    }

    /* Should not match 'b' since [a\-c] is a, -, c (not a range a-c) */
    if (result == 1 && len > 0) {
        printf(RED "FAIL" RESET " - pattern should NOT match 'b' (not a range)\n");
        tests_failed++;
        return;
    }

    printf(GREEN "PASS" RESET "\n");
    tests_passed++;
}

/* Test: escaped bracket [\]]+ */
static void test_escaped_bracket(void) {
    printf("  escaped bracket [\\]]+: ");
    fflush(stdout);

    int len = 0;
    int result = test_pattern_match("[\\]]+", "]]]", &len);

    if (result == -1) {
        printf(RED "FAIL" RESET " - grammar parse error\n");
        tests_failed++;
        return;
    }

    if (result == 0) {
        printf(RED "FAIL" RESET " - pattern should match ']]]'\n");
        tests_failed++;
        return;
    }

    if (len != 3) {
        printf(RED "FAIL" RESET " - expected match length 3, got %d\n", len);
        tests_failed++;
        return;
    }

    printf(GREEN "PASS" RESET "\n");
    tests_passed++;
}

/* Test: escaped backslash [\\]+ */
static void test_escaped_backslash(void) {
    printf("  escaped backslash [\\\\]+: ");
    fflush(stdout);

    int len = 0;
    int result = test_pattern_match("[\\\\]+", "\\\\\\", &len);

    if (result == -1) {
        printf(RED "FAIL" RESET " - grammar parse error\n");
        tests_failed++;
        return;
    }

    if (result == 0) {
        printf(RED "FAIL" RESET " - pattern should match '\\\\\\'\n");
        tests_failed++;
        return;
    }

    if (len != 3) {
        printf(RED "FAIL" RESET " - expected match length 3, got %d\n", len);
        tests_failed++;
        return;
    }

    printf(GREEN "PASS" RESET "\n");
    tests_passed++;
}

/* Main test runner */
int main(void) {
    printf("\n" YELLOW "=== Pika Charset Pattern Tests ===" RESET "\n");
    printf("Testing escaped characters in character classes\n\n");

    test_simple_charset();
    tests_run++;

    test_range_charset();
    tests_run++;

    test_negated_charset();
    tests_run++;

    test_negated_charset_no_match();
    tests_run++;

    test_escaped_caret();
    tests_run++;

    test_escaped_caret_mixed();
    tests_run++;

    test_escaped_hyphen();
    tests_run++;

    test_escaped_hyphen_no_b();
    tests_run++;

    test_escaped_bracket();
    tests_run++;

    test_escaped_backslash();
    tests_run++;

    printf("\n" YELLOW "=== Summary ===" RESET "\n");
    printf("  Total: %d, Passed: %d, Failed: %d\n", tests_run, tests_passed, tests_failed);
    if (tests_failed == 0) {
        printf("  Status: " GREEN "ALL TESTS PASSED" RESET "\n");
    } else {
        printf("  Status: " RED "SOME TESTS FAILED" RESET "\n");
    }

    return tests_failed > 0 ? 1 : 0;
}
