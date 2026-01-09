/*
 * Test for omni_compile_pattern function
 * Verifies T-wire-pika-compile-01 implementation
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

    /* Test 1: Compile a simple pattern and use it for matching */
    {
        total++;
        printf("\n--- Test 1: Compile and match simple pattern ---\n");
        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "hello", .name = "greeting", .action = NULL }
        };
        PikaState* compiled = omni_compile_pattern("hello world", rules, 1);
        if (!compiled) {
            printf("  FAIL: omni_compile_pattern returned NULL\n");
        } else {
            OmniValue* result = pika_run(compiled, 0);
            print_result("Compile 'hello' pattern and match against 'hello world'", result);
            if (result && result->tag != OMNI_ERROR) {
                passed++;
                printf("  PASS\n");
            } else {
                printf("  FAIL\n");
            }
            pika_free(compiled);
        }
    }

    /* Test 2: Compile pattern with NULL input */
    {
        total++;
        printf("\n--- Test 2: Compile NULL pattern (should return NULL) ---\n");
        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "test", .name = "test", .action = NULL }
        };
        PikaState* compiled = omni_compile_pattern(NULL, rules, 1);
        if (compiled == NULL) {
            passed++;
            printf("  PASS (correctly returned NULL)\n");
        } else {
            printf("  FAIL (should have returned NULL)\n");
            pika_free(compiled);
        }
    }

    /* Test 3: Compile pattern with NULL rules */
    {
        total++;
        printf("\n--- Test 3: Compile with NULL rules (should return NULL) ---\n");
        PikaState* compiled = omni_compile_pattern("test", NULL, 0);
        if (compiled == NULL) {
            passed++;
            printf("  PASS (correctly returned NULL)\n");
        } else {
            printf("  FAIL (should have returned NULL)\n");
            pika_free(compiled);
        }
    }

    /* Test 4: Compile and match character range pattern */
    {
        total++;
        printf("\n--- Test 4: Compile and match character range ---\n");
        PikaRule rules[] = {
            { .type = PIKA_RANGE, .data.range = { .min = '0', .max = '9' }, .name = "digit", .action = NULL }
        };
        PikaState* compiled = omni_compile_pattern("42 apples", rules, 1);
        if (!compiled) {
            printf("  FAIL: omni_compile_pattern returned NULL\n");
        } else {
            OmniValue* result = pika_run(compiled, 0);
            print_result("Compile [0-9] pattern and match against '42 apples'", result);
            if (result && result->tag != OMNI_ERROR) {
                passed++;
                printf("  PASS\n");
            } else {
                printf("  FAIL\n");
            }
            pika_free(compiled);
        }
    }

    /* Test 5: Compile pattern and use it multiple times */
    {
        total++;
        printf("\n--- Test 5: Reuse compiled pattern multiple times ---\n");
        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "test", .name = "test", .action = NULL }
        };
        PikaState* compiled = omni_compile_pattern("test test test", rules, 1);
        if (!compiled) {
            printf("  FAIL: omni_compile_pattern returned NULL\n");
        } else {
            /* Run the parser multiple times on the same compiled state */
            OmniValue* result1 = pika_run(compiled, 0);
            OmniValue* result2 = pika_run(compiled, 0);
            print_result("First match", result1);
            printf("  Second match: %s\n", (result2 && result2->tag != OMNI_ERROR) ? "success" : "failed");
            if (result1 && result1->tag != OMNI_ERROR && result2 && result2->tag != OMNI_ERROR) {
                passed++;
                printf("  PASS\n");
            } else {
                printf("  FAIL\n");
            }
            pika_free(compiled);
        }
    }

    /* Test 6: Compile complex pattern with sequence */
    {
        total++;
        printf("\n--- Test 6: Compile and match sequence pattern ---\n");
        int subrules[] = {1, 2};
        PikaRule rules[] = {
            { .type = PIKA_SEQ, .data.children = { .subrules = subrules, .count = 2 }, .name = "seq", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "hello", .name = "hello", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = " world", .name = "world", .action = NULL }
        };
        PikaState* compiled = omni_compile_pattern("hello world", rules, 3);
        if (!compiled) {
            printf("  FAIL: omni_compile_pattern returned NULL\n");
        } else {
            OmniValue* result = pika_run(compiled, 0);
            print_result("Compile 'hello' + ' world' sequence pattern", result);
            if (result && result->tag != OMNI_ERROR) {
                passed++;
                printf("  PASS\n");
            } else {
                printf("  FAIL\n");
            }
            pika_free(compiled);
        }
    }

    /* Summary */
    printf("\n========================================\n");
    printf("Test Results: %d/%d passed\n", passed, total);
    printf("========================================\n");

    return (passed == total) ? 0 : 1;
}
