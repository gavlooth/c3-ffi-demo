#include "../src/runtime/types.h"
#include "../src/runtime/pika/omni_grammar.h"
#include "../src/runtime/tower/tower.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("Test: full currying\n");
    fflush(stdout);
    
    OmniParseResult r = omni_pika_parse("(((lambda (x) (lambda (y) (+ x y))) 3) 4)", OMNI_OUTPUT_AST);
    char* ast = val_to_str(r.ast);
    printf("AST: %s\n", ast);
    fflush(stdout);
    free(ast);
    
    printf("Initializing tower...\n");
    fflush(stdout);
    TowerMEnv* menv = tower_init();
    
    printf("Evaluating inner application first...\n");
    fflush(stdout);
    
    /* Parse just the inner part */
    OmniParseResult r2 = omni_pika_parse("((lambda (x) (lambda (y) (+ x y))) 3)", OMNI_OUTPUT_AST);
    Value* inner_result = tower_eval(r2.ast, menv);
    printf("Inner result tag: %d\n", inner_result->tag);
    fflush(stdout);
    
    if (inner_result->tag == T_LAMBDA) {
        printf("Got lambda. Now apply to 4...\n");
        fflush(stdout);
        
        /* Create (inner_result 4) */
        Value* outer_expr = mk_cell(inner_result, mk_cell(mk_int(4), mk_nil()));
        char* outer_str = val_to_str(outer_expr);
        printf("Outer expr: %s\n", outer_str);
        fflush(stdout);
        free(outer_str);
        
        Value* final_result = tower_eval(outer_expr, menv);
        printf("Final result tag: %d\n", final_result->tag);
        fflush(stdout);
    }
    
    tower_cleanup();
    printf("Done!\n");
    return 0;
}
