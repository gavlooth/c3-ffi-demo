/*
 * effect.h - Algebraic Effects and Handlers for OmniLisp
 *
 * Provides a modern effect system that supersedes conditions/restarts:
 * - Effect types with typed payloads
 * - Handler blocks that intercept effects
 * - Resumable effects via delimited continuations
 * - Typed recovery protocols
 *
 * Built on top of the continuation infrastructure (continuation.h).
 *
 * Design inspired by:
 * - Koka (effect types, handlers)
 * - Eff (algebraic effects)
 * - OCaml 5 (effect handlers)
 * - "Algebraic Effects for Functional Programming" (Leijen, 2017)
 */

#ifndef OMNI_EFFECT_H
#define OMNI_EFFECT_H

#include <stdint.h>
#include <stdbool.h>
#include "memory/continuation.h"

/* Forward declarations */
#ifndef OMNI_OBJ_DECLARED
#define OMNI_OBJ_DECLARED
typedef struct Obj Obj;
#endif
typedef struct Effect Effect;
typedef struct EffectType EffectType;
typedef struct Handler Handler;
typedef struct HandlerClause HandlerClause;
typedef struct Resumption Resumption;
typedef struct RecoveryProtocol RecoveryProtocol;

/* ============================================================
 * Effect Types
 * ============================================================ */

/*
 * RecoveryMode - how a handler can use the resumption
 */
typedef enum {
    RECOVERY_ONE_SHOT,      /* Can resume at most once (default) */
    RECOVERY_MULTI_SHOT,    /* Can resume multiple times */
    RECOVERY_TAIL,          /* Tail-resumptive (no capture needed) */
    RECOVERY_ABORT,         /* Never resumes (like exceptions) */
} RecoveryMode;

/*
 * RecoveryProtocol - typed contract for effect recovery
 *
 * Defines what the handler must provide when resuming.
 */
struct RecoveryProtocol {
    const char* name;           /* Protocol name (for diagnostics) */
    RecoveryMode mode;          /* How resumption can be used */
    Obj* input_type;            /* Expected type when resuming (or NULL) */
    Obj* output_type;           /* What perform returns (or NULL) */
    const char* description;    /* Human-readable description */
};

/*
 * EffectType - definition of an effect kind
 *
 * Example: (effect Ask : String -> Int)
 */
struct EffectType {
    const char* name;           /* Effect name (e.g., "Ask", "Emit", "Fail") */
    uint32_t id;                /* Unique effect ID */
    Obj* payload_type;          /* Type of the effect payload (or NULL for any) */
    RecoveryProtocol* recovery; /* Recovery protocol (or NULL for default) */
    EffectType* next;           /* Next in registry chain */
};

/*
 * Effect - a performed effect instance
 *
 * Created when `perform` is called.
 */
struct Effect {
    EffectType* type;           /* What kind of effect */
    Obj* payload;               /* Effect payload value */

    /* Source location (for diagnostics) */
    const char* source_file;
    int source_line;
    int source_col;
};

/* ============================================================
 * Resumptions
 * ============================================================ */

/*
 * Resumption - capability to continue a suspended computation
 *
 * Passed to the handler when an effect is performed.
 * Calling resume(k, v) continues the computation with v as the
 * result of `perform`.
 */
struct Resumption {
    Continuation* cont;         /* Captured delimited continuation */
    EffectType* effect_type;    /* Which effect this resumes */
    RecoveryMode mode;          /* One-shot, multi-shot, etc. */
    bool used;                  /* Has been invoked? (for one-shot) */
    uint32_t refcount;          /* Reference count */

    /* For diagnostics */
    Effect* original_effect;    /* The effect that created this */
};

/* ============================================================
 * Handlers
 * ============================================================ */

/*
 * HandlerClause - one effect clause in a handler
 *
 * Matches an effect type and provides a handler function.
 */
struct HandlerClause {
    EffectType* effect_type;    /* Which effect to handle */
    Obj* handler_fn;            /* Handler function: (payload, resume) -> result */
    HandlerClause* next;        /* Next clause in handler */
};

/*
 * Handler - an effect handler block
 *
 * Installed when entering a `handle` block.
 */
struct Handler {
    uint32_t id;                /* Unique handler ID */
    uint32_t prompt_tag;        /* Corresponding prompt tag */
    HandlerClause* clauses;     /* Effect clauses */
    Obj* return_clause;         /* Optional return clause: v -> result */
    Obj* env;                   /* Environment for handler functions */
    Handler* parent;            /* Enclosing handler (for searching) */
};

/* ============================================================
 * Effect Type Registry
 * ============================================================ */

/* Initialize the effect system */
void effect_init(void);

/* Shutdown and cleanup */
void effect_shutdown(void);

/* Register a new effect type */
EffectType* effect_type_register(
    const char* name,
    Obj* payload_type,
    RecoveryProtocol* recovery
);

/* Find effect type by name */
EffectType* effect_type_find(const char* name);

/* Find effect type by ID */
EffectType* effect_type_find_by_id(uint32_t id);

/* List all registered effect types */
EffectType* effect_type_list_all(void);

/* ============================================================
 * Handler Stack Management
 * ============================================================ */

/* Push a new handler onto the stack */
Handler* handler_push(HandlerClause* clauses, Obj* return_clause, Obj* env);

