/*
 * Static Symmetric RC Analysis
 *
 * This pass identifies SCCs that can be collected statically using ASAP
 * instead of falling back to runtime Symmetric RC.
 */

#include "analysis.h"
#include <stdlib.h>
#include <string.h>

/* 
 * Helper to find CFG node by program position.
 */
static CFGNode* find_node_by_pos(CFG* cfg, int pos) {
    for (size_t i = 0; i < cfg->node_count; i++) {
        if (pos >= cfg->nodes[i]->position_start && pos <= cfg->nodes[i]->position_end) {
            return cfg->nodes[i];
        }
    }
    return NULL;
}

/* 
 * Identify which variable "owns" the SCC.
 * In a dominator-based lifetime model, the dominator of the SCC entry
 * is the primary candidate for ownership.
 */
void omni_analyze_static_symmetric(AnalysisContext* ctx, CFG* cfg) {
    if (!ctx || !cfg) return;

    /* 1. Ensure dominators and SCCs are computed */
    omni_compute_dominators(cfg);
    omni_compute_scc(cfg);

    /* 2. Scan for variables that own cyclic structures */
    for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
        /* Only consider cyclic shapes that we must free */
        if (o->shape != SHAPE_CYCLIC || !o->must_free) continue;

        /* Get variable usage to find its last use */
        VarUsage* u = omni_get_var_usage(ctx, o->name);
        if (!u) continue;

        /* Find the node where this variable dies */
        CFGNode* death_node = find_node_by_pos(cfg, u->last_use);
        if (!death_node) continue;

        /* 
         * If the variable dies at the exit of an SCC, 
         * and it was the "dominator" of that SCC, 
         * then we can statically collect the SCC here.
         */
        if (death_node->scc_id != -1) {
            /* Variable dies inside a cycle. 
             * Check if it was defined before the cycle or at entry.
             */
            EscapeClass escape = omni_get_escape_class(ctx, o->name);
            
            /* If it doesn't escape the function, it's a candidate for static collection */
            if (escape == ESCAPE_NONE) {
                o->is_static_scc = true;
                o->must_free = true;
            }
        }
    }
}
