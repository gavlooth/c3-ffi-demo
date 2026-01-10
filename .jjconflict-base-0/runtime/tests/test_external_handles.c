/*
 * External Handle Indexing Tests
 *
 * Tests for stable integer handles for FFI and deterministic replay.
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
static void* last_destructed = NULL;

static void test_destructor(void* data) {
    destructor_call_count++;
    last_destructed = data;
}

/* ========== Basic Table Tests ========== */

TEST(test_table_new) {
    ExternalHandleTable* table = external_table_new(100);
    ASSERT(table != NULL);
    ASSERT(external_table_count(table) == 0);
    external_table_free(table);
}

TEST(test_table_new_zero_capacity) {
    /* Zero capacity uses default capacity */
    ExternalHandleTable* table = external_table_new(0);
    ASSERT(table != NULL);
    external_table_free(table);
}

TEST(test_table_free_null) {
    /* Should not crash */
    external_table_free(NULL);
}

/* ========== Handle Creation Tests ========== */

TEST(test_handle_create) {
    ExternalHandleTable* table = external_table_new(100);
    int data = 42;

    ExternalHandle h = external_handle_create(table, &data, NULL);
    ASSERT(h != EXTERNAL_HANDLE_INVALID);
    ASSERT(external_table_count(table) == 1);

    external_table_free(table);
}

TEST(test_handle_create_null_table) {
    int data = 42;
    ExternalHandle h = external_handle_create(NULL, &data, NULL);
    ASSERT(h == EXTERNAL_HANDLE_INVALID);
}

TEST(test_handle_create_multiple) {
    ExternalHandleTable* table = external_table_new(100);
    int data[10];
    ExternalHandle handles[10];

    for (int i = 0; i < 10; i++) {
        data[i] = i * 10;
        handles[i] = external_handle_create(table, &data[i], NULL);
        ASSERT(handles[i] != EXTERNAL_HANDLE_INVALID);
    }

    ASSERT(external_table_count(table) == 10);

    /* All handles should be unique */
    for (int i = 0; i < 10; i++) {
        for (int j = i + 1; j < 10; j++) {
            ASSERT(handles[i] != handles[j]);
        }
    }

    external_table_free(table);
}

TEST(test_handle_get) {
    ExternalHandleTable* table = external_table_new(100);
    int data = 42;

    ExternalHandle h = external_handle_create(table, &data, NULL);
    void* result = external_handle_get(table, h);

    ASSERT(result == &data);
    ASSERT(*(int*)result == 42);

    external_table_free(table);
}

TEST(test_handle_get_invalid) {
    ExternalHandleTable* table = external_table_new(100);

    void* result = external_handle_get(table, EXTERNAL_HANDLE_INVALID);
    ASSERT(result == NULL);

    /* Non-existent handle */
    ExternalHandle fake = EXTERNAL_HANDLE_MAKE(50, 1);
    result = external_handle_get(table, fake);
    ASSERT(result == NULL);

    external_table_free(table);
}

TEST(test_handle_is_valid) {
    ExternalHandleTable* table = external_table_new(100);
    int data = 42;

    ExternalHandle h = external_handle_create(table, &data, NULL);
    ASSERT(external_handle_is_valid(table, h));
    ASSERT(!external_handle_is_valid(table, EXTERNAL_HANDLE_INVALID));

    external_table_free(table);
}

/* ========== Handle Release Tests ========== */

TEST(test_handle_release) {
    ExternalHandleTable* table = external_table_new(100);
    int data = 42;

    ExternalHandle h = external_handle_create(table, &data, NULL);
    ASSERT(external_table_count(table) == 1);

    external_handle_release(table, h);
    ASSERT(external_table_count(table) == 0);

    /* Handle should no longer be valid */
    ASSERT(!external_handle_is_valid(table, h));
    ASSERT(external_handle_get(table, h) == NULL);

    external_table_free(table);
}

