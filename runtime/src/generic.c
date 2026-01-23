/*
 * generic.c - Generic function and multiple dispatch system for OmniLisp
 *
 * Implements Julia-style multiple dispatch with method tables sorted by specificity.
 *
 * API:
 *   - mk_generic: Create a new generic function
 *   - generic_add_method: Add a method to a generic function
 *   - call_generic: Call a generic with multiple dispatch
 *   - omni_generic_lookup: Lookup most specific method for arguments
 *   - omni_generic_invoke: Invoke a generic with arguments
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../include/omni.h"
#include "internal_types.h"
#include "memory/region_core.h"

/* ============== Generic Function Creation ============== */

/* NOTE: mk_generic() is defined in runtime.c as a wrapper around mk_generic_region() */

/* ============== Method Management ============== */

/*
 * generic_add_method: Add a method to a generic function
 *
 * Args:
 *   - generic_obj: The generic function object
 *   - param_kinds: Array of Kind objects for each parameter
 *   - param_count: Number of parameters
 *   - impl: Method implementation function
 *
 * Returns: The generic function object (for chaining)
 *
 * Example:
 *   (defmethod my-function ((x Int) (y Int)) ...)
 *
 * Note: This computes specificity using the type hierarchy and
 * inserts the method in sorted order (most specific first).
 */
Obj* generic_add_method(Obj* generic_obj, Obj** param_kinds, int param_count, ClosureFn impl) {
    if (!generic_obj || !IS_BOXED(generic_obj) || generic_obj->tag != TAG_GENERIC) {
        fprintf(stderr, "generic_add_method: Invalid generic object\n");
        return NULL;
    }

    Generic* g = (Generic*)generic_obj->ptr;
    if (!g) {
        fprintf(stderr, "generic_add_method: Generic has no Generic struct\n");
        return NULL;
    }

    /* Allocate new method in global region */
    omni_ensure_global_region();
    Region* r = omni_get_global_region();

    MethodInfo* method = region_alloc(r, sizeof(MethodInfo));
    if (!method) {
        fprintf(stderr, "generic_add_method: Failed to allocate method\n");
        return NULL;
    }

    /* Copy parameter kinds */
    method->param_count = param_count;
    if (param_count > 0 && param_kinds) {
        method->param_kinds = region_alloc(r, sizeof(Obj*) * param_count);
        for (int i = 0; i < param_count; i++) {
            method->param_kinds[i] = param_kinds[i];
        }
    } else {
        method->param_kinds = NULL;
    }

    method->impl = impl;

    /* Compute specificity score */
    /* For now, use a simple heuristic: more specific types = higher score */
    /* This will be replaced with omni_compute_specificity in Phase 21 */
    method->specificity = 0;
    for (int i = 0; i < param_count; i++) {
        if (param_kinds[i] && IS_BOXED(param_kinds[i]) && param_kinds[i]->tag == TAG_KIND) {
            Kind* k = (Kind*)param_kinds[i]->ptr;
            /* Specificity based on type name length (temporary heuristic) */
            if (k && k->name) {
                method->specificity += (int)strlen(k->name);
            }
        }
    }

    /* Insert method in sorted order (most specific first) */
    MethodInfo* prev = NULL;
    MethodInfo* current = g->methods;

    while (current && current->specificity >= method->specificity) {
        prev = current;
        current = current->next;
    }

    /* Insert at beginning or between prev and current */
    if (!prev) {
        method->next = g->methods;
        g->methods = method;
    } else {
        method->next = prev->next;
        prev->next = method;
    }

    g->method_count++;

    return generic_obj;
}

/* ============== Type Checking ============== */

/*
 * check_argument_types: Check if arguments match parameter types
 *
 * Args:
 *   - param_kinds: Expected types for each parameter
 *   - param_count: Number of parameters
 *   - args: Actual arguments
 *   - argc: Number of arguments
 *
 * Returns: true if all arguments match, false otherwise
 *
 * This implements subtype checking: an argument matches if its type
 * is a subtype of the expected parameter type.
 */
static bool check_argument_types(Obj** param_kinds, int param_count, Obj** args, int argc) {
    if (param_count != argc) {
        return false;
    }

    for (int i = 0; i < param_count; i++) {
        Obj* expected_kind = param_kinds[i];
        Obj* arg = args[i];

        if (!expected_kind || !arg) {
            return false;
        }

        /* For now, use a simple tag-based check */
        /* This will be replaced with omni_type_is_subtype in Phase 21 */

        if (!IS_BOXED(expected_kind) || expected_kind->tag != TAG_KIND) {
            continue; /* Skip invalid kinds */
        }

        Kind* kind = (Kind*)expected_kind->ptr;
        if (!kind || !kind->name) {
            continue;
        }

        /* Check argument type */
        if (IS_IMMEDIATE(arg)) {
            /* Immediate types */
            if (IS_IMMEDIATE_INT(arg)) {
                if (strcmp(kind->name, "Int") != 0) return false;
            } else if (IS_IMMEDIATE_CHAR(arg)) {
                if (strcmp(kind->name, "Char") != 0) return false;
            } else if (IS_IMMEDIATE_BOOL(arg)) {
                if (strcmp(kind->name, "Bool") != 0) return false;
            }
        } else if (IS_BOXED(arg)) {
            /* Boxed types */
            if (arg->tag == TAG_PAIR || arg->tag == TAG_SYM) {
                if (strcmp(kind->name, "Pair") != 0 && strcmp(kind->name, "Symbol") != 0) {
                    return false;
                }
            } else if (arg->tag == TAG_STRING) {
                if (strcmp(kind->name, "String") != 0) return false;
            } else if (arg->tag == TAG_ARRAY) {
                if (strcmp(kind->name, "Array") != 0) return false;
            } else if (arg->tag == TAG_CLOSURE) {
                if (strcmp(kind->name, "Function") != 0 && strcmp(kind->name, "Closure") != 0) {
                    return false;
                }
            }
        }
    }

    return true;
}

