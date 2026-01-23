/*
 * condition.c - Structured Condition System Implementation
 *
 * Implements Common Lisp-style conditions for OmniLisp.
 * Part of T-cond-core-types implementation.
 */

#define _POSIX_C_SOURCE 200809L

#include "condition.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Global Condition Type Registry
 * ============================================================ */

static ConditionType* condition_registry = NULL;
static uint32_t next_condition_id = 1;

/* Base condition types (initialized by condition_init) */
ConditionType* COND_CONDITION = NULL;
ConditionType* COND_ERROR = NULL;
ConditionType* COND_TYPE_ERROR = NULL;
ConditionType* COND_ARITHMETIC_ERROR = NULL;
ConditionType* COND_DIVISION_BY_ZERO = NULL;
ConditionType* COND_UNBOUND_VARIABLE = NULL;
ConditionType* COND_UNDEFINED_FUNCTION = NULL;
ConditionType* COND_MEMORY_ERROR = NULL;
ConditionType* COND_USE_AFTER_FREE = NULL;
ConditionType* COND_DOUBLE_FREE = NULL;
ConditionType* COND_REGION_MISMATCH = NULL;
ConditionType* COND_FFI_ERROR = NULL;
ConditionType* COND_FFI_TYPE_MISMATCH = NULL;
ConditionType* COND_FFI_LOAD_ERROR = NULL;
ConditionType* COND_IO_ERROR = NULL;
ConditionType* COND_FILE_ERROR = NULL;
ConditionType* COND_END_OF_FILE = NULL;
ConditionType* COND_WARNING = NULL;
ConditionType* COND_STYLE_WARNING = NULL;
ConditionType* COND_DEPRECATION_WARNING = NULL;

/* ============================================================
 * Condition Type Registry Implementation
 * ============================================================ */

ConditionType* condition_type_register(
    const char* name,
    ConditionType* parent,
    const char** slot_names,
    int slot_count
) {
    ConditionType* type = (ConditionType*)malloc(sizeof(ConditionType));
    if (!type) return NULL;

    type->name = strdup(name);
    type->id = next_condition_id++;
    type->parent = parent;
    type->slot_names = slot_names;
    type->slot_count = slot_count;
    type->next = condition_registry;
    condition_registry = type;

    return type;
}

// REVIEWED:NAIVE
ConditionType* condition_type_lookup(const char* name) {
    for (ConditionType* t = condition_registry; t; t = t->next) {
        if (strcmp(t->name, name) == 0) {
            return t;
        }
    }
    return NULL;
}

// REVIEWED:NAIVE
ConditionType* condition_type_lookup_id(uint32_t id) {
    for (ConditionType* t = condition_registry; t; t = t->next) {
        if (t->id == id) {
            return t;
        }
    }
    return NULL;
}

int condition_type_is_subtype(ConditionType* sub, ConditionType* super) {
    if (!sub || !super) return 0;

    for (ConditionType* t = sub; t; t = t->parent) {
        if (t == super || t->id == super->id) {
            return 1;
        }
    }
    return 0;
}

/* Slot names for common condition types */
static const char* error_slots[] = {"message", NULL};
static const char* type_error_slots[] = {"message", "expected", "got", "datum", NULL};
static const char* unbound_slots[] = {"message", "name", NULL};
static const char* memory_error_slots[] = {"message", "address", NULL};
static const char* ffi_error_slots[] = {"message", "function", NULL};
static const char* io_error_slots[] = {"message", "pathname", NULL};

