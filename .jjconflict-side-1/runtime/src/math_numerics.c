/*
 * math_numerics.c - Math and numerics utilities for OmniLisp
 *
 * Core math operations:
 *   - Basic arithmetic: add, sub, mul, div, mod, pow
 *   - Trigonometric: sin, cos, tan, asin, acos, atan, atan2
 *   - Hyperbolic: sinh, cosh, tanh
 *   - Exponential/Logarithmic: exp, log, log10, log2, sqrt
 *   - Rounding: floor, ceil, round, trunc
 *   - Constants: pi, e, inf, nan
 *   - Comparison: min, max, clamp
 *   - Bitwise: band, bor, bxor, bnot, lshift, rshift
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include <limits.h>
#include "../include/omni.h"
#include "internal_types.h"

/* Ensure M_PI and M_E are defined (not always available in strict C99 mode) */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

/* ============== Helper Functions ============== */

/*
 * Helper: Extract double value from Obj
 * Uses obj_to_float from omni.h which correctly handles immediate values
 */
static double obj_to_double(Obj* obj) {
    return obj_to_float(obj);
}

/*
 * Helper: Extract long value from Obj
 * Uses obj_to_int from omni.h which correctly handles immediate values
 */
static long obj_to_long(Obj* obj) {
    return obj_to_int(obj);
}

/*
 * Helper: Create Obj from double
 */
static Obj* double_to_obj(double d) {
    /* Check if it's an integer */
    if (d == (long)d && d >= IMM_INT_MIN && d <= IMM_INT_MAX) {
        return mk_int((long)d);
    }
    return mk_float(d);
}

/*
 * Helper: Create Obj from long
 */
static Obj* long_to_obj(long l) {
    return mk_int(l);
}

/* ============== Basic Arithmetic ============== */

/*
 * prim_add: Addition
 * Args: a, b
 * Returns: a + b
 */
Obj* prim_add(Obj* a, Obj* b) {
    /* Check if either is float */
    bool is_float_a = IS_BOXED(a) && a->tag == TAG_FLOAT;
    bool is_float_b = IS_BOXED(b) && b->tag == TAG_FLOAT;

    if (is_float_a || is_float_b) {
        double da = obj_to_double(a);
        double db = obj_to_double(b);
        return mk_float(da + db);
    } else {
        long ia = obj_to_long(a);
        long ib = obj_to_long(b);
        return mk_int(ia + ib);
    }
}

/*
 * prim_sub: Subtraction
 * Args: a, b
 * Returns: a - b
 */
Obj* prim_sub(Obj* a, Obj* b) {
    bool is_float_a = IS_BOXED(a) && a->tag == TAG_FLOAT;
    bool is_float_b = IS_BOXED(b) && b->tag == TAG_FLOAT;

    if (is_float_a || is_float_b) {
        double da = obj_to_double(a);
        double db = obj_to_double(b);
        return mk_float(da - db);
    } else {
        long ia = obj_to_long(a);
        long ib = obj_to_long(b);
        return mk_int(ia - ib);
    }
}

/*
 * prim_mul: Multiplication
 * Args: a, b
 * Returns: a * b
 */
Obj* prim_mul(Obj* a, Obj* b) {
    bool is_float_a = IS_BOXED(a) && a->tag == TAG_FLOAT;
    bool is_float_b = IS_BOXED(b) && b->tag == TAG_FLOAT;

    if (is_float_a || is_float_b) {
        double da = obj_to_double(a);
        double db = obj_to_double(b);
        return mk_float(da * db);
    } else {
        long ia = obj_to_long(a);
        long ib = obj_to_long(b);
        return mk_int(ia * ib);
    }
}

/*
 * prim_div: Division
 * Args: a, b
 * Returns: a / b
 */
Obj* prim_div(Obj* a, Obj* b) {
    bool is_float_a = IS_BOXED(a) && a->tag == TAG_FLOAT;
    bool is_float_b = IS_BOXED(b) && b->tag == TAG_FLOAT;

    if (is_float_a || is_float_b) {
        double da = obj_to_double(a);
        double db = obj_to_double(b);
        return mk_float(da / db);
    } else {
        long ia = obj_to_long(a);
        long ib = obj_to_long(b);
        if (ib == 0) {
            fprintf(stderr, "prim_div: division by zero\n");
            return mk_float(0.0 / 0.0);  /* NaN */
        }
        return mk_int(ia / ib);
    }
}

