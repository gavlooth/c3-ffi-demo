/*
 * effect_check.c - Effect Row Type Checking Implementation
 *
 * Provides compile-time analysis of algebraic effects.
 *
 * Phase 22: Algebraic Effects - Type Checking Extension
 */

#include "effect_check.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============== Effect Row Implementation ============== */

EffectRow* effect_row_new(void) {
    EffectRow* row = malloc(sizeof(EffectRow));
    if (!row) return NULL;
    row->effects = NULL;
    row->count = 0;
    row->capacity = 0;
    row->is_open = false;
    return row;
}

void effect_row_free(EffectRow* row) {
    if (!row) return;
    for (int i = 0; i < row->count; i++) {
        free(row->effects[i]);
    }
    free(row->effects);
    free(row);
}

void effect_row_add(EffectRow* row, const char* effect_name) {
    if (!row || !effect_name) return;

    /* Check if already present */
    if (effect_row_contains(row, effect_name)) return;

    /* Expand capacity if needed */
    if (row->count >= row->capacity) {
        int new_cap = row->capacity == 0 ? 4 : row->capacity * 2;
        char** new_effects = realloc(row->effects, new_cap * sizeof(char*));
        if (!new_effects) return;
        row->effects = new_effects;
        row->capacity = new_cap;
    }

    row->effects[row->count++] = strdup(effect_name);
}

bool effect_row_contains(EffectRow* row, const char* effect_name) {
    if (!row || !effect_name) return false;
    for (int i = 0; i < row->count; i++) {
        if (strcmp(row->effects[i], effect_name) == 0) {
            return true;
        }
    }
    return false;
}

EffectRow* effect_row_union(EffectRow* a, EffectRow* b) {
    EffectRow* result = effect_row_new();
    if (!result) return NULL;

    /* Add all effects from a */
    if (a) {
        for (int i = 0; i < a->count; i++) {
            effect_row_add(result, a->effects[i]);
        }
        result->is_open = a->is_open;
    }

    /* Add effects from b that aren't already present */
    if (b) {
        for (int i = 0; i < b->count; i++) {
            effect_row_add(result, b->effects[i]);
        }
        if (b->is_open) result->is_open = true;
    }

    return result;
}

bool effect_row_subset(EffectRow* a, EffectRow* b) {
    if (!a) return true;  /* Empty is subset of everything */
    if (!b) return a->count == 0;  /* Non-empty can't be subset of empty */

    for (int i = 0; i < a->count; i++) {
        if (!effect_row_contains(b, a->effects[i])) {
            return false;
        }
    }
    return true;
}

EffectRow* effect_row_difference(EffectRow* a, EffectRow* b) {
    EffectRow* result = effect_row_new();
    if (!result || !a) return result;

    for (int i = 0; i < a->count; i++) {
        if (!b || !effect_row_contains(b, a->effects[i])) {
            effect_row_add(result, a->effects[i]);
        }
    }
    return result;
}

char* effect_row_to_string(EffectRow* row) {
    if (!row || row->count == 0) {
        return strdup("{}");
    }

    /* Calculate total length */
    size_t len = 3;  /* "{ " + "}" */
    for (int i = 0; i < row->count; i++) {
        len += strlen(row->effects[i]) + 2;  /* ", " */
    }
    if (row->is_open) len += 4;  /* "..." */

    char* result = malloc(len);
    if (!result) return strdup("{}");

    char* p = result;
    p += sprintf(p, "{");
    for (int i = 0; i < row->count; i++) {
        if (i > 0) p += sprintf(p, ", ");
        p += sprintf(p, "%s", row->effects[i]);
    }
    if (row->is_open) p += sprintf(p, ", ...");
    sprintf(p, "}");

    return result;
}

/* ============== Effect Context Implementation ============== */

EffectContext* effect_context_new(EffectContext* parent) {
    EffectContext* ctx = malloc(sizeof(EffectContext));
    if (!ctx) return NULL;
    ctx->current_handler = NULL;
    ctx->required_effects = NULL;
    ctx->in_pure_context = false;
    ctx->handler_depth = parent ? parent->handler_depth : 0;
    ctx->parent = parent;
    return ctx;
}

