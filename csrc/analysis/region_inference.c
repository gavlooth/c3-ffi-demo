/*
 * region_inference.c - Advanced Lifetime-Based Region Inference
 *
 * Implements region inference for Region-RC memory management:
 * 1. Build Variable Interaction Graph (VIG)
 * 2. Find Connected Components (Candidate Regions)
 * 3. Liveness Analysis for each Component
 * 4. Dominator Placement for region_create/destroy
 */

#define _POSIX_C_SOURCE 200809L
#include "region_inference.h"
#include "analysis.h"
#include "../ast/ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* ============================================================================
 * Variable Interaction Graph (VIG)
 * ============================================================================
 * Two variables are connected if they interact via:
 * - Data flow: v = u (assignment)
 * - Aliasing: f(u, v) (both arguments to same call)
 * - Structural: v = u.field (field access)
 */

typedef struct VIGNode {
    char* var_name;                 /* Variable name */
    char** neighbors;               /* Adjacent variables */
    size_t neighbor_count;
    size_t neighbor_capacity;
    int component_id;               /* Assigned component (-1 = unassigned) */
    bool visited;                   /* For BFS/DFS */
    int first_def;                  /* First definition position */
    int last_use;                   /* Last use position */
    struct VIGNode* next;
} VIGNode;

typedef struct {
    VIGNode* nodes;
    size_t node_count;
} VariableInteractionGraph;

/* Initialize VIG */
static void vig_init(VariableInteractionGraph* vig) {
    vig->nodes = NULL;
    vig->node_count = 0;
}

/* Find or create a VIG node */
static VIGNode* vig_get_node(VariableInteractionGraph* vig, const char* var_name) {
    for (VIGNode* n = vig->nodes; n; n = n->next) {
        if (strcmp(n->var_name, var_name) == 0) {
            return n;
        }
    }

    /* Create new node */
    VIGNode* node = calloc(1, sizeof(VIGNode));
    node->var_name = strdup(var_name);
    node->neighbor_capacity = 4;
    node->neighbors = malloc(sizeof(char*) * node->neighbor_capacity);
    node->component_id = -1;
    node->visited = false;
    node->first_def = -1;
    node->last_use = -1;

    node->next = vig->nodes;
    vig->nodes = node;
    vig->node_count++;

    return node;
}

