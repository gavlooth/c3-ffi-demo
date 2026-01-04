#include "test_framework.h"
#include "../src/memory/component.h"

void test_component_basic(void) {
    SymComponent* c = sym_component_new();
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(c->handle_count, 0);
    
    sym_acquire_handle(c);
    ASSERT_EQ(c->handle_count, 1);
    
    sym_release_handle(c);
    /* Component should be dismantled here as handle_count becomes 0 */
    PASS();
}

void test_component_tether(void) {
    SymComponent* c = sym_component_new();
    sym_acquire_handle(c);
    
    SymTetherToken t = sym_tether_begin(c);
    ASSERT_EQ(c->tether_count, 1);
    
    sym_release_handle(c);
    /* handle_count is 0, but tether_count is 1, so NOT dismantled yet */
    ASSERT_EQ(c->tether_count, 1);
    
    sym_tether_end(t);
    /* Now dismantled */
    PASS();
}

void test_component_cycle(void) {
    SymComponent* c = sym_component_new();
    sym_acquire_handle(c);
    
    SymObj* sa = malloc(sizeof(SymObj));
    memset(sa, 0, sizeof(SymObj));
    sa->inline_refs[0] = NULL;
    sa->refs = sa->inline_refs;
    
    SymObj* sb = malloc(sizeof(SymObj));
    memset(sb, 0, sizeof(SymObj));
    sb->inline_refs[0] = NULL;
    sb->refs = sb->inline_refs;
    
    sym_component_add_member(c, sa);
    sym_component_add_member(c, sb);
    
    /* Create cycle A <-> B */
    sa->refs[sa->ref_count++] = sb;
    sb->internal_rc++;
    
    sb->refs[sb->ref_count++] = sa;
    sa->internal_rc++;
    
    sym_release_handle(c);
    /* Both sa and sb should be freed by dismantle */
    PASS();
}

void run_component_tests(void) {
    TEST_SUITE("Component Tethering");
    RUN_TEST(test_component_basic);
    RUN_TEST(test_component_tether);
    RUN_TEST(test_component_cycle);
}
