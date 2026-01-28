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
#include <dlfcn.h>     /* FFI: dlopen, dlsym for ccall */

/* OmniLisp runtime uses the third-party arena implementation as its Arena type.
 * Include it here so the public API sees a single canonical Arena definition. */
#include "../../third_party/arena/arena_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== Forward Declarations ========== */
/*
 * Header include discipline:
 * Include omni.h before internal headers if you need Obj/Closure/Region definitions.
 *
 * C99 Compatibility: To avoid typedef redefinition warnings, we use the
 * "forward typedef + struct definition without typedef" pattern.
 * - Forward typedefs are declared here
 * - Struct definitions use plain "struct X { ... };" without typedef suffix
 * - This works because C99 allows forward typedefs to match later struct definitions
 * - Guards prevent redefinition warnings regardless of include order
 */
#ifndef OMNI_OBJ_DECLARED
#define OMNI_OBJ_DECLARED
typedef struct Obj Obj;
#endif

#ifndef OMNI_CLOSURE_DECLARED
#define OMNI_CLOSURE_DECLARED
typedef struct Closure Closure;
#endif

#ifndef OMNI_METHODINFO_DECLARED
#define OMNI_METHODINFO_DECLARED
typedef struct MethodInfo MethodInfo;
#endif

/* Region is part of the public API but its struct layout lives in internal headers.
 * Use a typedef guard so include order (omni.h vs region_core.h) is warning-clean. */
#ifndef OMNI_REGION_TYPEDEF
#define OMNI_REGION_TYPEDEF 1
typedef struct Region Region;
#endif

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
    TYPE_ID_SET,              /* Set data structure (Issue 24) */
    TYPE_ID_RESERVED_15,      /* Was TYPE_ID_NAMED_TUPLE - REMOVED 2026-01-15 */
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

/* Closure function signature (legacy - no region parameter) */
typedef Obj* (*ClosureFn)(Obj** captures, Obj** args, int argc);

/* Region-aware closure function signature (Issue 2 P1: Dynamic closure region support)
 * All new closures should use this signature to participate in Region-RC lifecycle. */
typedef Obj* (*ClosureFnRegion)(struct Region* caller_region, Obj** captures, Obj** args, int argc);

/* ========== Closure and Generic Structures ========== */

/*
 * Closure - Single-dispatch function (traditional Lisp)
 * Stored in an Obj with tag=TAG_CLOSURE, using the obj->a field for the closure pointer.
 *
 * Issue 2 P1: Supports both legacy (non-region) and region-aware function pointers.
 * New closures should set region_aware=1 and use fn_region.
 */
struct Closure {
    union {
        ClosureFn fn;              /* Legacy function pointer (no region) */
        ClosureFnRegion fn_region; /* Region-aware function pointer */
    };
    Obj** captures;         /* Captured variables (boxed array) */
    int capture_count;      /* Number of captures */
    int arity;              /* Expected number of arguments */
    const char* name;       /* Function name (for debugging) */
    const char* source_text; /* Original source code (for REPL introspection) */
    unsigned char region_aware; /* 1 if using fn_region, 0 if using fn */
};

/*
 * MethodInfo - A single method in a generic function's method table.
 * Methods are specialized for specific argument types.
 *
 * Issue 2 P1: Supports both legacy and region-aware method implementations.
 */
struct MethodInfo {
    Obj** param_kinds;     /* Array of Kind objects for each parameter */
    int param_count;        /* Number of parameters */
    union {
        ClosureFn impl;            /* Legacy method implementation */
        ClosureFnRegion impl_region; /* Region-aware method implementation */
    };
    int specificity;        /* Specificity score (higher = more specific) */
    struct MethodInfo* next; /* Next method in the table */
    unsigned char region_aware; /* 1 if using impl_region, 0 if using impl */
};

/*
 * Generic - Multi-dispatch generic function.
 * Contains a method table sorted by specificity.
 * Stored in an Obj with tag=TAG_GENERIC, using the obj->a field for the generic pointer.
 *
 * Optimization: dispatch_cache provides O(1) lookup for previously-resolved type
 * signatures. by_arity provides O(1) arity filtering.
 */
