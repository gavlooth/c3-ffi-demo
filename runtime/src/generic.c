/*
 * generic.c - Generic function and multiple dispatch system for OmniLisp
 *
 * Implements Julia-style multiple dispatch with method tables sorted by specificity.
 *
 * Optimizations:
 *   - dispatch_cache: HashMap caching type signature -> MethodInfo* for O(1) repeat lookups
 *   - by_arity: Array indexed by arity for O(1) arity filtering
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
#include "util/hashmap.h"

/* Maximum arity for the by_arity fast path (covers common cases) */
#define MAX_ARITY_FAST 16

/*
 * compute_type_signature_hash: Compute a hash for the argument types.
 * Used as cache key for dispatch_cache.
 */
static uint64_t compute_type_signature_hash(Obj** args, int argc) {
    uint64_t hash = 0xcbf29ce484222325ULL;  /* FNV-1a offset basis */
    for (int i = 0; i < argc; i++) {
        Obj* arg = args[i];
        uint64_t val;
        if (IS_IMMEDIATE(arg)) {
            /* Hash the immediate value directly */
            val = (uint64_t)(uintptr_t)arg;
        } else if (IS_BOXED(arg)) {
            /* Hash the tag */
            val = (uint64_t)arg->tag;
        } else {
            val = 0;
        }
        hash ^= val;
        hash *= 0x100000001b3ULL;  /* FNV-1a prime */
    }
    /* Mix in the arity */
    hash ^= (uint64_t)argc;
    hash *= 0x100000001b3ULL;
    return hash;
}

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

    /* Update by_arity index for O(1) arity lookup */
    if (param_count < MAX_ARITY_FAST) {
        if (!g->by_arity) {
            /* Lazily allocate by_arity array */
            g->by_arity = region_alloc(r, sizeof(MethodInfo*) * MAX_ARITY_FAST);
            if (g->by_arity) {
                memset(g->by_arity, 0, sizeof(MethodInfo*) * MAX_ARITY_FAST);
                g->max_arity = MAX_ARITY_FAST;
            }
        }
        if (g->by_arity && param_count < g->max_arity) {
            /* Store pointer to first method with this arity (most specific) */
            /* Note: We store the most specific method; if a more specific one
             * is added later, we need to update this slot. */
            if (!g->by_arity[param_count] ||
                method->specificity > g->by_arity[param_count]->specificity) {
                g->by_arity[param_count] = method;
            }
        }
    }

    /* Invalidate dispatch cache when methods change */
    if (g->dispatch_cache) {
        hashmap_clear((HashMap*)g->dispatch_cache);
    }

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

        /* Handle missing expected_kind: invalid method definition, skip */
        if (!expected_kind) {
            continue;
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

        /* P1 Fix: Handle nil/NULL arguments */
        /* nil matches "Nil", "Nothing", or "Any" types */
        if (!arg || (IS_BOXED(arg) && arg->tag == TAG_NOTHING)) {
            if (strcmp(kind->name, "Nil") == 0 ||
                strcmp(kind->name, "Nothing") == 0 ||
                strcmp(kind->name, "Any") == 0) {
                continue; /* nil matches these types */
            }
            return false; /* nil doesn't match other types */
        }

        /* "Any" type matches all values */
        if (strcmp(kind->name, "Any") == 0) {
            continue;
        }

        /* Check argument type */
        if (IS_IMMEDIATE(arg)) {
            /* Immediate types */
            if (IS_IMMEDIATE_INT(arg)) {
                if (strcmp(kind->name, "Int") != 0 &&
                    strcmp(kind->name, "Number") != 0) return false;
            } else if (IS_IMMEDIATE_CHAR(arg)) {
                if (strcmp(kind->name, "Char") != 0) return false;
            } else if (IS_IMMEDIATE_BOOL(arg)) {
                if (strcmp(kind->name, "Bool") != 0) return false;
            }
        } else if (IS_BOXED(arg)) {
            /* Boxed types */
            if (arg->tag == TAG_PAIR) {
                if (strcmp(kind->name, "Pair") != 0 &&
                    strcmp(kind->name, "List") != 0) {
                    return false;
                }
            } else if (arg->tag == TAG_SYM) {
                if (strcmp(kind->name, "Symbol") != 0) {
                    return false;
                }
            } else if (arg->tag == TAG_STRING) {
                if (strcmp(kind->name, "String") != 0) return false;
            } else if (arg->tag == TAG_ARRAY) {
                if (strcmp(kind->name, "Array") != 0) return false;
            } else if (arg->tag == TAG_CLOSURE) {
                if (strcmp(kind->name, "Function") != 0 &&
                    strcmp(kind->name, "Closure") != 0) {
                    return false;
                }
            } else if (arg->tag == TAG_FLOAT) {
                if (strcmp(kind->name, "Float") != 0 &&
                    strcmp(kind->name, "Number") != 0) return false;
            } else if (arg->tag == TAG_INT) {
                if (strcmp(kind->name, "Int") != 0 &&
                    strcmp(kind->name, "Number") != 0) return false;
            } else if (arg->tag == TAG_DICT) {
                if (strcmp(kind->name, "Dict") != 0) return false;
            } else if (arg->tag == TAG_SET) {
                if (strcmp(kind->name, "Set") != 0) return false;
            } else if (arg->tag == TAG_GENERIC) {
                if (strcmp(kind->name, "Generic") != 0 &&
                    strcmp(kind->name, "Function") != 0) return false;
            } else if (arg->tag == TAG_KIND) {
                if (strcmp(kind->name, "Type") != 0 &&
                    strcmp(kind->name, "Kind") != 0) return false;
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
 * Optimization: Uses dispatch_cache for O(1) repeat lookups of the same
 * type signature, and by_arity for fast arity filtering.
 */
MethodInfo* omni_generic_lookup(Obj* generic_obj, Obj** args, int argc) {
    if (!generic_obj || !IS_BOXED(generic_obj) || generic_obj->tag != TAG_GENERIC) {
        return NULL;
    }

    Generic* g = (Generic*)generic_obj->ptr;
    if (!g) {
        return NULL;
    }

    /* Fast path: Check dispatch cache for previously resolved signature */
    uint64_t sig_hash = compute_type_signature_hash(args, argc);
    if (g->dispatch_cache) {
        MethodInfo* cached = (MethodInfo*)hashmap_get((HashMap*)g->dispatch_cache, (void*)(uintptr_t)sig_hash);
        if (cached) {
            return cached;
        }
    }

    /* Fast path: Use by_arity to check if any method has this arity */
    if (g->by_arity && argc < g->max_arity && !g->by_arity[argc]) {
        /* No methods with this arity exist */
        return NULL;
    }

    /* Fallback: Linear search through methods (sorted by specificity) */
    MethodInfo* result = NULL;
    for (MethodInfo* method = g->methods; method; method = method->next) {
        if (check_argument_types(method->param_kinds, method->param_count, args, argc)) {
            result = method;
            break;
        }
    }

    /* Cache the result for future lookups */
    if (result) {
        if (!g->dispatch_cache) {
            /* Lazily allocate dispatch cache */
            g->dispatch_cache = hashmap_new();
        }
        if (g->dispatch_cache) {
            hashmap_put((HashMap*)g->dispatch_cache, (void*)(uintptr_t)sig_hash, result);
        }
    }

    return result;
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
 * Optimization: Uses by_arity array for O(1) lookup when argc < MAX_ARITY_FAST.
 */
bool omni_check_arity(Obj* generic_obj, int argc) {
    if (!generic_obj || !IS_BOXED(generic_obj) || generic_obj->tag != TAG_GENERIC) {
        return false;
    }

    Generic* g = (Generic*)generic_obj->ptr;
    if (!g) {
        return false;
    }

    /* Fast path: O(1) lookup using by_arity array */
    if (g->by_arity && argc >= 0 && argc < g->max_arity) {
        return g->by_arity[argc] != NULL;
    }

    /* Fallback: Linear scan for large arities */
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
