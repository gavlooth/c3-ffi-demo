/* ========== Set Data Structure Tests (Issue 24) ========== */
/*
 * Set implementation using Hashmap as backing store.
 * Set stores elements as both key and value in the hashmap.
 *
 * Tests:
 * - Basic operations: add, remove, contains, size, empty
 * - Set operations: union, intersection, difference, symmetric_difference
 * - Set predicates: subset, superset
 * - Conversions: to_list, to_array, list_to_set, array_to_set
 * - Higher-order functions: map, filter, reduce, foreach
 * - Store barrier: ensure Region Closure Property
 */

#include "test_framework.h"
/* Note: runtime.c is already included by test_main.c */

/* ==================== Basic Operations ==================== */

void test_set_add_contains(void) {
    printf("test_set_add_contains... ");
    
    Obj* s = mk_set();
    ASSERT(s != NULL);
    ASSERT(set_size(s) == 0);
    
    /* Add elements */
    set_add(s, mk_int(1));
    set_add(s, mk_int(2));
    set_add(s, mk_int(3));
    
    ASSERT(set_size(s) == 3);
    ASSERT(obj_to_int(set_contains(s, mk_int(1))) == 1);
    ASSERT(obj_to_int(set_contains(s, mk_int(2))) == 1);
    ASSERT(obj_to_int(set_contains(s, mk_int(3))) == 1);
    ASSERT(obj_to_int(set_contains(s, mk_int(4))) == 0);
    
    PASS();
}

void test_set_add_duplicate(void) {
    printf("test_set_add_duplicate... ");
    
    Obj* s = mk_set();
    set_add(s, mk_int(1));
    set_add(s, mk_int(1));  /* Duplicate */
    set_add(s, mk_int(1));  /* Duplicate again */
    
    /* Size should still be 1 (set semantics) */
    ASSERT(set_size(s) == 1);
    
    PASS();
}

void test_set_remove(void) {
    printf("test_set_remove... ");
    
    Obj* s = mk_set();
    set_add(s, mk_int(1));
    set_add(s, mk_int(2));
    set_add(s, mk_int(3));
    
    /* Remove middle element */
    Obj* removed = set_remove(s, mk_int(2));
    ASSERT(removed != NULL);
    ASSERT(obj_to_int(removed) == 2);
    ASSERT(set_size(s) == 2);
    ASSERT(obj_to_int(set_contains(s, mk_int(2))) == 0);
    
    /* Remove non-existent element */
    removed = set_remove(s, mk_int(99));
    ASSERT(removed == NULL);
    ASSERT(set_size(s) == 2);
    
    PASS();
}

void test_set_size(void) {
    printf("test_set_size... ");
    
    Obj* s = mk_set();
    ASSERT(set_size(s) == 0);
    
    for (int i = 0; i < 100; i++) {
        set_add(s, mk_int(i));
        ASSERT(set_size(s) == i + 1);
    }
    
    ASSERT(set_size(s) == 100);
    
    PASS();
}

void test_set_empty_p(void) {
    printf("test_set_empty_p... ");
    
    Obj* s = mk_set();
    ASSERT(obj_to_int(set_empty_p(s)) == 1);
    ASSERT(set_size(s) == 0);
    
    set_add(s, mk_int(1));
    ASSERT(obj_to_int(set_empty_p(s)) == 0);
    ASSERT(set_size(s) == 1);
    
    /* Remove all elements */
    set_remove(s, mk_int(1));
    ASSERT(obj_to_int(set_empty_p(s)) == 1);
    
    PASS();
}

void test_set_various_types(void) {
    printf("test_set_various_types... ");
    
    Obj* s = mk_set();
    
    /* Add various types */
    set_add(s, mk_int(42));
    set_add(s, mk_string("hello"));
    set_add(s, mk_sym("foo"));
    set_add(s, mk_float(3.14));
    
    ASSERT(set_size(s) == 4);
    ASSERT(obj_to_int(set_contains(s, mk_int(42))) == 1);
    ASSERT(obj_to_int(set_contains(s, mk_string("hello"))) == 1);
    ASSERT(obj_to_int(set_contains(s, mk_sym("foo"))) == 1);
    
    PASS();
}