typedef struct Generic {
    const char* name;       /* Generic function name */
    MethodInfo* methods;    /* Method table (sorted by specificity) */
    int method_count;       /* Total number of methods */
    /* Optimization: Dispatch cache (HashMap*) for O(1) cached lookups */
    void* dispatch_cache;   /* type_sig_hash -> MethodInfo* */
    /* Optimization: Methods indexed by arity for O(1) arity filtering */
    MethodInfo** by_arity;  /* Array indexed by arity */
    int max_arity;          /* Maximum arity in by_arity array */
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
    TAG_ERROR,
    TAG_ATOM,
    TAG_THREAD,
    TAG_ARRAY,
    TAG_DICT,
    TAG_STRING,
    TAG_KEYWORD,
    TAG_SET,                  /* Set data structure (Issue 24) */
    TAG_DATETIME,             /* DateTime type (Issue 25) */
    TAG_GENERIC,
    TAG_KIND,
    TAG_NOTHING,
    /* Effect system tags (Phase 22: Algebraic Effects) */
    TAG_EFFECT_TYPE,
    TAG_RESUMPTION,
    TAG_HANDLER,
    /* Condition system tag (Issue 14 P3) */
    TAG_CONDITION,
    /* Generator tag (Issue 14 P4: Iterator-Generator Integration) */
    TAG_GENERATOR,
    /* FFI C pointer tag (Issue 19: FFI Implementation) */
    TAG_CPTR
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
    Region* owner_region;    /* Issue 1 P1: Owning region for region_of(obj) */
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
Obj* mk_closure_region(struct Region* r, ClosureFnRegion fn, Obj** captures, int count, int arity);

/* ========== FFI Type Marshaling (Phase 0: ccall support) ========== */

/*
 * Obj* -> C type conversions for FFI ccall
 * These extract C-native values from OmniLisp Obj* wrappers
 */

/* Extract int from Obj* (immediate or boxed) */
static inline int obj_to_cint(Obj* o) {
    return (int)obj_to_int(o);
}

/* Extract double from Obj* (immediate or boxed) */
static inline double obj_to_cdouble(Obj* o) {
    return obj_to_float(o);
}

/* Extract const char* from Obj* string
 *
 * WARNING: UNSAFE LIFETIME SEMANTICS
 * This function returns a pointer directly into the OmniLisp string object's
 * internal buffer. The pointer is only valid for the lifetime of the Obj*.
 *
 * SAFE USAGE:
 * - Pass to C functions that use the string immediately and do NOT store it
 * - Examples: printf, strlen, strcmp, strchr, strstr, write(), etc.
 *
 * UNSAFE USAGE (will cause use-after-free):
 * - C functions that store the pointer for later use
 * - C functions that pass the pointer to async callbacks
 * - Storing the returned pointer in global/static variables
 * - Using the pointer after the OmniLisp object may have been GC'd/freed
 *
 * For C functions that need to retain the string, copy it first:
 *   const char* s = obj_to_cstring(o);
 *   char* owned = strdup(s);  // Caller owns this copy
 *
 * Issue 19: This is a known limitation. Future work may add region pinning
 * or reference counting for FFI string lifetimes.
 */
static inline const char* obj_to_cstring(Obj* o) {
    if (!o || IS_IMMEDIATE(o)) return "";
    if (o->tag == TAG_STRING || o->tag == TAG_SYM) {
        return o->ptr ? (const char*)o->ptr : "";
    }
    return "";
}

/* Extract void* from Obj* (pointer stored in ptr field)
 * Only extracts from objects with TAG_CPTR tag for type safety.
 * Returns NULL for non-CPtr objects instead of arbitrary data.
 *
 * Issue 19 Fix: Added type verification to prevent returning
 * arbitrary data from non-CPtr objects.
 */
static inline void* obj_to_cptr(Obj* o) {
    if (!o || IS_IMMEDIATE(o)) return NULL;
    /* Verify object is actually a CPtr before accessing ptr field */
    if (o->tag != TAG_CPTR) {
        return NULL;  /* Type mismatch - not a CPtr */
    }
    return o->ptr;
}

/* Extract size_t from Obj* */
static inline size_t obj_to_csize(Obj* o) {
    return (size_t)obj_to_int(o);
}

/*
 * C type -> Obj* conversions for FFI ccall return values
 */

/* Wrap void* as Obj* (CPtr type)
 * Creates a dedicated CPtr object with TAG_CPTR tag.
 * The pointer is stored in the ptr field and can be retrieved with obj_to_cptr().
 *
 * WARNING: The runtime does NOT track the lifetime of the C pointer.
 * The caller is responsible for ensuring the pointer remains valid.
 */
static inline Obj* mk_cptr(void* p) {
    /* Allocate object and set dedicated TAG_CPTR */
    Obj* o = mk_box(NULL);
    if (o) {
        o->ptr = p;
        o->tag = TAG_CPTR;  /* Dedicated CPtr tag (Issue 19: FFI safety) */
    }
    return o;
}

/*
 * FFI Error Reporting (Issue 19: ccall error handling)
 *
 * Creates an FFI error condition wrapped as an Obj* that can be returned
 * from ccall when dlopen/dlsym fails. This allows Lisp code to detect
 * FFI failures instead of silently receiving NOTHING.
 *
 * Returns: Obj* with TAG_CONDITION containing the FFI error
 */
Obj* mk_ffi_load_error(const char* library, const char* function);

/* ========== Specialization Box/Unbox Functions ========== */
/*
 * Phase 1 Type Specialization: Box/unbox wrappers for specialized codegen.
 * Implementations in primitives_specialized.c
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md
 */

/* Unbox functions - extract primitive C values from Obj* */
int64_t unbox_int(Obj* obj);
double unbox_float(Obj* obj);
char unbox_char(Obj* obj);
bool unbox_bool(Obj* obj);

/* Box functions - create Obj* from primitive C values */
Obj* box_int(int64_t value);
Obj* box_float(double value);
Obj* box_char(char value);
Obj* box_bool(bool value);

/* Specialized arithmetic primitives (Int64) */
int64_t prim_add_Int_Int(int64_t a, int64_t b);
int64_t prim_sub_Int_Int(int64_t a, int64_t b);
int64_t prim_mul_Int_Int(int64_t a, int64_t b);
int64_t prim_div_Int_Int(int64_t a, int64_t b);
int64_t prim_mod_Int_Int(int64_t a, int64_t b);
int64_t prim_negate_Int(int64_t a);
int64_t prim_abs_Int(int64_t a);

/* Specialized arithmetic primitives (Float64) */
double prim_add_Float_Float(double a, double b);
double prim_sub_Float_Float(double a, double b);
double prim_mul_Float_Float(double a, double b);
double prim_div_Float_Float(double a, double b);
double prim_negate_Float(double a);
double prim_abs_Float(double a);

/* Mixed arithmetic (Int64 + Float64) */
double prim_add_Int_Float(int64_t a, double b);
double prim_add_Float_Int(double a, int64_t b);
double prim_sub_Int_Float(int64_t a, double b);
double prim_sub_Float_Int(double a, int64_t b);
double prim_mul_Int_Float(int64_t a, double b);
double prim_mul_Float_Int(double a, int64_t b);
double prim_div_Int_Float(int64_t a, double b);
double prim_div_Float_Int(double a, int64_t b);

/* Specialized comparison primitives (Int64) */
bool prim_lt_Int_Int(int64_t a, int64_t b);
bool prim_gt_Int_Int(int64_t a, int64_t b);
bool prim_le_Int_Int(int64_t a, int64_t b);
bool prim_ge_Int_Int(int64_t a, int64_t b);
bool prim_eq_Int_Int(int64_t a, int64_t b);
bool prim_ne_Int_Int(int64_t a, int64_t b);

/* Specialized comparison primitives (Float64) */
bool prim_lt_Float_Float(double a, double b);
bool prim_gt_Float_Float(double a, double b);
bool prim_le_Float_Float(double a, double b);
bool prim_ge_Float_Float(double a, double b);
bool prim_eq_Float_Float(double a, double b);
bool prim_ne_Float_Float(double a, double b);

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

/* Issue 2 P1: Region-aware higher-order functions */
Obj* list_map_region(struct Region* r, Obj* fn, Obj* xs);
Obj* list_fold_region(struct Region* r, Obj* fn, Obj* init, Obj* xs);
Obj* list_foldr_region(struct Region* r, Obj* fn, Obj* init, Obj* xs);
Obj* list_filter_region(struct Region* r, Obj* fn, Obj* xs);

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
Obj* prim_kind_eq(Obj* a, Obj* b);
Obj* prim_not(Obj* a);

/* ========== Type Predicates ========== */

Obj* prim_null(Obj* x);
Obj* prim_pair(Obj* x);
Obj* prim_int(Obj* x);
Obj* prim_float(Obj* x);
Obj* prim_char(Obj* x);
Obj* prim_sym(Obj* x);
Obj* prim_nothing_p(Obj* x);

/* ========== Type Introspection ========== */

Obj* ctr_tag(Obj* x);
Obj* ctr_arg(Obj* x, Obj* idx);

/* ========== I/O Primitives ========== */

Obj* prim_display(Obj* x);
Obj* prim_print(Obj* x);
Obj* prim_println(Obj* args);
Obj* prim_newline(void);

/* File I/O */
Obj* prim_file_read(Obj* path);              /* Read entire file as string */
Obj* prim_file_write(Obj* path, Obj* content);  /* Write string to file */
Obj* prim_file_append(Obj* path, Obj* content); /* Append string to file */
Obj* prim_file_exists(Obj* path);            /* Check if file exists */
Obj* prim_file_delete(Obj* path);            /* Delete file */
Obj* prim_stdin_read_line(void);             /* Read line from stdin */
Obj* prim_stdout_write(Obj* content);        /* Write to stdout */

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
Obj* call_closure_region(struct Region* caller_region, Obj* clos, Obj** args, int argc);

/* ========== Generic Function Operations (Multiple Dispatch) ========== */

/* Create a new generic function object */
Obj* mk_generic(const char* name);

/* Add a method to a generic function. Returns the generic object for chaining. */
Obj* generic_add_method(Obj* generic_obj, Obj** param_kinds, int param_count, ClosureFn impl);
Obj* generic_add_method_region(Obj* generic_obj, Obj** param_kinds, int param_count, ClosureFnRegion impl);

/* Call a generic function with multiple dispatch. Selects the most specific method. */
Obj* call_generic(Obj* generic_obj, Obj** args, int argc);
Obj* call_generic_region(struct Region* caller_region, Obj* generic_obj, Obj** args, int argc);

/* Lookup the most specific method for arguments */
MethodInfo* omni_generic_lookup(Obj* generic_obj, Obj** args, int argc);

/* Invoke a generic function with arguments */
Obj* omni_generic_invoke(Obj* generic_obj, Obj** args, int argc);
Obj* omni_generic_invoke_region(struct Region* caller_region, Obj* generic_obj, Obj** args, int argc);

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

/* ========== Runtime Type Registry for User-Defined Types ========== */

/* Register a new user-defined type
 * Args:
 *   name - Type name (e.g., "Point")
 *   parent - Parent type name (e.g., "Any") or NULL for default (Any)
 * Returns: The assigned tag number for this type
 */
int omni_register_user_type(const char* name, const char* parent);

/* Get type name from a tag number
 * For built-in types, returns the hardcoded name.
 * For user-defined types, looks up in the registry.
 * Returns: Type name string or NULL if unknown
 */
const char* omni_type_name_from_tag(int tag);

/* Check if type_a is a subtype of type_b (by name)
 * Handles: Any, Number, List, and user-defined type hierarchies
 * Returns: true if type_a is a subtype of type_b
 */
bool omni_is_subtype_runtime(const char* type_a, const char* type_b);

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

/*
 * is_pattern_match: Structural pattern matching for match expressions
 *
 * Used by codegen to implement (match value [pattern result] ...) syntax.
 * Performs structural equality checking:
 * - Primitives: exact value equality
 * - Symbols: equality by name
 * - Lists/Arrays: recursive structural comparison
 * - Wildcard (_): always matches
 *
 * Args:
 *   - pattern: The pattern to match against
 *   - value: The value being matched
 *
 * Returns:
 *   - 1 if pattern matches value
 *   - 0 otherwise
 */
int is_pattern_match(Obj* pattern, Obj* value);

/*
 * prim_pattern_match - Pattern match with variable bindings
 *
 * Performs structural pattern matching and extracts variable bindings.
 * Supports:
 *   - Simple variables: [x y] matches [1 2] → {x: 1, y: 2}
 *   - Rest patterns: [x & rest] matches [1 2 3] → {x: 1, rest: [2 3]}
 *   - As patterns: [p as name] binds both inner vars and whole match
 *   - Nested patterns: [[a b] c] matches [[1 2] 3] → {a: 1, b: 2, c: 3}
 *
 * Args:
 *   - pattern: Pattern to match (array, list, symbol, or literal)
 *   - value: Value to match against
 * Returns:
 *   - Dict of bindings if match succeeded
 *   - NULL if no match
 */
Obj* prim_pattern_match(Obj* pattern, Obj* value);

/*
 * Compile a pattern string for later use
 * Args:
 *   - pattern_obj: Pattern string to compile
 * Returns:
 *   - Pattern object (string containing the pattern)
 *   - NULL (nil) if input is invalid
 *
 * Example:
 *   prim_compile_pattern(mk_string("[0-9]+"))
 *   => "[0-9]+"
 */
Obj* prim_compile_pattern(Obj* pattern_obj);

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
void arena_register_external(Arena* a, void* ptr, void (*cleanup)(void*));
void arena_destroy(Arena* a);
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
/* mk_tuple_region, mk_named_tuple_region REMOVED 2026-01-15 */
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
Obj* dict_get_by_name(Obj* dict, const char* name);  /* Symbol key lookup by name */

/* Array Collection Operations */
Obj* prim_array_sort(Obj* arr, Obj* cmp);        /* Sort with optional comparator */
Obj* prim_array_reverse(Obj* arr);               /* Reverse in-place */
Obj* prim_array_find(Obj* arr, Obj* pred);       /* Find first matching element */
Obj* prim_array_find_index(Obj* arr, Obj* pred); /* Find index of first match */
Obj* prim_array_copy(Obj* arr);                  /* Shallow copy */

/* Dict Collection Operations */
Obj* dict_keys(Obj* dict);                       /* Get all keys as array */
Obj* dict_values(Obj* dict);                     /* Get all values as array */
Obj* dict_entries(Obj* dict);                    /* Get key-value pairs as array */
Obj* dict_merge(Obj* base, Obj* overlay);        /* Merge two dicts */
Obj* dict_has_key(Obj* dict, Obj* key);          /* Check if key exists */
Obj* dict_remove(Obj* dict, Obj* key);           /* Remove key from dict */
Obj* prim_dict_copy(Obj* dict);                  /* Shallow copy */

/* Update Operators (Phase 4.2: Memory Management) */
Obj* prim_update(Obj* coll, Obj* key, Obj* val); /* Copy-on-write update */
Obj* prim_update_bang(Obj* coll, Obj* key, Obj* val); /* In-place update */
Obj* prim_update_in(Obj* coll, Obj* path, Obj* val);  /* Nested path COW update */
Obj* prim_update_in_bang(Obj* coll, Obj* path, Obj* val); /* Nested path in-place */

/* ========== Set Data Structure (Issue 24) ========== */

/* Set constructors */
Obj* mk_set_region(struct Region* r);
Obj* mk_set(void);

/* Set operations */
void set_add(Obj* set, Obj* elem);              /* Add element to set */
Obj* set_remove(Obj* set, Obj* elem);           /* Remove element, return removed or NULL */
Obj* set_contains(Obj* set, Obj* elem);         /* Check membership, return bool */
int set_size(Obj* set);                         /* Number of elements */
Obj* set_empty_p(Obj* set);                     /* Check if empty, return bool */

/* Set algebra (Issue 24 P2) */
Obj* set_union(Obj* a, Obj* b);                 /* A ∪ B */
Obj* set_intersection(Obj* a, Obj* b);          /* A ∩ B */
Obj* set_difference(Obj* a, Obj* b);            /* A \ B */
Obj* set_symmetric_difference(Obj* a, Obj* b);  /* A △ B */
Obj* set_subset_p(Obj* a, Obj* b);              /* A ⊆ B */
Obj* set_superset_p(Obj* a, Obj* b);            /* A ⊇ B */

/* Set conversion */
Obj* set_to_list(Obj* set);                     /* Convert set to list */
Obj* list_to_set(Obj* list);                    /* Convert list to set */
Obj* set_to_array(Obj* set);                    /* Convert set to array */
Obj* array_to_set(Obj* arr);                    /* Convert array to set */

/* Set iteration & HOFs (Issue 24 P3) */
Obj* set_foreach(Obj* set, Obj* fn);            /* Apply fn to each element */
Obj* set_map(Obj* set, Obj* fn);                /* Map fn over set, return new set */
Obj* set_filter(Obj* set, Obj* pred);           /* Filter set by predicate */
Obj* set_reduce(Obj* set, Obj* fn, Obj* init);  /* Reduce set to single value */

/* ========== DateTime (Issue 25) ========== */

/* DateTime constructors */
Obj* mk_datetime(int64_t timestamp, int32_t tz_offset);  /* Create DateTime with timestamp and timezone */
Obj* datetime_now(void);                         /* Current local time */
Obj* datetime_now_utc(void);                     /* Current UTC time */
Obj* datetime_from_unix(Obj* timestamp);         /* Create from Unix timestamp (UTC) */
Obj* datetime_from_unix_tz(Obj* timestamp, Obj* tz_offset);  /* Create with timezone */

/* DateTime accessors */
Obj* datetime_to_unix(Obj* dt);                  /* Get Unix timestamp */
Obj* datetime_tz_offset(Obj* dt);                /* Get timezone offset in seconds */

/* DateTime component constructors (Issue 25 P1) */
Obj* datetime_make(Obj* year, Obj* month, Obj* day, Obj* hour, Obj* minute, Obj* second);  /* UTC */
Obj* datetime_make_local(Obj* year, Obj* month, Obj* day, Obj* hour, Obj* minute, Obj* second);  /* Local TZ */

/* DateTime component accessors (Issue 25 P1) */
Obj* datetime_year(Obj* dt);                     /* Get year (e.g., 2026) */
Obj* datetime_month(Obj* dt);                    /* Get month (1-12) */
Obj* datetime_day(Obj* dt);                      /* Get day of month (1-31) */
Obj* datetime_hour(Obj* dt);                     /* Get hour (0-23) */
Obj* datetime_minute(Obj* dt);                   /* Get minute (0-59) */
Obj* datetime_second(Obj* dt);                   /* Get second (0-59) */
Obj* datetime_weekday(Obj* dt);                  /* Get day of week (0=Sun, 6=Sat) */
Obj* datetime_yearday(Obj* dt);                  /* Get day of year (1-366) */

/* DateTime arithmetic (Issue 25 P2) */
Obj* datetime_add_seconds(Obj* dt, Obj* seconds);   /* Add seconds */
Obj* datetime_add_minutes(Obj* dt, Obj* minutes);   /* Add minutes */
Obj* datetime_add_hours(Obj* dt, Obj* hours);       /* Add hours */
Obj* datetime_add_days(Obj* dt, Obj* days);         /* Add days */
Obj* datetime_diff(Obj* dt1, Obj* dt2);             /* Difference in seconds */
Obj* datetime_lt(Obj* dt1, Obj* dt2);               /* dt1 < dt2 */
Obj* datetime_gt(Obj* dt1, Obj* dt2);               /* dt1 > dt2 */
Obj* datetime_eq(Obj* dt1, Obj* dt2);               /* dt1 == dt2 */
Obj* datetime_le(Obj* dt1, Obj* dt2);               /* dt1 <= dt2 */
Obj* datetime_ge(Obj* dt1, Obj* dt2);               /* dt1 >= dt2 */

/* DateTime formatting (Issue 25 P3) */
Obj* datetime_format(Obj* dt, Obj* format);         /* Format with strftime specifiers */
Obj* datetime_to_iso8601(Obj* dt);                  /* "2026-01-18T12:30:00Z" */
Obj* datetime_to_rfc2822(Obj* dt);                  /* "Sun, 18 Jan 2026 12:30:00 +0000" */
Obj* datetime_parse_iso8601(Obj* str);              /* Parse ISO 8601 string */

Obj* mk_keyword(const char* name);

/* mk_tuple, tuple_get, tuple_length REMOVED 2026-01-15 - use mk_array */
/* mk_named_tuple, named_tuple_get REMOVED 2026-01-15 - use mk_dict */

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

/* Padding functions (Issue 26 P1) */
Obj* prim_string_pad_left(Obj* str_obj, Obj* width_obj, Obj* pad_obj);
Obj* prim_string_pad_right(Obj* str_obj, Obj* width_obj, Obj* pad_obj);
Obj* prim_string_center(Obj* str_obj, Obj* width_obj, Obj* pad_obj);

/* Convert to uppercase/lowercase */
Obj* prim_string_upcase(Obj* str_obj);
Obj* prim_string_lowcase(Obj* str_obj);
Obj* prim_string_downcase(Obj* str_obj);       /* Alias for lowcase (Issue 26 P0) */
Obj* prim_string_capitalize(Obj* str_obj);     /* Capitalize first letter (Issue 26 P0) */
Obj* prim_string_titlecase(Obj* str_obj);      /* Capitalize each word (Issue 26 P0) */

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

/* Search functions (Issue 26 P2) */
Obj* prim_string_last_index_of(Obj* str_obj, Obj* substr_obj);
Obj* prim_string_starts_with(Obj* str_obj, Obj* prefix_obj);
Obj* prim_string_ends_with(Obj* str_obj, Obj* suffix_obj);

/* Replace functions (Issue 26 P2) */
Obj* prim_string_replace_first(Obj* old_obj, Obj* new_obj, Obj* str_obj);
Obj* prim_string_replace_all(Obj* old_obj, Obj* new_obj, Obj* str_obj);

/* Split functions (Issue 26 P3) */
Obj* prim_string_lines(Obj* str_obj);
Obj* prim_string_words(Obj* str_obj);
Obj* prim_string_chars(Obj* str_obj);

/* Additional string utilities (Issue 26 P3) */
Obj* prim_string_reverse(Obj* str_obj);
Obj* prim_string_repeat(Obj* str_obj, Obj* count_obj);

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

/* Module registry globals - shared by main program and dlopen'd modules */
struct Module;
struct StrMap;
extern struct Module* g_module_registry;
extern struct StrMap* g_module_map;
extern struct Module* g_current_module;

/* Module management */
Obj* prim_module_begin(Obj* name_obj);
Obj* prim_module_end(void);
Obj* prim_module_get(Obj* name_obj);
Obj* prim_module_ref(Obj* module_name, Obj* symbol_name);
Obj* prim_module_exports(Obj* name_obj);
Obj* prim_module_list(void);

/* Export and import */
Obj* prim_export(Obj* symbol_name, Obj* value);
Obj* prim_import(Obj* name_obj, Obj* options);
Obj* prim_use(Obj* name_obj);
Obj* prim_require(Obj* name_obj);

/* Advanced import forms */
Obj* prim_import_only(Obj* name_obj, Obj* symbols_list);   /* (import mod :only [a b]) */
Obj* prim_import_as(Obj* name_obj, Obj* alias);            /* (import mod :as m) */
Obj* prim_import_except(Obj* name_obj, Obj* exclude_list); /* (import mod :except [x]) */

/* Qualified name resolution */
Obj* prim_resolve(Obj* qualified_name);

/* ========== Issue 1 P1: region_of(obj) mechanism ========== */

/*
 * omni_obj_region - Get the owning Region of a boxed object
 *
 * This function maps any boxed Obj* to its owning Region in O(1).
 * It returns NULL for immediates (ints, chars, bools) and NULL pointers.
 *
 * This is a foundation for Region-RC, enabling:
 * - retain/release operations at external boundaries
 * - store-barrier auto-repair (mutation-time lifetime checks)
 * - region accounting diagnostics
 *
 * Implementation: Uses of owner_region field added to struct Obj.
 * Option A (tooling-first) was chosen over pointer masking for ASAN/TSAN compatibility.
 *
 * @param o: The object to query
 * @return: The owning Region* for boxed objects, NULL for immediates or NULL
 */
static inline Region* omni_obj_region(Obj* o) {
    if (!o || IS_IMMEDIATE(o)) return NULL;
    return o->owner_region;
}

/* Define this so test files know the feature is implemented */
#define OMNI_OBJ_REGION_IMPLEMENTED 1

/* ========== Issue 2 P4.1: Region Lifetime Rank Accessors ========== */

/*
 * omni_region_set_lifetime_rank - Set the lifetime rank of a region
 *
 * The lifetime_rank represents the outlives depth (nesting order) of a region.
 * - Rank 0 = root/global region (no parent)
 * - Rank N = child region with N levels of nesting
 *
 * Ranks are only comparable when owner_thread matches (cross-thread ranks
 * are not comparable, so stores must always repair).
 *
 * This function should be called by generated code after region_create()
 * to establish the correct rank hierarchy.
 *
 * @param r: The region to update
 * @param rank: The new lifetime rank value
 */
void omni_region_set_lifetime_rank(struct Region* r, uint64_t rank);

/*
 * omni_region_get_lifetime_rank - Get the lifetime rank of a region
 *
 * Returns the outlives depth for the given region.
 *
 * @param r: The region to query
 * @return: The lifetime rank, or 0 if r is NULL
 */
uint64_t omni_region_get_lifetime_rank(struct Region* r);

/* ========== Issue 2 P4.3b: Region Parent/Ancestry Accessors ========== */

/*
 * omni_region_set_parent - Set the parent region for a region
 *
 * Issue 2 P4.3b: Establishes parent-child relationship
 * in the single-thread outlives tree.
 *
 * @param r: The region to set parent for
 * @param parent: The parent region (NULL for root/global)
 *
 * This is called by generated code after region_create() when
 * a new region is created within an existing region scope.
 */
void omni_region_set_parent(struct Region* r, struct Region* parent);

/*
 * omni_region_outlives - Check if region 'a' outlives region 'b'
 *
 * Issue 2 P4.3b: Use ancestry relation to determine ordering.
 * A region outlives another if it is an ancestor (or equal).
 *
 * Ranks alone are insufficient for this because same-rank regions
 * may be siblings (incomparable) and cannot prove ordering.
 *
 * @param a: Potential outliving region (ancestor candidate)
 * @param b: Potential outlived region (descendant candidate)
 * @return: true if a outlives b (a is ancestor of b or a == b)
 *
 * Usage in store barrier:
 *   if (omni_region_outlives(dst, src)) {
 *       // No repair needed: dst is older/outlives src
 *   } else {
 *       // Repair needed: dst is younger or ordering unknown
 *   }
 */
bool omni_region_outlives(struct Region* a, struct Region* b);

/* ========== Issue 2 P4: Mutation Store Barrier ========== */

/*
 * omni_store_repair - Store barrier with automatic lifetime repair
 *
 * This function enforces Region Closure Property by automatically repairing
 * illegal lifetime edges at mutation time (younger→older or shorter-lived→longer-lived).
 *
 * Required logic (high-level):
 * - If new_value is immediate/NULL → store directly.
 * - Region* src = omni_obj_region(new_value), Region* dst = omni_obj_region(container)
 * - If src == NULL || dst == NULL || src == dst → store directly.
 * - If the store would create a younger→older (or shorter-lived→longer-lived) edge:
 *   - Choose repair strategy via region accounting (Issue 2 P3):
 *     - small ⇒ transmigrate(new_value, src, dst) and store moved pointer
 *     - large ⇒ region_merge if permitted (Issue 2 P5), else fallback to transmigrate
 * - Update escape_repair_count (for Issue 2 accounting diagnostics).
 *
 * This is the single choke point for all mutation operations.
 * All pointer-storing primitives MUST use this helper.
 *
 * @param container: The container object being mutated (array, dict, box, etc.)
 * @param slot: The pointer slot being updated (e.g., &array->data[i])
 * @param new_value: The value to store
 * @return: The value that should actually be stored (may be repaired/transmigrated)
 */
Obj* omni_store_repair(Obj* container, Obj** slot, Obj* new_value);

/* ========== Issue 27: Developer Tools & Debugging ========== */

/* P0: Object Inspection (debug.c) */
Obj* prim_inspect(Obj* obj);               /* Print detailed object info */
Obj* prim_type_of(Obj* obj);               /* Return type as keyword */
Obj* prim_address_of(Obj* obj);            /* Return memory address as int */
Obj* prim_refcount_of(Obj* obj);           /* Return reference count */
Obj* prim_region_of_debug(Obj* obj);       /* Return owning region info */
Obj* prim_sizeof_obj(Obj* obj);            /* Return object size in bytes */

/* P2: REPL Documentation & Introspection (debug.c) */
Obj* prim_doc(Obj* symbol);                /* Return documentation for symbol */
Obj* prim_source(Obj* obj);                /* Return source code for closure */

/* P1: Memory Debugging (debug.c) */
Obj* prim_region_stats(void);              /* Print region allocation stats */
Obj* prim_memory_usage(void);              /* Return total memory allocated */
Obj* prim_gc_info(void);                   /* Show RC statistics */
Obj* prim_allocation_trace(Obj* enable);   /* Toggle allocation tracing */
Obj* prim_leak_check(void);                /* Scan for potential leaks */

/* P3: Testing Framework (testing.c) */
Obj* prim_deftest(Obj* name, Obj* test_fn);    /* Register a test case */
Obj* prim_clear_tests(void);                   /* Clear all registered tests */
Obj* prim_assert_eq(Obj* expected, Obj* actual);   /* Assert equality */
Obj* prim_assert_true(Obj* value);             /* Assert truthy */
Obj* prim_assert_false(Obj* value);            /* Assert falsy */
Obj* prim_assert_near(Obj* expected, Obj* actual, Obj* epsilon);  /* Assert float near */
Obj* prim_assert_throws(Obj* thunk);           /* Assert throws error */
Obj* prim_assert_not_eq(Obj* val1, Obj* val2); /* Assert not equal */
Obj* prim_assert_nil(Obj* value);              /* Assert nil/nothing */
Obj* prim_assert_not_nil(Obj* value);          /* Assert not nil */
Obj* prim_run_tests(void);                     /* Run all tests */
Obj* prim_run_tests_matching(Obj* pattern);    /* Run tests matching pattern */
Obj* prim_test_report(void);                   /* Generate test report */
Obj* prim_list_tests(void);                    /* List all test names */
Obj* prim_test_count(void);                    /* Return test count */

/* P4: Profiling (profile.c) */
Obj* prim_profile_enable(void);                /* Enable profiling */
Obj* prim_profile_disable(void);               /* Disable profiling */
Obj* prim_profile_reset(void);                 /* Reset profiling data */
Obj* prim_profile(Obj* name, Obj* thunk);      /* Profile expression */
Obj* prim_time(Obj* thunk);                    /* Time expression execution */
Obj* prim_call_counts(void);                   /* Get function call counts */
Obj* prim_hot_spots(Obj* limit);               /* Find slow functions */
Obj* prim_profile_report(void);                /* Generate profile report */
Obj* prim_profile_entry(Obj* name);            /* Get entry for function */
Obj* prim_benchmark(Obj* iterations, Obj* thunk);  /* Benchmark function */
Obj* prim_profile_memory(Obj* thunk);          /* Profile memory usage */
Obj* prim_profiling_enabled_p(void);           /* Check if profiling enabled */
Obj* prim_profile_count(void);                 /* Return profiled entry count */

/* ========== Issue 28: Standard Library Expansion ========== */

/* P0: Random Numbers (math_numerics.c) */
Obj* prim_seed_random(Obj* seed);              /* Set RNG seed */
Obj* prim_random(void);                        /* Random float [0,1) */
Obj* prim_random_int(Obj* n);                  /* Random int [0,n) */
Obj* prim_random_range(Obj* min, Obj* max);    /* Random int in range */
Obj* prim_random_float_range(Obj* min, Obj* max);  /* Random float in range */
Obj* prim_random_choice(Obj* coll);            /* Random element from collection */
Obj* prim_shuffle(Obj* coll);                  /* Shuffle collection */

/* P1: Collection Utilities (collections.c) */
Obj* prim_sort(Obj* coll);                     /* Sort list/array */
Obj* prim_sort_by(Obj* key_fn, Obj* coll);     /* Sort by key function */
Obj* prim_sort_with(Obj* cmp_fn, Obj* coll);   /* Sort with comparator */
Obj* prim_reverse(Obj* coll);                  /* Reverse list/array */
Obj* prim_group_by(Obj* key_fn, Obj* coll);    /* Group by key */
Obj* prim_partition(Obj* pred, Obj* coll);     /* Split by predicate */
Obj* prim_coll_take(Obj* n, Obj* coll);        /* First n elements */
Obj* prim_coll_drop(Obj* n, Obj* coll);        /* Skip first n elements */
Obj* prim_take_while(Obj* pred, Obj* coll);    /* Take while predicate */
Obj* prim_drop_while(Obj* pred, Obj* coll);    /* Drop while predicate */
Obj* prim_flatten(Obj* coll);                  /* Flatten one level */
Obj* prim_flatten_deep(Obj* coll);             /* Flatten all levels */
Obj* prim_zip(Obj* a, Obj* b);                 /* Pair elements */
Obj* prim_unzip(Obj* pairs);                   /* Unpair to lists */
Obj* prim_frequencies(Obj* coll);              /* Count occurrences */
Obj* prim_distinct(Obj* coll);                 /* Remove duplicates */
Obj* prim_interleave(Obj* a, Obj* b);          /* Interleave elements */
Obj* prim_interpose(Obj* sep, Obj* coll);      /* Insert separator */

/* P2: I/O Utilities (io.c) */
Obj* prim_io_read_file(Obj* path);             /* Read file as string */
Obj* prim_io_write_file(Obj* path, Obj* content);  /* Write string to file */
Obj* prim_read_lines(Obj* path);               /* Read file as lines */
Obj* prim_write_lines(Obj* path, Obj* lines);  /* Write lines to file */
Obj* prim_read_bytes(Obj* path);               /* Read file as bytes */
Obj* prim_write_bytes(Obj* path, Obj* bytes);  /* Write bytes to file */
Obj* prim_file_exists_p(Obj* path);            /* Check if path exists */
Obj* prim_file_p(Obj* path);                   /* Check if regular file */
Obj* prim_directory_p(Obj* path);              /* Check if directory */
Obj* prim_readable_p(Obj* path);               /* Check if readable */
Obj* prim_writable_p(Obj* path);               /* Check if writable */
Obj* prim_list_directory(Obj* path);           /* List directory contents */
Obj* prim_make_directory(Obj* path);           /* Create directory */
Obj* prim_make_directories(Obj* path);         /* Create nested dirs */
Obj* prim_io_delete_file(Obj* path);           /* Delete file */
Obj* prim_delete_directory(Obj* path);         /* Delete empty directory */
Obj* prim_io_rename(Obj* old_path, Obj* new_path);  /* Rename/move */
Obj* prim_copy_file(Obj* src, Obj* dst);       /* Copy file */
Obj* prim_path_join(Obj* parts);               /* Join path components */
Obj* prim_path_join2(Obj* a, Obj* b);          /* Join two paths */
Obj* prim_path_dirname(Obj* path);             /* Get directory part */
Obj* prim_path_basename(Obj* path);            /* Get filename part */
Obj* prim_path_extension(Obj* path);           /* Get extension */
Obj* prim_path_absolute(Obj* path);            /* Get absolute path */
Obj* prim_current_directory(void);             /* Get cwd */
Obj* prim_change_directory(Obj* path);         /* Change cwd */
Obj* prim_io_file_size(Obj* path);             /* Get file size */
Obj* prim_io_file_mtime(Obj* path);            /* Get modification time */
Obj* prim_io_getenv(Obj* name);                /* Get env variable */
Obj* prim_io_setenv(Obj* name, Obj* value);    /* Set env variable */
Obj* prim_io_unsetenv(Obj* name);              /* Unset env variable */
Obj* prim_io_environ(void);                    /* Get all env vars */

/* P3: JSON Support (json.c) */
Obj* prim_json_parse(Obj* json_str);           /* Parse JSON string */
Obj* prim_json_stringify(Obj* value);          /* Convert to JSON string */
Obj* prim_json_pretty(Obj* value);             /* Pretty-print JSON */
Obj* prim_json_read(Obj* path);                /* Read and parse JSON file */
Obj* prim_json_write(Obj* path, Obj* value);   /* Stringify and write JSON */
Obj* prim_json_get(Obj* json, Obj* path);      /* Get nested value by path */
Obj* prim_json_valid_p(Obj* json_str);         /* Check if valid JSON */

#ifdef __cplusplus
}
#endif

#endif /* OMNI_RUNTIME_H */
