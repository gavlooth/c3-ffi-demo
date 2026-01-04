#include "test_framework.h"
#include "../src/memory/component.h"

void test_dynamic_merge_basic(void) {
    SymComponent* c1 = sym_component_new();
    sym_acquire_handle(c1);
    
    SymComponent* c2 = sym_component_new();
    sym_acquire_handle(c2);
    
    ASSERT(c1 != c2);
    
    /* Merge c2 into c1 */
    sym_component_union(c1, c2);
    
    SymComponent* root1 = sym_component_find(c1);
    SymComponent* root2 = sym_component_find(c2);
    
    ASSERT(root1 == root2);
    ASSERT_EQ(root1->handle_count, 2); /* Combined handles */
    
    sym_release_handle(c1);
    ASSERT_EQ(root1->handle_count, 1);
    
    sym_release_handle(c2);
    /* Should dismantle now */
    PASS();
}

void test_dynamic_merge_cycle(void) {
    /* Create two separate components with objects */
    SymComponent* c1 = sym_component_new();
    sym_acquire_handle(c1);
    SymObj* a = sym_alloc(mk_int(1));
    sym_component_add_member(c1, a);
    
    SymComponent* c2 = sym_component_new();
    sym_acquire_handle(c2);
    SymObj* b = sym_alloc(mk_int(2));
    sym_component_add_member(c2, b);
    
    /* Link them: a -> b (should trigger merge) */
    /* Note: sym_link calls sym_ctx_link which calls union */
    /* We need to use the public API wrapper or call ctx_link if we had ctx */
    /* Let's simulate what runtime.c sym_link does */
    
    if (a->comp && b->comp && a->comp != b->comp) {
        sym_component_union(a->comp, b->comp);
    }
    
    /* Now they should be same component */
    ASSERT(sym_component_find(a->comp) == sym_component_find(b->comp));
    
    /* Complete the cycle: b -> a */
    /* Internal refs setup */
    a->refs[0] = b;
    a->ref_count = 1;
    b->internal_rc++;
    
    b->refs[0] = a;
    b->ref_count = 1;
    a->internal_rc++;
    
    /* Release external handles */
    sym_release_handle(c1);
    sym_release_handle(c2);
    
    /* If merged correctly, dismantle should free both */
    PASS();
}

void run_dynamic_merge_tests(void) {
    TEST_SUITE("Dynamic Component Merging");
    RUN_TEST(test_dynamic_merge_basic);
    RUN_TEST(test_dynamic_merge_cycle);
}
