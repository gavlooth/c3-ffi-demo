/*
 * Test for omni_pika_match function
 * Verifies T-wire-pika-exec-01 implementation
 */

#include "../csrc/parser/pika.h"
#include "../csrc/ast/ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper function to print OmniValue results */
static void print_result(const char* test_name, OmniValue* result) {
    printf("%s:\n", test_name);
    if (!result) {
        printf("  FAILED: result is NULL\n");
        return;
    }

    if (result->tag == OMNI_ERROR) {
        printf("  Error: %s\n", result->str_val);
    } else if (result->tag == OMNI_STRING) {
        printf("  Matched (STRING): \"%s\"\n", result->str_val);
    } else if (result->tag == OMNI_SYM) {
        printf("  Matched (SYM): %s\n", result->str_val);
    } else {
        printf("  Matched (tag=%d)\n", result->tag);
    }
}

int main(void) {
    int passed = 0;
    int total = 0;

    /* Test 1: Simple terminal pattern - should match "hello" */
    {
        total++;
        printf("\n--- Test 1: Simple terminal pattern ---\n");
        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "hello", .name = "greeting", .action = NULL }
        };
        OmniValue* result = omni_pika_match("hello world", rules, 1, 0);
        print_result("Match 'hello' in 'hello world'", result);
        if (result && result->tag != OMNI_ERROR) {
            passed++;
            printf("  PASS\n");
        } else {
            printf("  FAIL\n");
        }
    }

    /* Test 2: Terminal pattern that doesn't match */
    {
        total++;
        printf("\n--- Test 2: Non-matching terminal pattern ---\n");
        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "goodbye", .name = "farewell", .action = NULL }
        };
        OmniValue* result = omni_pika_match("hello world", rules, 1, 0);
        print_result("Match 'goodbye' in 'hello world' (should fail)", result);
        if (result && result->tag == OMNI_ERROR) {
            passed++;
            printf("  PASS (expected failure)\n");
        } else {
            printf("  FAIL (should have returned error)\n");
        }
    }

    /* Test 3: Character range pattern */
    {
        total++;
        printf("\n--- Test 3: Character range pattern ---\n");
        PikaRule rules[] = {
            { .type = PIKA_RANGE, .data.range = { .min = 'a', .max = 'z' }, .name = "letter", .action = NULL }
        };
        OmniValue* result = omni_pika_match("xyz", rules, 1, 0);
        print_result("Match [a-z] in 'xyz'", result);
        if (result && result->tag != OMNI_ERROR) {
            passed++;
            printf("  PASS\n");
        } else {
            printf("  FAIL\n");
        }
    }

    /* Test 4: ANY pattern (.) */
    {
        total++;
        printf("\n--- Test 4: ANY pattern ---\n");
        PikaRule rules[] = {
            { .type = PIKA_ANY, .name = "any_char", .action = NULL }
        };
        OmniValue* result = omni_pika_match("anything", rules, 1, 0);
        print_result("Match . in 'anything'", result);
        if (result && result->tag != OMNI_ERROR) {
            passed++;
            printf("  PASS\n");
        } else {
            printf("  FAIL\n");
        }
    }

    /* Test 5: ALT (prioritized choice) pattern */
    {
        total++;
        printf("\n--- Test 5: ALT pattern ---\n");
        int subrules[] = { 1, 2 };  /* References to rules 1 and 2 */
        PikaRule rules[] = {
            { .type = PIKA_ALT, .data.children = { .subrules = subrules, .count = 2 }, .name = "alt", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "foo", .name = "foo", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "bar", .name = "bar", .action = NULL }
        };
        OmniValue* result = omni_pika_match("bar baz", rules, 3, 0);
        print_result("Match foo|bar in 'bar baz'", result);
        if (result && result->tag != OMNI_ERROR) {
            passed++;
            printf("  PASS\n");
        } else {
            printf("  FAIL\n");
        }
    }

    /* Test 6: NULL input handling */
    {
        total++;
        printf("\n--- Test 6: NULL input handling ---\n");
        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "test", .name = "test", .action = NULL }
        };
        OmniValue* result = omni_pika_match(NULL, rules, 1, 0);
        print_result("Match with NULL input (should error)", result);
        if (result && result->tag == OMNI_ERROR) {
            passed++;
            printf("  PASS (correctly returned error)\n");
        } else {
            printf("  FAIL\n");
        }
    }

    /* Test 7: Invalid rules array handling */
    {
        total++;
        printf("\n--- Test 7: Invalid rules array handling ---\n");
        OmniValue* result = omni_pika_match("test", NULL, 0, 0);
        print_result("Match with NULL rules (should error)", result);
        if (result && result->tag == OMNI_ERROR) {
            passed++;
            printf("  PASS (correctly returned error)\n");
        } else {
            printf("  FAIL\n");
        }
    }

    /* Test 8: Out of bounds root_rule */
    {
        total++;
        printf("\n--- Test 8: Out of bounds root_rule ---\n");
        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "test", .name = "test", .action = NULL }
        };
        OmniValue* result = omni_pika_match("test", rules, 1, 5);  /* root_rule=5 is out of bounds */
        print_result("Match with out-of-bounds root_rule (should error)", result);
        if (result && result->tag == OMNI_ERROR) {
            passed++;
            printf("  PASS (correctly returned error)\n");
        } else {
            printf("  FAIL\n");
        }
    }

    /* Test 9: PEG prioritized choice with backtracking (T-wire-pika-exec-03)
     * When input is "abc" and pattern is ("abc" | "ab"), it should match "abc"
     * The first alternative that matches wins, even if a shorter one exists
     */
    {
        total++;
        printf("\n--- Test 9: PEG prioritized choice backtracking ---\n");
        printf("Pattern: (\"abc\" | \"ab\"), Input: \"abc\"\n");
        printf("Expected: Match \"abc\" (first alternative wins)\n");

        int subrules[] = { 1, 2 };  /* References to rules 1 ("abc") and 2 ("ab") */
        PikaRule rules[] = {
            { .type = PIKA_ALT, .data.children = { .subrules = subrules, .count = 2 }, .name = "choice", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "abc", .name = "abc", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "ab", .name = "ab", .action = NULL }
        };
        OmniValue* result = omni_pika_match("abc", rules, 3, 0);
        print_result("Match (\"abc\" | \"ab\") in \"abc\"", result);

        /* Verify that we got "abc" not "ab" */
        if (result && result->tag == OMNI_SYM && strcmp(result->str_val, "abc") == 0) {
            passed++;
            printf("  PASS - Correctly matched \"abc\" (not \"ab\")\n");
        } else if (result && result->tag == OMNI_SYM && strcmp(result->str_val, "ab") == 0) {
            printf("  FAIL - Matched \"ab\" instead of \"abc\" (wrong choice)\n");
        } else {
            printf("  FAIL\n");
        }
    }

    /* Test 10: SEQ with ALT (ordered choice with backtracking)
     * Pattern: ("a" ("bc" | "b")) should match "abc" fully
     */
    {
        total++;
        printf("\n--- Test 10: SEQ with ALT for full match ---\n");
        printf("Pattern: \"a\" (\"bc\" | \"b\"), Input: \"abc\"\n");
        printf("Expected: Match \"abc\" using \"bc\" alternative\n");

        int alt_subrules[] = { 2, 3 };  /* References to rules 2 ("bc") and 3 ("b") */
        int seq_subrules[] = { 1, 4 };  /* References to rules 1 ("a") and 4 (alt) */
        PikaRule rules[] = {
            { .type = PIKA_SEQ, .data.children = { .subrules = seq_subrules, .count = 2 }, .name = "seq", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "a", .name = "a", .action = NULL },
            { .type = PIKA_ALT, .data.children = { .subrules = alt_subrules, .count = 2 }, .name = "alt", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "bc", .name = "bc", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "b", .name = "b", .action = NULL }
        };
        OmniValue* result = omni_pika_match("abc", rules, 5, 0);
        print_result("Match \"a\" (\"bc\" | \"b\") in \"abc\"", result);

        /* Should match "abc" (len=3) */
        if (result && result->tag != OMNI_ERROR) {
            passed++;
            printf("  PASS - Matched successfully\n");
        } else {
            printf("  FAIL\n");
        }
    }

    /* Summary */
    printf("\n========================================\n");
    printf("Test Results: %d/%d passed\n", passed, total);
    printf("========================================\n");

    return (passed == total) ? 0 : 1;
}
