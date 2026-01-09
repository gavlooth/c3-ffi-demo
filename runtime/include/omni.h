/*
 * OmniLisp Runtime - Public API
 *
 * This header defines the public interface for the OmniLisp runtime.
 * All user-generated code includes this header and links against libomni.a
 */

#ifndef OMNI_RUNTIME_H
#define OMNI_RUNTIME_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Forward Declarations ========== */

typedef struct Obj Obj;
typedef struct Closure Closure;
typedef struct Arena Arena;
typedef struct Region Region;

/* Forward declaration for internal GenObj (legacy) */
struct GenObj;

/* ========== Type ID Constants (Phase 24: Region-Level Metadata) ========== */

/*
 * TypeID - Compile-time type identifier constants
 *
 * These values enable the compiler and runtime to use optimized allocation
 * strategies based on type-specific metadata (size, pointer fields, inline
 * capability, etc.).
 *
 * Aligned with TypeID enum in runtime/src/memory/region_metadata.h
 */
#ifndef OMNI_TYPE_ID_DEFINED
#define OMNI_TYPE_ID_DEFINED
typedef enum {
    TYPE_ID_INT = 0,
    TYPE_ID_FLOAT,
    TYPE_ID_CHAR,
    TYPE_ID_PAIR,      /* Cons cell */
    TYPE_ID_ARRAY,
    TYPE_ID_STRING,
    TYPE_ID_SYMBOL,
    TYPE_ID_DICT,
    TYPE_ID_CLOSURE,
    TYPE_ID_BOX,
    TYPE_ID_CHANNEL,
    TYPE_ID_THREAD,
    TYPE_ID_ERROR,
    TYPE_ID_ATOM,
    TYPE_ID_TUPLE,
    TYPE_ID_NAMED_TUPLE,
    TYPE_ID_GENERIC,
    TYPE_ID_KIND,
    TYPE_ID_NOTHING,
    TYPE_ID_MAX
} TypeID;
#endif

/* Forward declaration for Generation type (used by BorrowRef).
 * Actual definition is later, conditional on IPGE_ROBUST_MODE. */
#ifndef IPGE_ROBUST_MODE
#define IPGE_ROBUST_MODE 0
#endif
#if IPGE_ROBUST_MODE
typedef uint64_t Generation;
#else
typedef uint16_t Generation;
#endif

/* BorrowRef: Heap-allocated borrowed reference for legacy API compatibility.
 * For new code, prefer using the packed BorrowedRef type instead. */
typedef struct BorrowRef {
    struct GenObj* target;       /* Legacy GenObj system (internal use) */
    Generation remembered_gen;   /* Snapshot of generation at borrow time */
    const char* source_desc;     /* Debug description */
    struct Obj* ipge_target;     /* IPGE: Direct Obj* for generation check */
} BorrowRef;

/* Closure function signature */
typedef Obj* (*ClosureFn)(Obj** captures, Obj** args, int argc);

/* ========== Closure and Generic Structures ========== */

/*
 * Closure - Single-dispatch function (traditional Lisp)
 * Stored in an Obj with tag=TAG_CLOSURE, using the obj->a field for the closure pointer.
 */
typedef struct Closure {
    ClosureFn fn;           /* Function pointer */
    Obj** captures;         /* Captured variables (boxed array) */
    int capture_count;      /* Number of captures */
    int arity;              /* Expected number of arguments */
    const char* name;      /* Function name (for debugging) */
} Closure;

/*
 * MethodInfo - A single method in a generic function's method table.
 * Methods are specialized for specific argument types.
 */
typedef struct MethodInfo {
    Obj** param_kinds;     /* Array of Kind objects for each parameter */
    int param_count;        /* Number of parameters */
    ClosureFn impl;         /* Method implementation */
    int specificity;        /* Specificity score (higher = more specific) */
    struct MethodInfo* next; /* Next method in the table */
} MethodInfo;

/*
 * Generic - Multi-dispatch generic function.
 * Contains a method table sorted by specificity.
 * Stored in an Obj with tag=TAG_GENERIC, using the obj->a field for the generic pointer.
 */
typedef struct Generic {
    const char* name;       /* Generic function name */
    MethodInfo* methods;    /* Method table (sorted by specificity) */
    int method_count;       /* Total number of methods */
} Generic;

/*
 * Kind - Type object representing a primitive or parametric type.
 * Stored in an Obj with tag=TAG_KIND, using the obj->ptr field (or a) for the Kind*.
 */
typedef struct Kind {
    char* name;            /* Type name (e.g., "Int", "List", "Pair") */
    Obj** params;          /* Type parameters (for parametric types) */
    int param_count;       /* Number of parameters */
} Kind;

/* ========== Object Tags ========== */

typedef enum {
    TAG_INT = 1,
    TAG_FLOAT,
    TAG_CHAR,
    TAG_PAIR,
    TAG_SYM,
    TAG_BOX,
    TAG_CLOSURE,
    TAG_CHANNEL,
    TAG_ERROR,
    TAG_ATOM,
    TAG_THREAD,
    TAG_ARRAY,
    TAG_DICT,
    TAG_STRING,
    TAG_KEYWORD,
    TAG_TUPLE,
    TAG_NAMED_TUPLE,
    TAG_GENERIC,
    TAG_KIND,
    TAG_NOTHING
} ObjTag;

#define TAG_USER_BASE 1000

/* ========== Tagged Pointers (Multi-Type Immediates) ========== */
/*
 * 3-bit tag scheme for immediate values (no heap allocation):
 *
 * Low 3 bits | Type        | Payload
 * -----------|-------------|------------------
 *    000     | Heap ptr    | 64-bit pointer (aligned)
 *    001     | Integer     | 61-bit signed int
 *    010     | Character   | 21-bit Unicode codepoint
 *    011     | Boolean     | 1-bit (0=false, 1=true)
 *    1xx     | Reserved    | Future use
 *
 * This eliminates heap allocation for primitives entirely.
 * Performance: O(1) allocation, O(1) free, 8 bytes instead of 48.
 */

