#include <stdio.h>
#include <assert.h>
#include "../src/memory/region_core.h"
#include "../../src/runtime/types.h"

int main(void) {
    printf("Testing RCG Region Logic...\n");

    // 1. Basic Lifecycle
    printf("  [1] Basic Lifecycle...\n");
    Region* r1 = region_create();
    Value* p1 = region_alloc(r1, sizeof(Value));
    p1->tag = T_INT;
    p1->i = 100;
    assert(r1->scope_alive == true);
    region_exit(r1); 
    printf("      Passed.\n");

    // 2. RC Retention
    printf("  [2] RC Retention...\n");
    Region* r2 = region_create();
    region_retain_internal(r2); // RC = 1
    region_exit(r2);            // Scope dies, but Region lives (RC=1)
    
    Value* p2 = region_alloc(r2, sizeof(Value));
    p2->tag = T_INT;
    p2->i = 200;
    
    region_release_internal(r2); // RC -> 0. Should free.
    printf("      Passed.\n");

    // 3. Tethering
    printf("  [3] Tethering...\n");
    Region* r3 = region_create();
    region_tether_start(r3); // TC = 1
    region_exit(r3);         // Scope dies, Region lives (TC=1)
    
    region_tether_end(r3);   // TC -> 0. Should free.
    printf("      Passed.\n");

    // 4. Mixed
    printf("  [4] Mixed RC + Tether...\n");
    Region* r4 = region_create();
    region_retain_internal(r4); // RC=1
    region_tether_start(r4);    // TC=1
    
    region_exit(r4);            // Scope dies
    
    region_release_internal(r4); // RC=0, TC=1. Lives.
    region_tether_end(r4);       // TC=0. Dies.
    printf("      Passed.\n");

    printf("All RCG Region tests passed.\n");
    return 0;
}