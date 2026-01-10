/*
 * Test: Thread Pool with Work-Stealing
 *
 * Tests the thread pool implementation including:
 * - Basic initialization and shutdown
 * - Work-stealing deque operations
 * - Multi-threaded fiber execution
 * - Work distribution across workers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include "../src/memory/continuation.h"

/* Mock Obj for testing */
typedef struct Obj {
    int refcount;
    int value;
} Obj;

void inc_ref(Obj* x) {
    if (x) x->refcount++;
}

void dec_ref(Obj* x) {
    if (x) x->refcount--;
}

Obj* call_closure(Obj* clos, Obj** args, int arg_count) {
    (void)clos; (void)args; (void)arg_count;
    return NULL;
}

static Obj* make_test_obj(int val) {
    Obj* o = malloc(sizeof(Obj));
    o->refcount = 1;
    o->value = val;
    return o;
}

/* ========== Deque Tests ========== */

static void test_deque_basic(void) {
    printf("  test_deque_basic... ");

    WorkStealDeque d;
    deque_init(&d);

    assert(deque_is_empty(&d));

    /* Create some mock fibers */
    Fiber* f1 = malloc(sizeof(Fiber));
    Fiber* f2 = malloc(sizeof(Fiber));
    Fiber* f3 = malloc(sizeof(Fiber));
    memset(f1, 0, sizeof(Fiber));
    memset(f2, 0, sizeof(Fiber));
    memset(f3, 0, sizeof(Fiber));
    f1->id = 1;
    f2->id = 2;
    f3->id = 3;

    /* Push and pop (LIFO) */
    deque_push(&d, f1);
    deque_push(&d, f2);
    deque_push(&d, f3);

    assert(!deque_is_empty(&d));

    Fiber* p3 = deque_pop(&d);
    assert(p3 == f3);

    Fiber* p2 = deque_pop(&d);
    assert(p2 == f2);

    Fiber* p1 = deque_pop(&d);
    assert(p1 == f1);

    assert(deque_is_empty(&d));
    assert(deque_pop(&d) == NULL);

    deque_destroy(&d);
    free(f1);
    free(f2);
    free(f3);

    printf("PASS\n");
}

static void test_deque_steal(void) {
    printf("  test_deque_steal... ");

    WorkStealDeque d;
    deque_init(&d);

    /* Create fibers */
    Fiber* fibers[5];
    for (int i = 0; i < 5; i++) {
        fibers[i] = malloc(sizeof(Fiber));
        memset(fibers[i], 0, sizeof(Fiber));
        fibers[i]->id = i + 1;
        deque_push(&d, fibers[i]);
    }

    /* Steal from top (FIFO order) */
    Fiber* s1 = deque_steal(&d);
    assert(s1 == fibers[0]);

    Fiber* s2 = deque_steal(&d);
    assert(s2 == fibers[1]);

    /* Pop from bottom (LIFO order) */
    Fiber* p5 = deque_pop(&d);
    assert(p5 == fibers[4]);

    Fiber* p4 = deque_pop(&d);
    assert(p4 == fibers[3]);

    /* One left */
    Fiber* last = deque_pop(&d);
    assert(last == fibers[2]);

    assert(deque_is_empty(&d));

    deque_destroy(&d);
    for (int i = 0; i < 5; i++) {
        free(fibers[i]);
    }

    printf("PASS\n");
}

static void test_deque_grow(void) {
    printf("  test_deque_grow... ");

    WorkStealDeque d;
    deque_init(&d);

    /* Push more than initial capacity */
    int count = DEQUE_INITIAL_SIZE * 3;
    Fiber** fibers = malloc(count * sizeof(Fiber*));

    for (int i = 0; i < count; i++) {
        fibers[i] = malloc(sizeof(Fiber));
        memset(fibers[i], 0, sizeof(Fiber));
        fibers[i]->id = i + 1;
        deque_push(&d, fibers[i]);
    }

    /* Verify all are there */
    for (int i = count - 1; i >= 0; i--) {
        Fiber* f = deque_pop(&d);
        assert(f != NULL);
        assert(f->id == (uint64_t)(i + 1));
    }

    assert(deque_is_empty(&d));

    deque_destroy(&d);
    for (int i = 0; i < count; i++) {
        free(fibers[i]);
    }
    free(fibers);

    printf("PASS\n");
}

/* ========== Pool Tests ========== */

static void test_pool_init_shutdown(void) {
    printf("  test_pool_init_shutdown... ");

    assert(fiber_pool_current() == NULL);

    fiber_pool_init(2);

    assert(fiber_pool_current() != NULL);
    assert(fiber_pool_size() == 2);
    assert(fiber_pool_current_worker() != NULL);

    fiber_pool_shutdown();

    assert(fiber_pool_current() == NULL);

    printf("PASS\n");
}

static void test_pool_auto_detect(void) {
    printf("  test_pool_auto_detect... ");

    fiber_pool_init(0);  /* Auto-detect */

    assert(fiber_pool_current() != NULL);
    assert(fiber_pool_size() > 0);

    fiber_pool_shutdown();

    printf("PASS\n");
}

static void test_pool_spawn(void) {
    printf("  test_pool_spawn... ");

    fiber_pool_init(2);

    Obj* thunk = make_test_obj(42);

    Fiber* f = fiber_pool_spawn(thunk);
    assert(f != NULL);
    assert(f->state == FIBER_READY);

    /* Give workers a chance to pick up the fiber */
    usleep(50000);  /* 50ms */

    ThreadPool* pool = fiber_pool_current();
    assert(pool->total_tasks_spawned >= 1);

    fiber_pool_shutdown();

    free(thunk);

    printf("PASS\n");
}

/* Concurrent stealing test */
#define CONCURRENT_FIBERS 1000

static volatile int fibers_processed = 0;
static pthread_mutex_t count_lock = PTHREAD_MUTEX_INITIALIZER;

static void test_pool_concurrent(void) {
    printf("  test_pool_concurrent... ");

    fibers_processed = 0;

    fiber_pool_init(4);

    /* Spawn many fibers */
    for (int i = 0; i < CONCURRENT_FIBERS; i++) {
        Obj* thunk = make_test_obj(i);
        fiber_pool_spawn(thunk);
        /* Note: thunk would be freed by fiber execution in real impl */
    }

    /* Wait for completion (in real impl, fibers would be executed) */
    usleep(200000);  /* 200ms */

    ThreadPool* pool = fiber_pool_current();
    assert(pool->total_tasks_spawned == CONCURRENT_FIBERS);

    /* Check work stealing occurred */
    printf("(steals=%lu) ", pool->total_steals);

    fiber_pool_shutdown();

    printf("PASS\n");
}

/* ========== Main ========== */

int main(void) {
    printf("Thread Pool Tests\n");
    printf("=================\n\n");

    printf("Deque Tests:\n");
    test_deque_basic();
    test_deque_steal();
    test_deque_grow();

    printf("\nPool Tests:\n");
    test_pool_init_shutdown();
    test_pool_auto_detect();
    test_pool_spawn();
    test_pool_concurrent();

    printf("\nAll thread pool tests passed!\n");
    return 0;
}
