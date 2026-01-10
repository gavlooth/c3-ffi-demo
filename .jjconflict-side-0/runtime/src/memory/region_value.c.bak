/*
 * region_value.c - Implementation of Region-Aware Value Constructors
 *
 * This file implements all the mk_*_region functions that allocate
 * Values in a specific Region.
 *
 * Integration with ASAP:
 * - The compiler will emit calls to these functions when region management
 *   is enabled
 * - Each allocation goes through region_alloc(), which uses the bump
 *   allocator in the Region
 * - String allocations also go through region_alloc()
 *
 * Note: Some external resources (Channel, FiberContext, etc.) still use
 * malloc because they have complex lifecycle requirements beyond simple
 * region management.
 */

#include "region_value.h"

// ============================================================================
// Primitive Allocator
// ============================================================================

Value* alloc_val_region(Region* r, Tag tag) {
    if (!r) {
        // Fallback to malloc if no region (shouldn't happen in practice)
        Value* v = malloc(sizeof(Value));
        if (!v) return NULL;
        v->mark = 0;
        v->tag = tag;
        v->type = NULL;
        return v;
    }

    Value* v = region_alloc(r, sizeof(Value));
    if (!v) return NULL;
    v->mark = 0;
    v->tag = tag;
    v->type = NULL;
    return v;
}

// ============================================================================
// Singleton Values (no allocation needed)
// ============================================================================

Value* mk_nil_region(Region* r) {
    (void)r;  // Unused
    static Value nil_singleton = { .mark = 0, .tag = T_NIL, .type = NULL };
    return &nil_singleton;
}

Value* mk_nothing_region(Region* r) {
    (void)r;  // Unused
    static Value nothing_singleton = { .mark = 0, .tag = T_NOTHING, .type = NULL };
    return &nothing_singleton;
}

// ============================================================================
// Scalar Value Constructors
// ============================================================================

Value* mk_int_region(Region* r, long i) {
    Value* v = alloc_val_region(r, T_INT);
    if (!v) return NULL;
    v->i = i;
    return v;
}

Value* mk_char_region(Region* r, long codepoint) {
    Value* v = alloc_val_region(r, T_CHAR);
    if (!v) return NULL;
    v->codepoint = codepoint;
    return v;
}

Value* mk_float_region(Region* r, double f) {
    Value* v = alloc_val_region(r, T_FLOAT);
    if (!v) return NULL;
    v->f = f;
    return v;
}

// ============================================================================
// String-Allocating Constructors
// ============================================================================

Value* mk_sym_region(Region* r, const char* s) {
    if (!s) s = "";
    Value* v = alloc_val_region(r, T_SYM);
    if (!v) return NULL;

    // Allocate string in region
    size_t len = strlen(s);
    v->s = region_alloc(r, len + 1);
    if (!v->s) return NULL;
    strcpy(v->s, s);
    return v;
}

Value* mk_code_region(Region* r, const char* s) {
    if (!s) s = "";
    Value* v = alloc_val_region(r, T_CODE);
    if (!v) return NULL;

    size_t len = strlen(s);
    v->s = region_alloc(r, len + 1);
    if (!v->s) return NULL;
    strcpy(v->s, s);
    return v;
}

Value* mk_string_region(Region* r, const char* s, size_t len) {
    Value* v = alloc_val_region(r, T_STRING);
    if (!v) return NULL;

    // Allocate string data in region (no null terminator needed for T_STRING)
    v->str.data = region_alloc(r, len);
    if (!v->str.data) return NULL;
    memcpy(v->str.data, s, len);
    v->str.len = len;
    return v;
}

Value* mk_string_cstr_region(Region* r, const char* s) {
    if (!s) return mk_string_region(r, "", 0);
    return mk_string_region(r, s, strlen(s));
}

