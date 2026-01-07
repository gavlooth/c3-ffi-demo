/*
 * region_value.c - Implementation of Region-Aware Obj Constructors
 *
 * This file implements mk_*_region functions for the new Obj type.
 */

#include "region_value.h"
#include "../internal_types.h"

// ============================================================================
// Primitive Allocator
// ============================================================================

Obj* alloc_obj_region(Region* r, int tag) {
    if (!r) {
        // Fallback to malloc if no region (shouldn't happen in practice)
        Obj* o = malloc(sizeof(Obj));
        if (!o) return NULL;
        o->mark = 0;
        o->tag = tag;
        o->generation = 0;
        o->tethered = 0;
        return o;
    }

    Obj* o = region_alloc(r, sizeof(Obj));
    if (!o) return NULL;
    o->mark = 1;
    o->tag = tag;
    o->generation = 0; // Should ideally use region's generation or global
    o->tethered = 0;
    return o;
}

// ============================================================================
// Singleton Values
// ============================================================================

Obj* mk_nil_region(Region* r) {
    (void)r;
    return NULL; // NIL is NULL in new runtime
}

static Obj nothing_obj = { .tag = TAG_NOTHING };
Obj* mk_nothing_region(Region* r) {
    (void)r;
    return &nothing_obj;
}

// ============================================================================
// Scalar Value Constructors
// ============================================================================

Obj* mk_int_region(Region* r, long i) {
    Obj* o = alloc_obj_region(r, TAG_INT);
    if (!o) return NULL;
    o->i = i;
    return o;
}

Obj* mk_char_region(Region* r, long codepoint) {
    // Note: runtime often uses immediates for char, this is for boxed backup
    Obj* o = alloc_obj_region(r, TAG_CHAR);
    if (!o) return NULL;
    o->i = codepoint; // Use i for codepoint storage in boxed char
    return o;
}

Obj* mk_float_region(Region* r, double f) {
    Obj* o = alloc_obj_region(r, TAG_FLOAT);
    if (!o) return NULL;
    o->f = f;
    return o;
}

// ============================================================================
// String/Sym Constructors
// ============================================================================

Obj* mk_sym_region(Region* r, const char* s) {
    if (!s) return NULL;
    Obj* o = alloc_obj_region(r, TAG_SYM);
    if (!o) return NULL;

    size_t len = strlen(s);
    char* buf = (char*)region_alloc(r, len + 1);
    if (!buf) return NULL;
    strcpy(buf, s);
    o->ptr = buf;
    return o;
}

// Map string to sym for now as new runtime lacks TAG_STRING
Obj* mk_string_region(Region* r, const char* s, size_t len) {
    return mk_sym_region(r, s); // TODO: Proper string type
}

Obj* mk_string_cstr_region(Region* r, const char* s) {
    return mk_sym_region(r, s);
}

Obj* mk_code_region(Region* r, const char* s) {
    // Map to SYM for now, or add TAG_CODE to omni.h
    return mk_sym_region(r, s);
}

Obj* mk_error_region(Region* r, const char* msg) {
    if (!msg) return NULL;
    Obj* o = alloc_obj_region(r, TAG_ERROR);
    if (!o) return NULL;

    size_t len = strlen(msg);
    char* buf = (char*)region_alloc(r, len + 1);
    if (!buf) return NULL;
    strcpy(buf, msg);
    o->ptr = buf;
    return o;
}

Obj* mk_keyword_region(Region* r, const char* name) {
    if (!name) name = "";
    Obj* o = alloc_obj_region(r, TAG_KEYWORD);
    if (!o) return NULL;

    size_t len = strlen(name);
    char* buf = region_alloc(r, len + 1);
    if (!buf) return NULL;
    strcpy(buf, name);
    o->ptr = buf;
    return o;
}

// ============================================================================
// Cell/Box Constructors
// ============================================================================

Obj* mk_cell_region(Region* r, Obj* car, Obj* cdr) {
    Obj* o = alloc_obj_region(r, TAG_PAIR); // TAG_PAIR = T_CELL legacy
    if (!o) return NULL;
    o->a = car;
    o->b = cdr;
    o->is_pair = 1;
    return o;
}

Obj* mk_box_region(Region* r, Obj* initial) {
    Obj* o = alloc_obj_region(r, TAG_BOX);
    if (!o) return NULL;
    o->a = initial; // Store value in 'a'
    return o;
}

// ============================================================================
// Collections
// ============================================================================

Obj* mk_array_region(Region* r, int capacity) {
    Obj* o = alloc_obj_region(r, TAG_ARRAY);
    if (!o) return NULL;

    Array* arr = region_alloc(r, sizeof(Array));
    if (!arr) return NULL;
    
    arr->capacity = capacity > 0 ? capacity : 8;
    arr->len = 0;
    arr->data = region_alloc(r, arr->capacity * sizeof(Obj*));
    
    o->ptr = arr;
    return o;
}