void condition_init(void) {
    /* Avoid re-initialization */
    if (COND_CONDITION) return;

    /* Root condition type */
    COND_CONDITION = condition_type_register(
        ":condition", NULL, error_slots, 1);

    /* Error hierarchy */
    COND_ERROR = condition_type_register(
        ":error", COND_CONDITION, error_slots, 1);

    COND_TYPE_ERROR = condition_type_register(
        ":type-error", COND_ERROR, type_error_slots, 4);

    COND_ARITHMETIC_ERROR = condition_type_register(
        ":arithmetic-error", COND_ERROR, error_slots, 1);

    COND_DIVISION_BY_ZERO = condition_type_register(
        ":division-by-zero", COND_ARITHMETIC_ERROR, error_slots, 1);

    COND_UNBOUND_VARIABLE = condition_type_register(
        ":unbound-variable", COND_ERROR, unbound_slots, 2);

    COND_UNDEFINED_FUNCTION = condition_type_register(
        ":undefined-function", COND_ERROR, unbound_slots, 2);

    /* Memory errors */
    COND_MEMORY_ERROR = condition_type_register(
        ":memory-error", COND_ERROR, memory_error_slots, 2);

    COND_USE_AFTER_FREE = condition_type_register(
        ":use-after-free", COND_MEMORY_ERROR, memory_error_slots, 2);

    COND_DOUBLE_FREE = condition_type_register(
        ":double-free", COND_MEMORY_ERROR, memory_error_slots, 2);

    COND_REGION_MISMATCH = condition_type_register(
        ":region-mismatch", COND_MEMORY_ERROR, memory_error_slots, 2);

    /* FFI errors */
    COND_FFI_ERROR = condition_type_register(
        ":ffi-error", COND_ERROR, ffi_error_slots, 2);

    COND_FFI_TYPE_MISMATCH = condition_type_register(
        ":ffi-type-mismatch", COND_FFI_ERROR, ffi_error_slots, 2);

    COND_FFI_LOAD_ERROR = condition_type_register(
        ":ffi-load-error", COND_FFI_ERROR, ffi_error_slots, 2);

    /* IO errors */
    COND_IO_ERROR = condition_type_register(
        ":io-error", COND_ERROR, io_error_slots, 2);

    COND_FILE_ERROR = condition_type_register(
        ":file-error", COND_IO_ERROR, io_error_slots, 2);

    COND_END_OF_FILE = condition_type_register(
        ":end-of-file", COND_IO_ERROR, io_error_slots, 2);

    /* Warnings */
    COND_WARNING = condition_type_register(
        ":warning", COND_CONDITION, error_slots, 1);

    COND_STYLE_WARNING = condition_type_register(
        ":style-warning", COND_WARNING, error_slots, 1);

    COND_DEPRECATION_WARNING = condition_type_register(
        ":deprecation-warning", COND_WARNING, error_slots, 1);
}

/* ============================================================
 * Condition Creation and Access
 * ============================================================ */

Condition* condition_create(ConditionType* type) {
    if (!type) return NULL;

    Condition* cond = (Condition*)malloc(sizeof(Condition));
    if (!cond) return NULL;

    cond->type = type;
    cond->slots = NULL;
    cond->source_file = NULL;
    cond->source_line = 0;
    cond->source_col = 0;
    cond->cause = NULL;

    return cond;
}

Condition* condition_create_with_message(ConditionType* type, const char* message) {
    Condition* cond = condition_create(type);
    if (cond && message) {
        condition_set_slot_string(cond, "message", message);
    }
    return cond;
}

// REVIEWED:NAIVE
static ConditionSlot* find_slot(Condition* cond, const char* name) {
    for (ConditionSlot* s = cond->slots; s; s = s->next) {
        if (strcmp(s->name, name) == 0) {
            return s;
        }
    }
    return NULL;
}

static ConditionSlot* ensure_slot(Condition* cond, const char* name) {
    ConditionSlot* s = find_slot(cond, name);
    if (s) return s;

    s = (ConditionSlot*)malloc(sizeof(ConditionSlot));
    if (!s) return NULL;

    s->name = strdup(name);
    s->value = NULL;
    s->is_string = 0;
    s->next = cond->slots;
    cond->slots = s;

    return s;
}

void condition_set_slot_string(Condition* cond, const char* name, const char* value) {
    if (!cond || !name) return;

    ConditionSlot* s = ensure_slot(cond, name);
    if (!s) return;

    /* Free old string value if present */
    if (s->is_string && s->value) {
        free(s->value);
    }

    s->value = value ? strdup(value) : NULL;
    s->is_string = 1;
}

void condition_set_slot_value(Condition* cond, const char* name, void* value) {
    if (!cond || !name) return;

    ConditionSlot* s = ensure_slot(cond, name);
    if (!s) return;

    /* Free old string value if present */
    if (s->is_string && s->value) {
        free(s->value);
    }

    s->value = value;
    s->is_string = 0;
}

void* condition_get_slot(Condition* cond, const char* name, int* is_string) {
    if (!cond || !name) return NULL;

    ConditionSlot* s = find_slot(cond, name);
    if (!s) return NULL;

    if (is_string) *is_string = s->is_string;
    return s->value;
}

const char* condition_get_message(Condition* cond) {
    int is_string = 0;
    void* val = condition_get_slot(cond, "message", &is_string);
    return (is_string && val) ? (const char*)val : NULL;
}

void condition_set_location(Condition* cond, const char* file, int line, int col) {
    if (!cond) return;
    cond->source_file = file ? strdup(file) : NULL;
    cond->source_line = line;
    cond->source_col = col;
}

