/* test_concurrency.c - Atom and thread tests
 * DIRECTIVE: NO CHANNELS - All channel tests removed.
 * Use algebraic effects for structured concurrency instead.
 */
#include "test_framework.h"
#include <unistd.h>

/* DIRECTIVE: NO CHANNELS - Channel Creation, Send/Receive, Close, and Edge Case tests removed */

/* ========== Atom Creation Tests ========== */

void test_make_atom(void) {
    Obj* val = mk_int(42);
    Obj* atom = make_atom(val);
    ASSERT_NOT_NULL(atom);
    ASSERT_EQ(atom->tag, TAG_ATOM);
    dec_ref(atom);
    dec_ref(val);
    PASS();
}

void test_make_atom_null(void) {
    Obj* atom = make_atom(NULL);
    ASSERT_NOT_NULL(atom);
    dec_ref(atom);
    PASS();
}

void test_make_atom_immediate(void) {
    Obj* val = mk_int_unboxed(99);
    Obj* atom = make_atom(val);
    ASSERT_NOT_NULL(atom);
    dec_ref(atom);
    PASS();
}

/* ========== Atom Deref Tests ========== */

void test_atom_deref(void) {
    Obj* val = mk_int(42);
    Obj* atom = make_atom(val);
    Obj* deref = atom_deref(atom);
    ASSERT_NOT_NULL(deref);
    ASSERT_EQ(obj_to_int(deref), 42);
    dec_ref(deref);
    dec_ref(atom);
    dec_ref(val);
    PASS();
}

void test_atom_deref_null_atom(void) {
    Obj* deref = atom_deref(NULL);
    ASSERT_NULL(deref);
    PASS();
}

void test_atom_deref_immediate(void) {
    Obj* val = mk_int_unboxed(77);
    Obj* atom = make_atom(val);
    Obj* deref = atom_deref(atom);
    ASSERT_EQ(obj_to_int(deref), 77);
    dec_ref(atom);
    PASS();
}

/* ========== Atom Reset Tests ========== */

void test_atom_reset(void) {
    Obj* val1 = mk_int(10);
    Obj* val2 = mk_int(20);
    Obj* atom = make_atom(val1);

    Obj* result = atom_reset(atom, val2);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 20);

    Obj* current = atom_deref(atom);
    ASSERT_EQ(obj_to_int(current), 20);

    dec_ref(result);
    dec_ref(current);
    dec_ref(val1);
    dec_ref(val2);
    dec_ref(atom);
    PASS();
}

void test_atom_reset_null_atom(void) {
    Obj* val = mk_int(42);
    Obj* old = atom_reset(NULL, val);
    ASSERT_NULL(old);
    dec_ref(val);
    PASS();
}

void test_atom_reset_to_null(void) {
    Obj* val = mk_int(42);
    Obj* atom = make_atom(val);

    Obj* result = atom_reset(atom, NULL);
    ASSERT_NULL(result);

    Obj* current = atom_deref(atom);
    ASSERT_NULL(current);

    dec_ref(val);
    dec_ref(atom);
    PASS();
}

/* ========== Atom Swap Tests ========== */

/* Swap function - takes old val, returns new val (closure form) */
static Obj* conc_increment_closure_fn(Obj** caps, Obj** args, int nargs) {
    (void)caps; (void)nargs;
    if (!args || !args[0]) return mk_int(1);
    long n = obj_to_int(args[0]);
    return mk_int(n + 1);
}

void test_atom_swap(void) {
    Obj* val = mk_int(10);
    Obj* atom = make_atom(val);

    /* atom_swap takes a closure */
    Obj* inc_closure = mk_closure(conc_increment_closure_fn, NULL, NULL, 0, 1);
    Obj* result = atom_swap(atom, inc_closure);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 11);

    Obj* current = atom_deref(atom);
    ASSERT_EQ(obj_to_int(current), 11);

    dec_ref(result);
    dec_ref(current);
    dec_ref(inc_closure);
    dec_ref(atom);
    dec_ref(val);
    PASS();
}

void test_atom_swap_null_atom(void) {
    Obj* inc_closure = mk_closure(conc_increment_closure_fn, NULL, NULL, 0, 1);
    Obj* result = atom_swap(NULL, inc_closure);
    ASSERT_NULL(result);
    dec_ref(inc_closure);
    PASS();
}

void test_atom_swap_null_fn(void) {
    Obj* val = mk_int(42);
    Obj* atom = make_atom(val);
    Obj* result = atom_swap(atom, NULL);
    ASSERT_NULL(result);
    dec_ref(val);
    dec_ref(atom);
    PASS();
}

