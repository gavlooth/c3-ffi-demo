#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <setjmp.h>
#include <assert.h>

/* Simple test framework macros */
#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (%s:%d): %s\n", #cond, __FILE__, __LINE__, msg); \
        exit(1); \
    } \
} while(0)

#define ASSERT_EQ(a, b, msg) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "FAIL: %s != %s (%s:%d): %s\n", #a, #b, __FILE__, __LINE__, msg); \
        exit(1); \
    } \
} while(0)

/* Include the runtime source to get access to internal definitions and implementations */
#include "../src/runtime.c"

/* REMOVED: Component system was abandoned in favor of RC-G Region model */

/* BUG-0002: Pool Bounds Staleness */
void test_fix_pool_bounds(void) {
    printf("=== Test Fix: Pool Bounds Staleness ===\n");
    
    /* Trigger pool growth */
    int initial_slots = SLOT_POOL_BLOCK_SIZE;
    int count = initial_slots + 100;
    Obj** objs = malloc(count * sizeof(Obj*));
    
    for (int i = 0; i < count; i++) {
        objs[i] = handle_alloc_obj();
        ASSERT(handle_is_pool_obj(objs[i]), "Object should be recognized as pool object");
    }
    
    ASSERT(handle_is_pool_obj(objs[count-1]), "New block object should be recognized");
    
    for (int i = 0; i < count; i++) {
        handle_free_obj(objs[i]);
    }
    free(objs);
    
    printf("PASS: Pool bounds updated correctly\n");
}

/* BUG-0003: Frame Clone jmp_buf */
void test_fix_frame_clone(void) {
    printf("=== Test Fix: Frame Clone jmp_buf ===\n");
    
    Frame* f = frame_alloc(FRAME_PROMPT);
    f->prompt.tag = 123;
    
    Frame* clone = frame_clone(f);
    
    ASSERT(clone->prompt.cloned, "Cloned prompt frame should have cloned=true");
    
    frame_free(clone);
    frame_free(f);
    
    printf("PASS: Frame clone marked correctly\n");
}

/* BUG-0004: SCC Refcount Underflow */
void test_fix_scc_underflow(void) {
    printf("=== Test Fix: SCC Refcount Underflow ===\n");
    
    /* Using runtime.c version of create_scc which takes void */
    SCC* scc = create_scc();
    
    ASSERT_EQ(scc->ref_count, 1, "Initial refcount is 1");
    
    release_scc(scc);
    ASSERT_EQ(scc->ref_count, 0, "Refcount is 0");
    
    printf("PASS: SCC release logic verified\n");
}

/* BUG-0005: Type Punning */
void test_fix_type_punning(void) {
    printf("=== Test Fix: Symmetric Pool Type Punning ===\n");
    
    /* sym_obj_new is from symmetric.c (linked) */
    SymObj* p1 = sym_obj_new(NULL, NULL);
    ASSERT(p1 != NULL, "p1 allocation failed");
    
    printf("PASS: Pool alloc works\n");
}

int main(void) {
    printf("Verifying Fixes for OmniLisp Runtime Bugs\n");
    printf("=========================================\n");

    /* test_fix_component_merge_overflow() - REMOVED: Component system abandoned */
    test_fix_pool_bounds();
    test_fix_frame_clone();
    test_fix_scc_underflow();
    test_fix_type_punning();

    printf("\nAll verified!\n");
    return 0;
}