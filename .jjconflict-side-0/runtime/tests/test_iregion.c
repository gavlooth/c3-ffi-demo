/*
 * IRegion Vtable Tests
 *
 * Tests for pluggable region backends via vtable interface.
 * Covers Arena, Linear, Offset, and Pool region types.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/memory/region.h"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %s: ", #name); \
    name(); \
    tests_run++; \
    tests_passed++; \
    printf("\033[32mPASS\033[0m\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\033[31mFAIL\033[0m (line %d: %s)\n", __LINE__, #cond); \
        tests_run++; \
        return; \
    } \
} while(0)

/* ========== Arena Region Tests ========== */

TEST(test_arena_basic) {
    IRegion* arena = iregion_new_arena(4096);
    ASSERT(arena != NULL);
    ASSERT(iregion_kind(arena) == REGION_KIND_ARENA);
    ASSERT(!iregion_is_frozen(arena));

    /* Allocate some memory */
    int* p1 = (int*)iregion_alloc(arena, sizeof(int), sizeof(int));
    ASSERT(p1 != NULL);
    *p1 = 42;

    int* p2 = (int*)iregion_alloc(arena, sizeof(int), sizeof(int));
    ASSERT(p2 != NULL);
    *p2 = 100;

    ASSERT(*p1 == 42);
    ASSERT(*p2 == 100);

    iregion_free_all(arena);
}

TEST(test_arena_multiple_blocks) {
    /* Small arena to force multiple blocks */
    IRegion* arena = iregion_new_arena(64);
    ASSERT(arena != NULL);

    /* Allocate more than one block's worth */
    for (int i = 0; i < 100; i++) {
        int* p = (int*)iregion_alloc(arena, sizeof(int), sizeof(int));
        ASSERT(p != NULL);
        *p = i;
    }

    size_t alloc_count, bytes_used, bytes_total;
    iregion_stats(arena, &alloc_count, &bytes_used, &bytes_total);
    ASSERT(alloc_count == 100);
    ASSERT(bytes_total > 64);  /* Should have grown */

    iregion_free_all(arena);
}

TEST(test_arena_freeze) {
    IRegion* arena = iregion_new_arena(4096);
    ASSERT(arena != NULL);

    int* p1 = (int*)iregion_alloc(arena, sizeof(int), sizeof(int));
    ASSERT(p1 != NULL);

    iregion_freeze(arena);
    ASSERT(iregion_is_frozen(arena));

    /* Allocation should fail after freeze */
    int* p2 = (int*)iregion_alloc(arena, sizeof(int), sizeof(int));
    ASSERT(p2 == NULL);

    iregion_free_all(arena);
}

TEST(test_arena_free_one_noop) {
    IRegion* arena = iregion_new_arena(4096);
    ASSERT(arena != NULL);

    int* p = (int*)iregion_alloc(arena, sizeof(int), sizeof(int));
    ASSERT(p != NULL);
    *p = 123;

    /* free_one should be a no-op for arena */
    iregion_free_one(arena, p);

    /* Value should still be valid */
    ASSERT(*p == 123);

    iregion_free_all(arena);
}

/* ========== Linear Region Tests ========== */

TEST(test_linear_basic) {
    IRegion* linear = iregion_new_linear(4096);
    ASSERT(linear != NULL);
    ASSERT(iregion_kind(linear) == REGION_KIND_LINEAR);

    int* p = (int*)iregion_alloc(linear, sizeof(int), sizeof(int));
    ASSERT(p != NULL);
    *p = 999;
    ASSERT(*p == 999);

    size_t remaining = iregion_remaining(linear);
    ASSERT(remaining > 0);
    ASSERT(remaining < 4096);

    iregion_free_all(linear);
}

TEST(test_linear_clone) {
    IRegion* linear = iregion_new_linear(4096);
    ASSERT(linear != NULL);

    int* p = (int*)iregion_alloc(linear, sizeof(int), sizeof(int));
    ASSERT(p != NULL);
    *p = 12345;

    IRegion* clone = iregion_clone(linear);
    ASSERT(clone != NULL);

    /* Clone should have same data */
    size_t s1_alloc, s1_used, s1_total;
    size_t s2_alloc, s2_used, s2_total;
    iregion_stats(linear, &s1_alloc, &s1_used, &s1_total);
    iregion_stats(clone, &s2_alloc, &s2_used, &s2_total);
    ASSERT(s1_used == s2_used);

    iregion_free_all(linear);
    iregion_free_all(clone);
}

