/*
 * condition.h - Structured Condition System for OmniLisp
 *
 * Provides Common Lisp-style conditions with:
 * - Condition type hierarchy
 * - Structured condition objects with slots
 * - Condition type registry
 *
 * Part of T-cond-core-types implementation.
 */

#ifndef OMNI_CONDITION_H
#define OMNI_CONDITION_H

#include "types.h"
#include <stdint.h>

/* Forward declarations */
typedef struct Condition Condition;
typedef struct ConditionType ConditionType;
typedef struct ConditionSlot ConditionSlot;

/*
 * Condition slot - named field in a condition
 */
struct ConditionSlot {
    const char* name;           /* Slot name (e.g., "message", "datum") */
    void* value;                /* Slot value (Obj* or string) */
    int is_string;              /* 1 if value is a C string, 0 if Obj* */
    ConditionSlot* next;        /* Next slot in chain */
};

/*
 * Condition type - describes a class of conditions
 */
struct ConditionType {
    const char* name;           /* Type name (e.g., ":error", ":type-error") */
    uint32_t id;                /* Unique type ID */
    ConditionType* parent;      /* Parent type (for inheritance) */
    const char** slot_names;    /* Required slot names (NULL-terminated) */
    int slot_count;             /* Number of required slots */
    ConditionType* next;        /* Next in registry chain */
};

/*
 * Condition instance - a signaled condition
 */
struct Condition {
    ConditionType* type;        /* Condition type */
    ConditionSlot* slots;       /* Slot values */
    const char* source_file;    /* Source file where signaled (optional) */
    int source_line;            /* Source line (optional) */
    int source_col;             /* Source column (optional) */
    Condition* cause;           /* Chained cause (optional) */
};

/* ============================================================
 * Condition Type Registry
 * ============================================================ */

/* Initialize the condition system and register base types */
void condition_init(void);

/* Register a new condition type */
ConditionType* condition_type_register(
    const char* name,
    ConditionType* parent,
    const char** slot_names,
    int slot_count
);

/* Look up a condition type by name */
ConditionType* condition_type_lookup(const char* name);

/* Look up a condition type by ID */
ConditionType* condition_type_lookup_id(uint32_t id);

/* Check if a type is a subtype of another */
int condition_type_is_subtype(ConditionType* sub, ConditionType* super);

/* ============================================================
 * Base Condition Types (registered at init)
 * ============================================================ */

/* Root of all conditions */
extern ConditionType* COND_CONDITION;

/* Error conditions (serious, usually unrecoverable) */
extern ConditionType* COND_ERROR;
extern ConditionType* COND_TYPE_ERROR;
extern ConditionType* COND_ARITHMETIC_ERROR;
extern ConditionType* COND_DIVISION_BY_ZERO;
extern ConditionType* COND_UNBOUND_VARIABLE;
extern ConditionType* COND_UNDEFINED_FUNCTION;

/* Memory errors */
extern ConditionType* COND_MEMORY_ERROR;
extern ConditionType* COND_USE_AFTER_FREE;
extern ConditionType* COND_DOUBLE_FREE;
extern ConditionType* COND_REGION_MISMATCH;

/* FFI errors */
extern ConditionType* COND_FFI_ERROR;
extern ConditionType* COND_FFI_TYPE_MISMATCH;
extern ConditionType* COND_FFI_LOAD_ERROR;

/* IO errors */
extern ConditionType* COND_IO_ERROR;
extern ConditionType* COND_FILE_ERROR;
extern ConditionType* COND_END_OF_FILE;

/* Warnings (less serious, can often continue) */
extern ConditionType* COND_WARNING;
extern ConditionType* COND_STYLE_WARNING;
extern ConditionType* COND_DEPRECATION_WARNING;

/* ============================================================
 * Condition Creation and Access
 * ============================================================ */

/* Create a new condition of the given type */
Condition* condition_create(ConditionType* type);

/* Create a condition with a message */
Condition* condition_create_with_message(ConditionType* type, const char* message);

/* Set a slot value (string) */
void condition_set_slot_string(Condition* cond, const char* name, const char* value);

/* Set a slot value (Obj*) */
void condition_set_slot_value(Condition* cond, const char* name, void* value);

/* Get a slot value */
void* condition_get_slot(Condition* cond, const char* name, int* is_string);

/* Get the message slot (convenience) */
const char* condition_get_message(Condition* cond);

/* Set source location */
void condition_set_location(Condition* cond, const char* file, int line, int col);

/* Set chained cause */
void condition_set_cause(Condition* cond, Condition* cause);

/* Free a condition */
void condition_free(Condition* cond);

/* ============================================================
 * Condition Rendering
 * ============================================================ */

/* Render condition to string (caller must free) */
char* condition_to_string(Condition* cond);

/* Print condition to stderr */
void condition_print(Condition* cond);

/* Print condition with full details (including cause chain) */
void condition_print_full(Condition* cond);

/* ============================================================
 * Quick Constructors for Common Conditions
 * ============================================================ */

/* Create a simple error with message */
Condition* make_error(const char* message);

/* Create a type error */
Condition* make_type_error(const char* expected, const char* got, void* datum);

/* Create an unbound variable error */
Condition* make_unbound_variable(const char* name);

/* Create an undefined function error */
Condition* make_undefined_function(const char* name);

/* Create division by zero error */
Condition* make_division_by_zero(void);

/* Create FFI error */
Condition* make_ffi_error(const char* message, const char* function_name);

/* Create memory error */
Condition* make_memory_error(const char* message, void* address);

#endif /* OMNI_CONDITION_H */
