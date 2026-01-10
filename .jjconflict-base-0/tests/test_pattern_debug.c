#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/pika/omni_grammar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    const char* pattern = "^foo";
    char* error = NULL;
    
    /* Try to compile the pattern */
    PikaGrammar* grammar = omni_compile_pattern(pattern, &error);
    if (error) {
        printf("Error: %s\n", error);
        free(error);
    }
    if (grammar) {
        printf("Grammar compiled successfully!\n");
        pika_grammar_free(grammar);
    } else {
        printf("Failed to compile grammar\n");
    }
    
    /* Try a simpler pattern */
    pattern = "foo";
    grammar = omni_compile_pattern(pattern, &error);
    if (error) {
        printf("Error: %s\n", error);
        free(error);
    }
    if (grammar) {
        printf("Simple pattern compiled successfully!\n");
        pika_grammar_free(grammar);
    } else {
        printf("Failed to compile simple pattern\n");
    }
    
    return 0;
}
