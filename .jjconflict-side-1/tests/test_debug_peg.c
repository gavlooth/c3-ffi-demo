#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mock the regex_to_peg function to see what it generates */

int main() {
    /* Manually construct what the PEG should look like */
    const char* peg = "pattern <- [0-9]+\n";
    
    /* Print with position markers */
    printf("Generated PEG:\n");
    printf("  %s\n", peg);
    printf("  ");
    for (int i = 0; i < 15; i++) {
        printf("%d", i % 10);
    }
    printf("\n");
    
    /* Check position 7-9 */
    printf("\nPosition 7: '%c'\n", peg[7]);
    printf("Position 8: '%c'\n", peg[8]);
    printf("Position 9: '%c'\n", peg[9]);
    
    return 0;
}
