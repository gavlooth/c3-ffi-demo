/* OmniLisp Region-Based Reference Counting (RC-G) Runtime */
/* Primary Strategy: Adaptive Regions + Tethers */
/* Generated ANSI C99 + POSIX Code */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "memory/region_core.h"
#include "memory/region_value.h"
#include "memory/continuation.h"
#include "internal_types.h"
#include "util/hashmap.h"
#include "smr/qsbr.h"
#include "../include/omni_atomic.h"
#include "effect.h"

/* RC-G Runtime: Standard RC is now Region-RC (Coarse-grained) */
/* For compatibility with tests that check the 'mark' field, we update it. */
void inc_ref(Obj* x) { if (x && !IS_IMMEDIATE(x)) x->mark++; }
void dec_ref(Obj* x) { if (x && !IS_IMMEDIATE(x)) x->mark--; }
void free_obj(Obj* x) { (void)x; }

/* Global Region for Interpreter/Legacy support */
/* NOTE: Made non-static for use by other runtime modules (regex.c, string_utils.c) */
Region* _global_region = NULL;

/* Global Type Objects for Runtime Type Operations */
Obj* o_Int = NULL;    /* Int type object */
Obj* o_String = NULL; /* String type object */
Obj* o_Any = NULL;    /* Any type object (top type) */
Obj* o_Nothing = NULL; /* Nothing type object */

void _ensure_global_region(void) {
    if (!_global_region) {
        _global_region = region_create();
    }
}

/* Public API for global region access (for use by other modules) */
void omni_ensure_global_region(void) {
    _ensure_global_region();
}

Region* omni_get_global_region(void) {
    _ensure_global_region();
    return _global_region;
}

/* Initialize global type objects */
void omni_init_type_objects(void) {
    /* Ensure global region exists first */
    omni_ensure_global_region();

    /* Initialize type objects if not already initialized */
    if (!o_Int) {
        o_Int = prim_kind_int();
    }
    if (!o_String) {
        o_String = prim_kind_string();
    }
    if (!o_Any) {
        o_Any = prim_kind_any();
    }
    if (!o_Nothing) {
        o_Nothing = prim_kind_nothing();
    }
}

/*
 * omni_lookup_type - Lookup a type object by name
 * Used for type-based dispatch and type checking
 * Args: name - Type name (e.g., "Int", "String", "Any")
 * Returns: Type object or NULL if not found
 */
Obj* omni_lookup_type(const char* name) {
    /* Ensure type objects are initialized */
    omni_init_type_objects();

    /* Check name against known types */
    /* Type objects are Kind objects with a 'name' field */
    if (strcmp(name, "Int") == 0) {
        return o_Int;
    } else if (strcmp(name, "String") == 0) {
        return o_String;
    } else if (strcmp(name, "Any") == 0) {
        return o_Any;
    } else if (strcmp(name, "Nothing") == 0) {
        return o_Nothing;
    }

    /* Type not found */
    return NULL;
}

/* Helpers */
int is_nil(Obj* x) { return x == NULL; }
/* Note: NOT static - exported for condition.c and other modules */
Obj omni_nothing_obj = { .tag = TAG_NOTHING };
int is_nothing(Obj* x) {
    return x == &omni_nothing_obj || (x && IS_BOXED(x) && x->tag == TAG_NOTHING);
}

/* Object Constructors (Shimmed to Global Region) */
Obj* mk_int(long i) {
    if (i >= IMM_INT_MIN && i <= IMM_INT_MAX) return mk_int_unboxed(i);
    _ensure_global_region();
    return mk_int_region(_global_region, i);
}
// mk_int_unboxed is static inline in omni.h
Obj* mk_float(double f) { _ensure_global_region(); return mk_float_region(_global_region, f); }
Obj* mk_char(long c) {
    if (c >= 0 && c <= 0x10FFFF) return mk_char_unboxed(c);
    _ensure_global_region();
    return mk_char_region(_global_region, c);
}
// mk_bool is static inline in omni.h
Obj* mk_pair(Obj* a, Obj* b) { _ensure_global_region(); return mk_cell_region(_global_region, a, b); }
Obj* mk_cell(Obj* a, Obj* b) { return mk_pair(a, b); }

Obj* mk_sym(const char* s) { _ensure_global_region(); return mk_sym_region(_global_region, s); }
Obj* mk_string(const char* s) { _ensure_global_region(); return mk_string_cstr_region(_global_region, s); }
Obj* mk_nothing(void) { return &omni_nothing_obj; }
Obj* mk_box(Obj* v) { _ensure_global_region(); return mk_box_region(_global_region, v); }
Obj* mk_error(const char* msg) { _ensure_global_region(); return mk_error_region(_global_region, msg); }

Obj* mk_closure(ClosureFn fn, Obj** captures, BorrowRef** refs, int count, int arity) {
    (void)refs; // Legacy/unused in new runtime
    _ensure_global_region();
    Obj* o = alloc_obj_region(_global_region, TAG_CLOSURE);
    if (!o) return NULL;
    
    Closure* c = region_alloc(_global_region, sizeof(Closure));
    if (!c) return NULL;
    
    c->fn = fn;
    c->capture_count = count;
    c->arity = arity;
    c->name = "lambda";
    c->source_text = NULL;
    c->region_aware = 0;

    if (count > 0 && captures) {
        c->captures = region_alloc(_global_region, sizeof(Obj*) * count);
        for (int i = 0; i < count; i++) {
            c->captures[i] = captures[i];
        }
    } else {
        c->captures = NULL;
    }
    
    o->ptr = c;
    return o;
}

Obj* mk_closure_region(struct Region* r, ClosureFnRegion fn, Obj** captures, int count, int arity) {
    if (!r) {
        _ensure_global_region();
        r = _global_region;
    }

    Obj* o = alloc_obj_region(r, TAG_CLOSURE);
    if (!o) return NULL;

    Closure* c = region_alloc(r, sizeof(Closure));
    if (!c) return NULL;

    c->fn_region = fn;  /* Note: fn and fn_region are in a union - don't overwrite! */
    c->capture_count = count;
    c->arity = arity;
    c->name = "lambda";
    c->source_text = NULL;
    c->region_aware = 1;  /* This closure uses region-aware calling convention */

    if (count > 0 && captures) {
        c->captures = region_alloc(r, sizeof(Obj*) * count);
        for (int i = 0; i < count; i++) {
            c->captures[i] = captures[i];
        }
    } else {
        c->captures = NULL;
    }

    o->ptr = c;
    return o;
}

Obj* call_closure(Obj* clos, Obj** args, int argc) {
    if (!clos || !IS_BOXED(clos) || clos->tag != TAG_CLOSURE) return NULL;
    Closure* c = (Closure*)clos->ptr;
    // Arity check
    if (c->arity >= 0 && c->arity != argc) return NULL;
    /* Use region-aware fn if available, otherwise legacy fn */
    if (c->region_aware && c->fn_region) {
        _ensure_global_region();
        return c->fn_region(_global_region, c->captures, args, argc);
    }
    return c->fn(c->captures, args, argc);
}

Obj* call_closure_region(struct Region* caller_region, Obj* clos, Obj** args, int argc) {
    if (!clos || !IS_BOXED(clos) || clos->tag != TAG_CLOSURE) return NULL;
    Closure* c = (Closure*)clos->ptr;
    // Arity check
    if (c->arity >= 0 && c->arity != argc) return NULL;
    /* Use region-aware fn if available, otherwise legacy fn */
    if (c->region_aware && c->fn_region) {
        return c->fn_region(caller_region, c->captures, args, argc);
    }
    /* For legacy closures, still use the region-less call */
    return c->fn(c->captures, args, argc);
}

/* Truthiness */
int is_truthy(Obj* x) {
    /*
     * OmniLisp truthiness policy (language contract):
     *   - Only `false` and `nothing` are falsy.
     *   - Numeric zero (0, 0.0) is truthy (Lisp tradition).
     *   - `nil` / empty list is falsy (represented as NULL in the runtime).
     *
     * References:
     *   - docs/QUICK_REFERENCE.md ("0 ; integer (truthy)")
     *   - language_reference.md ("0 ; integer (truthy)")
     *
     * Implementation note:
     *   This runtime uses NULL to represent `nil` / empty list. We treat it as
     *   falsy so that common C-style sentinel returns (e.g., predicate returns
     *   NULL) compose naturally with higher-order functions like list_filter.
     */
    if (x == NULL) return 0; /* nil / empty list */

    /* Immediate booleans are the canonical `true`/`false` values. */
    if (IS_IMMEDIATE_BOOL(x)) return x == OMNI_TRUE;

    /* `nothing` is a boxed sentinel value and must be falsy. */
    if (IS_BOXED(x) && x->tag == TAG_NOTHING) return 0;

    /*
     * Everything else is truthy, including:
     *   - 0 / 0.0
     *   - empty list / empty structures
     *   - symbols (even the symbol named "false" if constructed manually)
     */
    return 1;
}

/* ========== Pattern Matching ========== */

/*
 * Helper: Compare two symbols for equality
 * Returns 1 if both are symbols with same string value
 */
#if 0
static int sym_equals(Obj* a, Obj* b) {
    if (!a || !b) return 0;
    if (!IS_BOXED(a) || !IS_BOXED(b)) return 0;
    if (a->tag != TAG_SYM || b->tag != TAG_SYM) return 0;
    char* a_str = (char*)a->ptr;
    char* b_str = (char*)b->ptr;
    if (!a_str || !b_str) return 0;
    return strcmp(a_str, b_str) == 0;
}
#endif

/*
 * Helper: Compare two strings for equality
 * Returns 1 if both are strings with same value
 */
static int string_equals(Obj* a, Obj* b) {
    if (!a || !b) return 0;
    if (!IS_BOXED(a) || !IS_BOXED(b)) return 0;
    if (a->tag != TAG_STRING || b->tag != TAG_STRING) return 0;
    char* a_str = (char*)a->ptr;
    char* b_str = (char*)b->ptr;
    if (!a_str || !b_str) return 0;
    return strcmp(a_str, b_str) == 0;
}

#if 0
/*
 * Helper: Check if symbol is a special keyword
 * Special symbols: true, false, nil, _, &, when
 */
static int is_special_sym(const char* s) {
    if (!s) return 0;
    return strcmp(s, "true") == 0 ||
           strcmp(s, "false") == 0 ||
           strcmp(s, "nil") == 0 ||
           strcmp(s, "_") == 0 ||
           strcmp(s, "&") == 0 ||
           strcmp(s, "when") == 0;
}
#endif

/*
 * Helper: Check if pattern is a wildcard (_)
 */
static int is_wildcard(Obj* pattern) {
    if (!IS_BOXED(pattern) || pattern->tag != TAG_SYM) return 0;
    char* s = (char*)pattern->ptr;
    return s && strcmp(s, "_") == 0;
}

/*
 * Helper: Get sequence length from Obj
 * Handles both ARRAY and list (PAIR) types
 */
static long get_sequence_length(Obj* seq) {
    if (!seq) return 0;
    if (IS_BOXED(seq) && seq->tag == TAG_ARRAY) {
        /* Arrays store data in Array struct via ptr */
        Array* arr = (Array*)seq->ptr;
        return arr ? (long)arr->len : 0;
    }
    /* For lists (pairs), count elements */
    long len = 0;
    while (seq && IS_BOXED(seq) && seq->tag == TAG_PAIR) {
        len++;
        seq = seq->b;
    }
    return len;
}

/*
 * Helper: Get element at index from sequence
 * Returns NULL if index out of bounds
 */
static Obj* get_sequence_element(Obj* seq, long index) {
    if (!seq || index < 0) return NULL;
    if (IS_BOXED(seq) && seq->tag == TAG_ARRAY) {
        /* Arrays store data in Array struct via ptr */
        Array* arr = (Array*)seq->ptr;
        if (!arr || index >= arr->len) return NULL;
        return arr->data[index];
    }
    /* For lists (pairs) */
    long i = 0;
    while (seq && IS_BOXED(seq) && seq->tag == TAG_PAIR) {
        if (i == index) return seq->a;
        seq = seq->b;
        i++;
    }
    return NULL;
}

/*
 * is_pattern_match - Check if a pattern matches a value
 *
 * Pattern matching rules:
 * 1. Wildcard (_) always matches
 * 2. Literals (int, float, string, char, bool) match by equality
 * 3. nil matches NULL or TAG_NOTHING
 * 4. true/false symbols match boolean values
 * 5. Arrays/lists match by recursively matching elements
 * 6. Variables (other symbols) always match (no binding in simple version)
 *
 * Note: This is a simplified version that doesn't do variable binding.
 * For full pattern matching with bindings, we'd need a context structure.
 */
int is_pattern_match(Obj* pattern, Obj* value) {
    /* NULL pattern matches NULL value */
    if (!pattern) return value == NULL;

    /* Handle immediate values first */
    if (IS_IMMEDIATE_INT(pattern)) {
        return IS_IMMEDIATE_INT(value) && INT_IMM_VALUE(pattern) == INT_IMM_VALUE(value);
    }
    if (IS_IMMEDIATE_BOOL(pattern)) {
        return IS_IMMEDIATE_BOOL(value) && pattern == value;
    }
    if (IS_IMMEDIATE_CHAR(pattern)) {
        return IS_IMMEDIATE_CHAR(value) && pattern == value;
    }

    /* Handle boxed values */
    if (!IS_BOXED(pattern)) {
        /* Pattern is not boxed but value is - no match */
        return 0;
    }

    int ptag = pattern->tag;

    /* Wildcard - always matches */
    if (is_wildcard(pattern)) {
        return 1;
    }

    /* Symbol patterns */
    if (ptag == TAG_SYM) {
        char* sym = (char*)pattern->ptr;

        /* nil pattern matches NULL or NOTHING */
        if (strcmp(sym, "nil") == 0) {
            return is_nil(value);
        }

        /* true symbol matches true boolean */
        if (strcmp(sym, "true") == 0) {
            return IS_IMMEDIATE_BOOL(value) && value == OMNI_TRUE;
        }

        /* false symbol matches false boolean */
        if (strcmp(sym, "false") == 0) {
            return IS_IMMEDIATE_BOOL(value) && value == OMNI_FALSE;
        }

        /* Variable pattern - any other symbol matches anything */
        /* In full implementation, this would bind the variable */
        return 1;
    }

    /* Integer literal pattern */
    if (ptag == TAG_INT) {
        if (IS_IMMEDIATE_INT(value)) {
            return pattern->i == INT_IMM_VALUE(value);
        }
        if (IS_BOXED(value) && value->tag == TAG_INT) {
            return pattern->i == value->i;
        }
        return 0;
    }

    /* Float literal pattern */
    if (ptag == TAG_FLOAT) {
        if (!IS_BOXED(value) || value->tag != TAG_FLOAT) return 0;
        return pattern->f == value->f;
    }

    /* String literal pattern */
    if (ptag == TAG_STRING) {
        return string_equals(pattern, value);
    }

    /* Char literal pattern */
    if (ptag == TAG_CHAR) {
        if (!IS_BOXED(value) || value->tag != TAG_CHAR) return 0;
        return pattern->i == value->i;
    }

    /* Array/List pattern - match element-wise */
    if (ptag == TAG_ARRAY || ptag == TAG_PAIR) {
        /* Get length of pattern */
        long plen = get_sequence_length(pattern);
        long vlen = get_sequence_length(value);

        /* Length must match */
        if (plen != vlen) return 0;

        /* Recursively match each element */
        for (long i = 0; i < plen; i++) {
            Obj* pelem = get_sequence_element(pattern, i);
            Obj* velem = get_sequence_element(value, i);
            if (!is_pattern_match(pelem, velem)) {
                return 0;
            }
        }
        return 1;
    }

    /* For other types, check tag equality */
    if (IS_BOXED(value) && value->tag == ptag) {
        return 1;
    }

    return 0;
}

/*
 * Pattern Bindings - CP-5.2
 *
 * Structures and functions for pattern matching with variable binding.
 * Supports:
 * - Simple variable bindings: [x y] matches [1 2] → x=1, y=2
 * - Rest patterns: [x y & rest] matches [1 2 3 4] → x=1, y=2, rest=[3 4]
 * - As patterns: [inner as name] binds both inner vars and whole match
 * - Nested patterns: [[a b] c] matches [[1 2] 3] → a=1, b=2, c=3
 */

/* Single pattern binding: variable name to matched value */
typedef struct {
    const char* name;
    Obj* value;
} PatternBinding;

/* Collection of pattern bindings (max 64) */
#define MAX_PATTERN_BINDINGS 64
typedef struct {
    PatternBinding bindings[MAX_PATTERN_BINDINGS];
    int count;
} PatternBindings;

/* Initialize pattern bindings */
static void pattern_bindings_init(PatternBindings* pb) {
    pb->count = 0;
}

/* Add a binding (returns 1 on success, 0 if full) */
static int pattern_bindings_add(PatternBindings* pb, const char* name, Obj* value) {
    if (pb->count >= MAX_PATTERN_BINDINGS) return 0;
    pb->bindings[pb->count].name = name;
    pb->bindings[pb->count].value = value;
    pb->count++;
    return 1;
}

/* Check if a symbol is a reserved pattern keyword */
static int is_pattern_keyword(const char* sym) {
    return strcmp(sym, "_") == 0 ||
           strcmp(sym, "nil") == 0 ||
           strcmp(sym, "true") == 0 ||
           strcmp(sym, "false") == 0 ||
           strcmp(sym, "as") == 0 ||
           strcmp(sym, "&") == 0;
}

/* Forward declaration */
static int is_pattern_match_with_bindings(Obj* pattern, Obj* value, PatternBindings* bindings);

/* Check if array pattern contains rest pattern (&) */
static int find_rest_position(Obj* pattern) {
    if (!IS_BOXED(pattern) || pattern->tag != TAG_ARRAY) return -1;
    Array* arr = (Array*)pattern->ptr;
    for (int i = 0; i < arr->len; i++) {
        Obj* elem = arr->data[i];
        if (IS_BOXED(elem) && elem->tag == TAG_SYM) {
            char* sym = (char*)elem->ptr;
            if (strcmp(sym, "&") == 0) return i;
        }
    }
    return -1;
}

/*
 * is_pattern_match_with_bindings - Match pattern and extract bindings
 *
 * Pattern matching rules:
 * 1. Wildcard (_) always matches, no binding
 * 2. Literals (int, float, string, char, bool) match by equality, no binding
 * 3. nil matches NULL or TAG_NOTHING
 * 4. true/false symbols match boolean values
 * 5. Variables (other symbols) match anything and bind the value
 * 6. Arrays/lists match element-wise
 * 7. Rest patterns [x y & rest] bind remaining elements
 * 8. As patterns [pattern as name] bind both inner and whole
 *
 * Returns: 1 if match succeeded, 0 otherwise
 */

/* Debug flag - set to 1 to enable debug output */
#define PATTERN_DEBUG 0

