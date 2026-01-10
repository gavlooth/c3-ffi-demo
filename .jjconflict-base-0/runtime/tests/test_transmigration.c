/*
 * Transmigration and Isolation Tests
 *
 * Tests for object graph migration between regions and
 * isolation checking for region containment.
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

/* ========== Transmigration Context Tests ========== */

TEST(test_transmigration_new) {
    IRegion* dest = iregion_new_arena(4096);
    ASSERT(dest != NULL);

    TransmigrationContext* ctx = transmigration_new(dest);
    ASSERT(ctx != NULL);

    transmigration_free(ctx);
    iregion_free_all(dest);
}

TEST(test_transmigration_new_null_dest) {
    TransmigrationContext* ctx = transmigration_new(NULL);
    ASSERT(ctx == NULL);
}

TEST(test_transmigration_record_lookup) {
    IRegion* dest = iregion_new_arena(4096);
    TransmigrationContext* ctx = transmigration_new(dest);
    ASSERT(ctx != NULL);

    int source1 = 1, dest1 = 10;
    int source2 = 2, dest2 = 20;

    /* Record mappings */
    transmigration_record(ctx, &source1, &dest1);
    transmigration_record(ctx, &source2, &dest2);

    /* Lookup */
    ASSERT(transmigration_lookup(ctx, &source1) == &dest1);
    ASSERT(transmigration_lookup(ctx, &source2) == &dest2);
    ASSERT(transmigration_lookup(ctx, NULL) == NULL);

    int unknown;
    ASSERT(transmigration_lookup(ctx, &unknown) == NULL);

    transmigration_free(ctx);
    iregion_free_all(dest);
}

TEST(test_transmigration_map_growth) {
    IRegion* dest = iregion_new_arena(4096);
    TransmigrationContext* ctx = transmigration_new(dest);

    /* Record many entries to trigger growth */
    int sources[200];
    int dests[200];
    for (int i = 0; i < 200; i++) {
        sources[i] = i;
        dests[i] = i * 10;
        transmigration_record(ctx, &sources[i], &dests[i]);
    }

    /* Verify all lookups work */
    for (int i = 0; i < 200; i++) {
        ASSERT(transmigration_lookup(ctx, &sources[i]) == &dests[i]);
    }

    transmigration_free(ctx);
    iregion_free_all(dest);
}

TEST(test_transmigrate_null) {
    IRegion* dest = iregion_new_arena(4096);
    TransmigrationContext* ctx = transmigration_new(dest);

    TransmigrationError err;
    void* result = transmigrate(ctx, NULL, &err);

    ASSERT(result == NULL);
    ASSERT(err == TRANSMIGRATE_OK);

    transmigration_free(ctx);
    iregion_free_all(dest);
}

TEST(test_transmigrate_frozen_region) {
    IRegion* dest = iregion_new_arena(4096);
    TransmigrationContext* ctx = transmigration_new(dest);

    /* Freeze the destination */
    iregion_freeze(dest);

    int source = 42;
    TransmigrationError err;
    void* result = transmigrate(ctx, &source, &err);

    ASSERT(result == NULL);
    ASSERT(err == TRANSMIGRATE_ERR_REGION_CLOSED);

    transmigration_free(ctx);
    iregion_free_all(dest);
}

TEST(test_transmigration_register_visitor) {
    IRegion* dest = iregion_new_arena(4096);
    TransmigrationContext* ctx = transmigration_new(dest);

    /* Register a dummy visitor */
    transmigration_register_visitor(ctx, 1, NULL);
    transmigration_register_visitor(ctx, 255, NULL);

    /* Invalid type tags should be ignored */
    transmigration_register_visitor(ctx, -1, NULL);
    transmigration_register_visitor(ctx, 256, NULL);

    transmigration_free(ctx);
    iregion_free_all(dest);
}

/* ========== Isolation Checking Tests ========== */

TEST(test_isolation_null_root) {
    IRegion* region = iregion_new_arena(4096);

    IsolationResult* result = check_isolation(NULL, region);
    ASSERT(result != NULL);
    ASSERT(result->is_isolated == true);
    ASSERT(result->escape_count == 0);

    isolation_result_free(result);
    iregion_free_all(region);
}

TEST(test_isolation_null_region) {
    int data = 42;

    IsolationResult* result = check_isolation(&data, NULL);
    ASSERT(result != NULL);
    ASSERT(result->is_isolated == false);
    ASSERT(result->escape_count == 1);

    isolation_result_free(result);
}

TEST(test_isolation_with_region) {
    IRegion* region = iregion_new_arena(4096);
    int* data = (int*)iregion_alloc(region, sizeof(int), sizeof(int));
    *data = 42;

    IsolationResult* result = check_isolation(data, region);
    ASSERT(result != NULL);
    ASSERT(result->is_isolated == true);

    isolation_result_free(result);
    iregion_free_all(region);
}