/* 3-bit tag constants */
#define IMM_TAG_MASK     0x7ULL
#define IMM_TAG_PTR      0x0ULL   /* Heap pointer (must be 8-byte aligned) */
#define IMM_TAG_INT      0x1ULL   /* Immediate integer */
#define IMM_TAG_CHAR     0x2ULL   /* Immediate character */
#define IMM_TAG_BOOL     0x3ULL   /* Immediate boolean */

/* Extract tag from value */
#define GET_IMM_TAG(p)   (((uintptr_t)(p)) & IMM_TAG_MASK)

/* Check immediate types */
#define IS_IMMEDIATE(p)      (GET_IMM_TAG(p) != IMM_TAG_PTR)
#define IS_IMMEDIATE_INT(p)  (GET_IMM_TAG(p) == IMM_TAG_INT)
#define IS_IMMEDIATE_CHAR(p) (GET_IMM_TAG(p) == IMM_TAG_CHAR)
#define IS_IMMEDIATE_BOOL(p) (GET_IMM_TAG(p) == IMM_TAG_BOOL)
#define IS_BOXED(p)          (GET_IMM_TAG(p) == IMM_TAG_PTR && (p) != NULL)

/* ---- Immediate Integers ---- */
#define IMM_INT_MAX      ((1LL << 60) - 1)
#define IMM_INT_MIN      (-(1LL << 60))

#define MAKE_INT_IMM(n)      ((Obj*)(((uintptr_t)(n) << 3) | IMM_TAG_INT))
#define INT_IMM_VALUE(p)     ((long)((intptr_t)(p) >> 3))

/* Backward compatibility */
#define MAKE_IMMEDIATE(n)    MAKE_INT_IMM(n)
#define IMMEDIATE_VALUE(p)   INT_IMM_VALUE(p)

/* Unboxed integer constructor */
static inline Obj* mk_int_unboxed(long i) {
    return MAKE_INT_IMM(i);
}

/* ---- Immediate Booleans ---- */
#define OMNI_FALSE         ((Obj*)(((uintptr_t)0 << 3) | IMM_TAG_BOOL))
#define OMNI_TRUE          ((Obj*)(((uintptr_t)1 << 3) | IMM_TAG_BOOL))

#define IS_FALSE(p)          ((p) == OMNI_FALSE)
#define IS_TRUE(p)           ((p) == OMNI_TRUE)

/* Unboxed boolean constructor */
static inline Obj* mk_bool(int b) {
    return b ? OMNI_TRUE : OMNI_FALSE;
}

/* ---- Immediate Characters ---- */
#define MAKE_CHAR_IMM(c)     ((Obj*)(((uintptr_t)(c) << 3) | IMM_TAG_CHAR))
#define CHAR_IMM_VALUE(p)    ((long)(((uintptr_t)(p)) >> 3))

/* Unboxed character constructor (Unicode codepoints 0-0x10FFFF) */
static inline Obj* mk_char_unboxed(long c) {
    return MAKE_CHAR_IMM(c);
}

/* Note: obj_to_char and IPGE functions defined after Obj struct */

/* ========== IPGE: In-Place Generational Evolution ========== */
/*
 * Memory safety via deterministic generation IDs.
 * Uses a full-period LCG to evolve generation on each alloc/free.
 * No indirection table - generation stored inline in object.
 *
 * Reference: RFC by Christos Chatzifountas
 *
 * Two modes available:
 * 1. COMPACT (default): 16-bit generation packed with 48-bit pointer = 64-bit ref
 * 2. ROBUST: 64-bit generation + 64-bit pointer = 128-bit ref (more collision resistant)
 *
 * Set IPGE_ROBUST_MODE=1 to use 128-bit references (slower but more resistant to
 * pathological allocation patterns with >65K reuses of same memory address).
 */

/* IPGE_ROBUST_MODE is defined earlier (near BorrowRef) */

#if IPGE_ROBUST_MODE
/* ---- ROBUST MODE: 64-bit generation (18 quintillion cycles) ---- */
#define IPGE_MULTIPLIER  0x5851f42D4C957F2DULL  /* Knuth MMIX */
#define IPGE_INCREMENT   0x1442695040888963ULL  /* Must be odd */

/* Generation typedef is at top of file */

static inline Generation ipge_evolve(Generation gen) {
    return (gen * IPGE_MULTIPLIER) + IPGE_INCREMENT;
}

/* Borrowed reference - 128 bits (robust, slower) */
typedef struct {
    Obj* ptr;
    Generation gen;
} BorrowedRef;

#define BORROW_REF_PACK(ptr, gen)    ((BorrowedRef){ (ptr), (gen) })
#define BORROW_REF_PTR(ref)          ((ref).ptr)
#define BORROW_REF_GEN(ref)          ((ref).gen)

#else
/* ---- COMPACT MODE: 16-bit generation (65K cycles per slot) ---- */
/*
 * Packed format: [gen:16][ptr:48] in a single uint64_t
 * x86-64 uses 48-bit addresses, upper 16 bits available for generation.
 * Random seed + bijective evolution = thread-safe, no collisions for 65K reuses.
 */
#define IPGE16_MULTIPLIER  0xAC4BULL  /* Odd = bijection over Z/2^16 */
#define IPGE16_INCREMENT   0x9E37ULL  /* Odd increment */

/* Generation typedef is at top of file */

static inline Generation ipge_evolve(Generation gen) {
    return (Generation)((gen * IPGE16_MULTIPLIER) + IPGE16_INCREMENT);
}

