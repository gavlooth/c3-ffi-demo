/*
 * Dominator Analysis for CFG
 *
 * Implements the iterative dominator algorithm by Cooper, Harvey, and Kennedy.
 * Used for Static Symmetric RC to identify cycle entry points.
 */

#include "analysis.h"
#include <stdlib.h>
#include <string.h>

/* Compute post-order traversal of CFG */
static void dfs_postorder(CFGNode* node, bool* visited, CFGNode** order, size_t* idx) {
    visited[node->id] = true;
    for (size_t i = 0; i < node->succ_count; i++) {
        if (!visited[node->successors[i]->id]) {
            dfs_postorder(node->successors[i], visited, order, idx);
        }
    }
    order[(*idx)++] = node;
}

/*
 * Compute dominators for Control Flow Graph.
 */
// TESTED
void omni_compute_dominators(CFG* cfg) {
    if (!cfg || !cfg->entry) return;

    size_t num_nodes = cfg->node_count;

    /* Allocate temporary storage */
    CFGNode** postorder = malloc(num_nodes * sizeof(CFGNode*));
    bool* visited = calloc(num_nodes * 2, sizeof(bool)); /* IDs might be sparse */

    /* 1. Compute Reverse Post-Order (RPO) */
    size_t idx = 0;
    dfs_postorder(cfg->entry, visited, postorder, &idx);

    /* Map: NodeID -> RPO Index for fast intersection */
    int* rpo_map = malloc(num_nodes * 2 * sizeof(int));
    for (size_t i = 0; i < idx; i++) {
        rpo_map[postorder[i]->id] = (int)(idx - 1 - i);
    }

    /* 2. Initialize IDoms */
    for (size_t i = 0; i < num_nodes; i++) {
        cfg->nodes[i]->idom = NULL;
    }
    cfg->entry->idom = cfg->entry; /* Entry dominates itself for algorithm */

    /* 3. Iterative solver */
    bool changed = true;
    while (changed) {
        changed = false;

        /* Iterate in Reverse Post-Order (excluding entry which is RPO 0) */
        for (int i = (int)idx - 2; i >= 0; i--) {
            CFGNode* node = postorder[i];
            CFGNode* new_idom = NULL;

            /* Find first processed predecessor */
            for (size_t p = 0; p < node->pred_count; p++) {
                CFGNode* pred = node->predecessors[p];
                if (pred->idom != NULL) {
                    if (new_idom == NULL) {
                        new_idom = pred;
                    } else {
                        /* Intersect: find common ancestor in dominator tree */
                        CFGNode* finger1 = new_idom;
                        CFGNode* finger2 = pred;

                        while (finger1 != finger2) {
                            while (rpo_map[finger1->id] > rpo_map[finger2->id]) {
                                finger1 = finger1->idom;
                            }
                            while (rpo_map[finger2->id] > rpo_map[finger1->id]) {
                                finger2 = finger2->idom;
                            }
                        }
                        new_idom = finger1;
                    }
                }
            }

            if (node->idom != new_idom) {
                node->idom = new_idom;
                changed = true;
            }
        }
    }

    /* Fix entry's idom to NULL (strictly it has no immediate dominator) */
    cfg->entry->idom = NULL;

    free(postorder);
    free(visited);
    free(rpo_map);
}