void effect_context_free(EffectContext* ctx) {
    if (!ctx) return;
    if (ctx->current_handler) effect_row_free(ctx->current_handler);
    if (ctx->required_effects) effect_row_free(ctx->required_effects);
    free(ctx);
}

EffectContext* effect_context_push_handler(EffectContext* ctx, EffectRow* handled) {
    EffectContext* new_ctx = effect_context_new(ctx);
    if (!new_ctx) return ctx;
    new_ctx->current_handler = handled;
    new_ctx->handler_depth = (ctx ? ctx->handler_depth : 0) + 1;
    return new_ctx;
}

EffectContext* effect_context_pop_handler(EffectContext* ctx) {
    if (!ctx || !ctx->parent) return ctx;
    EffectContext* parent = ctx->parent;
    ctx->parent = NULL;  /* Detach to prevent double-free */
    effect_context_free(ctx);
    return parent;
}

/* ============== Effect Checking Implementation ============== */

/*
 * Check if an effect is handled by any enclosing handler
 */
static bool effect_is_handled(EffectContext* ctx, const char* effect_name) {
    while (ctx) {
        if (ctx->current_handler && effect_row_contains(ctx->current_handler, effect_name)) {
            return true;
        }
        ctx = ctx->parent;
    }
    return false;
}

/*
 * Collect effects handled by all enclosing handlers
 */
static EffectRow* effect_collect_handlers(EffectContext* ctx) {
    EffectRow* result = effect_row_new();
    while (ctx) {
        if (ctx->current_handler) {
            EffectRow* merged = effect_row_union(result, ctx->current_handler);
            effect_row_free(result);
            result = merged;
        }
        ctx = ctx->parent;
    }
    return result;
}

/*
 * Extract effect name from a perform call
 * (perform 'EffectName payload) -> "EffectName"
 */
static const char* extract_perform_effect(OmniValue* args) {
    if (!args || omni_is_nil(args)) return NULL;

    OmniValue* name_arg = omni_car(args);
    if (!name_arg) return NULL;

    /* Handle quoted symbol: (quote Foo) */
    if (omni_is_cell(name_arg)) {
        OmniValue* head = omni_car(name_arg);
        if (head && omni_is_sym(head) && strcmp(head->str_val, "quote") == 0) {
            OmniValue* quoted = omni_car(omni_cdr(name_arg));
            if (quoted && omni_is_sym(quoted)) {
                return quoted->str_val;
            }
        }
    }

    /* Handle bare symbol */
    if (omni_is_sym(name_arg)) {
        return name_arg->str_val;
    }

    return NULL;
}

/*
 * Check effects in an expression
 */