Value* mk_error_region(Region* r, const char* msg) {
    if (!msg) msg = "unknown error";
    Value* v = alloc_val_region(r, T_ERROR);
    if (!v) return NULL;

    size_t len = strlen(msg);
    v->s = region_alloc(r, len + 1);
    if (!v->s) return NULL;
    strcpy(v->s, msg);
    return v;
}

// ============================================================================
// Pair/Cell Constructor
// ============================================================================

Value* mk_cell_region(Region* r, Value* car, Value* cdr) {
    Value* v = alloc_val_region(r, T_CELL);
    if (!v) return NULL;
    v->cell.car = car;
    v->cell.cdr = cdr;
    return v;
}

// ============================================================================
// Lambda/Closure Constructors
// ============================================================================

Value* mk_lambda_region(Region* r, Value* params, Value* body, Value* env) {
    Value* v = alloc_val_region(r, T_LAMBDA);
    if (!v) return NULL;
    v->lam.params = params;
    v->lam.body = body;
    v->lam.env = env;
    v->lam.defaults = NULL;
    return v;
}

Value* mk_lambda_with_defaults_region(Region* r, Value* params, Value* body, Value* env, Value* defaults) {
    Value* v = alloc_val_region(r, T_LAMBDA);
    if (!v) return NULL;
    v->lam.params = params;
    v->lam.body = body;
    v->lam.env = env;
    v->lam.defaults = defaults;
    return v;
}

// ============================================================================
// Box Constructor
// ============================================================================

Value* mk_box_region(Region* r, Value* initial) {
    Value* v = alloc_val_region(r, T_BOX);
    if (!v) return NULL;
    v->box_value = initial;
    return v;
}

// ============================================================================
// Continuation Constructor
// ============================================================================

Value* mk_cont_region(Region* r, ContFn fn, Value* menv, int tag) {
    Value* v = alloc_val_region(r, T_CONT);
    if (!v) return NULL;
    v->cont.fn = fn;
    v->cont.menv = menv;
    v->cont.tag = tag;
    return v;
}

// ============================================================================
// Primitive Constructor
// ============================================================================

Value* mk_prim_region(Region* r, PrimFn fn) {
    Value* v = alloc_val_region(r, T_PRIM);
    if (!v) return NULL;
    v->prim = fn;
    return v;
}

// ============================================================================
// Complex Constructors (use malloc for external resources)
// ============================================================================

Value* mk_chan_region(Region* r, int capacity) {
    Value* v = alloc_val_region(r, T_CHAN);
    if (!v) return NULL;

    // Channel structure itself is allocated with malloc
    // because it has complex lifecycle (shared between processes)
    Channel* ch = malloc(sizeof(Channel));
    if (!ch) return NULL;

    ch->capacity = capacity;
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = 0;
    ch->send_waiters = NULL;
    ch->recv_waiters = NULL;

    // Allocate buffer for buffered channels
    if (capacity > 0) {
        ch->buffer = malloc(sizeof(Value*) * capacity);
        if (!ch->buffer) {
            free(ch);
            return NULL;
        }
        for (int i = 0; i < capacity; i++) {
            ch->buffer[i] = NULL;
        }
    } else {
        ch->buffer = NULL;
    }

    v->chan.ch = ch;
    v->chan.capacity = capacity;
    return v;
}

Value* mk_process_region(Region* r, Value* thunk) {
    Value* v = alloc_val_region(r, T_PROCESS);
    if (!v) return NULL;

    v->proc.thunk = thunk;
    v->proc.cont = NULL;
    v->proc.menv = NULL;
    v->proc.result = NULL;
    v->proc.park_value = NULL;
    v->proc.state = PROC_READY;

    // Fiber context uses malloc (complex lifecycle with ucontext)
    FiberContext* ctx = malloc(sizeof(FiberContext));
    if (!ctx) return NULL;

    ctx->stack = malloc(FIBER_STACK_SIZE);
    if (!ctx->stack) {
        free(ctx);
        return NULL;
    }
    ctx->yield_value = NULL;
    ctx->started = 0;
    v->proc.fiber_ctx = ctx;

    return v;
}

