#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/types.h"
#include "../src/runtime/pika/omni_grammar.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    const char* inputs[] = {
        "(test \"hello\")",
        "(test \"hello world\")",
        "(test \"hello world test\")",
    };
    
    for (int i = 0; i < 3; i++) {
        const char* input = inputs[i];
        printf("Input: %s\n", input);
        OmniParseResult r = omni_pika_parse(input, OMNI_OUTPUT_AST);
        printf("Success: %d\n", r.success);
        if (r.success) {
            printf("AST: ");
            char* ast_str = val_to_str(r.ast);
            printf("%s\n", ast_str);
            free(ast_str);
            omni_parse_result_free(&r);
        }
        printf("\n");
    }
    
    return 0;
}
