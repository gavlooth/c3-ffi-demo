/*
 * Component-Level Scope Tethering
 *
 * Evolution of Symmetric RC:
 * - Islands of cyclic data are grouped into a "Component".
 * - Liveness is tracked per-component (handle_count).
 * - Performance is improved via "Scope Tethers" (zero-cost access).
 */

#ifndef OMNI_COMPONENT_H
#define OMNI_COMPONENT_H

#include <stdint.h>
#include <stdbool.h>
#include "symmetric.h" /* For SymObj base if needed, or we redefine */

typedef struct SymComponent SymComponent;

/* 
 * SymComponent: Unit of cyclic reclamation.
 */
struct SymComponent {
    uint32_t id;
    
    /* 
     * Pack counts for O(1) atomics
     * Bits 0-31: handle_count (boundary refs)
     * Bits 32-63: tether_count (scoped tethers)
     */
    union {
        uint64_t state;
        struct {
            uint32_t handle_count;
            uint32_t tether_count;
        };
    };
    
    struct SymObj** members; /* All objects in this island */
    size_t member_count;
    size_t member_capacity;
    
    struct SymComponent* parent; /* Union-Find parent (NULL if root) */
    
    bool dismantle_scheduled;
};

/* Component Lifecycle */
SymComponent* sym_component_new(void);
void sym_component_add_member(SymComponent* c, SymObj* obj);
void sym_component_cleanup(void);

/* Union-Find Operations (Dynamic Merging) */
SymComponent* sym_component_find(SymComponent* c);
void sym_component_union(SymComponent* a, SymComponent* b);

/* Boundary Operations (ASAP called) */
void sym_acquire_handle(SymComponent* c);
void sym_release_handle(SymComponent* c);

/* Scope Tethering (Zero-cost access) */
typedef struct {
    SymComponent* comp;
} SymTetherToken;

SymTetherToken sym_tether_begin(SymComponent* c);
void sym_tether_end(SymTetherToken token);

/* Dismantling */
void sym_dismantle_component(SymComponent* c);
void sym_process_dismantle(int batch_size);

#endif /* OMNI_COMPONENT_H */