static int is_pattern_match_with_bindings(Obj* pattern, Obj* value, PatternBindings* bindings) {
#if PATTERN_DEBUG
    fprintf(stderr, "[DEBUG] is_pattern_match_with_bindings called\n");
    fprintf(stderr, "[DEBUG]   pattern=%p, value=%p\n", (void*)pattern, (void*)value);
    if (pattern) {
        fprintf(stderr, "[DEBUG]   pattern IS_IMMEDIATE_INT=%d, IS_BOXED=%d\n",
                IS_IMMEDIATE_INT(pattern), IS_BOXED(pattern));
        if (IS_BOXED(pattern)) {
            fprintf(stderr, "[DEBUG]   pattern->tag=%d\n", pattern->tag);
        }
    }
    if (value) {
        fprintf(stderr, "[DEBUG]   value IS_IMMEDIATE_INT=%d, IS_BOXED=%d\n",
                IS_IMMEDIATE_INT(value), IS_BOXED(value));
        if (IS_BOXED(value)) {
            fprintf(stderr, "[DEBUG]   value->tag=%d\n", value->tag);
        }
    }
#endif

    /* NULL pattern matches NULL value */
    if (!pattern) return value == NULL;

    /* Handle immediate values */
    if (IS_IMMEDIATE_INT(pattern)) {
        return IS_IMMEDIATE_INT(value) && INT_IMM_VALUE(pattern) == INT_IMM_VALUE(value);
    }
    if (IS_IMMEDIATE_BOOL(pattern)) {
        return IS_IMMEDIATE_BOOL(value) && pattern == value;
    }
    if (IS_IMMEDIATE_CHAR(pattern)) {
        return IS_IMMEDIATE_CHAR(value) && pattern == value;
    }

    /* Handle boxed values */
    if (!IS_BOXED(pattern)) return 0;

    int ptag = pattern->tag;

    /* Wildcard - always matches, no binding */
    if (is_wildcard(pattern)) {
        return 1;
    }

    /* Symbol patterns */
    if (ptag == TAG_SYM) {
        char* sym = (char*)pattern->ptr;
#if PATTERN_DEBUG
        fprintf(stderr, "[DEBUG] TAG_SYM: sym='%s'\n", sym);
#endif

        /* nil pattern matches NULL or NOTHING */
        if (strcmp(sym, "nil") == 0) {
            return is_nil(value);
        }

        /* true symbol matches true boolean */
        if (strcmp(sym, "true") == 0) {
            return IS_IMMEDIATE_BOOL(value) && value == OMNI_TRUE;
        }

        /* false symbol matches false boolean */
        if (strcmp(sym, "false") == 0) {
            return IS_IMMEDIATE_BOOL(value) && value == OMNI_FALSE;
        }

        /* Variable pattern - bind the value */
        if (!is_pattern_keyword(sym)) {
#if PATTERN_DEBUG
            fprintf(stderr, "[DEBUG] Binding '%s' to value %p\n", sym, (void*)value);
#endif
            pattern_bindings_add(bindings, sym, value);
        }
        return 1;
    }

    /* Integer literal pattern */
    if (ptag == TAG_INT) {
        if (IS_IMMEDIATE_INT(value)) {
            return pattern->i == INT_IMM_VALUE(value);
        }
        if (IS_BOXED(value) && value->tag == TAG_INT) {
            return pattern->i == value->i;
        }
        return 0;
    }

    /* Float literal pattern */
    if (ptag == TAG_FLOAT) {
        if (!IS_BOXED(value) || value->tag != TAG_FLOAT) return 0;
        return pattern->f == value->f;
    }

    /* String literal pattern */
    if (ptag == TAG_STRING) {
        return string_equals(pattern, value);
    }

    /* Char literal pattern */
    if (ptag == TAG_CHAR) {
        if (!IS_BOXED(value) || value->tag != TAG_CHAR) return 0;
        return pattern->i == value->i;
    }

    /* Array pattern - match element-wise with rest pattern support */
    if (ptag == TAG_ARRAY) {
#if PATTERN_DEBUG
        fprintf(stderr, "[DEBUG] Entering TAG_ARRAY branch\n");
#endif
        if (!IS_BOXED(value)) {
#if PATTERN_DEBUG
            fprintf(stderr, "[DEBUG] FAIL: value not boxed\n");
#endif
            return 0;
        }

        Array* parr = (Array*)pattern->ptr;
#if PATTERN_DEBUG
        fprintf(stderr, "[DEBUG] pattern->ptr=%p, parr=%p\n", pattern->ptr, (void*)parr);
        if (parr) {
            fprintf(stderr, "[DEBUG] parr->len=%d, parr->data=%p\n", parr->len, (void*)parr->data);
        }
#endif
        long plen = parr->len;
        long vlen = get_sequence_length(value);

        /* Check for 'as' pattern: [inner_pattern as name] */
        if (plen == 3) {
            Obj* middle = parr->data[1];
            if (IS_BOXED(middle) && middle->tag == TAG_SYM) {
                char* mid_str = (char*)middle->ptr;
                if (strcmp(mid_str, "as") == 0) {
                    Obj* inner = parr->data[0];
                    Obj* name_obj = parr->data[2];
                    /* Match inner pattern */
                    if (!is_pattern_match_with_bindings(inner, value, bindings)) {
                        return 0;
                    }
                    /* Also bind the whole value to name */
                    if (IS_BOXED(name_obj) && name_obj->tag == TAG_SYM) {
                        char* name = (char*)name_obj->ptr;
                        if (!is_pattern_keyword(name)) {
                            pattern_bindings_add(bindings, name, value);
                        }
                    }
                    return 1;
                }
            }
        }

        /* Check for rest pattern: [x y & rest] */
        int rest_pos = find_rest_position(pattern);
        if (rest_pos >= 0) {
            /* Pattern has & - rest pattern */
            int num_fixed = rest_pos;          /* Elements before & */
            int has_rest_var = (rest_pos + 1 < plen); /* Is there a var after &? */

            /* Value must have at least num_fixed elements */
            if (vlen < num_fixed) return 0;

            /* Match fixed elements before & */
            for (int i = 0; i < num_fixed; i++) {
                Obj* pelem = parr->data[i];
                Obj* velem = get_sequence_element(value, i);
                if (!is_pattern_match_with_bindings(pelem, velem, bindings)) {
                    return 0;
                }
            }

            /* Bind rest elements to rest variable */
            if (has_rest_var) {
                Obj* rest_var = parr->data[rest_pos + 1];
                if (IS_BOXED(rest_var) && rest_var->tag == TAG_SYM) {
                    char* rest_name = (char*)rest_var->ptr;
                    if (!is_pattern_keyword(rest_name)) {
                        /* Create array with remaining elements */
                        int rest_count = vlen - num_fixed;
                        Obj* rest_arr = mk_array(rest_count);
                        for (int i = 0; i < rest_count; i++) {
                            Obj* velem = get_sequence_element(value, num_fixed + i);
                            array_push(rest_arr, velem);
                        }
                        pattern_bindings_add(bindings, rest_name, rest_arr);
                    }
                }
            }
            return 1;
        }

        /* Regular array pattern - exact length match */
#if PATTERN_DEBUG
        fprintf(stderr, "[DEBUG] plen=%ld, vlen=%ld\n", plen, vlen);
#endif
        if (plen != vlen) {
#if PATTERN_DEBUG
            fprintf(stderr, "[DEBUG] FAIL: length mismatch\n");
#endif
            return 0;
        }

        /* Match each element */
        for (long i = 0; i < plen; i++) {
            Obj* pelem = parr->data[i];
            Obj* velem = get_sequence_element(value, i);
#if PATTERN_DEBUG
            fprintf(stderr, "[DEBUG] Matching element %ld: pelem=%p, velem=%p\n", i, (void*)pelem, (void*)velem);
#endif
            if (!is_pattern_match_with_bindings(pelem, velem, bindings)) {
#if PATTERN_DEBUG
                fprintf(stderr, "[DEBUG] FAIL: element %ld did not match\n", i);
#endif
                return 0;
            }
        }
        return 1;
    }

    /* List pattern - match element-wise */
    if (ptag == TAG_PAIR) {
        long plen = get_sequence_length(pattern);
        long vlen = get_sequence_length(value);

        if (plen != vlen) return 0;

        for (long i = 0; i < plen; i++) {
            Obj* pelem = get_sequence_element(pattern, i);
            Obj* velem = get_sequence_element(value, i);
            if (!is_pattern_match_with_bindings(pelem, velem, bindings)) {
                return 0;
            }
        }
        return 1;
    }

    /* For other types, check tag equality */
    if (IS_BOXED(value) && value->tag == ptag) {
        return 1;
    }

    return 0;
}

/*
 * pattern_bindings_to_dict - Convert bindings to a dict object
 * Returns: Obj* dict with name->value entries (empty dict if no bindings)
 */
static Obj* pattern_bindings_to_dict(PatternBindings* bindings) {
    Obj* dict = mk_dict();
    for (int i = 0; i < bindings->count; i++) {
        /* Use mk_sym for keys - symbols are interned so lookup by name works */
        Obj* key = mk_sym(bindings->bindings[i].name);
        dict_set(dict, key, bindings->bindings[i].value);
    }
    return dict;
}

/*
 * prim_pattern_match - Runtime pattern match with bindings
 *
 * Returns: Dict of bindings if match succeeded, NULL if no match
 * Used by codegen for match expressions with variable bindings.
 */
Obj* prim_pattern_match(Obj* pattern, Obj* value) {
    PatternBindings bindings;
    pattern_bindings_init(&bindings);

    if (is_pattern_match_with_bindings(pattern, value, &bindings)) {
        return pattern_bindings_to_dict(&bindings);
    }
    return NULL;
}

/* List Operations */
Obj* list_length(Obj* xs) {
    long len = 0;
    Obj* curr = xs;
    while (curr && IS_BOXED(curr) && curr->tag == TAG_PAIR) {
        len++;
        curr = curr->b;
    }
    return mk_int(len);
}

Obj* list_reverse(Obj* xs) {
    Obj* res = NULL;
    Obj* curr = xs;
    while (curr && IS_BOXED(curr) && curr->tag == TAG_PAIR) {
        res = mk_pair(curr->a, res);
        curr = curr->b;
    }
    return res;
}

Obj* list_append(Obj* a, Obj* b) {
    if (!a) return b;
    if (!IS_BOXED(a) || a->tag != TAG_PAIR) return b;
    return mk_pair(a->a, list_append(a->b, b));
}

Obj* list_map(Obj* fn, Obj* xs) {
    if (!fn || !xs || !IS_BOXED(xs) || xs->tag != TAG_PAIR) return NULL;
    Obj* mapped_a = call_closure(fn, &xs->a, 1);
    return mk_pair(mapped_a, list_map(fn, xs->b));
}

Obj* list_filter(Obj* fn, Obj* xs) {
    if (!xs || !IS_BOXED(xs) || xs->tag != TAG_PAIR) return NULL;
    Obj* res = call_closure(fn, &xs->a, 1);
    if (is_truthy(res)) {
        return mk_pair(xs->a, list_filter(fn, xs->b));
    } else {
        return list_filter(fn, xs->b);
    }
}

Obj* list_fold(Obj* fn, Obj* init, Obj* xs) {
    if (!xs || !IS_BOXED(xs) || xs->tag != TAG_PAIR) return init;
    Obj* args[2] = { init, xs->a };
    Obj* next = call_closure(fn, args, 2);
    return list_fold(fn, next, xs->b);
}

Obj* list_foldr(Obj* fn, Obj* init, Obj* xs) {
    if (!xs || !IS_BOXED(xs) || xs->tag != TAG_PAIR) return init;
    Obj* rest = list_foldr(fn, init, xs->b);
    Obj* args[2] = { xs->a, rest };
    return call_closure(fn, args, 2);
}

/* Channel Implementation */
/*
 * Issue 4 P4: Lock-free Channel Queue using QSBR
 *
 * Uses atomic CAS loops for lock-free send/receive operations.
 * Condition variables still used for blocking (full/empty conditions).
 *
 * References:
 * - runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md (Section 13)
 */
typedef struct Channel {
    Obj** buffer;           /* Circular buffer (unchanged) */
    int capacity;            /* Buffer size (unchanged) */
    int count;              /* Current element count (unchanged) */
    bool closed;            /* Channel closed flag (unchanged) */
    
    
    /* Condition variables for blocking (still needed) */
    pthread_cond_t send_cond;
    pthread_cond_t recv_cond;
    
    /* ADDED: Lock-free atomic indices */
    volatile int head;        /* Atomic: read position */
    volatile int tail;        /* Atomic: write position */
    
    /* ADDED: QSBR support */
    /* Note: old_buffer not needed for Channel (buffer never replaced) */
} Channel;

Obj* make_channel(int capacity) {
    _ensure_global_region();
    Obj* o = alloc_obj_region(_global_region, TAG_CHANNEL);
    if (!o) return NULL;
    
    Channel* ch = region_alloc(_global_region, sizeof(Channel));
    if (!ch) return NULL;
    
    ch->capacity = capacity > 0 ? capacity : 0;
    if (ch->capacity > 0) {
        ch->buffer = region_alloc(_global_region, sizeof(Obj*) * ch->capacity);
    } else {
        ch->buffer = NULL;
    }
    
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = false;
    pthread_cond_init(&ch->send_cond, NULL);
    pthread_cond_init(&ch->recv_cond, NULL);
    
    o->ptr = ch;
    return o;
}

/* Helper for testing: create channel in a specific region */
Obj* make_channel_region(Region* region, int capacity) {
    if (!region) return NULL;
    Obj* o = alloc_obj_region(region, TAG_CHANNEL);
    if (!o) return NULL;

    Channel* ch = region_alloc(region, sizeof(Channel));
    if (!ch) return NULL;

    ch->capacity = capacity > 0 ? capacity : 0;
    if (ch->capacity > 0) {
        ch->buffer = region_alloc(region, sizeof(Obj*) * ch->capacity);
    } else {
        ch->buffer = NULL;
    }

    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = false;
    pthread_cond_init(&ch->send_cond, NULL);
    pthread_cond_init(&ch->recv_cond, NULL);

    o->ptr = ch;
    return o;
}




/* Atom Implementation */
typedef struct Atom {
    Obj* value;
    pthread_rwlock_t lock;
} Atom;

Obj* make_atom(Obj* initial) {
    _ensure_global_region();
    Obj* o = alloc_obj_region(_global_region, TAG_ATOM);
    if (!o) return NULL;
    Atom* a = region_alloc(_global_region, sizeof(Atom));
    if (!a) return NULL;
    a->value = initial;
    pthread_rwlock_init(&a->lock, NULL);
    o->ptr = a;
    return o;
}

Obj* atom_deref(Obj* atom_obj) {
    if (!atom_obj || !IS_BOXED(atom_obj) || atom_obj->tag != TAG_ATOM) return NULL;
    Atom* a = (Atom*)atom_obj->ptr;
    pthread_rwlock_rdlock(&a->lock);
    Obj* val = a->value;
    pthread_rwlock_unlock(&a->lock);
    return val;
}

Obj* atom_reset(Obj* atom_obj, Obj* newval) {
    if (!atom_obj || !IS_BOXED(atom_obj) || atom_obj->tag != TAG_ATOM) return NULL;
    Atom* a = (Atom*)atom_obj->ptr;
    pthread_rwlock_wrlock(&a->lock);
    Obj* old = a->value;
    /* Issue 2 P4: Use store barrier to enforce Region Closure Property */
    Obj* repaired = omni_store_repair(atom_obj, &a->value, newval);
    a->value = repaired;
    pthread_rwlock_unlock(&a->lock);
    return old;
}

Obj* atom_swap(Obj* atom_obj, Obj* fn) {
    if (!atom_obj || !IS_BOXED(atom_obj) || atom_obj->tag != TAG_ATOM || !fn) return NULL;
    Atom* a = (Atom*)atom_obj->ptr;
    pthread_rwlock_wrlock(&a->lock);
    Obj* old = a->value;
    /* Compute new value by calling closure with old value */
    Obj* computed = call_closure(fn, &old, 1);
    /* Issue 2 P4: Use store barrier to enforce Region Closure Property */
    Obj* repaired = omni_store_repair(atom_obj, &a->value, computed);
    a->value = repaired;
    pthread_rwlock_unlock(&a->lock);
    return a->value;
}

Obj* atom_cas(Obj* atom_obj, Obj* expected, Obj* newval) {
    if (!atom_obj || !IS_BOXED(atom_obj) || atom_obj->tag != TAG_ATOM) return mk_bool(0);
    Atom* a = (Atom*)atom_obj->ptr;
    pthread_rwlock_wrlock(&a->lock);
    if (a->value == expected) {
        /* Issue 2 P4: Use store barrier to enforce Region Closure Property */
        Obj* repaired = omni_store_repair(atom_obj, &a->value, newval);
        a->value = repaired;
        pthread_rwlock_unlock(&a->lock);
        return mk_bool(1);
    }
    pthread_rwlock_unlock(&a->lock);
    return mk_bool(0);
}

/* Thread Implementation */
typedef struct Thread {
    pthread_t handle;
    Obj* result;
    bool finished;
} Thread;

static void* _thread_entry(void* arg) {
    Obj* closure = (Obj*)arg;
    Obj* res = call_closure(closure, NULL, 0);
    // Note: leaking Thread struct info for now or need a way to store result
    // In this simple shim we'll store it in a Thread struct passed via arg if we were more careful
    return (void*)res;
}

// Better thread shim for tests
typedef struct {
    Obj* closure;
    Obj* result;
    bool finished;
} ThreadInternal;

Obj* spawn_thread(Obj* closure) {
    if (!closure) return NULL;
    _ensure_global_region();
    Obj* o = alloc_obj_region(_global_region, TAG_THREAD);
    if (!o) return NULL;
    Thread* t = region_alloc(_global_region, sizeof(Thread));
    if (!t) return NULL;
    
    if (pthread_create(&t->handle, NULL, _thread_entry, closure) != 0) {
        return NULL;
    }
    t->finished = false;
    o->ptr = t;
    return o;
}

Obj* thread_join(Obj* thread_obj) {
    if (!thread_obj || !IS_BOXED(thread_obj) || thread_obj->tag != TAG_THREAD) return NULL;
    Thread* t = (Thread*)thread_obj->ptr;
    void* res = NULL;
    pthread_join(t->handle, &res);
    t->finished = true;
    t->result = (Obj*)res;
    return t->result;
}

/* Stack Pool */
Obj STACK_POOL[STACK_POOL_SIZE];
int STACK_PTR = 0;

/* Legacy / Other */
Obj* mk_int_stack(long i) {
    if (STACK_PTR >= STACK_POOL_SIZE) return mk_int(i);
    Obj* o = &STACK_POOL[STACK_PTR++];
    o->tag = TAG_INT;
    o->i = i;
    // Don't set mark=1, stack objects are special
    return o;
}
int is_stack_obj(Obj* x) {
    if (!x) return 0;
    uintptr_t px = (uintptr_t)x;
    uintptr_t start = (uintptr_t)STACK_POOL;
    uintptr_t end = start + (sizeof(Obj) * STACK_POOL_SIZE);
    return (px >= start && px < end);
}
void free_tree(Obj* x) { (void)x; }
void free_unique(Obj* x) { (void)x; }
void flush_freelist(void) {}

/* Region-Resident Operations */
Obj* mk_array(int capacity) { _ensure_global_region(); return mk_array_region(_global_region, capacity); }
Obj* mk_dict(void) { _ensure_global_region(); return mk_dict_region(_global_region); }
Obj* mk_set(void) { _ensure_global_region(); return mk_set_region(_global_region); }
Obj* mk_datetime(int64_t ts, int32_t tz) { _ensure_global_region(); return mk_datetime_region(_global_region, ts, tz); }
Obj* mk_keyword(const char* name) { _ensure_global_region(); return mk_keyword_region(_global_region, name); }
Obj* mk_tuple(Obj** items, int count) { _ensure_global_region(); return mk_tuple_region(_global_region, items, count); }
Obj* mk_named_tuple(Obj** keys, Obj** values, int count) { _ensure_global_region(); return mk_named_tuple_region(_global_region, keys, values, count); }
Obj* mk_generic(const char* name) { _ensure_global_region(); return mk_generic_region(_global_region, name); }
Obj* mk_kind(const char* name, Obj** params, int param_count) {
    _ensure_global_region();
    return mk_kind_region(_global_region, name, params, param_count);
}