/* Add undirected edge between two variables */
static void vig_add_edge(VariableInteractionGraph* vig, const char* u, const char* v) {
    if (strcmp(u, v) == 0) return; /* Skip self-loops */

    VIGNode* nu = vig_get_node(vig, u);
    VIGNode* nv = vig_get_node(vig, v);

    /* Add v to u's neighbors */
    bool found = false;
    for (size_t i = 0; i < nu->neighbor_count; i++) {
        if (strcmp(nu->neighbors[i], v) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        if (nu->neighbor_count >= nu->neighbor_capacity) {
            nu->neighbor_capacity *= 2;
            nu->neighbors = realloc(nu->neighbors, sizeof(char*) * nu->neighbor_capacity);
        }
        nu->neighbors[nu->neighbor_count++] = strdup(v);
    }

    /* Add u to v's neighbors (undirected) */
    found = false;
    for (size_t i = 0; i < nv->neighbor_count; i++) {
        if (strcmp(nv->neighbors[i], u) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        if (nv->neighbor_count >= nv->neighbor_capacity) {
            nv->neighbor_capacity *= 2;
            nv->neighbors = realloc(nv->neighbors, sizeof(char*) * nv->neighbor_capacity);
        }
        nv->neighbors[nv->neighbor_count++] = strdup(u);
    }
}

/* Free VIG */
static void vig_free(VariableInteractionGraph* vig) {
    VIGNode* node = vig->nodes;
    while (node) {
        VIGNode* next = node->next;
        free(node->var_name);
        for (size_t i = 0; i < node->neighbor_count; i++) {
            free(node->neighbors[i]);
        }
        free(node->neighbors);
        free(node);
        node = next;
    }
}

/* ============================================================================
 * Step 1: Build Variable Interaction Graph
 * ============================================================================ */

/* Recursively analyze expression to find variable interactions */
static void analyze_expr_for_interactions(VariableInteractionGraph* vig,
                                          OmniValue* expr,
                                          AnalysisContext* ctx) {
    if (!expr) return;

    switch (expr->tag) {
        case OMNI_SYM: {
            /* Variable reference */
            break;
        }

        case OMNI_CELL: {
            /* (cons car cdr) - car and cdr are related */
            OmniValue* car = expr->cell.car;
            OmniValue* cdr = expr->cell.cdr;

            analyze_expr_for_interactions(vig, car, ctx);
            analyze_expr_for_interactions(vig, cdr, ctx);

            /* If both are symbols (variables), they interact via construction */
            if (car && cdr && car->tag == OMNI_SYM && cdr->tag == OMNI_SYM) {
                vig_add_edge(vig, car->str_val, cdr->str_val);
            }
            break;
        }

        case OMNI_LAMBDA:
        case OMNI_REC_LAMBDA: {
            /* Lambda body - parameters are related */
            OmniValue* params = expr->lambda.params;
            OmniValue* body = expr->lambda.body;

            /* Add edges between all parameters (they form a closure) */
            if (params && params->tag == OMNI_CELL) {
                OmniValue* p = params;
                while (p && p->tag == OMNI_CELL) {
                    OmniValue* p2 = p->cell.cdr;
                    while (p2 && p2->tag == OMNI_CELL) {
                        if (p->cell.car && p2->cell.car &&
                            p->cell.car->tag == OMNI_SYM && p2->cell.car->tag == OMNI_SYM) {
                            vig_add_edge(vig, p->cell.car->str_val, p2->cell.car->str_val);
                        }
                        p2 = p2->cell.cdr;
                    }
                    p = p->cell.cdr;
                }
            }

            analyze_expr_for_interactions(vig, body, ctx);
            break;
        }

        case OMNI_NIL:
        case OMNI_INT:
        case OMNI_CHAR:
        case OMNI_FLOAT:
        case OMNI_NOTHING:
        case OMNI_PRIM:
        case OMNI_CODE:
        case OMNI_ERROR:
            /* Leaf nodes - no interactions */
            break;

        default:
            /* For other types, recursively analyze */
            if (expr->tag == OMNI_CELL) {
                analyze_expr_for_interactions(vig, expr->cell.car, ctx);
                analyze_expr_for_interactions(vig, expr->cell.cdr, ctx);
            }
            break;
    }
}

/* Step 1: Build Variable Interaction Graph */
static void build_interaction_graph(CompilerCtx* compiler_ctx,
                                     VariableInteractionGraph* vig) {
    AnalysisContext* ctx = compiler_ctx->analysis;

    /* Import existing variable usage info */
    VarUsage* vu = ctx->var_usages;
    while (vu) {
        VIGNode* node = vig_get_node(vig, vu->name);
        node->first_def = vu->def_pos;
        node->last_use = vu->last_use;
        vu = vu->next;
    }

    /* Analyze all expressions to find interactions */
    /* For now, we use the existing stub that connects all variables */
    /* TODO: Full AST traversal for precise interaction detection */

    /* Fallback: Connect all variables (safe but conservative) */
    VIGNode* n1 = vig->nodes;
    while (n1) {
        VIGNode* n2 = n1->next;
        while (n2) {
            vig_add_edge(vig, n1->var_name, n2->var_name);
            n2 = n2->next;
        }
        n1 = n1->next;
    }
}

/* ============================================================================
 * Step 2: Find Connected Components (Candidate Regions)
 * ============================================================================ */

static void find_connected_components(VariableInteractionGraph* vig) {
    int component_id = 0;

    for (VIGNode* node = vig->nodes; node; node = node->next) {
        if (!node->visited) {
            /* BFS to find all nodes in this component */
            VIGNode** queue = malloc(sizeof(VIGNode*) * vig->node_count);
            int queue_head = 0;
            int queue_tail = 0;

            queue[queue_tail++] = node;
            node->visited = true;
            node->component_id = component_id;

            while (queue_head < queue_tail) {
                VIGNode* current = queue[queue_head++];

                /* Visit all neighbors */
                for (size_t i = 0; i < current->neighbor_count; i++) {
                    VIGNode* neighbor = vig_get_node(vig, current->neighbors[i]);
                    if (!neighbor->visited) {
                        neighbor->visited = true;
                        neighbor->component_id = component_id;
                        queue[queue_tail++] = neighbor;
                    }
                }
            }

            free(queue);
            component_id++;
        }
    }
}

/* ============================================================================
 * Step 3: Liveness Analysis for Each Component
 * ============================================================================ */

typedef struct ComponentLiveness {
    int component_id;
    int start_pos;                /* Earliest def in component */
    int end_pos;                  /* Latest last-use in component */
    char** variables;             /* Variables in component */
    size_t var_count;
    size_t var_capacity;
    struct ComponentLiveness* next;
} ComponentLiveness;

static ComponentLiveness* compute_component_liveness(VariableInteractionGraph* vig,
                                                      AnalysisContext* ctx) {
    ComponentLiveness* components = NULL;

    /* Group variables by component */
    for (VIGNode* node = vig->nodes; node; node = node->next) {
        int comp_id = node->component_id;
        if (comp_id < 0) continue;

        /* Find existing component record */
        ComponentLiveness* comp = NULL;
        for (ComponentLiveness* c = components; c; c = c->next) {
            if (c->component_id == comp_id) {
                comp = c;
                break;
            }
        }

        /* Create new component record if needed */
        if (!comp) {
            comp = calloc(1, sizeof(ComponentLiveness));
            comp->component_id = comp_id;
            comp->start_pos = INT_MAX;
            comp->end_pos = -1;
            comp->var_capacity = 4;
            comp->variables = malloc(sizeof(char*) * comp->var_capacity);

            comp->next = components;
            components = comp;
        }

        /* Update liveness bounds */
        if (node->first_def >= 0 && node->first_def < comp->start_pos) {
            comp->start_pos = node->first_def;
        }
        if (node->last_use > comp->end_pos) {
            comp->end_pos = node->last_use;
        }

        /* Add variable to component */
        if (comp->var_count >= comp->var_capacity) {
            comp->var_capacity *= 2;
            comp->variables = realloc(comp->variables, sizeof(char*) * comp->var_capacity);
        }
        comp->variables[comp->var_count++] = strdup(node->var_name);
    }

    return components;
}

/* Free component liveness list */
static void free_component_liveness(ComponentLiveness* comps) {
    while (comps) {
        ComponentLiveness* next = comps->next;
        for (size_t i = 0; i < comps->var_count; i++) {
            free(comps->variables[i]);
        }
        free(comps->variables);
        free(comps);
        comps = next;
    }
}

/* ============================================================================
 * Step 4: Dominator Placement
 * ============================================================================ */

/* Store region placement information in AnalysisContext */
static void place_region_boundaries(CompilerCtx* compiler_ctx,
                                     ComponentLiveness* components) {
    AnalysisContext* ctx = compiler_ctx->analysis;

    for (ComponentLiveness* comp = components; comp; comp = comp->next) {
        /* Create a region for this component */
        char region_name[64];
        snprintf(region_name, sizeof(region_name), "region_%d", comp->component_id);

        RegionInfo* region = omni_region_new(ctx, region_name);
        if (region) {
            region->start_pos = comp->start_pos;
            region->end_pos = comp->end_pos;

            /* Add all variables to this region */
            for (size_t i = 0; i < comp->var_count; i++) {
                omni_region_add_var(ctx, comp->variables[i]);
            }
        }
    }
}

/* ============================================================================
 * Main Entry Point
 * ============================================================================ */

void infer_regions(CompilerCtx* ctx) {
    if (!ctx || !ctx->analysis) return;

    VariableInteractionGraph vig;
    vig_init(&vig);

    /* Step 1: Build Variable Interaction Graph */
    build_interaction_graph(ctx, &vig);

    /* Step 2: Find Connected Components */
    find_connected_components(&vig);

    /* Step 3: Liveness Analysis */
    ComponentLiveness* components = compute_component_liveness(&vig, ctx->analysis);

    /* Step 4: Dominator Placement */
    place_region_boundaries(ctx, components);

    /* Cleanup */
    free_component_liveness(components);
    vig_free(&vig);
}
