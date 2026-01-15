/*
 * effect.c - Algebraic Effects and Handlers Implementation
 *
 * Implements the effect system on top of delimited continuations.
 */

#define _POSIX_C_SOURCE 200809L

#include "effect.h"
#include "../include/omni.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <setjmp.h>

/* Forward declarations from runtime.c */
extern Obj* call_closure(Obj* clos, Obj** args, int arg_count);
extern Obj* mk_resumption_obj(Resumption* r);
extern void inc_ref(Obj* x);
extern void dec_ref(Obj* x);

/* ============================================================
 * Global State
 * ============================================================ */

/* Effect type registry */
static EffectType* effect_registry = NULL;
static uint32_t next_effect_id = 1;
static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;

/* Thread-local handler stack */
static pthread_key_t handler_stack_key;
static pthread_once_t handler_key_once = PTHREAD_ONCE_INIT;

/* Effect tracing */
static bool trace_enabled = false;

/* Trace buffer for effect events */
#define TRACE_BUFFER_SIZE 256

typedef struct {
    const char* effect_name;    /* Effect type name (borrowed, not owned) */
    const char* source_file;    /* Source file (borrowed) */
    int source_line;            /* Source line */
    uint64_t timestamp;         /* Monotonic timestamp (for ordering) */
    bool was_handled;           /* Was this effect handled? */
} TraceEntry;

static TraceEntry trace_buffer[TRACE_BUFFER_SIZE];
static int trace_count = 0;
static uint64_t trace_timestamp = 0;

/* Built-in effect types */
EffectType* EFFECT_FAIL = NULL;
EffectType* EFFECT_ASK = NULL;
EffectType* EFFECT_EMIT = NULL;
EffectType* EFFECT_STATE = NULL;
EffectType* EFFECT_YIELD = NULL;
EffectType* EFFECT_ASYNC = NULL;
EffectType* EFFECT_CHOICE = NULL;
EffectType* EFFECT_CONDITION = NULL;  /* Issue 14 P3: Condition signaling */

/* ============================================================
 * Thread-Local Handler Stack
 * ============================================================ */

static void handler_stack_key_init(void) {
    pthread_key_create(&handler_stack_key, NULL);
}

static Handler* get_handler_stack(void) {
    pthread_once(&handler_key_once, handler_stack_key_init);
    return (Handler*)pthread_getspecific(handler_stack_key);
}

static void set_handler_stack(Handler* h) {
    pthread_once(&handler_key_once, handler_stack_key_init);
    pthread_setspecific(handler_stack_key, h);
}

/* ============================================================
 * Effect System Initialization
 * ============================================================ */

void effect_init(void) {
    pthread_once(&handler_key_once, handler_stack_key_init);
    effect_init_builtins();
}

void effect_shutdown(void) {
    /* Free all registered effect types */
    pthread_mutex_lock(&registry_lock);
    EffectType* type = effect_registry;
    while (type) {
        EffectType* next = type->next;
        if (type->recovery) {
            recovery_protocol_free(type->recovery);
        }
        free((void*)type->name);
        free(type);
        type = next;
    }
    effect_registry = NULL;
    pthread_mutex_unlock(&registry_lock);
}

void effect_init_builtins(void) {
    /* Fail effect (exception-like, never resumes) */
    RecoveryProtocol* fail_rp = recovery_protocol_create(
        "fail", RECOVERY_ABORT, NULL, NULL,
        "Aborts computation with an error"
    );
    EFFECT_FAIL = effect_type_register("Fail", NULL, fail_rp);

    /* Ask effect (reader) */
    RecoveryProtocol* ask_rp = recovery_protocol_create(
        "ask", RECOVERY_ONE_SHOT, NULL, NULL,
        "Asks for a value from the environment"
    );
    EFFECT_ASK = effect_type_register("Ask", NULL, ask_rp);

    /* Emit effect (writer) */
    RecoveryProtocol* emit_rp = recovery_protocol_create(
        "emit", RECOVERY_ONE_SHOT, NULL, NULL,
        "Emits a value to be collected"
    );
    EFFECT_EMIT = effect_type_register("Emit", NULL, emit_rp);

    /* State effect */
    EFFECT_STATE = effect_type_register("State", NULL, NULL);

    /* Yield effect (generators) */
    RecoveryProtocol* yield_rp = recovery_protocol_create(
        "yield", RECOVERY_ONE_SHOT, NULL, NULL,
        "Yields a value and suspends"
    );
    EFFECT_YIELD = effect_type_register("Yield", NULL, yield_rp);

    /* Async effect */
    EFFECT_ASYNC = effect_type_register("Async", NULL, NULL);

    /* Choice effect (non-determinism) */
    RecoveryProtocol* choice_rp = recovery_protocol_create(
        "choice", RECOVERY_MULTI_SHOT, NULL, NULL,
        "Non-deterministic choice"
    );
    EFFECT_CHOICE = effect_type_register("Choice", NULL, choice_rp);

    /* Condition effect (Issue 14 P3: unified condition signaling)
     * ONE_SHOT mode - can resume once with a recovery value.
     * Used by condition_signal() for resumable conditions.
     */
    RecoveryProtocol* cond_rp = recovery_protocol_create(
        "condition", RECOVERY_ONE_SHOT, NULL, NULL,
        "Signals a condition that can be handled"
    );
    EFFECT_CONDITION = effect_type_register("Condition", NULL, cond_rp);
}

