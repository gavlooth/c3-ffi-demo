/*
 * SCC Analysis for CFG
 *
 * Implements Tarjan's strongly connected components algorithm.
 * Used for Static Symmetric RC to identify cycles in the control flow.
 */

#include "analysis.h"
#include <stdlib.h>
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef struct TarjanState {
    int* index;
    int* lowlink;
    bool* on_stack;
    CFGNode** stack;
    int stack_top;
    int current_index;
    int next_scc_id;
} TarjanState;

static void strongconnect(CFGNode* v, TarjanState* state) {
    /* Set the depth index for v to the smallest unused index */
    state->index[v->id] = state->current_index;
    state->lowlink[v->id] = state->current_index;
    state->current_index++;
    state->stack[state->stack_top++] = v;
    state->on_stack[v->id] = true;

    /* Consider successors of v */
    for (size_t i = 0; i < v->succ_count; i++) {
        CFGNode* w = v->successors[i];
        if (state->index[w->id] == -1) {
            /* Successor w has not yet been visited; recurse on it */
            strongconnect(w, state);
            state->lowlink[v->id] = MIN(state->lowlink[v->id], state->lowlink[w->id]);
        } else if (state->on_stack[w->id]) {
            /* Successor w is in stack and hence in the current SCC */
            state->lowlink[v->id] = MIN(state->lowlink[v->id], state->index[w->id]);
        }
    }

    /* If v is a root node, pop the stack and generate an SCC */
    if (state->lowlink[v->id] == state->index[v->id]) {
        int scc_id = state->next_scc_id++;
        CFGNode* w;
        int count = 0;
        
        do {
            w = state->stack[--state->stack_top];
            state->on_stack[w->id] = false;
            w->scc_id = scc_id;
            count++;
        } while (w != v);
        
        /* If SCC has more than 1 node, or it's a self-loop, it's a real cycle */
        /* For Static Symmetric RC, we'll mark the 'entry' of the SCC */
        /* The entry is often the node with the highest dominator in the tree 
         * that is part of the SCC. For Tarjan's, the 'v' here is a good candidate.
         */
        if (count > 1) {
            v->is_scc_entry = true;
        } else {
            /* Check for self-loop */
            bool self_loop = false;
            for (size_t i = 0; i < v->succ_count; i++) {
                if (v->successors[i] == v) {
                    self_loop = true;
                    break;
                }
            }
            if (self_loop) {
                v->is_scc_entry = true;
            } else {
                v->scc_id = -1; /* Not part of a cycle */
            }
        }
    }
}

// TESTED
void omni_compute_scc(CFG* cfg) {
    if (!cfg) return;

    size_t num_nodes = cfg->node_count;
    TarjanState state;
    
    state.index = malloc(num_nodes * 2 * sizeof(int));
    state.lowlink = malloc(num_nodes * 2 * sizeof(int));
    state.on_stack = calloc(num_nodes * 2, sizeof(bool));
    state.stack = malloc(num_nodes * 2 * sizeof(CFGNode*));
    state.stack_top = 0;
    state.current_index = 0;
    state.next_scc_id = 0;

    for (size_t i = 0; i < num_nodes; i++) {
        state.index[cfg->nodes[i]->id] = -1;
        cfg->nodes[i]->scc_id = -1;
        cfg->nodes[i]->is_scc_entry = false;
    }

    for (size_t i = 0; i < num_nodes; i++) {
        if (state.index[cfg->nodes[i]->id] == -1) {
            strongconnect(cfg->nodes[i], &state);
        }
    }

    free(state.index);
    free(state.lowlink);
    free(state.on_stack);
    free(state.stack);
}
