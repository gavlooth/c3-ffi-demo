/*
 * Tether Elision Optimization
 *
 * Removes redundant tether_begin/end calls when a strong handle
 * statically dominates the scope.
 */

#include "analysis.h"
#include <stdlib.h>
#include <string.h>

static TetherPoint* find_matching_exit(TetherPoint* entry) {
    if (!entry || !entry->is_entry) return NULL;
    
    TetherPoint* curr = entry->next;
    while (curr) {
        if (!curr->is_entry && strcmp(curr->tethered_var, entry->tethered_var) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

void omni_optimize_tethers(AnalysisContext* ctx, CFG* cfg) {
    if (!ctx || !cfg) return;

    /* 1. Ensure components are analyzed */
    omni_analyze_components(ctx, cfg);

    /* 2. Process each tether entry */
    for (TetherPoint* tp = ctx->tethers; tp; tp = tp->next) {
        if (!tp->is_entry || tp->elided) continue;

        /* Find matching exit to determine scope */
        TetherPoint* exit = find_matching_exit(tp);
        if (!exit) continue;

        /* Find component for this tethered var */
        ComponentInfo* comp = NULL;
        for (ComponentInfo* ci = ctx->components; ci; ci = ci->next) {
            /* Check if this var is a handle or member of this component */
            /* In simple model, we check if it's cyclic shape */
            OwnerInfo* oi = omni_get_owner_info(ctx, tp->tethered_var);
            if (oi && oi->shape == SHAPE_CYCLIC) {
                /* For now, we assume SCC IDs match. 
                 * More robust: check if tethered_var's node is in ci->scc_id */
                comp = ci; 
                break;
            }
        }

        if (comp) {
            /* Check if any handle to this component dominates the scope [tp->pos, exit->pos] */
            for (size_t i = 0; i < comp->handle_count; i++) {
                const char* handle_name = comp->handles[i];
                VarUsage* u = omni_get_var_usage(ctx, handle_name);
                
                if (u && u->def_pos < tp->position && u->last_use > exit->position) {
                    /* Handle is live across the entire tether scope! */
                    tp->elided = true;
                    exit->elided = true;
                    break;
                }
            }
        }
    }
}
