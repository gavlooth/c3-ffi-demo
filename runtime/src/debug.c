/*
 * debug.c - Developer tools and debugging utilities for OmniLisp
 *
 * Issue 27: Developer Tools & Debugging
 *
 * P0: Object Inspection
 *   - prim_inspect: Detailed object info (type, value, address, refcount, region)
 *   - prim_type_of: Return type as symbol
 *   - prim_address_of: Return object memory address as integer
 *   - prim_refcount_of: Return current reference count
 *   - prim_region_of: Return owning region info as dict
 *   - prim_sizeof: Return object memory footprint in bytes
 *
 * P1: Memory Debugging
 *   - prim_region_stats: Print region allocation statistics
 *   - prim_memory_usage: Return total memory allocated
 *   - prim_gc_info: Show RC statistics
 *   - prim_allocation_trace: Toggle allocation tracing
 *   - prim_leak_check: Scan for potential memory leaks
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include "../include/omni.h"
#include "internal_types.h"
#include "memory/region_core.h"
#include "memory/continuation.h"

/* ============== P1: Memory Debugging Globals ============== */

/* Global counters for memory debugging */
static bool g_alloc_trace = false;
static size_t g_total_allocs = 0;
static size_t g_total_frees = 0;
static size_t g_total_bytes_allocated = 0;

/* ============== Helper Functions ============== */

// REVIEWED:NAIVE
/*
 * env_lookup - Look up a symbol in an environment (alist)
 *
 * The environment is stored as an association list:
 *   ((sym1 . val1) (sym2 . val2) ...)
 *
 * Walks the list comparing symbol names until a match is found.
 * Returns the bound value or NULL if not found.
 */
static Obj* env_lookup(Obj* env, const char* name) {
    if (!name) return NULL;

    /* Walk the alist */
    while (env && IS_BOXED(env) && env->tag == TAG_PAIR) {
        Obj* binding = env->a;  /* (symbol . value) pair */

        if (binding && IS_BOXED(binding) && binding->tag == TAG_PAIR) {
            Obj* sym = binding->a;

            /* Check if this binding's symbol matches */
            if (sym && IS_BOXED(sym) && sym->tag == TAG_SYM) {
                const char* sym_name = (const char*)sym->ptr;
                if (sym_name && strcmp(sym_name, name) == 0) {
                    return binding->b;  /* Return the value */
                }
            }
        }

        env = env->b;  /* Move to next binding */
    }

    return NULL;  /* Not found */
}

/*
 * env_lookup_current - Look up a symbol in the current thread's environment
 *
 * Convenience wrapper that gets the current environment and performs lookup.
 */
static Obj* env_lookup_current(const char* name) {
    Obj* env = cont_get_env();
    return env_lookup(env, name);
}

/*
 * tag_to_string - Convert an ObjTag to a human-readable string
 */
static const char* tag_to_string(int tag) {
    switch (tag) {
        case TAG_INT:       return "Int";
        case TAG_FLOAT:     return "Float";
        case TAG_CHAR:      return "Char";
        case TAG_PAIR:      return "Pair";
        case TAG_SYM:       return "Symbol";
        case TAG_BOX:       return "Box";
        case TAG_CLOSURE:   return "Closure";
        /* DIRECTIVE: NO CHANNELS - TAG_CHANNEL removed */
        case TAG_ERROR:     return "Error";
        case TAG_ATOM:      return "Atom";
        case TAG_THREAD:    return "Thread";
        case TAG_ARRAY:     return "Array";
        case TAG_DICT:      return "Dict";
        case TAG_STRING:    return "String";
        case TAG_KEYWORD:   return "Keyword";
        case TAG_SET:       return "Set";
        case TAG_DATETIME:  return "DateTime";
        case TAG_GENERIC:   return "Generic";
        case TAG_KIND:      return "Kind";
        case TAG_NOTHING:   return "Nothing";
        case TAG_EFFECT_TYPE: return "EffectType";
        case TAG_RESUMPTION:  return "Resumption";
        case TAG_HANDLER:     return "Handler";
        case TAG_CONDITION:   return "Condition";
        case TAG_GENERATOR:   return "Generator";
        case TAG_CPTR:        return "CPtr";
        default:            return "Unknown";
    }
}