/* ============================================================
 * Effect Type Registry
 * ============================================================ */

EffectType* effect_type_register(
    const char* name,
    Obj* payload_type,
    RecoveryProtocol* recovery
) {
    pthread_mutex_lock(&registry_lock);

    /* Check for duplicate */
    for (EffectType* t = effect_registry; t; t = t->next) {
        if (strcmp(t->name, name) == 0) {
            pthread_mutex_unlock(&registry_lock);
            return t;  /* Already registered */
        }
    }

    /* Create new effect type */
    EffectType* type = (EffectType*)malloc(sizeof(EffectType));
    type->name = strdup(name);
    type->id = next_effect_id++;
    type->payload_type = payload_type;
    type->recovery = recovery;
    type->next = effect_registry;
    effect_registry = type;

    pthread_mutex_unlock(&registry_lock);
    return type;
}

EffectType* effect_type_find(const char* name) {
    pthread_mutex_lock(&registry_lock);
    for (EffectType* t = effect_registry; t; t = t->next) {
        if (strcmp(t->name, name) == 0) {
            pthread_mutex_unlock(&registry_lock);
            return t;
        }
    }
    pthread_mutex_unlock(&registry_lock);
    return NULL;
}

EffectType* effect_type_find_by_id(uint32_t id) {
    pthread_mutex_lock(&registry_lock);
    for (EffectType* t = effect_registry; t; t = t->next) {
        if (t->id == id) {
            pthread_mutex_unlock(&registry_lock);
            return t;
        }
    }
    pthread_mutex_unlock(&registry_lock);
    return NULL;
}

EffectType* effect_type_list_all(void) {
    return effect_registry;
}

/* ============================================================
 * Handler Stack Management
 * ============================================================ */

static uint32_t next_handler_id = 1;

Handler* handler_push(HandlerClause* clauses, Obj* return_clause, Obj* env) {
    Handler* h = (Handler*)malloc(sizeof(Handler));
    h->id = next_handler_id++;
    h->prompt_tag = prompt_tag_generate();
    h->clauses = clauses;
    h->return_clause = return_clause;
    h->env = env;
    h->parent = get_handler_stack();
    set_handler_stack(h);
    return h;
}

void handler_pop(void) {
    Handler* h = get_handler_stack();
    if (h) {
        set_handler_stack(h->parent);
        /* Don't free clauses here - they may be reused */
    }
}

Handler* handler_current(void) {
    return get_handler_stack();
}

Handler* handler_find(EffectType* type) {
    Handler* h = get_handler_stack();
    while (h) {
        if (handler_find_clause(h, type)) {
            return h;
        }
        h = h->parent;
    }
    return NULL;
}

HandlerClause* handler_find_clause(Handler* h, EffectType* type) {
    if (!h) return NULL;
    for (HandlerClause* c = h->clauses; c; c = c->next) {
        if (c->effect_type == type ||
            (c->effect_type && type && c->effect_type->id == type->id)) {
            return c;
        }
    }
    return NULL;
}

/* ============================================================
 * Effect Creation
 * ============================================================ */

Effect* effect_create(EffectType* type, Obj* payload) {
    Effect* eff = (Effect*)malloc(sizeof(Effect));
    eff->type = type;
    eff->payload = payload;
    eff->source_file = NULL;
    eff->source_line = 0;
    eff->source_col = 0;
    return eff;
}

