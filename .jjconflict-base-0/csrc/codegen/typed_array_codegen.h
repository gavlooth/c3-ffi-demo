/*
 * typed_array_codegen.h - Typed Array Code Generator
 *
 * Generates code for typed array operations.
 *
 * Part of Phase 27: Julia-Level Type Specialization.
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 4)
 */

#ifndef OMNILISP_TYPED_ARRAY_CODEGEN_H
#define OMNILISP_TYPED_ARRAY_CODEGEN_H

#include "codegen.h"
#include "../analysis/type_env.h"
#include "../ast/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Code Generation API ============== */

/**
 * Generate typed array allocation
 *
 * Args:
 *   ctx: Code generation context
 *   var_name: Variable to store array
 *   type_name: Element type name ("Int64", "Float64", etc.)
 *   rank: Number of dimensions
 *   dimensions: Array of dimension sizes
 *
 * Example:
 *   generate_typed_array_alloc(ctx, "arr", "Float64", 1, &size)
 *   → emits: TypedArray* arr = omni_typed_array_create(r, ARRAY_TYPE_FLOAT64, 1, &size);
 */
void generate_typed_array_alloc(CodeGenContext* ctx,
                               const char* var_name,
                               const char* type_name,
                               int rank,
                               int* dimensions);

/**
 * Generate typed array get operation
 *
 * Args:
 *   ctx: Code generation context
 *   result_var: Variable to store result
 *   array_var: Array variable
 *   indices: Array of index variable names
 *   elem_type: Element type
 *
 * Example:
 *   generate_typed_array_get(ctx, "val", "arr", &idx, float_type)
 *   → emits: double val = omni_typed_array_get_float(arr, &idx);
 */
void generate_typed_array_get(CodeGenContext* ctx,
                             const char* result_var,
                             const char* array_var,
                             const char** indices,
                             ConcreteType* elem_type);

/**
 * Generate typed array set operation
 *
 * Args:
 *   ctx: Code generation context
 *   array_var: Array variable
 *   indices: Array of index variable names
 *   value_var: Value to set
 *   elem_type: Element type
 *
 * Example:
 *   generate_typed_array_set(ctx, "arr", &idx, "val", float_type)
 *   → emits: omni_typed_array_set_float(arr, &idx, val);
 */
void generate_typed_array_set(CodeGenContext* ctx,
                             const char* array_var,
                             const char** indices,
                             const char* value_var,
                             ConcreteType* elem_type);

/**
 * Generate typed array fill operation
 *
 * Args:
 *   ctx: Code generation context
 *   array_var: Array variable
 *   value_var: Value to fill with
 *
 * Example:
 *   generate_typed_array_fill(ctx, "arr", "val")
 *   → emits: omni_typed_array_fill(arr, val);
 */
void generate_typed_array_fill(CodeGenContext* ctx,
                              const char* array_var,
                              const char* value_var);

/**
 * Generate typed array to list conversion
 *
 * Args:
 *   ctx: Code generation context
 *   result_var: Variable to store result
 *   array_var: Array variable
 *
 * Example:
 *   generate_typed_array_to_list(ctx, "lst", "arr")
 *   → emits: Obj* lst = omni_typed_array_to_list(arr, r);
 */
void generate_typed_array_to_list(CodeGenContext* ctx,
                                 const char* result_var,
                                 const char* array_var);

/**
 * Generate list to typed array conversion
 *
 * Args:
 *   ctx: Code generation context
 *   result_var: Variable to store result
 *   list_var: List variable
 *   elem_type: Element type
 *
 * Example:
 *   generate_list_to_typed_array(ctx, "arr", "lst", float_type)
 *   → emits: TypedArray* arr = omni_list_to_typed_array(r, ARRAY_TYPE_FLOAT64, lst);
 */
void generate_list_to_typed_array(CodeGenContext* ctx,
                                 const char* result_var,
                                 const char* list_var,
                                 ConcreteType* elem_type);

/* ============== Type Name Utilities ============== */

/**
 * Get the ArrayElementType enum value for a ConcreteType
 *
 * Args:
 *   type: Concrete type
 *
 * Returns:
 *   ArrayElementType name (caller must free)
 */
char* get_array_element_type_name(ConcreteType* type);

/**
 * Get the typed array get function for a type
 *
 * Args:
 *   type: Element type
 *
 * Returns:
 *   Function name (caller must free)
 */
char* get_typed_array_get_function(ConcreteType* type);

/**
 * Get the typed array set function for a type
 *
 * Args:
 *   type: Element type
 *
 * Returns:
 *   Function name (caller must free)
 */
char* get_typed_array_set_function(ConcreteType* type);

/* ============== Helper Macros ============== */

/**
 * Generate code to check if a value is a typed array
 *
 * Args:
 *   ctx: Code generation context
 *   value_var: Value to check
 *   elem_type: Expected element type
 *   label_true: Label to jump if true
 *   label_false: Label to jump if false
 */
void generate_is_typed_array_check(CodeGenContext* ctx,
                                   const char* value_var,
                                   ConcreteType* elem_type,
                                   const char* label_true,
                                   const char* label_false);

/**
 * Generate code to get array element count
 *
 * Args:
 *   ctx: Code generation context
 *   result_var: Variable to store count
 *   array_var: Array variable
 */
void generate_typed_array_length(CodeGenContext* ctx,
                                const char* result_var,
                                const char* array_var);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_TYPED_ARRAY_CODEGEN_H */
