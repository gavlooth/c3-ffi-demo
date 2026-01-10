/*
 * Phase 33.4 - Pair Batch Allocator Soundness Tests
 *
 * This test exists to prevent a subtle but catastrophic bug:
 * - The pair "batch allocator" must allocate pair objects into the DESTINATION region.
 * - It must NOT allocate pairs from a temporary arena that is freed at the end of transmigrate().
 *
 * Contract:
 * - After transmigrate(list, src, dest) returns, every pair node in the returned list must
 *   be allocated inside dest's allocation domain (arena chunks or inline buffer).
 */

/* test_framework.h is provided by test_main.c */

static int ptr_in_region(const Region* r, const void* p) {
    if (!r || !p) return 0;

    uintptr_t addr = (uintptr_t)p;

    /* Inline buffer range (only the used prefix is valid allocation space) */
    uintptr_t inline_start = (uintptr_t)r->inline_buf.buffer;
    uintptr_t inline_end = inline_start + r->inline_buf.offset;
    if (addr >= inline_start && addr < inline_end) {
        return 1;
    }

    /* Arena chunk ranges */
    for (ArenaChunk* c = r->arena.begin; c; c = c->next) {
        uintptr_t start = (uintptr_t)c->data;
        uintptr_t end = start + (c->capacity * sizeof(uintptr_t));
        if (addr >= start && addr < end) {
            return 1;
        }
    }

    return 0;
}

static void test_transmigrate_pair_batch_allocates_in_dest_region(void) {
    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Build a list large enough to strongly exercise batch allocation. */
    const int N = 200;
    Obj* list = NULL;
    for (int i = 0; i < N; i++) {
        /* Use an immediate integer for the car (no extra heap edges). */
        list = mk_cell_region(src, mk_int_unboxed(i), list);
    }
    ASSERT_NOT_NULL(list);

    Obj* moved = (Obj*)transmigrate(list, src, dest);
    ASSERT_NOT_NULL(moved);

    /* Source region can now die; moved must remain valid and must live in dest. */
    region_exit(src);

    Obj* it = moved;
    for (int i = 0; i < N; i++) {
        ASSERT_NOT_NULL(it);
        ASSERT_EQ(it->tag, TAG_PAIR);

        /* Critical assertion: every node must be allocated in dest, not temp arena. */
        ASSERT_TRUE(ptr_in_region(dest, it));

        /* Cars are immediate ints in this test */
        ASSERT_TRUE(IS_IMMEDIATE(it->a));

        it = it->b;
    }

    /* End of list */
    ASSERT_NULL(it);

    region_exit(dest);
}

void run_transmigrate_pair_batch_tests(void) {
    TEST_SUITE("CTRR Transmigration Pair Batch Soundness (Phase 33.4)");

    TEST_SECTION("Batch Allocation Domain");
    RUN_TEST(test_transmigrate_pair_batch_allocates_in_dest_region);
}
