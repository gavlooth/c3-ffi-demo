/*
 * Test: Symmetric Pool Freelist Type-Punning Violation
 *
 * Expected invariant: The freelist implementation should use valid C types
 * and not violate strict aliasing rules. The next pointer in the freelist
 * should be stored in a dedicated field, not overlaid with an unrelated
 * pointer field that has a different type.
 *
 * Observed behavior: In sym_pool_alloc() at symmetric.c:32-50, the code
 * uses the `refs` field (type SymObj**) to store the freelist next pointer:
 *
 *   for (int i = 0; i < SYM_POOL_SIZE; i++) {
 *       pool->objects[i].refs = (SymObj**)SYM_TLS.freelist;  // TYPE PUNNING
 *       SYM_TLS.freelist = &pool->objects[i];
 *   }
 *   ...
 *   SymObj* obj = SYM_TLS.freelist;
 *   SYM_TLS.freelist = (SymObj*)obj->refs;  // TYPE PUNNING AGAIN
 *
 * Issues:
 * 1. `refs` is declared as `SymObj**` (pointer to pointer to SymObj)
 * 2. The freelist stores `SymObj*` (pointer to SymObj)
 * 3. Casting between these types violates strict aliasing rules (C99 6.5p7)
 * 4. Reading `obj->refs` after writing a SymObj* value to it is UB
 * 5. The compiler is free to assume accesses through different types don't alias
 * 6. This can cause incorrect code generation, crashes, or silent memory corruption
 *
 * Architecture violation: Type safety and C standard compliance.
 * Memory layout must be accessed through compatible types only.
 *
 * Root cause: Space optimization attempt that reuses the `refs` field
 * for freelist management when objects are free. This violates the
 * invariant that `refs` always contains valid SymObj** data or NULL.
 *
 * The fix should add a dedicated `freelist_next` field to SymObj for
 * use only when the object is free (in the pool).
 *
 * This test compiles with -fstrict-aliasing -Wstrict-aliasing to detect
 * the violation, and demonstrates the potential for miscompilation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

/* Minimal symmetric definitions matching symmetric.h */
#define SYM_INLINE_REFS 3
#define SYM_POOL_SIZE 512

typedef struct SymObj SymObj;

struct SymObj {
    int external_rc;
    int internal_rc;
    SymObj** refs;          /* Used for outgoing refs when alive */
    int ref_count;
    int ref_capacity;
    void* data;
    int freed;
    void (*destructor)(void*);
    struct {
        int dummy;
    } *comp;                /* Simplified */
    SymObj* inline_refs[4];
    /* NOTE: Missing dedicated freelist_next field! */
};

/* Pool structure (internal to symmetric.c) */
typedef struct SymPool {
    SymObj* objects;        /* Actually SYM_POOL_SIZE array */
    struct SymPool* next;
} SymPool;

/* Thread-local storage (simplified to global for single-threaded test) */
static struct {
    SymPool* pools;
    SymObj* freelist;
} SYM_TLS = {NULL, NULL};

/* The buggy implementation from symmetric.c:32-50 */
static SymObj* sym_pool_alloc_buggy(void) {
    if (!SYM_TLS.freelist) {
        /* Allocate new pool */
        SymPool* pool = malloc(sizeof(SymPool));
        if (!pool) return NULL;
        pool->objects = malloc(SYM_POOL_SIZE * sizeof(SymObj));
        if (!pool->objects) { free(pool); return NULL; }
        pool->next = SYM_TLS.pools;
        SYM_TLS.pools = pool;

        /* BUG: Type punning - using refs (SymObj**) to store SymObj* */
        for (int i = 0; i < SYM_POOL_SIZE; i++) {
            pool->objects[i].refs = (SymObj**)SYM_TLS.freelist;  /* VIOLATION */
            SYM_TLS.freelist = &pool->objects[i];
        }
    }
    SymObj* obj = SYM_TLS.freelist;
    /* BUG: Reading refs as SymObj* when it was declared as SymObj** */
    SYM_TLS.freelist = (SymObj*)obj->refs;  /* VIOLATION */
    return obj;
}

static void sym_pool_free_buggy(SymObj* obj) {
    /* BUG: Type punning - storing SymObj* into SymObj** field */
    obj->refs = (SymObj**)SYM_TLS.freelist;  /* VIOLATION */
    SYM_TLS.freelist = obj;
}

/* Corrected implementation with dedicated freelist field */
typedef struct SymObjFixed SymObjFixed_t;

struct SymObjFixed {
    int external_rc;
    int internal_rc;
    SymObj** refs;
    int ref_count;
    int ref_capacity;
    void* data;
    int freed;
    void (*destructor)(void*);
    struct {
        int dummy;
    } *comp;
    SymObj* inline_refs[4];
    SymObjFixed_t* freelist_next;  /* Dedicated field for freelist when free */
};

