/*
 * restart.h - Restart System for OmniLisp
 *
 * Provides Common Lisp-style restarts for condition handling:
 * - Named restarts that can be invoked during error recovery
 * - Thread-local restart stacks
 * - Integration with the condition system
 *
 * Part of T-restart-core implementation.
 */

#ifndef OMNI_RESTART_H
#define OMNI_RESTART_H

#include "types.h"
#include "condition.h"
#include <setjmp.h>

/* Forward declarations */
typedef struct Restart Restart;
typedef struct RestartContext RestartContext;

/*
 * Restart - a named recovery action
 */
struct Restart {
    const char* name;           /* Restart name (e.g., "use-value", "abort") */
    const char* description;    /* Human-readable description */
    void* (*action)(void*);     /* Action function to execute */
    void* context;              /* Context data for the action */
    Restart* next;              /* Next restart in chain */
};

/*
 * RestartContext - stack frame for restart-case
 */
struct RestartContext {
    jmp_buf jump_buffer;        /* For non-local return */
    Restart* restarts;          /* Available restarts in this frame */
    void* result;               /* Result from invoked restart */
    int invoked;                /* Was a restart invoked? */
    RestartContext* parent;     /* Parent context */
};

/* ============================================================
 * Restart Stack Management
 * ============================================================ */

/* Initialize the restart system (called at runtime init) */
void restart_init(void);

/* Push a new restart context (for restart-case) */
RestartContext* restart_push_context(void);

/* Pop the current restart context */
void restart_pop_context(void);

/* Get the current restart context */
RestartContext* restart_current_context(void);

/* ============================================================
 * Restart Registration
 * ============================================================ */

/* Add a restart to the current context */
Restart* restart_push(
    const char* name,
    const char* description,
    void* (*action)(void*),
    void* context
);

/* Remove a restart from the current context */
void restart_pop(const char* name);

/* ============================================================
 * Restart Query and Invocation
 * ============================================================ */

/* Find a restart by name (searches all active contexts) */
Restart* restart_find(const char* name);

/* Get list of all available restarts */
Restart* restart_list_all(void);

/* Invoke a restart by name with optional argument */
void* restart_invoke(const char* name, void* arg);

/* Invoke a restart directly */
void* restart_invoke_restart(Restart* restart, void* arg);

/* ============================================================
 * Common Restart Actions
 * ============================================================ */

/* Abort: Terminate the current computation (common restart) */
void* restart_action_abort(void* arg);

/* Use-Value: Return a specified value */
void* restart_action_use_value(void* arg);

/* Continue: Continue with a default action */
void* restart_action_continue(void* arg);

/* Retry: Retry the failed operation */
void* restart_action_retry(void* arg);

/* Store-Value: Store a value for later use */
void* restart_action_store_value(void* arg);

/* ============================================================
 * Helper Macros for C Usage
 * ============================================================ */

/*
 * RESTART_CASE - Establish restarts and handle their invocation
 *
 * Usage:
 *   RESTART_CASE_BEGIN()
 *     restart_push("retry", "Retry the operation", my_retry_action, ctx);
 *     restart_push("abort", "Abort and return nil", restart_action_abort, NULL);
 *
 *     // Do something that might signal a condition
 *     result = risky_operation();
 *
 *   RESTART_CASE_END(result)
 *   // result now contains either the normal result or the restart result
 */
#define RESTART_CASE_BEGIN() do { \
    RestartContext* _restart_ctx = restart_push_context(); \
    if (_restart_ctx && setjmp(_restart_ctx->jump_buffer) == 0) {

#define RESTART_CASE_END(result_var) \
        restart_pop_context(); \
    } else { \
        result_var = restart_current_context() ? \
            restart_current_context()->result : NULL; \
        restart_pop_context(); \
    } \
} while(0);

/* ============================================================
 * Integration with Conditions
 * ============================================================ */

/* Signal a condition and allow restarts to handle it */
void* condition_signal_with_restarts(Condition* cond);

/* Print available restarts for interactive debugging */
void restart_print_available(void);

#endif /* OMNI_RESTART_H */
