/**
 * Test: Channel Send Reference Count Leak
 *
 * Expected invariant:
 *   When channel_send() is called and the channel is closed while waiting,
 *   the function should properly clean up and NOT leak reference counts.
 *
 * Observed behavior:
 *   In the generated code at concurrent.c:167-162, channel_send() does:
 *
 *     atomic_fetch_add(&obj->rc, 1);  // Line 168: Increment RC
 *     ...
 *     while ((tail + 1) % ch->capacity == head) {
 *         pthread_cond_wait(&ch->not_full, &ch->mutex);
 *         if (atomic_load(&ch->closed)) {
 *             pthread_mutex_unlock(&ch->mutex);
 *             return -1;  // BUG: Returns WITHOUT decrementing RC!
 *         }
 *     }
 *
 *   This causes a reference count leak because:
 *   1. The object's refcount is incremented BEFORE checking if the channel is full
 *   2. If the channel is closed while waiting for space, the function returns -1
 *   3. The refcount increment is never undone
 *   4. The object will never be freed (refcount permanently too high)
 *
 * Why this violates architecture/invariants:
 *   1. Reference counting invariant violated: Every increment must have a
 *      corresponding decrement.
 *   2. Memory safety violated: Leaked refcounts cause permanent memory leaks.
 *   3. API contract broken: No way for caller to know refcount was leaked.
 *   4. Exception safety violated: Channel closure should not leak resources.
 *
 * Root cause:
 *   The code at concurrent.c:168 increments refcount too early, before
 *   ensuring the send will succeed. When the channel is closed during the
 *   wait (line 160), the early return doesn't undo the increment.
 *
 *   Correct behavior should be:
 *   1. Wait for space in the buffer FIRST
 *   2. Check if channel is closed
 *   3. Only THEN increment refcount and place object in buffer
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>
#include <assert.h>

/* Simplified structures matching concurrent.c generated code */
typedef struct ConcObj {
    _Atomic int rc;
    int owner_thread;
    int is_immutable;
    int is_pair;
    union {
        long i;
        struct { struct ConcObj *a, *b; };
    };
} ConcObj;

typedef struct MsgChannel {
    ConcObj** buffer;
    int capacity;
    _Atomic int head;
    _Atomic int tail;
    _Atomic int closed;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} MsgChannel;

/* Simulated channel operations */
MsgChannel* channel_create(int capacity) {
    if (capacity <= 0) return NULL;
    MsgChannel* ch = calloc(1, sizeof(MsgChannel));
    if (!ch) return NULL;
    ch->buffer = calloc(capacity, sizeof(ConcObj*));
    if (!ch->buffer) {
        free(ch);
        return NULL;
    }
    ch->capacity = capacity;
    atomic_init(&ch->head, 0);
    atomic_init(&ch->tail, 0);
    atomic_init(&ch->closed, 0);
    pthread_mutex_init(&ch->mutex, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);
    return ch;
}

/* BUGGY version from concurrent.c:152-175 */
int channel_send_buggy(MsgChannel* ch, ConcObj* obj) {
    if (!ch || !obj) return -1;
    if (atomic_load(&ch->closed)) return -1;
    pthread_mutex_lock(&ch->mutex);
    int tail = atomic_load(&ch->tail);
    int head = atomic_load(&ch->head);

    /* BUG: RC incremented BEFORE checking if channel is closed in the loop! */
    atomic_fetch_add(&obj->rc, 1);  /* Line 168 - TOO EARLY! */
    obj->owner_thread = -1;

    while ((tail + 1) % ch->capacity == head) {
        pthread_cond_wait(&ch->not_full, &ch->mutex);
        if (atomic_load(&ch->closed)) {
            pthread_mutex_unlock(&ch->mutex);
            return -1;  /* BUG: Returns without undoing the RC increment! */
        }
        tail = atomic_load(&ch->tail);
        head = atomic_load(&ch->head);
    }

    ch->buffer[tail] = obj;
    atomic_store(&ch->tail, (tail + 1) % ch->capacity);
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);
    return 0;
}

