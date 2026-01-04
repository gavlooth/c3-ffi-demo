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
    SymComponent* dismantle_queue; /* Linked list of components to free */
    uint32_t next_id;
} COMP_CTX = {NULL, NULL, NULL, 1};

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
    c->parent = NULL;
    
    return c;
}

static void sym_component_pool_free(SymComponent* c) {
    *((SymComponent**) c) = COMP_CTX.freelist;
    COMP_CTX.freelist = c;
}

/* Find root component with path compression */
SymComponent* sym_component_find(SymComponent* c) {
    if (!c) return NULL;
    if (c->parent == NULL) return c;
    
    /* Path compression */
    SymComponent* root = c;
    while (root->parent) {
        root = root->parent;
    }
    
    /* Second pass to compress */
    SymComponent* curr = c;
    while (curr->parent) {
        SymComponent* next = curr->parent;
        curr->parent = root;
        curr = next;
    }
    
    return root;
}

/* Merge two components */
void sym_component_union(SymComponent* a, SymComponent* b) {
    SymComponent* rootA = sym_component_find(a);
    SymComponent* rootB = sym_component_find(b);
    
    if (rootA == rootB) return;
    
    /* Merge B into A (arbitrary direction for now) */
    rootB->parent = rootA;
    
    /* Transfer counts */
    rootA->handle_count += rootB->handle_count;
    rootA->tether_count += rootB->tether_count;
    
    /* Transfer members */
    /* Ensure capacity */
    if (rootA->member_count + rootB->member_count > rootA->member_capacity) {
        int new_cap = rootA->member_capacity * 2;
        while (new_cap < rootA->member_count + rootB->member_count) {
            new_cap *= 2;
        }
        rootA->members = realloc(rootA->members, new_cap * sizeof(SymObj*));
        rootA->member_capacity = new_cap;
    }
    
    /* Copy members and update their component pointer */
    for (int i = 0; i < rootB->member_count; i++) {
        SymObj* obj = rootB->members[i];
        if (obj) {
            obj->comp = rootA;
            rootA->members[rootA->member_count++] = obj;
        }
    }
    
    /* Release B's member list memory and pool slot */
    if (rootB->members) {
        free(rootB->members);
        rootB->members = NULL;
    }
    /* We don't free rootB itself yet because 'parent' link is used for find() */
    /* Wait, if we use path compression, can we free B? 
     * No, internal objects might still point to B. 
     * BUT we updated obj->comp = rootA. 
     * However, there might be other SymComponent pointers (e.g. tether tokens) pointing to B.
     * So B must remain valid as a forwarding node until it's "dismantled".
     * Since B is now part of A, it will effectively be dismantled when A is.
     * We can't return B to the pool yet.
     */
}

void sym_component_cleanup(void) {
    /* Process remaining items in queue */
    sym_process_dismantle(0);
    
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
    SymComponent* root = sym_component_find(c);
    if (!root || !obj) return;
    
    if (root->member_count >= root->member_capacity) {
        root->member_capacity *= 2;
        root->members = realloc(root->members, root->member_capacity * sizeof(SymObj*));
    }
    
    obj->comp = root;
    root->members[root->member_count++] = obj;
}

void sym_acquire_handle(SymComponent* c) {
    SymComponent* root = sym_component_find(c);
    if (!root) return;
    root->handle_count++;
}

static void maybe_dismantle(SymComponent* c) {
    SymComponent* root = sym_component_find(c);
    if (!root) return;
    if (root->state == 0 && !root->dismantle_scheduled) {
        root->dismantle_scheduled = true;
        /* Lazy: Add to queue instead of direct call */
        *((SymComponent**) root) = COMP_CTX.dismantle_queue;
        COMP_CTX.dismantle_queue = root;
    }
}

void sym_process_dismantle(int batch_size) {
    int count = 0;
    while (COMP_CTX.dismantle_queue && (batch_size <= 0 || count < batch_size)) {
        SymComponent* c = COMP_CTX.dismantle_queue;
        COMP_CTX.dismantle_queue = *((SymComponent**) c);
        
        sym_dismantle_component(c);
        count++;
    }
}

void sym_release_handle(SymComponent* c) {
    SymComponent* root = sym_component_find(c);
    if (!root) return;
    assert(root->handle_count > 0);
    root->handle_count--;
    maybe_dismantle(root);
}

SymTetherToken sym_tether_begin(SymComponent* c) {
    SymComponent* root = sym_component_find(c);
    if (root) root->tether_count++;
    return (SymTetherToken){ .comp = root };
}

void sym_tether_end(SymTetherToken token) {
    SymComponent* root = sym_component_find(token.comp);
    if (root) {
        assert(root->tether_count > 0);
        root->tether_count--;
        maybe_dismantle(root);
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
