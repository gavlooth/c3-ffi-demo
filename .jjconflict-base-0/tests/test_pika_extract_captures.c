/*
 * Test for pika_extract_captures function
 * Verifies T-wire-pika-exec-02 implementation
 */

#include "../csrc/parser/pika.h"
#include "../csrc/ast/ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper function to print OmniValue array results */
static void print_captures(const char* test_name, OmniValue* result) {
    printf("%s:\n", test_name);
    if (!result) {
        printf("  FAILED: result is NULL\n");
        return;
    }

    if (result->tag == OMNI_ERROR) {
        printf("  Error: %s\n", result->str_val);
    } else if (result->tag == OMNI_ARRAY) {
        printf("  Captures (%zu elements):\n", omni_array_len(result));
        for (size_t i = 0; i < omni_array_len(result); i++) {
            OmniValue* elem = omni_array_get(result, i);
            if (elem && elem->tag == OMNI_STRING) {
                printf("    [%zu] = \"%s\"\n", i, elem->str_val);
            } else if (elem && elem->tag == OMNI_ERROR) {
                printf("    [%zu] = Error: %s\n", i, elem->str_val);
            } else {
                printf("    [%zu] = (tag=%d)\n", i, elem ? elem->tag : -1);
            }
        }
    } else {
        printf("  Unexpected tag: %d\n", result->tag);
    }
}

