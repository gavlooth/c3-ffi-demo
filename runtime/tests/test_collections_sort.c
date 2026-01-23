/*
 * test_collections_sort.c - Tests for prim_sort collection function
 *
 * Tests sorting behavior for:
 * - Arrays of integers (ascending order)
 * - Lists of integers (ascending order)
 * - Empty arrays and lists
 * - Single element collections
 * - Unsupported types (returns nothing)
 * - Float arrays
 */

#include "test_framework.h"

/* Helper to create list from C array (prefixed to avoid conflicts) */
static Obj* make_int_list_sort(long values[], int len) {
    Obj* result = NULL;
    for (int i = len - 1; i >= 0; i--) {
        result = mk_pair(mk_int(values[i]), result);
    }
    return result;
}

/* Helper to create array from C array (prefixed to avoid conflicts) */
static Obj* make_int_array_sort(long values[], int len) {
    Obj* result = mk_array(len);
    for (int i = 0; i < len; i++) {
        array_push(result, mk_int(values[i]));
    }
    return result;
}

/* Helper to count list length (prefixed to avoid conflicts) */
static int count_list_length_sort(Obj* xs) {
    int len = 0;
    while (xs && IS_BOXED(xs) && xs->tag == TAG_PAIR) {
        len++;
        xs = xs->b;
    }
    return len;
}

/* Helper to extract array element (prefixed to avoid conflicts) */
static long get_array_elem_sort(Obj* arr, int index) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) {
        return 0;
    }
    Array* a = (Array*)arr->ptr;
    if (index < 0 || index >= a->len) {
        return 0;
    }
    Obj* elem = a->data[index];
    if (IS_IMMEDIATE_INT(elem)) {
        return INT_IMM_VALUE(elem);
    } else if (!IS_IMMEDIATE(elem) && elem->tag == TAG_INT) {
        return elem->i;
    }
    return 0;
}

/* Helper to check if array is sorted (prefixed to avoid conflicts) */
static bool is_sorted_array_sort(Obj* arr) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) {
        return false;
    }
    Array* a = (Array*)arr->ptr;
    if (a->len <= 1) {
        return true;
    }
    for (int i = 0; i < a->len - 1; i++) {
        long curr = get_array_elem_sort(arr, i);
        long next = get_array_elem_sort(arr, i + 1);
        if (curr > next) {
            return false;
        }
    }
    return true;
}

/* Helper to check if list is sorted (prefixed to avoid conflicts) */
static bool is_sorted_list_sort(Obj* xs) {
    long prev = -999999999;
    while (xs && IS_BOXED(xs) && xs->tag == TAG_PAIR) {
        long curr;
        Obj* car = xs->a;
        if (IS_IMMEDIATE_INT(car)) {
            curr = INT_IMM_VALUE(car);
        } else if (!IS_IMMEDIATE(car) && car->tag == TAG_INT) {
            curr = car->i;
        } else {
            return false;
        }
        if (curr < prev) {
            return false;
        }
        prev = curr;
        xs = xs->b;
    }
    return true;
}

/* Helper macros to make code cleaner (avoid conflicts) */
#define make_int_list make_int_list_sort
#define make_int_array make_int_array_sort
#define count_list_length count_list_length_sort
#define get_array_elem get_array_elem_sort
#define is_sorted_array is_sorted_array_sort
#define is_sorted_list is_sorted_list_sort

/* ========== Array Sorting Tests ========== */

