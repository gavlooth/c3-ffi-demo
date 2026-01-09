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

#include "memory/region_core.h"
#include "memory/region_value.h"
#include "internal_types.h"

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
static Obj omni_nothing_obj = { .tag = TAG_NOTHING };
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

Obj* call_closure(Obj* clos, Obj** args, int argc) {
    if (!clos || !IS_BOXED(clos) || clos->tag != TAG_CLOSURE) return NULL;
    Closure* c = (Closure*)clos->ptr;
    // Arity check
    if (c->arity >= 0 && c->arity != argc) return NULL;
    return c->fn(c->captures, args, argc);
}

/* Truthiness */
int is_truthy(Obj* x) {
    if (x == NULL) return 0;
    if (IS_IMMEDIATE_BOOL(x)) return x == OMNI_TRUE;
    if (IS_IMMEDIATE_INT(x)) return INT_IMM_VALUE(x) != 0;
    if (IS_IMMEDIATE_CHAR(x)) return 1;
    if (IS_BOXED(x)) {
        if (x->tag == TAG_NOTHING) return 0;
        if (x->tag == TAG_INT) return x->i != 0;
        if (x->tag == TAG_FLOAT) return x->f != 0.0;
    }
    return 1;
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
typedef struct Channel {
    Obj** buffer;
    int capacity;
    int head;
    int tail;
    int count;
    bool closed;
    pthread_mutex_t lock;
    pthread_cond_t send_cond;
    pthread_cond_t recv_cond;
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
    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->send_cond, NULL);
    pthread_cond_init(&ch->recv_cond, NULL);
    
    o->ptr = ch;
    return o;
}

int channel_send(Obj* ch_obj, Obj* val) {
    if (!ch_obj || !IS_BOXED(ch_obj) || ch_obj->tag != TAG_CHANNEL) return -1;
    Channel* ch = (Channel*)ch_obj->ptr;
    
    pthread_mutex_lock(&ch->lock);
    
    while (!ch->closed && ((ch->capacity > 0 && ch->count >= ch->capacity) || (ch->capacity == 0 && ch->count > 0))) {
        pthread_cond_wait(&ch->send_cond, &ch->lock);
    }
    
    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    
    if (ch->capacity > 0) {
        ch->buffer[ch->tail] = val;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
    } else {
        // Unbuffered: one slot "handshake"
        ch->buffer = (Obj**)&val; // Temporarily use buffer pointer to store val
        ch->count = 1;
    }
    
    pthread_cond_signal(&ch->recv_cond);
    
    // For unbuffered, wait for receiver to take it
    if (ch->capacity == 0) {
        while (!ch->closed && ch->count > 0) {
            pthread_cond_wait(&ch->send_cond, &ch->lock);
        }
    }
    
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

Obj* channel_recv(Obj* ch_obj) {
    if (!ch_obj || !IS_BOXED(ch_obj) || ch_obj->tag != TAG_CHANNEL) return NULL;
    Channel* ch = (Channel*)ch_obj->ptr;
    
    pthread_mutex_lock(&ch->lock);
    
    while (!ch->closed && ch->count == 0) {
        pthread_cond_wait(&ch->recv_cond, &ch->lock);
    }
    
    if (ch->count == 0 && ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return NULL;
    }
    
    Obj* val = NULL;
    if (ch->capacity > 0) {
        val = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
    } else {
        val = *(Obj**)ch->buffer;
        ch->count = 0;
    }
    
    pthread_cond_signal(&ch->send_cond);
    pthread_mutex_unlock(&ch->lock);
    return val;
}

void channel_close(Obj* ch_obj) {
    if (!ch_obj || !IS_BOXED(ch_obj) || ch_obj->tag != TAG_CHANNEL) return;
    Channel* ch = (Channel*)ch_obj->ptr;
    
    pthread_mutex_lock(&ch->lock);
    ch->closed = true;
    pthread_cond_broadcast(&ch->send_cond);
    pthread_cond_broadcast(&ch->recv_cond);
    pthread_mutex_unlock(&ch->lock);
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
    a->value = newval;
    pthread_rwlock_unlock(&a->lock);
    return old;
}

Obj* atom_swap(Obj* atom_obj, Obj* fn) {
    if (!atom_obj || !IS_BOXED(atom_obj) || atom_obj->tag != TAG_ATOM || !fn) return NULL;
    Atom* a = (Atom*)atom_obj->ptr;
    pthread_rwlock_wrlock(&a->lock);
    Obj* old = a->value;
    a->value = call_closure(fn, &old, 1);
    Obj* new_val = a->value;
    pthread_rwlock_unlock(&a->lock);
    return new_val;
}

Obj* atom_cas(Obj* atom_obj, Obj* expected, Obj* newval) {
    if (!atom_obj || !IS_BOXED(atom_obj) || atom_obj->tag != TAG_ATOM) return mk_bool(0);
    Atom* a = (Atom*)atom_obj->ptr;
    pthread_rwlock_wrlock(&a->lock);
    if (a->value == expected) {
        a->value = newval;
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
    pthread_mutex_t lock;
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
Obj* mk_keyword(const char* name) { _ensure_global_region(); return mk_keyword_region(_global_region, name); }
Obj* mk_tuple(Obj** items, int count) { _ensure_global_region(); return mk_tuple_region(_global_region, items, count); }
Obj* mk_named_tuple(Obj** keys, Obj** values, int count) { _ensure_global_region(); return mk_named_tuple_region(_global_region, keys, values, count); }
Obj* mk_generic(const char* name) { _ensure_global_region(); return mk_generic_region(_global_region, name); }
Obj* mk_kind(const char* name, Obj** params, int param_count) {
    _ensure_global_region();
    return mk_kind_region(_global_region, name, params, param_count);
}

/* Region-Resident Operations */
void box_set(Obj* b, Obj* v) { if (b && IS_BOXED(b) && b->tag == TAG_BOX) b->a = v; }
Obj* box_get(Obj* b) { return (b && IS_BOXED(b) && b->tag == TAG_BOX) ? b->a : NULL; }

/* Array Operations */
void array_push(Obj* arr, Obj* val) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return;
    Array* a = (Array*)arr->ptr;
    // For global region usage we assume realloc works or fails
    if (a->len < a->capacity) {
        a->data[a->len++] = val;
    } else {
        // Simple realloc for global region (unsafe if region allocator doesn't support realloc)
        // Since we are in global region (malloc-based arena maybe?), we can't easily realloc in place.
        // For now, silently drop or crash?
        // TODO: Implement region_realloc
    }
}

Obj* array_get(Obj* arr, int idx) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return NULL;
    Array* a = (Array*)arr->ptr;
    if (idx >= 0 && idx < a->len) return a->data[idx];
    return NULL;
}

void array_set(Obj* arr, int idx, Obj* val) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return;
    Array* a = (Array*)arr->ptr;
    if (idx >= 0 && idx < a->len) a->data[idx] = val;
}

