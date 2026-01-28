/*
 * type_infer.h - Type Inference for Specialization
 *
 * Type inference module for inferring concrete types from expressions.
 * Part of Phase 27: Julia-Level Type Specialization.
 *
 * This module analyzes OmniValue AST nodes and infers their concrete types,
 * enabling the specialization engine to generate unboxed code.
 *
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 1)
 */

#ifndef OMNILISP_TYPE_INFER_H
#define OMNILISP_TYPE_INFER_H

#include "type_env.h"
#include "../ast/ast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Forward Declarations ============== */

typedef struct AnalysisContext AnalysisContext;

/* ============== Type Inference API ============== */

/**
 * Infer the type of an expression
 *
 * Analyzes an OmniValue AST node and determines its concrete type.
 * For constant expressions, returns the exact type.
 * For variables, looks up in the type environment.
 * For operations, computes the result type based on operand types.
 *
 * Args:
 *   ctx: Analysis context (for additional state)
 *   env: Type environment (for variable lookups)
 *   expr: Expression to analyze
 *
 * Returns:
 *   Inferred ConcreteType (caller must call concrete_type_dec_ref)
 *   Returns concrete_type_any() if type cannot be determined
 *
 * Example:
 *   TypeEnv* env = type_env_new(NULL);
 *   ConcreteType* t = infer_expr(NULL, env, some_int_value);
 *   // t->kind == TYPE_KIND_PRIMITIVE
 *   // t->primitive.prim == PRIMITIVE_INT64
 *   concrete_type_dec_ref(t);
 */
ConcreteType* infer_expr(AnalysisContext* ctx, TypeEnv* env, OmniValue* expr);

/**
 * Infer the type of a binary operation
 *
 * Computes the result type of a binary operation based on the types
 * of its operands and the operation being performed.
 *
 * Args:
 *   ctx: Analysis context
 *   env: Type environment
 *   op: Operation name (e.g., "+", "-", "*", "/", "<", "=")
 *   left: Left operand expression
 *   right: Right operand expression
 *
 * Returns:
 *   Result type of the operation
 *
 * Type rules:
 *   (+ Int Int) → Int
 *   (+ Float Float) → Float
 *   (+ Int Float) → Float  (promotion)
 *   (< Int Int) → Bool
 *   (< Float Float) → Bool
 */
ConcreteType* infer_binop(AnalysisContext* ctx,
                         TypeEnv* env,
                         const char* op,
                         OmniValue* left,
                         OmniValue* right);

/**
 * Infer the result type of a binary operation from operand types
 *
 * This is a lower-level version that takes already-inferred types
 * rather than expressions. Useful for codegen.
 *
 * Args:
 *   op: Operation name
 *   left_type: Type of left operand (borrowed)
 *   right_type: Type of right operand (borrowed)
 *
 * Returns:
 *   Result type (caller must dec_ref)
 */
ConcreteType* compute_binop_result(const char* op,
                                   ConcreteType* left_type,
                                   ConcreteType* right_type);

/**
 * Check if two types are compatible
 *
 * Two types are compatible if:
 *   - They are the same type, OR
 *   - One is a numeric type and the other is also numeric
 *     (for implicit conversion in mixed operations)
 *
 * Args:
 *   a: First type (borrowed)
 *   b: Second type (borrowed)
 *
 * Returns:
 *   true if types are compatible
 */
bool type_is_compatible(ConcreteType* a, ConcreteType* b);

/**
 * Check if type_a is a subtype of type_b
 *
 * Implements subtype checking with variance support for parametric types.
 *
 * Rules:
 *   - Any type is a supertype of all types (T <: Any)
 *   - Int <: Number, Float <: Number
 *   - Array{T} <: Array{S} if T <: S (covariant)
 *   - Function{[A...], R} <: Function{[B...], S} if
 *       - B[i] <: A[i] for all i (contravariant params)
 *       - R <: S (covariant return)
 *   - T <: Union{T, U, ...} (T is subtype of union containing T)
 *   - Union{T1, T2} <: U if T1 <: U and T2 <: U
 *
 * Args:
 *   a: Potential subtype (borrowed)
 *   b: Potential supertype (borrowed)
 *
 * Returns:
 *   true if a is a subtype of b
 */
bool type_is_subtype(ConcreteType* a, ConcreteType* b);

/**
 * Check if a type is a specific primitive
 *
 * Args:
 *   type: Type to check
 *   prim: Primitive type to compare against
 *
 * Returns:
 *   true if type is the specified primitive
 */
bool type_is_primitive(ConcreteType* type, PrimitiveType prim);

/**
 * Get the "common type" for two types in a mixed operation
 *
 * For example, in (Int + Float), the common type is Float.
 * Used for type promotion in mixed operations.
 *
 * Args:
 *   a: First type (borrowed)
 *   b: Second type (borrowed)
 *
 * Returns:
 *   Common type (caller must dec_ref), or concrete_type_any() if no common type
 */
ConcreteType* type_common_type(ConcreteType* a, ConcreteType* b);