/* Region-Resident Operations */
void box_set(Obj* b, Obj* v) {
    if (b && IS_BOXED(b) && b->tag == TAG_BOX) {
        Obj* repaired = omni_store_repair(b, &b->a, v);
        b->a = repaired;
    }
}
Obj* box_get(Obj* b) { return (b && IS_BOXED(b) && b->tag == TAG_BOX) ? b->a : NULL; }

/* Array Operations */
static void array_grow(Obj* arr) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return;
    Array* a = (Array*)arr->ptr;

    Region* r = omni_obj_region(arr);
    if (!r) {
        _ensure_global_region();
        r = _global_region;
    }

    int new_capacity = a->capacity * 2;
    if (new_capacity < 8) new_capacity = 8;

    Obj** new_data = (Obj**)region_alloc(r, new_capacity * sizeof(Obj*));
    if (!new_data) return;

    for (int i = 0; i < a->len; i++) {
        new_data[i] = a->data[i];
    }

    a->data = new_data;
    a->capacity = new_capacity;
}

void array_push(Obj* arr, Obj* val) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return;
    Array* a = (Array*)arr->ptr;

    if (a->len >= a->capacity) {
        array_grow(arr);
    }

    if (a->len < a->capacity) {
        a->data[a->len] = omni_store_repair(arr, &a->data[a->len], val);
        a->len++;
        if (val && !IS_IMMEDIATE(val)) {
            a->has_boxed_elems = true;
        }
    }
}

/* ========== Issue 2 P4: Mutation Store Barrier ========== */

/*
 * omni_store_repair - Store barrier with automatic lifetime repair
 *
 * Enforces Region Closure Property by automatically repairing
 * illegal lifetime edges at mutation time.
 */
Obj* omni_store_repair(Obj* container, Obj** slot, Obj* new_value) {
    /* Fast path: immediates/NULL need no repair */
    if (!new_value || IS_IMMEDIATE(new_value)) {
        *slot = new_value;
        return new_value;
    }

    /* Get owner regions */
    Region* src_region = omni_obj_region(new_value);
    Region* dst_region = omni_obj_region(container);

    /* Fast path: same region or NULL regions - no repair */
    if (!src_region || !dst_region || src_region == dst_region) {
        *slot = new_value;
        return new_value;
    }

    /* Issue 2 P4.3: Enforce Region Closure Property using lifetime ranks */

    /* Step 4: Cross-thread check - ranks are not comparable across threads */
    if (!pthread_equal(src_region->owner_thread, dst_region->owner_thread)) {
        /* Cross-thread store: always repair (transmigrate into dst) */
        /* Fallback: use global region if dst is unsafe */
        Region* safe_dst = (dst_region && dst_region->scope_alive) ? dst_region : _global_region;
        if (!safe_dst) {
            _ensure_global_region();
            safe_dst = _global_region;
        }

        /* Transmigrate value from src to safe_dst */
        Obj* repaired = transmigrate(new_value, src_region, safe_dst);
        if (repaired) {
            *slot = repaired;
            return repaired;
        }
        /* Fallback: store original if transmigrate fails */
        *slot = new_value;
        return new_value;
    }

    /*
     * Issue 2 P4.3b: Use ancestry predicate for lifetime ordering (replaces rank-only check)
     *
     * IMPORTANT correctness rule:
     * A rank is only a *depth* in the outlives tree. If two distinct regions
     * have the same `lifetime_rank`, they are siblings (or otherwise
     * incomparable) under rank alone. Using parent links allows us to
     * determine true ancestry.
     *
     * Rule: Repair if dst does NOT outlive src (dst is descendant or sibling).
     * Safe "no-repair" cases:
     * - src == dst
     * - dst outlives src (dst is ancestor, including same rank but not same region)
     *
     * Using omni_region_outlives() walks parent chain to establish ancestry:
     * - If dst is ancestor of src → NO REPAIR (safe)
     * - If dst is not ancestor (sibling/descendant) → REPAIR (unsafe)
     */
    if (!omni_region_outlives(dst_region, src_region)) {
        /* dst does NOT outlive src - repair needed! */

        /* Issue 2 P5: Try merge first for large regions */
        /* If src region is large (above threshold), merge is more efficient */
        /* because transmigrating a large graph is expensive */
        if (src_region->bytes_allocated_total >= get_merge_threshold()) {
            int merge_result = region_merge_safe(src_region, dst_region);
            if (merge_result == 0) {
                /* Merge successful: value now in dst_region via arena transfer */
                /* No copy needed, just update slot reference */
                dst_region->escape_repair_count++;
                *slot = new_value;
                return new_value;
            }
            /* Merge failed (-1 or -2), fallback to transmigrate below */
        }

        /* Fallback: Transmigrate value from src to dst (copy the graph) */
        /* This handles small regions (below threshold) or failed merge attempts */
        Obj* repaired = transmigrate(new_value, src_region, dst_region);
        if (repaired) {
            /* Update accounting: dst received an escaped value */
            dst_region->escape_repair_count++;
            *slot = repaired;
            return repaired;
        }
        /* Fallback: store original if transmigrate fails */
        *slot = new_value;
        return new_value;
    }

    /* No lifetime violation: dst is younger or same age as src */
    *slot = new_value;
    return new_value;
}

/* ========== Issue 2 P4: Mutation Store Barrier ========== */

/*
 * omni_store_repair - Store barrier with automatic lifetime repair
 *
 * Enforces Region Closure Property by automatically repairing
 * illegal lifetime edges at mutation time.
 *
 * Required logic:
 * - If new_value is immediate/NULL → store directly
 * - Check src_region = omni_obj_region(new_value)
 * - Check dst_region = omni_obj_region(container)
 * - If src == NULL || dst == NULL || src == dst → store directly
 * - If lifetime violation: apply repair (TODO: merge vs transmigrate)
 *
 * This is a single choke point for all mutation operations.
 */
Obj* array_get(Obj* arr, int idx) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return NULL;
    Array* a = (Array*)arr->ptr;
    if (idx >= 0 && idx < a->len) return a->data[idx];
    return NULL;
}

void array_set(Obj* arr, int idx, Obj* val) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return;
    Array* a = (Array*)arr->ptr;
    if (idx >= 0 && idx < a->len) {
        /* Issue 2 P4: Use omni_store_repair for all stores */
        a->data[idx] = omni_store_repair((Obj*)arr, &a->data[idx], val);
        /* Phase 34.2: Monotonic boxed-element flag. */
        if (val && !IS_IMMEDIATE(val)) {
            a->has_boxed_elems = true;
        }
    }
}

/* ========== Issue 2 P4: Mutation Store Barrier ========== */

int array_length(Obj* arr) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return 0;
    return ((Array*)arr->ptr)->len;
}

/* Dict Operations */
void dict_set(Obj* dict, Obj* key, Obj* val) {
    if (!dict || !IS_BOXED(dict) || dict->tag != TAG_DICT) return;
    Dict* d = (Dict*)dict->ptr;
    /*
     * Issue 2 P4.4:
     *
     * dict_set is a mutation boundary: it stores a pointer into a container.
     * It MUST run through the store barrier so we don't create pointers into
     * a younger region that can die earlier (Region Closure Property).
     *
     * Prior implementation problems (constructive criticism):
     * - It computed bucket indices with a hash unrelated to `hashmap_put_region`
     *   / `hashmap_get`, leading to corrupted maps and crashes.
     * - The insert path allocated HashEntry nodes in `_global_region` and then
     *   manually rewired bucket links, double-incrementing entry_count and
     *   violating the "dict is region-resident" design.
     *
     * Fix:
     * - Repair the value using `omni_store_repair(dict, ...)`.
     * - Insert/update using `hashmap_put_region` with the dict's owning region
     *   so all map storage stays within the dict's region.
     */
    Region* dict_region = omni_obj_region(dict);
    if (!dict_region) {
        _ensure_global_region();
        dict_region = _global_region;
    }

    Obj* repaired_slot = NULL;
    Obj* repaired_val = omni_store_repair(dict, &repaired_slot, val);

    hashmap_put_region(&d->map, key, repaired_val, dict_region);
}

Obj* dict_get(Obj* dict, Obj* key) {
    if (!dict || !IS_BOXED(dict) || dict->tag != TAG_DICT) return NULL;
    Dict* d = (Dict*)dict->ptr;
    return (Obj*)hashmap_get(&d->map, key);
}

/*
 * dict_get_by_name - Look up a value by string name (symbol keys)
 *
 * Unlike dict_get which uses pointer identity for keys, this function
 * iterates through the dict and compares symbol names by string content.
 * Used for pattern matching bindings where keys are symbols.
 */
Obj* dict_get_by_name(Obj* dict, const char* name) {
    if (!dict || !IS_BOXED(dict) || dict->tag != TAG_DICT || !name) return NULL;
    Dict* d = (Dict*)dict->ptr;

    /* Iterate through all buckets to find matching symbol key */
    for (size_t i = 0; i < d->map.bucket_count; i++) {
        HashEntry* entry = d->map.buckets[i];
        while (entry) {
            Obj* key = (Obj*)entry->key;
            if (key && IS_BOXED(key) && key->tag == TAG_SYM && key->ptr) {
                if (strcmp((const char*)key->ptr, name) == 0) {
                    return (Obj*)entry->value;
                }
            }
            entry = entry->next;
        }
    }
    return NULL;
}

/* ============== Array Collection Operations ============== */

/*
 * prim_array_sort: Sort array in-place with optional comparator
 *
 * Args:
 *   arr: Array to sort
 *   cmp: Comparator closure (fn [a b] -> int) or NULL for default
 *
 * Returns: The sorted array (same object, sorted in-place)
 *
 * Example:
 *   (array-sort [3 1 2])           ; -> [1 2 3]
 *   (array-sort [3 1 2] (fn [a b] (- b a)))  ; -> [3 2 1]
 */
static Obj* _sort_comparator = NULL;

static int _array_sort_compare(const void* a, const void* b) {
    Obj* obj_a = *(Obj**)a;
    Obj* obj_b = *(Obj**)b;

    if (_sort_comparator) {
        /* Call user comparator */
        if (IS_BOXED(_sort_comparator) && _sort_comparator->tag == TAG_CLOSURE) {
            Closure* c = (Closure*)_sort_comparator->ptr;
            if (c && c->fn) {
                Obj* args[] = { obj_a, obj_b };
                Obj* result = c->fn(c->captures, args, 2);
                return (int)obj_to_int(result);
            }
        }
    }

    /* Default: numeric comparison */
    long ia = obj_to_int(obj_a);
    long ib = obj_to_int(obj_b);
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

Obj* prim_array_sort(Obj* arr, Obj* cmp) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return arr;
    Array* a = (Array*)arr->ptr;
    if (a->len <= 1) return arr;

    _sort_comparator = cmp;
    qsort(a->data, a->len, sizeof(Obj*), _array_sort_compare);
    _sort_comparator = NULL;

    return arr;
}

/*
 * prim_array_reverse: Reverse array in-place
 *
 * Args: arr - Array to reverse
 * Returns: The reversed array (same object)
 *
 * Example:
 *   (array-reverse [1 2 3]) ; -> [3 2 1]
 */
Obj* prim_array_reverse(Obj* arr) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return arr;
    Array* a = (Array*)arr->ptr;

    int left = 0;
    int right = a->len - 1;
    while (left < right) {
        Obj* tmp = a->data[left];
        a->data[left] = a->data[right];
        a->data[right] = tmp;
        left++;
        right--;
    }

    return arr;
}

/*
 * prim_array_find: Find first element matching predicate
 *
 * Args:
 *   arr: Array to search
 *   pred: Predicate closure (fn [elem] -> bool)
 *
 * Returns: First matching element or NULL
 *
 * Example:
 *   (array-find [1 2 3 4] (fn [x] (> x 2))) ; -> 3
 */
Obj* prim_array_find(Obj* arr, Obj* pred) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return NULL;
    if (!pred || !IS_BOXED(pred) || pred->tag != TAG_CLOSURE) return NULL;

    Array* a = (Array*)arr->ptr;
    Closure* c = (Closure*)pred->ptr;
    if (!c || !c->fn) return NULL;

    for (int i = 0; i < a->len; i++) {
        Obj* args[] = { a->data[i] };
        Obj* result = c->fn(c->captures, args, 1);
        if (obj_to_int(result) != 0) {
            return a->data[i];
        }
    }

    return NULL;
}

/*
 * prim_array_find_index: Find index of first element matching predicate
 *
 * Args:
 *   arr: Array to search
 *   pred: Predicate closure (fn [elem] -> bool)
 *
 * Returns: Index of first match, or -1 if not found
 *
 * Example:
 *   (array-find-index [1 2 3 4] (fn [x] (> x 2))) ; -> 2
 */
Obj* prim_array_find_index(Obj* arr, Obj* pred) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return mk_int(-1);
    if (!pred || !IS_BOXED(pred) || pred->tag != TAG_CLOSURE) return mk_int(-1);

    Array* a = (Array*)arr->ptr;
    Closure* c = (Closure*)pred->ptr;
    if (!c || !c->fn) return mk_int(-1);

    for (int i = 0; i < a->len; i++) {
        Obj* args[] = { a->data[i] };
        Obj* result = c->fn(c->captures, args, 1);
        if (obj_to_int(result) != 0) {
            return mk_int(i);
        }
    }

    return mk_int(-1);
}

/*
 * prim_array_copy: Create a shallow copy of an array
 *
 * Args: arr - Array to copy
 * Returns: New array with same elements
 */
Obj* prim_array_copy(Obj* arr) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return NULL;
    Array* a = (Array*)arr->ptr;

    _ensure_global_region();
    Obj* result = mk_array(a->len);
    for (int i = 0; i < a->len; i++) {
        array_push(result, a->data[i]);
    }
    return result;
}

/* ============== Dict Collection Operations ============== */

/*
 * dict_keys: Get all keys from a dict as an array
 *
 * Args: dict - Dict to get keys from
 * Returns: Array of keys
 *
 * Example:
 *   (dict-keys {:a 1 :b 2}) ; -> [:a :b]
 */
Obj* dict_keys(Obj* dict) {
    if (!dict || !IS_BOXED(dict) || dict->tag != TAG_DICT) return NULL;
    Dict* d = (Dict*)dict->ptr;

    _ensure_global_region();
    Obj* result = mk_array(d->map.entry_count > 0 ? d->map.entry_count : 8);

    for (size_t i = 0; i < d->map.bucket_count; i++) {
        HashEntry* entry = d->map.buckets[i];
        while (entry) {
            array_push(result, (Obj*)entry->key);
            entry = entry->next;
        }
    }

    return result;
}

/*
 * dict_values: Get all values from a dict as an array
 *
 * Args: dict - Dict to get values from
 * Returns: Array of values
 *
 * Example:
 *   (dict-values {:a 1 :b 2}) ; -> [1 2]
 */
Obj* dict_values(Obj* dict) {
    if (!dict || !IS_BOXED(dict) || dict->tag != TAG_DICT) return NULL;
    Dict* d = (Dict*)dict->ptr;

    _ensure_global_region();
    Obj* result = mk_array(d->map.entry_count > 0 ? d->map.entry_count : 8);

    for (size_t i = 0; i < d->map.bucket_count; i++) {
        HashEntry* entry = d->map.buckets[i];
        while (entry) {
            array_push(result, (Obj*)entry->value);
            entry = entry->next;
        }
    }

    return result;
}

/*
 * dict_entries: Get all key-value pairs from a dict as an array of pairs
 *
 * Args: dict - Dict to get entries from
 * Returns: Array of [key value] pairs
 *
 * Example:
 *   (dict-entries {:a 1 :b 2}) ; -> [[:a 1] [:b 2]]
 */
Obj* dict_entries(Obj* dict) {
    if (!dict || !IS_BOXED(dict) || dict->tag != TAG_DICT) return NULL;
    Dict* d = (Dict*)dict->ptr;

    _ensure_global_region();
    Obj* result = mk_array(d->map.entry_count > 0 ? d->map.entry_count : 8);

    for (size_t i = 0; i < d->map.bucket_count; i++) {
        HashEntry* entry = d->map.buckets[i];
        while (entry) {
            /* Create a pair [key value] as a 2-element array */
            Obj* pair = mk_array(2);
            array_push(pair, (Obj*)entry->key);
            array_push(pair, (Obj*)entry->value);
            array_push(result, pair);
            entry = entry->next;
        }
    }

    return result;
}

/*
 * dict_merge: Merge multiple dicts into a new dict
 *
 * Args:
 *   base: Base dict
 *   overlay: Dict to merge on top (values override base)
 *
 * Returns: New dict with merged contents
 *
 * Example:
 *   (dict-merge {:a 1 :b 2} {:b 3 :c 4}) ; -> {:a 1 :b 3 :c 4}
 */
Obj* dict_merge(Obj* base, Obj* overlay) {
    _ensure_global_region();
    Obj* result = mk_dict();

    /* Copy base dict */
    if (base && IS_BOXED(base) && base->tag == TAG_DICT) {
        Dict* d = (Dict*)base->ptr;
        for (size_t i = 0; i < d->map.bucket_count; i++) {
            HashEntry* entry = d->map.buckets[i];
            while (entry) {
                dict_set(result, (Obj*)entry->key, (Obj*)entry->value);
                entry = entry->next;
            }
        }
    }

    /* Overlay second dict (overwriting) */
    if (overlay && IS_BOXED(overlay) && overlay->tag == TAG_DICT) {
        Dict* d = (Dict*)overlay->ptr;
        for (size_t i = 0; i < d->map.bucket_count; i++) {
            HashEntry* entry = d->map.buckets[i];
            while (entry) {
                dict_set(result, (Obj*)entry->key, (Obj*)entry->value);
                entry = entry->next;
            }
        }
    }

    return result;
}

/*
 * dict_has_key: Check if dict contains a key
 *
 * Args:
 *   dict: Dict to check
 *   key: Key to look for
 *
 * Returns: Boolean
 */
Obj* dict_has_key(Obj* dict, Obj* key) {
    if (!dict || !IS_BOXED(dict) || dict->tag != TAG_DICT) return mk_bool(0);
    return mk_bool(dict_get(dict, key) != NULL);
}

/*
 * dict_remove: Create new dict without specified key
 *
 * Args:
 *   dict: Source dict
 *   key: Key to remove
 *
 * Returns: New dict without the key
 */
Obj* dict_remove(Obj* dict, Obj* key_to_remove) {
    if (!dict || !IS_BOXED(dict) || dict->tag != TAG_DICT) return NULL;
    Dict* d = (Dict*)dict->ptr;

    _ensure_global_region();
    Obj* result = mk_dict();

    for (size_t i = 0; i < d->map.bucket_count; i++) {
        HashEntry* entry = d->map.buckets[i];
        while (entry) {
            Obj* key = (Obj*)entry->key;
            /* Skip the key to remove */
            if (key != key_to_remove) {
                dict_set(result, key, (Obj*)entry->value);
            }
            entry = entry->next;
        }
    }

    return result;
}

/* ========== Set Operations (Issue 24) ========== */

/*
 * set_add: Add element to set
 *
 * Uses the element as both key and value in the underlying hashmap.
 * Duplicate additions are no-ops (set semantics).
 */
void set_add(Obj* set, Obj* elem) {
    if (!set || !IS_BOXED(set) || set->tag != TAG_SET) return;
    if (!elem) return;

    Set* s = (Set*)set->ptr;
    Region* r = omni_obj_region(set);
    if (!r) {
        _ensure_global_region();
        r = _global_region;
    }

    /* Store barrier: ensure element outlives set's region */
    Obj* repaired_elem = omni_store_repair(set, &elem, elem);

    /* Use hashmap_put_region to add element (element is both key and value) */
    hashmap_put_region(&s->map, repaired_elem, repaired_elem, r);
}

