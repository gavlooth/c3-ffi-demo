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

#if 0
/*
 * Helper: Create Obj from double
 */
static Obj* double_to_obj(double d) {
    if (d == (long)d && (long long)d >= IMM_INT_MIN && (long long)d <= IMM_INT_MAX) {
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
#endif

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

// TESTED - tests/test_pow.lisp
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

// TESTED
Obj* prim_sin(Obj* x) { return mk_float(sin(obj_to_double(x))); }

// TESTED
Obj* prim_cos(Obj* x) { return mk_float(cos(obj_to_double(x))); }

// TESTED
Obj* prim_tan(Obj* x) { return mk_float(tan(obj_to_double(x))); }

// TESTED - runtime/tests/test_math_numerics.c
Obj* prim_asin(Obj* x) { return mk_float(asin(obj_to_double(x))); }
// TESTED - runtime/tests/test_math_numerics.c
Obj* prim_acos(Obj* x) { return mk_float(acos(obj_to_double(x))); }
// TESTED - runtime/tests/test_math_numerics.c
Obj* prim_atan(Obj* x) { return mk_float(atan(obj_to_double(x))); }

// TESTED - runtime/tests/test_math_numerics.c
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

// TESTED - tests/test_rounding.lisp
Obj* prim_floor(Obj* x) {
    double d = obj_to_double(x);
    if (d == (long)d && (long long)d >= IMM_INT_MIN && (long long)d <= IMM_INT_MAX) {
        return mk_int((long)d);
    }
    return mk_float(floor(d));
}

// TESTED - tests/test_rounding.lisp
Obj* prim_ceil(Obj* x) {
    double d = obj_to_double(x);
    if (d == (long)d && (long long)d >= IMM_INT_MIN && (long long)d <= IMM_INT_MAX) {
        return mk_int((long)d);
    }
    return mk_float(ceil(d));
}

// TESTED - tests/test_rounding.lisp
Obj* prim_round(Obj* x) {
    double d = obj_to_double(x);
    if (d == (long)d && (long long)d >= IMM_INT_MIN && (long long)d <= IMM_INT_MAX) {
        return mk_int((long)d);
    }
    return mk_float(round(d));
}

// TESTED - tests/test_rounding.lisp
Obj* prim_trunc(Obj* x) {
    double d = obj_to_double(x);
    if (d == (long)d && (long long)d >= IMM_INT_MIN && (long long)d <= IMM_INT_MAX) {
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

// TESTED - tests/test_clamp.omni
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

// TESTED - tests/test_bitwise_shift.lisp
Obj* prim_lshift(Obj* x, Obj* n) {
    return mk_int(obj_to_long(x) << obj_to_long(n));
}

// TESTED - tests/test_bitwise_shift.lisp
Obj* prim_rshift(Obj* x, Obj* n) {
    return mk_int(obj_to_long(x) >> obj_to_long(n));
}

/* ============== Numeric Predicates ============== */

// TESTED - tests/test_numeric_predicates.lisp
/*
 * prim_is_nan: Check if value is NaN
 */
Obj* prim_is_nan(Obj* x) {
    if (IS_BOXED(x) && x->tag == TAG_FLOAT) {
        return mk_bool(isnan(x->f));
    }
    return mk_bool(0);
}

// TESTED - tests/test_numeric_predicates.lisp
/*
 * prim_is_inf: Check if value is infinite
 */
Obj* prim_is_inf(Obj* x) {
    if (IS_BOXED(x) && x->tag == TAG_FLOAT) {
        return mk_bool(isinf(x->f));
    }
    return mk_bool(0);
}

// TESTED - tests/test_numeric_predicates.lisp
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

// TESTED - tests/test_numeric_predicates.lisp
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

// TESTED - tests/test_numeric_predicates.lisp
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

// TESTED - tests/test_gcd_lcm.omni
/*
 * prim_gcd - Greatest common divisor
 */
Obj* prim_gcd(Obj* a, Obj* b) {
    return mk_int(gcd_long(obj_to_long(a), obj_to_long(b)));
}

// TESTED - tests/test_gcd_lcm.omni
/*
 * prim_lcm - Least common multiple
 */
Obj* prim_lcm(Obj* a, Obj* b) {
    long ia = obj_to_long(a);
    long ib = obj_to_long(b);
    if (ia == 0 || ib == 0) return mk_int(0);
    return mk_int((ia / gcd_long(ia, ib)) * ib);
}

/* ============== Random Number Generation (Issue 28 P0) ============== */

/*
 * XorShift64 state for high-quality random numbers
 * Better statistical properties than drand48/lrand48
 */
static uint64_t g_rng_state = 0x123456789ABCDEF0ULL;
static bool g_rng_seeded = false;

/*
 * xorshift64 - Fast high-quality PRNG
 */
static uint64_t xorshift64(void) {
    uint64_t x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    g_rng_state = x;
    return x;
}

/*
 * ensure_seeded - Ensure RNG is seeded
 */
static void ensure_seeded(void) {
    if (!g_rng_seeded) {
        /* Seed from time if not explicitly seeded */
        g_rng_state = (uint64_t)time(NULL) ^ 0x123456789ABCDEF0ULL;
        if (g_rng_state == 0) g_rng_state = 1;  /* Avoid zero state */
        g_rng_seeded = true;
    }
}

/*
 * prim_seed_random - Set RNG seed
 *
 * Args: seed (integer)
 * Returns: nothing
 */
Obj* prim_seed_random(Obj* seed) {
    uint64_t s = (uint64_t)obj_to_long(seed);
    if (s == 0) s = 1;  /* Avoid zero state */
    g_rng_state = s;
    g_rng_seeded = true;
    return mk_nothing();
}

/*
 * prim_random - Random float in [0.0, 1.0)
 *
 * Returns: random float between 0 (inclusive) and 1 (exclusive)
 */
Obj* prim_random(void) {
    ensure_seeded();
    /* Convert 64-bit uint to [0, 1) double */
    double d = (xorshift64() >> 11) * (1.0 / 9007199254740992.0);
    return mk_float(d);
}

/*
 * prim_random_int - Random integer in [0, n)
 *
 * Args: n (upper bound, exclusive)
 * Returns: random integer from 0 to n-1
 */
Obj* prim_random_int(Obj* n_obj) {
    ensure_seeded();
    long n = obj_to_long(n_obj);
    if (n <= 0) return mk_int(0);

    /* Avoid modulo bias by rejection sampling */
    uint64_t limit = UINT64_MAX - (UINT64_MAX % (uint64_t)n);
    uint64_t r;
    do {
        r = xorshift64();
    } while (r >= limit);

    return mk_int((long)(r % (uint64_t)n));
}

/*
 * prim_random_range - Random integer in [min, max]
 *
 * Args: min, max (inclusive bounds)
 * Returns: random integer from min to max (inclusive)
 */
Obj* prim_random_range(Obj* min_obj, Obj* max_obj) {
    long min_val = obj_to_long(min_obj);
    long max_val = obj_to_long(max_obj);

    if (min_val > max_val) {
        /* Swap if reversed */
        long tmp = min_val;
        min_val = max_val;
        max_val = tmp;
    }

    long range = max_val - min_val + 1;
    Obj* offset = prim_random_int(mk_int(range));
    return mk_int(min_val + obj_to_long(offset));
}

/*
 * prim_random_float_range - Random float in [min, max]
 *
 * Args: min, max (inclusive bounds)
 * Returns: random float between min and max
 */
Obj* prim_random_float_range(Obj* min_obj, Obj* max_obj) {
    double min_val = obj_to_double(min_obj);
    double max_val = obj_to_double(max_obj);

    Obj* r = prim_random();
    double rand_val = obj_to_double(r);

    return mk_float(min_val + rand_val * (max_val - min_val));
}

/*
 * prim_random_choice - Random element from list/array
 *
 * Args: collection (list or array)
 * Returns: random element from the collection
 */
Obj* prim_random_choice(Obj* coll) {
    if (!coll) return mk_nothing();

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr || arr->len == 0) return mk_nothing();

        Obj* idx = prim_random_int(mk_int(arr->len));
        return arr->data[obj_to_long(idx)];
    }

    /* Handle list (pair) */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        /* Count elements */
        long len = 0;
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            len++;
            p = p->b;
        }
        if (len == 0) return mk_nothing();

        /* Pick random index */
        Obj* idx_obj = prim_random_int(mk_int(len));
        long idx = obj_to_long(idx_obj);

        /* Walk to that element */
        p = coll;
        for (long i = 0; i < idx && p; i++) {
            p = p->b;
        }
        return p ? p->a : mk_nothing();
    }

    return mk_nothing();
}

