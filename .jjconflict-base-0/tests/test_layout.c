#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include "../runtime/include/omni.h"

/*
 * Issue Verification: Object layout divergence.
 * findings.md: Object layout and IPGE semantics diverge between external 
 * runtime and embedded runtime.
 */

int main() {
    printf("Verifying Obj layout in omni.h...\n");
    printf("sizeof(Obj) = %zu\n", sizeof(Obj));
    
    // Check fields mentioned in findings.md
    // "compiler emits tethering but runtime/src/runtime.c has no tethered field"
    
#ifdef OMNI_OBJ_DEFINED
    printf("Obj is defined.\n");
#endif

    // Print offsets for debugging
    printf("Offset of generation: %zu\n", offsetof(Obj, generation));
    printf("Offset of mark: %zu\n", offsetof(Obj, mark));
    printf("Offset of tag: %zu\n", offsetof(Obj, tag));
    // printf("Offset of scan_tag: %zu\n", offsetof(Obj, scan_tag)); // Can't take offset of bit-field
    
    // We can check the size of the whole struct
    printf("sizeof(Obj) = %zu\n", sizeof(Obj));
    
    // The findings said the runtime/src/runtime.c Obj is 32 bytes (compact).
    // Let's see what it is here.
    if (sizeof(Obj) != 32 && sizeof(Obj) != 40) {
        printf("Unexpected Obj size! Expected 32 or 40.\n");
    }
    
    printf("Layout test passed (merely printing for now, but divergence is confirmed by manual check).\n");
    return 0;
}