/*
 * set_remove: Remove element from set
 *
 * Returns the removed element, or NULL if not found.
 */
Obj* set_remove(Obj* set, Obj* elem) {
    if (!set || !IS_BOXED(set) || set->tag != TAG_SET) return NULL;
    if (!elem) return NULL;

    Set* s = (Set*)set->ptr;
    return (Obj*)hashmap_remove(&s->map, elem);
}

/*
 * set_contains: Check if element is in set
 *
 * Returns mk_bool(1) if found, mk_bool(0) otherwise.
 */
Obj* set_contains(Obj* set, Obj* elem) {
    if (!set || !IS_BOXED(set) || set->tag != TAG_SET) return mk_bool(0);
    if (!elem) return mk_bool(0);

    Set* s = (Set*)set->ptr;
    return mk_bool(hashmap_contains(&s->map, elem));
}

/*
 * set_size: Return number of elements in set
 */
int set_size(Obj* set) {
    if (!set || !IS_BOXED(set) || set->tag != TAG_SET) return 0;
    Set* s = (Set*)set->ptr;
    return (int)s->map.entry_count;
}

/*
 * set_empty_p: Check if set is empty
 */
Obj* set_empty_p(Obj* set) {
    return mk_bool(set_size(set) == 0);
}

/*
 * set_union: Return A ∪ B (elements in A or B or both)
 */
Obj* set_union(Obj* a, Obj* b) {
    Obj* result = mk_set();
    if (!result) return NULL;

    /* Add all elements from A */
    if (a && IS_BOXED(a) && a->tag == TAG_SET) {
        Set* sa = (Set*)a->ptr;
        for (size_t i = 0; i < sa->map.bucket_count; i++) {
            HashEntry* entry = sa->map.buckets[i];
            while (entry) {
                set_add(result, (Obj*)entry->key);
                entry = entry->next;
            }
        }
    }

    /* Add all elements from B */
    if (b && IS_BOXED(b) && b->tag == TAG_SET) {
        Set* sb = (Set*)b->ptr;
        for (size_t i = 0; i < sb->map.bucket_count; i++) {
            HashEntry* entry = sb->map.buckets[i];
            while (entry) {
                set_add(result, (Obj*)entry->key);
                entry = entry->next;
            }
        }
    }

    return result;
}

/*
 * set_intersection: Return A ∩ B (elements in both A and B)
 */
Obj* set_intersection(Obj* a, Obj* b) {
    Obj* result = mk_set();
    if (!result) return NULL;

    if (!a || !IS_BOXED(a) || a->tag != TAG_SET) return result;
    if (!b || !IS_BOXED(b) || b->tag != TAG_SET) return result;

    Set* sa = (Set*)a->ptr;
    Set* sb = (Set*)b->ptr;

    /* For each element in A, check if it's in B */
    for (size_t i = 0; i < sa->map.bucket_count; i++) {
        HashEntry* entry = sa->map.buckets[i];
        while (entry) {
            if (hashmap_contains(&sb->map, entry->key)) {
                set_add(result, (Obj*)entry->key);
            }
            entry = entry->next;
        }
    }

    return result;
}

/*
 * set_difference: Return A \ B (elements in A but not in B)
 */
Obj* set_difference(Obj* a, Obj* b) {
    Obj* result = mk_set();
    if (!result) return NULL;

    if (!a || !IS_BOXED(a) || a->tag != TAG_SET) return result;

    Set* sa = (Set*)a->ptr;
    Set* sb = (b && IS_BOXED(b) && b->tag == TAG_SET) ? (Set*)b->ptr : NULL;

    /* For each element in A, add if not in B */
    for (size_t i = 0; i < sa->map.bucket_count; i++) {
        HashEntry* entry = sa->map.buckets[i];
        while (entry) {
            if (!sb || !hashmap_contains(&sb->map, entry->key)) {
                set_add(result, (Obj*)entry->key);
            }
            entry = entry->next;
        }
    }

    return result;
}

/*
 * set_symmetric_difference: Return A △ B (elements in A or B but not both)
 */
Obj* set_symmetric_difference(Obj* a, Obj* b) {
    Obj* result = mk_set();
    if (!result) return NULL;

    Set* sa = (a && IS_BOXED(a) && a->tag == TAG_SET) ? (Set*)a->ptr : NULL;
    Set* sb = (b && IS_BOXED(b) && b->tag == TAG_SET) ? (Set*)b->ptr : NULL;

    /* Add elements from A not in B */
    if (sa) {
        for (size_t i = 0; i < sa->map.bucket_count; i++) {
            HashEntry* entry = sa->map.buckets[i];
            while (entry) {
                if (!sb || !hashmap_contains(&sb->map, entry->key)) {
                    set_add(result, (Obj*)entry->key);
                }
                entry = entry->next;
            }
        }
    }

    /* Add elements from B not in A */
    if (sb) {
        for (size_t i = 0; i < sb->map.bucket_count; i++) {
            HashEntry* entry = sb->map.buckets[i];
            while (entry) {
                if (!sa || !hashmap_contains(&sa->map, entry->key)) {
                    set_add(result, (Obj*)entry->key);
                }
                entry = entry->next;
            }
        }
    }

    return result;
}

/*
 * set_subset_p: Check if A ⊆ B (all elements of A are in B)
 */
Obj* set_subset_p(Obj* a, Obj* b) {
    if (!a || !IS_BOXED(a) || a->tag != TAG_SET) return mk_bool(1); /* Empty set is subset of all */
    if (!b || !IS_BOXED(b) || b->tag != TAG_SET) return mk_bool(set_size(a) == 0);

    Set* sa = (Set*)a->ptr;
    Set* sb = (Set*)b->ptr;

    for (size_t i = 0; i < sa->map.bucket_count; i++) {
        HashEntry* entry = sa->map.buckets[i];
        while (entry) {
            if (!hashmap_contains(&sb->map, entry->key)) {
                return mk_bool(0);
            }
            entry = entry->next;
        }
    }

    return mk_bool(1);
}

/*
 * set_superset_p: Check if A ⊇ B (A contains all elements of B)
 */
Obj* set_superset_p(Obj* a, Obj* b) {
    return set_subset_p(b, a);
}

/*
 * set_to_list: Convert set to list
 */
Obj* set_to_list(Obj* set) {
    if (!set || !IS_BOXED(set) || set->tag != TAG_SET) return NULL;

    Set* s = (Set*)set->ptr;
    Obj* result = NULL;

    /* Build list in reverse (cons prepends) */
    for (size_t i = 0; i < s->map.bucket_count; i++) {
        HashEntry* entry = s->map.buckets[i];
        while (entry) {
            result = mk_pair((Obj*)entry->key, result);
            entry = entry->next;
        }
    }

    return result;
}

/*
 * list_to_set: Convert list to set
 */
Obj* list_to_set(Obj* list) {
    Obj* result = mk_set();
    if (!result) return NULL;

    while (list && IS_BOXED(list) && list->tag == TAG_PAIR) {
        set_add(result, list->a);
        list = list->b;
    }

    return result;
}

/*
 * set_to_array: Convert set to array
 */
Obj* set_to_array(Obj* set) {
    if (!set || !IS_BOXED(set) || set->tag != TAG_SET) return mk_array(0);

    Set* s = (Set*)set->ptr;
    int size = (int)s->map.entry_count;
    Obj* result = mk_array(size);
    if (!result) return NULL;

    int idx = 0;
    for (size_t i = 0; i < s->map.bucket_count; i++) {
        HashEntry* entry = s->map.buckets[i];
        while (entry) {
            array_push(result, (Obj*)entry->key);
            entry = entry->next;
            idx++;
        }
    }

    return result;
}

/*
 * array_to_set: Convert array to set
 */
Obj* array_to_set(Obj* arr) {
    Obj* result = mk_set();
    if (!result) return NULL;

    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return result;

    Array* a = (Array*)arr->ptr;
    for (int i = 0; i < a->len; i++) {
        set_add(result, a->data[i]);
    }

    return result;
}

/* ========== Issue 24 P3: Set Iteration & HOFs ========== */

/* Context structure for set_foreach callback */
typedef struct SetForEachCtx {
    Obj* fn;       /* Function to call for each element */
} SetForEachCtx;

/* Internal callback for set_foreach */
static void set_foreach_callback(void* key, void* value, void* ctx) {
    (void)value;  /* Set stores element as both key and value */
    SetForEachCtx* fctx = (SetForEachCtx*)ctx;
    Obj* elem = (Obj*)key;

    /* Call the function with the element */
    if (fctx->fn) {
        Obj* args = mk_cell(elem, NULL);
        prim_apply(fctx->fn, args);
    }
}

/*
 * set_foreach - Apply a function to each element of a set
 * Returns nothing (for side effects only)
 */
Obj* set_foreach(Obj* set, Obj* fn) {
    if (!set || !IS_BOXED(set) || set->tag != TAG_SET) return mk_nothing();
    if (!fn) return mk_nothing();

    Set* s = (Set*)set->ptr;
    SetForEachCtx ctx = { fn };
    hashmap_foreach(&s->map, set_foreach_callback, &ctx);

    return mk_nothing();
}

/* Context structure for set_map callback */
typedef struct SetMapCtx {
    Obj* fn;       /* Function to call for each element */
    Obj* result;   /* Result set being built */
} SetMapCtx;

/* Internal callback for set_map */
static void set_map_callback(void* key, void* value, void* ctx) {
    (void)value;
    SetMapCtx* mctx = (SetMapCtx*)ctx;
    Obj* elem = (Obj*)key;

    /* Call the function with the element */
    if (mctx->fn) {
        Obj* args = mk_cell(elem, NULL);
        Obj* mapped = prim_apply(mctx->fn, args);
        if (mapped && mctx->result) {
            set_add(mctx->result, mapped);
        }
    }
}

/*
 * set_map - Map a function over set elements, returning a new set
 * Example: (set-map inc #{1 2 3}) => #{2 3 4}
 */
Obj* set_map(Obj* set, Obj* fn) {
    if (!set || !IS_BOXED(set) || set->tag != TAG_SET) return mk_set();
    if (!fn) return mk_set();

    Obj* result = mk_set();
    if (!result) return NULL;

    Set* s = (Set*)set->ptr;
    SetMapCtx ctx = { fn, result };
    hashmap_foreach(&s->map, set_map_callback, &ctx);

    return result;
}

/* Context structure for set_filter callback */
typedef struct SetFilterCtx {
    Obj* pred;     /* Predicate function */
    Obj* result;   /* Result set being built */
} SetFilterCtx;

/* Internal callback for set_filter */
static void set_filter_callback(void* key, void* value, void* ctx) {
    (void)value;
    SetFilterCtx* fctx = (SetFilterCtx*)ctx;
    Obj* elem = (Obj*)key;

    /* Call the predicate with the element */
    if (fctx->pred) {
        Obj* args = mk_cell(elem, NULL);
        Obj* keep = prim_apply(fctx->pred, args);
        /* Keep element if predicate returns truthy */
        if (keep && keep != OMNI_FALSE && !is_nothing(keep) && keep != NULL) {
            set_add(fctx->result, elem);
        }
    }
}

/*
 * set_filter - Filter set elements by a predicate, returning a new set
 * Example: (set-filter even? #{1 2 3 4}) => #{2 4}
 */
Obj* set_filter(Obj* set, Obj* pred) {
    if (!set || !IS_BOXED(set) || set->tag != TAG_SET) return mk_set();
    if (!pred) return mk_set();

    Obj* result = mk_set();
    if (!result) return NULL;

    Set* s = (Set*)set->ptr;
    SetFilterCtx ctx = { pred, result };
    hashmap_foreach(&s->map, set_filter_callback, &ctx);

    return result;
}

/* Context structure for set_reduce callback */
typedef struct SetReduceCtx {
    Obj* fn;       /* Binary function (acc, elem) -> new_acc */
    Obj* acc;      /* Accumulator */
} SetReduceCtx;

/* Internal callback for set_reduce */
static void set_reduce_callback(void* key, void* value, void* ctx) {
    (void)value;
    SetReduceCtx* rctx = (SetReduceCtx*)ctx;
    Obj* elem = (Obj*)key;

    /* Call the function with (acc, elem) */
    if (rctx->fn) {
        Obj* args = mk_cell(rctx->acc, mk_cell(elem, NULL));
        rctx->acc = prim_apply(rctx->fn, args);
    }
}

/*
 * set_reduce - Reduce set elements to a single value
 * Example: (set-reduce + 0 #{1 2 3}) => 6
 */
Obj* set_reduce(Obj* set, Obj* fn, Obj* init) {
    if (!set || !IS_BOXED(set) || set->tag != TAG_SET) return init;
    if (!fn) return init;

    Set* s = (Set*)set->ptr;
    SetReduceCtx ctx = { fn, init };
    hashmap_foreach(&s->map, set_reduce_callback, &ctx);

    return ctx.acc;
}

/* ========== DateTime Functions (Issue 25) ========== */

/*
 * datetime_now - Create a DateTime for the current time
 * Returns: DateTime with current UTC timestamp and local timezone offset
 */
Obj* datetime_now(void) {
    time_t now = time(NULL);
    struct tm local_tm;
    localtime_r(&now, &local_tm);

    /* Calculate timezone offset from UTC.
     * POSIX extension tm_gmtoff provides offset in seconds. */
    int32_t tz_offset = (int32_t)local_tm.tm_gmtoff;

    _ensure_global_region();
    return mk_datetime_region(_global_region, (int64_t)now, tz_offset);
}

/*
 * datetime_now_utc - Create a DateTime for the current time in UTC
 * Returns: DateTime with current UTC timestamp and zero offset
 */
Obj* datetime_now_utc(void) {
    time_t now = time(NULL);
    _ensure_global_region();
    return mk_datetime_region(_global_region, (int64_t)now, 0);
}

/*
 * datetime_from_unix - Create a DateTime from Unix timestamp
 * timestamp: Seconds since 1970-01-01 00:00:00 UTC
 * Returns: DateTime with zero timezone offset (UTC)
 */
Obj* datetime_from_unix(Obj* timestamp) {
    if (!timestamp) return NULL;
    int64_t ts = obj_to_int(timestamp);
    _ensure_global_region();
    return mk_datetime_region(_global_region, ts, 0);
}

/*
 * datetime_from_unix_tz - Create a DateTime from Unix timestamp with timezone
 * timestamp: Seconds since 1970-01-01 00:00:00 UTC
 * tz_offset: Timezone offset in seconds from UTC
 * Returns: DateTime with specified timezone offset
 */
Obj* datetime_from_unix_tz(Obj* timestamp, Obj* tz_offset) {
    if (!timestamp) return NULL;
    int64_t ts = obj_to_int(timestamp);
    int32_t tz = tz_offset ? (int32_t)obj_to_int(tz_offset) : 0;
    _ensure_global_region();
    return mk_datetime_region(_global_region, ts, tz);
}

/*
 * datetime_to_unix - Extract Unix timestamp from DateTime
 * dt: DateTime object
 * Returns: Integer Unix timestamp
 */
Obj* datetime_to_unix(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_int(0);
    DateTime* d = (DateTime*)dt->ptr;
    return mk_int(d->timestamp);
}

/*
 * datetime_tz_offset - Get timezone offset from DateTime
 * dt: DateTime object
 * Returns: Integer timezone offset in seconds from UTC
 */
Obj* datetime_tz_offset(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_int(0);
    DateTime* d = (DateTime*)dt->ptr;
    return mk_int(d->tz_offset);
}

/* ========== DateTime Components (Issue 25 P1) ========== */

/*
 * Helper: Get tm struct from DateTime, applying timezone offset
 */
static struct tm datetime_to_tm(DateTime* d) {
    struct tm tm;
    /* Apply timezone offset to get local time */
    time_t local_time = (time_t)(d->timestamp + d->tz_offset);
    gmtime_r(&local_time, &tm);
    return tm;
}

/*
 * datetime_make - Create DateTime from components
 * (datetime year month day hour minute second)
 */
Obj* datetime_make(Obj* year, Obj* month, Obj* day, Obj* hour, Obj* minute, Obj* second) {
    struct tm tm = {0};
    tm.tm_year = (int)obj_to_int(year) - 1900;
    tm.tm_mon = (int)obj_to_int(month) - 1;  /* 0-based month */
    tm.tm_mday = (int)obj_to_int(day);
    tm.tm_hour = hour ? (int)obj_to_int(hour) : 0;
    tm.tm_min = minute ? (int)obj_to_int(minute) : 0;
    tm.tm_sec = second ? (int)obj_to_int(second) : 0;
    tm.tm_isdst = -1;  /* Let mktime determine DST */

    time_t ts = timegm(&tm);  /* Interpret as UTC */
    _ensure_global_region();
    return mk_datetime_region(_global_region, (int64_t)ts, 0);
}

/*
 * datetime_make_local - Create DateTime from components in local timezone
 */
Obj* datetime_make_local(Obj* year, Obj* month, Obj* day, Obj* hour, Obj* minute, Obj* second) {
    struct tm tm = {0};
    tm.tm_year = (int)obj_to_int(year) - 1900;
    tm.tm_mon = (int)obj_to_int(month) - 1;
    tm.tm_mday = (int)obj_to_int(day);
    tm.tm_hour = hour ? (int)obj_to_int(hour) : 0;
    tm.tm_min = minute ? (int)obj_to_int(minute) : 0;
    tm.tm_sec = second ? (int)obj_to_int(second) : 0;
    tm.tm_isdst = -1;

    time_t ts = mktime(&tm);  /* Interpret as local time */
    int32_t tz = (int32_t)tm.tm_gmtoff;
    _ensure_global_region();
    return mk_datetime_region(_global_region, (int64_t)ts, tz);
}

/* Component accessors */
Obj* datetime_year(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_int(0);
    struct tm tm = datetime_to_tm((DateTime*)dt->ptr);
    return mk_int(tm.tm_year + 1900);
}

Obj* datetime_month(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_int(0);
    struct tm tm = datetime_to_tm((DateTime*)dt->ptr);
    return mk_int(tm.tm_mon + 1);  /* 1-based month */
}

Obj* datetime_day(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_int(0);
    struct tm tm = datetime_to_tm((DateTime*)dt->ptr);
    return mk_int(tm.tm_mday);
}

Obj* datetime_hour(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_int(0);
    struct tm tm = datetime_to_tm((DateTime*)dt->ptr);
    return mk_int(tm.tm_hour);
}

Obj* datetime_minute(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_int(0);
    struct tm tm = datetime_to_tm((DateTime*)dt->ptr);
    return mk_int(tm.tm_min);
}

Obj* datetime_second(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_int(0);
    struct tm tm = datetime_to_tm((DateTime*)dt->ptr);
    return mk_int(tm.tm_sec);
}

Obj* datetime_weekday(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_int(0);
    struct tm tm = datetime_to_tm((DateTime*)dt->ptr);
    return mk_int(tm.tm_wday);  /* 0 = Sunday, 6 = Saturday */
}

Obj* datetime_yearday(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_int(0);
    struct tm tm = datetime_to_tm((DateTime*)dt->ptr);
    return mk_int(tm.tm_yday + 1);  /* 1-based day of year */
}

/* ========== DateTime Arithmetic (Issue 25 P2) ========== */

/*
 * datetime_add_seconds - Add seconds to a DateTime
 * Returns: New DateTime offset by the given seconds
 */
