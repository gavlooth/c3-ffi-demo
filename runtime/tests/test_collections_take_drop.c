/*
 * test_collections_take_drop.c - Tests for prim_coll_take and prim_coll_drop
 *
 * Tests take/drop behavior for:
 *   - Arrays of integers (take/drop first n elements)
 *   - Lists of integers (take/drop first n elements)
 *   - Empty arrays and lists
 *   - Taking/dropping more elements than available
 *   - Taking/dropping zero elements
 *   - Taking/dropping all elements
 */

#include "test_framework.h"

/* Helper to create list from C array (prefixed to avoid conflicts) */
static Obj* make_int_list_take_drop(long values[], int len) {
    Obj* result = NULL;
    for (int i = len - 1; i >= 0; i--) {
        result = mk_pair(mk_int(values[i]), result);
    }
    return result;
}

/* Helper to create array from C array (prefixed to avoid conflicts) */
static Obj* make_int_array_take_drop(long values[], int len) {
    Obj* result = mk_array(len);
    for (int i = 0; i < len; i++) {
        array_push(result, mk_int(values[i]));
    }
    return result;
}

/* Helper to count list length (prefixed to avoid conflicts) */
static int count_list_length_take_drop(Obj* xs) {
    int len = 0;
    while (xs && IS_BOXED(xs) && xs->tag == TAG_PAIR) {
        len++;
        xs = xs->b;
    }
    return len;
}

/* Helper to extract array element (prefixed to avoid conflicts) */
static long get_array_elem_take_drop(Obj* arr, int index) {
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

/* Helper to extract list element by index (prefixed to avoid conflicts) */
static long get_list_elem_take_drop(Obj* lst, int index) {
    int i = 0;
    while (lst && IS_BOXED(lst) && lst->tag == TAG_PAIR) {
        if (i == index) {
            Obj* car = lst->a;
            if (IS_IMMEDIATE_INT(car)) {
                return INT_IMM_VALUE(car);
            } else if (!IS_IMMEDIATE(car) && car->tag == TAG_INT) {
                return car->i;
            }
            return 0;
        }
        i++;
        lst = lst->b;
    }
    return 0;
}

/* Helper macros to make code cleaner */
#define make_int_list make_int_list_take_drop
#define make_int_array make_int_array_take_drop
#define count_list_length count_list_length_take_drop
#define get_array_elem get_array_elem_take_drop
#define get_list_elem get_list_elem_take_drop

/* ========== prim_coll_take Tests (Arrays) ========== */

void test_take_int_array_basic(void) {
    /* Create array: [1, 2, 3, 4, 5] */
    long values[] = {1, 2, 3, 4, 5};
    Obj* arr = make_int_array(values, 5);

    /* Take first 3 elements */
    Obj* result = prim_coll_take(mk_int(3), arr);

    /* Check result */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);
    
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 3);
    
    /* Verify values */
    ASSERT_EQ(get_array_elem(result, 0), 1);
    ASSERT_EQ(get_array_elem(result, 1), 2);
    ASSERT_EQ(get_array_elem(result, 2), 3);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_take_int_array_more_than_available(void) {
    /* Create array: [1, 2, 3] */
    long values[] = {1, 2, 3};
    Obj* arr = make_int_array(values, 3);

    /* Take 10 elements (more than available) */
    Obj* result = prim_coll_take(mk_int(10), arr);

    /* Check result - should return all elements */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);
    
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 3);
    
    ASSERT_EQ(get_array_elem(result, 0), 1);
    ASSERT_EQ(get_array_elem(result, 1), 2);
    ASSERT_EQ(get_array_elem(result, 2), 3);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_take_int_array_all_elements(void) {
    /* Create array: [1, 2, 3, 4, 5] */
    long values[] = {1, 2, 3, 4, 5};
    Obj* arr = make_int_array(values, 5);

    /* Take all 5 elements */
    Obj* result = prim_coll_take(mk_int(5), arr);

    /* Check result */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);
    
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 5);
    
    /* Verify all values */
    ASSERT_EQ(get_array_elem(result, 0), 1);
    ASSERT_EQ(get_array_elem(result, 4), 5);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_take_int_array_zero(void) {
    /* Create array: [1, 2, 3] */
    long values[] = {1, 2, 3};
    Obj* arr = make_int_array(values, 3);

    /* Take 0 elements */
    Obj* result = prim_coll_take(mk_int(0), arr);

    /* Check result - should be empty array */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);
    
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 0);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_take_int_array_empty(void) {
    /* Create empty array */
    Obj* arr = mk_array(0);

    /* Take 3 elements from empty array */
    Obj* result = prim_coll_take(mk_int(3), arr);

    /* Check result - should be empty array */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);
    
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 0);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_take_int_array_single_element(void) {
    /* Create single element array: [42] */
    long values[] = {42};
    Obj* arr = make_int_array(values, 1);

    /* Take 1 element */
    Obj* result = prim_coll_take(mk_int(1), arr);

    /* Check result */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);
    
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 1);
    ASSERT_EQ(get_array_elem(result, 0), 42);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