Obj* mk_dict_region(Region* r) {
    Obj* o = alloc_obj_region(r, TAG_DICT);
    if (!o) return NULL;

    Dict* d = region_alloc(r, sizeof(Dict));
    if (!d) return NULL;

    // Initialize region-resident hash map
    d->map.bucket_count = 16;
    d->map.entry_count = 0;
    d->map.load_factor = 0.75f;
    d->map.buckets = region_alloc(r, sizeof(HashEntry*) * d->map.bucket_count);
    memset(d->map.buckets, 0, sizeof(HashEntry*) * d->map.bucket_count);
    d->map.had_alloc_failure = 0;
    
    o->ptr = d;
    return o;
}

Obj* mk_tuple_region(Region* r, Obj** items, int count) {
    Obj* o = alloc_obj_region(r, TAG_TUPLE);
    if (!o) return NULL;

    Tuple* t = region_alloc(r, sizeof(Tuple) + count * sizeof(Obj*));
    if (!t) return NULL;

    t->count = count;
    for (int i = 0; i < count; i++) {
        t->items[i] = items[i];
    }

    o->ptr = t;
    return o;
}

Obj* mk_named_tuple_region(Region* r, Obj** keys, Obj** values, int count) {
    Obj* o = alloc_obj_region(r, TAG_NAMED_TUPLE);
    if (!o) return NULL;

    NamedTuple* nt = region_alloc(r, sizeof(NamedTuple));
    if (!nt) return NULL;

    nt->count = count;
    // Separate arrays for keys and values
    nt->keys = region_alloc(r, count * sizeof(Obj*));
    nt->values = region_alloc(r, count * sizeof(Obj*));

    if (nt->keys && nt->values) {
        for (int i = 0; i < count; i++) {
            nt->keys[i] = keys[i];
            nt->values[i] = values[i];
        }
    }

    o->ptr = nt;
    return o;
}

Obj* mk_generic_region(Region* r, const char* name) {
    Obj* o = alloc_obj_region(r, TAG_GENERIC);
    if (!o) return NULL;
    
    Generic* g = region_alloc(r, sizeof(Generic));
    if (!g) return NULL;
    
    size_t len = strlen(name);
    g->name = (char*)region_alloc(r, len + 1);
    strcpy((char*)g->name, name);
    
    o->ptr = g;
    return o;
}

Obj* mk_kind_region(Region* r, const char* name, Obj** params, int param_count) {
    Obj* o = alloc_obj_region(r, TAG_KIND);
    if (!o) return NULL;
    
    Kind* k = region_alloc(r, sizeof(Kind));
    if (!k) return NULL;
    
    size_t len = strlen(name);
    k->name = (char*)region_alloc(r, len + 1);
    strcpy((char*)k->name, name);

    k->param_count = param_count;
    if (param_count > 0 && params) {
        k->params = region_alloc(r, sizeof(Obj*) * param_count);
        for (int i = 0; i < param_count; i++) {
            k->params[i] = params[i];
        }
    } else {
        k->params = NULL;
    }
    
    o->ptr = k;
    return o;
}

// ============================================================================
// Lambda/Closure - Simplified/Stubs
// ============================================================================

Obj* mk_lambda_region(Region* r, Obj* params, Obj* body, Obj* env) {
    (void)r; (void)params; (void)body; (void)env;
    return NULL;
}

Obj* mk_lambda_with_defaults_region(Region* r, Obj* params, Obj* body, Obj* env, Obj* defaults) {
    (void)r; (void)params; (void)body; (void)env; (void)defaults;
    return NULL;
}

// ============================================================================
// Other Stubs
// ============================================================================

Obj* mk_cont_region(Region* r, ContFn fn, Obj* menv, int tag) {
    (void)r; (void)fn; (void)menv; (void)tag;
    return NULL;
}

Obj* mk_prim_region(Region* r, PrimFn fn) {
    (void)r; (void)fn;
    return NULL;
}

Obj* mk_thread_region(Region* r, Obj* thunk) {
    (void)r; (void)thunk;
    return NULL;
}

Obj* mk_port_region(Region* r, FILE* fp, const char* filename, int mode) {
    (void)r; (void)fp; (void)filename; (void)mode;
    return NULL;
}

Obj* mk_syntax_region(Region* r, const char* name, Obj* literals, Obj* rules, Obj* def_env) {
    (void)r; (void)name; (void)literals; (void)rules; (void)def_env;
    return NULL;
}

Obj* mk_ffi_lib_region(Region* r, void* handle, const char* name) {
    (void)r; (void)handle; (void)name;
    return NULL;
}

Obj* mk_ffi_ptr_region(Region* r, void* ptr, const char* type_name, int owned) {
    (void)r; (void)ptr; (void)type_name; (void)owned;
    return NULL;
}