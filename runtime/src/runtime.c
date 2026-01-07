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

#include "memory/region_core.h"
#include "memory/region_value.h"

/* RC-G Runtime: Standard RC is now Region-RC (Coarse-grained) */
void inc_ref(Value* x) { (void)x; }
void dec_ref(Value* x) { (void)x; }
void free_obj(Value* x) { (void)x; }

/* ========== Tagged Pointers (Multi-Type Immediates) ========== */
#define IMM_TAG_MASK     0x7ULL
#define IMM_TAG_PTR      0x0ULL
#define IMM_TAG_INT      0x1ULL
#define IMM_TAG_CHAR     0x2ULL
#define IMM_TAG_BOOL     0x3ULL

#define GET_IMM_TAG(p)   (((uintptr_t)(p)) & IMM_TAG_MASK)
#define IS_IMMEDIATE(p)      (GET_IMM_TAG(p) != IMM_TAG_PTR)
#define IS_IMMEDIATE_INT(p)  (GET_IMM_TAG(p) == IMM_TAG_INT)
#define IS_IMMEDIATE_CHAR(p) (GET_IMM_TAG(p) == IMM_TAG_CHAR)
#define IS_IMMEDIATE_BOOL(p) (GET_IMM_TAG(p) == IMM_TAG_BOOL)
#define IS_BOXED(p)          (GET_IMM_TAG(p) == IMM_TAG_PTR && (p) != NULL)

#define MAKE_INT_IMM(n)      ((Value*)(((uintptr_t)(n) << 3) | IMM_TAG_INT))
#define INT_IMM_VALUE(p)     ((long)((intptr_t)(p) >> 3))
#define OMNI_FALSE         ((Value*)(((uintptr_t)0 << 3) | IMM_TAG_BOOL))
#define OMNI_TRUE          ((Value*)(((uintptr_t)1 << 3) | IMM_TAG_BOOL))
#define MAKE_CHAR_IMM(c)     ((Value*)(((uintptr_t)(c) << 3) | IMM_TAG_CHAR))
#define CHAR_IMM_VALUE(p)    ((long)(((uintptr_t)(p)) >> 3))

/* Global Region for Interpreter/Legacy support */
static Region* _global_region = NULL;
static void _ensure_global_region(void) {
    if (!_global_region) _global_region = region_create();
}

/* Helpers */
int is_nil(Value* x) { return x == NULL || (IS_BOXED(x) && x->tag == T_NIL); }
static Value omni_nothing_obj = { .mark = 0, .tag = T_NOTHING, .type = NULL };
int is_nothing(Value* x) {
    return x == &omni_nothing_obj || (x && IS_BOXED(x) && x->tag == T_NOTHING);
}

static inline long obj_to_int(Value* p) {
    if (IS_IMMEDIATE_INT(p)) return INT_IMM_VALUE(p);
    if (IS_IMMEDIATE_BOOL(p)) return p == OMNI_TRUE ? 1 : 0;
    if (IS_IMMEDIATE_CHAR(p)) return CHAR_IMM_VALUE(p);
    return (p && IS_BOXED(p) && p->tag == T_INT) ? p->i : 0;
}

static inline int obj_to_bool(Value* p) {
    if (IS_IMMEDIATE_BOOL(p)) return p == OMNI_TRUE;
    if (p == NULL) return 1;
    if (IS_IMMEDIATE_INT(p) || IS_IMMEDIATE_CHAR(p)) return 1;
    if (IS_BOXED(p) && p->tag == T_NOTHING) return 0;
    return 1;
}

static inline int obj_tag(Value* p) {
    if (p == NULL) return T_NIL;
    if (IS_IMMEDIATE_INT(p)) return T_INT;
    if (IS_IMMEDIATE_CHAR(p)) return T_CHAR;
    if (IS_IMMEDIATE_BOOL(p)) return T_INT;
    return p->tag;
}

