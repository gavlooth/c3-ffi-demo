/*
 * Symmetric Reference Counting Implementation
 *
 * Key insight: Treat scope/stack frame as an object that participates in
 * the ownership graph. Each reference is bidirectional.
 *
 * This allows O(1) deterministic cycle collection without global GC.
 */

#define _POSIX_C_SOURCE 200809L
#include "symmetric.h"
#include <string.h>
#include <limits.h>

#define INITIAL_CAPACITY 8

/* Slab allocator for SymObj */
#define SYM_POOL_SIZE 512

typedef struct SymPool {
    struct SymObj* objects; /* Allocated as a block of SYM_POOL_SIZE objects */
    struct SymPool* next;
} SymPool;

/* Global symmetric RC context wrapper for TLS */
static __thread struct {
    SymPool* pools;       /* Slab allocator pools */
    SymObj* freelist;     /* Free list of SymObj */
} SYM_TLS = {NULL, NULL};

static SymObj* sym_pool_alloc(void) {
    if (!SYM_TLS.freelist) {
        /* Allocate new pool */
        SymPool* pool = malloc(sizeof(SymPool));
        if (!pool) return NULL;
        pool->objects = malloc(SYM_POOL_SIZE * sizeof(SymObj));
        if (!pool->objects) { free(pool); return NULL; }
        pool->next = SYM_TLS.pools;
        SYM_TLS.pools = pool;
        /* Add all objects to freelist */
        for (int i = 0; i < SYM_POOL_SIZE; i++) {
            pool->objects[i].refs = (SymObj**)SYM_TLS.freelist;
            SYM_TLS.freelist = &pool->objects[i];
        }
    }
    SymObj* obj = SYM_TLS.freelist;
    SYM_TLS.freelist = (SymObj*)obj->refs;
    return obj;
}

static void sym_pool_free(SymObj* obj) {
    obj->refs = (SymObj**)SYM_TLS.freelist;
    SYM_TLS.freelist = obj;
}

void sym_pool_cleanup(void) {
    SymPool* p = SYM_TLS.pools;
    while (p) {
        SymPool* next = p->next;
        free(p->objects);
        free(p);
        p = next;
    }
    SYM_TLS.pools = NULL;
    SYM_TLS.freelist = NULL;
}

/* ============== Object Operations ============== */

SymObj* sym_obj_new(void* data, void (*destructor)(void*)) {
    SymObj* obj = sym_pool_alloc();
    if (!obj) return NULL;
    memset(obj, 0, sizeof(SymObj));
    obj->ref_capacity = SYM_INLINE_REFS;
    obj->refs = obj->inline_refs;
    obj->data = data;
    obj->destructor = destructor;
    return obj;
}

void sym_obj_add_ref(SymObj* obj, SymObj* target) {
    if (!obj || !target) return;

    /* Grow refs array if needed */
    if (obj->ref_count >= obj->ref_capacity) {
        int new_cap = obj->ref_capacity == 0 ? 8 : obj->ref_capacity * 2;
        SymObj** new_refs;
        if (obj->refs == obj->inline_refs) {
            new_refs = malloc(new_cap * sizeof(SymObj*));
            if (!new_refs) return;
            memcpy(new_refs, obj->inline_refs, obj->ref_count * sizeof(SymObj*));
        } else {
            new_refs = realloc(obj->refs, new_cap * sizeof(SymObj*));
            if (!new_refs) return;
        }
        obj->refs = new_refs;
        obj->ref_capacity = new_cap;
    }

    obj->refs[obj->ref_count++] = target;
}

/* ============== Scope Operations ============== */

SymScope* sym_scope_new(SymScope* parent) {
    SymScope* scope = malloc(sizeof(SymScope));
    if (!scope) return NULL;

    scope->owned = NULL;
    scope->owned_count = 0;
    scope->owned_capacity = 0;
    scope->parent = parent;

    return scope;
}

void sym_scope_own(SymScope* scope, SymObj* obj) {
    if (!scope || !obj || obj->freed) return;

    /* Grow owned array if needed */
    if (scope->owned_count >= scope->owned_capacity) {
        int new_cap;
        if (scope->owned_capacity == 0) {
            new_cap = INITIAL_CAPACITY;
        } else if (scope->owned_capacity > INT_MAX / 2) {
            return;  /* Overflow protection */
        } else {
            new_cap = scope->owned_capacity * 2;
        }
        SymObj** new_owned = realloc(scope->owned, new_cap * sizeof(SymObj*));
        if (!new_owned) return;
        scope->owned = new_owned;
        scope->owned_capacity = new_cap;
    }

    obj->external_rc++;
    scope->owned[scope->owned_count++] = obj;
}

void sym_scope_release(SymScope* scope) {
    if (!scope) return;

    for (int i = 0; i < scope->owned_count; i++) {
        sym_dec_external(scope->owned[i]);
    }

    scope->owned_count = 0;
}

void sym_scope_free(SymScope* scope) {
    if (!scope) return;
    free(scope->owned);
    free(scope);
}