void test_sort_int_array_basic(void) {
    /* Create unsorted array: [5, 2, 8, 1, 9] */
    long values[] = {5, 2, 8, 1, 9};
    Obj* arr = make_int_array(values, 5);

    /* Sort the array */
    Obj* result = prim_sort(arr);

    /* Check result */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(is_sorted_array(result));

    /* Check specific values */
    ASSERT_EQ(get_array_elem(result, 0), 1);
    ASSERT_EQ(get_array_elem(result, 1), 2);
    ASSERT_EQ(get_array_elem(result, 2), 5);
    ASSERT_EQ(get_array_elem(result, 3), 8);
    ASSERT_EQ(get_array_elem(result, 4), 9);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_sort_int_array_already_sorted(void) {
    /* Create already sorted array: [1, 2, 3, 4, 5] */
    long values[] = {1, 2, 3, 4, 5};
    Obj* arr = make_int_array(values, 5);

    /* Sort the array */
    Obj* result = prim_sort(arr);

    /* Check result is still sorted */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(is_sorted_array(result));

    /* Verify values unchanged */
    ASSERT_EQ(get_array_elem(result, 0), 1);
    ASSERT_EQ(get_array_elem(result, 1), 2);
    ASSERT_EQ(get_array_elem(result, 2), 3);
    ASSERT_EQ(get_array_elem(result, 3), 4);
    ASSERT_EQ(get_array_elem(result, 4), 5);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_sort_int_array_reverse_order(void) {
    /* Create reverse sorted array: [5, 4, 3, 2, 1] */
    long values[] = {5, 4, 3, 2, 1};
    Obj* arr = make_int_array(values, 5);

    /* Sort the array */
    Obj* result = prim_sort(arr);

    /* Check result is sorted ascending */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(is_sorted_array(result));

    /* Verify values are now ascending */
    ASSERT_EQ(get_array_elem(result, 0), 1);
    ASSERT_EQ(get_array_elem(result, 1), 2);
    ASSERT_EQ(get_array_elem(result, 2), 3);
    ASSERT_EQ(get_array_elem(result, 3), 4);
    ASSERT_EQ(get_array_elem(result, 4), 5);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_sort_int_array_duplicates(void) {
    /* Create array with duplicates: [3, 1, 4, 1, 5, 3] */
    long values[] = {3, 1, 4, 1, 5, 3};
    Obj* arr = make_int_array(values, 6);

    /* Sort the array */
    Obj* result = prim_sort(arr);

    /* Check result is sorted */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(is_sorted_array(result));

    /* Verify sorted values with duplicates */
    ASSERT_EQ(get_array_elem(result, 0), 1);
    ASSERT_EQ(get_array_elem(result, 1), 1);
    ASSERT_EQ(get_array_elem(result, 2), 3);
    ASSERT_EQ(get_array_elem(result, 3), 3);
    ASSERT_EQ(get_array_elem(result, 4), 4);
    ASSERT_EQ(get_array_elem(result, 5), 5);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_sort_int_array_empty(void) {
    /* Create empty array */
    Obj* arr = mk_array(0);

    /* Sort the array */
    Obj* result = prim_sort(arr);

    /* Check result is not NULL and is an array */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);

    /* Check array is empty */
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 0);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_sort_int_array_single_element(void) {
    /* Create single element array: [42] */
    long values[] = {42};
    Obj* arr = make_int_array(values, 1);

    /* Sort the array */
    Obj* result = prim_sort(arr);

    /* Check result is sorted */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(is_sorted_array(result));

    /* Verify value unchanged */
    ASSERT_EQ(get_array_elem(result, 0), 42);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_sort_int_array_negative_numbers(void) {
    /* Create array with negative numbers: [-5, 2, -8, 1, 0] */
    long values[] = {-5, 2, -8, 1, 0};
    Obj* arr = make_int_array(values, 5);

    /* Sort the array */
    Obj* result = prim_sort(arr);

    /* Check result is sorted */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(is_sorted_array(result));

    /* Verify values are sorted correctly */
    ASSERT_EQ(get_array_elem(result, 0), -8);
    ASSERT_EQ(get_array_elem(result, 1), -5);
    ASSERT_EQ(get_array_elem(result, 2), 0);
    ASSERT_EQ(get_array_elem(result, 3), 1);
    ASSERT_EQ(get_array_elem(result, 4), 2);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

/* ========== List Sorting Tests ========== */

void test_sort_int_list_basic(void) {
    /* Create unsorted list: (5 2 8 1 9) */
    long values[] = {5, 2, 8, 1, 9};
    Obj* lst = make_int_list(values, 5);

    /* Sort the list */
    Obj* result = prim_sort(lst);

    /* Check result is sorted */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(is_sorted_list(result));

    /* Verify result is a list */
    ASSERT_TRUE(result->tag == TAG_PAIR);

    /* Cleanup */
    dec_ref(lst);
    dec_ref(result);
}

void test_sort_int_list_empty(void) {
    /* Create empty list (NULL) */
    Obj* lst = NULL;

    /* Sort the list */
    Obj* result = prim_sort(lst);

    /* Check result is NULL for empty list */
    ASSERT_NULL(result);
}

void test_sort_int_list_single_element(void) {
    /* Create single element list: (42) */
    long values[] = {42};
    Obj* lst = make_int_list(values, 1);

    /* Sort the list */
    Obj* result = prim_sort(lst);

    /* Check result is sorted */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(is_sorted_list(result));

    /* Cleanup */
    dec_ref(lst);
    dec_ref(result);
}

void test_sort_int_list_duplicates(void) {
    /* Create list with duplicates: (3 1 4 1 5 3) */
    long values[] = {3, 1, 4, 1, 5, 3};
    Obj* lst = make_int_list(values, 6);

    /* Sort the list */
    Obj* result = prim_sort(lst);

    /* Check result is sorted */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(is_sorted_list(result));

    /* Verify length is preserved */
    ASSERT_EQ(count_list_length(result), 6);

    /* Cleanup */
    dec_ref(lst);
    dec_ref(result);
}

/* ========== Error Handling / Unsupported Types ========== */

void test_sort_unsupported_type_string(void) {
    /* Create a string (unsupported for sorting) */
    Obj* str = mk_string("hello");

    /* Sort the string */
    Obj* result = prim_sort(str);

    /* Check result is nothing (TAG_NOTHING) */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(!IS_IMMEDIATE(result) && result->tag == TAG_NOTHING);

    /* Cleanup */
    dec_ref(str);
    if (result) dec_ref(result);
}

void test_sort_unsupported_type_nothing(void) {
    /* Create nothing value (unsupported for sorting) */
    Obj* nothing = mk_nothing();

    /* Sort nothing */
    Obj* result = prim_sort(nothing);

    /* Check result is nothing */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(!IS_IMMEDIATE(result) && result->tag == TAG_NOTHING);

    /* Cleanup */
    if (nothing) dec_ref(nothing);
    if (result) dec_ref(result);
}

/* ========== Run All Tests ========== */

void run_collections_sort_tests(void) {
    TEST_SUITE("prim_sort Collection Function");

    /* Array Sorting Tests */
    TEST_SECTION("Array Sorting");
    RUN_TEST(test_sort_int_array_basic);
    RUN_TEST(test_sort_int_array_already_sorted);
    RUN_TEST(test_sort_int_array_reverse_order);
    RUN_TEST(test_sort_int_array_duplicates);
    RUN_TEST(test_sort_int_array_empty);
    RUN_TEST(test_sort_int_array_single_element);
    RUN_TEST(test_sort_int_array_negative_numbers);

    /* List Sorting Tests */
    TEST_SECTION("List Sorting");
    RUN_TEST(test_sort_int_list_basic);
    RUN_TEST(test_sort_int_list_empty);
    RUN_TEST(test_sort_int_list_single_element);
    RUN_TEST(test_sort_int_list_duplicates);

    /* Error Handling */
    TEST_SECTION("Error Handling");
    RUN_TEST(test_sort_unsupported_type_string);
    RUN_TEST(test_sort_unsupported_type_nothing);
}
