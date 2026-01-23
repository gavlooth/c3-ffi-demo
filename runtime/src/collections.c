/*
 * collections.c - Extended collection utilities for OmniLisp
 *
 * Issue 28 P1: Collection Utilities
 *
 * Extended operations for lists, arrays, and dictionaries:
 *   - prim_sort, prim_sort_by: Sorting with optional key function
 *   - prim_reverse: Reverse list/array
 *   - prim_group_by: Group elements by key function
 *   - prim_partition: Split by predicate
 *   - prim_take, prim_drop: Take/drop first n elements
 *   - prim_take_while, prim_drop_while: Take/drop while predicate true
 *   - prim_flatten, prim_flatten_deep: Flatten nested collections
 *   - prim_zip, prim_unzip: Pair/unpair elements
 *   - prim_frequencies: Count occurrences
 *   - prim_distinct: Remove duplicates
 *   - prim_interleave, prim_interpose: Element interleaving
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "../include/omni.h"
#include "internal_types.h"

/* ============== Helper Functions ============== */

/*
 * list_to_array_internal - Convert a list to an array of Obj*
 * Returns the length and sets *out to the array (caller must free)
 */
static long list_to_array_internal(Obj* list, Obj*** out) {
    /* Count elements */
    long len = 0;
    Obj* p = list;
    while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
        len++;
        p = p->b;
    }

    if (len == 0) {
        *out = NULL;
        return 0;
    }

    Obj** arr = malloc(sizeof(Obj*) * len);
    p = list;
    for (long i = 0; i < len; i++) {
        arr[i] = p->a;
        p = p->b;
    }

    *out = arr;
    return len;
}

/*
 * array_to_list_internal - Convert an array of Obj* to a list
 */
static Obj* array_to_list_internal(Obj** arr, long len) {
    Obj* result = NULL;
    for (long i = len - 1; i >= 0; i--) {
        result = mk_pair(arr[i], result);
    }
    return result;
}

/*
 * obj_compare_default - Default comparison function for sorting
 * Returns: negative if a < b, 0 if a == b, positive if a > b
 */
static int obj_compare_default(Obj* a, Obj* b) {
    /* Handle NULLs */
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;

    /* Handle immediates */
    if (IS_IMMEDIATE_INT(a) && IS_IMMEDIATE_INT(b)) {
        long ia = INT_IMM_VALUE(a);
        long ib = INT_IMM_VALUE(b);
        return (ia > ib) - (ia < ib);
    }

    /* Handle numeric types */
    double da = obj_to_float(a);
    double db = obj_to_float(b);
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* Global comparator for qsort (not thread-safe, but simple) */
static Obj* g_sort_comparator = NULL;
static Obj* g_sort_key_fn = NULL;

static int qsort_compare_default(const void* pa, const void* pb) {
    Obj* a = *(Obj**)pa;
    Obj* b = *(Obj**)pb;
    return obj_compare_default(a, b);
}

static int qsort_compare_custom(const void* pa, const void* pb) {
    Obj* a = *(Obj**)pa;
    Obj* b = *(Obj**)pb;

    if (!g_sort_comparator) return obj_compare_default(a, b);

    /* Call comparator function with (a, b) */
    Obj* args[2] = { a, b };
    Obj* result = call_closure(g_sort_comparator, args, 2);

    return (int)obj_to_int(result);
}

static int qsort_compare_by_key(const void* pa, const void* pb) {
    Obj* a = *(Obj**)pa;
    Obj* b = *(Obj**)pb;

    if (!g_sort_key_fn) return obj_compare_default(a, b);

    /* Extract keys */
    Obj* args_a[1] = { a };
    Obj* args_b[1] = { b };
    Obj* key_a = call_closure(g_sort_key_fn, args_a, 1);
    Obj* key_b = call_closure(g_sort_key_fn, args_b, 1);

    return obj_compare_default(key_a, key_b);
}

/* ============== Sorting ============== */

/*
 * prim_sort - Sort list/array using default comparator
 *
 * Args: collection (list or array)
 * Returns: new sorted collection
 */
// TESTED - test_sort.omni
Obj* prim_sort(Obj* coll) {
    if (!coll) return NULL;

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr || arr->len == 0) return mk_array(0);

        /* Copy to new array */
        Obj* result = mk_array(arr->len);
        for (int i = 0; i < arr->len; i++) {
            array_push(result, arr->data[i]);
        }

        /* Sort in place */
        Array* res_arr = (Array*)result->ptr;
        qsort(res_arr->data, res_arr->len, sizeof(Obj*), qsort_compare_default);

        return result;
    }

    /* Handle list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj** arr;
        long len = list_to_array_internal(coll, &arr);
        if (len == 0) return NULL;

        qsort(arr, len, sizeof(Obj*), qsort_compare_default);

        Obj* result = array_to_list_internal(arr, len);
        free(arr);
        return result;
    }

    return mk_nothing();
}

/*
 * prim_sort_by - Sort by key function
 *
 * Args: key_fn (function to extract sort key), collection
 * Returns: new sorted collection
 */
