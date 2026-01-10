#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../../third_party/arena/arena.h"

int main(void) {
    printf("Testing RCG Arena integration...\n");

    Arena a = {0};
    
    // Test 1: Basic Allocation
    int* num = (int*)arena_alloc(&a, sizeof(int));
    *num = 42;
    assert(*num == 42);
    printf("Test 1 Passed: Basic Allocation\n");

    // Test 2: Array Allocation
    int count = 100;
    int* arr = (int*)arena_alloc(&a, sizeof(int) * count);
    for (int i = 0; i < count; i++) {
        arr[i] = i;
    }
    assert(arr[0] == 0);
    assert(arr[99] == 99);
    printf("Test 2 Passed: Array Allocation\n");

    // Test 3: String Duplication
    char* str = arena_strdup(&a, "Hello Arena");
    assert(strcmp(str, "Hello Arena") == 0);
    printf("Test 3 Passed: String Duplication\n");

    // Cleanup
    arena_free(&a);
    printf("Cleanup successful.\n");
    
    printf("All RCG Arena tests passed.\n");
    return 0;
}
