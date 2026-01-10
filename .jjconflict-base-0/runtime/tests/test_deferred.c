/* test_deferred.c - Deferred reference counting tests */
#include "test_framework.h"

/* ========== Deferred Context Tests ========== */

void test_deferred_ctx_initial(void) {
    ASSERT_NULL(DEFERRED_CTX.pending);
    ASSERT_EQ(DEFERRED_CTX.pending_count, 0);
    ASSERT(DEFERRED_CTX.batch_size > 0);
    PASS();
}

void test_set_deferred_batch_size(void) {
    int old_size = DEFERRED_CTX.batch_size;
    set_deferred_batch_size(100);
    ASSERT_EQ(DEFERRED_CTX.batch_size, 100);
    set_deferred_batch_size(old_size);  /* Restore */
    PASS();
}

void test_set_deferred_batch_size_zero(void) {
    int old_size = DEFERRED_CTX.batch_size;
    set_deferred_batch_size(0);  /* Should be ignored */
    ASSERT_EQ(DEFERRED_CTX.batch_size, old_size);
    PASS();
}

void test_set_deferred_batch_size_negative(void) {
    int old_size = DEFERRED_CTX.batch_size;
    set_deferred_batch_size(-10);  /* Should be ignored */
    ASSERT_EQ(DEFERRED_CTX.batch_size, old_size);
    PASS();
}

/* ========== defer_decrement Tests ========== */

void test_defer_decrement_single(void) {
    flush_deferred();  /* Clear any pending */
    Obj* obj = mk_int(42);
    inc_ref(obj);  /* Extra ref to prevent immediate free */

    int old_count = DEFERRED_CTX.pending_count;
    defer_decrement(obj);
    ASSERT_EQ(DEFERRED_CTX.pending_count, old_count + 1);

    flush_deferred();
    dec_ref(obj);  /* Clean up extra ref */
    PASS();
}

void test_defer_decrement_null(void) {
    int old_count = DEFERRED_CTX.total_deferred;
    defer_decrement(NULL);
    ASSERT_EQ(DEFERRED_CTX.total_deferred, old_count);  /* Unchanged */
    PASS();
}

void test_deferred_coalesce_same_obj(void) {
    flush_deferred();
    Obj* obj = mk_int(42);
    inc_ref(obj);
    inc_ref(obj);
    inc_ref(obj);

    defer_decrement(obj);
    int count_after_first = DEFERRED_CTX.pending_count;

    defer_decrement(obj);  /* Should coalesce */
    ASSERT_EQ(DEFERRED_CTX.pending_count, count_after_first);  /* Same entry */

    flush_deferred();
    dec_ref(obj);
    PASS();
}

void test_defer_decrement_multiple_objs(void) {
    flush_deferred();
    Obj* obj1 = mk_int(1);
    Obj* obj2 = mk_int(2);
    Obj* obj3 = mk_int(3);
    inc_ref(obj1);
    inc_ref(obj2);
    inc_ref(obj3);

    defer_decrement(obj1);
    defer_decrement(obj2);
    defer_decrement(obj3);

    ASSERT(DEFERRED_CTX.pending_count >= 3);

    flush_deferred();
    dec_ref(obj1);
    dec_ref(obj2);
    dec_ref(obj3);
    PASS();
}

/* ========== process_deferred Tests ========== */

void test_process_deferred_empty(void) {
    flush_deferred();
    process_deferred();  /* Should not crash */
    ASSERT_EQ(DEFERRED_CTX.pending_count, 0);
    PASS();
}

void test_process_deferred_single(void) {
    flush_deferred();
    Obj* obj = mk_int(42);
    inc_ref(obj);  /* ref=2 */
    defer_decrement(obj);

    set_deferred_batch_size(10);
    process_deferred();

    ASSERT_EQ(DEFERRED_CTX.pending_count, 0);
    dec_ref(obj);  /* Clean up */
    PASS();
}

