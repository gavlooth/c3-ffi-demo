/*
 * spec_codegen.h - Specialized Code Generator
 *
 * Generates specialized C code for functions with unboxed primitive values.
 *
 * Part of Phase 27: Julia-Level Type Specialization.
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 3)
 */

#ifndef OMNILISP_SPEC_CODEGEN_H
#define OMNILISP_SPEC_CODEGEN_H

#include "spec_db.h"
#include "../analysis/type_env.h"
#include "../ast/ast.h"
#include "codegen.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Code Generation Context ============== */

/**
 * SpecCodegenContext - Extended context for specialized code generation
 */
typedef struct {
    CodeGenContext* base;      /* Base codegen context */
    SpecDB* spec_db;           /* Specialization database */
    TypeEnv* type_env;         /* Type environment */
    FILE* specialized_output;  /* Separate file for specialized code */
    int spec_count;            /* Number of specializations generated */
} SpecCodegenContext;

/* ============== SpecCodegen API ============== */

/**
 * Create a new specialized codegen context
 *
 * Args:
 *   base: Base codegen context
 *   spec_db: Specialization database
 *   type_env: Type environment
 *
 * Returns:
 *   New SpecCodegenContext
 */
SpecCodegenContext* spec_codegen_new(CodeGenContext* base,
                                    SpecDB* spec_db,
                                    TypeEnv* type_env);

/**
 * Free a specialized codegen context
 *
 * Args:
 *   ctx: Context to free
 */
void spec_codegen_free(SpecCodegenContext* ctx);

/* ============== Function Generation ============== */

/**
 * Generate a specialized function
 *
 * Emits C code for a specialized version of a function that uses
 * unboxed primitive values directly in registers.
 *
 * Args:
 *   ctx: Specialized codegen context
 *   sig: Specialization signature
 *   func_body: Function body AST
 *
 * Generated code pattern:
 *   ```c
 *   int64_t add_Int_Int(int64_t x, int64_t y) {
 *       return x + y;
 *   }
 *   ```
 */
void generate_specialized_function(SpecCodegenContext* ctx,
                                  SpecSignature* sig,
                                  OmniValue* func_body);

/**
 * Generate function prologue with unboxed parameters
 *
 * Emits the function signature and opening brace.
 *
 * Args:
 *   ctx: Context
 *   sig: Specialization signature
 */
void generate_spec_prologue(SpecCodegenContext* ctx, SpecSignature* sig);

/**
 * Generate function epilogue with boxed return value
 *
 * Emits the closing brace and return statement.
 *
 * Args:
 *   ctx: Context
 *   sig: Specialization signature
 *   result_var: Variable containing the result
 */
void generate_spec_epilogue(SpecCodegenContext* ctx,
                           SpecSignature* sig,
                           const char* result_var);

/* ============== Unboxed Operations ============== */

/**
 * Generate an unboxed binary operation
 *
 * Emits code for an operation on unboxed values.
 *
 * Args:
 *   ctx: Context
 *   op: Operation name ("+", "-", "*", "/", "<", etc.)
 *   left: Left operand variable name
 *   right: Right operand variable name
 *   result_type: Type of the result
 *   result_var: Variable to store result
 *
 * Example:
 *   generate_unboxed_binop(ctx, "+", "x", "y", int_type, "result")
 *   → emits: "result = x + y;"
 */
void generate_unboxed_binop(SpecCodegenContext* ctx,
                           const char* op,
                           const char* left,
                           const char* right,
                           ConcreteType* result_type,
                           const char* result_var);

/**
 * Get the C type name for a ConcreteType
 *
 * Args:
 *   type: Concrete type
 *
 * Returns:
 *   C type name ("int64_t", "double", "char", "bool", etc.)
 *   Caller must free the returned string.
 */
char* get_c_type_name(ConcreteType* type);

/**
 * Get the default value for a type
 *
 * Args:
 *   type: Concrete type
 *
 * Returns:
 *   Default value as string ("0", "0.0", "'\\0'", "false", etc.)
 *   Caller must free the returned string.
 */
char* get_type_default(ConcreteType* type);

/* ============== Box/Unbox Operations ============== */