/* ============== Generic Dispatch ============== */

/*
 * omni_generic_lookup: Lookup the most specific method for arguments
 *
 * Args:
 *   - generic_obj: The generic function object
 *   - args: Array of arguments
 *   - argc: Number of arguments
 *
 * Returns: The most specific method that matches, or NULL if none match
 *
 * This searches the method table in order (already sorted by specificity)
 * and returns the first method whose parameter types match the arguments.
 */
MethodInfo* omni_generic_lookup(Obj* generic_obj, Obj** args, int argc) {
    if (!generic_obj || !IS_BOXED(generic_obj) || generic_obj->tag != TAG_GENERIC) {
        return NULL;
    }

    Generic* g = (Generic*)generic_obj->ptr;
    if (!g) {
        return NULL;
    }

    /* Search methods in order (most specific first) */
// REVIEWED:NAIVE
    for (MethodInfo* method = g->methods; method; method = method->next) {
        if (check_argument_types(method->param_kinds, method->param_count, args, argc)) {
            return method;
        }
    }

    /* No applicable method found */
    return NULL;
}

/*
 * omni_generic_invoke: Invoke a generic function with arguments
 *
 * Args:
 *   - generic_obj: The generic function object
 *   - args: Array of arguments
 *   - argc: Number of arguments
 *
 * Returns: The result of calling the most specific method, or an error
 *
 * This looks up the applicable method and calls it with the given arguments.
 * If no method applies, it returns an error object.
 */
Obj* omni_generic_invoke(Obj* generic_obj, Obj** args, int argc) {
    MethodInfo* method = omni_generic_lookup(generic_obj, args, argc);

    if (!method) {
        /* No applicable method */
        Generic* g = (Generic*)generic_obj->ptr;
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "No applicable method for generic %s with %d arguments",
                 g ? g->name : "<unknown>", argc);
        return mk_error_region(omni_get_global_region(), buffer);
    }

    /* Call the method implementation */
    // ClosureFn signature: (Obj** captures, Obj** args, int argc)
    // Methods don't have captures, so pass NULL for first argument
    return method->impl(NULL, args, argc);
}

/*
 * call_generic: Call a generic function with multiple dispatch
 *
 * Args:
 *   - generic_obj: The generic function object
 *   - args: Array of arguments
 *   - argc: Number of arguments
 *
 * Returns: The result of calling the most specific method
 *
 * This is a wrapper around omni_generic_invoke for backward compatibility.
 */
Obj* call_generic(Obj* generic_obj, Obj** args, int argc) {
    return omni_generic_invoke(generic_obj, args, argc);
}

/* ============== Arity Checking ============== */

/*
 * omni_check_arity: Check if a generic function can accept the given number of arguments
 *
 * Args:
 *   - generic_obj: The generic function object
 *   - argc: Number of arguments
 *
 * Returns: true if at least one method has the given arity, false otherwise
 *
 * This is used for arity validation before dispatch.
 */
bool omni_check_arity(Obj* generic_obj, int argc) {
    if (!generic_obj || !IS_BOXED(generic_obj) || generic_obj->tag != TAG_GENERIC) {
        return false;
    }

    Generic* g = (Generic*)generic_obj->ptr;
    if (!g) {
        return false;
    }

    /* Check if any method has the given arity */
// REVIEWED:NAIVE
    for (MethodInfo* method = g->methods; method; method = method->next) {
        if (method->param_count == argc) {
            return true;
        }
    }

    return false;
}

/* ============== Generic Function Metadata ============== */

/*
 * omni_generic_name: Get the name of a generic function
 *
 * Args: generic_obj - The generic function object
 * Returns: The name string, or NULL if invalid
 */
const char* omni_generic_name(Obj* generic_obj) {
    if (!generic_obj || !IS_BOXED(generic_obj) || generic_obj->tag != TAG_GENERIC) {
        return NULL;
    }

    Generic* g = (Generic*)generic_obj->ptr;
    return g ? g->name : NULL;
}

/*
 * omni_generic_method_count: Get the number of methods in a generic function
 *
 * Args: generic_obj - The generic function object
 * Returns: The number of methods, or 0 if invalid
 */
int omni_generic_method_count(Obj* generic_obj) {
    if (!generic_obj || !IS_BOXED(generic_obj) || generic_obj->tag != TAG_GENERIC) {
        return 0;
    }

    Generic* g = (Generic*)generic_obj->ptr;
    return g ? g->method_count : 0;
}