/* ==================== Set Operations ==================== */

void test_set_union(void) {
    printf("test_set_union... ");
    
    Obj* a = mk_set();
    set_add(a, mk_int(1));
    set_add(a, mk_int(2));
    
    Obj* b = mk_set();
    set_add(b, mk_int(2));
    set_add(b, mk_int(3));
    
    Obj* u = set_union(a, b);
    ASSERT(set_size(u) == 3);
    ASSERT(obj_to_int(set_contains(u, mk_int(1))) == 1);
    ASSERT(obj_to_int(set_contains(u, mk_int(2))) == 1);
    ASSERT(obj_to_int(set_contains(u, mk_int(3))) == 1);
    
    /* Original sets should be unchanged */
    ASSERT(set_size(a) == 2);
    ASSERT(set_size(b) == 2);
    
    PASS();
}

void test_set_union_disjoint(void) {
    printf("test_set_union_disjoint... ");
    
    Obj* a = mk_set();
    set_add(a, mk_int(1));
    set_add(a, mk_int(2));
    
    Obj* b = mk_set();
    set_add(b, mk_int(3));
    set_add(b, mk_int(4));
    
    Obj* u = set_union(a, b);
    ASSERT(set_size(u) == 4);
    
    PASS();
}

void test_set_union_self(void) {
    printf("test_set_union_self... ");
    
    Obj* s = mk_set();
    set_add(s, mk_int(1));
    set_add(s, mk_int(2));
    
    Obj* u = set_union(s, s);
    ASSERT(set_size(u) == 2);
    
    PASS();
}

void test_set_intersection(void) {
    printf("test_set_intersection... ");
    
    Obj* a = mk_set();
    set_add(a, mk_int(1));
    set_add(a, mk_int(2));
    set_add(a, mk_int(3));
    
    Obj* b = mk_set();
    set_add(b, mk_int(2));
    set_add(b, mk_int(3));
    set_add(b, mk_int(4));
    
    Obj* i = set_intersection(a, b);
    ASSERT(set_size(i) == 2);
    ASSERT(obj_to_int(set_contains(i, mk_int(2))) == 1);
    ASSERT(obj_to_int(set_contains(i, mk_int(3))) == 1);
    ASSERT(obj_to_int(set_contains(i, mk_int(1))) == 0);
    ASSERT(obj_to_int(set_contains(i, mk_int(4))) == 0);
    
    PASS();
}

void test_set_intersection_disjoint(void) {
    printf("test_set_intersection_disjoint... ");
    
    Obj* a = mk_set();
    set_add(a, mk_int(1));
    set_add(a, mk_int(2));
    
    Obj* b = mk_set();
    set_add(b, mk_int(3));
    set_add(b, mk_int(4));
    
    Obj* i = set_intersection(a, b);
    ASSERT(set_size(i) == 0);
    ASSERT(obj_to_int(set_empty_p(i)) == 1);
    
    PASS();
}

void test_set_difference(void) {
    printf("test_set_difference... ");
    
    Obj* a = mk_set();
    set_add(a, mk_int(1));
    set_add(a, mk_int(2));
    set_add(a, mk_int(3));
    
    Obj* b = mk_set();
    set_add(b, mk_int(2));
    set_add(b, mk_int(4));
    
    Obj* d = set_difference(a, b);  /* a \ b */
    ASSERT(set_size(d) == 2);
    ASSERT(obj_to_int(set_contains(d, mk_int(1))) == 1);
    ASSERT(obj_to_int(set_contains(d, mk_int(3))) == 1);
    ASSERT(obj_to_int(set_contains(d, mk_int(2))) == 0);
    ASSERT(obj_to_int(set_contains(d, mk_int(4))) == 0);
    
    PASS();
}

