/*
 * test_math_numerics.c - Comprehensive tests for math and numeric utilities
 *
 * Coverage: Functions in runtime/src/math_numerics.c
 *
 * Test Groups:
 *   - Inverse trigonometric functions (asin, acos, atan, atan2)
 *   - Exponential/logarithmic functions (exp, log, log10, log2, sqrt)
 *   - Rounding functions (floor, ceil, round, trunc)
 *   - Math constants (pi, e, inf, nan)
 *   - Comparison functions (min, max, clamp)
 *   - Bitwise operations (band, bor, bxor, bnot, lshift, rshift)
 */

#include "test_framework.h"
#include <math.h>
#include <limits.h>

/* ==================== Inverse Trig Tests ==================== */

void test_math_asin_zero(void) {
    /* asin(0) = 0 */
    Obj* x = mk_int(0);
    Obj* r = prim_asin(x);
    ASSERT_NOT_NULL(r);
    
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, 0.0, 0.0001);
    
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_asin_positive(void) {
    /* asin(0.5) = pi/6 ≈ 0.524 */
    Obj* x = mk_float(0.5);
    Obj* r = prim_asin(x);
    ASSERT_NOT_NULL(r);
    
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, asin(0.5), 0.0001);
    
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_asin_negative(void) {
    /* asin(-0.5) = -pi/6 ≈ -0.524 */
    Obj* x = mk_float(-0.5);
    Obj* r = prim_asin(x);
    ASSERT_NOT_NULL(r);
    
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, asin(-0.5), 0.0001);
    
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_asin_boundary(void) {
    /* asin(1) = pi/2 */
    Obj* x = mk_float(1.0);
    Obj* r = prim_asin(x);
    ASSERT_NOT_NULL(r);
    
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, M_PI / 2.0, 0.0001);
    
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_acos_zero(void) {
    /* acos(0) = pi/2 */
    Obj* x = mk_int(0);
    Obj* r = prim_acos(x);
    ASSERT_NOT_NULL(r);
    
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, M_PI / 2.0, 0.0001);
    
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_acos_positive(void) {
    /* acos(0.5) = pi/3 ≈ 1.047 */
    Obj* x = mk_float(0.5);
    Obj* r = prim_acos(x);
    ASSERT_NOT_NULL(r);
    
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, acos(0.5), 0.0001);
    
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_acos_negative(void) {
    /* acos(-0.5) = 2*pi/3 ≈ 2.094 */
    Obj* x = mk_float(-0.5);
    Obj* r = prim_acos(x);
    ASSERT_NOT_NULL(r);
    
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, acos(-0.5), 0.0001);
    
    dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_acos_boundary(void) {
    /* acos(1) = 0 */
    Obj* x = mk_float(1.0);
    Obj* r = prim_acos(x);
    ASSERT_NOT_NULL(r);
    
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, 0.0, 0.0001);
    
    dec_ref(x); dec_ref(r);
    PASS();
}

/* ==================== atan2 Tests ==================== */

