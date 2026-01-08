/*
 * iterator.c - Iterator and sequence operations for OmniLisp
 *
 * Implements lazy iteration and collection primitives.
 *
 * API:
 *   - first: Get first element of a sequence
 *   - rest: Get rest of a sequence (tail)
 *   - has_next: Check if iterator has more elements
 *   - iterate: Create infinite lazy sequence from function
 *   - iter_next: Get next value from iterator
 *   - take: Take n elements from sequence
 *   - collect: Collect elements into a collection
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../include/omni.h"

/* ============== Basic Sequence Operations ============== */

/*
 * prim_first: Get first element of a sequence
 *
 * Args: seq - A list or pair
 * Returns: First element (car) or NULL if empty
 *
 * Example:
 *   (first '(1 2 3)) = 1
 *   (first '()) = NULL
 */
Obj* prim_first(Obj* seq) {
    if (!seq) return NULL;

    if (IS_BOXED(seq) && seq->tag == TAG_PAIR) {
        return seq->a;
    }

    /* For other types, return NULL (or could error) */
    return NULL;
}

/*
 * prim_rest: Get rest of a sequence (tail)
 *
 * Args: seq - A list or pair
 * Returns: Rest of sequence (cdr) or NULL if empty
 *
 * Example:
 *   (rest '(1 2 3)) = (2 3)
 *   (rest '(1)) = NULL
 *   (rest '()) = NULL
 */
Obj* prim_rest(Obj* seq) {
    if (!seq) return NULL;

    if (IS_BOXED(seq) && seq->tag == TAG_PAIR) {
        return seq->b;
    }

    return NULL;
}

/*
 * prim_has_next: Check if iterator has more elements
 *
 * Args: iter_obj - Iterator object
 * Returns: Boolean indicating if more elements exist
 *
 * Example:
 *   (has-next (iterate inc 0)) = true
 */
Obj* prim_has_next(Obj* iter_obj) {
    if (!iter_obj) return mk_bool(0);

    /* Check if it's an iterator (stored as a boxed pair) */
    if (IS_BOXED(iter_obj) && iter_obj->tag == TAG_PAIR) {
        /* Iterator is represented as (current . fn) pair */
        return mk_bool(1);  /* Always has next until exhausted */
    }

    /* For lists/pairs, check if not NULL */
    if (IS_BOXED(iter_obj) && iter_obj->tag == TAG_PAIR) {
        return mk_bool(1);
    }

    return mk_bool(0);
}

/* ============== Lazy Iteration ============== */

/*
 * prim_iterate: Create an infinite lazy sequence from a function and seed
 *
 * Args:
 *   - fn: Closure function that takes current value and returns next
 *   - seed: Starting value
 *
 * Returns: Iterator object (represented as pair: current . fn)
 *
 * Example:
 *   (iterate inc 0) produces sequence: 0, 1, 2, 3, ...
 */
Obj* prim_iterate(Obj* fn, Obj* seed) {
    if (!fn) return NULL;

    /* Represent iterator as (current . fn) pair */
    /* The iter_next function will update this pair */
    return mk_pair(seed, fn);
}

/*
 * prim_iter_next: Get the next value from an iterator
 *
 * Args: iter_obj - Iterator from prim_iterate (pair: current . fn)
 * Returns: Current value and advances the iterator
 *
 * Example:
 *   (def i (iterate inc 0))
 *   (iter-next i) = 0
 *   (iter-next i) = 1
 *   (iter-next i) = 2
 *
 * Note: This is a simplified implementation. For a true stateful iterator,
 * we'd need to use mutable cells or a different representation.
 */
Obj* prim_iter_next(Obj* iter_obj) {
    if (!iter_obj) return NULL;

    /* Extract iterator from pair */
    if (!IS_BOXED(iter_obj) || iter_obj->tag != TAG_PAIR) {
        return NULL;
    }

    Obj* current = iter_obj->a;
    Obj* fn = iter_obj->b;

    if (!current || !fn) return NULL;

    /* Advance: apply function to get next value */
    if (IS_BOXED(fn) && fn->tag == TAG_CLOSURE) {
        Closure* closure = (Closure*)fn->ptr;
        if (closure && closure->fn) {
            /* Create args array with current value */
            Obj* args[] = { current };
            /* Call the closure function with captures and args */
            Obj* next_value = closure->fn(closure->captures, args, 1);
            /* Update the iterator pair with new value */
            iter_obj->a = next_value;
        }
    }

    return current;
}

/* ============== Sequence Operations ============== */