void effect_free(Effect* eff) {
    if (eff) {
        free(eff);
    }
}

/* ============================================================
 * Resumption Operations
 * ============================================================ */

Resumption* resumption_create(Continuation* cont, EffectType* type, Effect* eff) {
    Resumption* r = (Resumption*)malloc(sizeof(Resumption));
    r->cont = cont;
    r->effect_type = type;
    r->mode = type->recovery ? type->recovery->mode : RECOVERY_ONE_SHOT;
    r->used = false;
    r->refcount = 1;
    r->original_effect = eff;
    return r;
}

Obj* resumption_invoke(Resumption* r, Obj* value) {
    if (!r || !r->cont) {
        return NULL;  /* Invalid resumption */
    }

    /* Check one-shot constraint */
    if (r->mode == RECOVERY_ONE_SHOT && r->used) {
        fprintf(stderr, "Error: One-shot resumption invoked multiple times\n");
        return NULL;
    }

    /* Check abort mode */
    if (r->mode == RECOVERY_ABORT) {
        fprintf(stderr, "Error: Cannot resume abortive effect\n");
        return NULL;
    }

    /* Validate recovery if protocol exists */
    if (!recovery_validate(r, value)) {
        fprintf(stderr, "Error: Recovery validation failed\n");
        return NULL;
    }

    r->used = true;

    /* Invoke the continuation with the value */
    return cont_invoke(r->cont, value);
}

bool resumption_is_valid(Resumption* r) {
    if (!r || !r->cont) return false;
    if (r->mode == RECOVERY_ABORT) return false;
    if (r->mode == RECOVERY_ONE_SHOT && r->used) return false;
    return cont_is_valid(r->cont);
}

Resumption* resumption_clone(Resumption* r) {
    if (!r) return NULL;
    if (r->mode != RECOVERY_MULTI_SHOT) {
        fprintf(stderr, "Warning: Cloning non-multi-shot resumption\n");
    }

    Resumption* clone = (Resumption*)malloc(sizeof(Resumption));
    clone->cont = r->cont;  /* Share the continuation */
    if (clone->cont) {
        /* Increment continuation refcount if needed */
    }
    clone->effect_type = r->effect_type;
    clone->mode = r->mode;
    clone->used = false;
    clone->refcount = 1;
    clone->original_effect = r->original_effect;
    return clone;
}

void resumption_discard(Resumption* r) {
    if (r) {
        r->used = true;  /* Mark as used so it can't be invoked */
        resumption_dec_ref(r);
    }
}

void resumption_inc_ref(Resumption* r) {
    if (r) {
        r->refcount++;
    }
}

void resumption_dec_ref(Resumption* r) {
    if (r && --r->refcount == 0) {
        if (r->cont) {
            cont_release(r->cont);
        }
        if (r->original_effect) {
            effect_free(r->original_effect);
        }
        free(r);
    }
}

/* ============================================================
 * Performing Effects
 * ============================================================ */

Obj* effect_perform(EffectType* type, Obj* payload) {
    if (!type) {
        fprintf(stderr, "Error: Cannot perform NULL effect type\n");
        return NULL;
    }

    /* Create effect instance */
    Effect* eff = effect_create(type, payload);

    /* Record in trace if enabled */
    if (trace_enabled) {
        effect_trace_record(eff);
    }

    /* Find handler */
    Handler* handler = handler_find(type);
    if (!handler) {
        fprintf(stderr, "Error: Unhandled effect: %s\n", type->name);
        effect_free(eff);
        return NULL;
    }

    /* Find the clause */
    HandlerClause* clause = handler_find_clause(handler, type);
    if (!clause || !clause->handler_fn) {
        fprintf(stderr, "Error: No handler clause for effect: %s\n", type->name);
        effect_free(eff);
        return NULL;
    }

    /* Check for abort mode - no continuation capture needed */
    if (type->recovery && type->recovery->mode == RECOVERY_ABORT) {
        /* Call the handler without a resumption (it can't resume) */
        Obj* args[2] = { payload, NULL };
        Obj* result = call_closure(clause->handler_fn, args, 2);
        effect_free(eff);
        return result;
    }

    /* Capture continuation up to handler's prompt */
    Continuation* cont = cont_capture(handler->prompt_tag);
    if (!cont) {
        fprintf(stderr, "Error: Failed to capture continuation for effect: %s\n",
                type->name);
        effect_free(eff);
        return NULL;
    }

    /* Create resumption */
    Resumption* resume = resumption_create(cont, type, eff);

    /* Create Obj wrapper for resumption */
    Obj* resume_obj = mk_resumption_obj(resume);

    /* Release our reference - the Obj now owns it */
    resumption_dec_ref(resume);

    /* Call the handler: (handler-fn payload resume) */
    Obj* args[2] = { payload, resume_obj };
    Obj* result = call_closure(clause->handler_fn, args, 2);

    /* The handler either:
     * 1. Called resume(value) - result is what the original computation returns
     * 2. Did not resume - result is the handler's return value
     */
    return result;
}

