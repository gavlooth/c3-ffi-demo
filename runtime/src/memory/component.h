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
    int handle_count;       /* External owning references (ASAP managed) */
    int tether_count;       /* Active scope tethers (Zero-cost locks) */
    
    struct SymObj** members; /* All objects in this island */
    int member_count;
    int member_capacity;
    
    bool dismantle_scheduled;
};

/* Component Lifecycle */
SymComponent* sym_component_new(void);
void sym_component_add_member(SymComponent* c, SymObj* obj);
void sym_component_cleanup(void);

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

#endif /* OMNI_COMPONENT_H */
