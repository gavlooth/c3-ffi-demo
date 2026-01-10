/*
 * Test: Slot Pool Bounds Staleness After Pool Growth
 *
 * Expected invariant: handle_is_pool_obj() should correctly identify all
 * objects allocated from the handle system's global slot pool, including those
 * from blocks added after the pool grows.
 *
 * Observed behavior: The g_pool_bounds global is only updated lazily when
 * handle_is_pool_obj() misses and calls update_pool_bounds(). However,
 * pool_grow() does NOT update the bounds. This means objects from newly
 * allocated blocks may be incorrectly identified as non-pool objects until
 * a bounds update is triggered.
 *
 * Why it violates architecture: handle_is_pool_obj() is critical for
 * routing free operations to the correct path (pool free vs regular free).
 * If it returns false for a pool object, handle_free_obj() will silently
 * do nothing (return early), causing a memory leak because the object is
 * never actually freed back to the pool.
 *
 * Refs: runtime/src/memory/handle.c:73-118 (update_pool_bounds, handle_is_pool_obj)
 *       runtime/src/memory/slot_pool.c:165-211 (pool_grow)
 *
 * Root cause analysis:
 * 1. handle_is_pool_obj() maintains g_pool_bounds list of block address ranges
 * 2. update_pool_bounds() rebuilds this list, but is only called on a "miss"
 * 3. pool_grow() adds new blocks but does NOT call update_pool_bounds()
 * 4. Objects from new blocks will fail the bounds check until a miss occurs
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/*
 * Force a smaller block size for testing.
 * This allows us to trigger pool growth with fewer allocations.
 */
#undef SLOT_POOL_BLOCK_SIZE
#define SLOT_POOL_BLOCK_SIZE 64  /* Small block size for testing */

/* Include runtime for SlotPool and handle functions */
#include "../src/memory/slot_pool.c"
#include "../src/memory/handle.c"

/* Use the MinimalObj from handle.c */
/* OMNI_OBJ_SIZE is already defined in slot_pool.c */

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

/* Forward declarations to access internal state for testing */
extern SlotPool* slot_pool_global(void);

/*
 * Test that demonstrates the bounds staleness issue directly.
 *
 * The key insight: when pool_grow() adds a new block, g_pool_bounds
 * is NOT updated. handle_is_pool_obj() will return false for objects
 * from the new block until update_pool_bounds() is triggered by a miss.
 *
 * This test:
 * 1. Gets the global pool (used by handle system)
 * 2. Allocates an object to initialize bounds tracking
 * 3. Forces pool growth by allocating many objects
 * 4. Checks if objects from the new block are recognized
 *
 * Expected: All objects should be recognized as pool objects
 * Actual (bug): Objects from new blocks return false until bounds update
 */
TEST(test_pool_bounds_staleness_global) {
    /* Initialize handle system (creates global pool) */
    handle_system_init();

    SlotPool* pool = slot_pool_global();
    ASSERT(pool != NULL);

    printf("\n    [DEBUG] Initial block_count=%zu\n", pool->block_count);

    /* Allocate first object to initialize bounds tracking */
    Obj* obj1 = handle_alloc_obj();
    ASSERT(obj1 != NULL);

    /* Force bounds initialization by checking is_pool_obj */
    bool is_pool1 = handle_is_pool_obj(obj1);
    ASSERT(is_pool1 == true);  /* Should be true */
    printf("    [DEBUG] First object recognized as pool object\n");

    /* Store the initial block count */
    size_t initial_block_count = pool->block_count;

    /* Allocate enough objects to force pool growth */
    /* SLOT_POOL_BLOCK_SIZE is now 64 (forced for testing) */
    #define NUM_ALLOCS 200  /* More than our test block size */

    Obj* objs[NUM_ALLOCS];
    int allocated = 0;

    for (int i = 0; i < NUM_ALLOCS; i++) {
        objs[i] = handle_alloc_obj();
        if (objs[i] == NULL) {
            printf("    [DEBUG] Allocation stopped at %d\n", i);
            break;
        }
        allocated++;

        /* Check if pool grew */
        if (pool->block_count > initial_block_count) {
            printf("    [DEBUG] Pool grew! block_count=%zu at allocation %d\n",
                   pool->block_count, i);

            /* Immediately test an object from the NEW block */
            /* The bug: this object may not be recognized as pool object */
            bool is_pool_new = handle_is_pool_obj(objs[i]);
            if (!is_pool_new) {
                printf("    [FAIL] Object from new block (alloc %d, ptr=%p) not recognized!\n",
                       i, (void*)objs[i]);
                printf("    [FAIL] g_pool_bounds is stale - update_pool_bounds() not called\n");
                printf("    [FAIL] This causes handle_free_obj() to silently do nothing -> LEAK\n");

                /* Clean up before returning */
                for (int j = 0; j <= i; j++) {
                    handle_free_obj(objs[j]);
                }
                handle_free_obj(obj1);
                handle_system_shutdown();
                ASSERT(false);
            } else {
                printf("    [OK] Object from new block was recognized (bounds updated via miss path)\n");
            }
            break;  /* We found growth, stop allocating */
        }
    }

    /* If pool never grew, test is inconclusive */
    if (pool->block_count == initial_block_count) {
        printf("    [SKIP] Pool did not grow (allocated %d objects)\n", allocated);
        for (int i = 0; i < allocated; i++) {
            handle_free_obj(objs[i]);
        }
        handle_free_obj(obj1);
        handle_system_shutdown();
        return;
    }

    /* Cleanup */
    for (int i = 0; i < allocated; i++) {
        handle_free_obj(objs[i]);
    }
    handle_free_obj(obj1);
    handle_system_shutdown();
}

