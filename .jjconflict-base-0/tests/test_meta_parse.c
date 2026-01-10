#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/pika_c/pika.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    /* Try parsing a very simple PEG */
    const char* simple_peg = "pattern <- \"a\"\n";
    char* error = NULL;
    
    printf("Testing PEG: '%s'\n", simple_peg);
    
    PikaGrammar* grammar = pika_meta_parse(simple_peg, &error);
    if (error) {
        printf("Error: %s\n", error);
        free(error);
    }
    if (grammar) {
        printf("SUCCESS! Grammar compiled.\n");
        pika_grammar_free(grammar);
    } else {
        printf("FAILED to compile grammar.\n");
    }
    
    return 0;
}