/* ========== prim_coll_take Tests (Lists) ========== */

void test_take_int_list_basic(void) {
    /* Create list: (1 2 3 4 5) */
    long values[] = {1, 2, 3, 4, 5};
    Obj* lst = make_int_list(values, 5);

    /* Take first 3 elements */
    Obj* result = prim_coll_take(mk_int(3), lst);

    /* Check result */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_PAIR);
    
    /* Verify values */
    ASSERT_EQ(get_list_elem(result, 0), 1);
    ASSERT_EQ(get_list_elem(result, 1), 2);
    ASSERT_EQ(get_list_elem(result, 2), 3);
    
    /* Verify length */
    ASSERT_EQ(count_list_length(result), 3);

    /* Cleanup */
    dec_ref(lst);
    dec_ref(result);
}

void test_take_int_list_more_than_available(void) {
    /* Create list: (1 2 3) */
    long values[] = {1, 2, 3};
    Obj* lst = make_int_list(values, 3);

    /* Take 10 elements */
    Obj* result = prim_coll_take(mk_int(10), lst);

    /* Check result - should return all elements */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_PAIR);
    
    ASSERT_EQ(count_list_length(result), 3);
    ASSERT_EQ(get_list_elem(result, 0), 1);
    ASSERT_EQ(get_list_elem(result, 2), 3);

    /* Cleanup */
    dec_ref(lst);
    dec_ref(result);
}

void test_take_int_list_zero(void) {
    /* Create list: (1 2 3) */
    long values[] = {1, 2, 3};
    Obj* lst = make_int_list(values, 3);

    /* Take 0 elements */
    Obj* result = prim_coll_take(mk_int(0), lst);

    /* Check result - should be NULL for empty list */
    ASSERT_NULL(result);

    /* Cleanup */
    dec_ref(lst);
}

void test_take_int_list_empty(void) {
    /* Create empty list (NULL) */
    Obj* lst = NULL;

    /* Take 3 elements */
    Obj* result = prim_coll_take(mk_int(3), lst);

    /* Check result - should be NULL */
    ASSERT_NULL(result);
}

/* ========== prim_coll_drop Tests (Arrays) ========== */

void test_drop_int_array_basic(void) {
    /* Create array: [1, 2, 3, 4, 5] */
    long values[] = {1, 2, 3, 4, 5};
    Obj* arr = make_int_array(values, 5);

    /* Drop first 2 elements */
    Obj* result = prim_coll_drop(mk_int(2), arr);

    /* Check result */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);
    
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 3);
    
    /* Verify remaining values */
    ASSERT_EQ(get_array_elem(result, 0), 3);
    ASSERT_EQ(get_array_elem(result, 1), 4);
    ASSERT_EQ(get_array_elem(result, 2), 5);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_drop_int_array_all_elements(void) {
    /* Create array: [1, 2, 3, 4, 5] */
    long values[] = {1, 2, 3, 4, 5};
    Obj* arr = make_int_array(values, 5);

    /* Drop all 5 elements */
    Obj* result = prim_coll_drop(mk_int(5), arr);

    /* Check result - should be empty array */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);
    
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 0);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_drop_int_array_zero(void) {
    /* Create array: [1, 2, 3] */
    long values[] = {1, 2, 3};
    Obj* arr = make_int_array(values, 3);

    /* Drop 0 elements */
    Obj* result = prim_coll_drop(mk_int(0), arr);

    /* Check result - should be unchanged */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);
    
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 3);
    
    ASSERT_EQ(get_array_elem(result, 0), 1);
    ASSERT_EQ(get_array_elem(result, 2), 3);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_drop_int_array_more_than_available(void) {
    /* Create array: [1, 2, 3] */
    long values[] = {1, 2, 3};
    Obj* arr = make_int_array(values, 3);

    /* Drop 10 elements (more than available) */
    Obj* result = prim_coll_drop(mk_int(10), arr);

    /* Check result - should be empty array */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);
    
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 0);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