/* Borrowed reference - 64 bits (compact, fast) */
typedef uint64_t BorrowedRef;

#define BORROW_REF_PACK(ptr, gen) \
    (((uint64_t)(gen) << 48) | ((uint64_t)(uintptr_t)(ptr) & 0xFFFFFFFFFFFFULL))
#define BORROW_REF_PTR(ref) \
    ((Obj*)((uintptr_t)((ref) & 0xFFFFFFFFFFFFULL)))
#define BORROW_REF_GEN(ref) \
    ((Generation)((ref) >> 48))

#endif /* IPGE_ROBUST_MODE */

/* Legacy 64-bit evolution for internal use */
#define IPGE64_MULTIPLIER  0x5851f42D4C957F2DULL
#define IPGE64_INCREMENT   0x1442695040888963ULL

static inline uint64_t ipge_evolve64(uint64_t gen) {
    return (gen * IPGE64_MULTIPLIER) + IPGE64_INCREMENT;
}

/* ========== Core Object Type ========== */

struct Obj {
    Generation generation;  /* IPGE generation ID for memory safety */
    int mark;               /* Reference count or mark bit */
    int tag;                /* ObjTag */
    int is_pair;            /* 1 if pair, 0 if not */
    int scc_id;             /* SCC identifier for cycle detection (-1 = none) */
    unsigned int scan_tag : 31;  /* Scanner mark (separate from RC) */
    unsigned int tethered : 1;   /* Scope tethering bit (Vale-style) */
    union {
        long i;
        double f;
        struct { struct Obj *a, *b; };
        void* ptr;
    };
};
/* Size: 32 bytes (compact mode) or 40 bytes (robust mode) */

/* Boolean extraction (only false and nothing are falsy; empty list is truthy) */
static inline int obj_to_bool(Obj* p) {
    if (IS_IMMEDIATE_BOOL(p)) return p == OMNI_TRUE;
    if (p == NULL) return 1;  /* Empty list is truthy */
    if (IS_IMMEDIATE_INT(p) || IS_IMMEDIATE_CHAR(p)) return 1;
    if (p->tag == TAG_NOTHING) return 0;
    if (p->tag == TAG_SYM && p->ptr && strcmp((char*)p->ptr, "false") == 0) return 0;
    return 1;
}

/* ========== Scope Tethering (Vale-style) ========== */
/*
 * When borrowing a reference in a scope, set the tethered bit.
 * While tethered, generation checks can be skipped - the object
 * is guaranteed to stay alive for the scope duration.
 */

/* Tether an object (on borrow) */
static inline void tether_obj(Obj* obj) {
    if (obj && !IS_IMMEDIATE(obj)) {
        obj->tethered = 1;
    }
}

/* Untether an object (on scope exit) */
static inline void untether_obj(Obj* obj) {
    if (obj && !IS_IMMEDIATE(obj)) {
        obj->tethered = 0;
    }
}

/* Fast deref with tether check */
static inline Obj* tethered_deref(Obj* obj, Generation expected_gen) {
    if (!obj) return NULL;
    if (IS_IMMEDIATE(obj)) return obj;
    if (obj->tethered) return obj;  /* Fast path */
    return (obj->generation == expected_gen) ? obj : NULL;
}

/* Tethered reference type */
typedef struct TetheredRef {
    Obj* ptr;
    Generation gen;
} TetheredRef;

/* Create tethered reference */
static inline TetheredRef tether_borrow(Obj* obj) {
    TetheredRef ref = { obj, 0 };
    if (obj && !IS_IMMEDIATE(obj)) {
        ref.gen = obj->generation;
        obj->tethered = 1;
    }
    return ref;
}

/* Release tethered reference */
static inline void tether_release(TetheredRef ref) {
    if (ref.ptr && !IS_IMMEDIATE(ref.ptr)) {
        ref.ptr->tethered = 0;
    }
}

/* ========== Tagged Pointer Inline Functions (need Obj defined) ========== */

/* Safe integer extraction - works for all immediate types */
static inline long obj_to_int(Obj* p) {
    if (p == NULL) return 0;
    if (IS_IMMEDIATE_INT(p)) return INT_IMM_VALUE(p);
    if (IS_IMMEDIATE_BOOL(p)) return p == OMNI_TRUE ? 1 : 0;
    if (IS_IMMEDIATE_CHAR(p)) return CHAR_IMM_VALUE(p);
    if (IS_BOXED(p)) {
        if (p->tag == TAG_INT) return p->i;
        if (p->tag == TAG_FLOAT) return (long)p->f;
    }
    return 0;
}

static inline double obj_to_float(Obj* p) {
    if (p == NULL) return 0.0;
    if (IS_IMMEDIATE_INT(p)) return (double)INT_IMM_VALUE(p);
    if (IS_IMMEDIATE_BOOL(p)) return p == OMNI_TRUE ? 1.0 : 0.0;
    if (IS_IMMEDIATE_CHAR(p)) return (double)CHAR_IMM_VALUE(p);
    if (IS_BOXED(p)) {
        if (p->tag == TAG_FLOAT) return p->f;
        if (p->tag == TAG_INT) return (double)p->i;
    }
    return 0.0;
}

/* Safe tag extraction - works for all immediate types */
static inline int obj_tag(Obj* p) {
    if (p == NULL) return 0;
    if (IS_IMMEDIATE_INT(p)) return TAG_INT;
    if (IS_IMMEDIATE_CHAR(p)) return TAG_CHAR;
    if (IS_IMMEDIATE_BOOL(p)) return TAG_INT;  /* Booleans are int-like */
    return p->tag;
}

