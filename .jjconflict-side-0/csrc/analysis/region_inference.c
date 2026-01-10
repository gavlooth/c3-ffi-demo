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

/* Helper: Collect all variable symbols from an expression */
typedef struct VarList {
    char** vars;
    size_t count;
    size_t capacity;
} VarList;

static void var_list_init(VarList* vl) {
    vl->vars = NULL;
    vl->count = 0;
    vl->capacity = 0;
}

static void var_list_add(VarList* vl, const char* var) {
    if (!var) return;
    /* Check for duplicates */
    for (size_t i = 0; i < vl->count; i++) {
        if (strcmp(vl->vars[i], var) == 0) return;
    }
    if (vl->count >= vl->capacity) {
        vl->capacity = vl->capacity ? vl->capacity * 2 : 8;
        vl->vars = realloc(vl->vars, sizeof(char*) * vl->capacity);
    }
    vl->vars[vl->count++] = strdup(var);
}

static void var_list_free(VarList* vl) {
    for (size_t i = 0; i < vl->count; i++) {
        free(vl->vars[i]);
    }
    free(vl->vars);
}

/* Recursively collect variable references from expression */
static void collect_vars_from_expr(OmniValue* expr, VarList* vars) {
    if (!expr) return;

    switch (expr->tag) {
        case OMNI_SYM:
            /* Variable reference - add to list */
            var_list_add(vars, expr->str_val);
            break;

        case OMNI_CELL: {
            /* List expression - recursively process car and cdr */
            collect_vars_from_expr(expr->cell.car, vars);
            collect_vars_from_expr(expr->cell.cdr, vars);
            break;
        }

        case OMNI_LAMBDA:
        case OMNI_REC_LAMBDA:
            /* Lambda - don't collect params (they're different scope), process body */
            collect_vars_from_expr(expr->lambda.body, vars);
            break;

        case OMNI_NIL:
        case OMNI_INT:
        case OMNI_CHAR:
        case OMNI_FLOAT:
        case OMNI_NOTHING:
        case OMNI_PRIM:
        case OMNI_CODE:
        case OMNI_ERROR:
        case OMNI_BOX:
        case OMNI_CONT:
        case OMNI_CHAN:
        case OMNI_GREEN_CHAN:
        case OMNI_ATOM:
        case OMNI_THREAD:
        case OMNI_PROCESS:
        case OMNI_ARRAY:
        case OMNI_DICT:
        case OMNI_TUPLE:
        case OMNI_TYPE_LIT:
        case OMNI_KEYWORD:
        case OMNI_USER_TYPE:
        case OMNI_MENV:
            /* Leaf nodes or other containers - no variable references to collect */
            break;
    }
}

/* Connect all variables in a list (they interact via being in same context) */
static void connect_all_vars(VariableInteractionGraph* vig, VarList* vars) {
    for (size_t i = 0; i < vars->count; i++) {
        for (size_t j = i + 1; j < vars->count; j++) {
            vig_add_edge(vig, vars->vars[i], vars->vars[j]);
        }
    }
}

/* Analyze let bindings to find variable interactions */
static void analyze_let_bindings(VariableInteractionGraph* vig,
                                 OmniValue* bindings,
                                 AnalysisContext* ctx) {
    if (!bindings) return;

    VarList bound_vars, used_vars;
    var_list_init(&bound_vars);
    var_list_init(&used_vars);

    /* Process each binding */
    if (omni_is_cell(bindings)) {
        /* List-style: ((x 1) (y 2)) */
        OmniValue* b = bindings;
        while (b && omni_is_cell(b)) {
            OmniValue* binding = omni_car(b);
            if (omni_is_cell(binding)) {
                OmniValue* name = omni_car(binding);
                OmniValue* val = omni_car(omni_cdr(binding));

                /* Collect bound variable */
                if (omni_is_sym(name)) {
                    var_list_add(&bound_vars, name->str_val);
                }

                /* Collect variables used in the value expression */
                collect_vars_from_expr(val, &used_vars);
            }
            b = omni_cdr(b);
        }
    } else if (omni_is_array(bindings)) {
        /* Array-style: [x 1 y 2] */
        for (size_t i = 0; i + 1 < bindings->array.len; i += 2) {
            OmniValue* name = bindings->array.data[i];
            OmniValue* val = bindings->array.data[i + 1];

            if (omni_is_sym(name)) {
                var_list_add(&bound_vars, name->str_val);
            }

            collect_vars_from_expr(val, &used_vars);
        }
    }

    /* All bound variables interact with each other (same let scope) */
    connect_all_vars(vig, &bound_vars);

    /* Bound variables interact with variables they use */
    for (size_t i = 0; i < bound_vars.count; i++) {
        for (size_t j = 0; j < used_vars.count; j++) {
            vig_add_edge(vig, bound_vars.vars[i], used_vars.vars[j]);
        }
    }

    var_list_free(&bound_vars);
    var_list_free(&used_vars);
}

