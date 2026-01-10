#define _POSIX_C_SOURCE 200809L
#include "../src/runtime/types.h"
#include "../src/runtime/pika/pika_reader.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    const char* input = "(pika-match simple :greeting \"hello world\")";
    printf("Input: %s\n", input);
    
    Value* expr = pika_parse(input);
    if (is_error(expr)) {
        printf("Parse error: %s\n", expr->s);
        return 1;
    }
    
    printf("Parsed AST tag: %d\n", expr->tag);
    printf("Is T_CELL? %d\n", expr->tag == T_CELL);
    
    /* Print the full AST */
    printf("Full AST: ");
    char* ast_str = val_to_str(expr);
    printf("%s\n", ast_str);
    free(ast_str);
    
    /* Check if it's a list */
    if (expr->tag == T_CELL) {
        printf("It's a list (T_CELL)\n");
        printf("car: ");
        char* car_str = val_to_str(car(expr));
        printf("%s\n", car_str);
        free(car_str);
        
        printf("cdr: ");
        char* cdr_str = val_to_str(cdr(expr));
        printf("%s\n", cdr_str);
        free(cdr_str);
        
        /* Count elements */
        int count = 0;
        Value* rest = expr;
        while (!is_nil(rest)) {
            count++;
            printf("  Element %d: ", count);
            char* elem_str = val_to_str(car(rest));
            printf("%s\n", elem_str);
            free(elem_str);
            rest = cdr(rest);
        }
    }
    
    return 0;
}