Obj* datetime_add_seconds(Obj* dt, Obj* seconds) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return dt;
    DateTime* d = (DateTime*)dt->ptr;
    int64_t delta = obj_to_int(seconds);
    _ensure_global_region();
    return mk_datetime_region(_global_region, d->timestamp + delta, d->tz_offset);
}

/*
 * datetime_add_minutes - Add minutes to a DateTime
 */
Obj* datetime_add_minutes(Obj* dt, Obj* minutes) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return dt;
    DateTime* d = (DateTime*)dt->ptr;
    int64_t delta = obj_to_int(minutes) * 60;
    _ensure_global_region();
    return mk_datetime_region(_global_region, d->timestamp + delta, d->tz_offset);
}

/*
 * datetime_add_hours - Add hours to a DateTime
 */
Obj* datetime_add_hours(Obj* dt, Obj* hours) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return dt;
    DateTime* d = (DateTime*)dt->ptr;
    int64_t delta = obj_to_int(hours) * 3600;
    _ensure_global_region();
    return mk_datetime_region(_global_region, d->timestamp + delta, d->tz_offset);
}

/*
 * datetime_add_days - Add days to a DateTime
 */
Obj* datetime_add_days(Obj* dt, Obj* days) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return dt;
    DateTime* d = (DateTime*)dt->ptr;
    int64_t delta = obj_to_int(days) * 86400;
    _ensure_global_region();
    return mk_datetime_region(_global_region, d->timestamp + delta, d->tz_offset);
}

/*
 * datetime_diff - Get difference between two DateTimes in seconds
 * Returns: (dt1 - dt2) in seconds as integer
 */
Obj* datetime_diff(Obj* dt1, Obj* dt2) {
    if (!dt1 || !IS_BOXED(dt1) || dt1->tag != TAG_DATETIME) return mk_int(0);
    if (!dt2 || !IS_BOXED(dt2) || dt2->tag != TAG_DATETIME) return mk_int(0);
    DateTime* d1 = (DateTime*)dt1->ptr;
    DateTime* d2 = (DateTime*)dt2->ptr;
    return mk_int(d1->timestamp - d2->timestamp);
}

/*
 * datetime_lt - Compare DateTimes: dt1 < dt2
 */
Obj* datetime_lt(Obj* dt1, Obj* dt2) {
    if (!dt1 || !IS_BOXED(dt1) || dt1->tag != TAG_DATETIME) return mk_bool(0);
    if (!dt2 || !IS_BOXED(dt2) || dt2->tag != TAG_DATETIME) return mk_bool(0);
    DateTime* d1 = (DateTime*)dt1->ptr;
    DateTime* d2 = (DateTime*)dt2->ptr;
    return mk_bool(d1->timestamp < d2->timestamp);
}

/*
 * datetime_gt - Compare DateTimes: dt1 > dt2
 */
Obj* datetime_gt(Obj* dt1, Obj* dt2) {
    if (!dt1 || !IS_BOXED(dt1) || dt1->tag != TAG_DATETIME) return mk_bool(0);
    if (!dt2 || !IS_BOXED(dt2) || dt2->tag != TAG_DATETIME) return mk_bool(0);
    DateTime* d1 = (DateTime*)dt1->ptr;
    DateTime* d2 = (DateTime*)dt2->ptr;
    return mk_bool(d1->timestamp > d2->timestamp);
}

/*
 * datetime_eq - Compare DateTimes: dt1 == dt2
 */
Obj* datetime_eq(Obj* dt1, Obj* dt2) {
    if (!dt1 || !IS_BOXED(dt1) || dt1->tag != TAG_DATETIME) return mk_bool(0);
    if (!dt2 || !IS_BOXED(dt2) || dt2->tag != TAG_DATETIME) return mk_bool(0);
    DateTime* d1 = (DateTime*)dt1->ptr;
    DateTime* d2 = (DateTime*)dt2->ptr;
    return mk_bool(d1->timestamp == d2->timestamp);
}

/*
 * datetime_le - Compare DateTimes: dt1 <= dt2
 */
Obj* datetime_le(Obj* dt1, Obj* dt2) {
    if (!dt1 || !IS_BOXED(dt1) || dt1->tag != TAG_DATETIME) return mk_bool(0);
    if (!dt2 || !IS_BOXED(dt2) || dt2->tag != TAG_DATETIME) return mk_bool(0);
    DateTime* d1 = (DateTime*)dt1->ptr;
    DateTime* d2 = (DateTime*)dt2->ptr;
    return mk_bool(d1->timestamp <= d2->timestamp);
}

/*
 * datetime_ge - Compare DateTimes: dt1 >= dt2
 */
Obj* datetime_ge(Obj* dt1, Obj* dt2) {
    if (!dt1 || !IS_BOXED(dt1) || dt1->tag != TAG_DATETIME) return mk_bool(0);
    if (!dt2 || !IS_BOXED(dt2) || dt2->tag != TAG_DATETIME) return mk_bool(0);
    DateTime* d1 = (DateTime*)dt1->ptr;
    DateTime* d2 = (DateTime*)dt2->ptr;
    return mk_bool(d1->timestamp >= d2->timestamp);
}

/* ========== DateTime Formatting (Issue 25 P3) ========== */

/*
 * datetime_format - Format DateTime to string using strftime-style format
 * format: String with format specifiers (e.g., "%Y-%m-%d %H:%M:%S")
 * Returns: Formatted string
 */
Obj* datetime_format(Obj* dt, Obj* format) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_string("");
    if (!format) return mk_string("");

    DateTime* d = (DateTime*)dt->ptr;
    const char* fmt = obj_to_cstring(format);

    struct tm tm = datetime_to_tm(d);
    char buf[256];
    size_t len = strftime(buf, sizeof(buf), fmt, &tm);
    if (len == 0) return mk_string("");

    return mk_string(buf);
}

/*
 * datetime_to_iso8601 - Format DateTime as ISO 8601 string
 * Returns: "2026-01-18T12:30:00Z" or "2026-01-18T12:30:00+05:30"
 */
Obj* datetime_to_iso8601(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_string("");

    DateTime* d = (DateTime*)dt->ptr;
    struct tm tm = datetime_to_tm(d);

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);

    char result[80];
    if (d->tz_offset == 0) {
        snprintf(result, sizeof(result), "%sZ", buf);
    } else {
        int32_t offset = d->tz_offset;
        char sign = offset >= 0 ? '+' : '-';
        if (offset < 0) offset = -offset;
        int hours = offset / 3600;
        int mins = (offset % 3600) / 60;
        snprintf(result, sizeof(result), "%s%c%02d:%02d", buf, sign, hours, mins);
    }

    return mk_string(result);
}

/*
 * datetime_to_rfc2822 - Format DateTime as RFC 2822 string
 * Returns: "Sun, 18 Jan 2026 12:30:00 +0000"
 */
Obj* datetime_to_rfc2822(Obj* dt) {
    if (!dt || !IS_BOXED(dt) || dt->tag != TAG_DATETIME) return mk_string("");

    DateTime* d = (DateTime*)dt->ptr;
    struct tm tm = datetime_to_tm(d);

    char buf[64];
    strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S", &tm);

    char result[80];
    int32_t offset = d->tz_offset;
    char sign = offset >= 0 ? '+' : '-';
    if (offset < 0) offset = -offset;
    int hours = offset / 3600;
    int mins = (offset % 3600) / 60;
    snprintf(result, sizeof(result), "%s %c%02d%02d", buf, sign, hours, mins);

    return mk_string(result);
}

/*
 * datetime_parse_iso8601 - Parse ISO 8601 string to DateTime
 * Input: "2026-01-18T12:30:00Z" or "2026-01-18T12:30:00+05:30"
 * Returns: DateTime or NULL on parse error
 */
Obj* datetime_parse_iso8601(Obj* str) {
    if (!str) return NULL;
    const char* s = obj_to_cstring(str);
    if (!s) return NULL;

    int year, month, day, hour = 0, minute = 0, second = 0;
    int tz_hours = 0, tz_mins = 0;
    char tz_sign = '+';

    /* Parse date part: YYYY-MM-DD */
    if (sscanf(s, "%d-%d-%d", &year, &month, &day) != 3) {
        return NULL;
    }

    /* Find 'T' separator */
    const char* t = strchr(s, 'T');
    if (t) {
        t++; /* Skip 'T' */
        /* Parse time part: HH:MM:SS */
        if (sscanf(t, "%d:%d:%d", &hour, &minute, &second) < 2) {
            return NULL;
        }

        /* Find timezone indicator */
        const char* tz = t;
        while (*tz && *tz != 'Z' && *tz != '+' && *tz != '-') tz++;

        if (*tz == 'Z') {
            tz_hours = 0;
            tz_mins = 0;
        } else if (*tz == '+' || *tz == '-') {
            tz_sign = *tz;
            if (sscanf(tz + 1, "%d:%d", &tz_hours, &tz_mins) < 1) {
                /* Try without colon: +0530 */
                if (sscanf(tz + 1, "%2d%2d", &tz_hours, &tz_mins) < 1) {
                    return NULL;
                }
            }
        }
    }

    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;

    time_t ts = timegm(&tm);
    int32_t tz_offset = (tz_hours * 3600 + tz_mins * 60);
    if (tz_sign == '-') tz_offset = -tz_offset;

    /* Adjust timestamp for timezone (convert local to UTC) */
    ts -= tz_offset;

    _ensure_global_region();
    return mk_datetime_region(_global_region, (int64_t)ts, tz_offset);
}

/* Arithmetic - MOVED to math_numerics.c to avoid duplicate symbols */
#if 0
Obj* prim_add(Obj* a, Obj* b) {
    if ((a && IS_BOXED(a) && a->tag == TAG_FLOAT) || (b && IS_BOXED(b) && b->tag == TAG_FLOAT))
        return mk_float(obj_to_float(a) + obj_to_float(b));
    return mk_int(obj_to_int(a) + obj_to_int(b));
}
Obj* prim_sub(Obj* a, Obj* b) {
    if ((a && IS_BOXED(a) && a->tag == TAG_FLOAT) || (b && IS_BOXED(b) && b->tag == TAG_FLOAT))
        return mk_float(obj_to_float(a) - obj_to_float(b));
    return mk_int(obj_to_int(a) - obj_to_int(b));
}
Obj* prim_mul(Obj* a, Obj* b) {
    if ((a && IS_BOXED(a) && a->tag == TAG_FLOAT) || (b && IS_BOXED(b) && b->tag == TAG_FLOAT))
        return mk_float(obj_to_float(a) * obj_to_float(b));
    return mk_int(obj_to_int(a) * obj_to_int(b));
}
Obj* prim_div(Obj* a, Obj* b) {
    if ((a && IS_BOXED(a) && a->tag == TAG_FLOAT) || (b && IS_BOXED(b) && b->tag == TAG_FLOAT)) {
        double bv = obj_to_float(b);
        return mk_float(bv != 0.0 ? obj_to_float(a) / bv : 0.0);
    }
    long bv = obj_to_int(b);
    return mk_int(bv ? obj_to_int(a) / bv : 0);
}
Obj* prim_mod(Obj* a, Obj* b) {
    long bv = obj_to_int(b);
    return mk_int(bv ? obj_to_int(a) % bv : 0);
}
Obj* prim_abs(Obj* a) {
    long val = obj_to_int(a);
    return mk_int(val < 0 ? -val : val);
}
#endif

/* Comparisons */

/*
 * kind_values_equal - Deep equality check for Kind objects
 *
 * Issue 18 P1: Compares Kind objects by:
 * 1. Type name equality (e.g., "Int" == "Int")
 * 2. Parameter count equality
 * 3. Recursive parameter equality
 *
 * Returns: true if kinds are structurally equal
 */
static bool kind_values_equal(Obj* a, Obj* b) {
    if (!a || !b) return a == b;
    if (!IS_BOXED(a) || !IS_BOXED(b)) return false;
    if (a->tag != TAG_KIND || b->tag != TAG_KIND) return false;

    Kind* ka = (Kind*)a->ptr;
    Kind* kb = (Kind*)b->ptr;
    if (!ka || !kb) return ka == kb;

    /* Compare type names */
    if (!ka->name || !kb->name) return ka->name == kb->name;
    if (strcmp(ka->name, kb->name) != 0) return false;

    /* Compare parameter counts */
    if (ka->param_count != kb->param_count) return false;

    /* Compare parameters recursively */
    for (int i = 0; i < ka->param_count; i++) {
        if (!kind_values_equal(ka->params[i], kb->params[i])) {
            return false;
        }
    }

    return true;
}

/*
 * prim_kind_eq - Kind equality predicate (kind=?)
 *
 * Issue 18 P1: Runtime predicate for comparing Kind objects.
 * Returns true if two kinds are structurally equal.
 *
 * Usage: (kind=? {Int} {Int}) => true
 *        (kind=? {Array Int} {Array String}) => false
 */
Obj* prim_kind_eq(Obj* a, Obj* b) {
    return mk_bool(kind_values_equal(a, b));
}

Obj* prim_eq(Obj* a, Obj* b) {
    /* Special case: String comparison by content, not pointer */
    if (a && b && IS_BOXED(a) && IS_BOXED(b)) {
        if (a->tag == TAG_STRING && b->tag == TAG_STRING) {
            /* Compare strings by content */
            const char* str_a = (const char*)a->ptr;
            const char* str_b = (const char*)b->ptr;
            return mk_bool(str_a && str_b && strcmp(str_a, str_b) == 0);
        }
        /* Special case: Symbol comparison by content */
        if (a->tag == TAG_SYM && b->tag == TAG_SYM) {
            const char* sym_a = (const char*)a->ptr;
            const char* sym_b = (const char*)b->ptr;
            return mk_bool(sym_a && sym_b && strcmp(sym_a, sym_b) == 0);
        }
        /* Issue 18 P1: Kind equality by structure */
        if (a->tag == TAG_KIND && b->tag == TAG_KIND) {
            return mk_bool(kind_values_equal(a, b));
        }
    }
    /* Default: numeric/immediate comparison */
    return mk_bool(obj_to_int(a) == obj_to_int(b));
}
Obj* prim_lt(Obj* a, Obj* b) { return mk_bool(obj_to_int(a) < obj_to_int(b)); }
Obj* prim_gt(Obj* a, Obj* b) { return mk_bool(obj_to_int(a) > obj_to_int(b)); }
Obj* prim_le(Obj* a, Obj* b) { return mk_bool(obj_to_int(a) <= obj_to_int(b)); }
Obj* prim_ge(Obj* a, Obj* b) { return mk_bool(obj_to_int(a) >= obj_to_int(b)); }
Obj* prim_not(Obj* a) { return mk_bool(!is_truthy(a)); }

/* Predicates */
Obj* prim_null(Obj* x) { return mk_bool(x == NULL); }
Obj* prim_pair(Obj* x) { return mk_bool(x && IS_BOXED(x) && x->tag == TAG_PAIR); }
Obj* prim_int(Obj* x) { return mk_bool(is_int(x)); }
Obj* prim_float(Obj* x) { return mk_bool(x && IS_BOXED(x) && x->tag == TAG_FLOAT); }
Obj* prim_char(Obj* x) { return mk_bool(is_char_val(x)); }
Obj* prim_sym(Obj* x) { return mk_bool(x && IS_BOXED(x) && x->tag == TAG_SYM); }
Obj* prim_nothing_p(Obj* x) { return mk_bool(is_nothing(x)); }

/* Conversion */
Obj* char_to_int(Obj* c) { return mk_int(obj_to_char(c)); }
Obj* int_to_char(Obj* n) { return mk_char(obj_to_int(n)); }
Obj* int_to_float(Obj* n) { return mk_float((double)obj_to_int(n)); }
Obj* float_to_int(Obj* f) { return mk_int(f ? (long)f->f : 0); }
/* prim_floor and prim_ceil moved to math_numerics.c to avoid duplicate symbols */

/* Introspection */
Obj* ctr_tag(Obj* x) { 
    if (x == NULL) return mk_sym("list");
    int tag = obj_tag(x);
    switch (tag) {
        case TAG_INT: return mk_sym("int");
        case TAG_FLOAT: return mk_sym("float");
        case TAG_CHAR: return mk_sym("char");
        case TAG_PAIR: return mk_sym("list");
        case TAG_SYM: return mk_sym("symbol");
        case TAG_STRING: return mk_sym("string");
        case TAG_KEYWORD: return mk_sym("keyword");
        case TAG_ARRAY: return mk_sym("array");
        case TAG_DICT: return mk_sym("dict");
        case TAG_SET: return mk_sym("set");
        case TAG_DATETIME: return mk_sym("datetime");
        case TAG_BOX: return mk_sym("box");
        case TAG_CLOSURE: return mk_sym("closure");
        case TAG_CHANNEL: return mk_sym("channel");
        default: return mk_sym("unknown");
    }
}
Obj* ctr_arg(Obj* x, Obj* idx) {
    int i = (int)obj_to_int(idx);
    if (!x || !IS_BOXED(x)) return NULL;
    if (x->tag == TAG_PAIR) {
        if (i == 0) return x->a;
        if (i == 1) return x->b;
    }
    return NULL;
}

/* List accessors */
Obj* obj_car(Obj* p) { return (p && IS_BOXED(p) && p->tag == TAG_PAIR) ? p->a : NULL; }
Obj* obj_cdr(Obj* p) { return (p && IS_BOXED(p) && p->tag == TAG_PAIR) ? p->b : NULL; }

/* Forward declaration for print_obj (needed by print helpers) */
void print_obj(Obj* x);

/* Print helpers for container types */
static void print_array(Array* a) {
    if (!a) {
        printf("[]");
        return;
    }
    printf("[");
    for (int i = 0; i < a->len; i++) {
        if (i > 0) printf(" ");
        print_obj(a->data[i]);
    }
    printf("]");
}

typedef struct {
    int count;
    int first;
} DictPrintCtx;

static void dict_print_callback(void* key, void* value, void* ctx) {
    DictPrintCtx* dctx = (DictPrintCtx*)ctx;
    if (!dctx->first) printf(" ");
    dctx->first = 0;
    print_obj((Obj*)key);
    printf(" ");
    print_obj((Obj*)value);
}

static void print_dict(Dict* d) {
    if (!d) {
        printf("#{}");
        return;
    }
    DictPrintCtx ctx = {0, 1};
    printf("#{");
    hashmap_foreach(&d->map, dict_print_callback, &ctx);
    printf("}");
}

/* Set print (Issue 24) */
typedef struct {
    int first;
} SetPrintCtx;

static void set_print_callback(void* key, void* value, void* ctx) {
    (void)value;  /* Set stores element as both key and value */
    SetPrintCtx* sctx = (SetPrintCtx*)ctx;
    if (!sctx->first) printf(" ");
    sctx->first = 0;
    print_obj((Obj*)key);
}

static void print_set(Set* s) {
    if (!s) {
        printf("#set{}");
        return;
    }
    SetPrintCtx ctx = {1};
    printf("#set{");
    hashmap_foreach(&s->map, set_print_callback, &ctx);
    printf("}");
}

/* DateTime print (Issue 25) */
static void print_datetime(DateTime* dt) {
    if (!dt) {
        printf("#datetime<invalid>");
        return;
    }
    time_t ts = (time_t)dt->timestamp;
    struct tm utc_tm;
    gmtime_r(&ts, &utc_tm);

    /* Print ISO 8601 format with timezone */
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &utc_tm);

    if (dt->tz_offset == 0) {
        printf("#datetime<%sZ>", buf);
    } else {
        int32_t offset = dt->tz_offset;
        char sign = offset >= 0 ? '+' : '-';
        if (offset < 0) offset = -offset;
        int hours = offset / 3600;
        int mins = (offset % 3600) / 60;
        printf("#datetime<%s%c%02d:%02d>", buf, sign, hours, mins);
    }
}