void test_atom_swap_multiple(void) {
    Obj* val = mk_int(0);
    Obj* atom = make_atom(val);
    Obj* inc_closure = mk_closure(conc_increment_closure_fn, NULL, NULL, 0, 1);

    for (int i = 0; i < 10; i++) {
        Obj* result = atom_swap(atom, inc_closure);
        ASSERT_EQ(obj_to_int(result), i + 1);
        dec_ref(result);
    }

    Obj* final = atom_deref(atom);
    ASSERT_EQ(obj_to_int(final), 10);
    dec_ref(final);
    dec_ref(inc_closure);
    dec_ref(atom);
    dec_ref(val);
    PASS();
}

/* ========== Atom Compare and Swap Tests ========== */

void test_atom_cas_success(void) {
    Obj* val = mk_int(10);
    Obj* atom = make_atom(val);

    Obj* expected = val;
    Obj* new_val = mk_int(20);

    Obj* result = atom_cas(atom, expected, new_val);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 1);  /* success */

    Obj* current = atom_deref(atom);
    ASSERT_EQ(obj_to_int(current), 20);

    dec_ref(result);
    dec_ref(current);
    dec_ref(new_val);
    dec_ref(atom);
    dec_ref(val);
    PASS();
}

void test_atom_cas_failure(void) {
    Obj* val = mk_int(10);
    Obj* atom = make_atom(val);

    Obj* expected = mk_int(999);  /* Wrong expected value */
    Obj* new_val = mk_int(20);

    Obj* result = atom_cas(atom, expected, new_val);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 0);  /* failure */

    Obj* current = atom_deref(atom);
    ASSERT_EQ(obj_to_int(current), 10);  /* Unchanged */

    dec_ref(expected);
    dec_ref(new_val);
    dec_ref(result);
    dec_ref(current);
    dec_ref(atom);
    dec_ref(val);
    PASS();
}

void test_atom_cas_null_atom(void) {
    Obj* expected = mk_int(10);
    Obj* new_val = mk_int(20);

    Obj* result = atom_cas(NULL, expected, new_val);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 0);

    dec_ref(expected);
    dec_ref(new_val);
    dec_ref(result);
    PASS();
}

/* ========== Thread Tests ========== */

static Obj* conc_return_42(Obj** caps, Obj** args, int nargs) {
    (void)caps; (void)args; (void)nargs;
    return mk_int(42);
}

void test_spawn_thread(void) {
    Obj* closure = mk_closure(conc_return_42, NULL, NULL, 0, 0);
    Obj* thread = spawn_thread(closure);
    ASSERT_NOT_NULL(thread);
    ASSERT_EQ(thread->tag, TAG_THREAD);

    Obj* result = thread_join(thread);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 42);

    dec_ref(result);
    dec_ref(thread);
    dec_ref(closure);
    PASS();
}

void test_spawn_thread_null(void) {
    Obj* thread = spawn_thread(NULL);
    ASSERT_NOT_NULL(thread);
    Obj* result = thread_join(thread);
    ASSERT_NULL(result);
    dec_ref(thread);
    PASS();
}

static Obj* add_captures(Obj** caps, Obj** args, int nargs) {
    (void)args; (void)nargs;
    long a = obj_to_int(caps[0]);
    long b = obj_to_int(caps[1]);
    return mk_int(a + b);
}

void test_spawn_thread_with_captures(void) {
    Obj* cap1 = mk_int(30);
    Obj* cap2 = mk_int(12);
    Obj* caps[] = {cap1, cap2};
    Obj* closure = mk_closure(add_captures, caps, NULL, 2, 0);

    Obj* thread = spawn_thread(closure);
    Obj* result = thread_join(thread);

    ASSERT_EQ(obj_to_int(result), 42);

    dec_ref(result);
    dec_ref(thread);
    dec_ref(closure);
    dec_ref(cap1);
    dec_ref(cap2);
    PASS();
}

/* ========== Thread Join Tests ========== */

void test_thread_join_null(void) {
    Obj* result = thread_join(NULL);
    ASSERT_NULL(result);
    PASS();
}

void test_thread_join_multiple_times(void) {
    Obj* closure = mk_closure(conc_return_42, NULL, NULL, 0, 0);
    Obj* thread = spawn_thread(closure);

    Obj* result1 = thread_join(thread);
    ASSERT_EQ(obj_to_int(result1), 42);

    /* Join again - should return cached result */
    Obj* result2 = thread_join(thread);
    ASSERT_EQ(obj_to_int(result2), 42);

    dec_ref(result1);
    dec_ref(result2);
    dec_ref(thread);
    dec_ref(closure);
    PASS();
}

/* DIRECTIVE: NO CHANNELS - test_concurrent_channel and producer_fn removed */

/* ========== Concurrent Atom Tests ========== */