// TESTED - tests/test_sort_by.omni
Obj* prim_sort_by(Obj* key_fn, Obj* coll) {
    if (!coll) return NULL;

    g_sort_key_fn = key_fn;

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr || arr->len == 0) {
            g_sort_key_fn = NULL;
            return mk_array(0);
        }

        /* Copy to new array */
        Obj* result = mk_array(arr->len);
        for (int i = 0; i < arr->len; i++) {
            array_push(result, arr->data[i]);
        }

        /* Sort in place */
        Array* res_arr = (Array*)result->ptr;
        qsort(res_arr->data, res_arr->len, sizeof(Obj*), qsort_compare_by_key);

        g_sort_key_fn = NULL;
        return result;
    }

    /* Handle list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj** arr;
        long len = list_to_array_internal(coll, &arr);
        if (len == 0) {
            g_sort_key_fn = NULL;
            return NULL;
        }

        qsort(arr, len, sizeof(Obj*), qsort_compare_by_key);

        Obj* result = array_to_list_internal(arr, len);
        free(arr);
        g_sort_key_fn = NULL;
        return result;
    }

    g_sort_key_fn = NULL;
    return mk_nothing();
}

// TESTED - tests/test_sort_with.lisp
/*
 * prim_sort_with - Sort with custom comparator
 *
 * Args: comparator (function returning -1, 0, 1), collection
 * Returns: new sorted collection
 */
Obj* prim_sort_with(Obj* cmp_fn, Obj* coll) {
    if (!coll) return NULL;

    g_sort_comparator = cmp_fn;

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr || arr->len == 0) {
            g_sort_comparator = NULL;
            return mk_array(0);
        }

        /* Copy to new array */
        Obj* result = mk_array(arr->len);
        for (int i = 0; i < arr->len; i++) {
            array_push(result, arr->data[i]);
        }

        /* Sort in place */
        Array* res_arr = (Array*)result->ptr;
        qsort(res_arr->data, res_arr->len, sizeof(Obj*), qsort_compare_custom);

        g_sort_comparator = NULL;
        return result;
    }

    /* Handle list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj** arr;
        long len = list_to_array_internal(coll, &arr);
        if (len == 0) {
            g_sort_comparator = NULL;
            return NULL;
        }

        qsort(arr, len, sizeof(Obj*), qsort_compare_custom);

        Obj* result = array_to_list_internal(arr, len);
        free(arr);
        g_sort_comparator = NULL;
        return result;
    }

    g_sort_comparator = NULL;
    return mk_nothing();
}

/* ============== Reverse ============== */

// TESTED - test_reverse.omni
/*
 * prim_reverse - Reverse list/array (non-destructive)
 *
 * Args: collection (list or array)
 * Returns: new reversed collection
 */
Obj* prim_reverse(Obj* coll) {
    if (!coll) return NULL;

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr || arr->len == 0) return mk_array(0);

        Obj* result = mk_array(arr->len);
        for (int i = arr->len - 1; i >= 0; i--) {
            array_push(result, arr->data[i]);
        }
        return result;
    }

    /* Handle list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj* result = NULL;
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            result = mk_pair(p->a, result);
            p = p->b;
        }
        return result;
    }

    return mk_nothing();
}

/* ============== Group By ============== */

/*
 * prim_group_by - Group elements by key function
 *
 * Args: key_fn (function to extract group key), collection
 * Returns: dict mapping keys to lists of elements
 */