Obj* effect_perform_named(const char* name, Obj* payload) {
    EffectType* type = effect_type_find(name);
    if (!type) {
        fprintf(stderr, "Error: Unknown effect type: %s\n", name);
        return NULL;
    }
    return effect_perform(type, payload);
}

/* ============================================================
 * Handler Block API
 * ============================================================ */

Obj* effect_handle(
    Obj* body,
    HandlerClause* clauses,
    Obj* return_clause,
    Obj* env
) {
    /* Push handler */
    Handler* h = handler_push(clauses, return_clause, env);

    /*
     * Establish prompt with the handler's tag.
     * cont_prompt sets up the escape point (setjmp) and returns body.
     * If a longjmp occurs (from effect capture), we return via the
     * setjmp return path with the handler's result.
     */
    Obj* result = cont_prompt(h->prompt_tag, body, env, NULL);

    /*
     * If body is a thunk (closure), we need to call it.
     * The prompt frame is now active, so effects in the body
     * will be able to capture continuations up to this prompt.
     */
    if (result && IS_BOXED(result) && result->tag == TAG_CLOSURE) {
        /* Call the body thunk with no arguments */
        result = call_closure(result, NULL, 0);
        /* Exit prompt normally with the result */
        result = cont_prompt_exit(h->prompt_tag, result);
    }

    /* Pop handler */
    handler_pop();

    /* Apply return clause if present */
    if (return_clause && result) {
        Obj* args[1] = { result };
        result = call_closure(return_clause, args, 1);
    }

    return result;
}

HandlerClause* handler_clause_add(
    HandlerClause* clauses,
    EffectType* type,
    Obj* handler_fn
) {
    HandlerClause* clause = (HandlerClause*)malloc(sizeof(HandlerClause));
    clause->effect_type = type;
    clause->handler_fn = handler_fn;
    clause->next = clauses;
    return clause;
}

void handler_clauses_free(HandlerClause* clauses) {
    while (clauses) {
        HandlerClause* next = clauses->next;
        free(clauses);
        clauses = next;
    }
}

/* ============================================================
 * Recovery Protocol
 * ============================================================ */

RecoveryProtocol* recovery_protocol_create(
    const char* name,
    RecoveryMode mode,
    Obj* input_type,
    Obj* output_type,
    const char* description
) {
    RecoveryProtocol* rp = (RecoveryProtocol*)malloc(sizeof(RecoveryProtocol));
    rp->name = strdup(name);
    rp->mode = mode;
    rp->input_type = input_type;
    rp->output_type = output_type;
    rp->description = description ? strdup(description) : NULL;
    return rp;
}

void recovery_protocol_free(RecoveryProtocol* rp) {
    if (rp) {
        free((void*)rp->name);
        free((void*)rp->description);
        free(rp);
    }
}

bool recovery_validate(Resumption* r, Obj* value) {
    if (!r || !r->effect_type) return true;  /* No validation if no protocol */

    RecoveryProtocol* rp = r->effect_type->recovery;
    if (!rp) return true;  /* No protocol means any value is ok */

    /* Check mode constraints */
    if (rp->mode == RECOVERY_ABORT) {
        return false;  /* Can never resume */
    }

    if (rp->mode == RECOVERY_ONE_SHOT && r->used) {
        return false;  /* Already used */
    }

    /* Type checking would go here if input_type is specified */
    /* For now, we accept any value */

    return true;
}

/* ============================================================
 * Diagnostics
 * ============================================================ */

