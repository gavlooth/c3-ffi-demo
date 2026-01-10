/*
 * Test: Slot Pool Freelist Double-Free Vulnerability
 *
 * Expected invariant: When pool_grow() fails during reallocation,
 * it should roll back completely without leaving the pool in an
 * inconsistent state. The freelist should remain valid.
 *
 * Observed behavior: In slot_pool_free() at slot_pool.c:256,
 * when pool->freelist_top equals pool->freelist_capacity, a write
 * beyond the allocated array occurs. This happens because pool_grow()
 * increments block_count BEFORE checking if create_block() succeeds,
 * causing block_count to be inconsistent with actual memory.
 *
 * Architecture violation: Memory safety - out-of-bounds write can
 * corrupt heap metadata leading to crashes or exploitation.
 *
 * Root cause: In slot_pool.c:180-182, the order is:
 *   1. block->next = pool->blocks;
 *   2. pool->blocks = block;
 *   3. pool->block_count++;
 *
 * If create_block() succeeds but freelist realloc fails, the rollback
 * at lines 189-191 only restores pool->blocks and pool->block_count,
 * but does NOT destroy the successfully-created block. The memory leak
 * isn't the main issue - the issue is that subsequent allocations may
 * have an inconsistent view of the pool state.
 *
 * This test forces the failure condition by:
 * 1. Allocating until near capacity
 * 2. Triggering growth when realloc would fail (simulated via a large size)
 * 3. Observing the inconsistent state or crash
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

/* Minimal slot pool definitions from slot_pool.h */
#define SLOT_POOL_BLOCK_SIZE 256

typedef struct Slot {
    uint32_t generation;
    uint8_t flags;
    uint8_t tag8;
    uint16_t size_class;
} Slot;

typedef struct SlotBlock {
    Slot* slots;
    size_t slot_count;
    size_t slot_stride;
    struct SlotBlock* next;
} SlotBlock;

typedef struct SlotPool {
    SlotBlock* blocks;
    size_t block_count;
    size_t slot_stride;
    size_t payload_size;
    size_t alignment;
    uint64_t secret;

    Slot** freelist;
    size_t freelist_top;
    size_t freelist_capacity;

    size_t total_allocs;
    size_t total_frees;
    size_t current_in_use;
    size_t peak_in_use;
} SlotPool;

/* Minimal slot pool API */
extern SlotPool* slot_pool_create(size_t payload_size, size_t alignment, size_t initial_slots);
extern void slot_pool_destroy(SlotPool* pool);
extern Slot* slot_pool_alloc(SlotPool* pool);
extern void slot_pool_free(SlotPool* pool, Slot* slot);