void condition_set_cause(Condition* cond, Condition* cause) {
    if (!cond) return;
    cond->cause = cause;
}

void condition_free(Condition* cond) {
    if (!cond) return;

    /* Free slots */
    ConditionSlot* s = cond->slots;
    while (s) {
        ConditionSlot* next = s->next;
        free((void*)s->name);
        if (s->is_string && s->value) {
            free(s->value);
        }
        free(s);
        s = next;
    }

    /* Free source file string */
    if (cond->source_file) {
        free((void*)cond->source_file);
    }

    /* Note: We don't free cause - it may be shared or managed elsewhere */

    free(cond);
}

/* ============================================================
 * Condition Rendering
 * ============================================================ */

char* condition_to_string(Condition* cond) {
    if (!cond) return strdup("<nil condition>");

    /* Calculate buffer size */
    size_t size = 256;  /* Base size */
    const char* msg = condition_get_message(cond);
    if (msg) size += strlen(msg);
    if (cond->source_file) size += strlen(cond->source_file) + 32;

    char* buf = (char*)malloc(size);
    if (!buf) return NULL;

    const char* type_name = cond->type ? cond->type->name : ":unknown";

    if (cond->source_file && cond->source_line > 0) {
        snprintf(buf, size, "%s: %s\n  at %s:%d:%d",
                 type_name,
                 msg ? msg : "(no message)",
                 cond->source_file,
                 cond->source_line,
                 cond->source_col);
    } else {
        snprintf(buf, size, "%s: %s",
                 type_name,
                 msg ? msg : "(no message)");
    }

    return buf;
}

void condition_print(Condition* cond) {
    char* str = condition_to_string(cond);
    if (str) {
        fprintf(stderr, "%s\n", str);
        free(str);
    }
}

void condition_print_full(Condition* cond) {
    if (!cond) {
        fprintf(stderr, "<nil condition>\n");
        return;
    }

    /* Print main condition */
    condition_print(cond);

    /* Print additional slots */
    for (ConditionSlot* s = cond->slots; s; s = s->next) {
        if (strcmp(s->name, "message") == 0) continue;  /* Already printed */

        if (s->is_string && s->value) {
            fprintf(stderr, "  %s: %s\n", s->name, (char*)s->value);
        } else if (s->value) {
            fprintf(stderr, "  %s: <value %p>\n", s->name, s->value);
        }
    }

    /* Print cause chain */
    if (cond->cause) {
        fprintf(stderr, "Caused by:\n");
        condition_print_full(cond->cause);
    }
}

/* ============================================================
 * Quick Constructors
 * ============================================================ */

Condition* make_error(const char* message) {
    return condition_create_with_message(COND_ERROR, message);
}

Condition* make_type_error(const char* expected, const char* got, void* datum) {
    Condition* cond = condition_create(COND_TYPE_ERROR);
    if (!cond) return NULL;

    char msg[256];
    snprintf(msg, sizeof(msg), "Expected %s, got %s", expected, got);
    condition_set_slot_string(cond, "message", msg);
    condition_set_slot_string(cond, "expected", expected);
    condition_set_slot_string(cond, "got", got);
    condition_set_slot_value(cond, "datum", datum);

    return cond;
}

Condition* make_unbound_variable(const char* name) {
    Condition* cond = condition_create(COND_UNBOUND_VARIABLE);
    if (!cond) return NULL;

    char msg[256];
    snprintf(msg, sizeof(msg), "Unbound variable: %s", name);
    condition_set_slot_string(cond, "message", msg);
    condition_set_slot_string(cond, "name", name);

    return cond;
}

// TESTED - runtime/tests/test_condition_additions.c
Condition* make_undefined_function(const char* name) {
    Condition* cond = condition_create(COND_UNDEFINED_FUNCTION);
    if (!cond) return NULL;

    char msg[256];
    snprintf(msg, sizeof(msg), "Undefined function: %s", name);
    condition_set_slot_string(cond, "message", msg);
    condition_set_slot_string(cond, "name", name);

    return cond;
}

Condition* make_division_by_zero(void) {
    return condition_create_with_message(
        COND_DIVISION_BY_ZERO,
// TESTED - runtime/tests/test_condition_additions.c
        "Division by zero"
    );
}

Condition* make_ffi_error(const char* message, const char* function_name) {
    Condition* cond = condition_create(COND_FFI_ERROR);
    if (!cond) return NULL;

// TESTED - runtime/tests/test_condition_additions.c
    condition_set_slot_string(cond, "message", message);
    if (function_name) {
        condition_set_slot_string(cond, "function", function_name);
    }

    return cond;
}

