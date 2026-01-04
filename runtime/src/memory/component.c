#include "component.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Slab allocator for SymComponent headers */
#define COMP_POOL_SIZE 128

typedef struct CompPool {
    SymComponent objects[COMP_POOL_SIZE];
    struct CompPool* next;
} CompPool;

static __thread struct {
    CompPool* pools;
    SymComponent* freelist;
    uint32_t next_id;
} COMP_CTX = {NULL, NULL, 1};

SymComponent* sym_component_new(void) {
    if (!COMP_CTX.freelist) {
        CompPool* pool = malloc(sizeof(CompPool));
        if (!pool) return NULL;
        pool->next = COMP_CTX.pools;
        COMP_CTX.pools = pool;
        for (int i = 0; i < COMP_POOL_SIZE; i++) {
            /* Use dismantle_scheduled as a link field for freelist */
            *((SymComponent**) &pool->objects[i]) = COMP_CTX.freelist;
            COMP_CTX.freelist = &pool->objects[i];
        }
    }
    
    SymComponent* c = COMP_CTX.freelist;
    COMP_CTX.freelist = *((SymComponent**) c);
    
    memset(c, 0, sizeof(SymComponent));
    c->id = COMP_CTX.next_id++;
    c->member_capacity = 8;
    c->members = malloc(c->member_capacity * sizeof(SymObj*));
    
    return c;
}

static void sym_component_pool_free(SymComponent* c) {
    *((SymComponent**) c) = COMP_CTX.freelist;
    COMP_CTX.freelist = c;
}

void sym_component_cleanup(void) {
    CompPool* p = COMP_CTX.pools;
    while (p) {
        CompPool* next = p->next;
        /* We don't free individual member lists here as they should 
         * have been freed by dismantle or thread-exit logic. 
         * In a robust implementation, we'd track active components. */
        free(p);
        p = next;
    }
    COMP_CTX.pools = NULL;
    COMP_CTX.freelist = NULL;
}

void sym_component_add_member(SymComponent* c, SymObj* obj) {
    if (!c || !obj) return;
    
    if (c->member_count >= c->member_capacity) {
        c->member_capacity *= 2;
        c->members = realloc(c->members, c->member_capacity * sizeof(SymObj*));
    }
    
    obj->comp = c;
    c->members[c->member_count++] = obj;
}

void sym_acquire_handle(SymComponent* c) {
    if (!c) return;
    c->handle_count++;
}

static void maybe_dismantle(SymComponent* c) {
    if (c->handle_count == 0 && c->tether_count == 0 && !c->dismantle_scheduled) {
        c->dismantle_scheduled = true;
        sym_dismantle_component(c);
    }
}

void sym_release_handle(SymComponent* c) {
    if (!c) return;
    c->handle_count--;
    maybe_dismantle(c);
}

SymTetherToken sym_tether_begin(SymComponent* c) {
    if (c) c->tether_count++;
    return (SymTetherToken){ .comp = c };
}

void sym_tether_end(SymTetherToken token) {
    if (token.comp) {
        token.comp->tether_count--;
        maybe_dismantle(token.comp);
    }
}

/* 
 * Internal symmetric teardown.
 * Cancel edges. When an object has no internal RC, it is freed.
 */
void sym_dismantle_component(SymComponent* c) {
    if (!c) return;
    
    /* 
     * In this model, we can bulk-free if we know all members.
     * The proposal suggests edge cancellation for robustness.
     */
    
    for (int i = 0; i < c->member_count; i++) {
        SymObj* obj = c->members[i];
        if (!obj || obj->freed) continue;
        
        /* 1. Cancel outgoing edges to other members */
        for (int j = 0; j < obj->ref_count; j++) {
            SymObj* target = obj->refs[j];
            if (target && target->comp == c) {
                target->internal_rc--;
            }
        }
        
        /* 2. Free data payload */
        if (obj->data) {
            /* In real runtime, this would be dec_ref(obj->data) 
             * or similar to trigger ASAP cleanup of payload. */
        }
        
        /* 3. Mark as freed */
        obj->freed = true;
        
        /* 4. Cleanup refs array */
        if (obj->refs && obj->refs != obj->inline_refs) {
            free(obj->refs);
        }
    }
    
    /* Final cleanup of component metadata */
    if (c->members) {
        free(c->members);
        c->members = NULL;
    }
    sym_component_pool_free(c);
}