EffectRow* effect_check_expr(EffectContext* ctx, OmniValue* expr) {
    EffectRow* effects = effect_row_new();
    if (!expr || !ctx) return effects;

    /* Handle different expression types */
    if (omni_is_cell(expr)) {
        OmniValue* head = omni_car(expr);
        OmniValue* args = omni_cdr(expr);

        if (head && omni_is_sym(head)) {
            const char* name = head->str_val;

            /* perform - adds effect to row */
            if (strcmp(name, "perform") == 0) {
                const char* effect_name = extract_perform_effect(args);
                if (effect_name) {
                    effect_row_add(effects, effect_name);

                    /* Check if handled */
                    if (!effect_is_handled(ctx, effect_name)) {
                        effect_warn_unhandled(effect_name, "perform");
                    }

                    /* Check purity */
                    if (ctx->in_pure_context) {
                        effect_warn_purity(effect_name, "perform");
                    }
                }
            }
            /* raise - special case of Fail effect */
            else if (strcmp(name, "raise") == 0) {
                effect_row_add(effects, EFFECT_NAME_FAIL);
                if (!effect_is_handled(ctx, EFFECT_NAME_FAIL)) {
                    effect_warn_unhandled(EFFECT_NAME_FAIL, "raise");
                }
            }
            /* yield - Yield effect */
            else if (strcmp(name, "yield") == 0) {
                effect_row_add(effects, EFFECT_NAME_YIELD);
            }
            /* handle - analyze body with handler context */
            else if (strcmp(name, "handle") == 0) {
                /* Extract handled effects from clauses */
                EffectRow* handled = effect_row_new();
                OmniValue* clauses = omni_cdr(args);  /* Skip body */
                while (clauses && omni_is_cell(clauses)) {
                    OmniValue* clause = omni_car(clauses);
                    if (clause && omni_is_cell(clause)) {
                        OmniValue* effect_name_val = omni_car(clause);
                        if (effect_name_val && omni_is_sym(effect_name_val)) {
                            effect_row_add(handled, effect_name_val->str_val);
                        }
                    }
                    clauses = omni_cdr(clauses);
                }

                /* Check body with handler context */
                EffectContext* handler_ctx = effect_context_push_handler(ctx, handled);
                OmniValue* body = omni_car(args);
                EffectRow* body_effects = effect_check_expr(handler_ctx, body);

                /* Residual effects = body effects - handled effects */
                EffectRow* residual = effect_row_difference(body_effects, handled);
                EffectRow* merged = effect_row_union(effects, residual);
                effect_row_free(effects);
                effects = merged;

                effect_row_free(body_effects);
                effect_row_free(residual);
                effect_context_pop_handler(handler_ctx);
            }
            /* if/cond - merge branches */
            else if (strcmp(name, "if") == 0 || strcmp(name, "cond") == 0) {
                while (args && omni_is_cell(args)) {
                    EffectRow* branch = effect_check_expr(ctx, omni_car(args));
                    EffectRow* merged = effect_row_union(effects, branch);
                    effect_row_free(effects);
                    effect_row_free(branch);
                    effects = merged;
                    args = omni_cdr(args);
                }
            }
            /* Other forms - check subexpressions */
            else {
                while (args && omni_is_cell(args)) {
                    EffectRow* sub = effect_check_expr(ctx, omni_car(args));
                    EffectRow* merged = effect_row_union(effects, sub);
                    effect_row_free(effects);
                    effect_row_free(sub);
                    effects = merged;
                    args = omni_cdr(args);
                }
            }
        }
    }

    return effects;
}

EffectRow* effect_check_handle(EffectContext* ctx, OmniValue* body, OmniValue* clauses) {
    /* Extract handled effects */
    EffectRow* handled = effect_row_new();
    while (clauses && omni_is_cell(clauses)) {
        OmniValue* clause = omni_car(clauses);
        if (clause && omni_is_cell(clause)) {
            OmniValue* name = omni_car(clause);
            if (name && omni_is_sym(name)) {
                effect_row_add(handled, name->str_val);
            }
        }
        clauses = omni_cdr(clauses);
    }

    /* Check body with handler context */
    EffectContext* handler_ctx = effect_context_push_handler(ctx, handled);
    EffectRow* body_effects = effect_check_expr(handler_ctx, body);

    /* Compute residual (unhandled) effects */
    EffectRow* residual = effect_row_difference(body_effects, handled);

    effect_row_free(body_effects);
    effect_context_pop_handler(handler_ctx);

    return residual;
}

EffectRow* effect_infer_function(EffectContext* ctx, OmniValue* params, OmniValue* body) {
    (void)params;  /* TODO: Use for purity annotations */
    return effect_check_expr(ctx, body);
}

/* ============== Warning Functions ============== */

void effect_warn_unhandled(const char* effect_name, const char* source) {
    fprintf(stderr, "Warning: Effect '%s' may not be handled (%s)\n",
            effect_name, source);
}

void effect_warn_purity(const char* effect_name, const char* source) {
    fprintf(stderr, "Warning: Effect '%s' in pure context (%s)\n",
            effect_name, source);
}