static Obj* conc_atom_incrementer(Obj** caps, Obj** args, int nargs) {
    (void)args; (void)nargs;
    Obj* atom = caps[0];
    Obj* inc_closure = caps[1];  /* Pass the increment closure as capture */
    for (int i = 0; i < 100; i++) {
        Obj* result = atom_swap(atom, inc_closure);
        if (result) dec_ref(result);
    }
    return NULL;
}

void test_concurrent_atom(void) {
    Obj* val = mk_int(0);
    Obj* atom = make_atom(val);
    Obj* inc_closure = mk_closure(conc_increment_closure_fn, NULL, NULL, 0, 1);
    Obj* caps[] = {atom, inc_closure};

    Obj* closure1 = mk_closure(conc_atom_incrementer, caps, NULL, 2, 0);
    Obj* closure2 = mk_closure(conc_atom_incrementer, caps, NULL, 2, 0);

    Obj* thread1 = spawn_thread(closure1);
    Obj* thread2 = spawn_thread(closure2);

    thread_join(thread1);
    thread_join(thread2);

    Obj* final = atom_deref(atom);
    ASSERT_EQ(obj_to_int(final), 200);  /* 100 + 100 */

    dec_ref(final);
    dec_ref(thread1);
    dec_ref(thread2);
    dec_ref(closure1);
    dec_ref(closure2);
    dec_ref(inc_closure);
    dec_ref(atom);
    dec_ref(val);
    PASS();
}

/* ========== Stress Tests ========== */

/* DIRECTIVE: NO CHANNELS - test_channel_stress_many_values removed */

void test_atom_stress_many_swaps(void) {
    Obj* val = mk_int(0);
    Obj* atom = make_atom(val);
    Obj* inc_closure = mk_closure(conc_increment_closure_fn, NULL, NULL, 0, 1);

    for (int i = 0; i < 1000; i++) {
        Obj* result = atom_swap(atom, inc_closure);
        if (result) dec_ref(result);
    }

    Obj* final = atom_deref(atom);
    ASSERT_EQ(obj_to_int(final), 1000);
    dec_ref(final);
    dec_ref(inc_closure);
    dec_ref(atom);
    dec_ref(val);
    PASS();
}

static Obj* noop_fn(Obj** caps, Obj** args, int nargs) {
    (void)caps; (void)args; (void)nargs;
    return mk_int(0);
}

void test_thread_stress_many_threads(void) {
    Obj* threads[50];
    Obj* closure = mk_closure(noop_fn, NULL, NULL, 0, 0);

    for (int i = 0; i < 50; i++) {
        threads[i] = spawn_thread(closure);
    }

    for (int i = 0; i < 50; i++) {
        Obj* result = thread_join(threads[i]);
        dec_ref(result);
        dec_ref(threads[i]);
    }

    dec_ref(closure);
    PASS();
}

/* ========== Run All Concurrency Tests ========== */

void run_concurrency_tests(void) {
    TEST_SUITE("Concurrency");

    /* DIRECTIVE: NO CHANNELS - Channel Creation, Send/Receive, Close, Edge Case test sections removed */

    TEST_SECTION("Atom Creation");
    RUN_TEST(test_make_atom);
    RUN_TEST(test_make_atom_null);
    RUN_TEST(test_make_atom_immediate);

    TEST_SECTION("Atom Deref");
    RUN_TEST(test_atom_deref);
    RUN_TEST(test_atom_deref_null_atom);
    RUN_TEST(test_atom_deref_immediate);

    TEST_SECTION("Atom Reset");
    RUN_TEST(test_atom_reset);
    RUN_TEST(test_atom_reset_null_atom);
    RUN_TEST(test_atom_reset_to_null);

    TEST_SECTION("Atom Swap");
    RUN_TEST(test_atom_swap);
    RUN_TEST(test_atom_swap_null_atom);
    RUN_TEST(test_atom_swap_null_fn);
    RUN_TEST(test_atom_swap_multiple);

    TEST_SECTION("Atom CAS");
    RUN_TEST(test_atom_cas_success);
    RUN_TEST(test_atom_cas_failure);
    RUN_TEST(test_atom_cas_null_atom);

    TEST_SECTION("Thread Spawn");
    RUN_TEST(test_spawn_thread);
    RUN_TEST(test_spawn_thread_null);
    RUN_TEST(test_spawn_thread_with_captures);

    TEST_SECTION("Thread Join");
    RUN_TEST(test_thread_join_null);
    RUN_TEST(test_thread_join_multiple_times);

    TEST_SECTION("Concurrent Operations");
    /* DIRECTIVE: NO CHANNELS - test_concurrent_channel removed */
    RUN_TEST(test_concurrent_atom);

    TEST_SECTION("Stress Tests");
    /* DIRECTIVE: NO CHANNELS - test_channel_stress_many_values removed */
    RUN_TEST(test_atom_stress_many_swaps);
    RUN_TEST(test_thread_stress_many_threads);
}
