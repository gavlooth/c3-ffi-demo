#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/types.h"
#include "../src/runtime/pika/omni_grammar.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    /* Test symbols with hyphens */
    const char* inputs[] = {
        "(pika-match x)",
        "(pika-find-all x)",
        "(my-func x)",
        "(some-function x y)",
        "(test-hyphen x y z)",
    };
    
    for (int i = 0; i < 5; i++) {
        const char* input = inputs[i];
        printf("Input %d: %s\n", i+1, input);
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