/*
 * prim_mod: Modulo
 * Args: a, b
 * Returns: a % b
 */
Obj* prim_mod(Obj* a, Obj* b) {
    long ia = obj_to_long(a);
    long ib = obj_to_long(b);
    if (ib == 0) {
        fprintf(stderr, "prim_mod: division by zero\n");
        return mk_int(0);
    }
    return mk_int(ia % ib);
}

/*
 * prim_pow: Power
 * Args: base, exp
 * Returns: base ^ exp
 */
Obj* prim_pow(Obj* base, Obj* exp) {
    double dbase = obj_to_double(base);
    double dexp = obj_to_double(exp);
    return mk_float(pow(dbase, dexp));
}

/* ============== Trigonometric Functions ============== */

Obj* prim_sin(Obj* x) { return mk_float(sin(obj_to_double(x))); }
Obj* prim_cos(Obj* x) { return mk_float(cos(obj_to_double(x))); }
Obj* prim_tan(Obj* x) { return mk_float(tan(obj_to_double(x))); }

Obj* prim_asin(Obj* x) { return mk_float(asin(obj_to_double(x))); }
Obj* prim_acos(Obj* x) { return mk_float(acos(obj_to_double(x))); }
Obj* prim_atan(Obj* x) { return mk_float(atan(obj_to_double(x))); }

Obj* prim_atan2(Obj* y, Obj* x) {
    return mk_float(atan2(obj_to_double(y), obj_to_double(x)));
}

/* ============== Hyperbolic Functions ============== */

Obj* prim_sinh(Obj* x) { return mk_float(sinh(obj_to_double(x))); }
Obj* prim_cosh(Obj* x) { return mk_float(cosh(obj_to_double(x))); }
Obj* prim_tanh(Obj* x) { return mk_float(tanh(obj_to_double(x))); }

/* ============== Exponential/Logarithmic Functions ============== */

Obj* prim_exp(Obj* x) { return mk_float(exp(obj_to_double(x))); }
Obj* prim_log(Obj* x) { return mk_float(log(obj_to_double(x))); }
Obj* prim_log10(Obj* x) { return mk_float(log10(obj_to_double(x))); }
Obj* prim_log2(Obj* x) { return mk_float(log2(obj_to_double(x))); }
Obj* prim_sqrt(Obj* x) { return mk_float(sqrt(obj_to_double(x))); }

/* ============== Rounding Functions ============== */

Obj* prim_floor(Obj* x) {
    double d = obj_to_double(x);
    if (d == (long)d && d >= IMM_INT_MIN && d <= IMM_INT_MAX) {
        return mk_int((long)d);
    }
    return mk_float(floor(d));
}

Obj* prim_ceil(Obj* x) {
    double d = obj_to_double(x);
    if (d == (long)d && d >= IMM_INT_MIN && d <= IMM_INT_MAX) {
        return mk_int((long)d);
    }
    return mk_float(ceil(d));
}

Obj* prim_round(Obj* x) {
    double d = obj_to_double(x);
    if (d == (long)d && d >= IMM_INT_MIN && d <= IMM_INT_MAX) {
        return mk_int((long)d);
    }
    return mk_float(round(d));
}

Obj* prim_trunc(Obj* x) {
    double d = obj_to_double(x);
    if (d == (long)d && d >= IMM_INT_MIN && d <= IMM_INT_MAX) {
        return mk_int((long)d);
    }
    return mk_float(trunc(d));
}

/* ============== Math Constants ============== */

Obj* prim_pi(void) {
    return mk_float(M_PI);
}

Obj* prim_e(void) {
    return mk_float(M_E);
}

Obj* prim_inf(void) {
    return mk_float(INFINITY);
}

Obj* prim_nan(void) {
    return mk_float(NAN);
}

/* ============== Comparison Functions ============== */

/*
 * prim_min: Minimum of two values
 */
Obj* prim_min(Obj* a, Obj* b) {
    bool is_float_a = IS_BOXED(a) && a->tag == TAG_FLOAT;
    bool is_float_b = IS_BOXED(b) && b->tag == TAG_FLOAT;

    if (is_float_a || is_float_b) {
        double da = obj_to_double(a);
        double db = obj_to_double(b);
        return mk_float(da < db ? da : db);
    } else {
        long ia = obj_to_long(a);
        long ib = obj_to_long(b);
        return mk_int(ia < ib ? ia : ib);
    }
}