// TESTED - tests/test_group_by.lisp
Obj* prim_group_by(Obj* key_fn, Obj* coll) {
    if (!coll) return mk_dict();

    Obj* result = mk_dict();

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr) return result;

        for (int i = 0; i < arr->len; i++) {
            Obj* elem = arr->data[i];
            Obj* args[1] = { elem };
            Obj* key = call_closure(key_fn, args, 1);

            /* Get existing list or create new */
            Obj* existing = dict_get(result, key);
            if (!existing || is_nothing(existing)) {
                existing = mk_array(4);
                dict_set(result, key, existing);
            }
            array_push(existing, elem);
        }
        return result;
    }

    /* Handle list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            Obj* elem = p->a;
            Obj* args[1] = { elem };
            Obj* key = call_closure(key_fn, args, 1);

            Obj* existing = dict_get(result, key);
            if (!existing || is_nothing(existing)) {
                existing = mk_array(4);
                dict_set(result, key, existing);
            }
            array_push(existing, elem);

            p = p->b;
        }
        return result;
    }

    return result;
}

/* ============== Partition ============== */

// TESTED - tests/test_partition.omni
/*
 * prim_partition - Split by predicate
 *
 * Args: pred (predicate function), collection
 * Returns: array of [matches, non-matches]
 */
Obj* prim_partition(Obj* pred, Obj* coll) {
    Obj* matches = mk_array(8);
    Obj* non_matches = mk_array(8);

    if (!coll) {
        Obj* result = mk_array(2);
        array_push(result, matches);
        array_push(result, non_matches);
        return result;
    }

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (arr) {
            for (int i = 0; i < arr->len; i++) {
                Obj* elem = arr->data[i];
                Obj* args[1] = { elem };
                Obj* res = call_closure(pred, args, 1);
                if (is_truthy(res)) {
                    array_push(matches, elem);
                } else {
                    array_push(non_matches, elem);
                }
            }
        }
    }
    /* Handle list */
    else if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            Obj* elem = p->a;
            Obj* args[1] = { elem };
            Obj* res = call_closure(pred, args, 1);
            if (is_truthy(res)) {
                array_push(matches, elem);
            } else {
                array_push(non_matches, elem);
            }
            p = p->b;
        }
    }

    Obj* result = mk_array(2);
    array_push(result, matches);
    array_push(result, non_matches);
    return result;
}

/* ============== Take / Drop ============== */

// TESTED - tests/test_take_drop.lisp
/*
 * prim_coll_take - Take first n elements
 *
 * Args: n (count), collection
 * Returns: new collection with first n elements
 */
Obj* prim_coll_take(Obj* n_obj, Obj* coll) {
    long n = obj_to_int(n_obj);
    if (n <= 0 || !coll) return IS_BOXED(coll) && coll->tag == TAG_ARRAY ? mk_array(0) : NULL;

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr) return mk_array(0);

        int take_count = n < arr->len ? (int)n : arr->len;
        Obj* result = mk_array(take_count);
        for (int i = 0; i < take_count; i++) {
            array_push(result, arr->data[i]);
        }
        return result;
    }

    /* Handle list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj** arr = malloc(sizeof(Obj*) * n);
        long count = 0;
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR && count < n) {
            arr[count++] = p->a;
            p = p->b;
        }
        Obj* result = array_to_list_internal(arr, count);
        free(arr);
        return result;
    }

    return mk_nothing();
}

// TESTED - tests/test_take_drop.lisp
/*
 * prim_coll_drop - Drop first n elements
 *
 * Args: n (count), collection
 * Returns: new collection without first n elements
 */
Obj* prim_coll_drop(Obj* n_obj, Obj* coll) {
    long n = obj_to_int(n_obj);
    if (!coll) return IS_BOXED(coll) && coll->tag == TAG_ARRAY ? mk_array(0) : NULL;
    if (n <= 0) return coll;  /* Return original */

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr || n >= arr->len) return mk_array(0);

        Obj* result = mk_array(arr->len - (int)n);
        for (int i = (int)n; i < arr->len; i++) {
            array_push(result, arr->data[i]);
        }
        return result;
    }

    /* Handle list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj* p = coll;
        long count = 0;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR && count < n) {
            p = p->b;
            count++;
        }
        return p;  /* Rest of the list */
    }

    return mk_nothing();
}

/*
 * prim_take_while - Take while predicate is true
 *
 * Args: pred (predicate function), collection
 * Returns: new collection with leading elements that satisfy predicate
 */
