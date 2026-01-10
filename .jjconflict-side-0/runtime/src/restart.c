/*
 * restart.c - Restart System Implementation
 *
 * Implements Common Lisp-style restarts for OmniLisp.
 * Part of T-restart-core implementation.
 */

#define _POSIX_C_SOURCE 200809L

#include "restart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ============================================================
 * Thread-Local Restart Stack
 * ============================================================ */

static pthread_key_t restart_stack_key;
static pthread_once_t restart_once = PTHREAD_ONCE_INIT;

static void restart_stack_destroy(void* ptr) {
    /* Clean up any remaining contexts */
    RestartContext* ctx = (RestartContext*)ptr;
    while (ctx) {
        RestartContext* parent = ctx->parent;

        /* Free restarts */
        Restart* r = ctx->restarts;
        while (r) {
            Restart* next = r->next;
            free((void*)r->name);
            if (r->description) free((void*)r->description);
            free(r);
            r = next;
        }

        free(ctx);
        ctx = parent;
    }
}

static void restart_init_once(void) {
    pthread_key_create(&restart_stack_key, restart_stack_destroy);
}

void restart_init(void) {
    pthread_once(&restart_once, restart_init_once);
}

/* ============================================================
 * Restart Stack Management
 * ============================================================ */

RestartContext* restart_push_context(void) {
    restart_init();

    RestartContext* ctx = (RestartContext*)calloc(1, sizeof(RestartContext));
    if (!ctx) return NULL;

    ctx->restarts = NULL;
    ctx->result = NULL;
    ctx->invoked = 0;
    ctx->parent = (RestartContext*)pthread_getspecific(restart_stack_key);

    pthread_setspecific(restart_stack_key, ctx);
    return ctx;
}

void restart_pop_context(void) {
    restart_init();

    RestartContext* ctx = (RestartContext*)pthread_getspecific(restart_stack_key);
    if (!ctx) return;

    /* Free restarts in this context */
    Restart* r = ctx->restarts;
    while (r) {
        Restart* next = r->next;
        free((void*)r->name);
        if (r->description) free((void*)r->description);
        free(r);
        r = next;
    }

    /* Restore parent context */
    pthread_setspecific(restart_stack_key, ctx->parent);
    free(ctx);
}

RestartContext* restart_current_context(void) {
    restart_init();
    return (RestartContext*)pthread_getspecific(restart_stack_key);
}

/* ============================================================
 * Restart Registration
 * ============================================================ */

Restart* restart_push(
    const char* name,
    const char* description,
    void* (*action)(void*),
    void* context
) {
    RestartContext* ctx = restart_current_context();
    if (!ctx) {
        /* Create a context if none exists */
        ctx = restart_push_context();
        if (!ctx) return NULL;
    }

    Restart* r = (Restart*)malloc(sizeof(Restart));
    if (!r) return NULL;

    r->name = strdup(name);
    r->description = description ? strdup(description) : NULL;
    r->action = action;
    r->context = context;
    r->next = ctx->restarts;
    ctx->restarts = r;

    return r;
}

void restart_pop(const char* name) {
    RestartContext* ctx = restart_current_context();
    if (!ctx) return;

    Restart** prev = &ctx->restarts;
    Restart* r = ctx->restarts;

    while (r) {
        if (strcmp(r->name, name) == 0) {
            *prev = r->next;
            free((void*)r->name);
            if (r->description) free((void*)r->description);
            free(r);
            return;
        }
        prev = &r->next;
        r = r->next;
    }
}

/* ============================================================
 * Restart Query and Invocation
 * ============================================================ */

Restart* restart_find(const char* name) {
    RestartContext* ctx = restart_current_context();

    /* Search through all active contexts */
    while (ctx) {
        Restart* r = ctx->restarts;
        while (r) {
            if (strcmp(r->name, name) == 0) {
                return r;
            }
            r = r->next;
        }
        ctx = ctx->parent;
    }

    return NULL;
}

Restart* restart_list_all(void) {
    RestartContext* ctx = restart_current_context();
    if (!ctx) return NULL;

    /* Build a linked list of all available restarts */
    Restart* result = NULL;
    Restart** tail = &result;

    while (ctx) {
        Restart* r = ctx->restarts;
        while (r) {
            /* Create a copy for the result list */
            Restart* copy = (Restart*)malloc(sizeof(Restart));
            if (copy) {
                copy->name = r->name;  /* Share strings */
                copy->description = r->description;
                copy->action = r->action;
                copy->context = r->context;
                copy->next = NULL;
                *tail = copy;
                tail = &copy->next;
            }
            r = r->next;
        }
        ctx = ctx->parent;
    }

    return result;
}

void* restart_invoke(const char* name, void* arg) {
    Restart* r = restart_find(name);
    if (!r) {
        fprintf(stderr, "No restart named '%s' is available\n", name);
        return NULL;
    }
    return restart_invoke_restart(r, arg);
}

void* restart_invoke_restart(Restart* restart, void* arg) {
    if (!restart) return NULL;

    /* Find the context that owns this restart */
    RestartContext* ctx = restart_current_context();
    while (ctx) {
        Restart* r = ctx->restarts;
        while (r) {
            if (r == restart || strcmp(r->name, restart->name) == 0) {
                /* Found the owning context */
                ctx->invoked = 1;

                /* Execute the action */
                void* result = NULL;
                if (restart->action) {
                    /* Action receives either the arg or its stored context */
                    result = restart->action(arg ? arg : restart->context);
                } else {
                    result = arg;
                }

                ctx->result = result;

                /* Jump back to the restart-case */
                longjmp(ctx->jump_buffer, 1);

                /* Never reached */
                return result;
            }
            r = r->next;
        }
        ctx = ctx->parent;
    }

    fprintf(stderr, "Restart '%s' not found in active contexts\n", restart->name);
    return NULL;
}

/* ============================================================
 * Common Restart Actions
 * ============================================================ */

void* restart_action_abort(void* arg) {
    (void)arg;
    return NULL;
}

void* restart_action_use_value(void* arg) {
    return arg;
}

void* restart_action_continue(void* arg) {
    (void)arg;
    return NULL;
}

void* restart_action_retry(void* arg) {
    /* Caller should handle retry logic */
    return arg;
}

void* restart_action_store_value(void* arg) {
    return arg;
}

/* ============================================================
 * Integration with Conditions
 * ============================================================ */

void* condition_signal_with_restarts(Condition* cond) {
    if (!cond) return NULL;

    /* For now, print the condition and available restarts */
    fprintf(stderr, "Condition signaled:\n");
    condition_print_full(cond);

    /* Print available restarts */
    restart_print_available();

    /* In a full implementation, this would:
     * 1. Search for handler-bind handlers
     * 2. Let them invoke restarts
     * 3. If no handler handles it, search for handler-case handlers
     * For now, we just abort */
    return NULL;
}

void restart_print_available(void) {
    RestartContext* ctx = restart_current_context();
    if (!ctx) {
        fprintf(stderr, "No restarts available.\n");
        return;
    }

    fprintf(stderr, "\nAvailable restarts:\n");
    int index = 0;

    while (ctx) {
        Restart* r = ctx->restarts;
        while (r) {
            fprintf(stderr, "  %d. [%s]", index++, r->name);
            if (r->description) {
                fprintf(stderr, " - %s", r->description);
            }
            fprintf(stderr, "\n");
            r = r->next;
        }
        ctx = ctx->parent;
    }
}
