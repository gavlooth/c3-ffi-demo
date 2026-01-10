/*
 * test_bridge.cpp - Test the Immer bridge
 */

#include <stdio.h>
#include "immer_bridge.h"

int main() {
    printf("Testing Immer bridge...\n");

    // Test vector
    printf("\n=== Vector Tests ===\n");
    void* v = immer_vector_empty();
    printf("Empty vector size: %d\n", immer_vector_size(v));

    // Push some elements (using int pointers as demo)
    int a = 10, b = 20, c = 30;
    void* v1 = immer_vector_push(v, &a);
    void* v2 = immer_vector_push(v1, &b);
    void* v3 = immer_vector_push(v2, &c);

    printf("After 3 pushes: size=%d\n", immer_vector_size(v3));
    printf("Original still empty: size=%d\n", immer_vector_size(v));

    int* got = (int*)immer_vector_get(v3, 1);
    printf("v3[1] = %d (expected 20)\n", *got);

    // Test set
    int d = 40;
    void* v4 = immer_vector_set(v3, 1, &d);
    got = (int*)immer_vector_get(v4, 1);
    printf("After set v4[1] = %d (expected 40)\n", *got);
    got = (int*)immer_vector_get(v3, 1);
    printf("Original v3[1] = %d (still 20)\n", *got);

    // Test pop
    void* v5 = immer_vector_pop(v4);
    printf("After pop: size=%d (expected 2)\n", immer_vector_size(v5));

    // Clean up
    immer_vector_free(v);
    immer_vector_free(v1);
    immer_vector_free(v2);
    immer_vector_free(v3);
    immer_vector_free(v4);
    immer_vector_free(v5);

    // Test map
    printf("\n=== Map Tests ===\n");
    void* m = immer_map_empty();
    printf("Empty map count: %d\n", immer_map_count(m));

    // Use string pointers as keys
    const char* k1 = "foo";
    const char* k2 = "bar";
    int v_foo = 100, v_bar = 200;

    void* m1 = immer_map_assoc(m, (void*)k1, &v_foo);
    void* m2 = immer_map_assoc(m1, (void*)k2, &v_bar);

    printf("After 2 assocs: count=%d\n", immer_map_count(m2));
    printf("Contains 'foo': %d\n", immer_map_contains(m2, (void*)k1));
    printf("Contains 'baz': %d\n", immer_map_contains(m2, (void*)"baz"));

    int* got_val = (int*)immer_map_get(m2, (void*)k1, NULL);
    printf("get(m2, 'foo') = %d (expected 100)\n", *got_val);

    // Test dissoc
    void* m3 = immer_map_dissoc(m2, (void*)k1);
    printf("After dissoc 'foo': count=%d\n", immer_map_count(m3));
    printf("Contains 'foo': %d (expected 0)\n", immer_map_contains(m3, (void*)k1));

    // Clean up
    immer_map_free(m);
    immer_map_free(m1);
    immer_map_free(m2);
    immer_map_free(m3);

    // Test set
    printf("\n=== Set Tests ===\n");
    void* s = immer_set_empty();
    printf("Empty set count: %d\n", immer_set_count(s));

    int e1 = 1, e2 = 2, e3 = 3;
    void* s1 = immer_set_conj(s, &e1);
    void* s2 = immer_set_conj(s1, &e2);
    void* s3 = immer_set_conj(s2, &e3);
    void* s4 = immer_set_conj(s3, &e1);  // Duplicate

    printf("After 4 conjs (1 dup): count=%d (expected 3)\n", immer_set_count(s4));
    printf("Contains e1: %d\n", immer_set_contains(s4, &e1));
    printf("Contains e2: %d\n", immer_set_contains(s4, &e2));

    // Test disj
    void* s5 = immer_set_disj(s4, &e2);
    printf("After disj e2: count=%d (expected 2)\n", immer_set_count(s5));
    printf("Contains e2: %d (expected 0)\n", immer_set_contains(s5, &e2));

    // Clean up
    immer_set_free(s);
    immer_set_free(s1);
    immer_set_free(s2);
    immer_set_free(s3);
    immer_set_free(s4);
    immer_set_free(s5);

    printf("\n=== All Tests Passed ===\n");
    return 0;
}
