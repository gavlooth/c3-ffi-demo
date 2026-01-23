/*
 * piping.c - Pipe operator and leading dot syntax support
 *
 * Implements the pipe operator (|>) and leading dot (.field) syntax
 * for more readable and composable code.
 *
 * API:
 *   - pipe: Pipe operator for function chaining (x |> f) means f(x)
 *   - compose: Function composition (f . g) means (lambda (x) (f (g x)))
 *   - dot-field: Leading dot field access (.field obj) means (obj-field obj)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../include/omni.h"
#include "internal_types.h"

/* ============== Pipe Operator ============== */

// TESTED - tests/test_pipe_operator.lisp
/*
 * prim_pipe: Pipe operator for function chaining
 *
 * The pipe operator (|>) allows for left-to-right function composition.
 * (x |> f) is equivalent to (f x)
 *
 * Args: value, function
 * Returns: (function value)
 *
 * Example:
 *   (5 |> inc |> square) = (square (inc 5))
 *
 * Note: This is a runtime primitive. The compiler should optimize
 * pipe chains to avoid intermediate allocations.
 */
/*
 * Issue 29: Properly implemented pipe operator to call closures.
 */
Obj* prim_pipe(Obj* value, Obj* func) {
    if (!func) return value;

    /* Check if func is a closure (lambda/function) */
    if (IS_BOXED(func) && func->tag == TAG_CLOSURE) {
        Closure* c = (Closure*)func->ptr;
        if (!c || !c->fn) return value;

        /* Create single-element args list and call prim_apply */
        Obj* args = mk_pair(value, NULL);
        Obj* result = prim_apply(func, args);
        return result ? result : value;
    }

    /* If func is a symbol, we cannot look it up at runtime without
     * an environment reference. The compiler should resolve symbols
     * to closures before calling prim_pipe. */
    if (IS_BOXED(func) && func->tag == TAG_SYM) {
        /* Symbol resolution requires compiler support. Return value unchanged. */
        return value;
    }

    return value;
}

/*
 * prim_pipe_many: Chain multiple pipes
 *
 * Args: value, functions (list of functions)
 * Returns: result of applying each function in sequence
 *
 * Example:
 *   (pipe-many 5 [inc inc square]) = 49
 */
Obj* prim_pipe_many(Obj* value, Obj* functions) {
    Obj* current = value;
    Obj* funcs = functions;

    while (funcs && IS_BOXED(funcs) && funcs->tag == TAG_PAIR) {
        Obj* func = funcs->a;
        current = prim_pipe(current, func);
        funcs = funcs->b;
    }

    return current;
}

/* ============== Function Composition ============== */

/*
 * prim_compose: Function composition (right-to-left)
 *
 * (f . g) creates a new function that applies g first, then f.
 * This is the mathematical composition: (f ∘ g)(x) = f(g(x))
 *
 * Args: f, g
 * Returns: A new function representing f ∘ g
 *
 * Example:
 *   ((compose square inc) 5) = (square (inc 5)) = 36
 */
Obj* prim_compose(Obj* f, Obj* g) {
    /* Create a closure that represents f ∘ g */
    omni_ensure_global_region();

    /* For now, return a pair (f . g) as a simple representation */
    /* The compiler would need to handle this properly */
    return mk_pair(f, g);
}

/*
 * prim_compose_many: Compose multiple functions (right-to-left)
 *
 * Args: functions (list of functions)
 * Returns: A composed function
 *
 * Example:
 *   ((compose-many [inc square double]) 5) = (double (square (inc 5)))
 */
Obj* prim_compose_many(Obj* functions) {
    if (!functions) {
        /* Identity function */
        return mk_pair(NULL, NULL);
    }

    /* For multiple functions, compose them right-to-left */
    /* (f1 f2 f3) becomes (f1 . (f2 . f3)) */
    return prim_compose(functions->a, prim_compose_many(functions->b));
}

/* ============== Leading Dot Field Access ============== */