int main(void) {
    int passed = 0;
    int total = 0;

    /* Test 1: Simple capture - extract two terminal matches */
    {
        total++;
        printf("\n--- Test 1: Extract simple terminal captures ---\n");
        printf("Input: \"hello world\"\n");

        /* Define rules: alt between "hello" and "world" */
        int subrules[] = { 1, 2 };
        PikaRule rules[] = {
            { .type = PIKA_ALT, .data.children = { .subrules = subrules, .count = 2 }, .name = "alt", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "hello", .name = "hello", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "world", .name = "world", .action = NULL }
        };

        /* Create parser state and run */
        PikaState* state = pika_new("hello world", rules, 3);
        if (!state) {
            printf("  FAILED: Could not create parser state\n");
        } else {
            pika_run(state, 0);  /* Run the alt rule */

            /* Extract both rules at position 0 */
            int rule_ids[] = {1, 2};
            size_t positions[] = {0, 0};
            OmniValue* captures = pika_extract_captures(state, rule_ids, positions, 2);
            print_captures("Extract 'hello' and 'world' at position 0", captures);

            /* Should capture "hello" at position 0 */
            if (captures && captures->tag == OMNI_ARRAY && omni_array_len(captures) == 2) {
                OmniValue* first = omni_array_get(captures, 0);
                if (first && first->tag == OMNI_STRING && strcmp(first->str_val, "hello") == 0) {
                    passed++;
                    printf("  PASS\n");
                } else {
                    printf("  FAIL - First capture should be \"hello\"\n");
                }
            } else {
                printf("  FAIL\n");
            }

            pika_free(state);
        }
    }

    /* Test 2: Email-style pattern - extract user and host parts */
    {
        total++;
        printf("\n--- Test 2: Email pattern (user@host) capture ---\n");
        printf("Input: \"user@host\"\n");

        /* Define rules: user_part + "@" + host_part */
        int seq_subrules[] = { 1, 2, 3 };
        PikaRule rules[] = {
            { .type = PIKA_SEQ, .data.children = { .subrules = seq_subrules, .count = 3 }, .name = "email", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "user", .name = "user_part", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "@", .name = "at", .action = NULL },
            { .type = PIKA_TERMINAL, .data.str = "host", .name = "host_part", .action = NULL }
        };

        /* Create parser state and run */
        PikaState* state = pika_new("user@host", rules, 4);
        if (!state) {
            printf("  FAILED: Could not create parser state\n");
        } else {
            pika_run(state, 0);

            /* Extract user_part (rule 1 at pos 0) and host_part (rule 3 at pos 5) */
            int rule_ids[] = {1, 3};
            size_t positions[] = {0, 5};  /* user@host = 012345678 */
            OmniValue* captures = pika_extract_captures(state, rule_ids, positions, 2);
            print_captures("Extract 'user' and 'host' from email", captures);

            /* Should capture "user" and "host" */
            if (captures && captures->tag == OMNI_ARRAY && omni_array_len(captures) == 2) {
                OmniValue* first = omni_array_get(captures, 0);
                OmniValue* second = omni_array_get(captures, 1);
                if (first && first->tag == OMNI_STRING && strcmp(first->str_val, "user") == 0 &&
                    second && second->tag == OMNI_STRING && strcmp(second->str_val, "host") == 0) {
                    passed++;
                    printf("  PASS\n");
                } else {
                    printf("  FAIL - Expected [\"user\", \"host\"]\n");
                }
            } else {
                printf("  FAIL\n");
            }

            pika_free(state);
        }
    }

    /* Test 3: NULL state handling */
    {
        total++;
        printf("\n--- Test 3: NULL state handling ---\n");

        int rule_ids[] = {0};
        size_t positions[] = {0};
        OmniValue* result = pika_extract_captures(NULL, rule_ids, positions, 1);
        print_captures("Extract with NULL state", result);

        if (result && result->tag == OMNI_ERROR) {
            passed++;
            printf("  PASS (correctly returned error)\n");
        } else {
            printf("  FAIL\n");
        }
    }

    /* Test 4: Empty captures (num_captures = 0) */
    {
        total++;
        printf("\n--- Test 4: Empty captures array ---\n");

        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "test", .name = "test", .action = NULL }
        };
        PikaState* state = pika_new("test", rules, 1);

        if (state) {
            pika_run(state, 0);
            OmniValue* result = pika_extract_captures(state, NULL, NULL, 0);
            print_captures("Extract with 0 captures", result);

            if (result && result->tag == OMNI_ARRAY && omni_array_len(result) == 0) {
                passed++;
                printf("  PASS (returned empty array)\n");
            } else {
                printf("  FAIL\n");
            }

            pika_free(state);
        }
    }

    /* Test 5: Invalid rule ID */
    {
        total++;
        printf("\n--- Test 5: Invalid rule ID ---\n");

        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "test", .name = "test", .action = NULL }
        };
        PikaState* state = pika_new("test", rules, 1);

        if (state) {
            pika_run(state, 0);
            int rule_ids[] = {99};  /* Invalid rule ID */
            size_t positions[] = {0};
            OmniValue* result = pika_extract_captures(state, rule_ids, positions, 1);
            print_captures("Extract with invalid rule ID", result);

            if (result && result->tag == OMNI_ARRAY && omni_array_len(result) == 1) {
                OmniValue* first = omni_array_get(result, 0);
                if (first && first->tag == OMNI_STRING &&
                    strcmp(first->str_val, "<invalid rule id>") == 0) {
                    passed++;
                    printf("  PASS (correctly handled invalid rule)\n");
                } else {
                    printf("  FAIL\n");
                }
            } else {
                printf("  FAIL\n");
            }

            pika_free(state);
        }
    }

    /* Test 6: Position out of bounds */
    {
        total++;
        printf("\n--- Test 6: Position out of bounds ---\n");

        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "test", .name = "test", .action = NULL }
        };
        PikaState* state = pika_new("test", rules, 1);

        if (state) {
            pika_run(state, 0);
            int rule_ids[] = {0};
            size_t positions[] = {100};  /* Way past end of "test" */
            OmniValue* result = pika_extract_captures(state, rule_ids, positions, 1);
            print_captures("Extract with out-of-bounds position", result);

            if (result && result->tag == OMNI_ARRAY && omni_array_len(result) == 1) {
                OmniValue* first = omni_array_get(result, 0);
                if (first && first->tag == OMNI_STRING &&
                    strcmp(first->str_val, "<position out of bounds>") == 0) {
                    passed++;
                    printf("  PASS (correctly handled out-of-bounds position)\n");
                } else {
                    printf("  FAIL\n");
                }
            } else {
                printf("  FAIL\n");
            }

            pika_free(state);
        }
    }

    /* Test 7: No match at specified position */
    {
        total++;
        printf("\n--- Test 7: No match at specified position ---\n");

        PikaRule rules[] = {
            { .type = PIKA_TERMINAL, .data.str = "hello", .name = "hello", .action = NULL }
        };
        PikaState* state = pika_new("world", rules, 1);

        if (state) {
            pika_run(state, 0);
            int rule_ids[] = {0};
            size_t positions[] = {0};  /* "hello" doesn't match at position 0 */
            OmniValue* result = pika_extract_captures(state, rule_ids, positions, 1);
            print_captures("Extract with no match at position", result);

            if (result && result->tag == OMNI_ARRAY && omni_array_len(result) == 1) {
                OmniValue* first = omni_array_get(result, 0);
                if (first && first->tag == OMNI_STRING && strcmp(first->str_val, "") == 0) {
                    passed++;
                    printf("  PASS (returned empty string for no match)\n");
                } else {
                    printf("  FAIL\n");
                }
            } else {
                printf("  FAIL\n");
            }

            pika_free(state);
        }
    }

    /* Summary */
    printf("\n========================================\n");
    printf("Test Results: %d/%d passed\n", passed, total);
    printf("========================================\n");

    return (passed == total) ? 0 : 1;
}
