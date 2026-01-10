/*
 * spec_decision.c - Specialization Decision Engine Implementation
 *
 * Implementation of specialization decision logic.
 *
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 2)
 */

#include "spec_decision.h"
#include <string.h>
#include <stdio.h>

/* ============== Policy Determination ============== */

static bool is_spec_always(const char* func_name) {
    return is_builtin_primitive(func_name);
}

static bool is_spec_never(const char* func_name) {
    /* Functions that should never be specialized */
    if (!func_name) return false;

    /* Generic functions that work on any type */
    if (strcmp(func_name, "apply") == 0 ||
        strcmp(func_name, "map") == 0 ||
        strcmp(func_name, "filter") == 0 ||
        strcmp(func_name, "fold") == 0 ||
        strcmp(func_name, "reduce") == 0) {
        return true;
    }

    /* I/O functions */
    if (strcmp(func_name, "print") == 0 ||
        strcmp(func_name, "println") == 0 ||
        strcmp(func_name, "display") == 0 ||
        strcmp(func_name, "read") == 0) {
        return true;
    }

    return false;
}

SpecPolicy get_specialization_policy(AnalysisContext* ctx,
                                    const char* func_name) {
    if (!func_name) return SPEC_NEVER;

    if (is_spec_always(func_name)) {
        return SPEC_ALWAYS;
    }

    if (is_spec_never(func_name)) {
        return SPEC_NEVER;
    }

    return SPEC_MAYBE;
}

/* ============== Type Checking Helpers ============== */

bool all_types_known(ConcreteType** param_types,
                    int param_count) {
    if (!param_types) return param_count == 0;

    for (int i = 0; i < param_count; i++) {
        if (!param_types[i] || param_types[i]->kind == TYPE_KIND_ANY) {
            return false;
        }
    }

    return true;
}

bool all_params_unboxable(ConcreteType** param_types,
                         int param_count) {
    if (!param_types) return param_count == 0;

    for (int i = 0; i < param_count; i++) {
        if (!type_is_unboxable(param_types[i])) {
            return false;
        }
    }

    return true;
}

/* ============== Decision Functions ============== */

bool should_specialize(AnalysisContext* ctx,
                      const char* func_name,
                      ConcreteType** param_types,
                      int param_count) {
    /* Get policy for this function */
    SpecPolicy policy = get_specialization_policy(ctx, func_name);

    switch (policy) {
        case SPEC_ALWAYS:
            /* Always specialize if types are known */
            return all_types_known(param_types, param_count);

        case SPEC_NEVER:
            /* Never specialize */
            return false;

        case SPEC_HOT:
            /* Specialize if hot and types known */
            // TODO: Track call frequency
            return all_types_known(param_types, param_count);

        case SPEC_MAYBE:
            /* Decide based on heuristics */
            return is_worth_specializing(ctx, func_name, param_types, param_count);

        default:
            return false;
    }
}

bool is_worth_specializing(AnalysisContext* ctx,
                          const char* func_name,
                          ConcreteType** param_types,
                          int param_count) {
    /* Must have known types */
    if (!all_types_known(param_types, param_count)) {
        return false;
    }

    /* Must be a builtin or user-defined function */
    if (!func_name) return false;

    /* Estimate speedup */
    ConcreteType** generic_types = NULL;  // TODO: track original types
    double speedup = estimate_speedup(generic_types, param_types, param_count);

    /* Specialization is worth it if speedup > 2x */
    return speedup > 2.0;
}

double estimate_speedup(ConcreteType** from_types,
                       ConcreteType** to_types,
                       int param_count) {
    double total_speedup = 1.0;

    for (int i = 0; i < param_count; i++) {
        ConcreteType* from = from_types ? from_types[i] : NULL;
        ConcreteType* to = to_types ? to_types[i] : NULL;

        if (!from || !to) continue;

        /* Going from boxed to unboxed: ~25x speedup */
        if (from->kind == TYPE_KIND_ANY && type_is_unboxable(to)) {
            total_speedup *= 25.0;
        }
        /* Going from unboxed to unboxed: no speedup */
        else if (type_is_unboxable(from) && type_is_unboxable(to)) {
            total_speedup *= 1.0;
        }
        /* Going from unboxed to boxed: slower (unlikely) */
        else if (type_is_unboxable(from) && to->kind == TYPE_KIND_ANY) {
            total_speedup *= 0.5;
        }
    }

    return total_speedup;
}

SpecCriteria build_criteria(AnalysisContext* ctx,
                           const char* func_name,
                           ConcreteType** param_types,
                           int param_count) {
    SpecCriteria criteria = {0};

    criteria.is_arithmetic = is_arithmetic_primitive(func_name);
    criteria.is_comparison = is_comparison_primitive(func_name);
    criteria.has_type_hints = all_types_known(param_types, param_count);

    // TODO: Track call frequency, loop hotness, code size

    return criteria;
}

/* ============== Primitive Classification ============== */

bool is_arithmetic_primitive(const char* func_name) {
    if (!func_name) return false;

    return strcmp(func_name, "+") == 0 ||
           strcmp(func_name, "-") == 0 ||
           strcmp(func_name, "*") == 0 ||
           strcmp(func_name, "/") == 0 ||
           strcmp(func_name, "mod") == 0 ||
           strcmp(func_name, "quotient") == 0 ||
           strcmp(func_name, "remainder") == 0 ||
           strcmp(func_name, "abs") == 0 ||
           strcmp(func_name, "negate") == 0;
}

bool is_comparison_primitive(const char* func_name) {
    if (!func_name) return false;

    return strcmp(func_name, "<") == 0 ||
           strcmp(func_name, ">") == 0 ||
           strcmp(func_name, "<=") == 0 ||
           strcmp(func_name, ">=") == 0 ||
           strcmp(func_name, "=") == 0 ||
           strcmp(func_name, "!=") == 0 ||
           strcmp(func_name, "==") == 0;
}

bool is_math_primitive(const char* func_name) {
    if (!func_name) return false;

    return strcmp(func_name, "sin") == 0 ||
           strcmp(func_name, "cos") == 0 ||
           strcmp(func_name, "tan") == 0 ||
           strcmp(func_name, "asin") == 0 ||
           strcmp(func_name, "acos") == 0 ||
           strcmp(func_name, "atan") == 0 ||
           strcmp(func_name, "atan2") == 0 ||
           strcmp(func_name, "sinh") == 0 ||
           strcmp(func_name, "cosh") == 0 ||
           strcmp(func_name, "tanh") == 0 ||
           strcmp(func_name, "exp") == 0 ||
           strcmp(func_name, "log") == 0 ||
           strcmp(func_name, "log10") == 0 ||
           strcmp(func_name, "log2") == 0 ||
           strcmp(func_name, "sqrt") == 0 ||
           strcmp(func_name, "pow") == 0 ||
           strcmp(func_name, "floor") == 0 ||
           strcmp(func_name, "ceil") == 0 ||
           strcmp(func_name, "round") == 0 ||
           strcmp(func_name, "trunc") == 0;
}

bool is_builtin_primitive(const char* func_name) {
    return is_arithmetic_primitive(func_name) ||
           is_comparison_primitive(func_name) ||
           is_math_primitive(func_name);
}

/* ============== Utility Functions ============== */

const char* spec_policy_name(SpecPolicy policy) {
    switch (policy) {
        case SPEC_ALWAYS: return "ALWAYS";
        case SPEC_HOT:    return "HOT";
        case SPEC_NEVER:  return "NEVER";
        case SPEC_MAYBE:  return "MAYBE";
        default:          return "UNKNOWN";
    }
}