void channel_close(MsgChannel* ch) {
    if (!ch) return;
    pthread_mutex_lock(&ch->mutex);
    atomic_store(&ch->closed, 1);
    pthread_cond_broadcast(&ch->not_empty);
    pthread_cond_broadcast(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);
}

void channel_free(MsgChannel* ch) {
    if (!ch) return;
    pthread_mutex_destroy(&ch->mutex);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
    free(ch->buffer);
    free(ch);
}

ConcObj* mk_obj(long val) {
    ConcObj* obj = calloc(1, sizeof(ConcObj));
    if (!obj) return NULL;
    atomic_init(&obj->rc, 1);
    obj->i = val;
    obj->is_pair = 0;
    obj->owner_thread = 0;
    obj->is_immutable = 0;
    return obj;
}

void free_obj(ConcObj* obj) {
    if (!obj) return;
    printf("  [Freeing object with value %ld, rc=%d]\n", obj->i, atomic_load(&obj->rc));
    free(obj);
}

void dec_ref(ConcObj* obj) {
    if (!obj) return;
    int old = atomic_fetch_sub(&obj->rc, 1);
    if (old == 1) {
        free_obj(obj);
    }
}

/* Thread that closes the channel after a delay */
typedef struct {
    MsgChannel* ch;
    int delay_ms;
} CloseCtx;

