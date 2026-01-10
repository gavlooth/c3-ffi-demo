/*
 * Weak Reference Control Block Tests
 *
 * Tests for O(1) weak reference invalidation using control blocks.
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

/* Test data */
static int destructor_call_count = 0;
static void test_destructor(void* data) {
    (void)data;
    destructor_call_count++;
}

/* ========== Control Block Tests ========== */

TEST(test_weak_cb_new) {
    int data = 42;
    WeakControlBlock* cb = weak_cb_new(&data, NULL);
    ASSERT(cb != NULL);
    ASSERT(weak_cb_is_valid(cb));
    ASSERT(weak_cb_get_target(cb) == &data);

    weak_cb_invalidate(cb);
}

TEST(test_weak_cb_invalidate) {
    int data = 42;
    WeakControlBlock* cb = weak_cb_new(&data, NULL);
    ASSERT(weak_cb_is_valid(cb));

    weak_cb_invalidate(cb);

    /* After invalidation, should not be valid */
    /* Note: cb is freed after invalidation if no weak refs */
}

TEST(test_weak_cb_with_destructor) {
    destructor_call_count = 0;
    int data = 42;
    WeakControlBlock* cb = weak_cb_new(&data, test_destructor);
    ASSERT(cb != NULL);

    weak_cb_invalidate(cb);
    ASSERT(destructor_call_count == 1);
}

TEST(test_weak_cb_ref_counting) {
    int data = 42;
    WeakControlBlock* cb = weak_cb_new(&data, NULL);

    /* Add weak refs */
    weak_cb_inc_ref(cb);
    weak_cb_inc_ref(cb);
    ASSERT(weak_cb_is_valid(cb));

    /* Remove weak refs */
    weak_cb_dec_ref(cb);
    ASSERT(weak_cb_is_valid(cb));

    /* Invalidate - cb should not be freed yet due to remaining ref */
    weak_cb_invalidate(cb);

    /* Final dec_ref frees the cb */
    weak_cb_dec_ref(cb);
}

/* ========== Weak Handle Tests ========== */

TEST(test_weak_handle_new) {
    int data = 42;
    WeakControlBlock* cb = weak_cb_new(&data, NULL);
    WeakHandle* h = weak_handle_new(cb);

    ASSERT(h != NULL);
    ASSERT(weak_handle_is_valid(h));
    ASSERT(weak_handle_lock(h) == &data);

    weak_handle_free(h);
    weak_cb_invalidate(cb);
}

TEST(test_weak_handle_clone) {
    int data = 42;
    WeakControlBlock* cb = weak_cb_new(&data, NULL);
    WeakHandle* h1 = weak_handle_new(cb);
    WeakHandle* h2 = weak_handle_clone(h1);

    ASSERT(h2 != NULL);
    ASSERT(weak_handle_is_valid(h2));
    ASSERT(weak_handle_lock(h2) == &data);

    weak_handle_free(h1);
    weak_handle_free(h2);
    weak_cb_invalidate(cb);
}

TEST(test_weak_handle_invalidation) {
    int data = 42;
    WeakControlBlock* cb = weak_cb_new(&data, NULL);
    WeakHandle* h = weak_handle_new(cb);

    ASSERT(weak_handle_is_valid(h));

    /* Invalidate the control block */
    weak_cb_invalidate(cb);

    /* Handle should now be invalid */
    ASSERT(!weak_handle_is_valid(h));
    ASSERT(weak_handle_lock(h) == NULL);

    weak_handle_free(h);
}

TEST(test_weak_handle_multiple) {
    int data = 42;
    WeakControlBlock* cb = weak_cb_new(&data, NULL);

    WeakHandle* handles[10];
    for (int i = 0; i < 10; i++) {
        handles[i] = weak_handle_new(cb);
        ASSERT(handles[i] != NULL);
        ASSERT(weak_handle_is_valid(handles[i]));
    }

    /* Invalidate control block */
    weak_cb_invalidate(cb);

    /* All handles should be invalid */
    for (int i = 0; i < 10; i++) {
        ASSERT(!weak_handle_is_valid(handles[i]));
        weak_handle_free(handles[i]);
    }
}

/* ========== Weakable Object Tests ========== */

TEST(test_weakable_object_new) {
    int* data = malloc(sizeof(int));
    *data = 42;

    WeakableObject wo = weak_object_new(data, free);
    ASSERT(wo.object == data);
    ASSERT(wo.cb != NULL);
    ASSERT(weak_cb_is_valid(wo.cb));

    weak_object_free(&wo);
    ASSERT(wo.object == NULL);
    ASSERT(wo.cb == NULL);
}