TEST(test_linear_serialize) {
    IRegion* linear = iregion_new_linear(4096);
    ASSERT(linear != NULL);

    /* Write some data */
    int* values = (int*)iregion_alloc(linear, 10 * sizeof(int), sizeof(int));
    ASSERT(values != NULL);
    for (int i = 0; i < 10; i++) {
        values[i] = i * 10;
    }

    size_t size;
    void* serialized = iregion_serialize(linear, &size);
    ASSERT(serialized != NULL);
    ASSERT(size >= 10 * sizeof(int));

    /* Verify serialized data */
    int* data = (int*)serialized;
    for (int i = 0; i < 10; i++) {
        ASSERT(data[i] == i * 10);
    }

    free(serialized);
    iregion_free_all(linear);
}

TEST(test_linear_exhaust) {
    IRegion* linear = iregion_new_linear(64);
    ASSERT(linear != NULL);

    /* Keep allocating until we run out */
    int alloc_count = 0;
    while (iregion_alloc(linear, 16, 8) != NULL) {
        alloc_count++;
        if (alloc_count > 100) break;  /* Safety limit */
    }

    ASSERT(alloc_count >= 1);
    ASSERT(alloc_count <= 4);  /* 64 bytes / 16 = 4 allocations max */

    iregion_free_all(linear);
}

/* ========== Offset Region Tests ========== */

TEST(test_offset_basic) {
    IRegion* offset = iregion_new_offset(4096);
    ASSERT(offset != NULL);
    ASSERT(iregion_kind(offset) == REGION_KIND_OFFSET);

    int* p = (int*)iregion_alloc(offset, sizeof(int), sizeof(int));
    ASSERT(p != NULL);
    *p = 777;
    ASSERT(*p == 777);

    iregion_free_all(offset);
}

TEST(test_offset_serialize_deserialize) {
    OffsetRegion* offset = offset_region_new(4096);
    ASSERT(offset != NULL);

    /* Allocate and write data */
    OffsetPtr p1 = offset_region_alloc(offset, sizeof(int), sizeof(int));
    ASSERT(p1 != OFFSET_NULL);
    int* v1 = (int*)offset_to_ptr(offset, p1);
    *v1 = 12345;

    OffsetPtr p2 = offset_region_alloc(offset, sizeof(int), sizeof(int));
    ASSERT(p2 != OFFSET_NULL);
    int* v2 = (int*)offset_to_ptr(offset, p2);
    *v2 = 67890;

    /* Serialize */
    size_t size;
    void* serialized = offset_region_serialize(offset, &size);
    ASSERT(serialized != NULL);

    /* Deserialize */
    OffsetRegion* restored = offset_region_deserialize(serialized, size);
    ASSERT(restored != NULL);

    /* Verify data */
    int* rv1 = (int*)offset_to_ptr(restored, p1);
    int* rv2 = (int*)offset_to_ptr(restored, p2);
    ASSERT(*rv1 == 12345);
    ASSERT(*rv2 == 67890);

    free(serialized);
    offset_region_free(offset);
    offset_region_free(restored);
}

TEST(test_offset_clone) {
    IRegion* offset = iregion_new_offset(4096);
    ASSERT(offset != NULL);

    int* p = (int*)iregion_alloc(offset, sizeof(int), sizeof(int));
    ASSERT(p != NULL);
    *p = 54321;

    IRegion* clone = iregion_clone(offset);
    ASSERT(clone != NULL);

    iregion_free_all(offset);
    iregion_free_all(clone);
}

/* ========== Pool Region Tests ========== */

TEST(test_pool_basic) {
    IRegion* pool = iregion_new_pool(sizeof(int), 100);
    ASSERT(pool != NULL);
    ASSERT(iregion_kind(pool) == REGION_KIND_POOL);

    int* p = (int*)iregion_alloc(pool, 0, 0);
    ASSERT(p != NULL);
    *p = 111;
    ASSERT(*p == 111);

    iregion_free_all(pool);
}

TEST(test_pool_alloc_free) {
    IRegion* pool = iregion_new_pool(sizeof(int), 10);
    ASSERT(pool != NULL);

    /* Allocate all slots */
    int* ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = (int*)iregion_alloc(pool, 0, 0);
        ASSERT(ptrs[i] != NULL);
        *ptrs[i] = i;
    }

    /* Pool should be full */
    int* extra = (int*)iregion_alloc(pool, 0, 0);
    ASSERT(extra == NULL);

    /* Free one and reallocate */
    iregion_free_one(pool, ptrs[5]);
    int* reused = (int*)iregion_alloc(pool, 0, 0);
    ASSERT(reused != NULL);
    ASSERT(reused == ptrs[5]);  /* Should reuse same slot */

    iregion_free_all(pool);
}