/*
 * prim_shuffle - Fisher-Yates shuffle of list/array
 *
 * Args: collection (list or array)
 * Returns: new shuffled collection (does not modify original)
 */
Obj* prim_shuffle(Obj* coll) {
    if (!coll) return mk_nothing();

    /* Handle array */
    if (IS_BOXED(coll) && coll->tag == TAG_ARRAY) {
        Array* arr = (Array*)coll->ptr;
        if (!arr || arr->len == 0) return mk_array(0);

        /* Create new array with same elements */
        Obj* result = mk_array(arr->len);
        for (int i = 0; i < arr->len; i++) {
            array_push(result, arr->data[i]);
        }

        /* Fisher-Yates shuffle */
        Array* res_arr = (Array*)result->ptr;
        for (int i = res_arr->len - 1; i > 0; i--) {
            Obj* j_obj = prim_random_int(mk_int(i + 1));
            int j = (int)obj_to_long(j_obj);
            /* Swap */
            Obj* tmp = res_arr->data[i];
            res_arr->data[i] = res_arr->data[j];
            res_arr->data[j] = tmp;
        }

        return result;
    }

    /* Handle list - convert to array, shuffle, convert back */
    if (IS_BOXED(coll) && coll->tag == TAG_PAIR) {
        /* Count and collect elements */
        long len = 0;
        Obj* p = coll;
        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            len++;
            p = p->b;
        }
        if (len == 0) return NULL;  /* Empty list */

        /* Create array */
        Obj** elems = malloc(sizeof(Obj*) * len);
        p = coll;
        for (long i = 0; i < len; i++) {
            elems[i] = p->a;
            p = p->b;
        }

        /* Fisher-Yates shuffle */
        for (long i = len - 1; i > 0; i--) {
            Obj* j_obj = prim_random_int(mk_int(i + 1));
            long j = obj_to_long(j_obj);
            Obj* tmp = elems[i];
            elems[i] = elems[j];
            elems[j] = tmp;
        }

        /* Convert back to list */
        Obj* result = NULL;
        for (long i = len - 1; i >= 0; i--) {
            result = mk_pair(elems[i], result);
        }

        free(elems);
        return result;
    }

    return mk_nothing();
}