/*
 * print_obj_value - Print the value of an object for debugging
 */
static void print_obj_value(Obj* obj) {
    if (!obj) {
        printf("nil");
        return;
    }

    /* Handle immediates */
    if (IS_IMMEDIATE_INT(obj)) {
        printf("%ld", INT_IMM_VALUE(obj));
        return;
    }
    if (IS_IMMEDIATE_BOOL(obj)) {
        printf("%s", obj == OMNI_TRUE ? "true" : "false");
        return;
    }
    if (IS_IMMEDIATE_CHAR(obj)) {
        long c = CHAR_IMM_VALUE(obj);
        if (c >= 32 && c < 127) {
            printf("'%c'", (char)c);
        } else {
            printf("'\\u%04lx'", c);
        }
        return;
    }

    /* Handle boxed values */
    if (!IS_BOXED(obj)) {
        printf("<invalid>");
        return;
    }

    switch (obj->tag) {
        case TAG_INT:
            printf("%ld", obj->i);
            break;
        case TAG_FLOAT:
            printf("%g", obj->f);
            break;
        case TAG_CHAR:
            if (obj->i >= 32 && obj->i < 127) {
                printf("'%c'", (char)obj->i);
            } else {
                printf("'\\u%04lx'", obj->i);
            }
            break;
        case TAG_STRING:
            printf("\"%s\"", obj->ptr ? (char*)obj->ptr : "");
            break;
        case TAG_SYM:
            printf("%s", obj->ptr ? (char*)obj->ptr : "<null-sym>");
            break;
        case TAG_KEYWORD:
            printf(":%s", obj->ptr ? (char*)obj->ptr : "<null-kw>");
            break;
        case TAG_PAIR:
            printf("(");
            print_obj_value(obj->a);
            printf(" . ");
            print_obj_value(obj->b);
            printf(")");
            break;
        case TAG_ARRAY: {
            Array* arr = (Array*)obj->ptr;
            printf("[");
            if (arr) {
                for (int i = 0; i < arr->len && i < 5; i++) {
                    if (i > 0) printf(" ");
                    print_obj_value(arr->data[i]);
                }
                if (arr->len > 5) printf(" ... (%d more)", arr->len - 5);
            }
            printf("]");
            break;
        }
        case TAG_DICT:
            printf("#{...}");
            break;
        case TAG_SET:
            printf("#{set ...}");
            break;
        case TAG_CLOSURE: {
            Closure* c = (Closure*)obj->ptr;
            printf("<closure %s arity=%d>", c ? c->name : "?", c ? c->arity : -1);
            break;
        }
        case TAG_GENERIC: {
            Generic* g = (Generic*)obj->ptr;
            printf("<generic %s methods=%d>", g ? g->name : "?", g ? g->method_count : 0);
            break;
        }
        case TAG_KIND: {
            Kind* k = (Kind*)obj->ptr;
            printf("<kind %s>", k ? k->name : "?");
            break;
        }
        case TAG_BOX:
            printf("<box ");
            print_obj_value(obj->a);
            printf(">");
            break;
        case TAG_ERROR:
            printf("<error: %s>", obj->ptr ? (char*)obj->ptr : "unknown");
            break;
        case TAG_NOTHING:
            printf("nothing");
            break;
        default:
            printf("<%s>", tag_to_string(obj->tag));
    }
}

/*
 * calculate_obj_size - Calculate the memory footprint of an object
 */
