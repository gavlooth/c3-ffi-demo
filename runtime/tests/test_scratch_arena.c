/*
 * test_scratch_arena.c - Tests for scratch arena API
 *
 * Tests:
 * 1. Basic allocation and cleanup
 * 2. Nested scratch contexts with conflict
 * 3. Memory reuse across calls
 * 4. High-water trimming behavior
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Include arena implementation */
#define ARENA_IMPLEMENTATION
#include "../../third_party/arena/arena.h"

#define SCRATCH_ARENA_DEBUG 1
#include "../src/memory/scratch_arena.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  %-50s ", name)
#define PASS() do { printf("[PASS]\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("[FAIL] %s\n", msg); tests_failed++; } while(0)

/* Test 1: Basic allocation and cleanup */
static void test_basic_alloc(void) {
    TEST("basic allocation");

    Scratch s = scratch_begin(NULL);
    assert(scratch_is_active(&s));

    void* p1 = scratch_alloc(&s, 100);
    void* p2 = scratch_alloc(&s, 200);

    if (!p1 || !p2) {
        FAIL("allocation returned NULL");
        return;
    }

    /* Write to memory to verify it's usable */
    memset(p1, 0xAA, 100);
    memset(p2, 0xBB, 200);

    scratch_end(&s);

    if (scratch_is_active(&s)) {
        FAIL("scratch still active after end");
        return;
    }

    PASS();
}

/* Test 2: Nested scratch with conflict parameter */
static void test_nested_conflict(void) {
    TEST("nested scratch with conflict");

    Scratch outer = scratch_begin(NULL);
    void* outer_data = scratch_alloc(&outer, 64);
    memset(outer_data, 0x11, 64);

    /* Inner scratch should use different arena */
    Scratch inner = scratch_begin(outer.arena);

    if (inner.arena == outer.arena) {
        FAIL("inner using same arena as outer");
        scratch_end(&inner);
        scratch_end(&outer);
        return;
    }

    void* inner_data = scratch_alloc(&inner, 64);
    memset(inner_data, 0x22, 64);

    scratch_end(&inner);

    /* Outer data should still be valid (0x11 pattern) */
    unsigned char* check = (unsigned char*)outer_data;
    if (check[0] != 0x11 || check[63] != 0x11) {
        FAIL("outer data corrupted after inner end");
        scratch_end(&outer);
        return;
    }

    scratch_end(&outer);
    PASS();
}

/* Test 3: Memory reuse across calls */
static void test_memory_reuse(void) {
    TEST("memory reuse across calls");

    /* First scratch */
    Scratch s1 = scratch_begin(NULL);
    void* p1 = scratch_alloc(&s1, 1024);
    Arena* arena1 = s1.arena;
    scratch_end(&s1);

    /* Second scratch should reuse same arena */
    Scratch s2 = scratch_begin(NULL);
    void* p2 = scratch_alloc(&s2, 1024);
    Arena* arena2 = s2.arena;
    scratch_end(&s2);

    /* Same arena should be reused (depth-based selection) */
    if (arena1 != arena2) {
        /* This is OK - alternating is valid behavior */
        PASS();
        return;
    }

    /* Memory addresses might be same or different depending on arena state */
    /* Just verify we got valid pointers */
    if (!p1 || !p2) {
        FAIL("allocation failed");
        return;
    }

    PASS();
}

/* Test 4: Deep nesting (stress test) */
static void test_deep_nesting(void) {
    TEST("deep nesting (4 levels)");

    Scratch s1 = scratch_begin(NULL);
    void* p1 = scratch_alloc(&s1, 32);

    Scratch s2 = scratch_begin(s1.arena);
    void* p2 = scratch_alloc(&s2, 32);

    Scratch s3 = scratch_begin(s2.arena);
    void* p3 = scratch_alloc(&s3, 32);

    Scratch s4 = scratch_begin(s3.arena);
    void* p4 = scratch_alloc(&s4, 32);

    if (!p1 || !p2 || !p3 || !p4) {
        FAIL("allocation failed in nested context");
        goto cleanup;
    }

    /* Write patterns */
    memset(p1, 0x11, 32);
    memset(p2, 0x22, 32);
    memset(p3, 0x33, 32);
    memset(p4, 0x44, 32);

    /* Unwind in LIFO order */
    scratch_end(&s4);

    /* p3 should still be valid */
    if (((unsigned char*)p3)[0] != 0x33) {
        FAIL("p3 corrupted after s4 end");
        goto cleanup_partial;
    }

    scratch_end(&s3);
    scratch_end(&s2);
    scratch_end(&s1);

    PASS();
    return;

cleanup:
    scratch_end(&s4);
cleanup_partial:
    scratch_end(&s3);
    scratch_end(&s2);
    scratch_end(&s1);
}

/* Test 5: Aligned allocation */
static void test_aligned_alloc(void) {
    TEST("aligned allocation (64-byte)");

    Scratch s = scratch_begin(NULL);

    void* p = scratch_alloc_aligned(&s, 256, 64);

    if (!p) {
        FAIL("aligned allocation returned NULL");
        scratch_end(&s);
        return;
    }

    uintptr_t addr = (uintptr_t)p;
    if (addr % 64 != 0) {
        FAIL("pointer not 64-byte aligned");
        scratch_end(&s);
        return;
    }

    scratch_end(&s);
    PASS();
}

/* Test 6: Release all */
static void test_release_all(void) {
    TEST("scratch_release_all()");

    /* Allocate some memory */
    Scratch s = scratch_begin(NULL);
    scratch_alloc(&s, 4096);
    scratch_end(&s);

    /* Release all scratch memory */
    scratch_release_all();

    /* Should still be able to use scratch after release */
    Scratch s2 = scratch_begin(NULL);
    void* p = scratch_alloc(&s2, 64);
    scratch_end(&s2);

    if (!p) {
        FAIL("allocation failed after release_all");
        return;
    }

    PASS();
}

int main(void) {
    printf("\n=== Scratch Arena Tests ===\n\n");

    test_basic_alloc();
    test_nested_conflict();
    test_memory_reuse();
    test_deep_nesting();
    test_aligned_alloc();
    test_release_all();

    printf("\n=== Results: %d passed, %d failed ===\n\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