/* Check if value is an integer (boxed or immediate) */
static inline int is_int(Obj* p) {
    if (IS_IMMEDIATE_INT(p) || IS_IMMEDIATE_BOOL(p)) return 1;
    return p && IS_BOXED(p) && p->tag == TAG_INT;
}

/* Check if value is a character (boxed or immediate) */
static inline int is_char_val(Obj* p) {
    if (IS_IMMEDIATE_CHAR(p)) return 1;
    return p && IS_BOXED(p) && p->tag == TAG_CHAR;
}

/* Safe inc_ref that handles immediates */
static inline void inc_ref_safe(Obj* x) {
    if (x && !IS_IMMEDIATE(x)) {
        x->mark++;
    }
}

/* Safe dec_ref that handles immediates */
static inline void dec_ref_safe(Obj* x) {
    if (x && !IS_IMMEDIATE(x)) {
        if (--x->mark <= 0) {
            /* Will be handled by free_obj */
        }
    }
}

/* Character extraction (needs Obj defined) */
static inline long obj_to_char(Obj* p) {
    if (IS_IMMEDIATE_CHAR(p)) return CHAR_IMM_VALUE(p);
    if (p && IS_BOXED(p) && p->tag == TAG_CHAR) return p->i;
    return 0;
}

/* ========== IPGE Functions (need Obj defined) ========== */

/* Validate a borrowed reference */
static inline int ipge_valid(Obj* obj, Generation expected_gen) {
    if (!obj || IS_IMMEDIATE(obj)) return 1;  /* Immediates always valid */
    return obj->generation == expected_gen;
}

/* Borrow a reference (snapshot current generation) */
static inline BorrowedRef borrow_ref(Obj* obj) {
#if IPGE_ROBUST_MODE
    BorrowedRef ref = { obj, 0 };
    if (obj && !IS_IMMEDIATE(obj)) {
        ref.gen = obj->generation;
    }
    return ref;
#else
    if (!obj || IS_IMMEDIATE(obj)) return 0;
    return BORROW_REF_PACK(obj, obj->generation);
#endif
}

/* Dereference with validation */
static inline Obj* deref_borrowed(BorrowedRef ref) {
#if IPGE_ROBUST_MODE
    if (!ipge_valid(ref.ptr, ref.gen)) {
        return NULL;  /* Use-after-free detected */
    }
    return ref.ptr;
#else
    Obj* ptr = BORROW_REF_PTR(ref);
    Generation gen = BORROW_REF_GEN(ref);
    if (!ipge_valid(ptr, gen)) {
        return NULL;  /* Use-after-free detected */
    }
    return ptr;
#endif
}

/* ========== Object Constructors ========== */

Obj* mk_int(long i);
Obj* mk_float(double f);
Obj* mk_char(long c);
Obj* mk_pair(Obj* a, Obj* b);
Obj* mk_sym(const char* s);
Obj* mk_string(const char* s);
Obj* mk_box(Obj* v);
Obj* mk_error(const char* msg);
Obj* mk_nothing(void);
Obj* mk_closure(ClosureFn fn, Obj** captures, BorrowRef** refs, int count, int arity);

/* Stack-allocated primitives (optimization for non-escaping values) */
Obj* mk_int_stack(long i);
Obj* mk_float_stack(double f);
Obj* mk_char_stack(long c);

/* ========== Memory Management ========== */

void inc_ref(Obj* x);
void dec_ref(Obj* x);
void free_obj(Obj* x);
void free_tree(Obj* x);
void free_unique(Obj* x);
void flush_freelist(void);

/* Check if object is nil */
int is_nil(Obj* x);
int is_nothing(Obj* x);

/* Check if object is stack-allocated */
int is_stack_obj(Obj* x);

/* ========== Deferred Reference Counting ========== */

void defer_decrement(Obj* obj);
void flush_deferred(void);

/* ========== Box Operations ========== */

Obj* box_get(Obj* b);
void box_set(Obj* b, Obj* v);

/* ========== Pair/List Operations ========== */

Obj* obj_car(Obj* p);
Obj* obj_cdr(Obj* p);
Obj* list_length(Obj* xs);
Obj* list_map(Obj* fn, Obj* xs);
Obj* list_fold(Obj* fn, Obj* init, Obj* xs);
Obj* list_foldr(Obj* fn, Obj* init, Obj* xs);
Obj* list_filter(Obj* fn, Obj* xs);
Obj* list_append(Obj* a, Obj* b);
Obj* list_reverse(Obj* xs);

/* ========== Arithmetic Primitives ========== */

Obj* prim_add(Obj* a, Obj* b);
Obj* prim_sub(Obj* a, Obj* b);
Obj* prim_mul(Obj* a, Obj* b);
Obj* prim_div(Obj* a, Obj* b);
Obj* prim_mod(Obj* a, Obj* b);
Obj* prim_abs(Obj* a);

/* ========== Comparison Primitives ========== */

Obj* prim_lt(Obj* a, Obj* b);
Obj* prim_gt(Obj* a, Obj* b);
Obj* prim_le(Obj* a, Obj* b);
Obj* prim_ge(Obj* a, Obj* b);
Obj* prim_eq(Obj* a, Obj* b);
Obj* prim_not(Obj* a);

/* ========== Type Predicates ========== */

Obj* prim_null(Obj* x);
Obj* prim_pair(Obj* x);
Obj* prim_int(Obj* x);
Obj* prim_float(Obj* x);
Obj* prim_char(Obj* x);
Obj* prim_sym(Obj* x);

/* ========== Type Introspection ========== */

Obj* ctr_tag(Obj* x);
Obj* ctr_arg(Obj* x, Obj* idx);

/* ========== I/O Primitives ========== */

Obj* prim_display(Obj* x);
Obj* prim_print(Obj* x);
Obj* prim_println(Obj* args);
Obj* prim_newline(void);