void test_math_atan2_quadrant1(void) {
    /* Quadrant I: x > 0, y > 0 => angle 0 to pi/2 */
    Obj* y = mk_int(1);
    Obj* x = mk_int(1);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    ASSERT(IS_BOXED(r) && r->tag == TAG_FLOAT);
    
    /* atan2(1, 1) = pi/4 ≈ 0.785 */
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, M_PI / 4.0, 0.0001);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_atan2_quadrant2(void) {
    /* Quadrant II: x < 0, y > 0 => angle pi/2 to pi */
    Obj* y = mk_int(1);
    Obj* x = mk_int(-1);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    
    /* atan2(1, -1) = 3*pi/4 ≈ 2.356 */
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, 3.0 * M_PI / 4.0, 0.0001);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_atan2_quadrant3(void) {
    /* Quadrant III: x < 0, y < 0 => angle -pi to -pi/2 */
    Obj* y = mk_int(-1);
    Obj* x = mk_int(-1);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    
    /* atan2(-1, -1) = -3*pi/4 ≈ -2.356 */
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, -3.0 * M_PI / 4.0, 0.0001);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_atan2_quadrant4(void) {
    /* Quadrant IV: x > 0, y < 0 => angle -pi/2 to 0 */
    Obj* y = mk_int(-1);
    Obj* x = mk_int(1);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    
    /* atan2(-1, 1) = -pi/4 ≈ -0.785 */
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, -M_PI / 4.0, 0.0001);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_atan2_positive_x_axis(void) {
    /* Positive x-axis: y = 0, x > 0 => angle = 0 */
    Obj* y = mk_int(0);
    Obj* x = mk_int(5);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, 0.0, 0.0001);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_atan2_negative_x_axis(void) {
    /* Negative x-axis: y = 0, x < 0 => angle = pi or -pi */
    Obj* y = mk_int(0);
    Obj* x = mk_int(-5);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    
    /* atan2(0, -x) = pi */
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, M_PI, 0.0001);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_atan2_positive_y_axis(void) {
    /* Positive y-axis: x = 0, y > 0 => angle = pi/2 */
    Obj* y = mk_int(5);
    Obj* x = mk_int(0);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, M_PI / 2.0, 0.0001);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_atan2_negative_y_axis(void) {
    /* Negative y-axis: x = 0, y < 0 => angle = -pi/2 */
    Obj* y = mk_int(-5);
    Obj* x = mk_int(0);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, -M_PI / 2.0, 0.0001);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_atan2_origin(void) {
    /* Origin: x = 0, y = 0 => undefined, but typically returns 0 */
    Obj* y = mk_int(0);
    Obj* x = mk_int(0);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    
    /* atan2(0, 0) is implementation-defined; we just verify it returns something */
    ASSERT(IS_BOXED(r) && r->tag == TAG_FLOAT);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_atan2_with_floats(void) {
    /* Test with float arguments */
    Obj* y = mk_float(2.5);
    Obj* x = mk_float(1.5);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    
    /* atan2(2.5, 1.5) ≈ 1.030 */
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, atan2(2.5, 1.5), 0.0001);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_atan2_mixed_types(void) {
    /* Test with mixed int/float arguments */
    Obj* y = mk_int(2);
    Obj* x = mk_float(1.5);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    
    /* Should promote to float */
    double result = obj_to_float(r);
    ASSERT_EQ_FLOAT(result, atan2(2.0, 1.5), 0.0001);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

void test_math_atan2_negative_both(void) {
    /* Both negative: should preserve angle in quadrant III */
    Obj* y = mk_int(-3);
    Obj* x = mk_int(-4);
    Obj* r = prim_atan2(y, x);
    ASSERT_NOT_NULL(r);
    
    /* atan2(-3, -4) should be ≈ -2.498 (not 0.643) */
    double result = obj_to_float(r);
    ASSERT(result < 0.0);  /* Should be negative (quadrant III) */
    ASSERT_EQ_FLOAT(result, atan2(-3.0, -4.0), 0.0001);
    
    dec_ref(y); dec_ref(x); dec_ref(r);
    PASS();
}

/* ==================== Test Runner ==================== */

void run_math_numerics_tests(void) {
    TEST_SUITE("Math Numerics Tests");
    
    TEST_SECTION("Inverse Trigonometric Functions");
    RUN_TEST(test_math_asin_zero);
    RUN_TEST(test_math_asin_positive);
    RUN_TEST(test_math_asin_negative);
    RUN_TEST(test_math_asin_boundary);
    RUN_TEST(test_math_acos_zero);
    RUN_TEST(test_math_acos_positive);
    RUN_TEST(test_math_acos_negative);
    RUN_TEST(test_math_acos_boundary);
    
    TEST_SECTION("atan2 (Arctangent)");
    RUN_TEST(test_math_atan2_quadrant1);
    RUN_TEST(test_math_atan2_quadrant2);
    RUN_TEST(test_math_atan2_quadrant3);
    RUN_TEST(test_math_atan2_quadrant4);
    RUN_TEST(test_math_atan2_positive_x_axis);
    RUN_TEST(test_math_atan2_negative_x_axis);
    RUN_TEST(test_math_atan2_positive_y_axis);
    RUN_TEST(test_math_atan2_negative_y_axis);
    RUN_TEST(test_math_atan2_origin);
    RUN_TEST(test_math_atan2_with_floats);
    RUN_TEST(test_math_atan2_mixed_types);
    RUN_TEST(test_math_atan2_negative_both);
}
