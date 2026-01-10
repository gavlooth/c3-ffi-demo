/*
 * Test: Pika Grammar to Code Transformation
 *
 * This test verifies that pika_codegen_rule and pika_codegen_grammar
 * correctly generate C code from PikaRule structures.
 */

#include "pika.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test helper: Check if generated code contains expected substring */
static bool code_contains(const char* code, const char* expected) {
    if (!code || !expected) return false;
    return strstr(code, expected) != NULL;
}

/* Test 1: Generate code for terminal rule (literal string) */
void test_codegen_terminal() {
    printf("Test 1: Terminal rule code generation...\n");

    PikaRule rule = {
        .type = PIKA_TERMINAL,
        .data.str = "hello",
        .name = "greeting"
    };

    char* code = pika_codegen_rule(&rule, 0, "test");
    assert(code != NULL);

    /* Verify generated code contains expected elements */
    assert(code_contains(code, "bool test_rule_0"));
    assert(code_contains(code, "strncmp(input + pos, \"hello\""));
    assert(code_contains(code, "*result_pos"));

    printf("  ✓ Generated code for terminal rule:\n");
    printf("    %s\n", code);

    free(code);
    printf("  PASSED\n\n");
}

/* Test 2: Generate code for character range rule */
void test_codegen_range() {
    printf("Test 2: Range rule code generation...\n");

    PikaRule rule = {
        .type = PIKA_RANGE,
        .data.range = { .min = 'a', .max = 'z' },
        .name = "lowercase"
    };

    char* code = pika_codegen_rule(&rule, 0, "test");
    assert(code != NULL);

    assert(code_contains(code, "bool test_rule_0"));
    assert(code_contains(code, "input[pos] >= 'a'"));
    assert(code_contains(code, "input[pos] <= 'z'"));

    printf("  ✓ Generated code for range rule:\n");
    printf("    %s\n", code);

    free(code);
    printf("  PASSED\n\n");
}

/* Test 3: Generate code for sequence rule */
void test_codegen_seq() {
    printf("Test 3: Sequence rule code generation...\n");

    /* Create subrules */
    int subrule_ids[] = {1, 2};
    PikaRule rule = {
        .type = PIKA_SEQ,
        .data.children = { .subrules = subrule_ids, .count = 2 },
        .name = "seq_test"
    };

    char* code = pika_codegen_rule(&rule, 0, "test");
    assert(code != NULL);

    assert(code_contains(code, "bool test_rule_0"));
    assert(code_contains(code, "current_pos"));
    assert(code_contains(code, "test_rule_1"));
    assert(code_contains(code, "test_rule_2"));

    printf("  ✓ Generated code for sequence rule:\n");
    printf("    %s\n", code);

    free(code);
    printf("  PASSED\n\n");
}

/* Test 4: Generate code for alternation rule */
void test_codegen_alt() {
    printf("Test 4: Alternation rule code generation...\n");

    int subrule_ids[] = {1, 2};
    PikaRule rule = {
        .type = PIKA_ALT,
        .data.children = { .subrules = subrule_ids, .count = 2 },
        .name = "alt_test"
    };

    char* code = pika_codegen_rule(&rule, 0, "test");
    assert(code != NULL);

    assert(code_contains(code, "bool test_rule_0"));
    assert(code_contains(code, "alt_pos"));
    assert(code_contains(code, "test_rule_1"));
    assert(code_contains(code, "test_rule_2"));

    printf("  ✓ Generated code for alternation rule:\n");
    printf("    %s\n", code);

    free(code);
    printf("  PASSED\n\n");
}

/* Test 5: Generate code for repetition (zero or more) */
void test_codegen_rep() {
    printf("Test 5: Repetition rule code generation (A*)...\n");

    int subrule_ids[] = {1};
    PikaRule rule = {
        .type = PIKA_REP,
        .data.children = { .subrules = subrule_ids, .count = 1 },
        .name = "rep_test"
    };

    char* code = pika_codegen_rule(&rule, 0, "test");
    assert(code != NULL);

    assert(code_contains(code, "bool test_rule_0"));
    assert(code_contains(code, "while (test_rule_1"));
    assert(code_contains(code, "return true"));

    printf("  ✓ Generated code for repetition rule:\n");
    printf("    %s\n", code);

    free(code);
    printf("  PASSED\n\n");
}

