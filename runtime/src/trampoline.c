/*
 * trampoline.c - Stack-safe trampoline for mutual recursion
 *
 * Implements bounce and trampoline for stack-safe mutual recursion.
 * This allows functions that would normally cause stack overflow to
 * execute safely by using an iterative loop with explicit thunks.
 *
 * ARCHITECTURE NOTE (2026-01-14):
 * This module now uses the CEK machine from continuation.c for trampolining.
 * The old "bounce" mechanism using mark field hacks is deprecated.
 * Instead, thunks are represented as FRAME_APP_DONE frames.
 *
 * API:
 *   - bounce: Create a thunk for delayed computation (deprecated)
 *   - trampoline: Execute thunks until final result
 *   - is_bounce: Check if value is a thunk
 *   - cek_trampoline: New CEK-based trampoline (preferred)
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "../include/omni.h"
#include "internal_types.h"
#include "memory/continuation.h"

/* ============== Bounce Tag ============== */

/*
 * Bounce objects represent delayed computations (thunks).
 * They are tagged with a special tag so the trampoline can recognize them.
 */

#define TAG_BOUNCE TAG_NOTHING  /* Use NOTHING tag as placeholder for bounce */

/*
 * Helper: Create a bounce (thunk) object
 * A bounce wraps a function and its argument for delayed execution.
 */
static Obj* make_bounce(Obj* (*func)(Obj*), Obj* arg) {
    omni_ensure_global_region();

    /* Create a pair to hold the function pointer and argument */
    /* We encode the function pointer as an integer in the car */
    /* and the argument in the cdr */
    Obj* bounce = mk_pair(NULL, arg);

    /* Store function pointer in the mark field (temporary hack) */
    /* In a real implementation, we'd have a proper bounce structure */
    bounce->mark = (int)(intptr_t)func;

    /* Use a special tag to identify bounce objects */
    bounce->tag = TAG_BOUNCE;

    return bounce;
}

/*
 * Helper: Check if an object is a bounce
 */
int is_bounce(Obj* obj) {
    return obj && IS_BOXED(obj) && obj->tag == TAG_BOUNCE;
}

/*
 * Helper: Extract function from bounce
 */
static Obj* (*get_bounce_func(Obj* bounce))(Obj*) {
    if (!is_bounce(bounce)) return NULL;
    return (Obj* (*)(Obj*))(intptr_t)bounce->mark;
}

/*
 * Helper: Extract argument from bounce
 */
static Obj* get_bounce_arg(Obj* bounce) {
    if (!is_bounce(bounce)) return NULL;
    return bounce->b;
}

/* ============== Public API ============== */

/*
 * prim_is_bounce: Check if value is a bounce (thunk)
 * Args: obj
 * Returns: Boolean indicating if obj is a bounce
 */
Obj* prim_is_bounce(Obj* obj) {
    return mk_bool(is_bounce(obj));
}

/*
 * prim_bounce: Create a bounce (thunk) for delayed computation
 *
 * This is a wrapper that can be used in user code.
 * However, the actual bounce creation is typically done by the compiler
 * when generating tail-recursive code.
 *
 * Args: func_symbol, arg
 * Returns: A bounce object wrapping the function and argument
 *
 * Note: This is a simplified version. In practice, the compiler
 * would generate more efficient bounce objects.
 */
Obj* prim_bounce(Obj* func, Obj* arg) {
    /* For now, we can't properly wrap arbitrary functions */
    /* This would need compiler support to work correctly */
    fprintf(stderr, "prim_bounce: Compiler support needed for arbitrary function wrapping\n");
    return NULL;
}

/*
 * prim_trampoline: Execute thunks until final result
 *
 * The trampoline repeatedly executes bounce objects until
 * a non-bounce value is returned, which is the final result.
 *
 * IMPLEMENTATION NOTE:
 * This function handles both:
 * 1. Legacy bounce objects (TAG_BOUNCE with mark field hack) - for backwards compat
 * 2. Closures that return closures (CEK-style trampolining)
 *
 * For new code, prefer using cek_trampoline() directly.
 *
 * Args: initial_value
 * Returns: Final result after executing all thunks
 *
 * Example:
 *   (trampoline (recursive-fact 10))
 *   -> 3628800
 */
Obj* prim_trampoline(Obj* initial) {
    Obj* current = initial;
    int iterations = 0;
    const int MAX_ITERATIONS = 10000000;  /* Prevent infinite loops */

    while (iterations < MAX_ITERATIONS) {
        /* Check for legacy bounce objects */
        if (is_bounce(current)) {
            Obj* (*func)(Obj*) = get_bounce_func(current);
            Obj* arg = get_bounce_arg(current);

            if (!func) {
                fprintf(stderr, "trampoline: Invalid bounce object (NULL function)\n");
                return NULL;
            }

            /* Execute the thunk */
            current = func(arg);
            iterations++;
            continue;
        }

        /* Check for closure (CEK-style thunk) */
        if (current && IS_BOXED(current) && current->tag == TAG_CLOSURE) {
            /* Use CEK trampoline for closures */
            current = cek_trampoline(current);
            /* cek_trampoline handles the full execution, so we're done */
            return current;
        }

        /* Not a bounce or closure - we're done */
        break;
    }

    if (iterations >= MAX_ITERATIONS) {
        fprintf(stderr, "trampoline: Exceeded maximum iterations (possible infinite loop)\n");
    }

    return current;
}

