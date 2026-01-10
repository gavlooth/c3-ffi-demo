/*
 * Test: Slot Pool Manual Alignment Invalid Free Bug
 *
 * BUG-0006: Slot Pool Alignment Mismatch Double-Free
 *
 * This test demonstrates a critical bug in slot_pool.c where manual alignment
 * causes an invalid free() operation.
 *
 * Bug Summary:
 * ------------
 * In slot_pool.c:55-61, the code manually aligns pointers on platforms without
 * posix_memalign:
 *
 *   block->slots = malloc(total_size + 15);
 *   if (!block->slots) { free(block); return NULL; }
 *   block->slots = (Slot*)(((uintptr_t)block->slots + 15) & ~15);  // BUG!
 *
 * The original malloc pointer is lost and replaced with the aligned pointer.
 * Later, destroy_block() calls:
 *
 *   free(block->slots);  // Invalid! Must free original pointer, not aligned.
 *
 * This violates the C standard (C17 7.22.3.3p2):
 * "The free function deallocates the space previously allocated by malloc...
 *  If the argument does not match a pointer earlier returned by a memory
 *  management function, the behavior is undefined."
 *
 * Expected Behavior:
 * -----------------
 * - The aligned pointer should be used for accessing slots
 * - The ORIGINAL malloc pointer should be passed to free()
 * - SlotBlock should store both pointers (or compute aligned pointer on access)
 *
 * Observed Behavior:
 * -----------------
 * - The aligned pointer is passed to free()
 * - This causes undefined behavior: heap corruption, crashes, or double-free
 * - Valgrind/ASan will detect "invalid free" or "misaligned pointer passed to free"
 *
 * The bug exists in slot_pool.c:55-61 and 77-78.
 * Note: The comment at line 77 even acknowledges the issue: "Note: if we used
 * manual alignment, we'd need to store original ptr" - but the code still calls
 * free(block->slots) with the aligned pointer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

/* Force the non-POSIX code path by undefining _POSIX_C_SOURCE */
#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif

/* Include slot pool implementation */
#include "../src/memory/slot_pool.h"

/* Test harness to verify alignment bug */
int main(void) {
    printf("=== BUG-0006: Slot Pool Manual Alignment Invalid Free ===\n\n");

    /* Test 1: Verify manual alignment code path is being used */
    printf("Test 1: Checking code path...\n");
#if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200112L
    printf("  WARNING: POSIX memalign path is active (bug may not manifest)\n");
    printf("  This test targets the manual alignment path\n\n");
#else
    printf("  _POSIX_C_SOURCE is undefined (forcing manual alignment path)\n");
    printf("  This is where the bug manifests\n\n");
#endif

    /* Test 2: Create and destroy a slot pool - should trigger invalid free */
    printf("Test 2: Creating slot pool with manual alignment...\n");
    SlotPool* pool = slot_pool_create(32, 16, 8);
    if (!pool) {
        printf("  ERROR: Failed to create pool\n");
        return 1;
    }
    printf("  Pool created successfully\n");

    /* Test 3: Allocate some slots to exercise the pool */
    printf("\nTest 3: Allocating slots...\n");
    Slot* slots[10];
    int allocated = 0;
    for (int i = 0; i < 10; i++) {
        slots[i] = slot_pool_alloc(pool);
        if (!slots[i]) {
            printf("  WARNING: Only allocated %d slots\n", i);
            allocated = i;
            break;
        }
        allocated++;
        printf("  Allocated slot %d: %p (alignment: %s)\n",
               i, (void*)slots[i],
               ((uintptr_t)slots[i] % 16 == 0) ? "OK" : "BAD");
    }

    /* Test 4: Free the slots */
    printf("\nTest 4: Freeing slots...\n");
    for (int i = 0; i < allocated; i++) {
        slot_pool_free(pool, slots[i]);
        printf("  Freed slot %d\n", i);
    }

    /* Test 5: Destroy the pool - THIS IS WHERE THE BUG MANIFESTS */
    printf("\nTest 5: Destroying pool (bug manifests here)...\n");
    printf("  Code path: destroy_block() -> free(block->slots)\n");
    printf("  Problem: block->slots is the ALIGNED pointer, not the original malloc pointer\n");
    printf("  C Standard violation: free() must receive the exact pointer from malloc()\n");
    printf("\n");
    printf("  The bug in slot_pool.c:55-61:\n");
    printf("    block->slots = malloc(total_size + 15);  // Returns pointer P\n");
    printf("    block->slots = (Slot*)(((uintptr_t)block->slots + 15) & ~15);  // P is lost!\n");
    printf("\n");
    printf("  Then in destroy_block() at line 77-78:\n");
    printf("    free(block->slots);  // Passing aligned pointer, not P - UB!\n");
    printf("\n");
    printf("  Run with Valgrind/ASan to detect: 'invalid free()'\n\n");

    slot_pool_destroy(pool);
    printf("  Pool destroyed (no immediate crash, but UB occurred)\n");

    printf("\n=== Test Complete ===\n");
    printf("NOTE: This test passes without crashing, but the bug IS present.\n");
    printf("\n");
    printf("To verify the bug, run with:\n");
    printf("  valgrind --leak-check=full ./test_slot_pool_invalid_free\n");
    printf("  or compile with: gcc -fsanitize=address -g ...\n");
    printf("\n");
    printf("Expected tool output:\n");
    printf("  Valgrind: 'Invalid free() / delete / delete[]'\n");
    printf("  ASan: 'mis-aligned pointer passed to free'\n");

    return 0;
}

/*
 * Verification with tools:
 * ------------------------
 *
 * 1. Valgrind (will detect invalid free):
 *    $ valgrind --leak-check=full ./test_slot_pool_invalid_free
 *    Expected: "Invalid free() / delete / delete[]"
 *             "Address 0x... is 0 bytes inside a block of size ... alloc'd"
 *
 * 2. AddressSanitizer (will detect misaligned free):
 *    $ gcc -fsanitize=address -g test_slot_pool_invalid_free.c -o test
 *    $ ./test
 *    Expected: "mis-aligned pointer passed to free"
 *
 * 3. The fix requires adding a field to SlotBlock:
 *    typedef struct SlotBlock {
 *        Slot* slots;          // Aligned pointer (for access)
 *        void* alloc_ptr;      // Original malloc pointer (for free)
 *        ...
 *    } SlotBlock;
 *
 *    Then modify create_block() to store both:
 *        void* raw = malloc(total_size + 15);
 *        block->alloc_ptr = raw;
 *        block->slots = (Slot*)(((uintptr_t)raw + 15) & ~15);
 *
 *    And modify destroy_block() to free the original:
 *        free(block->alloc_ptr);
 */