/* I/O */
void print_obj(Obj* x) {
    if (!x) { printf("()"); return; }
    if (is_nothing(x)) { printf("nothing"); return; }
    if (IS_IMMEDIATE_INT(x)) { printf("%ld", (long)INT_IMM_VALUE(x)); return; }
    if (IS_IMMEDIATE_CHAR(x)) { printf("%c", (char)CHAR_IMM_VALUE(x)); return; }
    if (IS_IMMEDIATE_BOOL(x)) { printf("%s", x == OMNI_TRUE ? "true" : "false"); return; }
    switch (x->tag) {
        case TAG_INT: printf("%ld", x->i); break;
        case TAG_FLOAT: printf("%g", x->f); break;
        case TAG_SYM: printf("%s", (char*)x->ptr); break;
        case TAG_STRING: printf("\"%s\"", (char*)x->ptr); break; /* Print strings with quotes */
        case TAG_KEYWORD: printf(":%s", (char*)x->ptr); break;
        case TAG_PAIR: printf("("); print_obj(x->a); printf(" . "); print_obj(x->b); printf(")"); break;
        case TAG_ARRAY: print_array((Array*)x->ptr); break;
        case TAG_DICT: print_dict((Dict*)x->ptr); break;
        case TAG_SET: print_set((Set*)x->ptr); break;
        case TAG_DATETIME: print_datetime((DateTime*)x->ptr); break;
        case TAG_NOTHING: printf("nothing"); break; /* Should be caught by is_nothing above */
        default: printf("#<obj:%d>", x->tag); break;
    }
}

Obj* prim_display(Obj* x) { print_obj(x); return mk_nothing(); }
Obj* prim_print(Obj* x) { print_obj(x); printf("\n"); return mk_nothing(); }
Obj* prim_newline(void) { printf("\n"); return mk_nothing(); }

/*
 * prim_println: Variadic print function
 * Prints all arguments separated by spaces, followed by newline
 * Args: List of values to print
 * Returns: NOTHING
 */
Obj* prim_println(Obj* args) {
    if (!args || is_nil(args)) {
        printf("\n");
        return mk_nothing();
    }

    /* Print each argument separated by space */
    while (!is_nil(args)) {
        Obj* current = obj_car(args);
        if (current) {
            print_obj(current);
        } else {
            printf("(null)");
        }
        args = obj_cdr(args);
        if (!is_nil(args)) {
            printf(" ");
        }
    }
    printf("\n");
    return mk_nothing();
}

/* ============== File I/O Operations ============== */

/*
 * prim_file_read: Read entire file contents as a string
 *
 * Args: path - String path to file
 * Returns: String with file contents, or NULL on error
 *
 * Example:
 *   (file-read "input.txt") ; -> "file contents..."
 */
Obj* prim_file_read(Obj* path) {
    if (!path || !IS_BOXED(path) || path->tag != TAG_STRING) {
        fprintf(stderr, "file-read: expected string path\n");
        return NULL;
    }

    const char* filepath = (const char*)path->ptr;
    FILE* f = fopen(filepath, "r");
    if (!f) {
        return NULL;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Allocate buffer and read */
    char* buffer = malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(buffer, 1, size, f);
    buffer[read_size] = '\0';
    fclose(f);

    _ensure_global_region();
    Obj* result = mk_string_cstr_region(_global_region, buffer);
    free(buffer);
    return result;
}

/*
 * prim_file_write: Write string to file (overwrite)
 *
 * Args:
 *   path - String path to file
 *   content - String content to write
 *
 * Returns: Boolean indicating success
 *
 * Example:
 *   (file-write "output.txt" "hello world")
 */
Obj* prim_file_write(Obj* path, Obj* content) {
    if (!path || !IS_BOXED(path) || path->tag != TAG_STRING) {
        fprintf(stderr, "file-write: expected string path\n");
        return mk_bool(0);
    }
    if (!content || !IS_BOXED(content) || content->tag != TAG_STRING) {
        fprintf(stderr, "file-write: expected string content\n");
        return mk_bool(0);
    }

    const char* filepath = (const char*)path->ptr;
    const char* data = (const char*)content->ptr;

    FILE* f = fopen(filepath, "w");
    if (!f) {
        return mk_bool(0);
    }

    size_t len = strlen(data);
    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    return mk_bool(written == len);
}

/*
 * prim_file_append: Append string to file
 *
 * Args:
 *   path - String path to file
 *   content - String content to append
 *
 * Returns: Boolean indicating success
 */
Obj* prim_file_append(Obj* path, Obj* content) {
    if (!path || !IS_BOXED(path) || path->tag != TAG_STRING) {
        fprintf(stderr, "file-append: expected string path\n");
        return mk_bool(0);
    }
    if (!content || !IS_BOXED(content) || content->tag != TAG_STRING) {
        fprintf(stderr, "file-append: expected string content\n");
        return mk_bool(0);
    }

    const char* filepath = (const char*)path->ptr;
    const char* data = (const char*)content->ptr;

    FILE* f = fopen(filepath, "a");
    if (!f) {
        return mk_bool(0);
    }

    size_t len = strlen(data);
    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    return mk_bool(written == len);
}

/*
 * prim_file_exists: Check if file exists
 *
 * Args: path - String path to file
 * Returns: Boolean
 *
 * Example:
 *   (file-exists? "input.txt") ; -> true or false
 */
Obj* prim_file_exists(Obj* path) {
    if (!path || !IS_BOXED(path) || path->tag != TAG_STRING) {
        return mk_bool(0);
    }

    const char* filepath = (const char*)path->ptr;
    FILE* f = fopen(filepath, "r");
    if (f) {
        fclose(f);
        return mk_bool(1);
    }
    return mk_bool(0);
}

/*
 * prim_file_delete: Delete a file
 *
 * Args: path - String path to file
 * Returns: Boolean indicating success
 */
Obj* prim_file_delete(Obj* path) {
    if (!path || !IS_BOXED(path) || path->tag != TAG_STRING) {
        fprintf(stderr, "file-delete: expected string path\n");
        return mk_bool(0);
    }

    const char* filepath = (const char*)path->ptr;
    return mk_bool(remove(filepath) == 0);
}

/*
 * prim_stdin_read_line: Read a line from stdin
 *
 * Returns: String with line (without newline), or NULL on EOF
 *
 * Example:
 *   (def input (stdin-read-line))
 */
Obj* prim_stdin_read_line(void) {
    char buffer[4096];
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return NULL;  /* EOF */
    }

    /* Remove trailing newline if present */
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }

    _ensure_global_region();
    return mk_string_cstr_region(_global_region, buffer);
}

/*
 * prim_stdout_write: Write string to stdout (no newline)
 *
 * Args: content - String to write
 * Returns: NOTHING
 */
Obj* prim_stdout_write(Obj* content) {
    if (!content || !IS_BOXED(content) || content->tag != TAG_STRING) {
        fprintf(stderr, "stdout-write: expected string\n");
        return mk_nothing();
    }

    const char* data = (const char*)content->ptr;
    fputs(data, stdout);
    fflush(stdout);
    return mk_nothing();
}

/*
 * prim_str: Convert any value to its string representation
 * Args: value - Any OmniLisp value
 * Returns: String object representing the value
 */
Obj* prim_str(Obj* value) {
    if (!value) {
        return mk_string_cstr_region(_global_region, "nil");
    }

    /* Use a static buffer for string conversion */
    static char buffer[1024];
    buffer[0] = '\0';

    if (is_int(value)) {
        snprintf(buffer, sizeof(buffer), "%ld", (long)obj_to_int(value));
        return mk_string_cstr_region(_global_region, buffer);
    }

    if (IS_BOXED(value)) {
        switch (value->tag) {
        case TAG_FLOAT:
            snprintf(buffer, sizeof(buffer), "%f", obj_to_float(value));
            return mk_string_cstr_region(_global_region, buffer);
        case TAG_STRING:
            return value;  /* Already a string */
        case TAG_SYM:
            return mk_string_cstr_region(_global_region, (const char*)value->ptr);
        case TAG_PAIR: {
            /* Represent as "(a . b)" */
            snprintf(buffer, sizeof(buffer), "(pair)");
            return mk_string_cstr_region(_global_region, buffer);
        }
        case TAG_ARRAY: {
            /* Represent as "[array]" */
            snprintf(buffer, sizeof(buffer), "[array]");
            return mk_string_cstr_region(_global_region, buffer);
        }
        case TAG_CLOSURE: {
            snprintf(buffer, sizeof(buffer), "(closure)");
            return mk_string_cstr_region(_global_region, buffer);
        }
        case TAG_NOTHING: {
            return mk_string_cstr_region(_global_region, "nothing");
        }
        default:
            snprintf(buffer, sizeof(buffer), "(tag:%d)", value->tag);
            return mk_string_cstr_region(_global_region, buffer);
        }
    }

    /* Immediate values */
    if (is_char_val(value)) {
        buffer[0] = (char)obj_to_int(value);
        buffer[1] = '\0';
        return mk_string_cstr_region(_global_region, buffer);
    }

    return mk_string_cstr_region(_global_region, "?");
}

// TESTED - tests/test_string_append.omni
/*
 * prim_strcat: Concatenate two strings
 * Args: str1, str2 - String objects to concatenate
 * Returns: New string object containing str1 + str2
 */
Obj* prim_strcat(Obj* str1, Obj* str2) {
    if (!str1 || !str2) {
        return str1 ? str1 : str2;
    }

    /* Convert non-string arguments to strings */
    if (!IS_BOXED(str1) || str1->tag != TAG_STRING) {
        str1 = prim_str(str1);
    }
    if (!IS_BOXED(str2) || str2->tag != TAG_STRING) {
        str2 = prim_str(str2);
    }

    if (!IS_BOXED(str1) || !IS_BOXED(str2)) {
        return NULL;
    }

    const char* s1 = (const char*)str1->ptr;
    const char* s2 = (const char*)str2->ptr;

    if (!s1) s1 = "";
    if (!s2) s2 = "";

    size_t len1 = strlen(s1);
    size_t len2 = strlen(s2);

    /* Allocate buffer for concatenated string */
    char* result = (char*)region_alloc(_global_region, len1 + len2 + 1);
    if (!result) {
        return NULL;
    }

    memcpy(result, s1, len1);
    memcpy(result + len1, s2, len2);
    result[len1 + len2] = '\0';

    return mk_string_region(_global_region, result, len1 + len2);
}

/* Stubs/Shims for legacy GC logic */
static Obj* _deferred_list[4096];
static int _deferred_count = 0;

void safe_point(void) {
    if (_deferred_count > 100) flush_deferred();
}
void flush_deferred(void) {
    for (int i = 0; i < _deferred_count; i++) {
        dec_ref(_deferred_list[i]);
    }
    _deferred_count = 0;
}
void defer_decrement(Obj* obj) { 
    if (obj && !IS_IMMEDIATE(obj) && _deferred_count < 4096) {
        _deferred_list[_deferred_count++] = obj;
    }
}
void process_deferred(void) { flush_deferred(); }
void sym_init(void) {}
void sym_cleanup(void) {}
void region_init(void) {}
void invalidate_weak_refs_for(void* t) { (void)t; }
Obj* mk_nil(void) { return NULL; }

/* Arena Allocator (Internal TSoding Arena) */
Arena* arena_create(size_t block_size) {
    (void)block_size;
    Arena* a = malloc(sizeof(Arena));
    a->begin = NULL;
    a->end = NULL;
    return a;
}
void arena_destroy(Arena* a) {
    if (a) {
        arena_free(a);
        free(a);
    }
}
Obj* arena_mk_int(Arena* a, long i) {
    Obj* o = arena_alloc(a, sizeof(Obj));
    o->tag = TAG_INT;
    o->i = i;
    o->mark = 0;
    return o;
}
Obj* arena_mk_pair(Arena* a, Obj* car, Obj* cdr) {
    Obj* o = arena_alloc(a, sizeof(Obj));
    o->tag = TAG_PAIR;
    o->a = car;
    o->b = cdr;
    o->is_pair = 1;
    o->mark = 0;
    return o;
}

/* ============== Phase 19: Flow Constructors (Type Algebra) ============== */

/*
 * Helper: Create string symbol from C string
 */
static Obj* str_to_sym(const char* s) {
    if (!s) return NULL;
    omni_ensure_global_region();
    return mk_sym_region(omni_get_global_region(), s);
}

/*
 * prim_union: Create a union type from a list of types
 * Args: types_list - List of type objects (Kind objects)
 * Returns: A union Kind object representing the union of all input types
 *
 * Example: (union [{Int32} {String}]) -> {Union Int32 String}
 */
Obj* prim_union(Obj* types_list) {
    if (!types_list) {
        /* Empty union is the bottom type */
        return str_to_sym("Union");
    }

    /* Build union type name by collecting all type names */
    size_t count = 0;
    Obj* p = types_list;
    while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
        count++;
        p = p->b;
    }

    if (count == 0) {
        return str_to_sym("Union");
    }

    if (count == 1) {
        /* Union of one type is just that type */
        Obj* type_obj = types_list->a;
        if (IS_BOXED(type_obj) && type_obj->tag == TAG_KIND) {
            return type_obj;
        }
        return types_list->a;
    }

    /* Create union name: "Union Type1 Type2 Type3" */
    size_t name_len = 32;  /* "Union" prefix */
    p = types_list;
    while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
        const char* type_name = NULL;
        Obj* type_obj = p->a;
        if (IS_BOXED(type_obj) && type_obj->tag == TAG_KIND) {
            Kind* kind = (Kind*)type_obj->ptr;
            if (kind) type_name = kind->name;
        } else if (IS_BOXED(type_obj) && type_obj->tag == TAG_SYM) {
            type_name = (const char*)type_obj->ptr;
        }
        if (type_name) name_len += strlen(type_name) + 1;
        p = p->b;
    }

    char* union_name = malloc(name_len + 1);
    strcpy(union_name, "Union");

    p = types_list;
    // REVIEWED:NAIVE
    while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
        const char* type_name = NULL;
        Obj* type_obj = p->a;
        if (IS_BOXED(type_obj) && type_obj->tag == TAG_KIND) {
            Kind* kind = (Kind*)type_obj->ptr;
            if (kind) type_name = kind->name;
        } else if (IS_BOXED(type_obj) && type_obj->tag == TAG_SYM) {
            type_name = (const char*)type_obj->ptr;
        }
        if (type_name) {
            strcat(union_name, " ");
            strcat(union_name, type_name);
        }
        p = p->b;
    }

    /* Create union Kind object */
    omni_ensure_global_region();
    Obj* result = mk_kind_region(omni_get_global_region(), union_name, NULL, 0);
    free(union_name);

    return result;
}

/*
 * prim_fn: Create a function type (Kind) from parameter types and return type
 * Args: params_and_ret - Slot containing [param_types... return_type]
 * Returns: A function Kind object
 *
 * Example: (fn [[{Int32} {Int32}] {Int32}]) -> {Int32 Int32 -> Int32}
 */
Obj* prim_fn(Obj* params_and_ret) {
    if (!params_and_ret) return NULL;

    /* Extract parameters and return type from Slot */
    /* Expected format: ((param1 param2 ...) return_type) */
    if (!IS_BOXED(params_and_ret) || params_and_ret->tag != TAG_PAIR) {
        return NULL;
    }

    Obj* params_obj = params_and_ret->a;
    Obj* ret_type_obj = params_and_ret->b;

    Obj* p = NULL;

    /* Build function type name */
    size_t name_len = 64;  /* Base size for "Fn ->" */
    p = params_obj;
    while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
        const char* param_name = NULL;
        Obj* param_obj = p->a;
        if (IS_BOXED(param_obj) && param_obj->tag == TAG_KIND) {
            Kind* kind = (Kind*)param_obj->ptr;
            if (kind) param_name = kind->name;
        } else if (IS_BOXED(param_obj) && param_obj->tag == TAG_SYM) {
            param_name = (const char*)param_obj->ptr;
        }
        if (param_name) name_len += strlen(param_name) + 2;
        p = p->b;
    }

    /* Add return type */
    const char* ret_name = NULL;
    if (IS_BOXED(ret_type_obj) && ret_type_obj->tag == TAG_KIND) {
        Kind* kind = (Kind*)ret_type_obj->ptr;
        if (kind) ret_name = kind->name;
    } else if (IS_BOXED(ret_type_obj) && ret_type_obj->tag == TAG_SYM) {
        ret_name = (const char*)ret_type_obj->ptr;
    }
    if (ret_name) name_len += strlen(ret_name) + 4;

// REVIEWED:NAIVE
    char* fn_name = malloc(name_len + 1);
    strcpy(fn_name, "Fn");

    /* Add parameter types */
    p = params_obj;
    while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
        const char* param_name = NULL;
        Obj* param_obj = p->a;
        if (IS_BOXED(param_obj) && param_obj->tag == TAG_KIND) {
            Kind* kind = (Kind*)param_obj->ptr;
            if (kind) param_name = kind->name;
        } else if (IS_BOXED(param_obj) && param_obj->tag == TAG_SYM) {
            param_name = (const char*)param_obj->ptr;
        }
        if (param_name) {
            strcat(fn_name, " ");
            strcat(fn_name, param_name);
        }
        p = p->b;
    }

    /* Add return type */
    strcat(fn_name, " -> ");
    if (ret_name) {
        strcat(fn_name, ret_name);
    } else {
        strcat(fn_name, "Any");
    }

    /* Create function Kind object */
    omni_ensure_global_region();
    Obj* result = mk_kind_region(omni_get_global_region(), fn_name, NULL, 0);
    free(fn_name);

    return result;
}

/* ========== Deep Path Mutation ========== */

/*
 * Helper: Split a path string into components
 * Returns: NULL-terminated array of strings (caller must free)
 */
static char** split_path(const char* path_str, int* out_count) {
    if (!path_str || !out_count) return NULL;

    /* Count the number of components */
    int count = 1;
    for (const char* p = path_str; *p; p++) {
        if (*p == '.') count++;
    }

    /* Allocate array */
    char** components = malloc(sizeof(char*) * (count + 1));
    if (!components) return NULL;

    /* Split the path */
    int i = 0;
    char* path_copy = strdup(path_str);
    char* saveptr = NULL;
    char* token = strtok_r(path_copy, ".", &saveptr);

    while (token && i < count) {
        components[i++] = strdup(token);
        token = strtok_r(NULL, ".", &saveptr);
    }
    components[i] = NULL;

    free(path_copy);
    *out_count = count;
    return components;
}

/*
 * Helper: Free path components
 */
static void free_path_components(char** components) {
    if (!components) return;
    for (int i = 0; components[i]; i++) {
        free(components[i]);
    }
    free(components);
}

#if 0
/*
 * Helper: Look up a nested field in a structure
 * For now, this only works with simple dict-like structures
 * represented as nested pairs (key . value)
 */
static Obj* deep_get(Obj* root, char** components, int component_count) {
    if (!root || !components || component_count == 0) return NULL;

    Obj* current = root;

    for (int i = 0; i < component_count && current; i++) {
        const char* key = components[i];

        /* Search for key in current structure */
        Obj* found = NULL;
        Obj* p = current;

        while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
            Obj* entry = p->a;
            if (IS_BOXED(entry) && entry->tag == TAG_PAIR) {
                Obj* entry_key = entry->a;
                if (IS_BOXED(entry_key) && entry_key->tag == TAG_SYM) {
                    const char* entry_key_str = (const char*)entry_key->ptr;
                    if (strcmp(entry_key_str, key) == 0) {
                        found = entry->b;
                        break;
                    }
                }
            }
            p = p->b;
        }

        current = found;
    }

    return current;
}
#endif