void effect_type_print(EffectType* type) {
    if (!type) {
        printf("(null effect type)\n");
        return;
    }
    printf("effect %s (id=%u", type->name, type->id);
    if (type->recovery) {
        printf(", mode=");
        switch (type->recovery->mode) {
            case RECOVERY_ONE_SHOT:   printf("one-shot"); break;
            case RECOVERY_MULTI_SHOT: printf("multi-shot"); break;
            case RECOVERY_TAIL:       printf("tail"); break;
            case RECOVERY_ABORT:      printf("abort"); break;
        }
    }
    printf(")\n");
}

void handler_stack_print(void) {
    printf("Handler stack:\n");
    Handler* h = get_handler_stack();
    int depth = 0;
    while (h) {
        printf("  [%d] Handler %u (prompt=%u)\n", depth, h->id, h->prompt_tag);
        for (HandlerClause* c = h->clauses; c; c = c->next) {
            printf("       - %s\n", c->effect_type ? c->effect_type->name : "(null)");
        }
        h = h->parent;
        depth++;
    }
    if (depth == 0) {
        printf("  (empty)\n");
    }
}

void effect_trace_print(void) {
    if (trace_count == 0) {
        printf("(effect trace empty)\n");
        return;
    }
    printf("Effect trace (%d events):\n", trace_count);
    for (int i = 0; i < trace_count; i++) {
        TraceEntry* e = &trace_buffer[i];
        printf("  [%d] %s", i, e->effect_name ? e->effect_name : "(unknown)");
        if (e->source_file) {
            printf(" at %s:%d", e->source_file, e->source_line);
        }
        printf(" %s\n", e->was_handled ? "(handled)" : "(unhandled)");
    }
}

char* effect_trace_to_string(void) {
    if (trace_count == 0) {
        return strdup("(effect trace empty)");
    }

    /* Estimate buffer size: ~100 chars per entry */
    size_t buf_size = 64 + (trace_count * 128);
    char* buf = malloc(buf_size);
    if (!buf) return strdup("(allocation failed)");

    char* p = buf;
    int remaining = (int)buf_size;
    int written = snprintf(p, remaining, "Effect trace (%d events):\n", trace_count);
    p += written;
    remaining -= written;

    for (int i = 0; i < trace_count && remaining > 0; i++) {
        TraceEntry* e = &trace_buffer[i];
        if (e->source_file) {
            written = snprintf(p, remaining, "  [%d] %s at %s:%d %s\n",
                i, e->effect_name ? e->effect_name : "(unknown)",
                e->source_file, e->source_line,
                e->was_handled ? "(handled)" : "(unhandled)");
        } else {
            written = snprintf(p, remaining, "  [%d] %s %s\n",
                i, e->effect_name ? e->effect_name : "(unknown)",
                e->was_handled ? "(handled)" : "(unhandled)");
        }
        p += written;
        remaining -= written;
    }

    return buf;
}

/* ============================================================
 * Effect Tracing
 * ============================================================ */

void effect_trace_enable(bool enable) {
    trace_enabled = enable;
}

bool effect_trace_is_enabled(void) {
    return trace_enabled;
}

void effect_trace_record(Effect* eff) {
    if (!trace_enabled || !eff) return;

    /* Ring buffer: overwrite oldest if full */
    int idx = trace_count;
    if (trace_count >= TRACE_BUFFER_SIZE) {
        /* Shift buffer left by 1 (discard oldest) */
        memmove(&trace_buffer[0], &trace_buffer[1],
                (TRACE_BUFFER_SIZE - 1) * sizeof(TraceEntry));
        idx = TRACE_BUFFER_SIZE - 1;
    } else {
        trace_count++;
    }

    TraceEntry* e = &trace_buffer[idx];
    e->effect_name = eff->type ? eff->type->name : NULL;
    e->source_file = eff->source_file;
    e->source_line = eff->source_line;
    e->timestamp = trace_timestamp++;
    e->was_handled = false;  /* Will be updated when handled */
}

void effect_trace_mark_handled(int index) {
    if (index >= 0 && index < trace_count) {
        trace_buffer[index].was_handled = true;
    }
}

int effect_trace_last_index(void) {
    return trace_count > 0 ? trace_count - 1 : -1;
}

void effect_trace_clear(void) {
    trace_count = 0;
    trace_timestamp = 0;
}