/* Test 6: Generate code for positive lookahead */
void test_codegen_and() {
    printf("Test 6: Positive lookahead rule code generation (&A)...\n");

    int subrule_ids[] = {1};
    PikaRule rule = {
        .type = PIKA_AND,
        .data.children = { .subrules = subrule_ids, .count = 1 },
        .name = "and_test"
    };

    char* code = pika_codegen_rule(&rule, 0, "test");
    assert(code != NULL);

    assert(code_contains(code, "bool test_rule_0"));
    assert(code_contains(code, "and_pos"));
    assert(code_contains(code, "*result_pos = pos"));

    printf("  ✓ Generated code for positive lookahead rule:\n");
    printf("    %s\n", code);

    free(code);
    printf("  PASSED\n\n");
}

/* Test 7: Generate code for negative lookahead */
void test_codegen_not() {
    printf("Test 7: Negative lookahead rule code generation (!A)...\n");

    int subrule_ids[] = {1};
    PikaRule rule = {
        .type = PIKA_NOT,
        .data.children = { .subrules = subrule_ids, .count = 1 },
        .name = "not_test"
    };

    char* code = pika_codegen_rule(&rule, 0, "test");
    assert(code != NULL);

    assert(code_contains(code, "bool test_rule_0"));
    assert(code_contains(code, "not_pos"));
    assert(code_contains(code, "!test_rule_1"));

    printf("  ✓ Generated code for negative lookahead rule:\n");
    printf("    %s\n", code);

    free(code);
    printf("  PASSED\n\n");
}

/* Test 8: Generate code for complete grammar */
void test_codegen_grammar() {
    printf("Test 8: Complete grammar code generation...\n");

    /* Create a simple arithmetic grammar:
     *   expr  := term ('+' term)*
     *   term  := factor ('*' factor)*
     *   factor := NUMBER
     */
    int subrule_ids_1[] = {1};
    PikaRule rules[3] = {
        {   /* Rule 0: expr := term ('+' term)* */
            .type = PIKA_SEQ,
            .data.children = { .subrules = (int[]){1, 1}, .count = 2 },  /* Simplified */
            .name = "expr"
        },
        {   /* Rule 1: term := factor ('*' factor)* */
            .type = PIKA_SEQ,
            .data.children = { .subrules = (int[]){2, 2}, .count = 2 },  /* Simplified */
            .name = "term"
        },
        {   /* Rule 2: factor := NUMBER */
            .type = PIKA_TERMINAL,
            .data.str = "[0-9]+",  /* Would normally be a more complex pattern */
            .name = "factor"
        }
    };

    char* code = pika_codegen_grammar(rules, 3, "arithmetic");
    assert(code != NULL);

    /* Verify complete module structure */
    assert(code_contains(code, "Generated Pika Grammar Matcher: arithmetic"));
    assert(code_contains(code, "#include <stdbool.h>"));
    assert(code_contains(code, "bool arithmetic_rule_0"));
    assert(code_contains(code, "bool arithmetic_rule_1"));
    assert(code_contains(code, "bool arithmetic_rule_2"));
    assert(code_contains(code, "bool arithmetic_match"));

    printf("  ✓ Generated complete grammar module:\n");
    printf("    First 500 chars:\n");
    printf("    %.500s...\n", code);

    free(code);
    printf("  PASSED\n\n");
}

/* Test 9: Error handling for NULL rule */
void test_codegen_null_rule() {
    printf("Test 9: Error handling for NULL rule...\n");

    char* code = pika_codegen_rule(NULL, 0, "test");
    assert(code == NULL);

    printf("  ✓ Correctly returns NULL for NULL rule\n");
    printf("  PASSED\n\n");
}

/* Test 10: Error handling for NULL name */
void test_codegen_null_name() {
    printf("Test 10: Error handling for NULL name...\n");

    PikaRule rule = {
        .type = PIKA_TERMINAL,
        .data.str = "test"
    };

    /* Should use default name "pika" instead of failing */
    char* code = pika_codegen_rule(&rule, 0, NULL);
    assert(code != NULL);
    assert(code_contains(code, "bool pika_rule_0"));

    free(code);
    printf("  ✓ Uses default name 'pika' when name is NULL\n");
    printf("  PASSED\n\n");
}

int main(void) {
    printf("========================================\n");
    printf("Pika Code Generation Tests\n");
    printf("========================================\n\n");

    test_codegen_terminal();
    test_codegen_range();
    test_codegen_seq();
    test_codegen_alt();
    test_codegen_rep();
    test_codegen_and();
    test_codegen_not();
    test_codegen_grammar();
    test_codegen_null_rule();
    test_codegen_null_name();

    printf("========================================\n");
    printf("All 10 tests PASSED!\n");
    printf("========================================\n");

    return 0;
}