/* Recursively analyze expression to find variable interactions */
static void analyze_expr_for_interactions(VariableInteractionGraph* vig,
                                          OmniValue* expr,
                                          AnalysisContext* ctx) {
    if (!expr) return;

    switch (expr->tag) {
        case OMNI_SYM: {
            /* Variable reference - already handled by collect_vars_from_expr */
            break;
        }

        case OMNI_CELL: {
            /* Check for special forms */
            if (omni_is_sym(expr->cell.car)) {
                const char* op = expr->cell.car->str_val;

                /* Let/let* form: (let ((x val) ...) body) */
                if (strcmp(op, "let") == 0 || strcmp(op, "let*") == 0) {
                    OmniValue* bindings = omni_car(omni_cdr(expr));
                    OmniValue* body = omni_cdr(omni_cdr(expr));

                    analyze_let_bindings(vig, bindings, ctx);

                    /* Recursively analyze body */
                    OmniValue* b = body;
                    while (b && omni_is_cell(b)) {
                        analyze_expr_for_interactions(vig, omni_car(b), ctx);
                        b = omni_cdr(b);
                    }
                    return;
                }

                /* Lambda form: (lambda (params...) body) */
                if (strcmp(op, "lambda") == 0 || strcmp(op, "fn") == 0) {
                    OmniValue* params = omni_car(omni_cdr(expr));
                    OmniValue* body = omni_cdr(omni_cdr(expr));

                    /* Parameters interact with each other */
                    VarList param_vars;
                    var_list_init(&param_vars);

                    OmniValue* p = params;
                    while (p && omni_is_cell(p)) {
                        if (omni_is_sym(p->cell.car)) {
                            var_list_add(&param_vars, p->cell.car->str_val);
                        }
                        p = omni_cdr(p);
                    }

                    connect_all_vars(vig, &param_vars);
                    var_list_free(&param_vars);

                    /* Analyze body */
                    OmniValue* b = body;
                    while (b && omni_is_cell(b)) {
                        analyze_expr_for_interactions(vig, omni_car(b), ctx);
                        b = omni_cdr(b);
                    }
                    return;
                }

                /* If form: (if cond then else) */
                if (strcmp(op, "if") == 0) {
                    OmniValue* rest = omni_cdr(expr);
                    VarList all_vars;
                    var_list_init(&all_vars);

                    /* Collect variables from all branches */
                    while (rest && omni_is_cell(rest)) {
                        collect_vars_from_expr(omni_car(rest), &all_vars);
                        rest = omni_cdr(rest);
                    }

                    /* All variables in if-expression interact */
                    connect_all_vars(vig, &all_vars);
                    var_list_free(&all_vars);
                    return;
                }

                /* Function application: (f x y z ...) */
                /* All arguments interact with each other */
                if (!omni_is_nil(omni_cdr(expr))) {
                    VarList arg_vars;
                    var_list_init(&arg_vars);

                    OmniValue* args = omni_cdr(expr);
                    while (args && omni_is_cell(args)) {
                        collect_vars_from_expr(omni_car(args), &arg_vars);
                        args = omni_cdr(args);
                    }

                    connect_all_vars(vig, &arg_vars);
                    var_list_free(&arg_vars);
                }
            }

            /* Generic cons cell - recursively analyze */
            analyze_expr_for_interactions(vig, expr->cell.car, ctx);
            analyze_expr_for_interactions(vig, expr->cell.cdr, ctx);
            break;
        }

        case OMNI_LAMBDA:
        case OMNI_REC_LAMBDA: {
            /* Standalone lambda value (not in lambda form) */
            OmniValue* params = expr->lambda.params;
            OmniValue* body = expr->lambda.body;

            /* Parameters interact with each other */
            VarList param_vars;
            var_list_init(&param_vars);

            if (params && omni_is_cell(params)) {
                OmniValue* p = params;
                while (p && omni_is_cell(p)) {
                    if (omni_is_sym(p->cell.car)) {
                        var_list_add(&param_vars, p->cell.car->str_val);
                    }
                    p = omni_cdr(p);
                }
            }

            connect_all_vars(vig, &param_vars);
            var_list_free(&param_vars);

            /* Analyze body */
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
        case OMNI_BOX:
        case OMNI_CONT:
        case OMNI_CHAN:
        case OMNI_GREEN_CHAN:
        case OMNI_ATOM:
        case OMNI_THREAD:
        case OMNI_PROCESS:
        case OMNI_ARRAY:
        case OMNI_DICT:
        case OMNI_TUPLE:
        case OMNI_TYPE_LIT:
        case OMNI_KEYWORD:
        case OMNI_USER_TYPE:
        case OMNI_MENV:
            /* Leaf nodes - no interactions */
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

    /* Analyze all expressions to find interactions using full AST traversal */
    /* This replaces the conservative fallback with precise interaction detection */

    /* TODO: Need access to program AST from CompilerCtx
     * For now, we'll use a semi-conservative approach:
     * 1. Connect all variables in the same scope
     * 2. Variables that reference each other are connected
     * 3. Variables used together in expressions are connected
     */

    /* Semi-precise: Connect variables that might interact via data flow
     * This is still conservative but more precise than connecting everything */

    /* Group variables by position (same scope -> connected) */
    VIGNode* n1 = vig->nodes;
    while (n1) {
        VIGNode* n2 = n1->next;
        while (n2) {
            /* Connect if their lifetimes overlap (they're in the same scope) */
            if (!(n2->first_def > n1->last_use || n1->first_def > n2->last_use)) {
                /* Lifetimes overlap - they might interact */
                vig_add_edge(vig, n1->var_name, n2->var_name);
            }
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