/**
 * Generate code to unbox a value
 *
 * Converts a boxed Obj* to an unboxed primitive value.
 *
 * Args:
 *   ctx: Context
 *   obj_var: Variable containing boxed value
 *   type: Type to unbox to
 *   unboxed_var: Variable to store unboxed value
 *
 * Example:
 *   generate_unbox(ctx, "boxed_val", int_type, "unboxed")
 *   → emits: "int64_t unboxed = unbox_int(boxed_val);"
 */
void generate_unbox(SpecCodegenContext* ctx,
                   const char* obj_var,
                   ConcreteType* type,
                   const char* unboxed_var);

/**
 * Generate code to box a value
 *
 * Converts an unboxed primitive value to a boxed Obj*.
 *
 * Args:
 *   ctx: Context
 *   unboxed_var: Variable containing unboxed value
 *   type: Type of the unboxed value
 *   obj_var: Variable to store boxed value
 *
 * Example:
 *   generate_box(ctx, "unboxed", int_type, "boxed")
 *   → emits: "Obj* boxed = box_int(unboxed);"
 */
void generate_box(SpecCodegenContext* ctx,
                 const char* unboxed_var,
                 ConcreteType* type,
                 const char* obj_var);

/**
 * Generate unbox function declaration
 *
 * Declares an unbox function for a specific type.
 *
 * Args:
 *   ctx: Context
 *   type: Type to unbox
 */
void generate_unbox_decl(SpecCodegenContext* ctx, ConcreteType* type);

/**
 * Generate box function declaration
 *
 * Declares a box function for a specific type.
 *
 * Args:
 *   ctx: Context
 *   type: Type to box
 */
void generate_box_decl(SpecCodegenContext* ctx, ConcreteType* type);

/* ============== Call Generation ============== */

/**
 * Generate a specialized function call
 *
 * Emits a call to a specialized function with unboxed arguments.
 *
 * Args:
 *   ctx: Context
 *   sig: Specialization being called
 *   args: Array of argument variable names
 *   result_var: Variable to store result
 */
void generate_specialized_call(SpecCodegenContext* ctx,
                              SpecSignature* sig,
                              const char** args,
                              const char* result_var);

/**
 * Generate a generic function call (fallback)
 *
 * Emits a call to the generic version of a function.
 *
 * Args:
 *   ctx: Context
 *   func_name: Function name
 *   args: Array of argument variable names
 *   arg_count: Number of arguments
 *   result_var: Variable to store result
 */
void generate_generic_call(SpecCodegenContext* ctx,
                          const char* func_name,
                          const char** args,
                          int arg_count,
                          const char* result_var);

/**
 * Generate a dispatch call (specialized or generic)
 *
 * Automatically chooses specialized or generic based on types.
 *
 * Args:
 *   ctx: Context
 *   func_name: Function name
 *   args: Array of argument variable names
 *   arg_types: Array of argument types
 *   arg_count: Number of arguments
 *   result_var: Variable to store result
 */
void generate_dispatch_call(SpecCodegenContext* ctx,
                           const char* func_name,
                           const char** args,
                           ConcreteType** arg_types,
                           int arg_count,
                           const char* result_var);

/* ============== Utility Functions ============== */

/**
 * Get the mangled C function name for a specialization
 *
 * Args:
 *   sig: Specialization signature
 *
 * Returns:
 *   Mangled name (caller must free)
 */
char* get_spec_function_name(SpecSignature* sig);

/**
 * Check if a type needs boxing/unboxing
 *
 * Args:
 *   type: Type to check
 *
 * Returns:
 *   true if type is unboxable (primitive)
 */
bool type_needs_boxing(ConcreteType* type);

/**
 * Get the unbox function name for a type
 *
 * Args:
 *   type: Type to unbox
 *
 * Returns:
 *   Function name (caller must free)
 */
char* get_unbox_function(ConcreteType* type);

/**
 * Get the box function name for a type
 *
 * Args:
 *   type: Type to box
 *
 * Returns:
 *   Function name (caller must free)
 */
char* get_box_function(ConcreteType* type);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_SPEC_CODEGEN_H */
