/*
 * type_id.h - Compiler Type ID Constants for Region-Level Metadata
 *
 * This header defines the TypeID enum that maps OmniLisp types to
 * their corresponding runtime TypeID values. This enables the compiler
 * to emit compile-time type constants for use with alloc_obj_typed().
 *
 * Phase 24: Region-Level Metadata Optimization
 * Task: T-opt-region-metadata-compiler
 */

#ifndef OMNILISP_TYPE_ID_H
#define OMNILISP_TYPE_ID_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * TypeID - Compile-time type identifier constants
 *
 * These values MUST match the TypeID enum in runtime/include/omni.h.
 * If omni.h has been included, use its TypeID definition.
 * Otherwise, define our own (for compiler-only code).
 */
#ifndef OMNI_TYPE_ID_DEFINED
#define OMNI_TYPE_ID_DEFINED
typedef enum {
    TYPE_ID_INT = 0,
    TYPE_ID_FLOAT,
    TYPE_ID_CHAR,
    TYPE_ID_PAIR,      /* Cons cell */
    TYPE_ID_ARRAY,
    TYPE_ID_STRING,
    TYPE_ID_SYMBOL,
    TYPE_ID_DICT,
    TYPE_ID_CLOSURE,
    TYPE_ID_BOX,
    TYPE_ID_CHANNEL,
    TYPE_ID_THREAD,
    TYPE_ID_ERROR,
    TYPE_ID_ATOM,
    TYPE_ID_TUPLE,
    TYPE_ID_NAMED_TUPLE,
    TYPE_ID_GENERIC,
    TYPE_ID_KIND,
    TYPE_ID_NOTHING,
    TYPE_ID_MAX
} TypeID;
#endif /* OMNI_TYPE_ID_DEFINED */

/*
 * Type name to TypeID mapping function
 *
 * Maps string type names (from type inference) to TypeID enum values.
 * Used during analysis to assign type_id to variables.
 *
 * Args:
 *   type_name: String type name (e.g., "Int", "Pair", "Array")
 *
 * Returns:
 *   Corresponding TypeID enum value, or TYPE_ID_GENERIC if unknown
 */
TypeID type_name_to_type_id(const char* type_name);

/*
 * TypeID to string mapping function (for debugging)
 *
 * Args:
 *   type_id: TypeID enum value
 *
 * Returns:
 *   String name of the type (e.g., "Int", "Pair")
 */
const char* type_id_to_name(TypeID type_id);

/*
 * Check if a TypeID value is valid
 *
 * Args:
 *   type_id: TypeID enum value
 *
 * Returns:
 *   true if type_id is in valid range [0, TYPE_ID_MAX)
 */
bool is_valid_type_id(TypeID type_id);

/* ============== Phase 24: Inline Allocation Metadata Queries ============== */

/*
 * Check if a type can be inline-allocated
 *
 * This corresponds to the can_inline field in TypeMetadata.
 * Types that can be inlined are typically small (<= 64 bytes) and
 * have simple structure (no complex nested pointers).
 *
 * Args:
 *   type_id: TypeID enum value
 *
 * Returns:
 *   true if the type can be allocated in the inline buffer
 *
 * Note: This is a compile-time approximation. The actual decision
 * is made at runtime by region_alloc_typed() based on available
 * inline buffer capacity.
 */
bool type_id_can_inline(TypeID type_id);

/*
 * Get the inline threshold for a type
 *
 * This corresponds to the inline_threshold field in TypeMetadata.
 * Objects smaller than this threshold can be allocated in the
 * inline buffer when space is available.
 *
 * Args:
 *   type_id: TypeID enum value
 *
 * Returns:
 *   Maximum size in bytes for inline allocation, or 0 if not inlineable
 */
size_t type_id_inline_threshold(TypeID type_id);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_TYPE_ID_H */
