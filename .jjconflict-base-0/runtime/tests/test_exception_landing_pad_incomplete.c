/**
 * Test: Exception Landing Pad Incomplete Cleanup
 *
 * Expected invariant:
 *   When create_landing_pad() is called, it should create cleanup actions
 *   for ALL live allocations in the context, or return an error if it cannot.
 *
 * Observed behavior:
 *   In create_landing_pad() at exception.c:55-88, the function iterates
 *   through all live allocations and creates CleanupAction structures for each.
 *   If malloc() fails for a CleanupAction, the loop breaks early:
 *
 *     while (alloc) {
 *         CleanupAction* action = malloc(sizeof(CleanupAction));
 *         if (!action) break;  // BUG: Silent failure, incomplete cleanup list
 *         ...
 *     }
 *
 *   This causes:
 *   1. Some allocations after the failure point won't have cleanup actions
 *   2. If an exception occurs, those allocations won't be cleaned up
 *   3. No error indication is returned to the caller
 *   4. The landing pad appears valid but is incomplete
 *
 * Why this violates architecture/invariants:
 *   1. Exception safety invariant violated: All live allocations must be
 *      cleaned up during exception unwinding.
 *   2. API contract broken: create_landing_pad() provides no way to signal
 *      partial failure (returns LandingPad* without error indicator).
 *   3. Memory leak: Silent failures cause leaks during exception handling.
 *   4. Determinism: Can't predict which allocations will be cleaned up.
 *
 * Root cause:
 *   The code at exception.c:69-84 uses malloc without error propagation:
 *     - When malloc fails, it breaks the loop
 *     - The partial landing pad is still returned
 *     - No cleanup of previously allocated actions on failure
 *     - No way for caller to detect the incompleteness
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Minimal structures matching exception.h */
typedef struct LiveAlloc {
    char* var_name;
    char* type_name;
    int program_point;
    struct LiveAlloc* next;
} LiveAlloc;

typedef struct CleanupAction {
    char* var_name;
    char* cleanup_fn;
    struct CleanupAction* next;
} CleanupAction;

typedef struct LandingPad {
    int id;
    int try_start;
    int try_end;
    CleanupAction* cleanups;
    struct LandingPad* next;
} LandingPad;

typedef struct ExceptionContext {
    int depth;
    LandingPad* current_pad;
    LiveAlloc* live_allocs;
    struct ExceptionContext* parent;
} ExceptionContext;

/* Global counter for pad IDs */
static int next_pad_id = 0;

/* Custom malloc wrapper for testing */
/* fail_counter: 0 = never fail, N = fail after N successful allocations */
static void* test_malloc(size_t size, int* fail_counter) {
    if (fail_counter && *fail_counter > 0) {
        (*fail_counter)--;
        return NULL;  /* Simulate malloc failure */
    }
    return malloc(size);
}

/* Buggy version from exception.c:55-88 */
/* malloc_fail_after: 0 = never fail, N = fail after N allocations */
LandingPad* create_landing_pad_buggy(ExceptionContext* ctx, int malloc_fail_after) {
    if (!ctx) return NULL;

    /* LandingPad malloc never fails in our test scenarios */
    LandingPad* pad = malloc(sizeof(LandingPad));
    if (!pad) return NULL;
    
    pad->id = next_pad_id++;
    pad->try_start = 0;
    pad->try_end = 0;
    pad->cleanups = NULL;
    pad->next = NULL;

    /* Create cleanup actions for all live allocations */
    LiveAlloc* alloc = ctx->live_allocs;
    int action_count = 0;
    while (alloc) {
        /* Use test_malloc for CleanupAction allocations */
        int fail_this = (malloc_fail_after == action_count) ? 1 : 0;
        CleanupAction* action = test_malloc(sizeof(CleanupAction), &fail_this);
        if (!action) break;  /* BUG: Stop on allocation failure */
        
        action->var_name = strdup(alloc->var_name);
        action->cleanup_fn = strdup("dec_ref");
        if (!action->var_name || !action->cleanup_fn) {
            free(action->var_name);
            free(action->cleanup_fn);
            free(action);
            break;  /* Also problematic */
        }

        action->next = pad->cleanups;
        pad->cleanups = action;
        action_count++;

        alloc = alloc->next;
    }

    ctx->current_pad = pad;
    return pad;  /* Returns incomplete pad! */
}