/* ========== String Primitives ========== */
Obj* prim_str(Obj* value);      /* Convert any value to string */
Obj* prim_strcat(Obj* str1, Obj* str2);  /* Concatenate two strings */

/* ========== Character/String Primitives ========== */

Obj* char_to_int(Obj* c);
Obj* int_to_char(Obj* n);

/* ========== Float Primitives ========== */

Obj* int_to_float(Obj* n);
Obj* float_to_int(Obj* f);
Obj* prim_floor(Obj* f);
Obj* prim_ceil(Obj* f);

/* ========== Higher-Order Functions ========== */

Obj* prim_apply(Obj* fn, Obj* args);
Obj* prim_compose(Obj* f, Obj* g);

/* ========== Closure Operations ========== */

Obj* call_closure(Obj* clos, Obj** args, int argc);

/* ========== Generic Function Operations (Multiple Dispatch) ========== */

/* Create a new generic function object */
Obj* mk_generic(const char* name);

/* Add a method to a generic function. Returns the generic object for chaining. */
Obj* generic_add_method(Obj* generic_obj, Obj** param_kinds, int param_count, ClosureFn impl);

/* Call a generic function with multiple dispatch. Selects the most specific method. */
Obj* call_generic(Obj* generic_obj, Obj** args, int argc);

/* Lookup the most specific method for arguments */
typedef struct MethodInfo MethodInfo;
MethodInfo* omni_generic_lookup(Obj* generic_obj, Obj** args, int argc);

/* Invoke a generic function with arguments */
Obj* omni_generic_invoke(Obj* generic_obj, Obj** args, int argc);

/* Check if a generic function can accept the given number of arguments */
bool omni_check_arity(Obj* generic_obj, int argc);

/* Get the name of a generic function */
const char* omni_generic_name(Obj* generic_obj);

/* Get the number of methods in a generic function */
int omni_generic_method_count(Obj* generic_obj);

/* Compute specificity score for a method (higher = more specific) */
int compute_specificity(Obj** param_kinds, int param_count);

/* Check if a method is more specific than another (for sorting) */
int is_more_specific(Obj* kind_a, Obj* kind_b);

/* ========== Kind (Type) Operations for Parametric Types ========== */

/* Create a Kind object representing a type */
Obj* mk_kind(const char* name, Obj** params, int param_count);

/* Check if two Kinds are equal */
int kind_equals(Obj* a, Obj* b);

/* Check if kind_a is a subtype of kind_b (for specificity calculation) */
int is_subtype(Obj* kind_a, Obj* kind_b);

/* ========== Type System Primitives ========== */

/* Return the type of a value at runtime */
Obj* prim_value_to_type(Obj* value);

/* Check if a value is of a specific type (equivalent to Julia's isa()) */
Obj* prim_type_is(Obj* value, Obj* type_obj);

/* Get Kind objects for basic types */
Obj* prim_kind_int(void);
Obj* prim_kind_string(void);
Obj* prim_kind_array(void);
Obj* prim_kind_list(void);
Obj* prim_kind_pair(void);
Obj* prim_kind_bool(void);
Obj* prim_kind_char(void);
Obj* prim_kind_float(void);
Obj* prim_kind_function(void);
Obj* prim_kind_any(void);
Obj* prim_kind_nothing(void);

/* Global Type Objects for Runtime Type Operations */
extern Obj* o_Int;    /* Int type object */
extern Obj* o_String; /* String type object */
extern Obj* o_Any;    /* Any type object (top type) */
extern Obj* o_Nothing; /* Nothing type object */

/* Initialize global type objects (call before using type objects) */
void omni_init_type_objects(void);

/* Lookup type object by name (for type-based dispatch) */
Obj* omni_lookup_type(const char* name);

/* ========== Pika Grammar Engine Integration ========== */

/*
 * Parse a grammar specification string into a grammar object.
 * The spec string uses the Pika meta-grammar format.
 * Returns a grammar object wrapped in an Obj (TAG_BOX).
 *
 * Note: Only available when compiled with OMNI_PIKA_ENABLED.
 */
Obj* prim_pika_parse_grammar(Obj* spec_str);

/*
 * Match a string against a grammar rule.
 * Args:
 *   - grammar_obj: Grammar object from prim_pika_parse_grammar
 *   - rule_name: Symbol naming the rule to match
 *   - input_str: String to parse
 * Returns: List of match results (each as a pair of (start . matched-text))
 *          or nil if no matches.
 *
 * Note: Only available when compiled with OMNI_PIKA_ENABLED.
 */
Obj* prim_pika_match(Obj* grammar_obj, Obj* rule_name, Obj* input_str);

/* ========== Deep Path Mutation ========== */

/*
 * Mutate a nested field path in a data structure.
 * Args:
 *   - root: The root object (dict/record)
 *   - path_str: Dotted path string (e.g., "user.address.city")
 *   - new_value: The value to set at the leaf
 * Returns: The modified root object
 *
 * Note: This is a simplified implementation; full support requires
 * proper dict/record representation and get/set operations.
 */
Obj* prim_deep_put(Obj* root, const char* path_str, Obj* new_value);

/* ========== Pattern Matching (T-wire-pika-exec-04) ========== */

/*
 * prim_match_pattern: Runtime pattern matching
 *
 * Matches a pattern against an input string and returns the matched substring.
 * Supports basic regex-like patterns:
 * - Literal strings: "hello"
 * - Character classes: [a-z], [0-9], [^0-9]
 * - Quantifiers: *, +, ?
 * - Anchors: ^, $
 *
 * Args:
 *   - input_obj: Input string to match against
 *   - pattern_obj: Pattern string
 *
 * Returns:
 *   - Matched substring as string if pattern matches
 *   - NULL (nil) if no match or invalid input
 *
 * Example:
 *   prim_match_pattern(mk_string("hello world"), mk_string("hello"))
 *   => "hello"
 */
