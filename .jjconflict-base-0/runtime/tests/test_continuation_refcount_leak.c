/*
 * Test for continuation reference counting bug
 *
 * Expected behavior: When cont_invoke is called, it should properly manage
 * reference counts for both the new frames being installed and the old
 * tl_current_frame being replaced.
 *
 * Actual behavior (bug): In cont_invoke (continuation.c:244-309):
 * 1. When installing new frames via tl_current_frame = frames_to_install,
 *    the old tl_current_frame is not decremented, causing a reference leak.
 * 2. On the error path when frame_clone fails (line 263-265), cloned_head
 *    is released but frames_to_install was never set, so the cleanup is
 *    incomplete.
 *
 * Architectural violation:
 * - Memory leak: Old tl_current_frame reference is not released
 * - Incorrect refcount management: The frame chain being replaced should
 *   have frame_dec_ref called on it
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>

/* Minimal definitions needed */
#define FRAME_PROMPT 1
#define PROMPT_TAG_USER_BASE 0x1000

typedef struct Obj Obj;

typedef struct Frame {
    int tag;
    int refcount;
    struct Frame* prev;
    void* env;
    /* Simplified - only what we need */
    struct {
        int tag;
        void* result;
        void* handler;
        void* escape[10]; /* jmp_buf */
    } prompt;
} Frame;

typedef struct Continuation {
    int refcount;
    uint32_t prompt_tag;
    bool one_shot;
    bool invoked;
    Frame* frames;
    void* prompt_env;
} Continuation;

static _Thread_local Frame* tl_current_frame = NULL;
static _Thread_local uint32_t tl_next_prompt_tag = PROMPT_TAG_USER_BASE;

/* Track allocations for leak detection */
static int g_frame_alloc_count = 0;
static int g_continuation_alloc_count = 0;
static int g_frame_free_count = 0;
static int g_continuation_free_count = 0;

/* Frame pool alloc stub */
static Frame* frame_pool_alloc(void) {
    Frame* f = malloc(sizeof(Frame));
    if (f) {
        memset(f, 0, sizeof(Frame));
        g_frame_alloc_count++;
    }
    return f;
}

/* Frame pool free stub */
static void frame_pool_free(Frame* f) {
    if (f) {
        g_frame_free_count++;
        free(f);
    }
}

/* Frame alloc */
static Frame* frame_alloc(int tag) {
    Frame* f = frame_pool_alloc();
    if (!f) return NULL;
    f->tag = tag;
    f->refcount = 1;
    return f;
}

/* Frame inc ref */
static void frame_inc_ref(Frame* f) {
    if (f) f->refcount++;
}

/* Frame dec ref */
static void frame_dec_ref(Frame* f) {
    if (!f) return;
    if (--f->refcount == 0) {
        Frame* prev = f->prev;
        frame_pool_free(f);
        if (prev) frame_dec_ref(prev);
    }
}

/* Frame clone (simplified) */
static Frame* frame_clone(Frame* f) {
    if (!f) return NULL;

    Frame* clone = frame_alloc(f->tag);
    if (!clone) return NULL;

    /* Shallow copy */
    memcpy(clone, f, sizeof(Frame));
    clone->refcount = 1;
    clone->prev = NULL;

    return clone;
}

/* Continuation capture stub */
static Continuation* cont_capture(uint32_t tag) {
    Continuation* k = malloc(sizeof(Continuation));
    if (!k) return NULL;

    g_continuation_alloc_count++;
    memset(k, 0, sizeof(Continuation));
    k->refcount = 1;
    k->prompt_tag = tag;
    k->one_shot = false;
    k->invoked = false;

    /* Capture current frame chain */
    Frame* captured_head = NULL;
    Frame* captured_tail = NULL;
    Frame* current = tl_current_frame;

    while (current) {
        if (current->tag == FRAME_PROMPT && current->prompt.tag == tag) {
            k->prompt_env = current->env;
            break;
        }

        Frame* clone = frame_clone(current);
        if (!clone) {
            frame_dec_ref(captured_head);
            free(k);
            g_continuation_free_count++;
            return NULL;
        }

        if (!captured_head) {
            captured_head = clone;
            captured_tail = clone;
        } else {
            captured_tail->prev = clone;
            captured_tail = clone;
        }

        current = current->prev;
    }

    k->frames = captured_head;
    return k;
}