Obj* prim_take_while(Obj* pred, Obj* coll) {
    if (!coll) return IS_BOXED(coll) && coll->tag == TAG_ARRAY ? mk_array(0) : NULL;

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr) return mk_array(0);

        Obj* result = mk_array(arr->len);
        for (int i = 0; i < arr->len; i++) {
            Obj* args[1] = { arr->data[i] };
            Obj* res = call_closure(pred, args, 1);
            if (!is_truthy(res)) break;
            array_push(result, arr->data[i]);
        }
        return result;
    }

    /* Handle list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj** arr = malloc(sizeof(Obj*) * 1024);  /* Reasonable max */
        long count = 0;
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR && count < 1024) {
            Obj* args[1] = { p->a };
            Obj* res = call_closure(pred, args, 1);
            if (!is_truthy(res)) break;
            arr[count++] = p->a;
            p = p->b;
        }
        Obj* result = array_to_list_internal(arr, count);
        free(arr);
        return result;
    }

    return mk_nothing();
}

/*
 * prim_drop_while - Drop while predicate is true
 *
 * Args: pred (predicate function), collection
 * Returns: collection starting from first element that fails predicate
 */
Obj* prim_drop_while(Obj* pred, Obj* coll) {
    if (!coll) return IS_BOXED(coll) && coll->tag == TAG_ARRAY ? mk_array(0) : NULL;

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr) return mk_array(0);

        int start = 0;
        for (int i = 0; i < arr->len; i++) {
            Obj* args[1] = { arr->data[i] };
            Obj* res = call_closure(pred, args, 1);
            if (!is_truthy(res)) {
                start = i;
                break;
            }
            start = arr->len;  /* All elements passed */
        }

        Obj* result = mk_array(arr->len - start);
        for (int i = start; i < arr->len; i++) {
            array_push(result, arr->data[i]);
        }
        return result;
    }

    /* Handle list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            Obj* args[1] = { p->a };
            Obj* res = call_closure(pred, args, 1);
            if (!is_truthy(res)) return p;
            p = p->b;
        }
        return NULL;  /* All elements dropped */
    }

    return mk_nothing();
}

/* ============== Flatten ============== */

// TESTED - tests/test_flatten.omni
/*
 * prim_flatten - Flatten one level of nesting
 *
 * Args: collection of collections
 * Returns: flattened collection
 */
Obj* prim_flatten(Obj* coll) {
    if (!coll) return mk_array(0);

    Obj* result = mk_array(16);

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr) return result;

        for (int i = 0; i < arr->len; i++) {
            Obj* elem = arr->data[i];
            /* If element is array, add its elements */
            if (IS_BOXED(elem) && elem->tag == TAG_ARRAY) {
                Array* inner = (Array*)elem->ptr;
                if (inner) {
                    for (int j = 0; j < inner->len; j++) {
                        array_push(result, inner->data[j]);
                    }
                }
            }
            /* If element is list, add its elements */
            else if (IS_BOXED(elem) && elem->tag == TAG_PAIR) {
                Obj* p = elem;
                while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
                    array_push(result, p->a);
                    p = p->b;
                }
            }
            /* Otherwise add element as-is */
            else {
                array_push(result, elem);
            }
        }
        return result;
    }

    /* Handle list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            Obj* elem = p->a;
            /* If element is array, add its elements */
            if (IS_BOXED(elem) && elem->tag == TAG_ARRAY) {
                Array* inner = (Array*)elem->ptr;
                if (inner) {
                    for (int j = 0; j < inner->len; j++) {
                        array_push(result, inner->data[j]);
                    }
                }
            }
            /* If element is list, add its elements */
            else if (IS_BOXED(elem) && elem->tag == TAG_PAIR) {
                Obj* q = elem;
                while (q && IS_BOXED(q) && q->tag == TAG_PAIR) {
                    array_push(result, q->a);
                    q = q->b;
                }
            }
            /* Otherwise add element as-is */
            else {
                array_push(result, elem);
            }
            p = p->b;
        }
        return result;
    }

    return result;
}

/* Helper for flatten_deep: recursively flatten into result array */
static void flatten_into_helper(Obj* x, Obj* result) {
    if (!x) return;

    /* Handle array */
    if (IS_BOXED(x) && x->tag == TAG_ARRAY) {
        Array* arr = (Array*)x->ptr;
        if (arr) {
            for (int i = 0; i < arr->len; i++) {
                flatten_into_helper(arr->data[i], result);
            }
        }
        return;
    }

    /* Handle list */
    if (IS_BOXED(x) && x->tag == TAG_PAIR) {
        Obj* p = x;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            flatten_into_helper(p->a, result);
            p = p->b;
        }
        return;
    }

    /* Non-collection element: add to result */
    array_push(result, x);
}

/*
 * prim_flatten_deep - Flatten all levels of nesting recursively
 *
 * Args: collection
 * Returns: completely flattened array
 */
