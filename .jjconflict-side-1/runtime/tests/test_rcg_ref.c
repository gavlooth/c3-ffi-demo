#include <stdio.h>
#include <assert.h>
#include "../src/memory/region_core.h"

int main(void) {
    printf("Testing RCG RegionRef...\n");

    // 1. Create Source Region
    Region* r = region_create();
    int* val = region_alloc(r, sizeof(int));
    *val = 42;
    
    // 2. Create Ref
    RegionRef ref = { .ptr = val, .region = r };
    
    // 3. Retain (Simulate passing to another thread/struct)
    region_retain(ref);
    // Now RC should be 1.
    
    // 4. Kill Original Scope
    region_exit(r); 
    // Scope dead, but RC=1. Region must live.
    
    // 5. Verify Access
    int* p = (int*)ref.ptr;
    assert(*p == 42);
    printf("  Access after scope death confirmed.\n");
    
    // 6. Release
    region_release(ref); 
    // RC -> 0. Region dies.
    
    printf("All RCG RegionRef tests passed.\n");
    return 0;
}
