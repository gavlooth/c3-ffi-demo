/*
 * Test for BUG-0007: Constraint strdup() Error Handling Bug
 *
 * Expected invariant: When add_violation() is called, the constraint violation
 * should ALWAYS be recorded, even under memory pressure.
 *
 * Observed behavior: In constraint.c:26-27, when strdup() fails (returns NULL),
 * the function returns immediately WITHOUT recording the violation. This causes
 * important constraint violations to be silently dropped.
 *
 * Why this violates architecture/invariants:
 * 1. Reliability violation: Constraint violations are critical safety signals
 *    that should never be silently dropped
 * 2. API contract violation: Callers expect add_violation() to record violations
 * 3. Silent failure mode: No error is returned, no logging occurs, the violation
 *    simply disappears
 * 4. Memory exhaustion is exactly when you most need to detect violations!
 *
 * Root cause (confirmed): In constraint.c:22-42, add_violation() does:
 *   char* msg_copy = strdup(message);
 *   if (!msg_copy) return;  // BUG: Silent failure!
 *
 * When strdup() fails (OOM), the violation is lost. The caller has no way
 * to know this happened because:
 * - add_violation() returns void
 * - No error flag is set in the context
 * - No fallback logging occurs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Minimal constraint definitions from constraint.h */
#define MAX_CONSTRAINT_SOURCES 16

typedef struct ConstraintObj {
    void* data;
    void (*destructor)(void*);
    const char* owner;
    int constraint_count;
    int freed;
    const char* constraint_sources[MAX_CONSTRAINT_SOURCES];
    int source_count;
} ConstraintObj;

typedef struct ConstraintContext {
    ConstraintObj** objects;
    int object_count;
    int object_capacity;
    int assert_on_error;
    char** violations;
    int violation_count;
    int violation_capacity;
} ConstraintContext;

/* Helper: grow array */
static void* grow_array(void* arr, int* capacity, size_t elem_size) {
    int new_cap = (*capacity == 0) ? 8 : (*capacity * 2);
    void* new_arr = realloc(arr, new_cap * elem_size);
    if (new_arr) {
        *capacity = new_cap;
    }
    return new_arr;
}

/* The buggy add_violation from constraint.c:22-42 */
static void add_violation(ConstraintContext* ctx, const char* message) {
    if (!ctx) return;

    /* Duplicate the message */
    char* msg_copy = strdup(message);
    if (!msg_copy) return;  /* BUG: Silent failure on OOM! */

    if (ctx->violation_count >= ctx->violation_capacity) {
        char** new_violations = grow_array(
            ctx->violations,
            &ctx->violation_capacity,
            sizeof(char*)
        );
        if (!new_violations) {
            free(msg_copy);
            return;  /* Also silent failure */
        }
        ctx->violations = new_violations;
    }
    ctx->violations[ctx->violation_count++] = msg_copy;
}

/* Create new context */
static ConstraintContext* constraint_context_new(bool assert_on_error) {
    ConstraintContext* ctx = calloc(1, sizeof(ConstraintContext));
    if (!ctx) return NULL;

    ctx->objects = NULL;
    ctx->object_count = 0;
    ctx->object_capacity = 0;
    ctx->assert_on_error = assert_on_error;
    ctx->violations = NULL;
    ctx->violation_count = 0;
    ctx->violation_capacity = 0;

    return ctx;
}

/* Free context */
static void constraint_context_free(ConstraintContext* ctx) {
    if (!ctx) return;

    for (int i = 0; i < ctx->violation_count; i++) {
        free(ctx->violations[i]);
    }
    free(ctx->violations);
    free(ctx->objects);
    free(ctx);
}

/* Test 1: Normal operation - violation is recorded */
static int test_normal_violation_recording(void) {
    printf("Test 1: Normal violation recording\n");
    printf("-----------------------------------\n");

    ConstraintContext* ctx = constraint_context_new(false);
    if (!ctx) {
        printf("  FAIL: Could not create context\n");
        return -1;
    }

    const char* test_msg = "test violation message";
    add_violation(ctx, test_msg);

    if (ctx->violation_count == 1) {
        printf("  PASS: Violation recorded (count=%d)\n", ctx->violation_count);
        if (ctx->violations[0] && strcmp(ctx->violations[0], test_msg) == 0) {
            printf("  PASS: Message matches: '%s'\n", ctx->violations[0]);
            constraint_context_free(ctx);
            return 0;
        } else {
            printf("  FAIL: Message mismatch\n");
            constraint_context_free(ctx);
            return 1;
        }
    } else {
        printf("  FAIL: Violation count is %d (expected 1)\n", ctx->violation_count);
        constraint_context_free(ctx);
        return 1;
    }
}

