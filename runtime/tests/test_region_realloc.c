/*
 * test_region_realloc.c - Tests for region-aware reallocation (Issue 10 P1)
 *
 * Verifies that region_realloc:
 * 1. Preserves region membership
 * 2. Copies data correctly
 * 3. Handles edge cases (NULL, shrink, same size)
 * 4. Updates accounting correctly
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "memory/region_core.h"
#include "omni.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { printf("  Testing: %s... ", #name); } while(0)

#define PASS() \
    do { printf("PASS\n"); tests_passed++; } while(0)

#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* Test basic reallocation */
static void test_basic_realloc(void) {
    TEST(basic_realloc);

    Region* r = region_create();
    assert(r != NULL);

    /* Allocate initial buffer */
    int* data = (int*)region_alloc(r, 4 * sizeof(int));
    assert(data != NULL);

    /* Fill with test data */
    data[0] = 10;
    data[1] = 20;
    data[2] = 30;
    data[3] = 40;

    /* Realloc to larger size */
    int* new_data = (int*)region_realloc(r, data, 4 * sizeof(int), 8 * sizeof(int));

    if (new_data == NULL) {
        FAIL("region_realloc returned NULL");
        region_exit(r);
        return;
    }

    /* Verify data was copied */
    if (new_data[0] != 10 || new_data[1] != 20 || new_data[2] != 30 || new_data[3] != 40) {
        FAIL("data not preserved after realloc");
        region_exit(r);
        return;
    }

    /* Can use new space */
    new_data[4] = 50;
    new_data[5] = 60;
    new_data[6] = 70;
    new_data[7] = 80;

    region_exit(r);
    PASS();
}

/* Test shrink is no-op */
static void test_shrink_noop(void) {
    TEST(shrink_noop);

    Region* r = region_create();
    assert(r != NULL);

    /* Allocate initial buffer */
    int* data = (int*)region_alloc(r, 8 * sizeof(int));
    assert(data != NULL);

    /* Fill with test data */
    for (int i = 0; i < 8; i++) {
        data[i] = i * 10;
    }

    /* Shrink - should return same pointer */
    int* same = (int*)region_realloc(r, data, 8 * sizeof(int), 4 * sizeof(int));

    if (same != data) {
        FAIL("shrink should return same pointer");
        region_exit(r);
        return;
    }

    region_exit(r);
    PASS();
}

/* Test same size is no-op */
static void test_same_size_noop(void) {
    TEST(same_size_noop);

    Region* r = region_create();
    assert(r != NULL);

    int* data = (int*)region_alloc(r, 4 * sizeof(int));
    assert(data != NULL);

    /* Same size - should return same pointer */
    int* same = (int*)region_realloc(r, data, 4 * sizeof(int), 4 * sizeof(int));

    if (same != data) {
        FAIL("same size should return same pointer");
        region_exit(r);
        return;
    }

    region_exit(r);
    PASS();
}

/* Test NULL oldptr behaves like alloc */
static void test_null_oldptr(void) {
    TEST(null_oldptr);

    Region* r = region_create();
    assert(r != NULL);

    /* NULL oldptr, 0 oldsz - should allocate fresh */
    int* data = (int*)region_realloc(r, NULL, 0, 4 * sizeof(int));

    if (data == NULL) {
        FAIL("realloc with NULL should allocate");
        region_exit(r);
        return;
    }

    /* Should be usable */
    data[0] = 100;
    data[1] = 200;
    data[2] = 300;
    data[3] = 400;

    region_exit(r);
    PASS();
}

/* Test accounting is updated */
static void test_accounting_updated(void) {
    TEST(accounting_updated);

    Region* r = region_create();
    assert(r != NULL);

    size_t before = r->bytes_allocated_total;

    /* Initial allocation */
    int* data = (int*)region_alloc(r, 4 * sizeof(int));
    assert(data != NULL);

    size_t after_alloc = r->bytes_allocated_total;

    /* Realloc to larger */
    int* new_data = (int*)region_realloc(r, data, 4 * sizeof(int), 8 * sizeof(int));
    assert(new_data != NULL);

    size_t after_realloc = r->bytes_allocated_total;

    /* Accounting should increase (new allocation, old not freed) */
    if (after_realloc <= after_alloc) {
        FAIL("accounting not updated after realloc");
        region_exit(r);
        return;
    }

    region_exit(r);
    PASS();
}

/* Test large reallocation (arena path) */
static void test_large_realloc(void) {
    TEST(large_realloc);

    Region* r = region_create();
    assert(r != NULL);

    /* Allocate larger than inline buffer threshold */
    size_t large_size = 1024;
    char* data = (char*)region_alloc(r, large_size);
    assert(data != NULL);

    /* Fill with pattern */
    for (size_t i = 0; i < large_size; i++) {
        data[i] = (char)(i & 0xFF);
    }

    /* Realloc to even larger */
    size_t new_size = 2048;
    char* new_data = (char*)region_realloc(r, data, large_size, new_size);

    if (new_data == NULL) {
        FAIL("large realloc returned NULL");
        region_exit(r);
        return;
    }

    /* Verify pattern preserved */
    for (size_t i = 0; i < large_size; i++) {
        if (new_data[i] != (char)(i & 0xFF)) {
            FAIL("data pattern not preserved");
            region_exit(r);
            return;
        }
    }

    region_exit(r);
    PASS();
}

/* Test multiple reallocs in sequence */
static void test_multiple_reallocs(void) {
    TEST(multiple_reallocs);

    Region* r = region_create();
    assert(r != NULL);

    int* data = (int*)region_alloc(r, 2 * sizeof(int));
    assert(data != NULL);
    data[0] = 1;
    data[1] = 2;

    /* Grow several times */
    data = (int*)region_realloc(r, data, 2 * sizeof(int), 4 * sizeof(int));
    assert(data != NULL);
    data[2] = 3;
    data[3] = 4;

    data = (int*)region_realloc(r, data, 4 * sizeof(int), 8 * sizeof(int));
    assert(data != NULL);
    data[4] = 5;
    data[5] = 6;
    data[6] = 7;
    data[7] = 8;

    data = (int*)region_realloc(r, data, 8 * sizeof(int), 16 * sizeof(int));
    assert(data != NULL);

    /* Verify all data preserved */
    for (int i = 0; i < 8; i++) {
        if (data[i] != i + 1) {
            FAIL("data lost during multiple reallocs");
            region_exit(r);
            return;
        }
    }

    region_exit(r);
    PASS();
}

int main(void) {
    printf("=== Region Realloc Tests (Issue 10 P1) ===\n");

    test_basic_realloc();
    test_shrink_noop();
    test_same_size_noop();
    test_null_oldptr();
    test_accounting_updated();
    test_large_realloc();
    test_multiple_reallocs();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
