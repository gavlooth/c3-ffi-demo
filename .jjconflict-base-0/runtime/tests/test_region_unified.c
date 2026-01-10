#include "test_framework.h"
#include "../src/memory/region.h"
#include "../src/memory/component.h"

void test_region_component_alloc(void) {
    RegionContext* ctx = region_context_new();
    Region* r = ctx->root;
    
    SymComponent* c = region_alloc_component(r);
    ASSERT_NOT_NULL(c);
    
    /* Should be tracked */
    ASSERT_EQ(r->component_count, 1);
    ASSERT_EQ(r->components[0], c);
    
    /* Should have 1 handle (from the region) */
    ASSERT_EQ(c->handle_count, 1);
    
    region_context_free(ctx);
    /* Context free destroys region, which should release component handle/dismantle */
    /* We can't easily verify it's freed after free, but Valgrind will */
    PASS();
}

void run_region_unified_tests(void) {
    TEST_SUITE("Unified Region-Based Memory");
    RUN_TEST(test_region_component_alloc);
}
