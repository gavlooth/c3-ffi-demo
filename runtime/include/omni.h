/*
 * OmniLisp Runtime - Public API
 *
 * This header defines the public interface for the OmniLisp runtime.
 * All user-generated code includes this header and links against libomni.a
 */

#ifndef OMNI_RUNTIME_H
#define OMNI_RUNTIME_H

#define _POSIX_C_SOURCE 200112L

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

/* Forward declaration for internal GenObj (legacy) */
struct GenObj;

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

typedef struct Obj {
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
} Obj;
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
    if (IS_IMMEDIATE_INT(p)) return INT_IMM_VALUE(p);
    if (IS_IMMEDIATE_BOOL(p)) return p == OMNI_TRUE ? 1 : 0;
    if (IS_IMMEDIATE_CHAR(p)) return CHAR_IMM_VALUE(p);
    return p ? p->i : 0;
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
    return p && p->tag == TAG_INT;
}

/* Check if value is a character (boxed or immediate) */
static inline int is_char_val(Obj* p) {
    if (IS_IMMEDIATE_CHAR(p)) return 1;
    return p && p->tag == TAG_CHAR;
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
    if (p && p->tag == TAG_CHAR) return p->i;
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
Obj* prim_newline(void);

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

#ifdef __cplusplus
}
#endif

#endif /* OMNI_RUNTIME_H */