/*
 * prim_max: Maximum of two values
 */
Obj* prim_max(Obj* a, Obj* b) {
    bool is_float_a = IS_BOXED(a) && a->tag == TAG_FLOAT;
    bool is_float_b = IS_BOXED(b) && b->tag == TAG_FLOAT;

    if (is_float_a || is_float_b) {
        double da = obj_to_double(a);
        double db = obj_to_double(b);
        return mk_float(da > db ? da : db);
    } else {
        long ia = obj_to_long(a);
        long ib = obj_to_long(b);
        return mk_int(ia > ib ? ia : ib);
    }
}

/*
 * prim_clamp: Clamp value between min and max
 */
Obj* prim_clamp(Obj* x, Obj* min_val, Obj* max_val) {
    Obj* min_result = prim_min(x, max_val);
    Obj* max_result = prim_max(min_result, min_val);
    return max_result;
}

/* ============== Bitwise Operations ============== */

Obj* prim_band(Obj* a, Obj* b) {
    return mk_int(obj_to_long(a) & obj_to_long(b));
}

Obj* prim_bor(Obj* a, Obj* b) {
    return mk_int(obj_to_long(a) | obj_to_long(b));
}

Obj* prim_bxor(Obj* a, Obj* b) {
    return mk_int(obj_to_long(a) ^ obj_to_long(b));
}

Obj* prim_bnot(Obj* x) {
    return mk_int(~obj_to_long(x));
}

Obj* prim_lshift(Obj* x, Obj* n) {
    return mk_int(obj_to_long(x) << obj_to_long(n));
}

Obj* prim_rshift(Obj* x, Obj* n) {
    return mk_int(obj_to_long(x) >> obj_to_long(n));
}

/* ============== Numeric Predicates ============== */

/*
 * prim_is_nan: Check if value is NaN
 */
Obj* prim_is_nan(Obj* x) {
    if (IS_BOXED(x) && x->tag == TAG_FLOAT) {
        return mk_bool(isnan(x->f));
    }
    return mk_bool(0);
}

/*
 * prim_is_inf: Check if value is infinite
 */
Obj* prim_is_inf(Obj* x) {
    if (IS_BOXED(x) && x->tag == TAG_FLOAT) {
        return mk_bool(isinf(x->f));
    }
    return mk_bool(0);
}

/*
 * prim_is_finite: Check if value is finite
 */
Obj* prim_is_finite(Obj* x) {
    if (IS_BOXED(x) && x->tag == TAG_FLOAT) {
        return mk_bool(isfinite(x->f));
    }
    return mk_bool(1);  /* All integers are finite */
}

/* ============== Absolute Value ============== */

Obj* prim_abs(Obj* x) {
    if (IS_BOXED(x) && x->tag == TAG_FLOAT) {
        return mk_float(fabs(x->f));
    } else {
        long l = obj_to_long(x);
        if (l == LONG_MIN) {
            /* Can't represent -LONG_MIN as long, use float */
            return mk_float(fabs((double)l));
        }
        return mk_int(l < 0 ? -l : l);
    }
}

/* ============== Sign Function ============== */

Obj* prim_signum(Obj* x) {
    if (IS_BOXED(x) && x->tag == TAG_FLOAT) {
        double d = x->f;
        if (d > 0) return mk_int(1);
        if (d < 0) return mk_int(-1);
        return mk_int(0);
    } else {
        long l = obj_to_long(x);
        if (l > 0) return mk_int(1);
        if (l < 0) return mk_int(-1);
        return mk_int(0);
    }
}

/* ============== GCD and LCM ============== */

static long gcd_long(long a, long b) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b != 0) {
        long t = b;
        b = a % b;
        a = t;
    }
    return a;
}

Obj* prim_gcd(Obj* a, Obj* b) {
    return mk_int(gcd_long(obj_to_long(a), obj_to_long(b)));
}

Obj* prim_lcm(Obj* a, Obj* b) {
    long ia = obj_to_long(a);
    long ib = obj_to_long(b);
    if (ia == 0 || ib == 0) return mk_int(0);
    return mk_int((ia / gcd_long(ia, ib)) * ib);
}