TEST(test_handle_release_with_destructor) {
    destructor_call_count = 0;
    last_destructed = NULL;

    ExternalHandleTable* table = external_table_new(100);
    int data = 42;

    ExternalHandle h = external_handle_create(table, &data, test_destructor);
    ASSERT(destructor_call_count == 0);

    external_handle_release(table, h);
    ASSERT(destructor_call_count == 1);
    ASSERT(last_destructed == &data);

    external_table_free(table);
}

TEST(test_handle_release_invalid) {
    ExternalHandleTable* table = external_table_new(100);

    /* Releasing invalid handle should be safe (no-op) */
    external_handle_release(table, EXTERNAL_HANDLE_INVALID);
    /* Just verify it doesn't crash */

    external_table_free(table);
}

TEST(test_handle_double_release) {
    ExternalHandleTable* table = external_table_new(100);
    int data = 42;

    ExternalHandle h = external_handle_create(table, &data, NULL);
    ASSERT(external_handle_is_valid(table, h));

    external_handle_release(table, h);
    ASSERT(!external_handle_is_valid(table, h));

    /* Second release should be safe (no-op) */
    external_handle_release(table, h);
    /* Just verify it doesn't crash or change state */
    ASSERT(!external_handle_is_valid(table, h));

    external_table_free(table);
}

/* ========== Generation/ABA Protection Tests ========== */

TEST(test_handle_generation_protection) {
    ExternalHandleTable* table = external_table_new(10);
    int data1 = 1;
    int data2 = 2;

    /* Create and release */
    ExternalHandle h1 = external_handle_create(table, &data1, NULL);
    uint32_t gen1 = EXTERNAL_HANDLE_GEN(h1);
    external_handle_release(table, h1);

    /* Create new handle - may reuse slot */
    ExternalHandle h2 = external_handle_create(table, &data2, NULL);

    /* Old handle should NOT work even if slot reused */
    ASSERT(!external_handle_is_valid(table, h1));
    ASSERT(external_handle_get(table, h1) == NULL);

    /* New handle should work */
    ASSERT(external_handle_is_valid(table, h2));
    ASSERT(external_handle_get(table, h2) == &data2);

    external_table_free(table);
}

TEST(test_handle_index_extraction) {
    ExternalHandle h = EXTERNAL_HANDLE_MAKE(42, 7);
    ASSERT(EXTERNAL_HANDLE_INDEX(h) == 42);
    ASSERT(EXTERNAL_HANDLE_GEN(h) == 7);
}

/* ========== Deterministic Mode Tests ========== */

TEST(test_deterministic_mode) {
    ExternalHandleTable* table = external_table_new(100);
    external_table_set_deterministic(table, true);

    int data[5];
    ExternalHandle handles[5];

    for (int i = 0; i < 5; i++) {
        data[i] = i;
        handles[i] = external_handle_create(table, &data[i], NULL);
        ASSERT(handles[i] != EXTERNAL_HANDLE_INVALID);
    }

    /* In deterministic mode, handles should be sequential starting at 1 */
    /* (index 0 is reserved to keep handle 0 invalid) */
    for (int i = 0; i < 5; i++) {
        ASSERT(EXTERNAL_HANDLE_INDEX(handles[i]) == (uint32_t)(i + 1));
    }

    external_table_free(table);
}

TEST(test_deterministic_no_reuse) {
    ExternalHandleTable* table = external_table_new(100);
    external_table_set_deterministic(table, true);

    int data1 = 1, data2 = 2, data3 = 3;

    ExternalHandle h1 = external_handle_create(table, &data1, NULL);
    ExternalHandle h2 = external_handle_create(table, &data2, NULL);
    external_handle_release(table, h1);
    ExternalHandle h3 = external_handle_create(table, &data3, NULL);

    /* In deterministic mode, slots are sequential starting at 1 */
    /* Released slot should NOT be reused */
    ASSERT(EXTERNAL_HANDLE_INDEX(h1) == 1);
    ASSERT(EXTERNAL_HANDLE_INDEX(h2) == 2);
    ASSERT(EXTERNAL_HANDLE_INDEX(h3) == 3);

    external_table_free(table);
}

/* ========== Table Clear Tests ========== */