/*
 * prim_dot_field: Leading dot field access
 *
 * (.field obj) accesses the 'field' member of obj.
 * This is syntax sugar for (obj-field obj).
 *
 * Args: field_name, obj
 * Returns: The field value
 *
 * Example:
 *   (.x point) = (point-x point)
 *
 * Note: This is typically handled by the reader/parser, which
 * converts .field to (dot-field 'field obj). This primitive
 * does the actual field lookup.
 *
 * Issue 29: Properly implemented to access dict fields and pair components.
 */
Obj* prim_dot_field(Obj* field_name, Obj* obj) {
    if (!field_name || !obj) return NULL;

    /* Get field name as string */
    const char* field = NULL;
    if (IS_BOXED(field_name) && field_name->tag == TAG_SYM) {
        field = (const char*)field_name->ptr;
    } else if (IS_BOXED(field_name) && field_name->tag == TAG_STRING) {
        field = (const char*)field_name->ptr;
    }

    if (!field) return NULL;

    /* Handle dicts - look up field by name */
    if (IS_BOXED(obj) && obj->tag == TAG_DICT) {
        return dict_get_by_name(obj, field);
    }

    /* Handle pairs - support car/cdr/a/b/first/rest accessors */
    if (IS_BOXED(obj) && obj->tag == TAG_PAIR) {
        if (strcmp(field, "car") == 0 || strcmp(field, "a") == 0 ||
            strcmp(field, "first") == 0 || strcmp(field, "head") == 0) {
            return obj->a;
        }
        if (strcmp(field, "cdr") == 0 || strcmp(field, "b") == 0 ||
            strcmp(field, "rest") == 0 || strcmp(field, "tail") == 0) {
            return obj->b;
        }
    }

    /* Handle arrays - support numeric string indices and length */
    if (IS_BOXED(obj) && obj->tag == TAG_ARRAY) {
        if (strcmp(field, "length") == 0 || strcmp(field, "len") == 0) {
            return mk_int(array_length(obj));
        }
        /* Try parsing as index */
        char* endptr;
        long idx = strtol(field, &endptr, 10);
        if (*endptr == '\0') {
            return array_get(obj, (int)idx);
        }
    }

    /* Handle strings - support length accessor */
    if (IS_BOXED(obj) && obj->tag == TAG_STRING) {
        if (strcmp(field, "length") == 0 || strcmp(field, "len") == 0) {
            const char* s = (const char*)obj->ptr;
            return mk_int(s ? (int)strlen(s) : 0);
        }
    }

    return NULL;
}

/*
 * prim_dot_field_chain: Chain multiple field accesses
 * 
 * TESTED - tests/test_dot_field_chain.lisp
 *
 * Args: obj, field_names (list of symbols)
 * Returns: The nested field value
 *
 * Example:
 *   (chain-dots point [x y]) = (y (x point))
 */
Obj* prim_dot_field_chain(Obj* obj, Obj* field_names) {
    Obj* current = obj;
    Obj* fields = field_names;

    while (current && fields && IS_BOXED(fields) && fields->tag == TAG_PAIR) {
        Obj* field = fields->a;
        current = prim_dot_field(field, current);
        fields = fields->b;
    }

    return current;
}

/* ============== Method Chaining Syntax ============== */

/*
 * prim_method_chain: Chain method calls
 *
 * This is a more sophisticated version of pipe that handles
 * object-oriented method chaining syntax.
 *
 * Args: obj, method_calls (list of (method-name . args))
 * Returns: Result of chaining all method calls
 *
 * Example:
 *   (method-chain obj [(add . 5) (mul . 2)])
 *   Equivalent to: (mul (add obj 5) 2)
 *
 * Issue 29: Properly implemented method chaining.
 */