void test_set_symmetric_difference(void) {
    printf("test_set_symmetric_difference... ");
    
    Obj* a = mk_set();
    set_add(a, mk_int(1));
    set_add(a, mk_int(2));
    set_add(a, mk_int(3));
    
    Obj* b = mk_set();
    set_add(b, mk_int(2));
    set_add(b, mk_int(3));
    set_add(b, mk_int(4));
    
    Obj* d = set_symmetric_difference(a, b);  /* A △ B */
    ASSERT(set_size(d) == 2);
    ASSERT(obj_to_int(set_contains(d, mk_int(1))) == 1);
    ASSERT(obj_to_int(set_contains(d, mk_int(4))) == 1);
    ASSERT(obj_to_int(set_contains(d, mk_int(2))) == 0);
    ASSERT(obj_to_int(set_contains(d, mk_int(3))) == 0);
    
    PASS();
}

/* ==================== Set Predicates ==================== */

void test_set_subset_p(void) {
    printf("test_set_subset_p... ");
    
    Obj* a = mk_set();
    set_add(a, mk_int(1));
    set_add(a, mk_int(2));
    
    Obj* b = mk_set();
    set_add(b, mk_int(1));
    set_add(b, mk_int(2));
    set_add(b, mk_int(3));
    
    /* a ⊆ b (a is subset of b) */
    ASSERT(obj_to_int(set_subset_p(a, b)) == 1);
    
    /* b ⊈ a (b is not subset of a) */
    ASSERT(obj_to_int(set_subset_p(b, a)) == 0);
    
    /* a ⊆ a (reflexive) */
    ASSERT(obj_to_int(set_subset_p(a, a)) == 1);
    
    /* ∅ ⊆ b (empty set is subset of all) */
    Obj* empty = mk_set();
    ASSERT(obj_to_int(set_subset_p(empty, b)) == 1);
    
    PASS();
}

void test_set_superset_p(void) {
    printf("test_set_superset_p... ");
    
    Obj* a = mk_set();
    set_add(a, mk_int(1));
    set_add(a, mk_int(2));
    
    Obj* b = mk_set();
    set_add(b, mk_int(1));
    set_add(b, mk_int(2));
    set_add(b, mk_int(3));
    
    /* b ⊇ a (b is superset of a) */
    ASSERT(obj_to_int(set_superset_p(b, a)) == 1);
    
    /* a ⊉ b (a is not superset of b) */
    ASSERT(obj_to_int(set_superset_p(a, b)) == 0);
    
    PASS();
}

/* ==================== Set Conversions ==================== */

void test_set_to_list(void) {
    printf("test_set_to_list... ");
    
    Obj* s = mk_set();
    set_add(s, mk_int(1));
    set_add(s, mk_int(2));
    set_add(s, mk_int(3));
    
    Obj* list = set_to_list(s);
    
    /* List should have 3 elements (order may vary) */
    int count = 0;
    int seen[4] = {0, 0, 0, 0};
    
    Obj* curr = list;
    while (curr && IS_BOXED(curr) && curr->tag == TAG_PAIR) {
        long val = obj_to_int(curr->a);
        if (val >= 1 && val <= 3) {
            seen[(int)val] = 1;
            count++;
        }
        curr = curr->b;
    }
    
    ASSERT(count == 3);
    ASSERT(seen[1] == 1);
    ASSERT(seen[2] == 1);
    ASSERT(seen[3] == 1);
    
    PASS();
}

void test_list_to_set(void) {
    printf("test_list_to_set... ");
    
    /* Create list with duplicates */
    Obj* list = mk_pair(mk_int(1),
                     mk_pair(mk_int(2),
                            mk_pair(mk_int(1),    /* Duplicate */
                                   mk_pair(mk_int(3),
                                          mk_pair(mk_int(2), NULL)))));  /* Duplicate */
    
    Obj* s = list_to_set(list);
    
    /* Set should have only unique elements */
    ASSERT(set_size(s) == 3);
    ASSERT(obj_to_int(set_contains(s, mk_int(1))) == 1);
    ASSERT(obj_to_int(set_contains(s, mk_int(2))) == 1);
    ASSERT(obj_to_int(set_contains(s, mk_int(3))) == 1);
    
    PASS();
}

