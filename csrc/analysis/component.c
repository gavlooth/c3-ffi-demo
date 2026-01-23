/*
 * Component Analysis for Static Symmetric RC
 *
 * Groups SCCs into "Components" and identifies external handles.
 */

#include "analysis.h"
#include <stdlib.h>
#include <string.h>

// REVIEWED:NAIVE
static ComponentInfo* find_or_create_component(AnalysisContext* ctx, int scc_id) {
    for (ComponentInfo* c = ctx->components; c; c = c->next) {
        if (c->scc_id == scc_id) return c;
    }
    
    ComponentInfo* c = malloc(sizeof(ComponentInfo));
    c->component_id = ctx->next_component_id++;
    c->scc_id = scc_id;
    c->handle_count = 0;
    c->handle_capacity = 4;
    c->handles = malloc(c->handle_capacity * sizeof(char*));
    c->is_static = true; /* Assume static until proven escaping */
    c->next = ctx->components;
    ctx->components = c;
    return c;
}

void omni_analyze_components(AnalysisContext* ctx, CFG* cfg) {
    if (!ctx || !cfg) return;

    /* 1. Ensure SCCs are computed */
    omni_compute_scc(cfg);

    /* 2. Group SCCs into components and find handles */
    for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
        if (o->shape != SHAPE_CYCLIC) continue;

        VarUsage* u = omni_get_var_usage(ctx, o->name);
        if (!u) continue;

        /* Find node by position */
        CFGNode* def_node = NULL;
// REVIEWED:NAIVE
        for (size_t i = 0; i < cfg->node_count; i++) {
            if (u->def_pos >= cfg->nodes[i]->position_start && 
                u->def_pos <= cfg->nodes[i]->position_end) {
                def_node = cfg->nodes[i];
                break;
            }
        }

        if (def_node && def_node->scc_id != -1) {
            /* This variable is a handle to a component */
            ComponentInfo* comp = find_or_create_component(ctx, def_node->scc_id);
            
            /* Add variable as a handle */
            if (comp->handle_count >= comp->handle_capacity) {
                comp->handle_capacity *= 2;
                comp->handles = realloc(comp->handles, comp->handle_capacity * sizeof(char*));
            }
            comp->handles[comp->handle_count++] = strdup(o->name);
            
            /* If any handle escapes, the component is not static */
            EscapeClass escape = omni_get_escape_class(ctx, o->name);
            if (escape != ESCAPE_NONE) {
                comp->is_static = false;
            }
        }
    }
}