void test_process_deferred_bounded(void) {
    flush_deferred();
    set_deferred_batch_size(5);

    /* Create 20 deferred decrements */
    Obj* objs[20];
    for (int i = 0; i < 20; i++) {
        objs[i] = mk_int(i);
        inc_ref(objs[i]);
        defer_decrement(objs[i]);
    }

    int before = DEFERRED_CTX.pending_count;
    process_deferred();  /* Should process at most 5 */
    int after = DEFERRED_CTX.pending_count;

    ASSERT(before - after <= 5);  /* Bounded processing */

    flush_deferred();
    for (int i = 0; i < 20; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

/* ========== flush_deferred Tests ========== */

void test_flush_deferred_empty(void) {
    flush_deferred();
    ASSERT_EQ(DEFERRED_CTX.pending_count, 0);
    PASS();
}

void test_flush_deferred_all(void) {
    Obj* objs[10];
    for (int i = 0; i < 10; i++) {
        objs[i] = mk_int(i);
        inc_ref(objs[i]);
        defer_decrement(objs[i]);
    }

    ASSERT(DEFERRED_CTX.pending_count > 0);

    flush_deferred();

    ASSERT_EQ(DEFERRED_CTX.pending_count, 0);
    ASSERT_NULL(DEFERRED_CTX.pending);

    for (int i = 0; i < 10; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

/* ========== should_process_deferred Tests ========== */

void test_should_process_deferred_low(void) {
    flush_deferred();
    set_deferred_batch_size(100);

    /* Add a few items */
    Obj* obj = mk_int(42);
    inc_ref(obj);
    defer_decrement(obj);

    ASSERT(!should_process_deferred());  /* Not enough pending */

    flush_deferred();
    dec_ref(obj);
    PASS();
}

void test_should_process_deferred_high(void) {
    flush_deferred();
    set_deferred_batch_size(5);

    /* Add many items to exceed threshold */
    Obj* objs[20];
    for (int i = 0; i < 20; i++) {
        objs[i] = mk_int(i);
        inc_ref(objs[i]);
        defer_decrement(objs[i]);
    }

    ASSERT(should_process_deferred());  /* Exceeds threshold */

    flush_deferred();
    for (int i = 0; i < 20; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

/* ========== safe_point Tests ========== */

void test_safe_point_no_pending(void) {
    flush_deferred();
    safe_point();  /* Should not crash */
    PASS();
}

void test_safe_point_low_pending(void) {
    flush_deferred();
    set_deferred_batch_size(100);

    Obj* obj = mk_int(42);
    inc_ref(obj);
    defer_decrement(obj);

    int count = DEFERRED_CTX.pending_count;
    safe_point();
    ASSERT_EQ(DEFERRED_CTX.pending_count, count);  /* Unchanged */

    flush_deferred();
    dec_ref(obj);
    PASS();
}

void test_safe_point_triggers_processing(void) {
    flush_deferred();
    set_deferred_batch_size(5);

    Obj* objs[30];
    for (int i = 0; i < 30; i++) {
        objs[i] = mk_int(i);
        inc_ref(objs[i]);
        defer_decrement(objs[i]);
    }

    int before = DEFERRED_CTX.pending_count;
    safe_point();  /* Should trigger processing */
    int after = DEFERRED_CTX.pending_count;

    ASSERT(after < before);  /* Some processed */

    flush_deferred();
    for (int i = 0; i < 30; i++) {
        dec_ref(objs[i]);
    }
    PASS();
}

/* ========== Integration Tests ========== */

void test_deferred_with_dec_ref(void) {
    flush_deferred();

    Obj* obj = mk_int(42);
    /* obj has ref=1 */

    /* Defer instead of immediate dec_ref */
    defer_decrement(obj);

    /* Object should still exist (deferred) */
    ASSERT(DEFERRED_CTX.pending_count > 0);

    /* Flush to actually free */
    flush_deferred();
    PASS();
}

void test_deferred_total_counter(void) {
    flush_deferred();
    int old_total = DEFERRED_CTX.total_deferred;

    Obj* obj = mk_int(42);
    /* Need 4 refs: 1 original + 3 for deferred decrements + 1 for final dec_ref */
    inc_ref(obj);  /* ref=2 */
    inc_ref(obj);  /* ref=3 */
    inc_ref(obj);  /* ref=4 */
    defer_decrement(obj);  /* will dec to 3 */
    defer_decrement(obj);  /* will dec to 2 */
    defer_decrement(obj);  /* will dec to 1 */

    ASSERT_EQ(DEFERRED_CTX.total_deferred, old_total + 3);

    flush_deferred();  /* ref: 4→3→2→1 */
    dec_ref(obj);      /* ref: 1→0, freed */
    PASS();
}

/* ========== Stress Tests ========== */

void test_deferred_stress_many_objects(void) {
    flush_deferred();
    set_deferred_batch_size(50);

    Obj* objs[500];
    for (int i = 0; i < 500; i++) {
        objs[i] = mk_int(i);
        inc_ref(objs[i]);  /* ref=2, one for us, one for deferred */
        defer_decrement(objs[i]);
    }

    /* Process in batches */
    while (DEFERRED_CTX.pending_count > 0) {
        process_deferred();
    }

    ASSERT_EQ(DEFERRED_CTX.pending_count, 0);

    for (int i = 0; i < 500; i++) {
        dec_ref(objs[i]);  /* ref: 1→0, freed */
    }
    PASS();
}

void test_deferred_stress_flush_cycle(void) {
    for (int round = 0; round < 100; round++) {
        Obj* obj = mk_int(round);
        inc_ref(obj);
        defer_decrement(obj);
        flush_deferred();
        dec_ref(obj);
    }
    ASSERT_EQ(DEFERRED_CTX.pending_count, 0);
    PASS();
}

void test_deferred_stress_safe_points(void) {
    set_deferred_batch_size(10);

    for (int i = 0; i < 1000; i++) {
        Obj* obj = mk_int(i);
        inc_ref(obj);
        defer_decrement(obj);

        /* Periodic safe points */
        if (i % 100 == 0) {
            safe_point();
        }
    }

    flush_deferred();
    PASS();
}

/* ========== Edge Cases ========== */

void test_deferred_coalesce_many(void) {
    flush_deferred();
    Obj* obj = mk_int(42);

    /* Add many refs then defer many decrements */
    for (int i = 0; i < 100; i++) {
        inc_ref(obj);
    }

    for (int i = 0; i < 100; i++) {
        defer_decrement(obj);
    }

    /* Should have coalesced into fewer entries */
    ASSERT(DEFERRED_CTX.pending_count < 100);

    flush_deferred();
    dec_ref(obj);  /* Final ref */
    PASS();
}

void test_deferred_interleaved_process(void) {
    flush_deferred();
    set_deferred_batch_size(5);

    for (int round = 0; round < 10; round++) {
        /* Add some */
        for (int i = 0; i < 10; i++) {
            Obj* obj = mk_int(round * 10 + i);
            inc_ref(obj);
            defer_decrement(obj);
            dec_ref(obj);
        }
        /* Process some */
        process_deferred();
    }

    flush_deferred();
    PASS();
}

/* ========== Run All Deferred Tests ========== */

void run_deferred_tests(void) {
    TEST_SUITE("Deferred Reference Counting");

    TEST_SECTION("Context");
    RUN_TEST(test_deferred_ctx_initial);
    RUN_TEST(test_set_deferred_batch_size);
    RUN_TEST(test_set_deferred_batch_size_zero);
    RUN_TEST(test_set_deferred_batch_size_negative);

    TEST_SECTION("defer_decrement");
    RUN_TEST(test_defer_decrement_single);
    RUN_TEST(test_defer_decrement_null);
    RUN_TEST(test_deferred_coalesce_same_obj);
    RUN_TEST(test_defer_decrement_multiple_objs);

    TEST_SECTION("process_deferred");
    RUN_TEST(test_process_deferred_empty);
    RUN_TEST(test_process_deferred_single);
    RUN_TEST(test_process_deferred_bounded);

    TEST_SECTION("flush_deferred");
    RUN_TEST(test_flush_deferred_empty);
    RUN_TEST(test_flush_deferred_all);

    TEST_SECTION("should_process_deferred");
    RUN_TEST(test_should_process_deferred_low);
    RUN_TEST(test_should_process_deferred_high);

    TEST_SECTION("safe_point");
    RUN_TEST(test_safe_point_no_pending);
    RUN_TEST(test_safe_point_low_pending);
    RUN_TEST(test_safe_point_triggers_processing);

    TEST_SECTION("Integration");
    RUN_TEST(test_deferred_with_dec_ref);
    RUN_TEST(test_deferred_total_counter);

    TEST_SECTION("Stress Tests");
    RUN_TEST(test_deferred_stress_many_objects);
    RUN_TEST(test_deferred_stress_flush_cycle);
    RUN_TEST(test_deferred_stress_safe_points);

    TEST_SECTION("Edge Cases");
    RUN_TEST(test_deferred_coalesce_many);
    RUN_TEST(test_deferred_interleaved_process);
}
