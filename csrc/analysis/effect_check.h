/*
 * effect_check.h - Effect Row Type Checking
 *
 * Provides compile-time analysis of algebraic effects:
 * - Effect inference: determines which effects a function can perform
 * - Effect coverage: verifies handlers cover all possible effects
 * - Purity checking: ensures pure contexts don't call effectful functions
 *
 * Phase 22: Algebraic Effects - Type Checking Extension
 */

#ifndef OMNILISP_EFFECT_CHECK_H
#define OMNILISP_EFFECT_CHECK_H

#include "../ast/ast.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Effect Row Representation ============== */

/*
 * Effect types are identified by name strings.
 * Built-in effects: Fail, Ask, Emit, State, Yield, Async, Choice
 */

typedef struct EffectRow {
    char** effects;         /* Array of effect names */
    int count;              /* Number of effects */
    int capacity;           /* Allocated capacity */
    bool is_open;           /* Open row (can have more effects)? */
} EffectRow;

/*
 * Effect context during analysis
 */
typedef struct EffectContext {
    EffectRow* current_handler;   /* Effects handled by current handler */
    EffectRow* required_effects;  /* Effects that must be provided */
    bool in_pure_context;         /* Are we in a pure (no effects) context? */
    int handler_depth;            /* Nesting depth of handlers */
    struct EffectContext* parent; /* Parent context (for nesting) */
} EffectContext;

/* ============== Effect Row API ============== */

/*
 * Create a new empty effect row
 */
EffectRow* effect_row_new(void);

/*
 * Free an effect row
 */
void effect_row_free(EffectRow* row);

/*
 * Add an effect to a row (if not already present)
 */
void effect_row_add(EffectRow* row, const char* effect_name);

/*
 * Check if a row contains an effect
 */
bool effect_row_contains(EffectRow* row, const char* effect_name);

/*
 * Merge two effect rows (union)
 */
EffectRow* effect_row_union(EffectRow* a, EffectRow* b);

/*
 * Check if row A is subset of row B
 * (all effects in A are covered by B)
 */
bool effect_row_subset(EffectRow* a, EffectRow* b);

/*
 * Get effects in A that are not in B
 */
EffectRow* effect_row_difference(EffectRow* a, EffectRow* b);

/*
 * Convert row to string for error messages
 */
char* effect_row_to_string(EffectRow* row);

/* ============== Effect Context API ============== */

/*
 * Create a new effect context
 */
EffectContext* effect_context_new(EffectContext* parent);

/*
 * Free an effect context
 */
void effect_context_free(EffectContext* ctx);

/*
 * Push a handler context (entering a handle block)
 */
EffectContext* effect_context_push_handler(EffectContext* ctx, EffectRow* handled);

/*
 * Pop a handler context (leaving a handle block)
 */
EffectContext* effect_context_pop_handler(EffectContext* ctx);

/* ============== Effect Checking API ============== */

/*
 * Check effect usage in an expression
 *
 * Returns an EffectRow of all effects that may be performed.
 * Reports warnings for:
 * - Unhandled effects
 * - Effects in pure context
 */
EffectRow* effect_check_expr(EffectContext* ctx, OmniValue* expr);

/*
 * Check effect coverage in a handle block
 *
 * Verifies that all effects performed in body are handled.
 * Returns the residual effects (unhandled).
 */
EffectRow* effect_check_handle(EffectContext* ctx, OmniValue* body,
                                OmniValue* clauses);

/*
 * Infer effects of a function definition
 */
EffectRow* effect_infer_function(EffectContext* ctx, OmniValue* params,
                                  OmniValue* body);

/*
 * Report an unhandled effect warning
 */
void effect_warn_unhandled(const char* effect_name, const char* source);

/*
 * Report a purity violation warning
 */
void effect_warn_purity(const char* effect_name, const char* source);

/* ============== Built-in Effect Names ============== */

#define EFFECT_NAME_FAIL    "Fail"
#define EFFECT_NAME_ASK     "Ask"
#define EFFECT_NAME_EMIT    "Emit"
#define EFFECT_NAME_STATE   "State"
#define EFFECT_NAME_YIELD   "Yield"
#define EFFECT_NAME_ASYNC   "Async"
#define EFFECT_NAME_CHOICE  "Choice"
#define EFFECT_NAME_IO      "IO"

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_EFFECT_CHECK_H */