/*
 * prim_take: Take n elements from a sequence/iterator
 *
 * Args:
 *   - n: Number of elements to take
 *   - seq: Iterator or list
 *
 * Returns: List of first n elements
 *
 * For iterators, advances the iterator. For lists, returns first n elements.
 *
 * Example:
 *   (take 3 (iterate inc 0)) = (0 1 2)
 *   (take 2 '(1 2 3 4)) = (1 2)
 */
Obj* prim_take(long n, Obj* seq) {
    if (n <= 0 || !seq) return NULL;

    omni_ensure_global_region();

    Obj* result = NULL;
    Obj* tail = NULL;

    /* Check if it's an iterator (pair with function as cdr) */
    if (IS_BOXED(seq) && seq->tag == TAG_PAIR &&
        IS_BOXED(seq->b) && seq->b->tag == TAG_CLOSURE) {
        /* Iterator: call iter_next n times */
        Obj* iter = seq;
        for (long i = 0; i < n; i++) {
            Obj* val = prim_iter_next(iter);
            if (!val) break;  /* Iterator exhausted */

            Obj* new_pair = mk_pair(val, NULL);
            if (!result) {
                result = new_pair;
                tail = new_pair;
            } else {
                tail->b = new_pair;
                tail = new_pair;
            }
        }
        return result;
    }

    /* For lists/pairs: take first n elements */
    Obj* current = seq;
    for (long i = 0; i < n && current; i++) {
        if (!IS_BOXED(current) || current->tag != TAG_PAIR) break;

        Obj* val = current->a;
        Obj* new_pair = mk_pair(val, NULL);

        if (!result) {
            result = new_pair;
            tail = new_pair;
        } else {
            tail->b = new_pair;
            tail = new_pair;
        }

        current = current->b;
    }

    return result;
}

/* ============== Collection Operations ============== */

/*
 * prim_collect: Collect elements from sequence into collection
 *
 * Args:
 *   - seq: Iterator or list
 *   - kind: Symbol indicating collection type ('list, 'array, 'string)
 *
 * Returns: Collected elements in specified collection type
 *
 * Example:
 *   (collect (range 10)) = array [0, 1, 2, ..., 9]
 *   (collect (range 10) 'list) = list (0 1 2 ... 9)
 */
Obj* prim_collect(Obj* seq, Obj* kind) {
    if (!seq) return NULL;

    /* Determine collection type (default: list) */
    int is_list = 1;  /* Default */
    int is_array = 0;
    int is_string = 0;

    if (kind && IS_BOXED(kind) && kind->tag == TAG_SYM) {
        const char* kind_str = (const char*)kind->ptr;
        if (strcmp(kind_str, "array") == 0) {
            is_array = 1;
            is_list = 0;
        } else if (strcmp(kind_str, "string") == 0) {
            is_string = 1;
            is_list = 0;
        }
    }

    /* Check if it's an iterator (pair with closure) */
    if (IS_BOXED(seq) && seq->tag == TAG_PAIR &&
        IS_BOXED(seq->b) && seq->b->tag == TAG_CLOSURE) {
        /* Collect from iterator - for now, just collect into list */
        /* A full implementation would need to handle array/string */
        Obj* result = NULL;
        Obj* tail = NULL;

        /* Limit iterations to avoid infinite loops */
        int max_iters = 1000;
        int i = 0;

        Obj* iter = seq;
        while (i < max_iters) {
            Obj* val = prim_iter_next(iter);
            if (!val) break;

            Obj* new_pair = mk_pair(val, NULL);
            if (!result) {
                result = new_pair;
                tail = new_pair;
            } else {
                tail->b = new_pair;
                tail = new_pair;
            }
            i++;
        }

        return result;
    }

    /* For lists: return as-is for list type */
    if (is_list) {
        return seq;
    }

    /* For other types, just return the list for now */
    return seq;
}

/* ============== Range ============== */

/*
 * prim_range: Create a range iterator (0 to n-1)
 *
 * Args: n - Upper bound (exclusive)
 * Returns: Iterator producing 0, 1, 2, ..., n-1
 *
 * Example:
 *   (range 5) produces: 0, 1, 2, 3, 4
 */
Obj* prim_range(long n) {
    omni_ensure_global_region();

    /* Create a closure that increments a counter */
    /* For simplicity, this returns a list for now */
    /* A full implementation would create a proper iterator */

    if (n <= 0) return NULL;

    Obj* result = NULL;
    Obj* tail = NULL;

    for (long i = 0; i < n; i++) {
        Obj* val = mk_int(i);
        Obj* new_pair = mk_pair(val, NULL);

        if (!result) {
            result = new_pair;
            tail = new_pair;
        } else {
            tail->b = new_pair;
            tail = new_pair;
        }
    }

    return result;
}