static size_t calculate_obj_size(Obj* obj) {
    if (!obj) return 0;

    /* Immediates take no heap space */
    if (IS_IMMEDIATE(obj)) return 0;

    size_t size = sizeof(Obj);

    switch (obj->tag) {
        case TAG_STRING:
        case TAG_SYM:
        case TAG_KEYWORD:
            if (obj->ptr) {
                size += strlen((char*)obj->ptr) + 1;
            }
            break;
        case TAG_ARRAY: {
            Array* arr = (Array*)obj->ptr;
            if (arr) {
                size += sizeof(Array);
                size += arr->capacity * sizeof(Obj*);
            }
            break;
        }
        case TAG_DICT:
        case TAG_SET:
            /* HashMap internals */
            size += sizeof(Dict);  /* or Set */
            /* Would need to introspect hashmap for accurate size */
            break;
        case TAG_CLOSURE: {
            Closure* c = (Closure*)obj->ptr;
            if (c) {
                size += sizeof(Closure);
                size += c->capture_count * sizeof(Obj*);
            }
            break;
        }
        case TAG_GENERIC: {
            Generic* g = (Generic*)obj->ptr;
            if (g) {
                size += sizeof(Generic);
                /* Count method infos */
                MethodInfo* m = g->methods;
                while (m) {
                    size += sizeof(MethodInfo);
                    m = m->next;
                }
            }
            break;
        }
        case TAG_KIND: {
            Kind* k = (Kind*)obj->ptr;
            if (k) {
                size += sizeof(Kind);
                if (k->name) size += strlen(k->name) + 1;
                size += k->param_count * sizeof(Obj*);
            }
            break;
        }
        default:
            /* Basic object size only */
            break;
    }

    return size;
}

/* ============== P0: Object Inspection ============== */

// TESTED - tests/test_debug_primitives.lisp
/*
 * prim_inspect - Detailed object info
 *
 * Prints detailed information about an object including:
 * - Type (tag)
 * - Value representation
 * - Memory address
 * - Reference count
 * - Region info
 * - Size in bytes
 *
 * Returns: nothing
 */
Obj* prim_inspect(Obj* obj) {
    printf("=== Object Inspection ===\n");

    if (!obj) {
        printf("  Value: nil (NULL pointer)\n");
        printf("  Type: Nil\n");
        printf("  Address: 0x0\n");
        printf("=========================\n");
        return mk_nothing();
    }

    /* Check for immediates */
    if (IS_IMMEDIATE(obj)) {
        printf("  Value: ");
        print_obj_value(obj);
        printf("\n");

        if (IS_IMMEDIATE_INT(obj)) {
            printf("  Type: Int (immediate)\n");
        } else if (IS_IMMEDIATE_BOOL(obj)) {
            printf("  Type: Bool (immediate)\n");
        } else if (IS_IMMEDIATE_CHAR(obj)) {
            printf("  Type: Char (immediate)\n");
        }

        printf("  Address: 0x%016" PRIxPTR " (tagged pointer)\n", (uintptr_t)obj);
        printf("  Refcount: N/A (immediate)\n");
        printf("  Region: N/A (immediate)\n");
        printf("  Size: 0 bytes (no heap allocation)\n");
        printf("=========================\n");
        return mk_nothing();
    }

    /* Boxed object */
    printf("  Value: ");
    print_obj_value(obj);
    printf("\n");

    printf("  Type: %s (tag=%d)\n", tag_to_string(obj->tag), obj->tag);
    printf("  Address: %p\n", (void*)obj);
    printf("  Refcount: %d\n", obj->mark);
    printf("  Generation: %" PRIu16 "\n", obj->generation);
    printf("  SCC ID: %d\n", obj->scc_id);
    printf("  Tethered: %s\n", obj->tethered ? "yes" : "no");

    Region* r = omni_obj_region(obj);
    if (r) {
        printf("  Region: %p (rank=%" PRIu64 ")\n", (void*)r, omni_region_get_lifetime_rank(r));
    } else {
        printf("  Region: none (global or immediate)\n");
    }

    printf("  Size: %zu bytes\n", calculate_obj_size(obj));
    printf("=========================\n");

    return mk_nothing();
}

// TESTED - runtime/tests/test_debug_primitives.c
/*
 * prim_type_of - Return type as symbol
 *
 * Returns a symbol representing the type of the object.
 * Examples: :int, :float, :string, :array, :closure, etc.
 *
 * For user-defined struct types, returns the struct type name
 * registered via omni_register_user_type.
 */