void test_set_to_array(void) {
    printf("test_set_to_array... ");
    
    Obj* s = mk_set();
    set_add(s, mk_int(1));
    set_add(s, mk_int(2));
    set_add(s, mk_int(3));
    
    Obj* arr = set_to_array(s);
    
    /* Array should have 3 elements */
    int len = array_length(arr);
    ASSERT(len == 3);
    
    /* Check all elements are present */
    int seen[4] = {0, 0, 0, 0};
    for (int i = 0; i < len; i++) {
        Obj* elem = array_get(arr, i);
        long val = obj_to_int(elem);
        if (val >= 1 && val <= 3) {
            seen[(int)val] = 1;
        }
    }
    
    ASSERT(seen[1] == 1);
    ASSERT(seen[2] == 1);
    ASSERT(seen[3] == 1);
    
    PASS();
}

void test_array_to_set(void) {
    printf("test_array_to_set... ");
    
    /* Create array with duplicates */
    Obj* arr = mk_array(6);
    array_push(arr, mk_int(1));
    array_push(arr, mk_int(2));
    array_push(arr, mk_int(1));   /* Duplicate */
    array_push(arr, mk_int(3));
    array_push(arr, mk_int(2));   /* Duplicate */
    array_push(arr, mk_int(1));   /* Duplicate */
    
    Obj* s = array_to_set(arr);
    
    /* Set should have only unique elements */
    ASSERT(set_size(s) == 3);
    ASSERT(obj_to_int(set_contains(s, mk_int(1))) == 1);
    ASSERT(obj_to_int(set_contains(s, mk_int(2))) == 1);
    ASSERT(obj_to_int(set_contains(s, mk_int(3))) == 1);
    
    PASS();
}

/* ==================== Higher-Order Functions ==================== */

/* Test closure for set_map */
static Obj* set_map_double_fn(Obj** captures, Obj** args, int argc) {
    (void)captures;
    (void)argc;
    long x = obj_to_int(args[0]);
    return mk_int(x * 2);
}

void test_set_map(void) {
    printf("test_set_map... ");
    
    Obj* s = mk_set();
    set_add(s, mk_int(1));
    set_add(s, mk_int(2));
    set_add(s, mk_int(3));
    
    Obj* fn = mk_closure(set_map_double_fn, NULL, NULL, 0, 1);
    Obj* mapped = set_map(s, fn);
    
    /* Mapped set should have doubled values */
    ASSERT(set_size(mapped) == 3);
    ASSERT(obj_to_int(set_contains(mapped, mk_int(2))) == 1);
    ASSERT(obj_to_int(set_contains(mapped, mk_int(4))) == 1);
    ASSERT(obj_to_int(set_contains(mapped, mk_int(6))) == 1);
    
    PASS();
}

/* Test closure for set_filter (even numbers only) */
static Obj* set_filter_even_fn(Obj** captures, Obj** args, int argc) {
    (void)captures;
    (void)argc;
    long x = obj_to_int(args[0]);
    return mk_bool((x % 2) == 0);
}

void test_set_filter(void) {
    printf("test_set_filter... ");
    
    Obj* s = mk_set();
    set_add(s, mk_int(1));
    set_add(s, mk_int(2));
    set_add(s, mk_int(3));
    set_add(s, mk_int(4));
    set_add(s, mk_int(5));
    
    Obj* pred = mk_closure(set_filter_even_fn, NULL, NULL, 0, 1);
    Obj* filtered = set_filter(s, pred);
    
    /* Filtered set should have only even numbers */
    ASSERT(set_size(filtered) == 2);
    ASSERT(obj_to_int(set_contains(filtered, mk_int(2))) == 1);
    ASSERT(obj_to_int(set_contains(filtered, mk_int(4))) == 1);
    
    PASS();
}

/* Test closure for set_reduce (sum) */
static Obj* set_reduce_sum_fn(Obj** captures, Obj** args, int argc) {
    (void)captures;
    (void)argc;
    long acc = obj_to_int(args[0]);
    long x = obj_to_int(args[1]);
    return mk_int(acc + x);
}