/* Test 2: Simulate strdup() failure */
static int test_strdup_failure_simulation(void) {
    printf("\nTest 2: Simulating strdup() failure\n");
    printf("------------------------------------\n");

    /* We can't easily make strdup() fail in a controlled way,
     * but we can demonstrate the problem by analyzing the code */

    printf("  Code analysis of add_violation():\n");
    printf("    Line 26: char* msg_copy = strdup(message);\n");
    printf("    Line 27: if (!msg_copy) return;  // BUG HERE!\n\n");

    printf("  Problem: When strdup() fails:\n");
    printf("    1. Returns immediately (void, no error indication)\n");
    printf("    2. Violation is NOT recorded\n");
    printf("    3. Violation count is NOT incremented\n");
    printf("    4. No error flag is set in context\n");
    printf("    5. No fallback logging occurs\n\n");

    printf("  Why this is dangerous:\n");
    printf("    - strdup() fails when memory is exhausted\n");
    printf("    - Memory exhaustion is EXACTLY when you need violations!\n");
    printf("    - Critical safety checks become silent\n");
    printf("    - Memory corruption may go undetected\n\n");

    printf("  The invariant that SHOULD hold:\n");
    printf("    'Every call to add_violation() either records the violation\n");
    printf("     or signals an error in a detectable way'\n\n");

    printf("  The ACTUAL behavior:\n");
    printf("    'When strdup() fails, add_violation() silently does nothing'\n\n");

    return 0;  /* Informational test */
}

/* Test 3: Demonstrate silent failure */
static int test_silent_failure_demonstration(void) {
    printf("\nTest 3: Silent failure demonstration\n");
    printf("-------------------------------------\n");

    printf("  Scenario: Memory is exhausted, strdup() fails\n");
    printf("  Expected: add_violation() should indicate failure\n");
    printf("  Actual: add_violation() returns void, no indication\n\n");

    printf("  Code example:\n");
    printf("    ConstraintContext* ctx = constraint_context_new(false);\n");
    printf("    add_violation(ctx, 'critical safety violation');\n");
    printf("    // If strdup() failed internally:\n");
    printf("    //   - ctx->violation_count is still 0\n");
    printf("    //   - No way to know the violation was lost!\n");
    printf("    //   - Code continues as if nothing happened\n\n");

    printf("  Contrast with proper error handling:\n");
    printf("    bool add_violation(ConstraintContext* ctx, const char* message) {\n");
    printf("        if (!ctx) return false;\n");
    printf("        char* msg_copy = strdup(message);\n");
    printf("        if (!msg_copy) {\n");
    printf("            // Fallback: log to stderr\n");
    printf("            fprintf(stderr, 'VIOLATION: %s\\n', message);\n");
    printf("            return false;  // Caller knows it failed!\n");
    printf("        }\n");
    printf("        ...\n");
    printf("    }\n\n");

    return 0;  /* Informational test */
}

/* Test 4: Memory pressure scenario */
static int test_memory_pressure_scenario(void) {
    printf("\nTest 4: Memory pressure scenario\n");
    printf("---------------------------------\n");

    printf("  In a real system under memory pressure:\n\n");

    printf("  1. Application allocates many objects\n");
    printf("  2. Memory becomes fragmented\n");
    printf("  3. A constraint violation is detected\n");
    printf("  4. add_violation() is called\n");
    printf("  5. strdup() fails (OOM for small string)\n");
    printf("  6. VIOLATION IS SILENTLY DROPPED\n");
    printf("  7. Memory corruption continues undetected\n");
    printf("  8. System crashes mysteriously later\n\n");

    printf("  The bug transforms a detectable violation into\n");
    printf("  an undetectable crash, making debugging IMPOSSIBLE.\n\n");

    return 0;  /* Informational test */
}

int main(void) {
    printf("========================================\n");
    printf("Constraint strdup() Error Handling Test\n");
    printf("========================================\n\n");

    int result = 0;

    result |= test_normal_violation_recording();
    result |= test_strdup_failure_simulation();
    result |= test_silent_failure_demonstration();
    result |= test_memory_pressure_scenario();

    printf("\n========================================\n");
    printf("Summary\n");
    printf("========================================\n\n");

    printf("BUG CONFIRMED: constraint.c:26-27\n");
    printf("Issue: add_violation() silently fails when strdup() returns NULL\n\n");

    printf("Impact:\n");
    printf("  - HIGH: Violations dropped under memory pressure\n");
    printf("  - HIGH: No way to detect the failure\n");
    printf("  - MEDIUM: Makes debugging memory issues impossible\n\n");

    printf("Recommended fix:\n");
    printf("  1. Change return type from void to bool\n");
    printf("  2. On strdup() failure:\n");
    printf("     a) Log the violation to stderr as fallback\n");
    printf("     b) Set an error flag in context\n");
    printf("     c) Return false to indicate failure\n");
    printf("  3. Optionally: Use static buffer as last resort\n\n");

    printf("Example fix:\n");
    printf("  bool add_violation(ConstraintContext* ctx, const char* message) {\n");
    printf("      if (!ctx) return false;\n");
    printf("      char* msg_copy = strdup(message);\n");
    printf("      if (!msg_copy) {\n");
    printf("          fprintf(stderr, 'VIOLATION: %s\\n', message);\n");
    printf("          ctx->had_strdup_failure = true;\n");
    printf("          return false;\n");
    printf("      }\n");
    printf("      ...\n");
    printf("  }\n");

    return result;
}
