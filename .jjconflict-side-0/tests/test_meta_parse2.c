#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/pika_c/pika.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    /* Test with different PEG formats */
    const char* pega = "pattern <- a";
    const char* pegb = "pattern <- \"a\"";
    const char* pegc = "pattern <- 'a'";
    const char* pegd = "pattern <- [a-z]";
    
    for (int i = 0; i < 4; i++) {
        const char* peg = (i == 0) ? pega : (i == 1) ? pegb : (i == 2) ? pegc : pegd;
        printf("Testing PEG %d: '%s'\n", i+1, peg);
        
        char* error = NULL;
        PikaGrammar* grammar = pika_meta_parse(peg, &error);
        if (error) {
            printf("  Error: %s\n", error);
            free(error);
        }
        if (grammar) {
            printf("  SUCCESS!\n");
            pika_grammar_free(grammar);
        } else {
            printf("  FAILED\n");
        }
        printf("\n");
    }
    
    return 0;
}