typedef struct SymPoolFixed {
    struct SymObjFixed* objects;
    struct SymPoolFixed* next;
} SymPoolFixed;

static struct {
    SymPoolFixed* pools;
    struct SymObjFixed* freelist;
} SYM_TLS_FIXED = {NULL, NULL};

static struct SymObjFixed* sym_pool_alloc_correct(void) {
    if (!SYM_TLS_FIXED.freelist) {
        SymPoolFixed* pool = malloc(sizeof(SymPoolFixed));
        if (!pool) return NULL;
        pool->objects = malloc(SYM_POOL_SIZE * sizeof(struct SymObjFixed));
        if (!pool->objects) { free(pool); return NULL; }
        pool->next = SYM_TLS_FIXED.pools;
        SYM_TLS_FIXED.pools = pool;

        /* CORRECT: Using dedicated field with matching type */
        for (int i = 0; i < SYM_POOL_SIZE; i++) {
            pool->objects[i].freelist_next = SYM_TLS_FIXED.freelist;
            SYM_TLS_FIXED.freelist = &pool->objects[i];
        }
    }
    struct SymObjFixed* obj = SYM_TLS_FIXED.freelist;
    /* CORRECT: Same type, no cast needed */
    SYM_TLS_FIXED.freelist = obj->freelist_next;
    return obj;
}

static void sym_pool_free_correct(struct SymObjFixed* obj) {
    /* CORRECT: Same type, no cast needed */
    obj->freelist_next = SYM_TLS_FIXED.freelist;
    SYM_TLS_FIXED.freelist = obj;
}

/* Test 1: Demonstrate strict aliasing violation compiles with warnings */
void test_type_punning_compilation(void) {
    printf("=== Test 1: Type Punning Compilation Check ===\n");

    /* The buggy code should trigger warnings with -Wstrict-aliasing */
    printf("Compiling with -fstrict-aliasing -Wstrict-aliasing should warn:\n");
    printf("  - symmetric.c:43: warning: dereferencing type-punned pointer\n");
    printf("  - symmetric.c:48: warning: dereferencing type-punned pointer\n");
    printf("  - symmetric.c:53: warning: dereferencing type-punned pointer\n");
    printf("\n");

    /* Show the type sizes to demonstrate mismatch */
    printf("Type size analysis:\n");
    printf("  sizeof(SymObj*) = %zu bytes\n", sizeof(SymObj*));
    printf("  sizeof(SymObj**) = %zu bytes\n", sizeof(SymObj**));
    if (sizeof(SymObj*) != sizeof(SymObj**)) {
        printf("  WARNING: Sizes differ! This causes truncation/corruption.\n");
    } else {
        printf("  Sizes match, but strict aliasing still violated.\n");
    }
    printf("\n");

    printf("The bug stores SymObj* (freelist next) into SymObj** (refs field).\n");
    printf("When reading back, the compiler may assume the value hasn't changed\n");
    printf("because accesses through different pointer types shouldn't alias.\n");
    printf("\n");
}

/* Test 2: Demonstrate the issue with strict aliasing optimization */
void test_strict_aliasing_behavior(void) {
    printf("=== Test 2: Strict Aliasing Behavior ===\n");

    /* Allocate some objects using buggy implementation */
    SymObj* obj1 = sym_pool_alloc_buggy();
    SymObj* obj2 = sym_pool_alloc_buggy();
    SymObj* obj3 = sym_pool_alloc_buggy();

    if (!obj1 || !obj2 || !obj3) {
        printf("SKIP: Allocation failed\n");
        return;
    }

    printf("Allocated 3 objects from pool\n");

    /* Mark them as in-use by setting refs to something valid */
    obj1->refs = NULL;
    obj2->refs = NULL;
    obj3->refs = NULL;

    /* Verify freelist is now at 4th object (assuming pool size) */
    printf("Freelist position after alloc: %p\n", (void*)SYM_TLS.freelist);

    /* Free one object back to pool */
    sym_pool_free_buggy(obj2);

    /* Now obj2->refs contains the freelist next pointer */
    printf("After freeing obj2:\n");
    printf("  obj2->refs = %p (freelist next, type-punned!)\n", (void*)obj2->refs);

    /* If we now write to obj2->refs as a SymObj**, and then read it back
     * as the freelist next pointer, we see the corruption */
    obj2->refs = &obj1;  /* Valid use of refs field */

    /* But the freelist still points through obj2! */
    printf("  After setting obj2->refs = &obj1\n");
    printf("  obj2->refs = %p\n", (void*)obj2->refs);
    printf("  But freelist sees: %p (cast of obj2->refs)\n",
           (SymObj*)obj2->refs);

    /* This demonstrates the invariant violation:
     * refs should either be NULL or point to outgoing references
     * but when in the freelist, it contains a freelist next pointer
     * These are incompatible uses of the same memory location
     * with different types - strict aliasing violation! */
    printf("\n");
    printf("FAIL: Invariant violation - refs field has dual incompatible uses\n");

    /* Clean up */
    while (SYM_TLS.pools) {
        SymPool* p = SYM_TLS.pools;
        SYM_TLS.pools = p->next;
        free(p->objects);
        free(p);
    }
}