Obj* prim_match_pattern(Obj* input_obj, Obj* pattern_obj);

/* ========== Infinite Iterators ========== */

/*
 * Create an infinite lazy sequence from a function and seed.
 * Args:
 *   - fn: Closure function that takes current value and returns next
 *   - seed: Starting value
 * Returns: Iterator object
 *
 * Example: (iterate inc 0) produces sequence: 0, 1, 2, 3, ...
 */
Obj* prim_iterate(Obj* fn, Obj* seed);

/*
 * Get the next value from an iterator.
 * Args:
 *   - iter_obj: Iterator from prim_iterate
 * Returns: Current value and advances the iterator
 */
Obj* prim_iter_next(Obj* iter_obj);

/*
 * Take n elements from a sequence/iterator.
 * Args:
 *   - n: Number of elements to take
 *   - seq: Iterator or list
 * Returns: List of first n elements
 *
 * For iterators, advances the iterator. For lists, returns first n elements.
 */
Obj* prim_take(long n, Obj* seq);

/* ========== Basic Sequence Operations ========== */

/*
 * Get first element of a sequence.
 * Args: seq - A list or pair
 * Returns: First element (car) or NULL if empty
 */
Obj* prim_first(Obj* seq);

/*
 * Get rest of a sequence (tail).
 * Args: seq - A list or pair
 * Returns: Rest of sequence (cdr) or NULL if empty
 */
Obj* prim_rest(Obj* seq);

/*
 * Check if iterator has more elements.
 * Args: iter_obj - Iterator object
 * Returns: Boolean indicating if more elements exist
 */
Obj* prim_has_next(Obj* iter_obj);

/*
 * Collect elements from sequence into collection.
 * Args:
 *   - seq: Iterator or list
 *   - kind: Symbol indicating collection type ('list, 'array, 'string)
 * Returns: Collected elements in specified collection type
 */
Obj* prim_collect(Obj* seq, Obj* kind);

/*
 * Create a range (0 to n-1).
 * Args: n - Upper bound (exclusive)
 * Returns: List of integers from 0 to n-1
 */
Obj* prim_range(long n);

/* ========== Truthiness ========== */

int is_truthy(Obj* x);

/* Arena Allocator */
Arena* arena_create(size_t block_size);
void* arena_alloc(Arena* a, size_t size);
void arena_register_external(Arena* a, void* ptr, void (*cleanup)(void*));
void arena_destroy(Arena* a);
void arena_reset(Arena* a);
Obj* arena_mk_int(Arena* a, long i);
Obj* arena_mk_pair(Arena* a, Obj* car, Obj* cdr);

/* ========== Borrowed References (IPGE-validated) ========== */
/*
 * BorrowRef provides O(1) use-after-free detection for borrowed references.
 * Uses IPGE (In-Place Generational Evolution) for validation, NOT random
 * generation IDs. IPGE uses a full-period LCG guaranteeing zero collisions.
 */

BorrowRef* borrow_create(Obj* obj, const char* source_desc);
void borrow_release(BorrowRef* ref);
Obj* borrow_get(BorrowRef* ref);

/* ========== Concurrency: Atomic Operations ========== */

#ifdef __STDC_NO_ATOMICS__
/* Fallback for systems without C11 atomics - unsafe/mutex needed */
/* Note: Ideally we'd use a mutex here, but for header simplicity we assume external sync or GCC builtins */
#define ATOMIC_INC_REF(o) inc_ref(o)
#define ATOMIC_DEC_REF(o) dec_ref(o)
#else
/* Using __atomic builtins for GCC/Clang compatibility */
#define ATOMIC_INC_REF(o) do { \
    if ((o)) __atomic_add_fetch(&(o)->mark, 1, __ATOMIC_SEQ_CST); \
} while(0)

#define ATOMIC_DEC_REF(o) do { \
    if ((o)) { \
        if (__atomic_sub_fetch(&(o)->mark, 1, __ATOMIC_SEQ_CST) <= 0) { \
            free_obj(o); \
        } \
    } \
} while(0)
#endif

/* ========== Concurrency: Channels ========== */

Obj* make_channel(int capacity);
int channel_send(Obj* ch, Obj* val);
Obj* channel_recv(Obj* ch);
void channel_close(Obj* ch);
static inline Obj* channel_create(int buffered) { return make_channel(buffered); }

/* ========== Concurrency: Atoms ========== */

Obj* make_atom(Obj* initial);
Obj* atom_deref(Obj* atom);
Obj* atom_reset(Obj* atom, Obj* newval);
Obj* atom_swap(Obj* atom, Obj* fn);
Obj* atom_cas(Obj* atom, Obj* expected, Obj* newval);
static inline Obj* atom_create(Obj* initial) { return make_atom(initial); }
static inline Obj* atom_compare_and_set(Obj* atom, Obj* expected, Obj* newval) {
    return atom_cas(atom, expected, newval);
}

/* ========== Concurrency: Threads ========== */

Obj* spawn_thread(Obj* closure);
Obj* thread_join(Obj* thread);
static inline Obj* thread_create(Obj* closure) { return spawn_thread(closure); }

/* ========== Safe Points ========== */

void safe_point(void);

/* ========== Stack Pool ========== */

extern Obj STACK_POOL[];
extern int STACK_PTR;
#define STACK_POOL_SIZE 256

/* ========== Region-RC: Region-based memory management ========== */

struct Region;

// Lifecycle
struct Region* region_create(void);
void region_destroy_if_dead(struct Region* r);

// Scope Management
void region_exit(struct Region* r);

// RC Management (Internal - use RegionRef for high level)
void region_retain_internal(struct Region* r);
void region_release_internal(struct Region* r);

