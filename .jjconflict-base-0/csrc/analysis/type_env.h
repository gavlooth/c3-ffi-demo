/*
 * type_env.h - Type Environment for Specialization
 *
 * Type environment for tracking concrete types during analysis.
 * This is part of Phase 27: Julia-Level Type Specialization.
 *
 * The type environment tracks the concrete type of each variable in
 * nested scopes, enabling type inference and specialization decisions.
 *
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 1)
 */

#ifndef OMNILISP_TYPE_ENV_H
#define OMNILISP_TYPE_ENV_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Forward Declarations ============== */

typedef struct ConcreteType ConcreteType;
typedef struct TypeBinding TypeBinding;
typedef struct TypeEnv TypeEnv;

/* ============== Type Kind Enumeration ============== */

/**
 * TypeKind - The category of a concrete type
 *
 * This distinguishes between primitive values (unboxed), typed arrays,
 * functions, and unknown/generic types.
 */
typedef enum {
    TYPE_KIND_PRIMITIVE = 0,    /* Int, Float, Char, Bool (unboxed) */
    TYPE_KIND_ARRAY,            /* Typed array (e.g., Array Float64) */
    TYPE_KIND_CLOSURE,          /* Function type */
    TYPE_KIND_ANY,              /* Unknown/generic type */
} TypeKind;

/* ============== Primitive Type Enumeration ============== */

/**
 * PrimitiveType - Specific primitive type with bit width
 *
 * These correspond to unboxed values that can be stored in registers.
 */
typedef enum {
    PRIMITIVE_INT64 = 0,        /* 64-bit signed integer */
    PRIMITIVE_FLOAT64,          /* 64-bit floating point (double) */
    PRIMITIVE_CHAR,             /* 32-bit Unicode character */
    PRIMITIVE_BOOL,             /* Boolean value */
} PrimitiveType;

/* ============== Concrete Type Structure ============== */

/**
 * ConcreteType - Full type information for a value
 *
 * This represents the complete type of a value, including:
 * - What kind of type it is (primitive, array, closure, etc.)
 * - For primitives: which primitive and bit width
 * - For arrays: element type, rank (dimensionality), mutability
 * - For closures: parameter types, return type
 *
 * This is richer than TypeID (which is just an enum), enabling
 * proper type inference and specialization.
 */
struct ConcreteType {
    TypeKind kind;

    /* Discriminated union based on kind */
    union {
        /* TYPE_KIND_PRIMITIVE */
        struct {
            PrimitiveType prim;  /* Which primitive type */
            int bit_width;       /* Bit width (32, 64, etc.) */
        } primitive;

        /* TYPE_KIND_ARRAY */
        struct {
            ConcreteType* element_type;  /* Type of elements */
            int rank;                     /* Number of dimensions (1=vector, 2=matrix) */
            bool is_mutable;              /* true=Array, false=Vector (immutable) */
        } array;

        /* TYPE_KIND_CLOSURE */
        struct {
            ConcreteType** param_types;   /* Array of parameter types */
            int param_count;              /* Number of parameters */
            ConcreteType* return_type;    /* Return type */
        } closure;

        /* TYPE_KIND_ANY has no additional data */
    };

    /* Reference counting for memory management */
    int ref_count;
};

/* ============== Type Binding Structure ============== */

/**
 * TypeBinding - A single variable-to-type binding
 *
 * Represents that a variable has a specific type in the current scope.
 */
struct TypeBinding {
    char* var_name;        /* Variable name (owned) */
    ConcreteType* type;    /* Type of the variable (borrowed ref) */
    TypeBinding* next;     /* Next binding in scope (linked list) */
};

/* ============== Type Environment Structure ============== */

/**
 * TypeEnv - Type environment for a lexical scope
 *
 * The type environment maintains a mapping from variable names to their
 * concrete types. Environments form a tree structure matching the lexical
 * nesting of the program.
 *
 * Example:
 *   (let ((x 42) (y 3.14))
 *     (let ((z (+ x y)))
 *       body))
 *
 * Creates:
 *   - Outer env: x → Int64, y → Float64
 *   - Inner env: z → Float64 (with parent → outer env)
 */
struct TypeEnv {
    TypeBinding* bindings;  /* Bindings in this scope (linked list) */
    TypeEnv* parent;        /* Parent scope (NULL for global) */
};

/* ============== Concrete Type API ============== */

/**
 * Create a primitive concrete type
 *
 * Args:
 *   prim: The primitive type (PRIMITIVE_INT64, PRIMITIVE_FLOAT64, etc.)
 *   bit_width: Bit width (typically 32 or 64)
 *
 * Returns:
 *   New ConcreteType with ref_count = 1
 *
 * Example:
 *   ConcreteType* int_type = concrete_type_primitive(PRIMITIVE_INT64, 64);
 */
ConcreteType* concrete_type_primitive(PrimitiveType prim, int bit_width);

/**
 * Create an array concrete type
 *
 * Args:
 *   element_type: Type of the array elements
 *   rank: Number of dimensions (1 for vector, 2 for matrix, etc.)
 *   is_mutable: true for mutable Array, false for immutable Vector
 *
 * Returns:
 *   New ConcreteType with ref_count = 1
 *
 * Note:
 *   Increments ref_count on element_type
 */
ConcreteType* concrete_type_array(ConcreteType* element_type,
                                  int rank,
                                  bool is_mutable);

/**
 * Create a closure (function) concrete type
 *
 * Args:
 *   param_types: Array of parameter types
 *   param_count: Number of parameters
 *   return_type: Return type
 *
 * Returns:
 *   New ConcreteType with ref_count = 1
 *
 * Note:
 *   Increments ref_count on all param_types and return_type
 */
