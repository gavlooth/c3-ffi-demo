#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/runtime/pika/pika_reader.h"
#include "../src/runtime/types.h"

void verify_val_to_str_behavior() {
    const char* input = "(test \"hello world\")";
    printf("Input: %s\n", input);
    
    Value* result = pika_parse(input);
    
    if (is_error(result)) {
        printf("Parse error: %s\n", result->s);
        return;
    }
    
    // Check structure
    int len = 0;
    Value* curr = result;
    while (!is_nil(curr)) {
        len++;
        curr = cdr(curr);
    }
    
    printf("List length: %d\n", len);
    
    if (len == 2) {
        printf("Correct structure: (test, \"hello world\")\n");
    } else if (len == 3) {
        printf("Buggy structure: (test, hello, world)\n");
    }
    
    // Print using val_to_str
    char* s = val_to_str(result);
    printf("val_to_str output: %s\n", s);
    free(s);
}

int main() {
    verify_val_to_str_behavior();
    return 0;
}