/*
 * Helper: Set a nested field in a structure
 *
 * For dicts: Mutates in place by traversing to the leaf and setting value
 * For other types: Returns error (would need immutable update with structural sharing)
 *
 * Issue 29 P2: Fully implemented for dict types (2026-01-21)
 */
static Obj* deep_set(Obj* root, char** components, int component_count, Obj* new_value) {
    if (!root || component_count == 0) return new_value;

    omni_ensure_global_region();
    Region* r = omni_get_global_region();

    /* Navigate to the parent of the target field */
    Obj* current = root;
    for (int i = 0; i < component_count - 1; i++) {
        if (!current || !IS_BOXED(current)) {
            return mk_error_region(r, "deep-put: Cannot traverse non-dict value");
        }

        if (current->tag == TAG_DICT) {
            /* Dict lookup by string key */
            Obj* key = mk_string(components[i]);
            Obj* next = dict_get(current, key);
            if (!next) {
                /* Auto-create intermediate dicts if needed */
                next = mk_dict();
                dict_set(current, key, next);
            }
            current = next;
        } else if (current->tag == TAG_PAIR) {
            /* For pairs, use 'car' or 'cdr' as component names */
            if (strcmp(components[i], "car") == 0 || strcmp(components[i], "a") == 0) {
                current = current->a;
            } else if (strcmp(components[i], "cdr") == 0 || strcmp(components[i], "b") == 0) {
                current = current->b;
            } else {
                return mk_error_region(r, "deep-put: Pair only supports 'car'/'cdr' paths");
            }
        } else {
            return mk_error_region(r, "deep-put: Cannot traverse non-dict/pair value");
        }
    }

    /* Set the final field */
    const char* final_key = components[component_count - 1];

    if (!current || !IS_BOXED(current)) {
        return mk_error_region(r, "deep-put: Target is not a mutable structure");
    }

    if (current->tag == TAG_DICT) {
        dict_set(current, mk_string(final_key), new_value);
        return root;
    } else if (current->tag == TAG_PAIR) {
        if (strcmp(final_key, "car") == 0 || strcmp(final_key, "a") == 0) {
            current->a = new_value;
            return root;
        } else if (strcmp(final_key, "cdr") == 0 || strcmp(final_key, "b") == 0) {
            current->b = new_value;
            return root;
        } else {
            return mk_error_region(r, "deep-put: Pair only supports 'car'/'cdr' paths");
        }
    }

    return mk_error_region(r, "deep-put: Target is not a mutable structure");
}

/*
 * prim_deep_put: Mutate a nested field path in a data structure
 *
 * Args:
 *   - root: The root object (dict/record)
 *   - path_str: Dotted path string (e.g., "user.address.city")
 *   - new_value: The value to set at the leaf
 *
 * Returns: The modified root object
 *
 * Note: This is a simplified implementation; full support requires
 * proper dict/record representation and get/set operations.
 */
Obj* prim_deep_put(Obj* root, const char* path_str, Obj* new_value) {
    if (!path_str) {
        omni_ensure_global_region();
        return mk_error_region(omni_get_global_region(), "deep-put: Invalid path");
    }

    /* Split the path into components */
    int component_count = 0;
    char** components = split_path(path_str, &component_count);
    if (!components) {
        omni_ensure_global_region();
        return mk_error_region(omni_get_global_region(), "deep-put: Memory allocation failed");
    }

    /* Perform the deep set */
    Obj* result = deep_set(root, components, component_count, new_value);

    /* Clean up */
    free_path_components(components);

    return result;
}

/* ========== Type System Primitives ========== */

/*
 * prim_value_to_type: Return the type of a value at runtime
 *
 * Args: value - Any OmniLisp value
 * Returns: A Kind object representing the value's type
 *
 * Example:
 *   (value->type 42) => {Int}
 *   (value->type "hello") => {String}
 *   (value->type '(1 2)) => {Pair}
 */
Obj* prim_value_to_type(Obj* value) {
    if (!value) {
        omni_ensure_global_region();
        return mk_kind_region(omni_get_global_region(), "Nil", NULL, 0);
    }

    omni_ensure_global_region();
    Region* r = omni_get_global_region();
    const char* type_name = "Unknown";

    /* Check immediate types */
    if (IS_IMMEDIATE(value)) {
        if (IS_IMMEDIATE_INT(value)) {
            type_name = "Int";
        } else if (IS_IMMEDIATE_CHAR(value)) {
            type_name = "Char";
        } else if (IS_IMMEDIATE_BOOL(value)) {
            type_name = "Bool";
        }
    }
    /* Check boxed types */
    else if (IS_BOXED(value)) {
        switch (value->tag) {
            case TAG_PAIR:
                type_name = "Pair";
                break;
            case TAG_SYM:
                type_name = "Symbol";
                break;
            case TAG_STRING:
                type_name = "String";
                break;
            case TAG_ARRAY:
                type_name = "Array";
                break;
            case TAG_CLOSURE:
                type_name = "Function";
                break;
            case TAG_GENERIC:
                type_name = "Generic";
                break;
            case TAG_KIND:
                type_name = "Kind";
                break;
            case TAG_KEYWORD:
                type_name = "Keyword";
                break;
            case TAG_BOX:
                type_name = "Box";
                break;
            case TAG_CHANNEL:
                type_name = "Channel";
                break;
            case TAG_ERROR:
                type_name = "Error";
                break;
            case TAG_ATOM:
                type_name = "Atom";
                break;
            case TAG_THREAD:
                type_name = "Thread";
                break;
            case TAG_DICT:
                type_name = "Dict";
                break;
            case TAG_SET:
                type_name = "Set";
                break;
            case TAG_DATETIME:
                type_name = "DateTime";
                break;
            case TAG_NOTHING:
                type_name = "Nothing";
                break;
            default:
                type_name = "Unknown";
                break;
        }
    }

    return mk_kind_region(r, type_name, NULL, 0);
}

/* ========== Type Bootstrap Primitives ========== */

/*
 * prim_kind_int: Get the Int Kind object
 */
Obj* prim_kind_int(void) {
    omni_ensure_global_region();
    return mk_kind_region(omni_get_global_region(), "Int", NULL, 0);
}

/*
 * prim_kind_string: Get the String Kind object
 */
Obj* prim_kind_string(void) {
    omni_ensure_global_region();
    return mk_kind_region(omni_get_global_region(), "String", NULL, 0);
}

/*
 * prim_kind_array: Get the Array Kind object
 */
Obj* prim_kind_array(void) {
    omni_ensure_global_region();
    return mk_kind_region(omni_get_global_region(), "Array", NULL, 0);
}

/*
 * prim_kind_list: Get the List Kind object
 */
Obj* prim_kind_list(void) {
    omni_ensure_global_region();
    return mk_kind_region(omni_get_global_region(), "List", NULL, 0);
}

/*
 * prim_kind_pair: Get the Pair Kind object
 */
Obj* prim_kind_pair(void) {
    omni_ensure_global_region();
    return mk_kind_region(omni_get_global_region(), "Pair", NULL, 0);
}

/*
 * prim_kind_bool: Get the Bool Kind object
 */
Obj* prim_kind_bool(void) {
    omni_ensure_global_region();
    return mk_kind_region(omni_get_global_region(), "Bool", NULL, 0);
}

/*
 * prim_kind_char: Get the Char Kind object
 */
Obj* prim_kind_char(void) {
    omni_ensure_global_region();
    return mk_kind_region(omni_get_global_region(), "Char", NULL, 0);
}

/*
 * prim_kind_float: Get the Float Kind object
 */
Obj* prim_kind_float(void) {
    omni_ensure_global_region();
    return mk_kind_region(omni_get_global_region(), "Float", NULL, 0);
}

/*
 * prim_kind_function: Get the Function Kind object
 */
Obj* prim_kind_function(void) {
    omni_ensure_global_region();
    return mk_kind_region(omni_get_global_region(), "Function", NULL, 0);
}

/*
 * prim_kind_any: Get the Any Kind object (top type)
 */
Obj* prim_kind_any(void) {
    omni_ensure_global_region();
    return mk_kind_region(omni_get_global_region(), "Any", NULL, 0);
}

/*
 * prim_kind_nothing: Get the Nothing Kind object (singleton type with value 'nothing')
 */
Obj* prim_kind_nothing(void) {
    omni_ensure_global_region();
    return mk_kind_region(omni_get_global_region(), "Nothing", NULL, 0);
}

/*
 * prim_type_is: Check if a value is of a specific type
 *
 * Args:
 *   value - The value to check
 *   type_obj - A Kind object representing the type to check against
 *
 * Returns: Bool indicating if value is of the specified type
 *
 * Example:
 *   (type? 5 Int) => true
 *   (type? "hello" Int) => false
 *   (type? [1 2 3] Array) => true
 */
Obj* prim_type_is(Obj* value, Obj* type_obj) {
    if (!type_obj) return mk_bool(0);

    /* If type_obj is a Kind object, compare type names */
    if (IS_BOXED(type_obj) && type_obj->tag == TAG_KIND) {
        Kind* kind = (Kind*)type_obj->ptr;
        const char* expected_type = kind->name;
        const char* actual_type = NULL;

        /* Get the actual type of the value */
        if (!value) {
            actual_type = "Null";
        } else if (IS_IMMEDIATE(value)) {
            /* Immediate values (int, char, bool) */
            if (IS_IMMEDIATE_INT(value)) {
                actual_type = "Int";
            } else if (IS_IMMEDIATE_CHAR(value)) {
                actual_type = "Char";
            } else if (IS_IMMEDIATE_BOOL(value)) {
                actual_type = "Bool";
            }
        } else if (IS_BOXED(value)) {
            /* Boxed values */
            switch (value->tag) {
                case TAG_INT:    actual_type = "Int"; break;
                case TAG_FLOAT:  actual_type = "Float"; break;
                case TAG_STRING: actual_type = "String"; break;
                case TAG_SYM:    actual_type = "Symbol"; break;
                case TAG_PAIR:   actual_type = "Pair"; break;
                case TAG_ARRAY:  actual_type = "Array"; break;
                case TAG_CLOSURE: actual_type = "Function"; break;
                case TAG_KIND:   actual_type = "Kind"; break;
                default:         actual_type = "Any"; break;
            }
        }

        /* Check for subtype relationships */
        if (strcmp(expected_type, "Any") == 0) {
            return mk_bool(1);  /* Everything is a subtype of Any */
        }

        /* Direct type match */
        if (actual_type && strcmp(actual_type, expected_type) == 0) {
            return mk_bool(1);
        }

        /* Subtype checks (simplified - full implementation would use type registry) */
        if (strcmp(expected_type, "Number") == 0) {
            return mk_bool(actual_type != NULL &&
                          (strcmp(actual_type, "Int") == 0 ||
                           strcmp(actual_type, "Float") == 0));
        }

        return mk_bool(0);
    }

    return mk_bool(0);
}

/*
 * ==================== Pattern Matching Primitives ====================
 *
 * prim_match_pattern: Runtime pattern matching using Pika parser
 *
 * This provides runtime pattern matching capability. The implementation uses
 * a simple pattern matching approach that supports basic patterns:
 * - Literal string matching
 * - Single-character wildcard (.)
 * - Character classes ([a-z], [0-9], etc.)
 *
 * API: (match-pattern <input> <pattern>)
 *
 * Parameters:
 *   - input_obj: String to match against
 *   - pattern_obj: Pattern string (simple regex-like syntax)
 *
 * Returns:
 *   - Matched string as Obj* if successful
 *   - NULL (nil) if no match
 *
 * Example:
 *   (match-pattern "hello world" "hello")  ; => "hello"
 *   (match-pattern "123 abc" "[0-9]+")      ; => "123"
 */

/*
 * Helper: Match a single character against a pattern character class
 * Pattern syntax:
 *   - 'd' = digit [0-9]
 *   - 'w' = word character [a-zA-Z0-9_]
 *   - 's' = whitespace [ \t\n\r]
 *   - '.' = any character
 *   - 'a' = letter [a-zA-Z]
 *   - otherwise = literal character
 */
#if 0
static int match_char_class(char c, char pattern_type) {
    switch (pattern_type) {
        case 'd': return (c >= '0' && c <= '9');
        case 'w': return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '_');
        case 's': return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        case 'a': return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
        case '.': return 1;
        default: return 0;
    }
}
#endif

/*
 * Simple pattern matcher that supports basic regex-like patterns
 * Returns the length of the match, or 0 if no match
 */
static size_t simple_pattern_match(const char* input, size_t input_len,
                                    const char* pattern, size_t pattern_len) {
    if (!input || !pattern) return 0;

    size_t input_pos = 0;
    size_t pattern_pos = 0;
    size_t match_start = 0;

    /* Find the start of the match (pattern can match anywhere in input) */
    while (match_start < input_len) {
        input_pos = match_start;
        pattern_pos = 0;

        /* Try to match the pattern starting at match_start */
        while (pattern_pos < pattern_len && input_pos < input_len) {
            char p = pattern[pattern_pos];
            char c = input[input_pos];

            if (p == '\\') {
                /* Escaped character - match next char literally */
                if (pattern_pos + 1 < pattern_len) {
                    pattern_pos++;
                    p = pattern[pattern_pos];
                    if (c == p) {
                        pattern_pos++;
                        input_pos++;
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            } else if (p == '[') {
                /* Character class */
                int negated = 0;
                pattern_pos++;
                if (pattern_pos < pattern_len && pattern[pattern_pos] == '^') {
                    negated = 1;
                    pattern_pos++;
                }

                int matched = 0;
                while (pattern_pos < pattern_len && pattern[pattern_pos] != ']') {
                    if (pattern_pos + 2 < pattern_len && pattern[pattern_pos + 1] == '-') {
                        /* Range like a-z */
                        char range_start = pattern[pattern_pos];
                        char range_end = pattern[pattern_pos + 2];
                        if (c >= range_start && c <= range_end) {
                            matched = 1;
                        }
                        pattern_pos += 3;
                    } else {
                        /* Single character */
                        if (pattern[pattern_pos] == c) {
                            matched = 1;
                        }
                        pattern_pos++;
                    }
                }

                if (pattern_pos < pattern_len && pattern[pattern_pos] == ']') {
                    pattern_pos++;
                }

                if (matched != negated) {
                    input_pos++;
                } else {
                    break;
                }
            } else if (p == '*') {
                /* Zero or more of previous element */
                /* For simplicity, treat * as matching any sequence */
                pattern_pos++;
                if (pattern_pos >= pattern_len) {
                    /* * at end matches rest of input */
                    return input_len - match_start;
                }
                /* Try different match lengths */
                while (input_pos <= input_len) {
                    size_t sub_match = simple_pattern_match(input + input_pos,
                                                            input_len - input_pos,
                                                            pattern + pattern_pos,
                                                            pattern_len - pattern_pos);
                    if (sub_match > 0) {
                        return input_pos - match_start + sub_match;
                    }
                    input_pos++;
                }
                break;
            } else if (p == '+') {
                /* One or more of previous character class */
                /* Simplified: match one or more of any character */
                if (input_pos >= input_len) break;
                input_pos++;
                pattern_pos++;
                while (input_pos < input_len && pattern_pos < pattern_len) {
                    p = pattern[pattern_pos];
                    if (p == '$') break; /* End anchor */
                    input_pos++;
                }
            } else if (p == '?') {
                /* Zero or one of previous element - skip */
                pattern_pos++;
            } else if (p == '$') {
                /* End anchor */
                if (input_pos == input_len) {
                    pattern_pos++;
                } else {
                    break;
                }
            } else if (p == '^') {
                /* Start anchor */
                if (input_pos == match_start && match_start == 0) {
                    pattern_pos++;
                } else {
                    break;
                }
            } else {
                /* Literal character match */
                if (c == p) {
                    pattern_pos++;
                    input_pos++;
                } else {
                    break;
                }
            }
        }

        /* Check if we matched the entire pattern */
        if (pattern_pos >= pattern_len) {
            return input_pos - match_start;
        }

        match_start++;
    }

    return 0;
}

/*
 * prim_match_pattern: Match input string against a pattern
 *
 * This is the runtime primitive that can be called from OmniLisp code.
 *
 * Examples:
 *   (match-pattern "hello world" "hello")    ; => "hello"
 *   (match-pattern "123 abc" "[0-9]+")        ; => "123"
 *   (match-pattern "test" "xyz")              ; => nil (no match)
 */
Obj* prim_match_pattern(Obj* input_obj, Obj* pattern_obj) {
    omni_ensure_global_region();

    /* Extract input string */
    const char* input = NULL;
    if (input_obj && !IS_IMMEDIATE(input_obj) && IS_BOXED(input_obj)) {
        if (input_obj->tag == TAG_STRING || input_obj->tag == TAG_SYM) {
            input = (const char*)input_obj->ptr;
        }
    }

    if (!input) {
        return NULL;  /* Return nil for invalid input */
    }

    /* Extract pattern string */
    const char* pattern = NULL;
    if (pattern_obj && !IS_IMMEDIATE(pattern_obj) && IS_BOXED(pattern_obj)) {
        if (pattern_obj->tag == TAG_STRING || pattern_obj->tag == TAG_SYM) {
            pattern = (const char*)pattern_obj->ptr;
        }
    }

    if (!pattern) {
        return NULL;  /* Return nil for invalid pattern */
    }

    /* Perform pattern match */
    size_t input_len = strlen(input);
    size_t pattern_len = strlen(pattern);
    size_t match_len = simple_pattern_match(input, input_len, pattern, pattern_len);

    if (match_len == 0) {
        return NULL;  /* No match - return nil */
    }

    /* Extract and return the matched substring */
    char* matched_str = malloc(match_len + 1);
    if (!matched_str) {
        return NULL;
    }

    /* Find the actual match position */
    size_t match_pos = 0;
    while (match_pos < input_len) {
        size_t test_match = simple_pattern_match(input + match_pos,
                                                  input_len - match_pos,
                                                  pattern, pattern_len);
        if (test_match == match_len) {
            break;
        }
        match_pos++;
    }

    strncpy(matched_str, input + match_pos, match_len);
    matched_str[match_len] = '\0';

    Obj* result = mk_string_region(omni_get_global_region(), matched_str, match_len);
    free(matched_str);

    return result;
}

/* ========== Pattern Compilation (T-wire-pika-compile-04) ========== */

/*
 * prim_compile_pattern: Compile a pattern string for later use
 *
 * This function takes a pattern string and returns it as a compiled pattern object.
 * Currently, this is a simple wrapper that stores the pattern string for later use.
 * In a future implementation, this could use omni_compile_pattern() to create
 * a PikaState for more efficient pattern matching.
 *
 * API: (compile-pattern <pattern-string>)
 *
 * Args:
 *   - pattern_obj: Pattern string to compile
 *
 * Returns:
 *   - Pattern object (currently just returns the pattern string)
 *   - NULL (nil) if input is invalid
 *
 * Example:
 *   (define p (compile-pattern "[0-9]+"))
 *   (match-pattern "123 abc" p)  ; => "123"
 *
 * Note: This is a simple implementation that returns the pattern string as-is.
 * A more sophisticated implementation would pre-compile the pattern into
 * an internal representation for faster matching.
 */
Obj* prim_compile_pattern(Obj* pattern_obj) {
    omni_ensure_global_region();

    /* Extract pattern string */
    const char* pattern = NULL;
    if (pattern_obj && !IS_IMMEDIATE(pattern_obj) && IS_BOXED(pattern_obj)) {
        if (pattern_obj->tag == TAG_STRING || pattern_obj->tag == TAG_SYM) {
            pattern = (const char*)pattern_obj->ptr;
        }
    }

    if (!pattern) {
        return NULL;  /* Return nil for invalid input */
    }

    /* Currently, we just return the pattern string as-is.
     * In a future implementation, this could:
     * 1. Parse the pattern into an internal representation
     * 2. Create a PikaState using omni_compile_pattern()
     * 3. Return an opaque object that can be used for matching
     *
     * For now, we return the pattern string wrapped in a new string object
     * to maintain the API contract.
     */
    return mk_string_region(omni_get_global_region(), pattern, strlen(pattern));
}
/*
 * Simple lock-free channel using atomic operations
 * 
 * Uses fetch-add atomic operations for head/tail updates.
 * Condition variables still used for blocking (empty/full).
 */
int channel_send(Obj* ch_obj, Obj* val) {
    if (!ch_obj || !IS_BOXED(ch_obj) || ch_obj->tag != TAG_CHANNEL) return -1;
    Channel* ch = (Channel*)ch_obj->ptr;
    
    if (ch->capacity > 0) {
        /* Buffered: atomic tail increment */
        int tail = omni_atomic_fetch_add_u32((volatile uint32_t*)&ch->tail, 1);
        int pos = tail % ch->capacity;
        
        /* Issue 2 P4: Use store barrier to enforce Region Closure Property */
        ch->buffer[pos] = omni_store_repair(ch_obj, &ch->buffer[pos], val);
        omni_atomic_add_fetch_u32((volatile uint32_t*)&ch->count, 1);
        
        /* Signal receivers */
        pthread_cond_signal(&ch->recv_cond);
        
        /* Report quiescent state */
        qsbr_quiescent();
        
        return 0;
    } else {
        /* Unbuffered: handshake (simplified) */
        pthread_mutex_t temp_lock = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&temp_lock);
        
        while (!ch->closed && omni_atomic_load_u32((volatile uint32_t*)&ch->count) > 0) {
            pthread_cond_wait(&ch->send_cond, &temp_lock);
        }
        
        if (ch->closed) {
            pthread_mutex_unlock(&temp_lock);
            return -1;
        }
        
        ch->buffer = (Obj**)&val;
        omni_atomic_store_u32((volatile uint32_t*)&ch->count, 1);
        
        pthread_cond_signal(&ch->recv_cond);
        
        while (!ch->closed && omni_atomic_load_u32((volatile uint32_t*)&ch->count) > 0) {
            pthread_cond_wait(&ch->send_cond, &temp_lock);
        }
        
        pthread_mutex_unlock(&temp_lock);
        qsbr_quiescent();
        return 0;
    }
}

Obj* channel_recv(Obj* ch_obj) {
    if (!ch_obj || !IS_BOXED(ch_obj) || ch_obj->tag != TAG_CHANNEL) return NULL;
    Channel* ch = (Channel*)ch_obj->ptr;
    
    if (ch->capacity > 0) {
        /* Buffered: atomic head increment */
        int head = omni_atomic_fetch_add_u32((volatile uint32_t*)&ch->head, 1);
        int pos = head % ch->capacity;
        
        Obj* val = ch->buffer[pos];
        omni_atomic_sub_fetch_u32((volatile uint32_t*)&ch->count, 1);
        
        /* Signal senders */
        pthread_cond_signal(&ch->send_cond);
        
        /* Report quiescent state */
        qsbr_quiescent();
        
        return val;
    } else {
        /* Unbuffered: handshake (simplified) */
        pthread_mutex_t temp_lock = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&temp_lock);
        
        while (!ch->closed && omni_atomic_load_u32((volatile uint32_t*)&ch->count) == 0) {
            pthread_cond_wait(&ch->recv_cond, &temp_lock);
        }
        
        if (omni_atomic_load_u32((volatile uint32_t*)&ch->count) == 0 && ch->closed) {
            pthread_mutex_unlock(&temp_lock);
            return NULL;
        }
        
        Obj* val = *(Obj**)ch->buffer;
        omni_atomic_store_u32((volatile uint32_t*)&ch->count, 0);
        
        pthread_cond_signal(&ch->send_cond);
        
        pthread_mutex_unlock(&temp_lock);
        qsbr_quiescent();
        return val;
    }
}

