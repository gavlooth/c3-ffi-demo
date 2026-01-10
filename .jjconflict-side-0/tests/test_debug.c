#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/types.h"
#include "../src/runtime/pika/omni_grammar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Declare the internal function we want to test */
extern char* regex_to_peg(const char* pattern, char** error_out);

int main() {
    char* error = NULL;
    char* peg = regex_to_peg("[0-9]+", &error);
    if (error) {
        printf("Error: %s\n", error);
        free(error);
    }
    if (peg) {
        printf("PEG for '[0-9]+':\n%s\n", peg);
        free(peg);
    } else {
        printf("Failed to generate PEG\n");
    }

    peg = regex_to_peg("foo", &error);
    if (error) free(error);
    error = NULL;
    if (peg) {
        printf("PEG for 'foo':\n%s\n", peg);
        free(peg);
    }

    peg = regex_to_peg("^foo", &error);
    if (error) free(error);
    error = NULL;
    if (peg) {
        printf("PEG for '^foo':\n%s\n", peg);
        free(peg);
    }

    /* Try to compile the grammar */
    PikaGrammar* grammar = omni_compile_pattern("[0-9]+", &error);
    if (error) {
        printf("Compile error: %s\n", error);
        free(error);
    }
    if (grammar) {
        printf("Grammar compiled successfully!\n");
        pika_grammar_free(grammar);
    } else {
        printf("Failed to compile grammar\n");
    }

    return 0;
}