Value* mk_bounce_region(Region* r, Value* fn, Value* args) {
    Value* v = alloc_val_region(r, T_BOUNCE);
    if (!v) return NULL;
    v->bounce.fn = fn;
    v->bounce.args = args;
    return v;
}

// ============================================================================
// Port and Syntax Constructors
// ============================================================================

Value* mk_port_region(Region* r, FILE* fp, const char* filename, int mode) {
    Value* v = alloc_val_region(r, T_PORT);
    if (!v) return NULL;

    v->port.fp = fp;
    v->port.mode = mode;
    v->port.closed = 0;

    // Copy filename string into region
    if (filename) {
        size_t len = strlen(filename);
        v->port.filename = region_alloc(r, len + 1);
        if (!v->port.filename) return NULL;
        strcpy(v->port.filename, filename);
    } else {
        v->port.filename = NULL;
    }

    return v;
}

Value* mk_syntax_region(Region* r, const char* name, Value* literals, Value* rules, Value* def_env) {
    Value* v = alloc_val_region(r, T_SYNTAX);
    if (!v) return NULL;

    // Copy name string into region
    if (name) {
        size_t len = strlen(name);
        v->syntax.name = region_alloc(r, len + 1);
        if (!v->syntax.name) return NULL;
        strcpy(v->syntax.name, name);
    } else {
        v->syntax.name = NULL;
    }

    v->syntax.literals = literals;
    v->syntax.rules = rules;
    v->syntax.def_env = def_env;

    return v;
}

// ============================================================================
// Grammar and FFI Constructors
// ============================================================================

Value* mk_grammar_region(Region* r, struct PikaGrammar* grammar, const char* name) {
    Value* v = alloc_val_region(r, T_GRAMMAR);
    if (!v) return NULL;

    v->grammar.grammar = grammar;

    // Copy name string into region
    if (name) {
        size_t len = strlen(name);
        v->grammar.name = region_alloc(r, len + 1);
        if (!v->grammar.name) return NULL;
        strcpy(v->grammar.name, name);
    } else {
        v->grammar.name = NULL;
    }

    return v;
}

Value* mk_ffi_lib_region(Region* r, void* handle, const char* name) {
    Value* v = alloc_val_region(r, T_FFI_LIB);
    if (!v) return NULL;

    v->ffi_lib.handle = handle;

    // Copy name string into region
    if (name) {
        size_t len = strlen(name);
        v->ffi_lib.name = region_alloc(r, len + 1);
        if (!v->ffi_lib.name) return NULL;
        strcpy(v->ffi_lib.name, name);
    } else {
        v->ffi_lib.name = NULL;
    }

    return v;
}

Value* mk_ffi_ptr_region(Region* r, void* ptr, const char* type_name, int owned) {
    Value* v = alloc_val_region(r, T_FFI_PTR);
    if (!v) return NULL;

    v->ffi_ptr.ptr = ptr;
    v->ffi_ptr.owned = owned;

    // Copy type_name string into region
    if (type_name) {
        size_t len = strlen(type_name);
        v->ffi_ptr.type_name = region_alloc(r, len + 1);
        if (!v->ffi_ptr.type_name) return NULL;
        strcpy(v->ffi_ptr.type_name, type_name);
    } else {
        v->ffi_ptr.type_name = NULL;
    }

    return v;
}

// ============================================================================
// Thread Constructor
// ============================================================================

Value* mk_thread_region(Region* r, Value* thunk) {
    Value* v = alloc_val_region(r, T_THREAD);
    if (!v) return NULL;

    v->thread.thunk = thunk;
    v->thread.result = NULL;
    v->thread.started = 0;
    v->thread.joined = 0;
    v->thread.done = 0;

    return v;
}