// Tethering
void region_tether_start(struct Region* r);
void region_tether_end(struct Region* r);

// Allocation (region_alloc is static inline in region_core.h)
void* transmigrate(void* root, struct Region* src_region, struct Region* dest_region);

/* Phase 24: Type-metadata-based allocation */
/*
 * alloc_obj_typed - Allocate an Obj using compile-time type_id
 *
 * This function uses region-level type metadata to determine the optimal
 * allocation strategy:
 * - Inline buffer: For small objects (when type->can_inline and space available)
 * - Arena allocator: For larger objects
 *
 * The type_id enables compile-time optimization (no per-object tag overhead)
 * and centralized metadata per region.
 *
 * Args:
 *   r: Region to allocate in (must be valid)
 *   type_id: Compile-time TypeID constant (e.g., TYPE_ID_INT, TYPE_ID_PAIR)
 *
 * Returns:
 *   Pointer to allocated Obj, or NULL on failure
 *
 * Example:
 *   Obj* int_obj = alloc_obj_typed(region, TYPE_ID_INT);
 *   int_obj->i = 42;
 */
Obj* alloc_obj_typed(struct Region* r, TypeID type_id);

/* Region-aware constructors */
Obj* mk_int_region(struct Region* r, long i);
Obj* mk_char_region(struct Region* r, long codepoint);
Obj* mk_float_region(struct Region* r, double f);
Obj* mk_sym_region(struct Region* r, const char* s);
Obj* mk_cell_region(struct Region* r, Obj* car, Obj* cdr);
Obj* mk_box_region(struct Region* r, Obj* initial);
Obj* mk_error_region(struct Region* r, const char* msg);

// Collection constructors
Obj* mk_array_region(struct Region* r, int capacity);
Obj* mk_dict_region(struct Region* r);
Obj* mk_keyword_region(struct Region* r, const char* name);
Obj* mk_tuple_region(struct Region* r, Obj** items, int count);
Obj* mk_named_tuple_region(struct Region* r, Obj** keys, Obj** values, int count);
Obj* mk_generic_region(struct Region* r, const char* name);
Obj* mk_kind_region(struct Region* r, const char* name, Obj** params, int param_count);

Obj* mk_array(int capacity);
void array_push(Obj* arr, Obj* val);
Obj* array_get(Obj* arr, int idx);
void array_set(Obj* arr, int idx, Obj* val);
int array_length(Obj* arr);

Obj* mk_dict(void);
void dict_set(Obj* dict, Obj* key, Obj* val);
Obj* dict_get(Obj* dict, Obj* key);

Obj* mk_keyword(const char* name);

Obj* mk_tuple(Obj** items, int count);
Obj* tuple_get(Obj* tup, int idx);
int tuple_length(Obj* tup);

Obj* mk_named_tuple(Obj** keys, Obj** values, int count);
Obj* named_tuple_get(Obj* tup, Obj* key);

Obj* mk_generic(const char* name);

/* ========== Global Region (for legacy/shim support) ========== */

/* Ensure the global region exists (called automatically by constructors) */
void omni_ensure_global_region(void);

/* Get the global region (for advanced use) */
struct Region* omni_get_global_region(void);

/* ========== Regex Primitives (Pika-ready API) ========== */

/*
 * Match first occurrence of pattern in input.
 * Args: pattern (string), input (string)
 * Returns: Matched substring or NULL if no match
 */
Obj* prim_re_match(Obj* pattern_obj, Obj* input_obj);

/*
 * Find all non-overlapping matches.
 * Args: pattern (string), input (string)
 * Returns: List of matched substrings
 */
Obj* prim_re_find_all(Obj* pattern_obj, Obj* input_obj);

/*
 * Split string by pattern delimiter.
 * Args: pattern (string), input (string)
 * Returns: List of substrings
 */
Obj* prim_re_split(Obj* pattern_obj, Obj* input_obj);

/*
 * Replace all occurrences of pattern with replacement.
 * Args: pattern (string), replacement (string), input (string), global (boolean)
 * Returns: Modified string
 */
Obj* prim_re_replace(Obj* pattern_obj, Obj* replacement_obj, Obj* input_obj, Obj* global_obj);

/*
 * Check if pattern matches entire string.
 * Args: pattern (string), input (string)
 * Returns: Boolean
 */
Obj* prim_re_fullmatch(Obj* pattern_obj, Obj* input_obj);

/* ========== String Utility Primitives ========== */

/* String length */
Obj* prim_string_length(Obj* str_obj);

/* Split string by delimiter */
Obj* prim_string_split(Obj* delim_obj, Obj* str_obj);

/* Join list of strings with delimiter */
Obj* prim_string_join(Obj* delim_obj, Obj* list_obj);

/* Replace substring with another */
Obj* prim_string_replace(Obj* old_obj, Obj* new_obj, Obj* str_obj);

/* Trim whitespace from both ends */
Obj* prim_string_trim(Obj* str_obj);
Obj* prim_string_trim_left(Obj* str_obj);
Obj* prim_string_trim_right(Obj* str_obj);

/* Convert to uppercase/lowercase */
Obj* prim_string_upcase(Obj* str_obj);
Obj* prim_string_lowcase(Obj* str_obj);

/* Concatenate two strings */
Obj* prim_string_concat(Obj* str1_obj, Obj* str2_obj);

/* Get substring */
Obj* prim_string_substr(Obj* str_obj, Obj* start_obj, Obj* length_obj);

/* Check if string contains substring */
Obj* prim_string_contains(Obj* str_obj, Obj* substr_obj);

/* Find index of substring (returns -1 if not found) */
Obj* prim_string_index_of(Obj* str_obj, Obj* substr_obj);

/* Compare strings for equality */
Obj* prim_string_equals(Obj* str1_obj, Obj* str2_obj);