TEST(test_table_clear) {
    destructor_call_count = 0;
    ExternalHandleTable* table = external_table_new(100);
    int data[5];

    for (int i = 0; i < 5; i++) {
        data[i] = i;
        external_handle_create(table, &data[i], test_destructor);
    }

    ASSERT(external_table_count(table) == 5);
    ASSERT(destructor_call_count == 0);

    external_table_clear(table);

    ASSERT(external_table_count(table) == 0);
    ASSERT(destructor_call_count == 5);

    external_table_free(table);
}

/* ========== Table Full Tests ========== */

TEST(test_table_full) {
    ExternalHandleTable* table = external_table_new(5);
    int data[6];

    /* Fill the table */
    for (int i = 0; i < 5; i++) {
        data[i] = i;
        ExternalHandle h = external_handle_create(table, &data[i], NULL);
        ASSERT(h != EXTERNAL_HANDLE_INVALID);
    }

    ASSERT(external_table_count(table) == 5);

    /* Table full - should fail */
    data[5] = 5;
    ExternalHandle overflow = external_handle_create(table, &data[5], NULL);
    ASSERT(overflow == EXTERNAL_HANDLE_INVALID);

    external_table_free(table);
}

TEST(test_table_full_release_reuse) {
    ExternalHandleTable* table = external_table_new(5);
    int data[6];
    ExternalHandle handles[5];

    /* Fill the table */
    for (int i = 0; i < 5; i++) {
        data[i] = i;
        handles[i] = external_handle_create(table, &data[i], NULL);
    }

    /* Release one */
    external_handle_release(table, handles[2]);

    /* Should be able to create one more */
    data[5] = 5;
    ExternalHandle h = external_handle_create(table, &data[5], NULL);
    ASSERT(h != EXTERNAL_HANDLE_INVALID);
    ASSERT(external_handle_get(table, h) == &data[5]);

    external_table_free(table);
}

/* ========== Iteration Tests ========== */

static int iterate_count;
static void* iterate_objects[100];

static void iterate_callback(ExternalHandle handle, void* object, void* ctx) {
    (void)handle;
    (void)ctx;
    if (iterate_count < 100) {
        iterate_objects[iterate_count++] = object;
    }
}

TEST(test_table_iterate) {
    ExternalHandleTable* table = external_table_new(100);
    int data[5];

    for (int i = 0; i < 5; i++) {
        data[i] = i;
        external_handle_create(table, &data[i], NULL);
    }

    iterate_count = 0;
    external_table_iterate(table, iterate_callback, NULL);

    ASSERT(iterate_count == 5);

    /* All objects should be in the callback results */
    for (int i = 0; i < 5; i++) {
        bool found = false;
        for (int j = 0; j < iterate_count; j++) {
            if (iterate_objects[j] == &data[i]) {
                found = true;
                break;
            }
        }
        ASSERT(found);
    }

    external_table_free(table);
}

TEST(test_table_iterate_empty) {
    ExternalHandleTable* table = external_table_new(100);

    iterate_count = 0;
    external_table_iterate(table, iterate_callback, NULL);

    ASSERT(iterate_count == 0);

    external_table_free(table);
}

/* ========== Global Table Tests ========== */

TEST(test_global_table) {
    ExternalHandleTable* table = external_table_global();
    ASSERT(table != NULL);

    /* Should return same instance */
    ExternalHandleTable* table2 = external_table_global();
    ASSERT(table == table2);
}

/* ========== FFI Convenience Tests ========== */

TEST(test_ffi_obj_to_handle) {
    int data = 42;
    ExternalHandle h = ffi_obj_to_handle(&data);
    ASSERT(h != EXTERNAL_HANDLE_INVALID);

    void* result = ffi_handle_to_obj(h);
    ASSERT(result == &data);

    ffi_release_handle(h);
    ASSERT(ffi_handle_to_obj(h) == NULL);
}