/* Pop the current handler */
void handler_pop(void);

/* Get the current handler */
Handler* handler_current(void);

/* Find handler for an effect type (searches up the stack) */
Handler* handler_find(EffectType* type);

/* Find handler clause for an effect type */
HandlerClause* handler_find_clause(Handler* h, EffectType* type);

/* ============================================================
 * Performing Effects
 * ============================================================ */

/*
 * Perform an effect, transferring control to the nearest handler.
 *
 * 1. Creates an Effect with the given type and payload
 * 2. Searches for a matching handler
 * 3. Captures the continuation up to the handler's prompt
 * 4. Creates a Resumption and passes it to the handler
 * 5. Returns the handler's result (or resumes with a value)
 *
 * Returns: the value passed to resume(), or handler's final result
 */
Obj* effect_perform(EffectType* type, Obj* payload);

/*
 * Perform an effect by name (convenience wrapper)
 */
Obj* effect_perform_named(const char* name, Obj* payload);

/*
 * Create an Effect object (without performing)
 */
Effect* effect_create(EffectType* type, Obj* payload);

/* Free an Effect object */
void effect_free(Effect* eff);

/* ============================================================
 * Resumption Operations
 * ============================================================ */

/*
 * Create a resumption from a captured continuation.
 * Used internally by effect_perform.
 */
Resumption* resumption_create(Continuation* cont, EffectType* type, Effect* eff);

/*
 * Resume a suspended computation with a value.
 *
 * For one-shot resumptions, this can only be called once.
 * For multi-shot, the resumption can be invoked multiple times.
 */
Obj* resumption_invoke(Resumption* r, Obj* value);

/*
 * Check if resumption is still valid (hasn't been used for one-shot)
 */
bool resumption_is_valid(Resumption* r);

/*
 * Clone a resumption (for multi-shot handlers)
 */
Resumption* resumption_clone(Resumption* r);

/*
 * Discard a resumption without resuming (abortive handler)
 */
void resumption_discard(Resumption* r);

/* Increment resumption reference count */
void resumption_inc_ref(Resumption* r);

/* Decrement resumption reference count (may free) */
void resumption_dec_ref(Resumption* r);

/* Create an Obj wrapper for a resumption */
Obj* mk_resumption_obj(Resumption* r);

/* ============================================================
 * Handler Block API
 * ============================================================ */

/*
 * Install a handler and evaluate body.
 *
 * (handle body
 *   (effect1 (payload resume) handler-body1)
 *   (effect2 (payload resume) handler-body2)
 *   (return (value) return-body))
 */
Obj* effect_handle(
    Obj* body,
    HandlerClause* clauses,
    Obj* return_clause,
    Obj* env
);

/*
 * Add a clause to a handler
 */
HandlerClause* handler_clause_add(
    HandlerClause* clauses,
    EffectType* type,
    Obj* handler_fn
);

/* Free handler clauses */
void handler_clauses_free(HandlerClause* clauses);

/* ============================================================
 * Built-in Effects
 * ============================================================ */

/* Initialize built-in effect types */
void effect_init_builtins(void);

/* Built-in effect types (initialized at startup) */
extern EffectType* EFFECT_FAIL;      /* Exception-like: never resumes */
extern EffectType* EFFECT_ASK;       /* Reader effect: ask for a value */
extern EffectType* EFFECT_EMIT;      /* Writer effect: emit a value */
extern EffectType* EFFECT_STATE;     /* State effect: get/put state */
extern EffectType* EFFECT_YIELD;     /* Generator effect */
extern EffectType* EFFECT_ASYNC;     /* Async/await effect */
extern EffectType* EFFECT_CHOICE;    /* Non-determinism */
extern EffectType* EFFECT_CONDITION; /* Condition signaling (Issue 14 P3) */

/* ============================================================
 * Recovery Protocol Helpers
 * ============================================================ */

/* Create a recovery protocol */
RecoveryProtocol* recovery_protocol_create(
    const char* name,
    RecoveryMode mode,
    Obj* input_type,
    Obj* output_type,
    const char* description
);

/* Free a recovery protocol */
void recovery_protocol_free(RecoveryProtocol* rp);

/* Validate recovery (check types, one-shot constraint) */
bool recovery_validate(Resumption* r, Obj* value);

/* ============================================================
 * Diagnostics
 * ============================================================ */

/* Print effect type info */
void effect_type_print(EffectType* type);

/* Print handler stack */
void handler_stack_print(void);

/* Print effect trace (for error messages) */
void effect_trace_print(void);

/* Get effect trace as string */
char* effect_trace_to_string(void);

/* ============================================================
 * Effect Tracing (Debug)
 * ============================================================ */

/* Enable/disable effect tracing */
void effect_trace_enable(bool enable);

/* Is tracing enabled? */
bool effect_trace_is_enabled(void);

/* Record a performed effect (called by effect_perform) */
void effect_trace_record(Effect* eff);

/* Clear effect trace */
void effect_trace_clear(void);

/* Mark a trace entry as handled (called when handler catches effect) */
void effect_trace_mark_handled(int index);

/* Get index of last recorded trace entry (-1 if empty) */
int effect_trace_last_index(void);

#endif /* OMNI_EFFECT_H */