/* Count cleanup actions in a landing pad */
int count_cleanup_actions(LandingPad* pad) {
    int count = 0;
    CleanupAction* action = pad->cleanups;
    while (action) {
        count++;
        action = action->next;
    }
    return count;
}

/* Free landing pad and its cleanup actions */
void free_landing_pad(LandingPad* pad) {
    if (!pad) return;
    
    CleanupAction* action = pad->cleanups;
    while (action) {
        CleanupAction* next = action->next;
        free(action->var_name);
        free(action->cleanup_fn);
        free(action);
        action = next;
    }
    free(pad);
}

/* Test helper to create a live allocation */
LiveAlloc* create_live_alloc(const char* var, const char* type) {
    LiveAlloc* alloc = malloc(sizeof(LiveAlloc));
    if (!alloc) return NULL;
    alloc->var_name = strdup(var);
    alloc->type_name = strdup(type);
    alloc->program_point = 0;
    alloc->next = NULL;
    if (!alloc->var_name || !alloc->type_name) {
        free(alloc->var_name);
        free(alloc->type_name);
        free(alloc);
        return NULL;
    }
    return alloc;
}

int main(void) {
    printf("=== Exception Landing Pad Incomplete Cleanup Test ===\n\n");

    int tests_failed = 0;

    /* Test 1: Normal case (no malloc failures) */
    printf("Test 1: Normal case - all allocations get cleanup actions\n");
    printf("------------------------------------------------------------\n");

    {
        ExceptionContext ctx = {0, NULL, NULL, NULL};
        
        /* Create 5 live allocations */
        for (int i = 0; i < 5; i++) {
            char var[32], type[32];
            snprintf(var, sizeof(var), "obj%d", i);
            snprintf(type, sizeof(type), "Obj*");
            
            LiveAlloc* alloc = create_live_alloc(var, type);
            if (!alloc) {
                printf("  FAIL: Could not create live allocation\n");
                tests_failed++;
                goto cleanup1;
            }
            
            alloc->next = ctx.live_allocs;
            ctx.live_allocs = alloc;
        }

        int no_fail = -1;  /* Don't fail any mallocs */
        LandingPad* pad = create_landing_pad_buggy(&ctx, no_fail);
        
        int action_count = count_cleanup_actions(pad);
        printf("  Live allocations: 5\n");
        printf("  Cleanup actions created: %d\n", action_count);
        
        if (action_count == 5) {
            printf("  PASS: All allocations have cleanup actions\n");
        } else {
            printf("  FAIL: Expected 5 cleanup actions, got %d\n", action_count);
            tests_failed++;
        }
        
        free_landing_pad(pad);

cleanup1:
        /* Free live allocations */
        LiveAlloc* alloc = ctx.live_allocs;
        while (alloc) {
            LiveAlloc* next = alloc->next;
            free(alloc->var_name);
            free(alloc->type_name);
            free(alloc);
            alloc = next;
        }
    }

    printf("\n");

    /* Test 2: Malloc failure causes incomplete cleanup list */
    printf("Test 2: Malloc failure causes incomplete cleanup list\n");
    printf("-------------------------------------------------------\n");

    {
        ExceptionContext ctx = {0, NULL, NULL, NULL};
        
        /* Create 10 live allocations */
        for (int i = 0; i < 10; i++) {
            char var[32], type[32];
            snprintf(var, sizeof(var), "obj%d", i);
            snprintf(type, sizeof(type), "Obj*");
            
            LiveAlloc* alloc = create_live_alloc(var, type);
            if (!alloc) {
                printf("  FAIL: Could not create live allocation\n");
                tests_failed++;
                goto cleanup2;
            }
            
            alloc->next = ctx.live_allocs;
            ctx.live_allocs = alloc;
        }

        /* Fail malloc after 3 allocations */
        LandingPad* pad = create_landing_pad_buggy(&ctx, 3);

        if (!pad) {
            printf("  FAIL: LandingPad malloc failed (not the bug we're testing)\n");
            tests_failed++;
            goto cleanup2;
        }

        int action_count = count_cleanup_actions(pad);
        printf("  Live allocations: 10\n");
        printf("  Malloc configured to fail after 3 allocations\n");
        printf("  Cleanup actions created: %d\n", action_count);

        if (action_count < 10) {
            printf("\n*** BUG CONFIRMED ***\n");
            printf("Landing pad is INCOMPLETE!\n");
            printf("If an exception occurs now:\n");
            printf("  - Only %d allocations would be cleaned up\n", action_count);
            printf("  - %d allocations would leak (no cleanup action)\n", 10 - action_count);
            printf("  - Caller has no way to detect this failure\n");
            tests_failed = 1;  /* Bug exists */
        } else {
            printf("  UNEXPECTED: All cleanup actions created despite malloc failure\n");
        }
        
        free_landing_pad(pad);

cleanup2:
        /* Free live allocations */
        LiveAlloc* alloc = ctx.live_allocs;
        while (alloc) {
            LiveAlloc* next = alloc->next;
            free(alloc->var_name);
            free(alloc->type_name);
            free(alloc);
            alloc = next;
        }
    }

    printf("\n");

    /* Test 3: First allocation failure */
    printf("Test 3: First CleanupAction allocation fails\n");
    printf("----------------------------------------------\n");

    {
        ExceptionContext ctx = {0, NULL, NULL, NULL};
        
        /* Create 5 live allocations */
        for (int i = 0; i < 5; i++) {
            char var[32], type[32];
            snprintf(var, sizeof(var), "obj%d", i);
            snprintf(type, sizeof(type), "Obj*");
            
            LiveAlloc* alloc = create_live_alloc(var, type);
            if (!alloc) {
                printf("  FAIL: Could not create live allocation\n");
                tests_failed++;
                goto cleanup3;
            }
            
            alloc->next = ctx.live_allocs;
            ctx.live_allocs = alloc;
        }

        /* Fail first CleanupAction malloc */
        LandingPad* pad = create_landing_pad_buggy(&ctx, 0);

        if (!pad) {
            printf("  FAIL: LandingPad malloc failed (not the bug we're testing)\n");
            tests_failed++;
            goto cleanup3;
        }

        int action_count = count_cleanup_actions(pad);
        printf("  Live allocations: 5\n");
        printf("  First CleanupAction malloc fails\n");
        printf("  Cleanup actions created: %d\n", action_count);

        if (action_count == 0) {
            printf("\n*** BUG CONFIRMED ***\n");
            printf("Landing pad has NO cleanup actions!\n");
            printf("All 5 live allocations would leak on exception.\n");
            tests_failed = 1;
        } else {
            printf("  INFO: Some cleanup actions created (%d)\n", action_count);
        }
        
        free_landing_pad(pad);

cleanup3:
        /* Free live allocations */
        LiveAlloc* alloc = ctx.live_allocs;
        while (alloc) {
            LiveAlloc* next = alloc->next;
            free(alloc->var_name);
            free(alloc->type_name);
            free(alloc);
            alloc = next;
        }
    }

    printf("\n=== Summary ===\n");
    if (tests_failed) {
        printf("FAILED: Incomplete landing pad bug confirmed\n");
        printf("Location: exception.c:69-84 (create_landing_pad)\n");
        printf("Issue: malloc failure causes silent incomplete cleanup list\n");
        printf("\nFix options:\n");
        printf("  1. Pre-allocate all cleanup actions before linking\n");
        printf("  2. Return error code (change API to return LandingPad* with error flag)\n");
        printf("  3. Use arena allocator for cleanup actions (cannot fail)\n");
        printf("  4. Add cleanup_count field to LandingPad for validation\n");
        return 1;
    } else {
        printf("PASSED: Landing pad cleanup complete (bug may be fixed)\n");
        return 0;
    }
}