ConcreteType* concrete_type_closure(ConcreteType** param_types,
                                    int param_count,
                                    ConcreteType* return_type);

/**
 * Create an "any" type (unknown/generic)
 *
 * Returns:
 *   Singleton ConcreteType representing unknown types
 */
ConcreteType* concrete_type_any(void);

/**
 * Increment reference count on a ConcreteType
 *
 * Args:
 *   type: Type to increment (may be NULL)
 */
void concrete_type_inc_ref(ConcreteType* type);

/**
 * Decrement reference count on a ConcreteType
 *
 * Args:
 *   type: Type to decrement (may be NULL)
 *
 * Note:
 *   Type is freed when ref_count reaches 0
 */
void concrete_type_dec_ref(ConcreteType* type);

/**
 * Check if two concrete types are equal
 *
 * Args:
 *   a: First type
 *   b: Second type
 *
 * Returns:
 *   true if types are structurally equal
 */
bool concrete_type_equals(ConcreteType* a, ConcreteType* b);

/**
 * Get string representation of a type (for debugging)
 *
 * Args:
 *   type: Type to convert
 *
 * Returns:
 *   String describing the type (e.g., "Int64", "Array Float64")
 *   Caller must free the returned string.
 */
char* concrete_type_to_string(ConcreteType* type);

/* ============== Type Environment API ============== */

/**
 * Create a new type environment
 *
 * Args:
 *   parent: Parent environment (NULL for global scope)
 *
 * Returns:
 *   New empty TypeEnv
 */
TypeEnv* type_env_new(TypeEnv* parent);

/**
 * Free a type environment
 *
 * Args:
 *   env: Environment to free (may be NULL)
 *
 * Note:
 *   Does NOT free the parent environment
 *   Decrements ref_count on all types in bindings
 */
void type_env_free(TypeEnv* env);

/**
 * Bind a variable to a type in the environment
 *
 * Args:
 *   env: Environment to bind in
 *   var_name: Variable name (copied)
 *   type: Type to bind (reference count NOT incremented)
 *
 * Note:
 *   If variable already exists in this scope, replaces the binding
 *   Does NOT search parent scopes
 *   Caller should have already incremented ref_count if sharing type
 */
void type_env_bind(TypeEnv* env, const char* var_name, ConcreteType* type);

/**
 * Lookup a variable's type
 *
 * Args:
 *   env: Environment to search
 *   var_name: Variable name to lookup
 *
 * Returns:
 *   Type of the variable, or NULL if not found
 *
 * Note:
 *   Searches current scope, then parent scopes recursively
 *   Does NOT increment ref_count on returned type
 *   Returns borrowed reference (valid while env exists)
 */
ConcreteType* type_env_lookup(TypeEnv* env, const char* var_name);

/**
 * Create a new nested scope (push)
 *
 * Args:
 *   parent: Current environment
 *
 * Returns:
 *   New environment with parent as parent scope
 *
 * Example:
 *   TypeEnv* inner = type_env_push(outer);
 *   // ... use inner ...
 *   type_env_free(inner);  // Does NOT free outer
 */
TypeEnv* type_env_push(TypeEnv* parent);

/**
 * Pop a nested scope
 *
 * Args:
 *   child: Child environment to pop
 *
 * Returns:
 *   Parent environment
 *
 * Note:
 *   Frees the child environment
 *   Parent environment is still valid
 *
 * Example:
 *   TypeEnv* outer = type_env_pop(inner);
 */
TypeEnv* type_env_pop(TypeEnv* child);

/**
 * Check if a variable is defined in the current scope
 *
 * Args:
 *   env: Environment to check
 *   var_name: Variable name
 *
 * Returns:
 *   true if variable exists in current scope (not parents)
 */
bool type_env_defined_in_current_scope(TypeEnv* env, const char* var_name);

/**
 * Get all variables in the current scope
 *
 * Args:
 *   env: Environment
 *   out_count: Output parameter for number of variables
 *
 * Returns:
 *   Array of variable names (caller must free)
 *   Array is NULL-terminated
 */
char** type_env_list_vars(TypeEnv* env, size_t* out_count);

/* ============== Utility Functions ============== */

/**
 * Get the PrimitiveType enum value from a string
 *
 * Args:
 *   name: Type name (e.g., "Int64", "Float64", "Char", "Bool")
 *
 * Returns:
 *   Corresponding PrimitiveType, or PRIMITIVE_INT64 if unknown
 */
PrimitiveType primitive_type_from_string(const char* name);

/**
 * Get the string name for a PrimitiveType
 *
 * Args:
 *   prim: Primitive type
 *
 * Returns:
 *   String name (e.g., "Int64", "Float64")
 */
const char* primitive_type_to_string(PrimitiveType prim);

/**
 * Get the bit width for a primitive type
 *
 * Args:
 *   prim: Primitive type
 *
 * Returns:
 *   Bit width (typically 32 or 64)
 */
int primitive_type_bit_width(PrimitiveType prim);

/**
 * Check if a type is a numeric primitive
 *
 * Args:
 *   type: Type to check
 *
 * Returns:
 *   true if type is PRIMITIVE_INT64 or PRIMITIVE_FLOAT64
 */
bool type_is_numeric(ConcreteType* type);

/**
 * Check if a type is unboxable (can be stored in registers)
 *
 * Args:
 *   type: Type to check
 *
 * Returns:
 *   true if type is a primitive (can be unboxed)
 */
bool type_is_unboxable(ConcreteType* type);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_TYPE_ENV_H */