/* ========== Region-Bound Reference Tests ========== */

TEST(test_region_bound_ref_new) {
    IRegion* region = iregion_new_arena(4096);
    int data = 42;

    RegionBoundRef* ref = region_bound_ref_new(&data, region);
    ASSERT(ref != NULL);
    ASSERT(ref->target == &data);
    ASSERT(ref->region == region);

    region_bound_ref_free(ref);
    iregion_free_all(region);
}

TEST(test_region_bound_ref_null_region) {
    int data = 42;
    RegionBoundRef* ref = region_bound_ref_new(&data, NULL);
    ASSERT(ref == NULL);
}

TEST(test_region_bound_ref_deref) {
    IRegion* region = iregion_new_arena(4096);
    int data = 42;

    RegionBoundRef* ref = region_bound_ref_new(&data, region);
    ASSERT(ref != NULL);

    void* target = region_bound_ref_deref(ref);
    ASSERT(target == &data);
    ASSERT(*(int*)target == 42);

    region_bound_ref_free(ref);
    iregion_free_all(region);
}

TEST(test_region_bound_ref_valid) {
    IRegion* region = iregion_new_arena(4096);
    int data = 42;

    RegionBoundRef* ref = region_bound_ref_new(&data, region);
    ASSERT(region_bound_ref_is_valid(ref) == true);

    /* Freeze region - ref should become invalid */
    iregion_freeze(region);
    ASSERT(region_bound_ref_is_valid(ref) == false);

    /* Deref should return NULL */
    ASSERT(region_bound_ref_deref(ref) == NULL);

    region_bound_ref_free(ref);
    iregion_free_all(region);
}

TEST(test_region_bound_ref_null) {
    ASSERT(region_bound_ref_is_valid(NULL) == false);
    ASSERT(region_bound_ref_deref(NULL) == NULL);
}

/* ========== Stress Tests ========== */

TEST(test_transmigration_stress) {
    IRegion* dest = iregion_new_arena(8192);
    TransmigrationContext* ctx = transmigration_new(dest);

    /* Record many mappings */
    int sources[1000];
    int dests[1000];
    for (int i = 0; i < 1000; i++) {
        sources[i] = i;
        dests[i] = i * 2;
        transmigration_record(ctx, &sources[i], &dests[i]);
    }

    /* Lookup all */
    for (int i = 0; i < 1000; i++) {
        ASSERT(transmigration_lookup(ctx, &sources[i]) == &dests[i]);
    }

    transmigration_free(ctx);
    iregion_free_all(dest);
}

TEST(test_region_bound_ref_stress) {
    IRegion* region = iregion_new_arena(8192);
    RegionBoundRef* refs[100];
    int data[100];

    for (int i = 0; i < 100; i++) {
        data[i] = i;
        refs[i] = region_bound_ref_new(&data[i], region);
        ASSERT(refs[i] != NULL);
    }

    /* All should be valid */
    for (int i = 0; i < 100; i++) {
        ASSERT(region_bound_ref_is_valid(refs[i]));
        ASSERT(*(int*)region_bound_ref_deref(refs[i]) == i);
    }

    /* Cleanup */
    for (int i = 0; i < 100; i++) {
        region_bound_ref_free(refs[i]);
    }
    iregion_free_all(region);
}

/* ========== Main ========== */

int main(void) {
    printf("\n\033[33m=== Transmigration/Isolation Tests ===\033[0m\n");

    printf("\n\033[33m--- Transmigration Context ---\033[0m\n");
    RUN_TEST(test_transmigration_new);
    RUN_TEST(test_transmigration_new_null_dest);
    RUN_TEST(test_transmigration_record_lookup);
    RUN_TEST(test_transmigration_map_growth);
    RUN_TEST(test_transmigrate_null);
    RUN_TEST(test_transmigrate_frozen_region);
    RUN_TEST(test_transmigration_register_visitor);

    printf("\n\033[33m--- Isolation Checking ---\033[0m\n");
    RUN_TEST(test_isolation_null_root);
    RUN_TEST(test_isolation_null_region);
    RUN_TEST(test_isolation_with_region);

    printf("\n\033[33m--- Region-Bound References ---\033[0m\n");
    RUN_TEST(test_region_bound_ref_new);
    RUN_TEST(test_region_bound_ref_null_region);
    RUN_TEST(test_region_bound_ref_deref);
    RUN_TEST(test_region_bound_ref_valid);
    RUN_TEST(test_region_bound_ref_null);

    printf("\n\033[33m--- Stress Tests ---\033[0m\n");
    RUN_TEST(test_transmigration_stress);
    RUN_TEST(test_region_bound_ref_stress);

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
