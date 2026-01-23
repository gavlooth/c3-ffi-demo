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
 *
 * Issue 14 P4: Generator Integration
 *   - make_generator: Create a generator from a producer thunk
 *   - generator_next: Get next value from generator
 *   - yield: Yield a value from within a generator
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../include/omni.h"
#include "memory/continuation.h"

/* ============== Basic Sequence Operations ============== */

// TESTED
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

// TESTED
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

// TESTED
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

// TESTED
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
// TESTED

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

// TESTED
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
 *           Default (NULL or omitted): 'array per SYNTAX_REVISION.md
 *
 * Returns: Collected elements in specified collection type
 *
 * Example:
 *   (collect (range 10)) = array [0, 1, 2, ..., 9]
 *   (collect (range 10) 'array) = array [0, 1, 2, ..., 9]
 *   (collect (range 10) 'list) = list (0 1 2 ... 9)
 */
Obj* prim_collect(Obj* seq, Obj* kind) {
    if (!seq) return NULL;

    /* Determine collection type (default: array per SYNTAX_REVISION.md) */
    int is_list = 0;
    int is_array = 1;  /* Default per spec */
    int is_string = 0;

    if (kind && IS_BOXED(kind) && kind->tag == TAG_SYM) {
        const char* kind_str = (const char*)kind->ptr;
        if (strcmp(kind_str, "list") == 0) {
            is_list = 1;
            is_array = 0;
            is_string = 0;
        } else if (strcmp(kind_str, "array") == 0) {
            is_array = 1;
            is_list = 0;
            is_string = 0;
        } else if (strcmp(kind_str, "string") == 0) {
            is_string = 1;
            is_list = 0;
            is_array = 0;
        }
    }

    omni_ensure_global_region();

    /* First, collect all elements into a temporary list */
    Obj* temp_list = NULL;
    Obj* tail = NULL;
    int count = 0;

    /* Check if it's an iterator (pair with closure) */
    if (IS_BOXED(seq) && seq->tag == TAG_PAIR &&
        IS_BOXED(seq->b) && seq->b->tag == TAG_CLOSURE) {
        /* Collect from iterator */
        Obj* iter = seq;
        int max_iters = 1000;  /* Limit to avoid infinite loops */
        int i = 0;

        while (i < max_iters) {
            Obj* val = prim_iter_next(iter);
            if (!val) break;

            Obj* new_pair = mk_pair(val, NULL);
            if (!temp_list) {
                temp_list = new_pair;
                tail = new_pair;
            } else {
                tail->b = new_pair;
                tail = new_pair;
            }
            count++;
            i++;
        }
    } else {
        /* For lists, use as-is (but we may need to convert) */
        temp_list = seq;
        /* Count elements */
        Obj* curr = temp_list;
        while (IS_BOXED(curr) && curr->tag == TAG_PAIR) {
            count++;
            curr = curr->b;
        }
    }

    /* Now convert to the requested collection type */
    if (is_list) {
        /* Return as-is (already a list) */
        return temp_list;
    }

    if (is_array) {
        /* Convert list to array */
        /* First, create an empty array */
        Obj* result = mk_array(count);
        if (!result) return NULL;

        /* Fill the array with elements */
        Obj* curr = temp_list;
        for (int i = 0; i < count && IS_BOXED(curr) && curr->tag == TAG_PAIR; i++) {
            /* Use array_set to add elements */
            array_push(result, curr->a);
            curr = curr->b;
        }

        return result;
    }

    if (is_string) {
        /* Convert list of characters to string */
        /* Allocate buffer for string (count chars + null terminator) */
        char* buffer = malloc(count + 1);
        if (!buffer) return NULL;

        /* Fill buffer with characters */
        Obj* curr = temp_list;
        for (int i = 0; i < count && IS_BOXED(curr) && curr->tag == TAG_PAIR; i++) {
            Obj* elem = curr->a;
            /* Extract character value from integer */
            /* obj_to_int handles both immediate and boxed integers */
            long char_val = obj_to_int(elem);
            buffer[i] = (char)char_val;
            curr = curr->b;
        }
        buffer[count] = '\0';

        /* Create string object */
        Obj* result = mk_string(buffer);
        free(buffer);
        return result;
    }

    /* Default: return as-is (list) */
    return temp_list;
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

/* ============================================================
 * Generator-Based Iteration (Issue 14 P4)
 * ============================================================ */

/*
 * prim_make_generator: Create a generator from a producer thunk
 *
 * Args: producer - A zero-arg closure that yields values via (yield val)
 * Returns: Generator object wrapped in Obj
 *
 * Example:
 *   (def g (make-generator (fn [] (yield 1) (yield 2) (yield 3))))
 *   (generator-next g) = 1
 *   (generator-next g) = 2
 *   (generator-next g) = 3
 *   (generator-next g) = NULL (exhausted)
 */
Obj* prim_make_generator(Obj* producer) {
    if (!producer) return NULL;

    /* Check that producer is a closure */
    if (!IS_BOXED(producer) || producer->tag != TAG_CLOSURE) {
        return NULL;
    }

    Generator* g = generator_create(producer);
    if (!g) return NULL;

    return mk_generator_obj(g);
}

/*
 * prim_generator_next: Get the next value from a generator
 *
 * Args: gen_obj - Generator Obj (TAG_GENERATOR)
 * Returns: Next yielded value, or NULL if exhausted
 *
 * Example:
 *   (generator-next gen) = value or nil
 */
Obj* prim_generator_next(Obj* gen_obj) {
    if (!gen_obj) return NULL;

    /* Extract generator from Obj */
    if (!IS_BOXED(gen_obj) || gen_obj->tag != TAG_GENERATOR) {
        return NULL;
    }

    Generator* g = (Generator*)gen_obj->ptr;
    if (!g) return NULL;

    return generator_next(g);
}

/*
 * prim_generator_done: Check if generator is exhausted
 *
 * Args: gen_obj - Generator Obj (TAG_GENERATOR)
 * Returns: Boolean true if generator is done
 */
Obj* prim_generator_done(Obj* gen_obj) {
    if (!gen_obj) return mk_bool(1);  /* NULL = done */

    if (!IS_BOXED(gen_obj) || gen_obj->tag != TAG_GENERATOR) {
        return mk_bool(1);  /* Not a generator = done */
    }

    Generator* g = (Generator*)gen_obj->ptr;
    return mk_bool(generator_is_done(g));
}

/*
 * prim_yield: Yield a value from within a generator
 *
 * NOTE: This function is defined in runtime.c using the effect system.
 * The effect-based version (effect_perform(EFFECT_YIELD, value)) is the
 * canonical implementation that integrates with the handler infrastructure.
 *
 * To use the direct generator_yield() for frame-based generators, call
 * generator_yield() directly rather than prim_yield().
 *
 * Example:
 *   (fn [] (yield 1) (yield 2) (yield 3))
 */
/* prim_yield is defined in runtime.c - uses effect_perform(EFFECT_YIELD, value) */

/*
 * Helper: Check if an Obj is a generator
 */
int is_generator(Obj* obj) {
    return obj && IS_BOXED(obj) && obj->tag == TAG_GENERATOR;
}

/*
 * prim_iter_next_unified: Get next value from iterator or generator
 *
 * Supports both:
 * 1. Pair-based iterators: (current . fn)
 * 2. Generator-based iterators: TAG_GENERATOR
 *
 * This provides backward compatibility while enabling generator usage.
 */
Obj* prim_iter_next_unified(Obj* iter_obj) {
    if (!iter_obj) return NULL;

    /* Check if it's a generator */
    if (IS_BOXED(iter_obj) && iter_obj->tag == TAG_GENERATOR) {
        return prim_generator_next(iter_obj);
    }

    /* Fall back to pair-based iterator */
    return prim_iter_next(iter_obj);
}

/*
 * prim_take_unified: Take n elements from iterator or generator
 *
 * Supports both pair-based iterators and generators.
 */
Obj* prim_take_unified(long n, Obj* seq) {
    if (n <= 0 || !seq) return NULL;

    omni_ensure_global_region();

    Obj* result = NULL;
    Obj* tail = NULL;

    /* Check if it's a generator */
    if (IS_BOXED(seq) && seq->tag == TAG_GENERATOR) {
        Generator* g = (Generator*)seq->ptr;
        for (long i = 0; i < n && !generator_is_done(g); i++) {
            Obj* val = generator_next(g);
            if (!val) break;

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

    /* Fall back to existing prim_take behavior */
    return prim_take(n, seq);
}