int array_length(Obj* arr) {
    if (!arr || !IS_BOXED(arr) || arr->tag != TAG_ARRAY) return 0;
    return ((Array*)arr->ptr)->len;
}

/* Dict Operations */
void dict_set(Obj* dict, Obj* key, Obj* val) {
    if (!dict || !IS_BOXED(dict) || dict->tag != TAG_DICT) return;
    Dict* d = (Dict*)dict->ptr;
    _ensure_global_region();
    hashmap_put_region(&d->map, key, val, _global_region);
}

Obj* dict_get(Obj* dict, Obj* key) {
    if (!dict || !IS_BOXED(dict) || dict->tag != TAG_DICT) return NULL;
    Dict* d = (Dict*)dict->ptr;
    return (Obj*)hashmap_get(&d->map, key);
}

/* Tuple Operations */
Obj* tuple_get(Obj* tup, int idx) {
    if (!tup || !IS_BOXED(tup) || tup->tag != TAG_TUPLE) return NULL;
    Tuple* t = (Tuple*)tup->ptr;
    if (idx >= 0 && idx < t->count) return t->items[idx];
    return NULL;
}

int tuple_length(Obj* tup) {
    if (!tup || !IS_BOXED(tup) || tup->tag != TAG_TUPLE) return 0;
    return ((Tuple*)tup->ptr)->count;
}

/* Named Tuple Operations */
Obj* named_tuple_get(Obj* tup, Obj* key) {
    if (!tup || !IS_BOXED(tup) || tup->tag != TAG_NAMED_TUPLE) return NULL;
    NamedTuple* nt = (NamedTuple*)tup->ptr;
    // Linear scan
    for (int i = 0; i < nt->count; i++) {
        // Pointer equality
        if (nt->keys[i] == key) return nt->values[i];
        // Symbol equality fallback
        if (IS_BOXED(nt->keys[i]) && IS_BOXED(key)) {
            if (nt->keys[i]->tag == TAG_KEYWORD && key->tag == TAG_KEYWORD) {
                if (strcmp((char*)nt->keys[i]->ptr, (char*)key->ptr) == 0) return nt->values[i];
            }
        }
    }
    return NULL;
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
        case TAG_TUPLE: return mk_sym("tuple");
        case TAG_NAMED_TUPLE: return mk_sym("named-tuple");
        case TAG_ARRAY: return mk_sym("array");
        case TAG_DICT: return mk_sym("dict");
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
        case TAG_ARRAY: printf("[...]"); break; // TODO: iter
        case TAG_DICT: printf("#{...}"); break; // TODO: iter
        case TAG_TUPLE: printf("{...}"); break; // TODO: iter
        case TAG_NAMED_TUPLE: printf("#(:...)"); break;
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

    /* Count parameters */
    size_t param_count = 0;
    Obj* p = params_obj;
    while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
        param_count++;
        p = p->b;
    }

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

/*
 * Helper: Set a nested field in a structure (immutable - returns new structure)
 * For now, this is a simplified implementation that creates new pairs
 * A full implementation would use proper structural sharing
 */
static Obj* deep_set(Obj* root, char** components, int component_count, Obj* new_value) {
    if (!root || component_count == 0) return new_value;

    /* For simplicity, this is a placeholder that returns an error */
    /* A full implementation would recursively rebuild the structure */
    omni_ensure_global_region();
    return mk_error_region(omni_get_global_region(), "deep-put: Not yet fully implemented");
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
            case TAG_TUPLE:
                type_name = "Tuple";
                break;
            case TAG_NAMED_TUPLE:
                type_name = "NamedTuple";
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