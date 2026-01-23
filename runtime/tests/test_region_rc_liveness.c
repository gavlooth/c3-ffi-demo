/*
 * test_region_rc_liveness.c - Test for Issue 1 P2 region RC liveness
 *
 * Verifies that regions with RETAIN_REGION strategy outlive their scope
 * and are not reclaimed until all retains are released.
 *
 * STATUS: ENABLED (Issue 29 P3 - APIs now exposed)
 *
 * Reference:
 * - runtime/docs/REGION_RC_MODEL.md (Section 3.3 external boundaries)
 * - runtime/docs/CTRR_TRANSMIGRATION.md
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Colors for output */
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define RESET   "\033[0m"

/* Include runtime headers */
#include "../include/omni.h"
#include "../src/memory/region_core.h"  /* For Region struct internals */

/* Test counters (local to this test file) */
static int rc_tests_run = 0;
static int rc_tests_passed = 0;
static int rc_tests_failed = 0;

/* ========== Region RC Liveness Tests ========== */

/*
 * Test 1: Region outlives scope after retain_internal
 */
static void test_region_outlives_after_retain(void) {
    printf("  region outlives after retain: ");
    fflush(stdout);

    Region* r = region_create();
    if (!r) {
        printf(RED "FAIL" RESET " - region_create failed\n");
        rc_tests_failed++;
        return;
    }

    Obj* x = mk_int_region(r, 42);
    if (!x) {
        printf(RED "FAIL" RESET " - mk_int_region failed\n");
        rc_tests_failed++;
        return;
    }

    /* Create external reference BEFORE scope exit */
    region_retain_internal(r);
    if (r->external_rc != 1) {
        printf(RED "FAIL" RESET " - external_rc should be 1 after retain\n");
        rc_tests_failed++;
        return;
    }

    /* Simulate scope exit */
    region_exit(r);

    /* Region should still be alive (not destroyed) because external_rc > 0 */
    if (r->external_rc != 1) {
        printf(RED "FAIL" RESET " - external_rc changed after exit\n");
        rc_tests_failed++;
        return;
    }

    /* Release external reference */
    region_release_internal(r);
    /* Note: region_release_internal calls region_destroy_if_dead internally */

    printf(GREEN "PASS" RESET "\n");
    rc_tests_passed++;
}

/*
 * Test 2: Multiple retains require matching number of releases
 */
static void test_multiple_retains_matching_releases(void) {
    printf("  multiple retains require matching releases: ");
    fflush(stdout);

    Region* r = region_create();
    if (!r) {
        printf(RED "FAIL" RESET " - region_create failed\n");
        rc_tests_failed++;
        return;
    }

    /* Create multiple external references */
    region_retain_internal(r);
    region_retain_internal(r);
    region_retain_internal(r);

    if (r->external_rc != 3) {
        printf(RED "FAIL" RESET " - external_rc should be 3\n");
        rc_tests_failed++;
        return;
    }

    /* Exit scope while we have external refs */
    region_exit(r);

    /* First release - should still live */
    region_release_internal(r);
    if (r->external_rc != 2) {
        printf(RED "FAIL" RESET " - external_rc should be 2 after first release\n");
        rc_tests_failed++;
        return;
    }

    /* Second release - should still live */
    region_release_internal(r);
    if (r->external_rc != 1) {
        printf(RED "FAIL" RESET " - external_rc should be 1 after second release\n");
        rc_tests_failed++;
        return;
    }

    /* Third release - triggers destroy_if_dead */
    region_release_internal(r);
    /* After this, r may be destroyed or recycled */

    printf(GREEN "PASS" RESET "\n");
    rc_tests_passed++;
}

/*
 * Test 3: Region with zero external refs is destroyed on exit
 */
static void test_region_destroyed_without_retains(void) {
    printf("  region destroyed without retains: ");
    fflush(stdout);

    Region* r = region_create();
    if (!r) {
        printf(RED "FAIL" RESET " - region_create failed\n");
        rc_tests_failed++;
        return;
    }

    /* Verify initial external_rc is 0 */
    if (r->external_rc != 0) {
        printf(RED "FAIL" RESET " - initial external_rc should be 0\n");
        rc_tests_failed++;
        return;
    }

    /* Do NOT create external references */

    /* Exit scope - should trigger destruction since external_rc == 0 */
    region_exit(r);

    /* Region may be destroyed or recycled after exit with no external refs */
    /* We can't safely access r after this point */

    printf(GREEN "PASS" RESET "\n");
    rc_tests_passed++;
}

/*
 * Test 4: Retain/Release pairs with real values
 */
static void test_retain_preserves_value(void) {
    printf("  retain preserves value validity: ");
    fflush(stdout);

    Region* r = region_create();
    if (!r) {
        printf(RED "FAIL" RESET " - region_create failed\n");
        rc_tests_failed++;
        return;
    }

    /* Allocate a pair in region */
    Obj* a = mk_int_region(r, 1);
    Obj* b = mk_int_region(r, 2);
    Obj* pair = mk_cell_region(r, a, b);

    if (!pair || pair->tag != TAG_PAIR) {
        printf(RED "FAIL" RESET " - pair creation failed\n");
        rc_tests_failed++;
        return;
    }

    /* Retain region (pair will escape) */
    region_retain_internal(r);

    /* Exit scope */
    region_exit(r);

    /* Pair should still be valid and readable */
    if (pair->tag != TAG_PAIR || !pair->a || !pair->b) {
        printf(RED "FAIL" RESET " - pair corrupted after exit\n");
        rc_tests_failed++;
        return;
    }

    if (obj_to_int(pair->a) != 1 || obj_to_int(pair->b) != 2) {
        printf(RED "FAIL" RESET " - pair values changed after exit\n");
        rc_tests_failed++;
        return;
    }

    /* Release region */
    region_release_internal(r);
    /* After release, r may be destroyed */

    printf(GREEN "PASS" RESET "\n");
    rc_tests_passed++;
}

/* ========== Test Suite Entry Point ========== */

void run_region_rc_liveness_tests(void) {
    printf("\n" YELLOW "=== Region RC Liveness (Issue 1 P2) ===" RESET "\n");
    printf(GREEN "STATUS: ENABLED - APIs now exposed (Issue 29 P3 Fixed)" RESET "\n\n");

    rc_tests_run = 0;
    rc_tests_passed = 0;
    rc_tests_failed = 0;

    /* Run all tests */
    test_region_outlives_after_retain();
    rc_tests_run++;

    test_multiple_retains_matching_releases();
    rc_tests_run++;

    test_region_destroyed_without_retains();
    rc_tests_run++;

    test_retain_preserves_value();
    rc_tests_run++;

    printf("\n" YELLOW "=== Summary ===" RESET "\n");
    printf("  Total: %d, Passed: %d, Failed: %d\n", rc_tests_run, rc_tests_passed, rc_tests_failed);
    if (rc_tests_failed == 0) {
        printf("  Status: " GREEN "ALL TESTS PASSED" RESET "\n");
    } else {
        printf("  Status: " RED "SOME TESTS FAILED" RESET "\n");
    }
}

/* Standalone main() for independent testing */
int main(void) {
    run_region_rc_liveness_tests();
    return rc_tests_failed > 0 ? 1 : 0;
}