Obj* prim_method_chain(Obj* obj, Obj* method_calls) {
    Obj* current = obj;
    Obj* calls = method_calls;

    while (current && calls && IS_BOXED(calls) && calls->tag == TAG_PAIR) {
        Obj* call_spec = calls->a;

        if (IS_BOXED(call_spec) && call_spec->tag == TAG_PAIR) {
            Obj* method = call_spec->a;
            Obj* extra_args = call_spec->b;

            /* If method is a closure, call it with current as first arg */
            if (IS_BOXED(method) && method->tag == TAG_CLOSURE) {
                /* Build args list: (current . extra_args) */
                Obj* full_args = mk_pair(current, extra_args);
                Obj* result = prim_apply(method, full_args);
                if (result) {
                    current = result;
                }
            }
            /* If method is a symbol/string, try field access followed by call */
            else if (IS_BOXED(method) && (method->tag == TAG_SYM || method->tag == TAG_STRING)) {
                /* First try as field accessor (for dict method lookup) */
                Obj* field_value = prim_dot_field(method, current);
                if (field_value && IS_BOXED(field_value) && field_value->tag == TAG_CLOSURE) {
                    /* Found a closure in the field - call it with extra args */
                    Obj* result = prim_apply(field_value, extra_args);
                    if (result) {
                        current = result;
                    }
                }
            }
        }

        calls = calls->b;
    }

    return current;
}

/* ============== Flip Operator ============== */

/*
 * prim_flip: Reverse function arguments
 *
 * (flip f) creates a new function with arguments reversed.
 * (flip f) x y = f y x
 *
 * This is useful for converting functions to work with pipe operator.
 *
 * Args: func
 * Returns: A new function with reversed arguments
 *
 * Example:
 *   ((flip -) 5 10) = (- 10 5) = 5
 *   (10 |> (flip -) 5) = (- 5 10) = 5
 */
Obj* prim_flip(Obj* func) {
    /* Create a wrapper that reverses arguments */
    /* For now, just mark the function as flipped */
    /* The compiler would handle this more efficiently */

    if (!func) return NULL;

    /* Return a tagged object indicating flipped function */
    omni_ensure_global_region();
    Obj* flipped = mk_pair(func, mk_sym("flipped"));
    return flipped;
}

/* ============== Apply and Partial Application ============== */

/*
 * prim_apply: Apply function to argument list
 *
 * Args: func, args (list)
 * Returns: Result of (apply func args)
 *
 * Example:
 *   (apply + [1 2 3]) = 6
 *
 * Issue 29: Properly implemented to call closure function pointers.
 */
Obj* prim_apply(Obj* func, Obj* args) {
    if (!func) return NULL;

    /* Handle closures */
    if (IS_BOXED(func) && func->tag == TAG_CLOSURE) {
        Closure* c = (Closure*)func->ptr;
        if (!c || !c->fn) return NULL;

        /* Count arguments in the list */
        int argc = 0;
        Obj* cur = args;
        while (cur && IS_BOXED(cur) && cur->tag == TAG_PAIR) {
            argc++;
            cur = cur->b;
        }

        /* Build argument array */
        Obj** argv = NULL;
        if (argc > 0) {
            argv = malloc(argc * sizeof(Obj*));
            if (!argv) return NULL;

            cur = args;
            for (int i = 0; i < argc && cur && IS_BOXED(cur) && cur->tag == TAG_PAIR; i++) {
                argv[i] = cur->a;
                cur = cur->b;
            }
        }

        /* Arity check */
        if (c->arity >= 0 && c->arity != argc) {
            if (argv) free(argv);
            return NULL;  /* Arity mismatch */
        }

        /* Call the closure */
        Obj* result;
        if (c->region_aware && c->fn_region) {
            /* Region-aware function */
            omni_ensure_global_region();
            Region* r = omni_get_global_region();
            result = c->fn_region(r, c->captures, argv, argc);
        } else {
            /* Legacy function */
            result = c->fn(c->captures, argv, argc);
        }

        if (argv) free(argv);
        return result;
    }

    return NULL;
}

/*
 * prim_partial: Partially apply function
 *
 * (partial f x1 x2 ...) creates a new function that waits for
 * the remaining arguments.
 *
 * Args: func, fixed_args (list)
 * Returns: A new function with some arguments pre-applied
 *
 * Example:
 *   (let add5 (partial + 5))
 *   (add5 10) = 15
 */
Obj* prim_partial(Obj* func, Obj* fixed_args) {
    if (!func) return NULL;

    /* Create a closure containing the function and fixed args */
    omni_ensure_global_region();

    /* For now, return a pair (func . fixed_args) */
    return mk_pair(func, fixed_args);
}