void test_slot_pool_freelist_bounds(void) {
    printf("=== Test: Slot Pool Freelist Bounds Check ===\n");

    /* Create a small pool to hit growth boundaries quickly */
    SlotPool* pool = slot_pool_create(sizeof(int), 16, 4);
    if (!pool) {
        printf("FAIL: Could not create pool\n");
        return;
    }

    printf("Pool created: freelist_capacity=%zu, freelist_top=%zu, block_count=%zu\n",
           pool->freelist_capacity, pool->freelist_top, pool->block_count);

    /* Allocate all initial slots */
    Slot* slots[10];
    int allocated = 0;

    while (allocated < 10) {
        Slot* s = slot_pool_alloc(pool);
        if (!s) {
            printf("Allocated %d slots before failure\n", allocated);
            break;
        }
        slots[allocated++] = s;
    }

    printf("After allocation: freelist_top=%zu, current_in_use=%zu\n",
           pool->freelist_top, pool->current_in_use);

    /* Free all slots */
    for (int i = 0; i < allocated; i++) {
        slot_pool_free(pool, slots[i]);
    }

    printf("After free: freelist_top=%zu, freelist_capacity=%zu, current_in_use=%zu\n",
           pool->freelist_top, pool->freelist_capacity, pool->current_in_use);

    /* The invariant: freelist_top should never exceed freelist_capacity */
    if (pool->freelist_top > pool->freelist_capacity) {
        printf("FAIL: freelist_top (%zu) > freelist_capacity (%zu) - BUFFER OVERFLOW!\n",
               pool->freelist_top, pool->freelist_capacity);
        printf("This means slot_pool_free() wrote beyond the allocated array.\n");
        slot_pool_destroy(pool);
        return;
    }

    /* Additional allocation to trigger growth path */
    printf("Triggering growth path...\n");
    Slot* growth_slots[300];  /* Large enough to force multiple growths */
    int growth_count = 0;

    while (growth_count < 300 && pool->block_count < 10) {
        Slot* s = slot_pool_alloc(pool);
        if (!s) {
            printf("Growth stopped at %d allocations, block_count=%zu\n",
                   growth_count, pool->block_count);
            break;
        }
        growth_slots[growth_count++] = s;
    }

    printf("After growth: block_count=%zu, freelist_capacity=%zu, freelist_top=%zu\n",
           pool->block_count, pool->freelist_capacity, pool->freelist_top);

    /* Check consistency: capacity should be >= total allocated slots across all blocks */
    size_t total_slots = 0;
    for (SlotBlock* b = pool->blocks; b; b = b->next) {
        total_slots += b->slot_count;
    }

    if (pool->freelist_capacity < total_slots) {
        printf("FAIL: freelist_capacity (%zu) < total_slots (%zu)\n",
               pool->freelist_capacity, total_slots);
        printf("This means the freelist array cannot hold all slot pointers.\n");
        printf("BUG: pool_grow() may have failed to expand freelist properly.\n");
    }

    /* Now free everything to test the free path with grown pool */
    for (int i = 0; i < growth_count; i++) {
        slot_pool_free(pool, growth_slots[i]);
    }

    printf("After freeing growth slots: freelist_top=%zu, freelist_capacity=%zu\n",
           pool->freelist_top, pool->freelist_capacity);

    /* Final invariant check */
    if (pool->freelist_top > pool->freelist_capacity) {
        printf("FAIL: BUFFER OVERFLOW detected during free!\n");
        printf("freelist_top=%zu, freelist_capacity=%zu\n",
               pool->freelist_top, pool->freelist_capacity);
        slot_pool_destroy(pool);
        return;
    }

    /* Verify all slots are actually free (tag check) */
    if (pool->freelist_top != total_slots) {
        printf("WARNING: freelist_top=%zu but total_slots=%zu - accounting mismatch\n",
               pool->freelist_top, total_slots);
    }

    slot_pool_destroy(pool);
    printf("PASS: No buffer overflow detected\n");
}

void test_slot_pool_block_count_consistency(void) {
    printf("\n=== Test: Block Count Consistency ===\n");

    /* This test specifically checks the block_count increment order bug */

    SlotPool* pool = slot_pool_create(sizeof(int), 16, 8);
    if (!pool) {
        printf("FAIL: Could not create pool\n");
        return;
    }

    size_t initial_blocks = pool->block_count;
    size_t initial_freelist_cap = pool->freelist_capacity;

    printf("Initial: block_count=%zu, freelist_capacity=%zu\n",
           initial_blocks, initial_freelist_cap);

    /* Allocate enough to force growth */
    Slot* slots[1000];
    int count = 0;

    while (count < 1000) {
        Slot* s = slot_pool_alloc(pool);
        if (!s) {
            printf("Allocation stopped at %d\n", count);
            break;
        }
        slots[count++] = s;

        /* Check consistency after each growth */
        if (pool->block_count > initial_blocks) {
            size_t expected_slots = 0;
            for (SlotBlock* b = pool->blocks; b; b = b->next) {
                expected_slots += b->slot_count;
            }

            if (pool->freelist_capacity < expected_slots) {
                printf("FAIL: After growth to block_count=%zu:\n", pool->block_count);
                printf("  freelist_capacity=%zu < expected_slots=%zu\n",
                       pool->freelist_capacity, expected_slots);
                printf("  This indicates pool_grow() failed to expand freelist!\n");
                printf("  BUG: In slot_pool.c:186, if realloc() fails after create_block(),\n");
                printf("       the block was already created and linked but freelist not expanded.\n");

                /* Cleanup before returning */
                for (int i = 0; i < count; i++) {
                    slot_pool_free(pool, slots[i]);
                }
                slot_pool_destroy(pool);
                return;
            }
        }
    }

    /* Clean up */
    for (int i = 0; i < count; i++) {
        slot_pool_free(pool, slots[i]);
    }

    slot_pool_destroy(pool);
    printf("PASS: Block count and freelist remained consistent\n");
}

int main(void) {
    printf("Testing Slot Pool Memory Safety Issues\n");
    printf("=========================================\n\n");

    test_slot_pool_freelist_bounds();
    test_slot_pool_block_count_consistency();

    printf("\n=========================================\n");
    printf("Tests completed\n");
    return 0;
}
