/*
 * Test for frame_clone jmp_buf Safety Issue
 *
 * Issue: In continuation.c:115-163, the frame_clone() function uses
 * memcpy() to copy the entire Frame structure, including the jmp_buf
 * escape field (for FRAME_PROMPT frames).
 *
 * Expected behavior: Cloning a frame should handle the jmp_buf safely,
 * either by not copying it, or by using platform-specific mechanisms to
 * properly duplicate the jmp_buf state.
 *
 * Actual behavior: memcpy() blindly copies the jmp_buf, which contains
 * implementation-specific state including:
 * - Stack pointers (may become invalid after clone)
 * - Signal masks (platform-specific)
 * - Register state (architecture-specific)
 *
 * Architectural violation:
 * - Undefined behavior: jmp_buf may contain pointers that are invalid
 *   in the cloned context
 * - Portability: jmp_buf layout and contents are platform-specific
 * - Safety: Using a copied jmp_buf can lead to crashes or corruption
 *
 * This test demonstrates the issue by:
 * 1. Creating a prompt frame with a valid jmp_buf
 * 2. Cloning the frame
 * 3. Showing that the jmp_buf is blindly copied
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>

/* Minimal definitions */
typedef struct Obj Obj;

typedef enum {
    FRAME_APP_FN,
    FRAME_APP_ARGS,
    FRAME_APP_DONE,
    FRAME_IF_TEST,
    FRAME_LET_BIND,
    FRAME_LET_BODY,
    FRAME_SEQ,
    FRAME_PROMPT,
    FRAME_HANDLER,
    FRAME_YIELD,
    FRAME_AWAIT,
} FrameTag;

typedef struct Frame {
    FrameTag tag;
    struct Frame* prev;
    Obj* env;
    uint32_t refcount;

    union {
        struct {
            Obj* fn;
            Obj* args_done;
            Obj* args_todo;
        } app;

        struct {
            Obj* then_branch;
            Obj* else_branch;
        } if_test;

        struct {
            Obj* var;
            Obj* body;
            Obj* bindings_todo;
        } let;

        struct {
            Obj* remaining;
        } seq;

        struct {
            uint32_t tag;
            jmp_buf escape;     /* For non-local return */
            Obj* result;
            Obj* handler;
        } prompt;

        struct {
            struct Generator* gen;
        } yield;

        struct {
            struct Promise* prom;
        } await;
    };
} Frame;

/* Track allocations */
static int g_frame_alloc_count = 0;
static int g_frame_free_count = 0;

/* Frame allocation stubs */
static Frame* frame_alloc(FrameTag tag) {
    Frame* f = calloc(1, sizeof(Frame));
    if (!f) return NULL;
    g_frame_alloc_count++;
    f->tag = tag;
    f->refcount = 1;
    return f;
}

static void frame_free(Frame* f) {
    if (!f) return;
    g_frame_free_count++;
    free(f);
}

/* Buggy frame_clone from actual code */
static Frame* frame_clone_buggy(Frame* f) {
    if (!f) return NULL;

    Frame* clone = frame_alloc(f->tag);
    if (!clone) return NULL;

    /* Shallow copy - COPIES ENTIRE STRUCTURE INCLUDING jmp_buf */
    memcpy(clone, f, sizeof(Frame));
    clone->refcount = 1;
    clone->prev = NULL;

    return clone;
}

/* Test helpers */
static jmp_buf* get_prompt_escape(Frame* f) {
    if (f->tag != FRAME_PROMPT) return NULL;
    return &f->prompt.escape;
}

/* Compare jmp_buf contents */
static int jmpbuf_equal(jmp_buf* a, jmp_buf* b) {
    return memcmp(a, b, sizeof(jmp_buf)) == 0;
}

/* Print jmp_buf hex dump for inspection */
static void jmpbuf_dump(const char* label, jmp_buf* buf) {
    printf("%s jmp_buf at %p:\n", label, (void*)buf);
    unsigned char* bytes = (unsigned char*)buf;
    for (size_t i = 0; i < sizeof(jmp_buf); i++) {
        if (i % 16 == 0) printf("  %04zx: ", i);
        printf("%02x ", bytes[i]);
        if ((i + 1) % 16 == 0 || i == sizeof(jmp_buf) - 1) printf("\n");
    }
}

/*
 * Test 1: Demonstrate that jmp_buf is blindly copied
 *
 * This shows that the jmp_buf from the source frame is byte-identical
 * to the jmp_buf in the cloned frame, which is unsafe.
 */