/* ============== Example: Factorial with Trampoline ============== */

/*
 * This is an example of how to implement factorial using trampolines.
 * In practice, this would be generated by the compiler for tail-recursive functions.
 */

/*
 * FactState: State for factorial computation
 */
typedef struct {
    long n;          /* Current number */
    long accumulator; /* Running product */
    int done;        /* Completion flag */
} FactState;

/*
 * fact_step: Single step of factorial computation
 * Returns either a bounce (continue) or the result (done)
 */
static Obj* fact_step(Obj* state_obj) {
    if (!IS_BOXED(state_obj)) return NULL;

    /* Extract state from Obj (this is a simplified approach) */
    /* In practice, we'd have a proper way to encode state */
    FactState* state = (FactState*)state_obj->ptr;

    if (state->n <= 1) {
        /* Base case: return the accumulated result */
        Obj* result = mk_int(state->accumulator);
        state->done = 1;
        return result;
    } else {
        /* Recursive case: decrement and multiply */
        state->n--;
        state->accumulator *= (state->n + 1);

        /* Return bounce to continue */
        /* We use the state object itself as the "bounce" */
        return state_obj;
    }
}

/*
 * prim_fact_trampoline: Factorial using trampoline
 * Args: n (integer)
 * Returns: n!
 *
 * This is an example of using trampolines for stack-safe recursion.
 */
Obj* prim_fact_trampoline(Obj* n_obj) {
    long n = obj_to_int(n_obj);

    if (n < 0) {
        fprintf(stderr, "fact_trampoline: negative input\n");
        return mk_int(0);
    }

    if (n <= 1) {
        return mk_int(1);
    }

    /* Allocate state */
    FactState* state = malloc(sizeof(FactState));
    state->n = n;
    state->accumulator = 1;
    state->done = 0;

    /* Create a bounce object to hold the state */
    /* For this example, we use a simple approach */
    omni_ensure_global_region();
    Obj* state_obj = mk_pair(mk_int((long)state), NULL);
    state_obj->ptr = state;
    state_obj->tag = TAG_BOUNCE;
    state_obj->mark = (int)(intptr_t)fact_step;

    /* Run trampoline */
    Obj* result = prim_trampoline(state_obj);

    /* Cleanup */
    free(state);

    return result;
}

/* ============== Example: Mutual Recursion ============== */

/*
 * Even/Odd using trampolines (classic mutual recursion example)
 */

typedef enum {
    EVEN_OP,
    ODD_OP
} MutRecOp;

typedef struct {
    MutRecOp op;
    long n;
} MutRecState;

static Obj* even_step(Obj* state_obj);
static Obj* odd_step(Obj* state_obj);

static Obj* even_step(Obj* state_obj) {
    MutRecState* state = (MutRecState*)state_obj->ptr;

    if (state->n == 0) {
        return mk_int(1);  /* true */
    } else {
        state->n--;
        state->op = ODD_OP;
        return state_obj;  /* bounce to odd */
    }
}

static Obj* odd_step(Obj* state_obj) {
    MutRecState* state = (MutRecState*)state_obj->ptr;

    if (state->n == 0) {
        return mk_int(0);  /* false */
    } else {
        state->n--;
        state->op = EVEN_OP;
        return state_obj;  /* bounce to even */
    }
}

/*
 * prim_even_trampoline: Even using trampoline
 */
Obj* prim_even_trampoline(Obj* n_obj) {
    long n = obj_to_int(n_obj);

    MutRecState* state = malloc(sizeof(MutRecState));
    state->op = EVEN_OP;
    state->n = n;

    omni_ensure_global_region();
    Obj* state_obj = mk_pair(NULL, NULL);
    state_obj->ptr = state;
    state_obj->tag = TAG_BOUNCE;
    state_obj->mark = (int)(intptr_t)even_step;

    Obj* result = prim_trampoline(state_obj);

    free(state);
    return result;
}

/*
 * prim_odd_trampoline: Odd using trampoline
 */
Obj* prim_odd_trampoline(Obj* n_obj) {
    long n = obj_to_int(n_obj);

    MutRecState* state = malloc(sizeof(MutRecState));
    state->op = ODD_OP;
    state->n = n;

    omni_ensure_global_region();
    Obj* state_obj = mk_pair(NULL, NULL);
    state_obj->ptr = state;
    state_obj->tag = TAG_BOUNCE;
    state_obj->mark = (int)(intptr_t)odd_step;

    Obj* result = prim_trampoline(state_obj);

    free(state);
    return result;
}