TEST(test_weakable_object_with_handles) {
    int* data = malloc(sizeof(int));
    *data = 123;

    WeakableObject wo = weak_object_new(data, free);
    WeakHandle* h = weak_handle_new(wo.cb);

    ASSERT(weak_handle_lock(h) == data);

    /* Free the weakable object */
    weak_object_free(&wo);

    /* Handle should now be invalid */
    ASSERT(!weak_handle_is_valid(h));
    ASSERT(weak_handle_lock(h) == NULL);

    weak_handle_free(h);
}

/* ========== Weak Reference Table Tests ========== */

TEST(test_weak_table_new) {
    WeakRefTable* table = weak_table_new(100);
    ASSERT(table != NULL);
    weak_table_free(table);
}

TEST(test_weak_table_register) {
    WeakRefTable* table = weak_table_new(100);
    int data = 42;

    uint32_t index = weak_table_register(table, &data, NULL);
    ASSERT(index != 0xFFFFFFFF);

    WeakHandle* h = weak_table_get_handle(table, index);
    ASSERT(h != NULL);
    ASSERT(weak_handle_lock(h) == &data);

    weak_handle_free(h);
    weak_table_free(table);
}

TEST(test_weak_table_invalidate) {
    WeakRefTable* table = weak_table_new(100);
    int data = 42;

    uint32_t index = weak_table_register(table, &data, NULL);
    WeakHandle* h = weak_table_get_handle(table, index);

    ASSERT(weak_handle_is_valid(h));

    weak_table_invalidate(table, index);

    ASSERT(!weak_handle_is_valid(h));

    weak_handle_free(h);
    weak_table_free(table);
}

TEST(test_weak_table_lock) {
    WeakRefTable* table = weak_table_new(100);
    int data = 42;

    uint32_t index = weak_table_register(table, &data, NULL);
    WeakHandle* h = weak_table_get_handle(table, index);
    uint64_t gen = h->generation;

    /* Lock with correct generation */
    void* target = weak_table_lock(table, index, gen);
    ASSERT(target == &data);

    /* Lock with wrong generation should fail */
    void* bad_target = weak_table_lock(table, index, gen + 1);
    ASSERT(bad_target == NULL);

    weak_handle_free(h);
    weak_table_free(table);
}

TEST(test_weak_table_reuse) {
    WeakRefTable* table = weak_table_new(10);
    int data1 = 1;
    int data2 = 2;

    /* Register and invalidate */
    uint32_t index1 = weak_table_register(table, &data1, NULL);
    weak_table_invalidate(table, index1);

    /* Register again - should reuse slot */
    uint32_t index2 = weak_table_register(table, &data2, NULL);
    ASSERT(index2 == index1);  /* Slot reused */

    WeakHandle* h = weak_table_get_handle(table, index2);
    ASSERT(weak_handle_lock(h) == &data2);

    weak_handle_free(h);
    weak_table_free(table);
}

TEST(test_weak_table_full) {
    WeakRefTable* table = weak_table_new(5);

    /* Fill the table */
    uint32_t indices[5];
    for (int i = 0; i < 5; i++) {
        indices[i] = weak_table_register(table, (void*)(intptr_t)(i + 1), NULL);
        ASSERT(indices[i] != 0xFFFFFFFF);
    }

    /* Should fail - table full */
    uint32_t overflow = weak_table_register(table, (void*)100, NULL);
    ASSERT(overflow == 0xFFFFFFFF);

    weak_table_free(table);
}

TEST(test_weak_table_generation_aba) {
    WeakRefTable* table = weak_table_new(10);
    int data1 = 1;
    int data2 = 2;

    /* Register object */
    uint32_t index = weak_table_register(table, &data1, NULL);
    WeakHandle* h1 = weak_table_get_handle(table, index);
    uint64_t gen1 = h1->generation;

    /* Invalidate - slot won't be reused yet because h1 still holds it */
    weak_table_invalidate(table, index);

    /* Old handle should not work - target is dead */
    ASSERT(!weak_handle_is_valid(h1));
    void* target = weak_table_lock(table, index, gen1);
    ASSERT(target == NULL);  /* Target is dead */

    /* Free the old handle - now slot can be reused */
    weak_handle_free(h1);

    /* Register new object - may or may not be same slot, but should work */
    uint32_t index2 = weak_table_register(table, &data2, NULL);
    ASSERT(index2 != 0xFFFFFFFF);

    /* If same slot, old generation should not work */
    if (index2 == index) {
        void* bad_target = weak_table_lock(table, index2, gen1);
        ASSERT(bad_target == NULL);  /* ABA protection - old gen doesn't work */
    }

    /* New handle should work */
    WeakHandle* h2 = weak_table_get_handle(table, index2);
    ASSERT(weak_handle_lock(h2) == &data2);

    weak_handle_free(h2);
    weak_table_free(table);
}