int main(void) {
    printf("=== Frame Clone jmp_buf Safety Test ===\n\n");

    printf("Test 1: jmp_buf is blindly copied by memcpy\n");
    {
        Frame* original = frame_alloc(FRAME_PROMPT);
        original->prompt.tag = 0x1000;

        /* Initialize jmp_buf via setjmp */
        volatile int jmp_val = setjmp(original->prompt.escape);
        if (jmp_val == 0) {
            /* setjmp just returned - store a marker */
            printf("  Original jmp_buf initialized via setjmp\n");
        } else {
            printf("  ERROR: Unexpected longjmp!\n");
            return 1;
        }

        /* Create some distinguishing pattern in memory */
        /* This helps us see if we're copying the same memory */
        memset(&original->prompt.tag, 0xAA, sizeof(uint32_t) + sizeof(jmp_buf) + sizeof(Obj*) * 2);

        /* Clone the frame */
        Frame* cloned = frame_clone_buggy(original);
        if (!cloned) {
            printf("  FAIL: frame_clone returned NULL\n");
            return 1;
        }

        /* Check that jmp_buf was copied byte-for-byte */
        int equal = jmpbuf_equal(&original->prompt.escape, &cloned->prompt.escape);

        printf("  Original and cloned jmp_buf are %s\n",
               equal ? "IDENTICAL (bug!)" : "different");

        if (equal) {
            printf("  FAIL: jmp_buf was blindly copied via memcpy\n");
            printf("        This is unsafe because jmp_buf contains:\n");
            printf("        - Stack pointers (may be invalid after clone)\n");
            printf("        - Signal masks (platform-specific)\n");
            printf("        - Register state (architecture-specific)\n");
        } else {
            printf("  PASS: jmp_buf was not copied (safe behavior)\n");
        }

        /* Show the memory layout */
        printf("\n  Memory layout comparison:\n");
        printf("    Original: tag=0x%X, escape@%p\n",
               original->prompt.tag, (void*)&original->prompt.escape);
        printf("    Cloned:   tag=0x%X, escape@%p\n",
               cloned->prompt.tag, (void*)&cloned->prompt.escape);

        /* Cleanup */
        frame_free(original);
        frame_free(cloned);
    }

    printf("\n");

    /*
     * Test 2: Demonstrate the undefined behavior potential
     *
     * When jmp_buf is copied, it may contain stack pointers.
     * If the original frame is freed and the stack memory is reused,
     * the jmp_buf in the clone becomes invalid.
     */
    printf("Test 2: Potential undefined behavior from copied jmp_buf\n");
    {
        /* Allocate frames in a pattern that might reveal issues */
        Frame* frame1 = frame_alloc(FRAME_PROMPT);
        Frame* frame2 = frame_alloc(FRAME_PROMPT);
        Frame* frame3 = frame_alloc(FRAME_PROMPT);

        /* Initialize jmp_buf in frame1 */
        setjmp(frame1->prompt.escape);
        frame1->prompt.tag = 1;

        /* Clone frame1 */
        Frame* clone = frame_clone_buggy(frame1);

        /* Now free frame1 - its memory might be reused */
        frame_free(frame1);

        /* Allocate something that might reuse the memory */
        /* The jmp_buf in 'clone' now points to potentially freed memory */
        void* dummy = malloc(10000);
        (void)dummy; /* suppress unused warning */

        /* The clone's jmp_buf is now potentially corrupted */
        printf("  After freeing original, clone's jmp_buf may be invalid\n");
        printf("  Original address: %p (freed)\n", (void*)frame1);
        printf("  Clone escape address: %p (contains copied pointers)\n",
               (void*)&clone->prompt.escape);

        printf("  RISK: If jmp_buf contains stack pointers to freed memory,\n");
        printf("        using longjmp on the clone would crash or corrupt.\n");

        /* Cleanup */
        free(dummy);
        frame_free(clone);
        frame_free(frame2);
        frame_free(frame3);
    }

    printf("\n");

    /*
     * Test 3: Show jmp_buf size and contents
     */
    printf("Test 3: jmp_buf implementation details\n");
    {
        printf("  sizeof(jmp_buf) = %zu bytes\n", sizeof(jmp_buf));
        printf("  sizeof(Frame)    = %zu bytes\n", sizeof(Frame));
        printf("  jmp_buf offset   = %zu bytes (in prompt union)\n",
               offsetof(Frame, prompt.escape));

        Frame* test = frame_alloc(FRAME_PROMPT);
        setjmp(test->prompt.escape);

        printf("\n  ");
        jmpbuf_dump("Test frame", &test->prompt.escape);

        frame_free(test);
    }

    printf("\n=== Summary ===\n");
    printf("The frame_clone() function uses memcpy() to copy the entire\n");
    printf("Frame structure, including the jmp_buf escape field.\n\n");
    printf("This is architecturally incorrect because:\n");
    printf("1. jmp_buf contains implementation-specific state\n");
    printf("2. Stack pointers in jmp_buf may become invalid\n");
    printf("3. jmp_buf should not be copied - it should be re-initialized\n");
    printf("4. Using a copied jmp_buf is undefined behavior\n\n");
    printf("Fix: frame_clone should either:\n");
    printf("- Zero out the jmp_buf in the clone\n");
    printf("- Use platform-specific jmp_buf duplication if available\n");
    printf("- Mark cloned prompt frames as invalid for escape\n");

    printf("\nAllocations: %d, Frees: %d\n", g_frame_alloc_count, g_frame_free_count);

    return 0;
}