/* ============== Reference Counting ============== */

static void sym_check_free(SymObj* obj);

void sym_inc_external(SymObj* obj) {
    if (!obj || obj->freed) return;
    obj->external_rc++;
}

void sym_dec_external(SymObj* obj) {
    if (!obj || obj->freed) return;
    obj->external_rc--;
    sym_check_free(obj);
}

void sym_inc_internal(SymObj* from, SymObj* to) {
    if (!to || to->freed) return;
    to->internal_rc++;
    if (from) {
        sym_obj_add_ref(from, to);
    }
}

void sym_dec_internal(SymObj* obj) {
    if (!obj || obj->freed) return;
    obj->internal_rc--;
    sym_check_free(obj);
}

static void sym_check_free(SymObj* obj) {
    if (!obj || obj->freed) return;

    /* Object is garbage when it has no external references */
    if (obj->external_rc <= 0) {
        obj->freed = 1;

        /* Decrement internal refs for all objects this one referenced */
        for (int i = 0; i < obj->ref_count; i++) {
            sym_dec_internal(obj->refs[i]);
        }

        /* Free the object */
        if (obj->destructor && obj->data) {
            obj->destructor(obj->data);
        }
        if (obj->refs && obj->refs != obj->inline_refs) {
            free(obj->refs);
        }
        obj->refs = NULL;
        obj->ref_count = 0;
        obj->data = NULL;

        sym_pool_free(obj);
    }
}

/* ============== Context Operations ============== */

SymContext* sym_context_new(void) {
    SymContext* ctx = malloc(sizeof(SymContext));
    if (!ctx) return NULL;

    ctx->global_scope = sym_scope_new(NULL);
    if (!ctx->global_scope) {
        free(ctx);
        return NULL;
    }

    ctx->scope_stack = malloc(INITIAL_CAPACITY * sizeof(SymScope*));
    if (!ctx->scope_stack) {
        sym_scope_free(ctx->global_scope);
        free(ctx);
        return NULL;
    }

    ctx->scope_stack[0] = ctx->global_scope;
    ctx->stack_size = 1;
    ctx->stack_capacity = INITIAL_CAPACITY;

    ctx->objects_created = 0;
    ctx->objects_freed = 0;
    ctx->cycles_collected = 0;

    return ctx;
}

void sym_context_free(SymContext* ctx) {
    if (!ctx) return;

    /* Release all scopes from innermost to outermost */
    while (ctx->stack_size > 0) {
        sym_ctx_exit_scope(ctx);
    }

    sym_scope_free(ctx->global_scope);
    free(ctx->scope_stack);
    free(ctx);
}

SymScope* sym_current_scope(SymContext* ctx) {
    if (!ctx || ctx->stack_size == 0) return NULL;
    return ctx->scope_stack[ctx->stack_size - 1];
}

SymScope* sym_ctx_enter_scope(SymContext* ctx) {
    if (!ctx) return NULL;

    SymScope* parent = sym_current_scope(ctx);
    SymScope* scope = sym_scope_new(parent);
    if (!scope) return NULL;

    /* Grow stack if needed */
    if (ctx->stack_size >= ctx->stack_capacity) {
        int new_cap = ctx->stack_capacity * 2;
        SymScope** new_stack = realloc(ctx->scope_stack, new_cap * sizeof(SymScope*));
        if (!new_stack) {
            sym_scope_free(scope);
            return NULL;
        }
        ctx->scope_stack = new_stack;
        ctx->stack_capacity = new_cap;
    }

    ctx->scope_stack[ctx->stack_size++] = scope;
    return scope;
}

void sym_ctx_exit_scope(SymContext* ctx) {
    if (!ctx || ctx->stack_size <= 1) return;  /* Don't exit global scope */

    SymScope* scope = ctx->scope_stack[--ctx->stack_size];

    /* Count potential cycles before release */
    int cycles = 0;
    for (int i = 0; i < scope->owned_count; i++) {
        SymObj* obj = scope->owned[i];
        if (obj && !obj->freed && obj->internal_rc > 0 && obj->external_rc == 1) {
            cycles++;
        }
    }
    ctx->cycles_collected += cycles;

    sym_scope_release(scope);
    sym_scope_free(scope);
}

SymObj* sym_ctx_alloc(SymContext* ctx, void* data, void (*destructor)(void*)) {
    if (!ctx) return NULL;

    SymObj* obj = sym_obj_new(data, destructor);
    if (!obj) return NULL;

    SymScope* scope = sym_current_scope(ctx);
    sym_scope_own(scope, obj);
    ctx->objects_created++;

    return obj;
}

void sym_ctx_link(SymContext* ctx, SymObj* from, SymObj* to) {
    if (!ctx || !from || !to) return;
    sym_inc_internal(from, to);
}

/* ============== Utility ============== */

int sym_is_orphaned(SymObj* obj) {
    if (!obj) return 1;
    return obj->external_rc <= 0;
}

int sym_total_rc(SymObj* obj) {
    if (!obj) return 0;
    return obj->external_rc + obj->internal_rc;
}