void* close_thread(void* arg) {
    CloseCtx* ctx = (CloseCtx*)arg;
    struct timespec ts = {
        .tv_sec = ctx->delay_ms / 1000,
        .tv_nsec = (ctx->delay_ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);
    printf("  [Closing channel]\n");
    channel_close(ctx->ch);
    return NULL;
}

int main(void) {
    printf("=== Channel Send Reference Count Leak Test ===\n\n");

    int tests_failed = 0;

    /* Test 1: Refcount leak when channel closed during send */
    printf("Test 1: Refcount leak when channel closed during send\n");
    printf("-------------------------------------------------------\n");

    {
        /* Create a tiny channel (capacity 1) that will fill up */
        MsgChannel* ch = channel_create(1);
        if (!ch) {
            printf("  FAIL: Could not create channel\n");
            tests_failed++;
            goto end_test1;
        }

        /* Create a test object with refcount = 1 */
        ConcObj* obj = mk_obj(42);
        if (!obj) {
            printf("  FAIL: Could not create object\n");
            tests_failed++;
            channel_free(ch);
            goto end_test1;
        }

        printf("  Initial refcount: %d\n", atomic_load(&obj->rc));

        /* Start a thread that will close the channel after 50ms */
        CloseCtx close_ctx = {ch, 50};
        pthread_t close_th;
        pthread_create(&close_th, NULL, close_thread, &close_ctx);

        /* Fill the channel first */
        ConcObj* filler = mk_obj(999);
        atomic_fetch_add(&filler->rc, 1);  // Will be in buffer
        ch->buffer[0] = filler;
        atomic_store(&ch->tail, 1);

        /* Now try to send - this will block, then channel will close */
        printf("  Calling channel_send on full channel...\n");
        int result = channel_send_buggy(ch, obj);
        printf("  channel_send returned: %d\n", result);

        printf("  Refcount after send: %d\n", atomic_load(&obj->rc));

        /* Wait for close thread */
        pthread_join(close_th, NULL);

        /* Check if refcount is correct */
        int rc = atomic_load(&obj->rc);
        if (rc == 1) {
            printf("  PASS: Refcount unchanged (expected behavior)\n");
        } else {
            printf("\n*** BUG CONFIRMED ***\n");
            printf("Refcount LEAKED!\n");
            printf("  Expected refcount: 1 (unchanged)\n");
            printf("  Actual refcount: %d\n", rc);
            printf("  Leaked refcounts: %d\n", rc - 1);
            printf("\nConsequences:\n");
            printf("  - This object will never be freed\n");
            printf("  - Every failed send leaks a refcount\n");
            printf("  - Memory accumulates over time\n");
            tests_failed = 1;
        }

        /* Cleanup */
        dec_ref(filler);
        dec_ref(obj);
        channel_free(ch);
    }

end_test1:
    printf("\n");

    /* Test 2: Multiple sends on closing channel */
    printf("Test 2: Multiple failed sends compound the leak\n");
    printf("------------------------------------------------\n");

    {
        MsgChannel* ch = channel_create(2);
        if (!ch) {
            printf("  FAIL: Could not create channel\n");
            tests_failed++;
            goto end_test2;
        }

        /* Create test objects */
        ConcObj* objs[5];
        int initial_rcs[5];
        for (int i = 0; i < 5; i++) {
            objs[i] = mk_obj(100 + i);
            initial_rcs[i] = atomic_load(&objs[i]->rc);
        }

        /* Fill the channel */
        ch->buffer[0] = objs[0];
        ch->buffer[1] = objs[1];
        atomic_store(&ch->tail, 2);

        /* Close the channel */
        channel_close(ch);
        printf("  Channel closed\n");

        /* Try to send - should fail and leak refcounts */
        int total_leaked = 0;
        for (int i = 2; i < 5; i++) {
            int initial_rc = atomic_load(&objs[i]->rc);
            channel_send_buggy(ch, objs[i]);
            int final_rc = atomic_load(&objs[i]->rc);
            int leaked = final_rc - initial_rc;
            if (leaked > 0) {
                printf("  obj[%d]: leaked %d refcount(s)\n", i, leaked);
                total_leaked += leaked;
            }
        }

        if (total_leaked == 3) {
            printf("\n*** BUG CONFIRMED ***\n");
            printf("All 3 failed sends leaked refcounts!\n");
            printf("Total leaked: %d refcounts\n", total_leaked);
            tests_failed = 1;
        } else if (total_leaked > 0) {
            printf("\n*** BUG CONFIRMED ***\n");
            printf("Some failed sends leaked refcounts\n");
            printf("Total leaked: %d refcounts\n", total_leaked);
            tests_failed = 1;
        } else {
            printf("  PASS: No refcounts leaked\n");
        }

        /* Cleanup */
        for (int i = 0; i < 5; i++) {
            dec_ref(objs[i]);
        }
        channel_free(ch);
    }

end_test2:
    printf("\n=== Summary ===\n");
    if (tests_failed) {
        printf("FAILED: Reference count leak confirmed\n");
        printf("Location: concurrent.c:168 (generated code in channel_send)\n");
        printf("Issue: RC incremented before closed channel check, not undone on early return\n");
        printf("\nFix options:\n");
        printf("  1. Move RC increment AFTER the closed check in the wait loop\n");
        printf("  2. On closed channel return, decrement RC first: atomic_fetch_sub(&obj->rc, 1)\n");
        printf("  3. Use goto cleanup pattern to ensure RC is decremented on all error paths\n");
        printf("\nRecommended fix (move increment later):\n");
        printf("  while ((tail + 1) %% ch->capacity == head) {\n");
        printf("      pthread_cond_wait(&ch->not_full, &ch->mutex);\n");
        printf("      if (atomic_load(&ch->closed)) {\n");
        printf("          pthread_mutex_unlock(&ch->mutex);\n");
        printf("          return -1;  /* No RC to undo yet */\n");
        printf("      }\n");
        printf("      tail = atomic_load(&ch->tail);\n");
        printf("      head = atomic_load(&ch->head);\n");
        printf("  }\n");
        printf("  /* Only increment RC after we know we'll succeed */\n");
        printf("  atomic_fetch_add(&obj->rc, 1);\n");
        printf("  ch->buffer[tail] = obj;\n");
        return 1;
    } else {
        printf("PASSED: No refcount leaks detected\n");
        return 0;
    }
}