/* Cont invoke - BUGGY VERSION from actual code */
static void* cont_invoke_buggy(Continuation* k, void* value) {
    if (!k) return NULL;

    if (k->one_shot && k->invoked) {
        return NULL;
    }
    k->invoked = true;

    /* Clone frames if multi-shot */
    Frame* frames_to_install;
    if (k->refcount > 1 && !k->one_shot) {
        Frame* cloned_head = NULL;
        Frame* cloned_tail = NULL;
        Frame* src = k->frames;

        while (src) {
            Frame* clone = frame_clone(src);
            if (!clone) {
                frame_dec_ref(cloned_head);
                return NULL;
            }
            if (!cloned_head) {
                cloned_head = clone;
                cloned_tail = clone;
            } else {
                cloned_tail->prev = clone;
                cloned_tail = clone;
            }
            src = src->prev;
        }
        frames_to_install = cloned_head;
    } else {
        frames_to_install = k->frames;
        frame_inc_ref(frames_to_install);
    }

    /* Find bottom */
    Frame* bottom = frames_to_install;
    while (bottom && bottom->prev) {
        bottom = bottom->prev;
    }

    /* Find matching prompt */
    Frame* prompt_frame = tl_current_frame;
    while (prompt_frame) {
        if (prompt_frame->tag == FRAME_PROMPT &&
            prompt_frame->prompt.tag == k->prompt_tag) {
            break;
        }
        prompt_frame = prompt_frame->prev;
    }

    if (prompt_frame && bottom) {
        bottom->prev = prompt_frame;
        frame_inc_ref(prompt_frame);
    }

    /* BUG: Old tl_current_frame is not decremented! */
    /* Should do: Frame* old = tl_current_frame; frame_dec_ref(old); */
    tl_current_frame = frames_to_install;

    return value;
}

/* Fixed version for comparison */
static void* cont_invoke_fixed(Continuation* k, void* value) {
    if (!k) return NULL;

    if (k->one_shot && k->invoked) {
        return NULL;
    }
    k->invoked = true;

    Frame* frames_to_install;
    if (k->refcount > 1 && !k->one_shot) {
        Frame* cloned_head = NULL;
        Frame* cloned_tail = NULL;
        Frame* src = k->frames;

        while (src) {
            Frame* clone = frame_clone(src);
            if (!clone) {
                frame_dec_ref(cloned_head);
                return NULL;
            }
            if (!cloned_head) {
                cloned_head = clone;
                cloned_tail = clone;
            } else {
                cloned_tail->prev = clone;
                cloned_tail = clone;
            }
            src = src->prev;
        }
        frames_to_install = cloned_head;
    } else {
        frames_to_install = k->frames;
        frame_inc_ref(frames_to_install);
    }

    Frame* bottom = frames_to_install;
    while (bottom && bottom->prev) {
        bottom = bottom->prev;
    }

    Frame* prompt_frame = tl_current_frame;
    while (prompt_frame) {
        if (prompt_frame->tag == FRAME_PROMPT &&
            prompt_frame->prompt.tag == k->prompt_tag) {
            break;
        }
        prompt_frame = prompt_frame->prev;
    }

    if (prompt_frame && bottom) {
        bottom->prev = prompt_frame;
        frame_inc_ref(prompt_frame);
    }

    /* FIX: Decrement old tl_current_frame */
    Frame* old = tl_current_frame;
    tl_current_frame = frames_to_install;
    frame_dec_ref(old);

    return value;
}

/* Cont release */
static void cont_release(Continuation* k) {
    if (!k) return;
    if (--k->refcount == 0) {
        frame_dec_ref(k->frames);
        free(k);
        g_continuation_free_count++;
    }
}

