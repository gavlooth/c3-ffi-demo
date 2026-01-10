#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/types.h"
#include "../src/runtime/pika/omni_grammar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main() {
    /* Test existing pattern that was working */
    Value* result = omni_pika_match("[0-9]+", "abc123def");
    printf("Test 1: [0-9]+ in abc123def\n");
    if (result && result->tag == T_CODE) {
        printf("  Matched: '%s'\n", result->s);
    } else {
        printf("  FAILED\n");
    }

    /* Test simple pattern */
    result = omni_pika_match("foo", "food bar");
    printf("\nTest 2: foo in food bar\n");
    if (result && result->tag == T_CODE) {
        printf("  Matched: '%s'\n", result->s);
    } else {
        printf("  FAILED\n");
    }

    /* Test alternation */
    result = omni_pika_match("foo|bar", "bar baz");
    printf("\nTest 3: foo|bar in bar baz\n");
    if (result && result->tag == T_CODE) {
        printf("  Matched: '%s'\n", result->s);
    } else {
        printf("  FAILED\n");
    }

    /* Test start anchor - this is the failing test */
    result = omni_pika_match("^foo", "foo bar");
    printf("\nTest 4: ^foo in foo bar\n");
    if (result && result->tag == T_CODE) {
        printf("  Matched: '%s'\n", result->s);
    } else {
        printf("  FAILED (result = %p)\n", (void*)result);
        if (result) {
            printf("  tag = %d\n", result->tag);
        }
    }

    return 0;
}