void test_drop_int_array_empty(void) {
    /* Create empty array */
    Obj* arr = mk_array(0);

    /* Drop 3 elements */
    Obj* result = prim_coll_drop(mk_int(3), arr);

    /* Check result - should be empty array */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_ARRAY);
    
    Array* a = (Array*)result->ptr;
    ASSERT_EQ(a->len, 0);

    /* Cleanup */
    dec_ref(arr);
    dec_ref(result);
}

/* ========== prim_coll_drop Tests (Lists) ========== */

void test_drop_int_list_basic(void) {
    /* Create list: (1 2 3 4 5) */
    long values[] = {1, 2, 3, 4, 5};
    Obj* lst = make_int_list(values, 5);

    /* Drop first 2 elements */
    Obj* result = prim_coll_drop(mk_int(2), lst);

    /* Check result */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_PAIR);
    
    /* Verify remaining values */
    ASSERT_EQ(get_list_elem(result, 0), 3);
    ASSERT_EQ(get_list_elem(result, 1), 4);
    ASSERT_EQ(get_list_elem(result, 2), 5);
    
    ASSERT_EQ(count_list_length(result), 3);

    /* Cleanup */
    dec_ref(lst);
    dec_ref(result);
}

void test_drop_int_list_all_elements(void) {
    /* Create list: (1 2 3) */
    long values[] = {1, 2, 3};
    Obj* lst = make_int_list(values, 3);

    /* Drop all 3 elements */
    Obj* result = prim_coll_drop(mk_int(3), lst);

    /* Check result - should be NULL */
    ASSERT_NULL(result);

    /* Cleanup */
    dec_ref(lst);
}

void test_drop_int_list_zero(void) {
    /* Create list: (1 2 3) */
    long values[] = {1, 2, 3};
    Obj* lst = make_int_list(values, 3);

    /* Drop 0 elements */
    Obj* result = prim_coll_drop(mk_int(0), lst);

    /* Check result - should be unchanged */
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->tag == TAG_PAIR);
    
    ASSERT_EQ(get_list_elem(result, 0), 1);
    ASSERT_EQ(get_list_elem(result, 2), 3);
    ASSERT_EQ(count_list_length(result), 3);

    /* Cleanup */
    dec_ref(lst);
    dec_ref(result);
}

void test_drop_int_list_more_than_available(void) {
    /* Create list: (1 2 3) */
    long values[] = {1, 2, 3};
    Obj* lst = make_int_list(values, 3);

    /* Drop 10 elements */
    Obj* result = prim_coll_drop(mk_int(10), lst);

    /* Check result - should be NULL */
    ASSERT_NULL(result);

    /* Cleanup */
    dec_ref(lst);
}

void test_drop_int_list_empty(void) {
    /* Create empty list (NULL) */
    Obj* lst = NULL;

    /* Drop 3 elements */
    Obj* result = prim_coll_drop(mk_int(3), lst);

    /* Check result - should be NULL */
    ASSERT_NULL(result);
}

/* ========== Run All Tests ========== */

void run_collections_take_drop_tests(void) {
    TEST_SUITE("prim_coll_take and prim_coll_drop Functions");

    /* prim_coll_take Array Tests */
    TEST_SECTION("take Function (Arrays)");
    RUN_TEST(test_take_int_array_basic);
    RUN_TEST(test_take_int_array_more_than_available);
    RUN_TEST(test_take_int_array_all_elements);
    RUN_TEST(test_take_int_array_zero);
    RUN_TEST(test_take_int_array_empty);
    RUN_TEST(test_take_int_array_single_element);

    /* prim_coll_take List Tests */
    TEST_SECTION("take Function (Lists)");
    RUN_TEST(test_take_int_list_basic);
    RUN_TEST(test_take_int_list_more_than_available);
    RUN_TEST(test_take_int_list_zero);
    RUN_TEST(test_take_int_list_empty);

    /* prim_coll_drop Array Tests */
    TEST_SECTION("drop Function (Arrays)");
    RUN_TEST(test_drop_int_array_basic);
    RUN_TEST(test_drop_int_array_all_elements);
    RUN_TEST(test_drop_int_array_zero);
    RUN_TEST(test_drop_int_array_more_than_available);
    RUN_TEST(test_drop_int_array_empty);

    /* prim_coll_drop List Tests */
    TEST_SECTION("drop Function (Lists)");
    RUN_TEST(test_drop_int_list_basic);
    RUN_TEST(test_drop_int_list_all_elements);
    RUN_TEST(test_drop_int_list_zero);
    RUN_TEST(test_drop_int_list_more_than_available);
    RUN_TEST(test_drop_int_list_empty);
}