void test_set_reduce(void) {
    printf("test_set_reduce... ");
    
    Obj* s = mk_set();
    set_add(s, mk_int(1));
    set_add(s, mk_int(2));
    set_add(s, mk_int(3));
    set_add(s, mk_int(4));
    
    Obj* fn = mk_closure(set_reduce_sum_fn, NULL, NULL, 0, 2);
    Obj* init = mk_int(0);
    Obj* result = set_reduce(s, fn, init);
    
    /* Sum should be 10 */
    ASSERT(obj_to_int(result) == 10);
    
    PASS();
}

void test_set_reduce_empty(void) {
    printf("test_set_reduce_empty... ");
    
    Obj* s = mk_set();
    Obj* fn = mk_closure(set_reduce_sum_fn, NULL, NULL, 0, 2);
    Obj* init = mk_int(42);
    Obj* result = set_reduce(s, fn, init);
    
    /* Reduce on empty set should return init */
    ASSERT(obj_to_int(result) == 42);
    
    PASS();
}

/* Test closure for set_foreach */
static int foreach_count = 0;
static Obj* set_foreach_counter_fn(Obj** captures, Obj** args, int argc) {
    (void)captures;
    (void)argc;
    (void)args;
    foreach_count++;
    return mk_nothing();
}

void test_set_foreach(void) {
    printf("test_set_foreach... ");
    
    Obj* s = mk_set();
    set_add(s, mk_int(1));
    set_add(s, mk_int(2));
    set_add(s, mk_int(3));
    
    foreach_count = 0;
    Obj* fn = mk_closure(set_foreach_counter_fn, NULL, NULL, 0, 1);
    
    /* foreach returns nothing, but function should be called 3 times */
    Obj* result = set_foreach(s, fn);
    ASSERT(result == NULL);  /* foreach returns void/nothing */
    ASSERT(foreach_count == 3);
    
    PASS();
}

/* ==================== Large Set Tests ==================== */

void test_set_large_size(void) {
    printf("test_set_large_size... ");
    
    Obj* s = mk_set();
    
    /* Add 1000 elements */
    for (int i = 0; i < 1000; i++) {
        set_add(s, mk_int(i));
    }
    
    ASSERT(set_size(s) == 1000);
    
    /* Verify all elements present */
    for (int i = 0; i < 1000; i++) {
        ASSERT(obj_to_int(set_contains(s, mk_int(i))) == 1);
    }
    
    PASS();
}

void test_set_large_union(void) {
    printf("test_set_large_union... ");
    
    Obj* a = mk_set();
    Obj* b = mk_set();
    
    for (int i = 0; i < 500; i++) {
        set_add(a, mk_int(i));
    }
    
    for (int i = 500; i < 1000; i++) {
        set_add(b, mk_int(i));
    }
    
    Obj* u = set_union(a, b);
    ASSERT(set_size(u) == 1000);
    
    PASS();
}

/* ==================== Test Runner ==================== */

void run_set_tests(void) {
    printf("\n========== Set Data Structure Tests ==========\n");
    
    /* Basic operations */
    test_set_add_contains();
    test_set_add_duplicate();
    test_set_remove();
    test_set_size();
    test_set_empty_p();
    test_set_various_types();
    
    /* Set operations */
    test_set_union();
    test_set_union_disjoint();
    test_set_union_self();
    test_set_intersection();
    test_set_intersection_disjoint();
    test_set_difference();
    test_set_symmetric_difference();
    
    /* Set predicates */
    test_set_subset_p();
    test_set_superset_p();
    
    /* Conversions */
    test_set_to_list();
    test_list_to_set();
    test_set_to_array();
    test_array_to_set();
    
    /* Higher-order functions */
    test_set_map();
    test_set_filter();
    test_set_reduce();
    test_set_reduce_empty();
    test_set_foreach();
    
    /* Large sets */
    test_set_large_size();
    test_set_large_union();
    
    printf("\nSet tests completed\n");
}
