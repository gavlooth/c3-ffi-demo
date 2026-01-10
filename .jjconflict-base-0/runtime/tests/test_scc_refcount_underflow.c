/*
 * Test for BUG-0004: SCC Refcount Underflow
 *
 * Expected invariant: scc->ref_count should always be >= 0
 * Observed behavior: Calling release_scc() when ref_count == 0 causes
 *                   underflow to negative values, preventing proper cleanup
 *
 * Why it violates architecture: The refcount invariant is broken when
 * release_scc() is called more times than inc_scc_ref(). This can happen
 * due to:
 * 1. Programmer error (unbalanced inc/dec)
 * 2. Exception handling paths that call release multiple times
 * 3. Double-free scenarios in complex object graphs
 *
 * The fix should either:
 * a) Assert ref_count > 0 before decrement
 * b) Use unsigned refcount with saturating decrement
 * c) Return error/status instead of silently corrupting state
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* Minimal SCC structure for testing */
typedef struct SCC {
    int id;
    void** members;
    int member_count;
    int capacity;
    int ref_count;
    struct SCC* next;
    struct SCC* result_next;
} SCC;

/* Minimal test objects */
typedef struct Obj {
    int scc_id;
    int is_pair;
    struct Obj* a;
    struct Obj* b;
    int mark;
    int freed;
} Obj;

/* Test functions copied from scc.c */
void inc_scc_ref(SCC* scc) {
    if (scc) scc->ref_count++;
}

void release_scc(SCC* scc) {
    if (!scc) return;
    scc->ref_count--;  /* BUG: No underflow check! */

    if (scc->ref_count == 0) {
        /* Free all members */
        for (int i = 0; i < scc->member_count; i++) {
            free(scc->members[i]);
        }
        free(scc->members);
        scc->members = NULL;
        scc->member_count = 0;
    }
}

/* Test helper to create an SCC */
SCC* create_test_scc(int initial_refcount) {
    SCC* scc = malloc(sizeof(SCC));
    if (!scc) return NULL;

    scc->id = 1;
    scc->members = NULL;
    scc->member_count = 0;
    scc->capacity = 16;
    scc->ref_count = initial_refcount;
    scc->next = NULL;
    scc->result_next = NULL;

    return scc;
}

int test_scc_refcount_underflow(void) {
    printf("Testing SCC refcount underflow...\n");

    /* Test 1: Single decrement from ref_count=0 */
    printf("\n=== Test 1: Decrement from ref_count=0 ===\n");
    SCC* scc1 = create_test_scc(0);
    if (!scc1) {
        printf("FAIL: Could not allocate SCC\n");
        return -1;
    }

    printf("Initial ref_count: %d\n", scc1->ref_count);

    /* This is the bug: calling release when ref_count is already 0 */
    release_scc(scc1);

    printf("After release_scc() from 0: ref_count = %d\n", scc1->ref_count);

    if (scc1->ref_count < 0) {
        printf("DETECTED BUG: ref_count is negative! (%d)\n", scc1->ref_count);
        printf("This violates the refcount invariant (should always be >= 0)\n");
        free(scc1);
        return 0;  /* Test PASSED - bug reproduced */
    } else {
        printf("FAIL: ref_count did not go negative\n");
        free(scc1);
        return -1;
    }
}

int test_scc_refcount_double_release(void) {
    printf("\n=== Test 2: Double release from ref_count=1 ===\n");

    SCC* scc2 = create_test_scc(1);
    if (!scc2) {
        printf("FAIL: Could not allocate SCC\n");
        return -1;
    }

    printf("Initial ref_count: %d\n", scc2->ref_count);

    /* First release - should work correctly */
    release_scc(scc2);
    printf("After first release: ref_count = %d\n", scc2->ref_count);

    /* Second release - BUG: this causes underflow! */
    release_scc(scc2);
    printf("After second release: ref_count = %d\n", scc2->ref_count);

    if (scc2->ref_count < 0) {
        printf("DETECTED BUG: ref_count is negative after double release! (%d)\n", scc2->ref_count);
        printf("The second release should have been caught or prevented\n");
        free(scc2);
        return 0;  /* Test PASSED - bug reproduced */
    } else {
        printf("UNEXPECTED: ref_count did not go negative\n");
        free(scc2);
        return -1;
    }
}

int test_scc_refcount_never_frees(void) {
    printf("\n=== Test 3: Memory never freed after underflow ===\n");

    SCC* scc3 = create_test_scc(0);
    if (!scc3) {
        printf("FAIL: Could not allocate SCC\n");
        return -1;
    }

    /* Allocate some member data to track */
    scc3->members = malloc(sizeof(void*) * 2);
    scc3->member_count = 0;
    scc3->capacity = 2;

    printf("Initial ref_count: %d, members allocated: %p\n", scc3->ref_count, scc3->members);

    /* Release from 0 - causes underflow */
    release_scc(scc3);
    printf("After release from 0: ref_count = %d\n", scc3->ref_count);
    printf("Members still allocated: %p\n", scc3->members);

    if (scc3->ref_count < 0 && scc3->members != NULL) {
        printf("DETECTED BUG: Memory leaked!\n");
        printf("- ref_count is negative (%d), so cleanup won't run until it wraps to 0\n", scc3->ref_count);
        printf("- This requires %d more release calls - effectively never\n", -scc3->ref_count);
        printf("- Members pointer is still %p (should have been freed)\n", scc3->members);

        free(scc3->members);  /* Cleanup for test */
        free(scc3);
        return 0;  /* Test PASSED - bug reproduced */
    } else {
        free(scc3->members);
        free(scc3);
        printf("UNEXPECTED: Members were freed or ref_count not negative\n");
        return -1;
    }
}

int main(void) {
    int failures = 0;

    printf("========================================\n");
    printf("SCC Refcount Underflow Test\n");
    printf("Testing for BUG-0004\n");
    printf("========================================\n");

    if (test_scc_refcount_underflow() != 0) {
        printf("\nTest 1 FAILED\n");
        failures++;
    }

    if (test_scc_refcount_double_release() != 0) {
        printf("\nTest 2 FAILED\n");
        failures++;
    }

    if (test_scc_refcount_never_frees() != 0) {
        printf("\nTest 3 FAILED\n");
        failures++;
    }

    printf("\n========================================\n");
    if (failures == 0) {
        printf("All tests PASSED - bug successfully reproduced!\n");
        printf("The refcount underflow vulnerability is confirmed.\n");
        return 0;
    } else {
        printf("Some tests FAILED - bug may not be reproducible in this configuration\n");
        return 1;
    }
}