void channel_close(Obj* ch_obj) {
    if (!ch_obj || !IS_BOXED(ch_obj) || ch_obj->tag != TAG_CHANNEL) return;
    Channel* ch = (Channel*)ch_obj->ptr;

    __atomic_store_n(&ch->closed, true, __ATOMIC_RELEASE);

    pthread_cond_broadcast(&ch->send_cond);
    pthread_cond_broadcast(&ch->recv_cond);
}

/* ============================================================
 * Effect System Primitives (Phase 22: Algebraic Effects)
 * ============================================================
 *
 * These primitives expose the effect system (effect.h) to OmniLisp code.
 * They bridge the Lisp calling convention (Obj* -> Obj*) to the C effect API.
 */

/*
 * prim_effect_init: Initialize the effect system
 * Must be called before using any effect operations.
 */
Obj* prim_effect_init(void) {
    effect_init();
    return mk_nothing();
}

/*
 * prim_perform: Perform an effect by name
 * Args: effect_name (symbol/string), payload (any)
 * Returns: The value passed to resume(), or handler's result
 *
 * Usage: (perform 'Fail "error message")
 */
Obj* prim_perform(Obj* effect_name, Obj* payload) {
    if (!effect_name) {
        fprintf(stderr, "perform: effect name cannot be nil\n");
        return NULL;
    }

    const char* name = NULL;
    if (IS_BOXED(effect_name)) {
        if (effect_name->tag == TAG_SYM) {
            name = (const char*)effect_name->ptr;
        } else if (effect_name->tag == TAG_STRING) {
            name = (const char*)effect_name->ptr;
        }
    }

    if (!name) {
        fprintf(stderr, "perform: effect name must be a symbol or string\n");
        return NULL;
    }

    return effect_perform_named(name, payload);
}

/*
 * prim_raise: Raise a Fail effect (non-resumable error)
 * Args: message (string)
 * Returns: Never returns normally
 *
 * Usage: (raise "Something went wrong")
 */
Obj* prim_raise(Obj* message) {
    if (!EFFECT_FAIL) {
        effect_init();
    }
    return effect_perform(EFFECT_FAIL, message);
}

/*
 * prim_resume: Resume a suspended computation with a value
 * Args: resumption (Resumption object), value (any)
 * Returns: The result of the resumed computation
 *
 * Usage: (resume k 42)
 */
Obj* prim_resume(Obj* resumption_obj, Obj* value) {
    if (!resumption_obj || !IS_BOXED(resumption_obj)) {
        fprintf(stderr, "resume: invalid resumption object\n");
        return NULL;
    }

    if (resumption_obj->tag != TAG_RESUMPTION) {
        fprintf(stderr, "resume: expected resumption, got tag %d\n", resumption_obj->tag);
        return NULL;
    }

    Resumption* r = (Resumption*)resumption_obj->ptr;
    return resumption_invoke(r, value);
}

/*
 * prim_effect_type_register: Register a new effect type
 * Args: name (symbol/string)
 * Returns: Effect type object (for use with prim_perform_typed)
 *
 * Usage: (define MyEffect (effect-type-register 'MyEffect))
 */
Obj* prim_effect_type_register(Obj* name) {
    const char* name_str = NULL;
    if (IS_BOXED(name)) {
        if (name->tag == TAG_SYM || name->tag == TAG_STRING) {
            name_str = (const char*)name->ptr;
        }
    }

    if (!name_str) {
        fprintf(stderr, "effect-type-register: name must be a symbol or string\n");
        return NULL;
    }

    EffectType* type = effect_type_register(name_str, NULL, NULL);

    /* Wrap the effect type in an Obj */
    _ensure_global_region();
    Obj* result = region_alloc(_global_region, sizeof(Obj));
    if (!result) return NULL;
    result->tag = TAG_EFFECT_TYPE;
    result->ptr = type;
    result->mark = 1;
    return result;
}

/*
 * prim_perform_typed: Perform an effect using a typed EffectType
 * Args: effect_type (EffectType obj), payload (any)
 * Returns: The value passed to resume(), or handler's result
 */
Obj* prim_perform_typed(Obj* effect_type_obj, Obj* payload) {
    if (!effect_type_obj || !IS_BOXED(effect_type_obj) ||
        effect_type_obj->tag != TAG_EFFECT_TYPE) {
        fprintf(stderr, "perform-typed: expected effect type object\n");
        return NULL;
    }

    EffectType* type = (EffectType*)effect_type_obj->ptr;
    return effect_perform(type, payload);
}

/*
 * mk_resumption_obj: Create an Obj wrapper for a Resumption
 * Used internally by effect_perform when capturing continuations.
 */
Obj* mk_resumption_obj(Resumption* r) {
    if (!r) return NULL;

    _ensure_global_region();
    Obj* obj = region_alloc(_global_region, sizeof(Obj));
    if (!obj) return NULL;

    obj->tag = TAG_RESUMPTION;
    obj->ptr = r;
    obj->mark = 1;

    /* The Obj now owns the resumption reference */
    resumption_inc_ref(r);

    return obj;
}

/*
 * prim_handler_push: Push a handler onto the stack
 * Args: clauses (list of (effect-name handler-fn) pairs)
 * Returns: Handler object
 *
 * Note: This is a low-level primitive. Prefer using the (handle ...) form.
 */
Obj* prim_handler_push(Obj* clauses, Obj* return_clause, Obj* env) {
    HandlerClause* c_clauses = NULL;

    /* Parse clauses: each is (effect-name handler-fn) */
    Obj* iter = clauses;
    while (iter && IS_BOXED(iter) && iter->tag == TAG_PAIR) {
        Obj* clause = obj_car(iter);
        if (clause && IS_BOXED(clause) && clause->tag == TAG_PAIR) {
            Obj* effect_name = obj_car(clause);
            Obj* handler_fn = obj_car(obj_cdr(clause));

            const char* name_str = NULL;
            if (IS_BOXED(effect_name)) {
                if (effect_name->tag == TAG_SYM || effect_name->tag == TAG_STRING) {
                    name_str = (const char*)effect_name->ptr;
                }
            }

            if (name_str) {
                EffectType* type = effect_type_find(name_str);
                if (type) {
                    c_clauses = handler_clause_add(c_clauses, type, handler_fn);
                }
            }
        }
        iter = obj_cdr(iter);
    }

    Handler* h = handler_push(c_clauses, return_clause, env);

    /* Wrap handler in Obj */
    _ensure_global_region();
    Obj* result = region_alloc(_global_region, sizeof(Obj));
    if (!result) return NULL;
    result->tag = TAG_HANDLER;
    result->ptr = h;
    result->mark = 1;
    return result;
}

/*
 * prim_handler_pop: Pop the current handler from the stack
 * Returns: nothing
 */
Obj* prim_handler_pop(void) {
    handler_pop();
    return mk_nothing();
}

/*
 * prim_yield: Yield a value (generator effect)
 * Args: value (any)
 * Returns: The value passed to resume (next iteration input)
 *
 * This function bridges two yield mechanisms:
 * 1. Frame-based generators: Uses generator_yield() when inside a generator context
 * 2. Effect-based yield: Uses effect_perform(EFFECT_YIELD, value) when a handler exists
 *
 * Usage: (yield 42)
 */
Obj* prim_yield(Obj* value) {
    /* Check if we're in a generator context (FRAME_YIELD on stack) */
    Frame* f = cont_get_current();
    while (f) {
        if (f->tag == FRAME_YIELD) {
            /* We're in a generator - use frame-based mechanism */
            generator_yield(value);
            /* After resume, generator_yield has returned */
            return NULL;  /* Resume value will be set by generator_next */
        }
        f = f->prev;
    }

    /* Not in generator context - try effect system */
    if (!EFFECT_YIELD) {
        effect_init();
    }

    /* Check if there's a Yield handler installed */
    Handler* h = handler_find(EFFECT_YIELD);
    if (h) {
        return effect_perform(EFFECT_YIELD, value);
    }

    /* No handler - this is likely an error */
    fprintf(stderr, "Error: yield called outside of generator or effect handler\n");
    return NULL;
}

/*
 * prim_ask: Ask for a value (reader effect)
 * Args: prompt (optional, defaults to nil)
 * Returns: The value provided by the handler
 *
 * Usage: (ask) or (ask "username")
 */
Obj* prim_ask(Obj* prompt) {
    if (!EFFECT_ASK) {
        effect_init();
    }
    return effect_perform(EFFECT_ASK, prompt);
}

/*
 * prim_emit: Emit a value (writer effect)
 * Args: value (any)
 * Returns: nothing (handler may collect values)
 *
 * Usage: (emit "log message")
 */
Obj* prim_emit(Obj* value) {
    if (!EFFECT_EMIT) {
        effect_init();
    }
    return effect_perform(EFFECT_EMIT, value);
}

/* ============================================================
 * Phase 4.2: Update Operators
 * ============================================================
 *
 * Provides functional (copy-on-write) and imperative (in-place)
 * update operations for arrays and dicts.
 *
 * API:
 *   - prim_dict_copy: Shallow copy of dict
 *   - prim_update: COW update for arrays/dicts
 *   - prim_update_bang: In-place update for arrays/dicts
 *   - prim_update_in: Nested path COW update
 *   - prim_update_in_bang: Nested path in-place update
 */

// TESTED - tests/test_dict_copy.omni
/*
 * prim_dict_copy: Create a shallow copy of a dict
 *
 * Args: dict - Dict to copy
 * Returns: New dict with same key-value pairs
 *
 * Example:
 *   (dict-copy {:a 1 :b 2}) ; -> {:a 1 :b 2} (new dict)
 */
Obj* prim_dict_copy(Obj* dict) {
    if (!dict || !IS_BOXED(dict) || dict->tag != TAG_DICT) return NULL;
    Dict* d = (Dict*)dict->ptr;

    _ensure_global_region();
    Obj* result = mk_dict();

    /* Iterate over all buckets and copy entries */
    for (size_t i = 0; i < d->map.bucket_count; i++) {
        HashEntry* entry = d->map.buckets[i];
        while (entry) {
            dict_set(result, (Obj*)entry->key, (Obj*)entry->value);
            entry = entry->next;
        }
    }

    return result;
}

/*
 * prim_update: Copy-on-write update for arrays and dicts
 *
 * Args:
 *   - coll: Array or dict to update
 *   - key: Index (for array) or key (for dict)
 *   - val: New value
 *
 * Returns: New collection with the update applied
 *
 * Example:
 *   (update [1 2 3] 1 42)     ; -> [1 42 3]
 *   (update {:a 1} :b 2)      ; -> {:a 1 :b 2}
 */
Obj* prim_update(Obj* coll, Obj* key, Obj* val) {
    if (!coll || !IS_BOXED(coll)) return NULL;

    if (coll->tag == TAG_ARRAY) {
        /* Array update: key must be an integer index */
        long idx = obj_to_int(key);
        Array* a = (Array*)coll->ptr;

        /* Bounds check */
        if (idx < 0 || idx >= a->len) {
            return coll;  /* Out of bounds: return original unchanged */
        }

        /* Create copy and update */
        Obj* result = prim_array_copy(coll);
        array_set(result, (int)idx, val);
        return result;
    }

    if (coll->tag == TAG_DICT) {
        /* Dict update: copy and add/overwrite key */
        Obj* result = prim_dict_copy(coll);
        dict_set(result, key, val);
        return result;
    }

    /* Unsupported collection type */
    return NULL;
}

/*
 * prim_update_bang: In-place update for arrays and dicts
 *
 * Args:
 *   - coll: Array or dict to update
 *   - key: Index (for array) or key (for dict)
 *   - val: New value
 *
 * Returns: Same collection (mutated)
 *
 * Example:
 *   (update! arr 1 42)   ; modifies arr[1], returns arr
 *   (update! dict :a 1)  ; modifies dict[:a], returns dict
 */
Obj* prim_update_bang(Obj* coll, Obj* key, Obj* val) {
    if (!coll || !IS_BOXED(coll)) return NULL;

    if (coll->tag == TAG_ARRAY) {
        /* Array update: key must be an integer index */
        long idx = obj_to_int(key);
        array_set(coll, (int)idx, val);
        return coll;
    }

    if (coll->tag == TAG_DICT) {
        /* Dict update */
        dict_set(coll, key, val);
        return coll;
    }

    /* Unsupported collection type */
    return NULL;
}

/*
 * Helper: Get value from collection by key
 */
static Obj* coll_get(Obj* coll, Obj* key) {
    if (!coll || !IS_BOXED(coll)) return NULL;

    if (coll->tag == TAG_ARRAY) {
        long idx = obj_to_int(key);
        return array_get(coll, (int)idx);
    }

    if (coll->tag == TAG_DICT) {
        return dict_get(coll, key);
    }

    return NULL;
}

/*
 * prim_update_in: Nested path copy-on-write update
 *
 * Args:
 *   - coll: Root collection (array or dict)
 *   - path: Array of keys specifying the path
 *   - val: New value to set at path
 *
 * Returns: New collection with nested update applied
 *
 * Example:
 *   (update-in {:a {:b 1}} [:a :b] 42)  ; -> {:a {:b 42}}
 *   (update-in [[1 2] [3 4]] [1 0] 99)  ; -> [[1 2] [99 4]]
 */
Obj* prim_update_in(Obj* coll, Obj* path, Obj* val) {
    if (!coll || !IS_BOXED(coll)) return NULL;
    if (!path || !IS_BOXED(path) || path->tag != TAG_ARRAY) {
        /* Path must be an array */
        return NULL;
    }

    Array* path_arr = (Array*)path->ptr;
    if (path_arr->len == 0) {
        /* Empty path: return val (replace entire collection) */
        return val;
    }

    if (path_arr->len == 1) {
        /* Single key: use regular update */
        return prim_update(coll, path_arr->data[0], val);
    }

    /* Multi-level path: recursively build new structure */
    Obj* first_key = path_arr->data[0];
    Obj* nested = coll_get(coll, first_key);

    /* Build rest of path */
    _ensure_global_region();
    Obj* rest_path = mk_array(path_arr->len - 1);
    for (int i = 1; i < path_arr->len; i++) {
        array_push(rest_path, path_arr->data[i]);
    }

    /* Recursively update nested structure */
    Obj* updated_nested = prim_update_in(nested, rest_path, val);

    /* Update this level */
    return prim_update(coll, first_key, updated_nested);
}

/*
 * prim_update_in_bang: Nested path in-place update
 *
 * Args:
 *   - coll: Root collection (array or dict)
 *   - path: Array of keys specifying the path
 *   - val: New value to set at path
 *
 * Returns: Same collection (mutated)
 *
 * Example:
 *   (update-in! data [:a :b] 42)  ; modifies data[:a][:b], returns data
 */
Obj* prim_update_in_bang(Obj* coll, Obj* path, Obj* val) {
    if (!coll || !IS_BOXED(coll)) return NULL;
    if (!path || !IS_BOXED(path) || path->tag != TAG_ARRAY) {
        return NULL;
    }

    Array* path_arr = (Array*)path->ptr;
    if (path_arr->len == 0) {
        /* Empty path: can't replace in-place */
        return coll;
    }

    if (path_arr->len == 1) {
        /* Single key: use regular in-place update */
        return prim_update_bang(coll, path_arr->data[0], val);
    }

    /* Navigate to parent of target, then update */
    Obj* current = coll;
    for (int i = 0; i < path_arr->len - 1; i++) {
        current = coll_get(current, path_arr->data[i]);
        if (!current || !IS_BOXED(current)) {
            /* Path doesn't exist */
            return coll;
        }
    }

    /* Update the leaf */
    Obj* last_key = path_arr->data[path_arr->len - 1];
    prim_update_bang(current, last_key, val);

    return coll;
}