/* Compare strings (returns <0, 0, >0) */
Obj* prim_string_compare(Obj* str1_obj, Obj* str2_obj);

/* ========== Phase 19: Flow Constructors (Type Algebra) ========== */

/*
 * prim_union: Create a union type from a list of types
 * Args: types_list - List of type objects (Kind objects or symbols)
 * Returns: A union Kind object representing the union of all input types
 *
 * Example: (union [{Int32} {String}]) -> {Union Int32 String}
 */
Obj* prim_union(Obj* types_list);

/*
 * prim_fn: Create a function type (Kind) from parameter types and return type
 * Args: params_and_ret - Pair containing (param_types . return_type)
 * Returns: A function Kind object
 *
 * Example: (fn ({Int32} {Int32}) . {Int32}) -> {Int32 Int32 -> Int32}
 */
Obj* prim_fn(Obj* params_and_ret);

/* ========== Phase 18: Math and Numerics Library ========== */

/* Basic Arithmetic */
Obj* prim_add(Obj* a, Obj* b);
Obj* prim_sub(Obj* a, Obj* b);
Obj* prim_mul(Obj* a, Obj* b);
Obj* prim_div(Obj* a, Obj* b);
Obj* prim_mod(Obj* a, Obj* b);
Obj* prim_pow(Obj* base, Obj* exp);

/* Trigonometric Functions */
Obj* prim_sin(Obj* x);
Obj* prim_cos(Obj* x);
Obj* prim_tan(Obj* x);
Obj* prim_asin(Obj* x);
Obj* prim_acos(Obj* x);
Obj* prim_atan(Obj* x);
Obj* prim_atan2(Obj* y, Obj* x);

/* Hyperbolic Functions */
Obj* prim_sinh(Obj* x);
Obj* prim_cosh(Obj* x);
Obj* prim_tanh(Obj* x);

/* Exponential/Logarithmic Functions */
Obj* prim_exp(Obj* x);
Obj* prim_log(Obj* x);
Obj* prim_log10(Obj* x);
Obj* prim_log2(Obj* x);
Obj* prim_sqrt(Obj* x);

/* Rounding Functions */
Obj* prim_floor(Obj* x);
Obj* prim_ceil(Obj* x);
Obj* prim_round(Obj* x);
Obj* prim_trunc(Obj* x);

/* Math Constants */
Obj* prim_pi(void);
Obj* prim_e(void);
Obj* prim_inf(void);
Obj* prim_nan(void);

/* Comparison Functions */
Obj* prim_min(Obj* a, Obj* b);
Obj* prim_max(Obj* a, Obj* b);
Obj* prim_clamp(Obj* x, Obj* min_val, Obj* max_val);

/* Bitwise Operations */
Obj* prim_band(Obj* a, Obj* b);
Obj* prim_bor(Obj* a, Obj* b);
Obj* prim_bxor(Obj* a, Obj* b);
Obj* prim_bnot(Obj* x);
Obj* prim_lshift(Obj* x, Obj* n);
Obj* prim_rshift(Obj* x, Obj* n);

/* Numeric Predicates */
Obj* prim_is_nan(Obj* x);
Obj* prim_is_inf(Obj* x);
Obj* prim_is_finite(Obj* x);

/* Absolute Value */
Obj* prim_abs(Obj* x);

/* Sign Function */
Obj* prim_signum(Obj* x);

/* GCD and LCM */
Obj* prim_gcd(Obj* a, Obj* b);
Obj* prim_lcm(Obj* a, Obj* b);

/* ========== Phase 23: Trampoline for Stack-Safe Recursion ========== */

/* Check if value is a bounce (thunk) */
Obj* prim_is_bounce(Obj* obj);

/* Create a bounce (thunk) for delayed computation */
Obj* prim_bounce(Obj* func, Obj* arg);

/* Execute thunks until final result */
Obj* prim_trampoline(Obj* initial);

/* Example: Factorial using trampoline */
Obj* prim_fact_trampoline(Obj* n);

/* Example: Even/Odd using trampoline (mutual recursion) */
Obj* prim_even_trampoline(Obj* n);
Obj* prim_odd_trampoline(Obj* n);

/* ========== Phase 22: Pipe Operator and Syntax Refinement ========== */

/* Pipe operator for function chaining */
Obj* prim_pipe(Obj* value, Obj* func);
Obj* prim_pipe_many(Obj* value, Obj* functions);

/* Function composition */
Obj* prim_compose(Obj* f, Obj* g);
Obj* prim_compose_many(Obj* functions);

/* Leading dot field access */
Obj* prim_dot_field(Obj* field_name, Obj* obj);
Obj* prim_dot_field_chain(Obj* obj, Obj* field_names);

/* Method chaining */
Obj* prim_method_chain(Obj* obj, Obj* method_calls);

/* Flip operator */
Obj* prim_flip(Obj* func);

/* Apply and partial application */
Obj* prim_apply(Obj* func, Obj* args);
Obj* prim_partial(Obj* func, Obj* fixed_args);

/* ========== Phase 22: Module and Import System ========== */

/* Module management */
Obj* prim_module_begin(Obj* name_obj);
Obj* prim_module_end(void);
Obj* prim_module_get(Obj* name_obj);
Obj* prim_module_exports(Obj* name_obj);
Obj* prim_module_list(void);

/* Export and import */
Obj* prim_export(Obj* symbol_name, Obj* value);
Obj* prim_import(Obj* name_obj, Obj* options);
Obj* prim_use(Obj* name_obj);
Obj* prim_require(Obj* name_obj);

/* Qualified name resolution */
Obj* prim_resolve(Obj* qualified_name);

#ifdef __cplusplus
}
#endif

#endif /* OMNI_RUNTIME_H */
