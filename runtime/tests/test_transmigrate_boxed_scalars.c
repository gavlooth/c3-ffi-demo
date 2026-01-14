/*
 * Phase 34.x (Correctness Hardening) - Boxed Scalar Transmigration
 *
 * Why this test exists:
 * - OmniLisp supports immediate (tagged-pointer) integers/chars, but also boxed
 *   scalar objects (e.g., ints outside the immediate range, boxed floats, boxed
 *   chars outside the immediate codepoint range, etc).
 *
 * CTRR contract requirement:
 * - After `transmigrate(root, src, dst)`, no reachable value may contain any
 *   pointer into the closing `src` region.
 *
 * Failure mode this test catches:
 * - Treating boxed scalar objects as "immediate" in metadata clone logic
 *   (returning the old pointer instead of allocating a copy in `dst`).
 *
 * This is catastrophic: once `src` is exited and reclaimed/reused, the returned
 * pointer would dangle and violate CTRR Region Closure.
 */

/* test_framework.h is provided by test_main.c */

static int boxed_scalars_ptr_in_region(const Region* r, const void* p) {
    if (!r || !p) return 0;

    uintptr_t addr = (uintptr_t)p;

    /* Inline buffer range (only the used prefix is valid allocation space) */
    uintptr_t inline_start = (uintptr_t)r->inline_buf.buffer;
    uintptr_t inline_end = inline_start + r->inline_buf.offset;
    if (addr >= inline_start && addr < inline_end) return 1;

    /* Arena chunk ranges */
    for (ArenaChunk* c = r->arena.begin; c; c = c->next) {
        uintptr_t start = (uintptr_t)ARENA_CHUNK_DATA(c);
        uintptr_t end = start + (ARENA_CHUNK_CAPACITY(c) * sizeof(uintptr_t));
        if (addr >= start && addr < end) return 1;
    }

    return 0;
}

static void test_transmigrate_boxed_int_moves_to_dest(void) {
    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Use a value that would be boxed in the public mk_int() constructor. */
    long v = (long)(IMM_INT_MAX - 1);
    Obj* boxed = mk_int_region(src, v);
    ASSERT_NOT_NULL(boxed);
    ASSERT_EQ(boxed->tag, TAG_INT);
    ASSERT_TRUE(boxed_scalars_ptr_in_region(src, boxed));

    Obj* moved = (Obj*)transmigrate(boxed, src, dest);
    ASSERT_NOT_NULL(moved);
    ASSERT_EQ(moved->tag, TAG_INT);
    ASSERT_EQ(moved->i, v);

    /* Critical: result must NOT live in src's allocation domain. */
    ASSERT_TRUE(boxed_scalars_ptr_in_region(dest, moved));
    ASSERT_FALSE(boxed_scalars_ptr_in_region(src, moved));

    region_exit(src);
    region_exit(dest);
}

static void test_transmigrate_boxed_float_moves_to_dest(void) {
    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    Obj* boxed = mk_float_region(src, 3.141592653589793);
    ASSERT_NOT_NULL(boxed);
    ASSERT_EQ(boxed->tag, TAG_FLOAT);
    ASSERT_TRUE(boxed_scalars_ptr_in_region(src, boxed));

    Obj* moved = (Obj*)transmigrate(boxed, src, dest);
    ASSERT_NOT_NULL(moved);
    ASSERT_EQ(moved->tag, TAG_FLOAT);
    ASSERT_TRUE(moved->f > 3.1415926 && moved->f < 3.1415930);

    ASSERT_TRUE(boxed_scalars_ptr_in_region(dest, moved));
    ASSERT_FALSE(boxed_scalars_ptr_in_region(src, moved));

    region_exit(src);
    region_exit(dest);
}

static void test_transmigrate_boxed_char_moves_to_dest(void) {
    Region* src = region_create();
    Region* dest = region_create();
    ASSERT_NOT_NULL(src);
    ASSERT_NOT_NULL(dest);

    /* Use an out-of-range codepoint to ensure this is legitimately boxed. */
    long codepoint = 0x110000;
    Obj* boxed = mk_char_region(src, codepoint);
    ASSERT_NOT_NULL(boxed);
    ASSERT_EQ(boxed->tag, TAG_CHAR);
    ASSERT_TRUE(boxed_scalars_ptr_in_region(src, boxed));

    Obj* moved = (Obj*)transmigrate(boxed, src, dest);
    ASSERT_NOT_NULL(moved);
    ASSERT_EQ(moved->tag, TAG_CHAR);
    ASSERT_EQ(moved->i, codepoint);

    ASSERT_TRUE(boxed_scalars_ptr_in_region(dest, moved));
    ASSERT_FALSE(boxed_scalars_ptr_in_region(src, moved));

    region_exit(src);
    region_exit(dest);
}

void run_transmigrate_boxed_scalars_tests(void) {
    TEST_SUITE("CTRR Transmigration Boxed Scalars (Correctness)");

    TEST_SECTION("Boxed Scalar Movement");
    RUN_TEST(test_transmigrate_boxed_int_moves_to_dest);
    RUN_TEST(test_transmigrate_boxed_float_moves_to_dest);
    RUN_TEST(test_transmigrate_boxed_char_moves_to_dest);
}
