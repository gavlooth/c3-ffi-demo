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
Obj* prim_pipe(Obj* value, Obj* func) {
    if (!func) return value;

    /* Check if func is a closure (lambda/function) */
    if (IS_BOXED(func) && func->tag == TAG_CLOSURE) {
        /* Apply the function to the value */
        /* For now, we use a simple approach. In a full implementation,
         * this would call the closure with the value as argument. */

        /* Extract closure environment and code */
        Closure* closure = (Closure*)func->ptr;
        if (!closure) return value;

        /* For primitive functions stored as symbols, we'd look them up
         * and apply them. This is a simplified version. */

        /* Placeholder: Just return the value for now */
        /* The compiler would handle actual function application */
        return value;
    }

    /* If func is a symbol, look up the function and apply it */
    if (IS_BOXED(func) && func->tag == TAG_SYM) {
        const char* func_name = (const char*)func->ptr;

        /* This would need to look up the function in the environment
         * and apply it to the value. For now, it's a placeholder. */

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

    /* Construct accessor function name: obj-field */
    /* For example, .x becomes point-x */
    size_t len = strlen(field) + 20;  /* Enough for "get--" + field */
    char* accessor = malloc(len);
    strcpy(accessor, "get-");
    strcat(accessor, field);

    /* This would need to look up the accessor function and call it.
     * For now, this is a placeholder that returns NULL. */

    free(accessor);

    /* In a full implementation, we'd:
     * 1. Look up the accessor function (e.g., get-x or point-x)
     * 2. Call it with obj as argument
     * 3. Return the result
     */

    return NULL;
}

/*
 * prim_dot_field_chain: Chain multiple field accesses
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
 */
Obj* prim_method_chain(Obj* obj, Obj* method_calls) {
    Obj* current = obj;
    Obj* calls = method_calls;

    while (current && calls && IS_BOXED(calls) && calls->tag == TAG_PAIR) {
        Obj* call_spec = calls->a;

        if (IS_BOXED(call_spec) && call_spec->tag == TAG_PAIR) {
            Obj* method_name = call_spec->a;
            Obj* method_args = call_spec->b;

            /* Call the method with current as first argument */
            /* (method-name current . method-args) */

            /* For now, this is a placeholder */
            /* In a full implementation, we'd look up and call the method */
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
 */
Obj* prim_apply(Obj* func, Obj* args) {
    if (!func) return NULL;

    /* This would need to call the function with the given args
     * For now, it's a placeholder */

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