Obj* prim_type_of(Obj* obj) {
    if (!obj) {
        return mk_keyword("nil");
    }

    if (IS_IMMEDIATE_INT(obj)) {
        return mk_keyword("int");
    }
    if (IS_IMMEDIATE_BOOL(obj)) {
        return mk_keyword("bool");
    }
    if (IS_IMMEDIATE_CHAR(obj)) {
        return mk_keyword("char");
    }

    if (!IS_BOXED(obj)) {
        return mk_keyword("unknown");
    }

    /* Use omni_type_name_from_tag for unified type name lookup
     * This handles both built-in and user-defined types */
    const char* type_str = omni_type_name_from_tag(obj->tag);
    if (!type_str) {
        /* Fallback to tag_to_string for unregistered types */
        type_str = tag_to_string(obj->tag);
    }

    /* Convert to lowercase keyword */
    char lower[64];
    int i = 0;
    while (type_str[i] && i < 63) {
        char c = type_str[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
        i++;
    }
    lower[i] = '\0';

    return mk_keyword(lower);
}

// TESTED - tests/test_debug_primitives.lisp
/*
 * prim_address_of - Return object memory address as integer
 *
 * For debugging: returns the raw memory address of the object.
 * For immediates, returns the tagged pointer value.
 */
Obj* prim_address_of(Obj* obj) {
    return mk_int((long)(uintptr_t)obj);
}

// TESTED - tests/test_debug_primitives.lisp
/*
 * prim_region_of_debug - Return owning region info as dict
 *
 * Returns a dictionary with region information:
 * - :address - region pointer as integer
 * - :rank - lifetime rank
 *
 * Returns nothing if object has no region (immediate or global).
 */
Obj* prim_region_of_debug(Obj* obj) {
    if (!obj || IS_IMMEDIATE(obj)) {
        return mk_nothing();
    }

    Region* r = omni_obj_region(obj);
    if (!r) {
        return mk_nothing();
    }

    Obj* dict = mk_dict();
    dict_set(dict, mk_keyword("address"), mk_int((long)(uintptr_t)r));
    dict_set(dict, mk_keyword("rank"), mk_int((long)omni_region_get_lifetime_rank(r)));

    return dict;
}

// TESTED - tests/test_debug_primitives.lisp
/*
 * prim_sizeof - Return object memory footprint in bytes
 *
 * Returns the approximate memory footprint of an object,
 * including the Obj struct and any associated data (strings, arrays, etc.).
 */
Obj* prim_sizeof(Obj* obj) {
    return mk_int((long)calculate_obj_size(obj));
}

/* ============== P1: Memory Debugging ============== */

/*
 * prim_region_stats - Print region allocation statistics
 *
 * Prints statistics about the global region including:
 * - Total allocations
 * - Current live objects
 * - Memory usage
 *
 * Returns: nothing
 */
Obj* prim_region_stats(void) {
    printf("=== Region Statistics ===\n");
    printf("  Total allocations: %zu\n", g_total_allocs);
    printf("  Total frees: %zu\n", g_total_frees);
    printf("  Live objects: %zu (approx)\n", g_total_allocs - g_total_frees);
    printf("  Total bytes allocated: %zu\n", g_total_bytes_allocated);

    Region* r = omni_get_global_region();
    if (r) {
        printf("  Global region: %p\n", (void*)r);
        printf("  Global region rank: %" PRIu64 "\n", omni_region_get_lifetime_rank(r));
    }

    printf("=========================\n");
    return mk_nothing();
}

// TESTED - runtime/tests/test_memory_usage.c
/*
 * prim_memory_usage - Return total memory allocated
 *
 * Returns the total bytes allocated across all regions.
 */
Obj* prim_memory_usage(void) {
    return mk_int((long)g_total_bytes_allocated);
}

// TESTED - tests/test_gc_info.lisp
/*
 * prim_gc_info - Show RC statistics
 *
 * Returns a dictionary with garbage collection / reference counting info:
 * - :total_allocs - total object allocations
 * - :total_frees - total object deallocations
 * - :live_objects - current live object count (estimate)
 */
Obj* prim_gc_info(void) {
    Obj* dict = mk_dict();
    dict_set(dict, mk_keyword("total-allocs"), mk_int((long)g_total_allocs));
    dict_set(dict, mk_keyword("total-frees"), mk_int((long)g_total_frees));
    dict_set(dict, mk_keyword("live-objects"), mk_int((long)(g_total_allocs - g_total_frees)));
    dict_set(dict, mk_keyword("bytes-allocated"), mk_int((long)g_total_bytes_allocated));
    dict_set(dict, mk_keyword("tracing-enabled"), mk_bool(g_alloc_trace));
    return dict;
}

/*
 * prim_allocation_trace - Toggle allocation tracing to stderr
 *
 * When enabled, prints allocation/deallocation events to stderr.
 * Argument: boolean to enable/disable
 * Returns: previous state
 */
Obj* prim_allocation_trace(Obj* enable) {
    bool prev = g_alloc_trace;
    g_alloc_trace = is_truthy(enable);

    if (g_alloc_trace) {
        fprintf(stderr, "[ALLOC TRACE] Allocation tracing ENABLED\n");
    } else if (prev) {
        fprintf(stderr, "[ALLOC TRACE] Allocation tracing DISABLED\n");
    }

    return mk_bool(prev);
}

// TESTED - runtime/tests/test_leak_check.c
/*
 * prim_leak_check - Scan for potential memory leaks
 *
 * This is a diagnostic tool that reports on memory usage patterns.
 * In a full implementation, this would walk all live objects and
 * find those with no incoming references.
 *
 * Returns: dict with diagnostic info
 */
Obj* prim_leak_check(void) {
    printf("=== Leak Check ===\n");
    printf("  Estimated leaks: %zu objects\n",
           g_total_allocs > g_total_frees ? g_total_allocs - g_total_frees : 0);
    printf("  Note: This is a rough estimate based on allocation counters.\n");
    printf("  For precise leak detection, use a tool like Valgrind.\n");
    printf("==================\n");

    Obj* dict = mk_dict();
    dict_set(dict, mk_keyword("estimated-leaks"),
             mk_int((long)(g_total_allocs > g_total_frees ? g_total_allocs - g_total_frees : 0)));
    dict_set(dict, mk_keyword("total-allocs"), mk_int((long)g_total_allocs));
    dict_set(dict, mk_keyword("total-frees"), mk_int((long)g_total_frees));
    return dict;
}

/* ============== Debug Hooks for Allocation Tracking ============== */

/*
 * debug_track_alloc - Called when an object is allocated
 *
 * This function should be called by allocation routines to track
 * allocations for debugging purposes.
 */
void debug_track_alloc(Obj* obj, size_t size) {
    g_total_allocs++;
    g_total_bytes_allocated += size;

    if (g_alloc_trace && obj) {
        fprintf(stderr, "[ALLOC] %p type=%s size=%zu\n",
                (void*)obj, tag_to_string(obj->tag), size);
    }
}

/*
 * debug_track_free - Called when an object is freed
 *
 * This function should be called by deallocation routines to track
 * frees for debugging purposes.
 */
void debug_track_free(Obj* obj) {
    g_total_frees++;

    if (g_alloc_trace && obj) {
        fprintf(stderr, "[FREE] %p type=%s\n",
                (void*)obj, IS_BOXED(obj) ? tag_to_string(obj->tag) : "immediate");
    }
}

/*
 * debug_reset_counters - Reset all debug counters
 *
 * Useful for starting a fresh measurement period.
 */
void debug_reset_counters(void) {
    g_total_allocs = 0;
    g_total_frees = 0;
    g_total_bytes_allocated = 0;

    if (g_alloc_trace) {
        fprintf(stderr, "[ALLOC TRACE] Counters reset\n");
    }
}

/* ============== Documentation System (Issue 27 P2) ============== */

/* Built-in documentation table */
static const struct { const char* name; const char* doc; } g_builtin_docs[] = {
    /* Core forms */
    {"define", "(define name value) or (define (name args) body) - Define a binding"},
    {"lambda", "(lambda [args] body) - Create anonymous function"},
    {"fn", "(fn [args] body) - Create anonymous function (alias for lambda)"},
    {"let", "(let [name value ...] body) - Local bindings"},
    {"let*", "(let* [name value ...] body) - Sequential local bindings"},
    {"if", "(if cond then else) - Conditional expression"},
    {"cond", "(cond (test expr) ... (else expr)) - Multi-way conditional"},
    {"match", "(match val pattern expr ...) - Pattern matching"},
    {"do", "(do expr ...) - Sequence expressions, return last"},
    {"quote", "(quote expr) or 'expr - Return unevaluated expression"},

    /* List operations */
    {"cons", "(cons a b) - Create pair from two values"},
    {"car", "(car pair) - Get first element of pair"},
    {"cdr", "(cdr pair) - Get rest of pair"},
    {"list", "(list a b ...) - Create list from values"},
    {"append", "(append list1 list2) - Concatenate lists"},
    {"reverse", "(reverse list) - Reverse a list"},
    {"length", "(length coll) - Get collection length"},

    /* Higher-order functions */
    {"map", "(map f coll) - Apply f to each element, return new collection"},
    {"filter", "(filter pred coll) - Keep elements where pred is true"},
    {"reduce", "(reduce f init coll) - Fold collection with f"},
    {"fold", "(fold f init coll) - Alias for reduce"},
    {"for-each", "(for-each f coll) - Apply f to each element for side effects"},

    /* Arithmetic */
    {"+", "(+ a b ...) - Add numbers"},
    {"-", "(- a b ...) - Subtract numbers"},
    {"*", "(* a b ...) - Multiply numbers"},
    {"/", "(/ a b ...) - Divide numbers"},
    {"mod", "(mod a b) - Remainder after division"},
    {"abs", "(abs n) - Absolute value"},
    {"min", "(min a b ...) - Minimum value"},
    {"max", "(max a b ...) - Maximum value"},
    {"sqrt", "(sqrt n) - Square root"},
    {"pow", "(pow base exp) - Exponentiation"},

    /* Comparison */
    {"=", "(= a b) - Test equality"},
    {"<", "(< a b) - Less than"},
    {">", "(> a b) - Greater than"},
    {"<=", "(<= a b) - Less than or equal"},
    {">=", "(>= a b) - Greater than or equal"},

    /* Predicates */
    {"null?", "(null? x) - Test if x is null/empty"},
    {"pair?", "(pair? x) - Test if x is a pair"},
    {"list?", "(list? x) - Test if x is a proper list"},
    {"number?", "(number? x) - Test if x is a number"},
    {"string?", "(string? x) - Test if x is a string"},
    {"symbol?", "(symbol? x) - Test if x is a symbol"},

    /* I/O */
    {"display", "(display x) - Print x without newline"},
    {"print", "(print x) - Print x with newline"},
    {"newline", "(newline) - Print newline"},
    {"read-file", "(read-file path) - Read file as string"},
    {"write-file", "(write-file path content) - Write string to file"},

    /* Introspection */
    {"inspect", "(inspect obj) - Show detailed object info"},
    {"type-of", "(type-of obj) - Get type as keyword"},
    {"doc", "(doc 'symbol) - Show documentation for symbol"},
    {"source", "(source 'symbol) - Show source code for function"},
    {"address-of", "(address-of obj) - Get memory address as int"},
    {"refcount-of", "(refcount-of obj) - Get reference count"},

    /* Collections */
    {"sort", "(sort coll) - Sort collection"},
    {"group-by", "(group-by f coll) - Group elements by key function"},
    {"zip", "(zip coll1 coll2) - Pair elements from two collections"},
    {"take", "(take n coll) - Take first n elements"},
    {"drop", "(drop n coll) - Drop first n elements"},
    {"flatten", "(flatten coll) - Flatten nested collection one level"},
    {"distinct", "(distinct coll) - Remove duplicates"},

    /* JSON */
    {"json-parse", "(json-parse str) - Parse JSON string to data"},
    {"json-stringify", "(json-stringify data) - Convert data to JSON string"},

    /* Random */
    {"random", "(random) - Random float in [0, 1)"},
    {"random-int", "(random-int n) - Random integer in [0, n)"},

    /* String operations */
    {"string-length", "(string-length s) - Get string length"},
    {"string-split", "(string-split delim s) - Split string by delimiter"},
    {"string-join", "(string-join delim strs) - Join strings with delimiter"},
    {"string-trim", "(string-trim s) - Remove leading/trailing whitespace"},
    {"string-upcase", "(string-upcase s) - Convert to uppercase"},
    {"string-downcase", "(string-downcase s) - Convert to lowercase"},
    {"string-starts-with", "(string-starts-with s prefix) - Check if string starts with prefix"},
    {"string-ends-with", "(string-ends-with s suffix) - Check if string ends with suffix"},

    /* Concurrency */
    {"atom", "(atom val) - Create atomic reference"},
    {"deref", "(deref atom) - Get value from atom"},
    {"reset!", "(reset! atom val) - Set atom to new value"},
    {"swap!", "(swap! atom f) - Apply f to atom value atomically"},
    /* DIRECTIVE: NO CHANNELS - channel, send, recv help removed */

    /* Pipe operator */
    {"|>", "(|> val f1 f2 ...) - Pipe value through functions"},

    {NULL, NULL}
};

/*
 * prim_doc - Documentation lookup
 *
 * Returns documentation string for a symbol.
 * Accepts a symbol or string argument.
 */
Obj* prim_doc(Obj* sym) {
    const char* name = NULL;

    /* Accept symbol or string */
    if (IS_BOXED(sym) && sym->tag == TAG_SYM) {
        name = (const char*)sym->ptr;
    } else if (IS_BOXED(sym) && sym->tag == TAG_STRING) {
        name = (const char*)sym->ptr;
    }

    if (!name) {
        return mk_string("Usage: (doc 'symbol) - Show documentation for symbol");
    }

    /* Search built-in docs */
// REVIEWED:NAIVE
    for (int i = 0; g_builtin_docs[i].name; i++) {
        if (strcmp(g_builtin_docs[i].name, name) == 0) {
            return mk_string(g_builtin_docs[i].doc);
        }
    }

    return mk_string("No documentation available. Use (inspect obj) for runtime info.");
}

/*
 * prim_source - Source code display
 *
 * Returns source code for a function if available.
 * For closures, returns the source_text field if set.
 * For symbols, looks up the value in the current environment.
 */
Obj* prim_source(Obj* obj) {
    /* If symbol, look it up in the current environment */
    if (IS_BOXED(obj) && obj->tag == TAG_SYM) {
        const char* name = (const char*)obj->ptr;
        Obj* value = env_lookup_current(name);

        if (!value) {
            char buf[256];
            snprintf(buf, sizeof(buf), "Symbol '%s' not found in current environment", name);
            return mk_string(buf);
        }

        /* Recurse with the looked-up value */
        return prim_source(value);
    }

    /* If closure, get source */
    if (IS_BOXED(obj) && obj->tag == TAG_CLOSURE) {
        Closure* c = (Closure*)obj->ptr;
        if (c && c->source_text) {
            return mk_string(c->source_text);
        }

        /* No source, describe the closure */
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "(fn [%d arg%s] <compiled code>)",
                 c ? c->arity : 0, (c && c->arity == 1) ? "" : "s");
        return mk_string(buf);
    }

    return mk_string("Not a function");
}

// TESTED - tests/test_env_get.lisp
/*
 * prim_env_get - Look up a symbol in the current environment
 *
 * Args: symbol (the name to look up)
 * Returns: the bound value or nil if not found
 *
 * Example:
 *   (def x 42)
 *   (env-get 'x)  ; => 42
 */
Obj* prim_env_get(Obj* sym) {
    if (!sym || !IS_BOXED(sym)) return NULL;

    const char* name = NULL;
    if (sym->tag == TAG_SYM) {
        name = (const char*)sym->ptr;
    } else if (sym->tag == TAG_STRING) {
        name = (const char*)sym->ptr;
    }

    if (!name) return NULL;

    return env_lookup_current(name);
}

/*
 * prim_env_bindings - Get all bindings in the current environment
 *
 * Returns: a list of (symbol . value) pairs representing all bindings
 *
 * Example:
 *   (def x 1)
 *   (def y 2)
 *   (env-bindings)  ; => ((y . 2) (x . 1))
 */
Obj* prim_env_bindings(void) {
    return cont_get_env();
}
