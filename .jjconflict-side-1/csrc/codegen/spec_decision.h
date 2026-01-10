/*
 * spec_decision.h - Specialization Decision Engine
 *
 * Determines which functions should be specialized and for which
 * type signatures.
 *
 * Part of Phase 27: Julia-Level Type Specialization.
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 2)
 */

#ifndef OMNILISP_SPEC_DECISION_H
#define OMNILISP_SPEC_DECISION_H

#include "../analysis/type_env.h"
#include "../analysis/analysis.h"
#include "spec_db.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Specialization Policy Enumeration ============== */

/**
 * SpecPolicy - When should a function be specialized?
 *
 * Determines the aggressiveness of specialization for different
 * kinds of functions.
 */
typedef enum {
    SPEC_ALWAYS = 0,       /* Always specialize (arithmetic primitives) */
    SPEC_HOT,              /* Specialize if called frequently */
    SPEC_NEVER,            /* Never specialize (generic functions) */
    SPEC_MAYBE,            /* Decide based on heuristics */
} SpecPolicy;

/* ============== Decision Criteria ============== */

/**
 * SpecCriteria - Information used to make specialization decisions
 */
typedef struct {
    int call_count;         /* Number of times function is called */
    bool is_arithmetic;     /* Is this an arithmetic operation? */
    bool is_comparison;     /* Is this a comparison operation? */
    bool is_loop_hot;       /* Is function called in a hot loop? */
    bool has_type_hints;    /* Are type annotations present? */
    int code_size_estimate; /* Estimated code size if specialized */
} SpecCriteria;

/* ============== Decision API ============== */

/**
 * Get the specialization policy for a function
 *
 * Determines whether a function should always, never, or maybe
 * be specialized based on its characteristics.
 *
 * Args:
 *   ctx: Analysis context
 *   func_name: Function name
 *
 * Returns:
 *   The specialization policy for this function
 */
SpecPolicy get_specialization_policy(AnalysisContext* ctx,
                                    const char* func_name);

/**
 * Check if a function should be specialized
 *
 * Makes the final decision about whether to specialize a function
 * for a specific type signature.
 *
 * Args:
 *   ctx: Analysis context
 *   func_name: Function name
 *   param_types: Parameter types
 *   param_count: Number of parameters
 *
 * Returns:
 *   true if function should be specialized
 */
bool should_specialize(AnalysisContext* ctx,
                      const char* func_name,
                      ConcreteType** param_types,
                      int param_count);

/**
 * Check if specialization is worth it
 *
 * Evaluates whether the performance benefit justifies the
 * code size cost.
 *
 * Args:
 *   ctx: Analysis context
 *   func_name: Function name
 *   param_types: Parameter types
 *   param_count: Number of parameters
 *
 * Returns:
 *   true if benefit > cost
 */
bool is_worth_specializing(AnalysisContext* ctx,
                          const char* func_name,
                          ConcreteType** param_types,
                          int param_count);

/**
 * Estimate performance speedup from specialization
 *
 * Args:
 *   from_types: Original types (before specialization)
 *   to_types: Specialized types
 *   param_count: Number of parameters
 *
 * Returns:
 *   Estimated speedup factor (1.0 = no speedup, 25.0 = 25x faster)
 *
 * Examples:
 *   [Any, Any] → [Float, Float]: 25.0 (boxed → unboxed)
 *   [Int, Int] → [Int, Int]: 1.0 (already unboxed)
 */
double estimate_speedup(ConcreteType** from_types,
                       ConcreteType** to_types,
                       int param_count);

/**
 * Build criteria for making a decision
 *
 * Gathers information about a function call site to inform
 * the specialization decision.
 *
 * Args:
 *   ctx: Analysis context
 *   func_name: Function name
 *   param_types: Parameter types
 *   param_count: Number of parameters
 *
 * Returns:
 *   Populated SpecCriteria structure
 */
SpecCriteria build_criteria(AnalysisContext* ctx,
                           const char* func_name,
                           ConcreteType** param_types,
                           int param_count);

/**
 * Check if all parameter types are known
 *
 * A function can only be specialized if all parameter types are
 * known (no TYPE_KIND_ANY).
 *
 * Args:
 *   param_types: Parameter types
 *   param_count: Number of parameters
 *
 * Returns:
 *   true if all types are known
 */
bool all_types_known(ConcreteType** param_types,
                    int param_count);

/**
 * Check if all parameters are unboxable primitives
 *
 * Args:
 *   param_types: Parameter types
 *   param_count: Number of parameters
 *
 * Returns:
 *   true if all parameters are primitive types
 */
bool all_params_unboxable(ConcreteType** param_types,
                         int param_count);

/**
 * Get the name of a SpecPolicy
 *
 * Args:
 *   policy: Policy enum
 *
 * Returns:
 *   String name
 */
const char* spec_policy_name(SpecPolicy policy);

/* ============== Common Primitive Specializations ============== */

/**
 * Check if a function name is an arithmetic primitive
 *
 * Args:
 *   func_name: Function name
 *
 * Returns:
 *   true if function is +, -, *, /, mod, etc.
 */
bool is_arithmetic_primitive(const char* func_name);

/**
 * Check if a function name is a comparison primitive
 *
 * Args:
 *   func_name: Function name
 *
 * Returns:
 *   true if function is <, >, <=, >=, =, etc.
 */
bool is_comparison_primitive(const char* func_name);

/**
 * Check if a function name is a math library primitive
 *
 * Args:
 *   func_name: Function name
 *
 * Returns:
 *   true if function is sin, cos, sqrt, etc.
 */
bool is_math_primitive(const char* func_name);

/**
 * Check if a function is a builtin primitive
 *
 * Args:
 *   func_name: Function name
 *
 * Returns:
 *   true if function is any kind of builtin primitive
 */
bool is_builtin_primitive(const char* func_name);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_SPEC_DECISION_H */