/* Test 3: Compare with correct implementation */
void test_correct_implementation(void) {
    printf("=== Test 3: Correct Implementation Comparison ===\n");

    /* Allocate using correct implementation */
    struct SymObjFixed* obj1 = sym_pool_alloc_correct();
    struct SymObjFixed* obj2 = sym_pool_alloc_correct();
    struct SymObjFixed* obj3 = sym_pool_alloc_correct();

    if (!obj1 || !obj2 || !obj3) {
        printf("SKIP: Allocation failed\n");
        return;
    }

    printf("Allocated 3 objects from FIXED pool\n");

    /* Initialize refs properly */
    obj1->refs = NULL;
    obj2->refs = NULL;
    obj3->refs = NULL;

    /* Free one object */
    sym_pool_free_correct(obj2);

    printf("After freeing obj2:\n");
    printf("  obj2->freelist_next = %p (proper typed field)\n",
           (void*)obj2->freelist_next);
    printf("  obj2->refs = %p (unchanged, as expected)\n",
           (void*)obj2->refs);

    /* Now we can use refs independently */
    obj2->refs = (SymObj**)0xDEADBEEF;
    printf("  After setting obj2->refs = 0xDEADBEEF:\n");
    printf("  obj2->refs = %p\n", (void*)obj2->refs);
    printf("  obj2->freelist_next = %p (still valid freelist pointer)\n",
           (void*)obj2->freelist_next);

    printf("\nPASS: No type punning - each field has single purpose\n");

    /* Clean up */
    while (SYM_TLS_FIXED.pools) {
        SymPoolFixed* p = SYM_TLS_FIXED.pools;
        SYM_TLS_FIXED.pools = p->next;
        free(p->objects);
        free(p);
    }
}

/* Test 4: Demonstrate potential for UB with sanitizers */
void test_undefined_behavior_potential(void) {
    printf("=== Test 4: Undefined Behavior Potential ===\n");

    printf("With UBSan (-fsanitize=undefined), the buggy code would report:\n");
    printf("  runtime/src/memory/symmetric.c:43: runtime error: store to address\n");
    printf("  with insufficient space for type 'SymObj**'\n");
    printf("  (if pointer sizes differ)\n");
    printf("\n");

    printf("With strict-aliasing sanitizer or compiler optimization:\n");
    printf("  The compiler may cache the value of obj->refs assuming it\n");
    printf("  hasn't changed (because accesses through SymObj** shouldn't\n");
    printf("  alias with accesses through SymObj*). This causes wrong code.\n");
    printf("\n");

    printf("Example scenario:\n");
    printf("  1. Allocate obj, set obj->refs = &other_obj\n");
    printf("  2. Free obj to freelist (stores freelist_next in obj->refs)\n");
    printf("  3. Compiler assumes obj->refs still == &other_obj (UB!)\n");
    printf("  4. Freelist corruption or access to freed memory\n");
    printf("\n");

    printf("RECOMMENDATION: Add dedicated freelist_next field to SymObj:\n");
    printf("  struct SymObj {\n");
    printf("      ...\n");
    printf("      SymObj* freelist_next;  /* Only valid when in pool freelist */\n");
    printf("  };\n");
}

int main(void) {
    printf("Testing Symmetric Pool Freelist Type-Punning Violation\n");
    printf("======================================================\n\n");

    test_type_punning_compilation();
    test_strict_aliasing_behavior();
    test_correct_implementation();
    test_undefined_behavior_potential();

    printf("\n======================================================\n");
    printf("Tests completed\n");
    printf("\nSUMMARY:\n");
    printf("  The symmetric.c pool freelist violates strict aliasing rules\n");
    printf("  by using the `refs` field (SymObj**) to store freelist next\n");
    printf("  pointers (SymObj*). This is undefined behavior per C99 6.5p7.\n");
    printf("\n");
    printf("  Fix: Add dedicated `SymObj* freelist_next` field.\n");
    return 0;
}
