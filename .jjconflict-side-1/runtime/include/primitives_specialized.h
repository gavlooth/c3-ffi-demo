/*
 * primitives_specialized.h - Specialized Primitive Functions
 *
 * Box/unbox utilities and specialized primitive function declarations.
 * Part of Phase 27: Julia-Level Type Specialization.
 *
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 3)
 */

#ifndef OMNILISP_PRIMITIVES_SPECIALIZED_H
#define OMNILISP_PRIMITIVES_SPECIALIZED_H

#include "omni.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Box/Unbox Utilities ============== */

/**
 * Box an int64_t value into an Obj*
 *
 * Creates a tagged integer representation if the value fits in immediate range,
 * otherwise allocates a heap object.
 */
Obj* box_int(int64_t value);

/**
 * Unbox an Obj* to int64_t
 *
 * Extracts the integer value from an Obj*.
 * Behavior is undefined if the Obj* is not an integer.
 */
int64_t unbox_int(Obj* obj);

/**
 * Box a double value into an Obj*
 *
 * Floats always require heap allocation due to IEEE 754 using all 64 bits.
 */
Obj* box_float(double value);

/**
 * Unbox an Obj* to double
 *
 * Extracts the float value from an Obj*.
 * Behavior is undefined if the Obj* is not a float.
 */
double unbox_float(Obj* obj);

/**
 * Box a char value into an Obj*
 *
 * Creates a tagged character representation.
 */
Obj* box_char(char value);

/**
 * Unbox an Obj* to char
 *
 * Extracts the character value from an Obj*.
 * Behavior is undefined if the Obj* is not a character.
 */
char unbox_char(Obj* obj);

/**
 * Box a bool value into an Obj*
 *
 * Creates a tagged boolean representation.
 */
Obj* box_bool(bool value);

/**
 * Unbox an Obj* to bool
 *
 * Extracts the boolean value from an Obj*.
 * Behavior is undefined if the Obj* is not a boolean.
 */
bool unbox_bool(Obj* obj);

/* ============== Specialized Arithmetic Primitives ============== */

/* Integer operations */
Obj* add_Int_Int(Obj* a, Obj* b);
Obj* sub_Int_Int(Obj* a, Obj* b);
Obj* mul_Int_Int(Obj* a, Obj* b);
Obj* div_Int_Int(Obj* a, Obj* b);

/* Float operations */
Obj* add_Float_Float(Obj* a, Obj* b);
Obj* sub_Float_Float(Obj* a, Obj* b);
Obj* mul_Float_Float(Obj* a, Obj* b);
Obj* div_Float_Float(Obj* a, Obj* b);

/* Mixed operations (Int + Float = Float) */
Obj* add_Int_Float(Obj* a, Obj* b);
Obj* add_Float_Int(Obj* a, Obj* b);
Obj* sub_Int_Float(Obj* a, Obj* b);
Obj* sub_Float_Int(Obj* a, Obj* b);
Obj* mul_Int_Float(Obj* a, Obj* b);
Obj* mul_Float_Int(Obj* a, Obj* b);
Obj* div_Int_Float(Obj* a, Obj* b);
Obj* div_Float_Int(Obj* a, Obj* b);

/* ============== Specialized Comparison Primitives ============== */

/* Integer comparisons */
Obj* lt_Int_Int(Obj* a, Obj* b);
Obj* gt_Int_Int(Obj* a, Obj* b);
Obj* lte_Int_Int(Obj* a, Obj* b);
Obj* gte_Int_Int(Obj* a, Obj* b);
Obj* eq_Int_Int(Obj* a, Obj* b);
Obj* ne_Int_Int(Obj* a, Obj* b);

/* Float comparisons */
Obj* lt_Float_Float(Obj* a, Obj* b);
Obj* gt_Float_Float(Obj* a, Obj* b);
Obj* lte_Float_Float(Obj* a, Obj* b);
Obj* gte_Float_Float(Obj* a, Obj* b);
Obj* eq_Float_Float(Obj* a, Obj* b);
Obj* ne_Float_Float(Obj* a, Obj* b);

/* Mixed comparisons */
Obj* lt_Int_Float(Obj* a, Obj* b);
Obj* lt_Float_Int(Obj* a, Obj* b);
Obj* eq_Int_Float(Obj* a, Obj* b);
Obj* eq_Float_Int(Obj* a, Obj* b);

/* ============== Specialized Math Library Functions ============== */

Obj* sqrt_Float(Obj* x);
Obj* sin_Float(Obj* x);
Obj* cos_Float(Obj* x);
Obj* tan_Float(Obj* x);
Obj* log_Float(Obj* x);
Obj* exp_Float(Obj* x);
Obj* pow_Float_Float(Obj* x, Obj* y);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_PRIMITIVES_SPECIALIZED_H */
