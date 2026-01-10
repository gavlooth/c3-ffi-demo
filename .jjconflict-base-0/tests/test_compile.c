#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/types.h"
#include "../src/runtime/pika/omni_grammar.h"
#include "../src/runtime/pika_c/pika.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    char* error = NULL;
    
    /* Test compiling a simple pattern */
    PikaGrammar* grammar = omni_compile_pattern("[0-9]+", &error);
    if (error) {
        printf("Compile error for '[0-9]+': %s\n", error);
        free(error);
    } else if (grammar) {
        printf("Grammar for '[0-9]+' compiled successfully!\n");
        
        /* Try to parse with it */
        PikaMemoTable* memo = pika_grammar_parse(grammar, "abc123def");
        if (memo) {
            printf("Parsed 'abc123def' successfully!\n");
            
            size_t match_count = 0;
            PikaMatch** matches = pika_memo_get_non_overlapping_matches_for_rule(
                memo, "pattern", &match_count
            );
            printf("Match count: %zu\n", match_count);
            if (match_count > 0 && matches) {
                printf("First match: start=%d, len=%d\n", 
                       pika_match_start(matches[0]), pika_match_len(matches[0]));
            }
            free(matches);
            pika_memo_free(memo);
        } else {
            printf("Failed to parse 'abc123def'\n");
        }
        pika_grammar_free(grammar);
    } else {
        printf("Failed to compile grammar for '[0-9]+'\n");
    }

    return 0;
}