TEST(test_ffi_roundtrip) {
    /* Test multiple roundtrips */
    int data[5];
    ExternalHandle handles[5];

    for (int i = 0; i < 5; i++) {
        data[i] = i * 100;
        handles[i] = ffi_obj_to_handle(&data[i]);
        ASSERT(handles[i] != EXTERNAL_HANDLE_INVALID);
    }

    for (int i = 0; i < 5; i++) {
        void* obj = ffi_handle_to_obj(handles[i]);
        ASSERT(obj == &data[i]);
        ASSERT(*(int*)obj == i * 100);
    }

    for (int i = 0; i < 5; i++) {
        ffi_release_handle(handles[i]);
    }
}

/* ========== Stress Tests ========== */

TEST(test_stress_create_release) {
    ExternalHandleTable* table = external_table_new(1000);
    ExternalHandle handles[500];
    int data[500];

    /* Create many handles */
    for (int i = 0; i < 500; i++) {
        data[i] = i;
        handles[i] = external_handle_create(table, &data[i], NULL);
        ASSERT(handles[i] != EXTERNAL_HANDLE_INVALID);
    }

    ASSERT(external_table_count(table) == 500);

    /* Release every other */
    for (int i = 0; i < 500; i += 2) {
        external_handle_release(table, handles[i]);
    }

    ASSERT(external_table_count(table) == 250);

    /* Remaining should still be valid */
    for (int i = 1; i < 500; i += 2) {
        ASSERT(external_handle_is_valid(table, handles[i]));
        ASSERT(external_handle_get(table, handles[i]) == &data[i]);
    }

    external_table_free(table);
}

TEST(test_stress_generation_wrap) {
    ExternalHandleTable* table = external_table_new(1);
    int data = 42;

    /* Create and release many times to exercise generation counter */
    for (int i = 0; i < 100; i++) {
        ExternalHandle h = external_handle_create(table, &data, NULL);
        ASSERT(h != EXTERNAL_HANDLE_INVALID);
        ASSERT(external_handle_get(table, h) == &data);
        external_handle_release(table, h);
    }

    external_table_free(table);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== External Handle Indexing Tests ===\033[0m\n");

    printf("\n\033[33m--- Basic Table ---\033[0m\n");
    RUN_TEST(test_table_new);
    RUN_TEST(test_table_new_zero_capacity);
    RUN_TEST(test_table_free_null);

    printf("\n\033[33m--- Handle Creation ---\033[0m\n");
    RUN_TEST(test_handle_create);
    RUN_TEST(test_handle_create_null_table);
    RUN_TEST(test_handle_create_multiple);
    RUN_TEST(test_handle_get);
    RUN_TEST(test_handle_get_invalid);
    RUN_TEST(test_handle_is_valid);

    printf("\n\033[33m--- Handle Release ---\033[0m\n");
    RUN_TEST(test_handle_release);
    RUN_TEST(test_handle_release_with_destructor);
    RUN_TEST(test_handle_release_invalid);
    RUN_TEST(test_handle_double_release);

    printf("\n\033[33m--- Generation/ABA Protection ---\033[0m\n");
    RUN_TEST(test_handle_generation_protection);
    RUN_TEST(test_handle_index_extraction);

    printf("\n\033[33m--- Deterministic Mode ---\033[0m\n");
    RUN_TEST(test_deterministic_mode);
    RUN_TEST(test_deterministic_no_reuse);

    printf("\n\033[33m--- Table Clear ---\033[0m\n");
    RUN_TEST(test_table_clear);

    printf("\n\033[33m--- Table Capacity ---\033[0m\n");
    RUN_TEST(test_table_full);
    RUN_TEST(test_table_full_release_reuse);

    printf("\n\033[33m--- Iteration ---\033[0m\n");
    RUN_TEST(test_table_iterate);
    RUN_TEST(test_table_iterate_empty);

    printf("\n\033[33m--- Global Table ---\033[0m\n");
    RUN_TEST(test_global_table);

    printf("\n\033[33m--- FFI Convenience ---\033[0m\n");
    RUN_TEST(test_ffi_obj_to_handle);
    RUN_TEST(test_ffi_roundtrip);

    printf("\n\033[33m--- Stress Tests ---\033[0m\n");
    RUN_TEST(test_stress_create_release);
    RUN_TEST(test_stress_generation_wrap);

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