TEST(test_pool_free_one) {
    IRegion* pool = iregion_new_pool(32, 5);
    ASSERT(pool != NULL);

    void* p1 = iregion_alloc(pool, 0, 0);
    void* p2 = iregion_alloc(pool, 0, 0);
    void* p3 = iregion_alloc(pool, 0, 0);

    size_t count, used, total;
    iregion_stats(pool, &count, &used, &total);
    ASSERT(count == 3);

    iregion_free_one(pool, p2);
    iregion_stats(pool, &count, &used, &total);
    ASSERT(count == 2);

    /* Reallocate should give back p2's slot */
    void* p4 = iregion_alloc(pool, 0, 0);
    ASSERT(p4 == p2);

    iregion_free_all(pool);
}

TEST(test_pool_stats) {
    IRegion* pool = iregion_new_pool(64, 20);
    ASSERT(pool != NULL);

    for (int i = 0; i < 15; i++) {
        iregion_alloc(pool, 0, 0);
    }

    size_t alloc_count, bytes_used, bytes_total;
    iregion_stats(pool, &alloc_count, &bytes_used, &bytes_total);
    ASSERT(alloc_count == 15);
    ASSERT(bytes_used == 15 * 64);
    ASSERT(bytes_total == 20 * 64);

    size_t remaining = iregion_remaining(pool);
    ASSERT(remaining == 5 * 64);

    iregion_free_all(pool);
}

/* ========== IREGION_ALLOC Macro Tests ========== */

typedef struct {
    int x;
    int y;
    int z;
} Point3D;

TEST(test_typed_alloc_macro) {
    IRegion* arena = iregion_new_arena(4096);
    ASSERT(arena != NULL);

    Point3D* pt = IREGION_ALLOC(arena, Point3D);
    ASSERT(pt != NULL);
    pt->x = 1;
    pt->y = 2;
    pt->z = 3;
    ASSERT(pt->x == 1 && pt->y == 2 && pt->z == 3);

    int* arr = IREGION_ALLOC_ARRAY(arena, int, 10);
    ASSERT(arr != NULL);
    for (int i = 0; i < 10; i++) {
        arr[i] = i * i;
    }
    ASSERT(arr[9] == 81);

    iregion_free_all(arena);
}

/* ========== Mixed Region Tests ========== */

TEST(test_region_polymorphism) {
    /* Test that all region types work through the same interface */
    IRegion* regions[4];
    regions[0] = iregion_new_arena(1024);
    regions[1] = iregion_new_linear(1024);
    regions[2] = iregion_new_offset(1024);
    regions[3] = iregion_new_pool(sizeof(int), 100);

    for (int r = 0; r < 4; r++) {
        ASSERT(regions[r] != NULL);

        /* All should support allocation */
        int* p = (int*)iregion_alloc(regions[r], sizeof(int), sizeof(int));
        ASSERT(p != NULL);
        *p = r * 100;

        /* All should report stats */
        size_t count, used, total;
        iregion_stats(regions[r], &count, &used, &total);
        ASSERT(count >= 1);

        /* All should support freeze */
        iregion_freeze(regions[r]);
        ASSERT(iregion_is_frozen(regions[r]));
    }

    /* Cleanup */
    for (int r = 0; r < 4; r++) {
        iregion_free_all(regions[r]);
    }
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== IRegion Vtable Tests ===\033[0m\n");

    printf("\n\033[33m--- Arena Region ---\033[0m\n");
    RUN_TEST(test_arena_basic);
    RUN_TEST(test_arena_multiple_blocks);
    RUN_TEST(test_arena_freeze);
    RUN_TEST(test_arena_free_one_noop);

    printf("\n\033[33m--- Linear Region ---\033[0m\n");
    RUN_TEST(test_linear_basic);
    RUN_TEST(test_linear_clone);
    RUN_TEST(test_linear_serialize);
    RUN_TEST(test_linear_exhaust);

    printf("\n\033[33m--- Offset Region ---\033[0m\n");
    RUN_TEST(test_offset_basic);
    RUN_TEST(test_offset_serialize_deserialize);
    RUN_TEST(test_offset_clone);

    printf("\n\033[33m--- Pool Region ---\033[0m\n");
    RUN_TEST(test_pool_basic);
    RUN_TEST(test_pool_alloc_free);
    RUN_TEST(test_pool_free_one);
    RUN_TEST(test_pool_stats);

    printf("\n\033[33m--- Macro & Polymorphism ---\033[0m\n");
    RUN_TEST(test_typed_alloc_macro);
    RUN_TEST(test_region_polymorphism);

    printf("\n\033[33m=== Summary ===\033[0m\n");
    printf("  Total:  %d\n", tests_run);
    if (tests_passed == tests_run) {
        printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
    } else {
        printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
        printf("  \033[31mFailed: %d\033[0m\n", tests_run - tests_passed);
    }

    return (tests_passed == tests_run) ? 0 : 1;
}