/**
 * Infer the type of a literal value
 *
 * Args:
 *   expr: Literal expression (OMNI_INT, OMNI_FLOAT, OMNI_CHAR, etc.)
 *
 * Returns:
 *   Type of the literal
 *
 * Mapping:
 *   OMNI_INT    → PRIMITIVE_INT64
 *   OMNI_FLOAT  → PRIMITIVE_FLOAT64
 *   OMNI_CHAR   → PRIMITIVE_CHAR
 *   OMNI_STRING → (not implemented yet - would be TYPE_KIND_ARRAY)
 */
ConcreteType* infer_literal(OmniValue* expr);

/**
 * Infer the type of a variable reference
 *
 * Looks up the variable in the type environment.
 *
 * Args:
 *   env: Type environment
 *   var_name: Variable name
 *
 * Returns:
 *   Type of the variable, or concrete_type_any() if not found
 */
ConcreteType* infer_var(TypeEnv* env, const char* var_name);

/**
 * Check if an operation is arithmetic
 *
 * Args:
 *   op: Operation name
 *
 * Returns:
 *   true if op is +, -, *, /, mod, etc.
 */
bool is_arithmetic_op(const char* op);

/**
 * Check if an operation is comparison
 *
 * Args:
 *   op: Operation name
 *
 * Returns:
 *   true if op is <, >, <=, >=, =, etc.
 */
bool is_comparison_op(const char* op);

/**
 * Infer the type of a function application
 *
 * Analyzes a function call and infers the return type.
 * If the function type is known, returns its return type.
 * Otherwise, returns concrete_type_any().
 *
 * Args:
 *   ctx: Analysis context
 *   env: Type environment
 *   func: Function expression
 *   args: Arguments (list or array)
 *
 * Returns:
 *   Inferred return type
 */
ConcreteType* infer_apply(AnalysisContext* ctx,
                         TypeEnv* env,
                         OmniValue* func,
                         OmniValue* args);

/**
 * Infer the type of a lambda/function
 *
 * Analyzes a lambda expression and creates a closure type.
 *
 * Args:
 *   ctx: Analysis context
 *   env: Type environment
 *   params: Parameter list
 *   body: Function body
 *
 * Returns:
 *   Closure type with parameter and return types
 */
ConcreteType* infer_lambda(AnalysisContext* ctx,
                          TypeEnv* env,
                          OmniValue* params,
                          OmniValue* body);

/**
 * Infer the type of an if expression
 *
 * Analyzes an if expression and returns the common type of branches.
 *
 * Args:
 *   ctx: Analysis context
 *   env: Type environment
 *   condition: Condition expression
 *   then_branch: Then branch
 *   else_branch: Else branch (may be NULL)
 *
 * Returns:
 *   Type of the result (common type of branches)
 */
ConcreteType* infer_if(AnalysisContext* ctx,
                      TypeEnv* env,
                      OmniValue* condition,
                      OmniValue* then_branch,
                      OmniValue* else_branch);

/**
 * Infer the type of a let expression
 *
 * Analyzes a let binding and returns the type of the body.
 * Also adds the binding to the type environment.
 *
 * Args:
 *   ctx: Analysis context
 *   env: Type environment (will be extended)
 *   bindings: Binding pairs
 *   body: Body expression
 *
 * Returns:
 *   Type of the body
 *
 * Note:
 *   This function DOES modify the environment by adding bindings.
 *   Use type_env_push/pop to create a nested scope first.
 */
ConcreteType* infer_let(AnalysisContext* ctx,
                       TypeEnv* env,
                       OmniValue* bindings,
                       OmniValue* body);

/**
 * Infer the type of a type literal (e.g., {Int}, {Float})
 *
 * Args:
 *   type_lit: Type literal expression (OMNI_TYPE_LIT)
 *
 * Returns:
 *   The type itself (as a Kind object)
 */
ConcreteType* infer_type_literal(OmniValue* type_lit);

/**
 * Infer the type of a match expression
 *
 * Analyzes a match expression and returns the common type of all branches.
 *
 * Args:
 *   ctx: Analysis context
 *   env: Type environment
 *   args: Match arguments (value followed by clauses)
 *
 * Returns:
 *   Type of the result (common type of all branch results)
 *
 * Match syntax:
 *   (match value
 *     [pattern1 result1]
 *     [pattern2 result2]
 *     ...)
 */
ConcreteType* infer_match(AnalysisContext* ctx,
                         TypeEnv* env,
                         OmniValue* args);

/* ============== Utility Functions ============== */

/**
 * Get the type of an OmniValue tag
 *
 * Returns the ConcreteType corresponding to an OmniTag.
 * Useful for inferring types of tagged values.
 *
 * Args:
 *   tag: OmniTag value
 *
 * Returns:
 *   Corresponding ConcreteType
 */
ConcreteType* tag_to_concrete_type(OmniTag tag);

/**
 * Print a type to stdout (for debugging)
 *
 * Args:
 *   type: Type to print
 *   prefix: Optional prefix string
 */
void debug_print_type(ConcreteType* type, const char* prefix);

/**
 * Print the type environment (for debugging)
 *
 * Args:
 *   env: Environment to print
 *   prefix: Optional prefix string
 */
void debug_print_env(TypeEnv* env, const char* prefix);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_TYPE_INFER_H */
