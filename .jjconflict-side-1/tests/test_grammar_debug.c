#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/types.h"
#include "../src/runtime/pika/omni_grammar.h"
#include "../src/runtime/pika/pika_reader.h"
#include "../src/runtime/tower/tower.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    /* Test grammar definition */
    const char* program = "(define [grammar simple] [greeting \"hello\"])";
    printf("Parsing: %s\n", program);
    
    Value* expr = pika_parse(program);
    if (is_error(expr)) {
        printf("Parse error: %s\n", expr->s);
        return 1;
    }
    
    printf("Parsed AST: ");
    char* ast_str = val_to_str(expr);
    printf("%s\n", ast_str);
    free(ast_str);
    
    /* Evaluate */
    printf("Evaluating...\n");
    Value* result = tower_eval_string(program);
    if (is_error(result)) {
        printf("Eval error: %s\n", result->s);
        return 1;
    }
    
    printf("Result: ");
    char* result_str = val_to_str(result);
    printf("%s\n", result_str);
    free(result_str);
    
    /* Now test matching */
    const char* match_program = "(pika-match simple :greeting \"hello world\")";
    printf("\nParsing: %s\n", match_program);
    
    Value* match_expr = pika_parse(match_program);
    if (is_error(match_expr)) {
        printf("Parse error: %s\n", match_expr->s);
        return 1;
    }
    
    printf("Parsed AST: ");
    char* match_ast_str = val_to_str(match_expr);
    printf("%s\n", match_ast_str);
    free(match_ast_str);
    
    printf("Evaluating...\n");
    Value* match_result = tower_eval_string(match_program);
    if (is_error(match_result)) {
        printf("Match error: %s\n", match_result->s);
        return 1;
    }
    
    printf("Match result tag: %d\n", match_result->tag);
    if (match_result->tag == T_CODE) {
        printf("Match result: %s\n", match_result->s);
    } else {
        char* match_result_str = val_to_str(match_result);
        printf("Match result: %s\n", match_result_str);
        free(match_result_str);
    }
    
    tower_cleanup();
    return 0;
}