Obj* prim_flatten_deep(Obj* coll) {
    if (!coll) return mk_array(0);

    Obj* result = mk_array(16);
    flatten_into_helper(coll, result);
    return result;
}

/* ============== Zip / Unzip ============== */

/*
 * prim_zip - Pair elements from two collections
 *
 * Args: coll1, coll2
 * Returns: array of pairs [[a1,b1], [a2,b2], ...]
 */
// TESTED - tests/test_zip_unzip.lisp
Obj* prim_zip(Obj* coll1, Obj* coll2) {
    Obj* result = mk_array(16);

    if (!coll1 || !coll2) return result;

    /* Convert both to arrays for easier iteration */
    Obj** arr1 = NULL;
    Obj** arr2 = NULL;
    long len1 = 0, len2 = 0;

    if (IS_BOXED(coll1) && coll1->tag == TAG_ARRAY) {
        Array* a = (Array*)coll1->ptr;
        if (a) {
            arr1 = a->data;
            len1 = a->len;
        }
    } else if (IS_BOXED(coll1) && coll1->tag == TAG_PAIR) {
        len1 = list_to_array_internal(coll1, &arr1);
    }

    if (IS_BOXED(coll2) && coll2->tag == TAG_ARRAY) {
        Array* a = (Array*)coll2->ptr;
        if (a) {
            arr2 = a->data;
            len2 = a->len;
        }
    } else if (IS_BOXED(coll2) && coll2->tag == TAG_PAIR) {
        len2 = list_to_array_internal(coll2, &arr2);
    }

    /* Zip up to shorter length */
    long min_len = len1 < len2 ? len1 : len2;
    for (long i = 0; i < min_len; i++) {
        Obj* pair = mk_array(2);
        array_push(pair, arr1[i]);
        array_push(pair, arr2[i]);
        array_push(result, pair);
    }

    /* Free if we allocated */
    if (IS_BOXED(coll1) && coll1->tag == TAG_PAIR && arr1) free(arr1);
    if (IS_BOXED(coll2) && coll2->tag == TAG_PAIR && arr2) free(arr2);

    return result;
}

/*
 * prim_unzip - Unpair to two arrays
 *
 * Args: collection of pairs
 * Returns: array of two arrays [[a1,a2,...], [b1,b2,...]]
 */
// TESTED - tests/test_zip_unzip.lisp
Obj* prim_unzip(Obj* coll) {
    Obj* firsts = mk_array(16);
    Obj* seconds = mk_array(16);

    if (!coll) {
        Obj* result = mk_array(2);
        array_push(result, firsts);
        array_push(result, seconds);
        return result;
    }

    /* Handle array of pairs */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (arr) {
            for (int i = 0; i < arr->len; i++) {
                Obj* pair = arr->data[i];
                if (IS_BOXED(pair) && pair->tag == TAG_ARRAY) {
                    Array* p = (Array*)pair->ptr;
                    if (p && p->len >= 2) {
                        array_push(firsts, p->data[0]);
                        array_push(seconds, p->data[1]);
                    }
                } else if (IS_BOXED(pair) && pair->tag == TAG_PAIR) {
                    array_push(firsts, pair->a);
                    array_push(seconds, pair->b);
                }
            }
        }
    }
    /* Handle list of pairs */
    else if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            Obj* pair = p->a;
            if (IS_BOXED(pair) && pair->tag == TAG_ARRAY) {
                Array* pa = (Array*)pair->ptr;
                if (pa && pa->len >= 2) {
                    array_push(firsts, pa->data[0]);
                    array_push(seconds, pa->data[1]);
                }
            } else if (IS_BOXED(pair) && pair->tag == TAG_PAIR) {
                array_push(firsts, pair->a);
                array_push(seconds, pair->b);
            }
            p = p->b;
        }
    }

    Obj* result = mk_array(2);
    array_push(result, firsts);
    array_push(result, seconds);
    return result;
}

/* ============== Frequencies / Distinct ============== */

/*
 * prim_frequencies - Count occurrences of each element
 *
 * Args: collection
 * Returns: dict mapping elements to counts
 */
