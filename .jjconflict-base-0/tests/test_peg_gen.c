#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Minimal test to see what PEG is generated */

static char* regex_to_peg(const char* pattern);

int main() {
    const char* pattern = "^foo";
    char* peg = regex_to_peg(pattern);
    if (peg) {
        printf("Pattern '%s' -> PEG: '%s'\n", pattern, peg);
        free(peg);
    } else {
        printf("Pattern '%s' -> ERROR\n", pattern);
    }
    return 0;
}