/*
 * Test that demonstrates memory leak caused by bounds staleness.
 *
 * If handle_is_pool_obj() returns false due to stale bounds,
 * handle_free_obj() will return early without actually freeing.
 * This causes a memory leak that accumulates over time.
 */
TEST(test_memory_leak_from_stale_bounds) {
    handle_system_init();
    (void)slot_pool_global();  /* Just to ensure init */

    /* Get initial stats */
    SlotPoolStats stats_before;
    handle_get_stats(&stats_before);

    /* Allocate and track objects */
    #define NUM_OBJS 100
    Obj* objs[NUM_OBJS];
    for (int i = 0; i < NUM_OBJS; i++) {
        objs[i] = handle_alloc_obj();
        ASSERT(objs[i] != NULL);
    }

    /* Get stats after allocation */
    SlotPoolStats stats_after_alloc;
    handle_get_stats(&stats_after_alloc);

    /* Free all objects */
    for (int i = 0; i < NUM_OBJS; i++) {
        handle_free_obj(objs[i]);
    }

    /* Get stats after free */
    SlotPoolStats stats_after_free;
    handle_get_stats(&stats_after_free);

    /* Verify that frees were counted */
    ASSERT(stats_after_free.total_frees > stats_before.total_frees);
    ASSERT(stats_after_free.in_use_slots < stats_after_alloc.in_use_slots);

    /* If in_use_slots is not near zero, we have a leak */
    if (stats_after_free.in_use_slots > 10) {
        printf("    [FAIL] Potential leak detected: in_use_slots=%zu after freeing %d objects\n",
               stats_after_free.in_use_slots, NUM_OBJS);
        printf("    [FAIL] This could indicate handle_free_obj() is not freeing due to stale bounds\n");
    }

    handle_system_shutdown();
}

/*
 * Test the specific interaction between pool_grow() and bounds tracking.
 *
 * This test directly manipulates the pool to demonstrate that pool_grow()
 * does not update g_pool_bounds.
 */
TEST(test_pool_grow_does_not_update_bounds) {
    handle_system_init();
    SlotPool* pool = slot_pool_global();

    /* Allocate one object to initialize bounds */
    Obj* obj1 = handle_alloc_obj();
    ASSERT(handle_is_pool_obj(obj1) == true);

    /* Force pool growth */
    size_t initial_blocks = pool->block_count;

    Obj* growth_objs[5000];
    int growth_count = 0;

    /* Allocate until pool grows */
    while (growth_count < 5000 && pool->block_count == initial_blocks) {
        growth_objs[growth_count] = handle_alloc_obj();
        if (growth_objs[growth_count] == NULL) break;
        growth_count++;
    }

    if (pool->block_count > initial_blocks) {
        printf("    [DEBUG] Pool grew from %zu to %zu blocks\n",
               initial_blocks, pool->block_count);

        /* The critical test: check an object allocated right after growth */
        /* At this point, g_pool_bounds has NOT been updated */
        /* So handle_is_pool_obj() might return false for the new object */

        /* Find the first object from the new block */
        /* (In practice, the freelist LIFO means most new allocations are from new block) */
        Obj* new_block_obj = growth_objs[growth_count - 1];

        /* First call might fail (stale bounds) */
        bool first_check = handle_is_pool_obj(new_block_obj);

        /* Trigger bounds update by checking a stack address (non-pool object) */
        int stack_var;
        handle_is_pool_obj((Obj*)&stack_var);  /* Will miss and trigger update */

        /* Second call should succeed (bounds updated) */
        bool second_check = handle_is_pool_obj(new_block_obj);

        if (!first_check && second_check) {
            printf("    [FAIL] Bounds were stale! first_check=%d, second_check=%d\n",
                   first_check, second_check);
            printf("    [FAIL] This proves pool_grow() does NOT update g_pool_bounds\n");
            printf("    [FAIL] Objects from new blocks are not recognized until a miss triggers update\n");

            /* Clean up */
            handle_free_obj(obj1);
            for (int i = 0; i < growth_count; i++) {
                handle_free_obj(growth_objs[i]);
            }
            handle_system_shutdown();
            ASSERT(false);
        }

        if (first_check && second_check) {
            printf("    [OK] Bounds update appears to work (possibly updated via another path)\n");
        }
    } else {
        printf("    [SKIP] Pool did not grow (allocated %d objects)\n", growth_count);
    }

    /* Clean up */
    handle_free_obj(obj1);
    for (int i = 0; i < growth_count; i++) {
        handle_free_obj(growth_objs[i]);
    }
    handle_system_shutdown();
}

int main(void) {
    printf("\n\033[33m=== Slot Pool Bounds Staleness Tests ===\033[0m\n");

    printf("\n\033[33m--- Pool Bounds Staleness (Global Pool) ---\033[0m\n");
    RUN_TEST(test_pool_bounds_staleness_global);

    printf("\n\033[33m--- Memory Leak From Stale Bounds ---\033[0m\n");
    RUN_TEST(test_memory_leak_from_stale_bounds);

    printf("\n\033[33m--- Pool Grow Does Not Update Bounds ---\033[0m\n");
    RUN_TEST(test_pool_grow_does_not_update_bounds);

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
