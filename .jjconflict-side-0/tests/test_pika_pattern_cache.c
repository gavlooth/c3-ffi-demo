/*
 * Test for Pika Pattern Cache (T-wire-pika-compile-03)
 * Verifies that pattern caching works correctly and improves performance
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

    /* Test 1: Verify cache returns same pointer for identical patterns */
    {
        total++;
        printf("\n--- Test 1: Cache returns same pointer for identical patterns ---\n");
        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "hello", .name = "greeting", .action = NULL }
        };

        /* Compile same pattern twice */
        PikaState* compiled1 = omni_compile_pattern("hello world", rules, 1);
        PikaState* compiled2 = omni_compile_pattern("hello world", rules, 1);

        if (!compiled1 || !compiled2) {
            printf("  FAIL: omni_compile_pattern returned NULL\n");
        } else if (compiled1 == compiled2) {
            passed++;
            printf("  PASS: Both calls returned same pointer (cache hit)\n");
        } else {
            printf("  FAIL: Calls returned different pointers (cache miss)\n");
        }

        /* Note: Cache owns the PikaState, don't free it here */
        (void)compiled1; /* Suppress unused warning */
    }

    /* Test 2: Verify cache returns different pointers for different patterns */
    {
        total++;
        printf("\n--- Test 2: Cache returns different pointers for different patterns ---\n");
        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "hello", .name = "greeting", .action = NULL }
        };

        /* Compile different patterns */
        PikaState* compiled1 = omni_compile_pattern("hello world", rules, 1);
        PikaState* compiled2 = omni_compile_pattern("goodbye world", rules, 1);

        if (!compiled1 || !compiled2) {
            printf("  FAIL: omni_compile_pattern returned NULL\n");
        } else if (compiled1 != compiled2) {
            passed++;
            printf("  PASS: Different patterns returned different pointers\n");
        } else {
            printf("  FAIL: Different patterns returned same pointer (collision error)\n");
        }

        /* Note: Cache owns the PikaState, don't free it here */
    }

    /* Test 3: Verify cache statistics work correctly */
    {
        total++;
        printf("\n--- Test 3: Cache statistics tracking ---\n");

        /* Clear cache first */
        pika_pattern_cache_clear();

        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "test", .name = "test", .action = NULL }
        };

        PatternCacheStats stats_before;
        pika_pattern_cache_stats(&stats_before);

        /* Compile a pattern */
        PikaState* compiled1 = omni_compile_pattern("test input", rules, 1);
        PikaState* compiled2 = omni_compile_pattern("test input", rules, 1);  /* Should hit cache */

        PatternCacheStats stats_after;
        pika_pattern_cache_stats(&stats_after);

        if (!compiled1 || !compiled2) {
            printf("  FAIL: omni_compile_pattern returned NULL\n");
        } else if (stats_after.entry_count == 1 && stats_before.entry_count == 0) {
            passed++;
            printf("  PASS: Cache stats show 1 entry (not 2, proving cache works)\n");
            printf("  Before: %zu entries, After: %zu entries\n",
                   stats_before.entry_count, stats_after.entry_count);
        } else {
            printf("  FAIL: Expected 1 entry, got %zu\n", stats_after.entry_count);
        }

        /* Note: Cache owns the PikaState, don't free it here */
    }

    /* Test 4: Verify cached patterns still work correctly */
    {
        total++;
        printf("\n--- Test 4: Cached patterns produce correct results ---\n");

        /* Clear cache first */
        pika_pattern_cache_clear();

        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "hello", .name = "greeting", .action = NULL }
        };

        /* First compilation */
        PikaState* compiled1 = omni_compile_pattern("hello world", rules, 1);
        OmniValue* result1 = pika_run(compiled1, 0);

        /* Second compilation (should hit cache) */
        PikaState* compiled2 = omni_compile_pattern("hello world", rules, 1);
        OmniValue* result2 = pika_run(compiled2, 0);

        print_result("First compilation result", result1);
        printf("  Second compilation result: %s\n",
               (result2 && result2->tag != OMNI_ERROR) ? "success" : "failed");

        if (result1 && result1->tag != OMNI_ERROR &&
            result2 && result2->tag != OMNI_ERROR) {
            passed++;
            printf("  PASS: Both cached and fresh compilations produce correct results\n");
        } else {
            printf("  FAIL: Cached compilation produced wrong result\n");
        }

        /* Note: Cache owns the PikaState, don't free it here */
    }

    /* Test 5: Verify cache handles different rules correctly */
    {
        total++;
        printf("\n--- Test 5: Cache distinguishes different rules ---\n");

        /* Clear cache first */
        pika_pattern_cache_clear();

        /* Two different rule sets */
        PikaRule rules1[] = {
            { .type = PIKA_TERMINAL, .data.str = "hello", .name = "greeting", .action = NULL }
        };
        PikaRule rules2[] = {
            { .type = PIKA_TERMINAL, .data.str = "goodbye", .name = "farewell", .action = NULL }
        };

        /* Compile same pattern with different rules */
        PikaState* compiled1 = omni_compile_pattern("hello world", rules1, 1);
        PikaState* compiled2 = omni_compile_pattern("hello world", rules2, 1);

        if (!compiled1 || !compiled2) {
            printf("  FAIL: omni_compile_pattern returned NULL\n");
        } else if (compiled1 != compiled2) {
            passed++;
            printf("  PASS: Different rules produce different compiled states\n");
        } else {
            printf("  FAIL: Different rules produced same compiled state\n");
        }

        /* Note: Cache owns the PikaState, don't free it here */
    }

    /* Test 6: Verify cache clear works */
    {
        total++;
        printf("\n--- Test 6: Cache clear functionality ---\n");

        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "test", .name = "test", .action = NULL }
        };

        /* Compile a pattern */
        PikaState* compiled1 = omni_compile_pattern("test input", rules, 1);
        PikaState* compiled2 = omni_compile_pattern("test input", rules, 1);  /* Cache hit */

        /* Clear cache */
        pika_pattern_cache_clear();

        /* Compile again - should get new pointer */
        PikaState* compiled3 = omni_compile_pattern("test input", rules, 1);

        if (!compiled1 || !compiled2 || !compiled3) {
            printf("  FAIL: omni_compile_pattern returned NULL\n");
        } else if (compiled1 == compiled2 && compiled1 != compiled3) {
            passed++;
            printf("  PASS: Cache clear works, new compilation gets new pointer\n");
        } else {
            printf("  FAIL: Cache clear didn't work as expected\n");
            printf("  compiled1 == compiled2: %s\n", compiled1 == compiled2 ? "yes" : "no");
            printf("  compiled1 == compiled3: %s\n", compiled1 == compiled3 ? "yes" : "no");
        }

        /* Note: Cache owns the PikaState, don't free it here */
    }

    /* Final cleanup */
    pika_pattern_cache_clear();

    /* Summary */
    printf("\n========================================\n");
    printf("Pattern Cache Test Results: %d/%d passed\n", passed, total);
    printf("========================================\n");

    return (passed == total) ? 0 : 1;
}