// TESTED - tests/test_frequencies.omni
Obj* prim_frequencies(Obj* coll) {
    Obj* result = mk_dict();
    if (!coll) return result;

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (arr) {
            for (int i = 0; i < arr->len; i++) {
                Obj* elem = arr->data[i];
                Obj* count = dict_get(result, elem);
                if (!count || is_nothing(count)) {
                    dict_set(result, elem, mk_int(1));
                } else {
                    dict_set(result, elem, mk_int(obj_to_int(count) + 1));
                }
            }
        }
    }
    /* Handle list */
    else if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            Obj* elem = p->a;
            Obj* count = dict_get(result, elem);
            if (!count || is_nothing(count)) {
                dict_set(result, elem, mk_int(1));
            } else {
                dict_set(result, elem, mk_int(obj_to_int(count) + 1));
            }
            p = p->b;
        }
    }

    return result;
}

/*
 * prim_distinct - Remove duplicates preserving order
 *
 * Args: collection
 * Returns: collection with duplicates removed
 */
// TESTED - tests/test_distinct.omni
Obj* prim_distinct(Obj* coll) {
    if (!coll) return mk_array(0);

    Obj* seen = mk_set();
    Obj* result = mk_array(16);

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (arr) {
            for (int i = 0; i < arr->len; i++) {
                Obj* elem = arr->data[i];
                Obj* in_set = set_contains(seen, elem);
                if (!is_truthy(in_set)) {
                    set_add(seen, elem);
                    array_push(result, elem);
                }
            }
        }
        return result;
    }

    /* Handle list - return as list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        long capacity = 64;
        Obj** arr = malloc(sizeof(Obj*) * capacity);
        long count = 0;
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            Obj* elem = p->a;
            Obj* in_set = set_contains(seen, elem);
            if (!is_truthy(in_set)) {
                set_add(seen, elem);
                /* Grow array if needed */
                if (count >= capacity) {
                    capacity *= 2;
                    arr = realloc(arr, sizeof(Obj*) * capacity);
                }
                arr[count++] = elem;
            }
            p = p->b;
        }
        Obj* list_result = array_to_list_internal(arr, count);
        free(arr);
        return list_result;
    }

    return result;
}

/* ============== Interleave / Interpose ============== */

// TESTED - tests/test_interleave.lisp
/*
 * prim_interleave - Interleave elements from two collections
 *
 * Args: coll1, coll2
 * Returns: array [a1, b1, a2, b2, ...]
 */
Obj* prim_interleave(Obj* coll1, Obj* coll2) {
    Obj* result = mk_array(32);

    if (!coll1 && !coll2) return result;

    /* Convert both to arrays */
    Obj** arr1 = NULL;
    Obj** arr2 = NULL;
    long len1 = 0, len2 = 0;

    if (coll1) {
        if (IS_BOXED(coll1) && coll1->tag == TAG_ARRAY) {
            Array* a = (Array*)coll1->ptr;
            if (a) { arr1 = a->data; len1 = a->len; }
        } else if (IS_BOXED(coll1) && coll1->tag == TAG_PAIR) {
            len1 = list_to_array_internal(coll1, &arr1);
        }
    }

    if (coll2) {
        if (IS_BOXED(coll2) && coll2->tag == TAG_ARRAY) {
            Array* a = (Array*)coll2->ptr;
            if (a) { arr2 = a->data; len2 = a->len; }
        } else if (IS_BOXED(coll2) && coll2->tag == TAG_PAIR) {
            len2 = list_to_array_internal(coll2, &arr2);
        }
    }

    /* Interleave up to shorter length */
    long min_len = len1 < len2 ? len1 : len2;
    for (long i = 0; i < min_len; i++) {
        array_push(result, arr1[i]);
        array_push(result, arr2[i]);
    }

    /* Free if we allocated */
    if (coll1 && IS_BOXED(coll1) && coll1->tag == TAG_PAIR && arr1) free(arr1);
    if (coll2 && IS_BOXED(coll2) && coll2->tag == TAG_PAIR && arr2) free(arr2);

    return result;
}

// TESTED - tests/test_interpose.lisp
/*
 * prim_interpose - Insert separator between elements
 *
 * Args: separator, collection
 * Returns: array with separator between each pair of elements
 */
Obj* prim_interpose(Obj* sep, Obj* coll) {
    if (!coll) return mk_array(0);

    Obj* result = mk_array(32);

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr || arr->len == 0) return result;

        array_push(result, arr->data[0]);
        for (int i = 1; i < arr->len; i++) {
            array_push(result, sep);
            array_push(result, arr->data[i]);
        }
        return result;
    }

    /* Handle list */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        Obj* p = coll;
        bool first = true;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            if (!first) {
                array_push(result, sep);
            }
            array_push(result, p->a);
            first = false;
            p = p->b;
        }
        return result;
    }

    return result;
}