TEST(test_weak_table_destructor) {
    destructor_call_count = 0;
    WeakRefTable* table = weak_table_new(10);

    int data = 42;
    uint32_t index = weak_table_register(table, &data, test_destructor);

    ASSERT(destructor_call_count == 0);

    weak_table_invalidate(table, index);

    ASSERT(destructor_call_count == 1);

    weak_table_free(table);
}

/* ========== Global Table Tests ========== */

TEST(test_global_weak_table) {
    WeakRefTable* table = weak_table_global();
    ASSERT(table != NULL);

    /* Second call should return same table */
    WeakRefTable* table2 = weak_table_global();
    ASSERT(table == table2);
}

/* ========== Stress Tests ========== */

TEST(test_weak_cb_stress) {
    /* Create many control blocks and handles */
    WeakControlBlock* cbs[100];
    WeakHandle* handles[100];
    int data[100];

    for (int i = 0; i < 100; i++) {
        data[i] = i;
        cbs[i] = weak_cb_new(&data[i], NULL);
        handles[i] = weak_handle_new(cbs[i]);
    }

    /* Verify all valid */
    for (int i = 0; i < 100; i++) {
        ASSERT(weak_handle_is_valid(handles[i]));
        ASSERT(*(int*)weak_handle_lock(handles[i]) == i);
    }

    /* Invalidate odd ones */
    for (int i = 1; i < 100; i += 2) {
        weak_cb_invalidate(cbs[i]);
    }

    /* Check validity */
    for (int i = 0; i < 100; i++) {
        if (i % 2 == 0) {
            ASSERT(weak_handle_is_valid(handles[i]));
        } else {
            ASSERT(!weak_handle_is_valid(handles[i]));
        }
    }

    /* Cleanup */
    for (int i = 0; i < 100; i++) {
        weak_handle_free(handles[i]);
        if (i % 2 == 0) {
            weak_cb_invalidate(cbs[i]);
        }
    }
}

TEST(test_weak_table_stress) {
    WeakRefTable* table = weak_table_new(1000);
    uint32_t indices[500];
    int data[500];

    /* Register many objects */
    for (int i = 0; i < 500; i++) {
        data[i] = i;
        indices[i] = weak_table_register(table, &data[i], NULL);
        ASSERT(indices[i] != 0xFFFFFFFF);
    }

    /* Invalidate every other */
    for (int i = 0; i < 500; i += 2) {
        weak_table_invalidate(table, indices[i]);
    }

    /* Register more - should reuse slots */
    for (int i = 0; i < 250; i++) {
        uint32_t new_index = weak_table_register(table, &data[i], NULL);
        ASSERT(new_index != 0xFFFFFFFF);
    }

    weak_table_free(table);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Weak Reference Control Block Tests ===\033[0m\n");

    printf("\n\033[33m--- Control Block ---\033[0m\n");
    RUN_TEST(test_weak_cb_new);
    RUN_TEST(test_weak_cb_invalidate);
    RUN_TEST(test_weak_cb_with_destructor);
    RUN_TEST(test_weak_cb_ref_counting);

    printf("\n\033[33m--- Weak Handle ---\033[0m\n");
    RUN_TEST(test_weak_handle_new);
    RUN_TEST(test_weak_handle_clone);
    RUN_TEST(test_weak_handle_invalidation);
    RUN_TEST(test_weak_handle_multiple);

    printf("\n\033[33m--- Weakable Object ---\033[0m\n");
    RUN_TEST(test_weakable_object_new);
    RUN_TEST(test_weakable_object_with_handles);

    printf("\n\033[33m--- Weak Reference Table ---\033[0m\n");
    RUN_TEST(test_weak_table_new);
    RUN_TEST(test_weak_table_register);
    RUN_TEST(test_weak_table_invalidate);
    RUN_TEST(test_weak_table_lock);
    RUN_TEST(test_weak_table_reuse);
    RUN_TEST(test_weak_table_full);
    RUN_TEST(test_weak_table_generation_aba);
    RUN_TEST(test_weak_table_destructor);

    printf("\n\033[33m--- Global Table ---\033[0m\n");
    RUN_TEST(test_global_weak_table);

    printf("\n\033[33m--- Stress Tests ---\033[0m\n");
    RUN_TEST(test_weak_cb_stress);
    RUN_TEST(test_weak_table_stress);

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