/* Test case */
int main(void) {
    printf("=== Continuation Reference Count Leak Test ===\n\n");

    /* Test 1: Simple invocation should leak old frame */
    printf("Test 1: Basic invocation leak check\n");
    {
        g_frame_alloc_count = 0;
        g_frame_free_count = 0;
        g_continuation_alloc_count = 0;
        g_continuation_free_count = 0;

        /* Set up a frame */
        Frame* f1 = frame_alloc(FRAME_PROMPT);
        f1->prompt.tag = PROMPT_TAG_USER_BASE;
        f1->refcount = 1;
        tl_current_frame = f1;

        /* Add another frame on top */
        Frame* f2 = frame_alloc(FRAME_PROMPT);
        f2->prompt.tag = PROMPT_TAG_USER_BASE + 1;
        f2->refcount = 1;
        f2->prev = f1;
        f1->refcount++;  /* f2 references f1 */
        tl_current_frame = f2;

        int allocs_before = g_frame_alloc_count;
        int frees_before = g_frame_free_count;

        /* Capture continuation */
        Continuation* k = cont_capture(PROMPT_TAG_USER_BASE);
        assert(k != NULL);
        assert(k->frames != NULL);

        /* Invoke continuation */
        cont_invoke_buggy(k, NULL);

        /* Release continuation */
        cont_release(k);

        /* The bug: f2 is leaked because tl_current_frame was overwritten
         * without decrementing the old value */
        int expected_frees = 2;  /* k->frames should be freed */
        int actual_frees = g_frame_free_count - frees_before;

        /* With the bug, f2 is never freed because its refcount was 1
         * and we overwrote tl_current_frame without decrementing */
        int leaked_frames = g_frame_alloc_count - g_frame_free_count;

        printf("  Frames allocated: %d\n", g_frame_alloc_count - allocs_before);
        printf("  Frames freed: %d\n", actual_frees);
        printf("  Leaked frames: %d\n", leaked_frames);

        /* Current code has the bug - we expect a leak */
        if (leaked_frames > 0) {
            printf("  FAIL: Memory leak detected!\n");
            printf("        The old tl_current_frame was not decremented.\n");
        } else {
            printf("  PASS: No leak (bug may be fixed)\n");
        }

        /* Cleanup */
        frame_dec_ref(tl_current_frame);
    }

    printf("\n");

    /* Test 2: Multi-shot continuation */
    printf("Test 2: Multi-shot continuation leak check\n");
    {
        g_frame_alloc_count = 0;
        g_frame_free_count = 0;
        g_continuation_alloc_count = 0;
        g_continuation_free_count = 0;

        Frame* f1 = frame_alloc(FRAME_PROMPT);
        f1->prompt.tag = PROMPT_TAG_USER_BASE;
        f1->refcount = 1;
        tl_current_frame = f1;

        Frame* f2 = frame_alloc(FRAME_PROMPT);
        f2->prompt.tag = PROMPT_TAG_USER_BASE + 1;
        f2->refcount = 1;
        f2->prev = f1;
        f1->refcount++;
        tl_current_frame = f2;

        Continuation* k = cont_capture(PROMPT_TAG_USER_BASE);
        assert(k != NULL);

        /* Make it multi-shot by incrementing refcount */
        k->refcount++;

        int allocs_before = g_frame_alloc_count;
        int frees_before = g_frame_free_count;

        /* Invoke (should clone frames) */
        cont_invoke_buggy(k, NULL);
        cont_release(k);

        int leaked_frames = g_frame_alloc_count - g_frame_free_count;

        printf("  Leaked frames after multi-shot: %d\n", leaked_frames);

        /* Cleanup */
        frame_dec_ref(tl_current_frame);
    }

    printf("\n");

    /* Test 3: Compare with fixed version */
    printf("Test 3: Fixed version should not leak\n");
    {
        g_frame_alloc_count = 0;
        g_frame_free_count = 0;
        g_continuation_alloc_count = 0;
        g_continuation_free_count = 0;

        Frame* f1 = frame_alloc(FRAME_PROMPT);
        f1->prompt.tag = PROMPT_TAG_USER_BASE;
        f1->refcount = 1;
        tl_current_frame = f1;

        Frame* f2 = frame_alloc(FRAME_PROMPT);
        f2->prompt.tag = PROMPT_TAG_USER_BASE + 1;
        f2->refcount = 1;
        f2->prev = f1;
        f1->refcount++;
        tl_current_frame = f2;

        Continuation* k = cont_capture(PROMPT_TAG_USER_BASE);
        assert(k != NULL);

        int allocs_before = g_frame_alloc_count;
        int frees_before = g_frame_free_count;

        /* Use fixed version */
        cont_invoke_fixed(k, NULL);
        cont_release(k);

        int leaked_frames = g_frame_alloc_count - g_frame_free_count;

        printf("  Leaked frames with fix: %d\n", leaked_frames);

        if (leaked_frames == 0) {
            printf("  PASS: Fixed version has no leaks\n");
        } else {
            printf("  FAIL: Even fixed version leaks\n");
        }
    }

    printf("\n=== Summary ===\n");
    printf("The bug is in continuation.c:305 where tl_current_frame is\n");
    printf("assigned without decrementing the old value. This causes\n");
    printf("the previous frame chain to leak.\n");
    printf("\nFix: Add 'Frame* old = tl_current_frame; frame_dec_ref(old);'\n");
    printf("before 'tl_current_frame = frames_to_install;'\n");

    return 0;
}