Condition* make_memory_error(const char* message, void* address) {
    Condition* cond = condition_create(COND_MEMORY_ERROR);
    if (!cond) return NULL;

    condition_set_slot_string(cond, "message", message);
    condition_set_slot_value(cond, "address", address);

    return cond;
}

/* ============================================================
 * Effect-Based Condition Signaling (Issue 14 P3)
 * ============================================================ */

#include "effect.h"
#include "../include/omni.h"
#include "memory/region_core.h"  /* For region_alloc inline function */

/* Forward declaration */
extern Region* _global_region;
extern void _ensure_global_region(void);

/*
 * mk_condition_obj - Wrap a Condition struct in an Obj
 *
 * Creates an Obj with TAG_CONDITION that wraps the condition.
 * The condition is NOT freed when the Obj is freed - caller manages lifetime.
 */
Obj* mk_condition_obj(Condition* cond) {
    if (!cond) return NULL;

    _ensure_global_region();
    Obj* obj = region_alloc(_global_region, sizeof(Obj));
    if (!obj) return NULL;

    obj->tag = TAG_CONDITION;
    obj->ptr = cond;
    obj->mark = 1;

    return obj;
}

/*
 * mk_ffi_load_error - Create FFI load error as Obj*
 *
 * Issue 19: ccall error handling
 * Creates an FFI error condition wrapped in an Obj* that can be returned
 * from generated ccall code when dlopen/dlsym fails.
 *
 * This allows Lisp code to detect and handle FFI failures:
 *   (let [result (ccall "nonexistent.so" "foo" [] {CInt})]
 *     (if (condition? result)
 *         (handle-error result)
 *         (use-result result)))
 */
Obj* mk_ffi_load_error(const char* library, const char* function) {
    /* Build error message */
    char msg[512];
    snprintf(msg, sizeof(msg), "FFI: could not load '%s' from '%s'",
             function ? function : "(unknown)",
             library ? library : "(unknown)");

    /* Create FFI error condition */
    Condition* cond = make_ffi_error(msg, function);
    if (!cond) {
        /* Fallback: return NOTHING if condition creation fails */
        extern Obj omni_nothing_obj;
        return &omni_nothing_obj;
    }

    /* Wrap as Obj* for return to Lisp code */
    Obj* result = mk_condition_obj(cond);
    if (!result) {
        extern Obj omni_nothing_obj;
        return &omni_nothing_obj;
    }

    return result;
}

/*
 * condition_signal - Signal a resumable condition
 *
 * Uses the effect system to signal a condition. A handler can:
 * 1. Resume with a value (like invoking a restart)
 * 2. Not resume (like aborting)
 *
 * Returns: The value passed to resume(), or NULL if not handled/resumed.
 */
Obj* condition_signal(Condition* cond) {
    if (!cond) return NULL;

    /* Initialize effect system if needed */
    if (!EFFECT_CONDITION) {
        effect_init();
    }

    /* Wrap condition in Obj */
    Obj* cond_obj = mk_condition_obj(cond);
    if (!cond_obj) return NULL;

    /* Perform the Condition effect */
    return effect_perform(EFFECT_CONDITION, cond_obj);
}

/*
 * condition_error - Signal a non-resumable error condition
 *
 * Uses the EFFECT_FAIL effect which has RECOVERY_ABORT mode.
 * Handler cannot resume from this - it must handle or propagate.
 *
 * Returns: Handler's result, or NULL if unhandled.
 */
Obj* condition_error(Condition* cond) {
    if (!cond) return NULL;

    /* Initialize effect system if needed */
    if (!EFFECT_FAIL) {
        effect_init();
    }

    /* Wrap condition in Obj */
    Obj* cond_obj = mk_condition_obj(cond);
    if (!cond_obj) return NULL;

    /* Perform the Fail effect (non-resumable) */
    return effect_perform(EFFECT_FAIL, cond_obj);
}

/*
 * condition_signal_with_message - Convenience for simple error signals
 */
Obj* condition_signal_with_message(ConditionType* type, const char* message) {
    Condition* cond = condition_create_with_message(type, message);
    if (!cond) return NULL;

    Obj* result = condition_signal(cond);

    /* Don't free condition - it may be stored in handler */
    return result;
}

/*
 * condition_from_obj - Extract Condition from TAG_CONDITION Obj
 */
Condition* condition_from_obj(Obj* obj) {
    if (!obj || !IS_BOXED(obj) || obj->tag != TAG_CONDITION) {
        return NULL;
    }
    return (Condition*)obj->ptr;
}
