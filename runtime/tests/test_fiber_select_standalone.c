/* Standalone test for fiber_select multi-channel blocking */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "test_framework.h"
#include "../src/util/hashmap.c"
#include "../src/memory/arena_core.c"
#include "../src/memory/region_core.c"
#include "../src/memory/transmigrate.c"
#include "../src/memory/region_value.c"
#include "../src/memory/region_metadata.c"
#include "../src/runtime.c"
#include "../src/math_numerics.c"

/* Test counter definitions */
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;
const char* current_suite = NULL;

/* Test 1: Select with immediately ready channel (recv case) */
void test_select_immediate_recv(void) {
    FiberChannel* ch = fiber_channel_create(1);
    ASSERT_NOT_NULL(ch);

    Obj* val = mk_int_unboxed(42);
    bool sent = fiber_channel_try_send(ch, val);
    ASSERT_TRUE(sent);

    Obj* result = NULL;
    SelectCase cases[1] = {
        { SELECT_RECV, ch, NULL, &result }
    };

    int ready = fiber_select(cases, 1);
    ASSERT_EQ(ready, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 42);

    fiber_channel_release(ch);
    PASS();
}

/* Test 2: Select with immediately ready channel (send case) */
void test_select_immediate_send(void) {
    FiberChannel* ch = fiber_channel_create(1);
    ASSERT_NOT_NULL(ch);

    Obj* val = mk_int_unboxed(99);
    SelectCase cases[1] = {
        { SELECT_SEND, ch, val, NULL }
    };

    int ready = fiber_select(cases, 1);
    ASSERT_EQ(ready, 0);

    bool ok;
    Obj* received = fiber_channel_try_recv(ch, &ok);
    ASSERT_TRUE(ok);
    ASSERT_NOT_NULL(received);
    ASSERT_EQ(obj_to_int(received), 99);

    fiber_channel_release(ch);
    PASS();
}

/* Test 3: Select with default case when no channel ready */
void test_select_default(void) {
    FiberChannel* ch1 = fiber_channel_create(0);
    FiberChannel* ch2 = fiber_channel_create(0);
    ASSERT_NOT_NULL(ch1);
    ASSERT_NOT_NULL(ch2);

    Obj* result1 = NULL;
    Obj* result2 = NULL;
    SelectCase cases[3] = {
        { SELECT_RECV, ch1, NULL, &result1 },
        { SELECT_RECV, ch2, NULL, &result2 },
        { SELECT_DEFAULT, NULL, NULL, NULL }
    };

    int ready = fiber_select(cases, 3);
    ASSERT_EQ(ready, 2);
    ASSERT_NULL(result1);
    ASSERT_NULL(result2);

    fiber_channel_release(ch1);
    fiber_channel_release(ch2);
    PASS();
}

/* Test 4: Select with multiple ready channels returns first */
void test_select_multiple_ready(void) {
    FiberChannel* ch1 = fiber_channel_create(1);
    FiberChannel* ch2 = fiber_channel_create(1);
    ASSERT_NOT_NULL(ch1);
    ASSERT_NOT_NULL(ch2);

    fiber_channel_try_send(ch1, mk_int_unboxed(1));
    fiber_channel_try_send(ch2, mk_int_unboxed(2));

    Obj* result1 = NULL;
    Obj* result2 = NULL;
    SelectCase cases[2] = {
        { SELECT_RECV, ch1, NULL, &result1 },
        { SELECT_RECV, ch2, NULL, &result2 }
    };

    int ready = fiber_select(cases, 2);
    ASSERT_EQ(ready, 0);
    ASSERT_NOT_NULL(result1);
    ASSERT_EQ(obj_to_int(result1), 1);
    ASSERT_NULL(result2);

    fiber_channel_release(ch1);
    fiber_channel_release(ch2);
    PASS();
}

/* Test 5: Mixed send/recv cases */
void test_select_mixed_cases(void) {
    FiberChannel* ch1 = fiber_channel_create(0);  /* Unbuffered, empty */
    FiberChannel* ch2 = fiber_channel_create(1);  /* Buffered, will fill */
    FiberChannel* ch3 = fiber_channel_create(1);  /* Buffered, empty */

    ASSERT_NOT_NULL(ch1);
    ASSERT_NOT_NULL(ch2);
    ASSERT_NOT_NULL(ch3);

    /* Fill ch2 so send won't work */
    fiber_channel_try_send(ch2, mk_int_unboxed(100));

    Obj* result1 = NULL;
    SelectCase cases[3] = {
        { SELECT_RECV, ch1, NULL, &result1 },           /* Won't work - no sender */
        { SELECT_SEND, ch2, mk_int_unboxed(200), NULL }, /* Won't work - buffer full */
        { SELECT_SEND, ch3, mk_int_unboxed(300), NULL }  /* Should work */
    };

    int ready = fiber_select(cases, 3);
    ASSERT_EQ(ready, 2);

    bool ok;
    Obj* ch3_val = fiber_channel_try_recv(ch3, &ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(obj_to_int(ch3_val), 300);

    fiber_channel_release(ch1);
    fiber_channel_release(ch2);
    fiber_channel_release(ch3);
    PASS();
}

/* Test 6: Empty select returns -1 */
void test_select_empty(void) {
    int ready = fiber_select(NULL, 0);
    ASSERT_EQ(ready, -1);
    PASS();
}

/* Test 7: Closed channel handling */
void test_select_closed_channel(void) {
    FiberChannel* ch = fiber_channel_create(1);
    ASSERT_NOT_NULL(ch);

    fiber_channel_try_send(ch, mk_int_unboxed(555));
    fiber_channel_close(ch);

    Obj* result = NULL;
    SelectCase cases[1] = {
        { SELECT_RECV, ch, NULL, &result }
    };

    int ready = fiber_select(cases, 1);
    ASSERT_EQ(ready, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(obj_to_int(result), 555);

    fiber_channel_release(ch);
    PASS();
}

int main(void) {
    printf("Fiber Select Multi-Channel Tests\n");
    printf("=================================\n\n");

    TEST_SUITE("Fiber Select (Multi-Channel)");

    RUN_TEST(test_select_immediate_recv);
    RUN_TEST(test_select_immediate_send);
    RUN_TEST(test_select_default);
    RUN_TEST(test_select_multiple_ready);
    RUN_TEST(test_select_mixed_cases);
    RUN_TEST(test_select_empty);
    RUN_TEST(test_select_closed_channel);

    TEST_EXIT();
}
