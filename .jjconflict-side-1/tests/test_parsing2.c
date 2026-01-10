#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/types.h"
#include "../src/runtime/pika/omni_grammar.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    /* Test 1: Simple string without space */
    const char* input1 = "(pika-match simple :greeting \"hello\")";
    printf("Input 1: %s\n", input1);
    OmniParseResult r1 = omni_pika_parse(input1, OMNI_OUTPUT_AST);
    printf("Success: %d\n", r1.success);
    if (r1.success) {
        printf("AST: ");
        char* ast_str = val_to_str(r1.ast);
        printf("%s\n", ast_str);
        free(ast_str);
        omni_parse_result_free(&r1);
    }
    
    /* Test 2: String with space */
    const char* input2 = "(pika-match simple :greeting \"hello world\")";
    printf("\nInput 2: %s\n", input2);
    OmniParseResult r2 = omni_pika_parse(input2, OMNI_OUTPUT_AST);
    printf("Success: %d\n", r2.success);
    if (r2.success) {
        printf("AST: ");
        char* ast_str = val_to_str(r2.ast);
        printf("%s\n", ast_str);
        free(ast_str);
        omni_parse_result_free(&r2);
    }
    
    /* Test 3: Parse with full input mode */
    printf("\nInput 3 (parse all): %s\n", input2);
    OmniParseResult r3 = omni_pika_parse_all(input2, OMNI_OUTPUT_AST);
    printf("Success: %d\n", r3.success);
    if (r3.success) {
        printf("AST: ");
        char* ast_str = val_to_str(r3.ast);
        printf("%s\n", ast_str);
        free(ast_str);
        omni_parse_result_free(&r3);
    }
    
    return 0;
}
