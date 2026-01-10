#define _POSIX_C_SOURCE 200809L
#include <stdio.h>

int main() {
    /* Try parsing a very simple PEG */
    const char* simple_peg = "pattern <- a\n";
    printf("Testing PEG: '%s'\n", simple_peg);
    
    /* Count positions */
    printf("Position 0: '%c'\n", simple_peg[0]);
    printf("Position 7: '%c'\n", simple_peg[7]);
    printf("Position 8: '%c'\n", simple_peg[8]);
    printf("Position 9: '%c'\n", simple_peg[9]);
    
    return 0;
}