/* Object Constructors (Shimmed to Global Region) */
Value* mk_int(long i) { _ensure_global_region(); return mk_int_region(_global_region, i); }
Value* mk_int_unboxed(long i) { return MAKE_INT_IMM(i); }
Value* mk_float(double f) { _ensure_global_region(); return mk_float_region(_global_region, f); }
Value* mk_char(long c) {
    if (c >= 0 && c <= 0x10FFFF) return MAKE_CHAR_IMM(c);
    _ensure_global_region(); return mk_char_region(_global_region, c);
}
Value* mk_bool(int b) { return b ? OMNI_TRUE : OMNI_FALSE; }
Value* mk_pair(Value* a, Value* b) { _ensure_global_region(); return mk_cell_region(_global_region, a, b); }
Value* mk_cell(Value* a, Value* b) { return mk_pair(a, b); }
Value* mk_sym(const char* s) { _ensure_global_region(); return mk_sym_region(_global_region, s); }
Value* mk_nothing(void) { return &omni_nothing_obj; }
Value* mk_box(Value* v) { _ensure_global_region(); return mk_box_region(_global_region, v); }
Value* mk_error(const char* msg) { _ensure_global_region(); return mk_error_region(_global_region, msg); }

/* Region-Resident Operations */
void box_set(Value* b, Value* v) { if (b && IS_BOXED(b) && b->tag == T_BOX) b->box_value = v; }
Value* box_get(Value* b) { return (b && IS_BOXED(b) && b->tag == T_BOX) ? b->box_value : NULL; }

/* Arithmetic */
Value* prim_add(Value* a, Value* b) { return mk_int(obj_to_int(a) + obj_to_int(b)); }
Value* prim_sub(Value* a, Value* b) { return mk_int(obj_to_int(a) - obj_to_int(b)); }
Value* prim_mul(Value* a, Value* b) { return mk_int(obj_to_int(a) * obj_to_int(b)); }
Value* prim_div(Value* a, Value* b) { 
    long bv = obj_to_int(b); 
    return mk_int(bv ? obj_to_int(a) / bv : 0); 
}

/* Comparisons */
Value* prim_eq(Value* a, Value* b) { return mk_bool(obj_to_int(a) == obj_to_int(b)); }
Value* prim_lt(Value* a, Value* b) { return mk_bool(obj_to_int(a) < obj_to_int(b)); }
Value* prim_gt(Value* a, Value* b) { return mk_bool(obj_to_int(a) > obj_to_int(b)); }

/* List accessors */
Value* car(Value* p) { return (p && IS_BOXED(p) && p->tag == T_CELL) ? p->cell.car : NULL; }
Value* cdr(Value* p) { return (p && IS_BOXED(p) && p->tag == T_CELL) ? p->cell.cdr : NULL; }
Value* obj_car(Value* p) { return car(p); }
Value* obj_cdr(Value* p) { return cdr(p); }

/* I/O */
void print_obj(Value* x) {
    if (!x) { printf("()\n"); return; }
    if (IS_IMMEDIATE_INT(x)) { printf("%ld", (long)INT_IMM_VALUE(x)); return; }
    if (IS_IMMEDIATE_CHAR(x)) { printf("%c", (char)CHAR_IMM_VALUE(x)); return; }
    if (IS_IMMEDIATE_BOOL(x)) { printf("%s", x == OMNI_TRUE ? "true" : "false"); return; }
    switch (x->tag) {
        case T_INT: printf("%ld", x->i); break;
        case T_FLOAT: printf("%g", x->f); break;
        case T_SYM: printf("%s", x->s);
            break;
        case T_CELL: printf("("); print_obj(x->cell.car); printf(" . "); print_obj(x->cell.cdr); printf(")"); break;
        default: printf("#<obj:%d>", x->tag); break;
    }
}

Value* prim_display(Value* x) { print_obj(x); return mk_nothing(); }
Value* prim_print(Value* x) { print_obj(x); printf("\n"); return mk_nothing(); }
Value* prim_newline(void) { printf("\n"); return mk_nothing(); }

/* Stubs for legacy GC logic */
void safe_point(void) {}
void flush_deferred(void) {}
void process_deferred(void) {}
void sym_init(void) {}
void sym_cleanup(void) {}
void region_init(void) {}
void invalidate_weak_refs_for(void* t) { (void)t; }
Value* mk_nil(void) { return NULL; }
