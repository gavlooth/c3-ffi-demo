/*
 * spec_db.h - Specialization Database
 *
 * Database for tracking function specializations during code generation.
 * Part of Phase 27: Julia-Level Type Specialization.
 *
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 2)
 */

#ifndef OMNILISP_SPEC_DB_H
#define OMNILISP_SPEC_DB_H

#include "../analysis/type_env.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Forward Declarations ============== */

typedef struct SpecSignature SpecSignature;
typedef struct SpecDB SpecDB;

/* ============== Specialization Signature Structure ============== */

/**
 * SpecSignature - A single function specialization
 *
 * Represents a specialized version of a function for specific
 * parameter types. For example, "add_Int_Int" is a specialization
 * of the "add" function for two Int64 parameters.
 */
struct SpecSignature {
    char* func_name;           /* Base function name ("add", "mul", etc.) */
    ConcreteType** param_types; /* Array of parameter types */
    int param_count;           /* Number of parameters */
    ConcreteType* return_type;  /* Return type */
    char* mangled_name;        /* Mangled name ("add_Int_Int", etc.) */
    bool is_generated;         /* Has code been emitted for this spec? */
    bool is_builtin;           /* Is this a builtin primitive? */
    SpecSignature* next;       /* Next signature in linked list */
};

/* ============== Specialization Database Structure ============== */

/**
 * SpecDB - Database of all function specializations
 *
 * Maintains a collection of specialized function signatures and
 * provides O(1) lookup via hash table.
 */
struct SpecDB {
    SpecSignature* signatures;   /* Linked list of all signatures */
    SpecSignature** sig_table;   /* Hash table for O(1) lookup */
    int table_size;              /* Size of hash table */
    int count;                   /* Number of signatures */
};

/* ============== SpecDB API ============== */

/**
 * Create a new specialization database
 *
 * Returns:
 *   New SpecDB with empty hash table
 */
SpecDB* spec_db_new(void);

/**
 * Free a specialization database
 *
 * Args:
 *   db: Database to free
 */
void spec_db_free(SpecDB* db);

/**
 * Register a function specialization
 *
 * Adds a specialization to the database. If a specialization with
 * the same signature already exists, updates it.
 *
 * Args:
 *   db: Database
 *   func_name: Base function name
 *   param_types: Array of parameter types
 *   param_count: Number of parameters
 *   return_type: Return type
 *   is_builtin: true if this is a builtin primitive
 *
 * Returns:
 *   The SpecSignature (new or existing)
 *
 * Note:
 *   Takes ownership of param_types array (but not individual types)
 *   Increments ref_count on return_type
 */
SpecSignature* spec_db_register(SpecDB* db,
                                const char* func_name,
                                ConcreteType** param_types,
                                int param_count,
                                ConcreteType* return_type,
                                bool is_builtin);

/**
 * Lookup a specialization by signature
 *
 * Args:
 *   db: Database
 *   func_name: Function name
 *   param_types: Parameter types
 *   param_count: Number of parameters
 *
 * Returns:
 *   Found SpecSignature, or NULL if not found
 */
SpecSignature* spec_db_lookup(SpecDB* db,
                              const char* func_name,
                              ConcreteType** param_types,
                              int param_count);

/**
 * Generate a mangled name for a specialized function
 *
 * Args:
 *   func_name: Base function name
 *   param_types: Parameter types
 *   param_count: Number of parameters
 *
 * Returns:
 *   Mangled name (caller must free)
 *
 * Examples:
 *   "add" + [Int, Int] → "add_Int_Int"
 *   "mul" + [Float, Float] → "mul_Float_Float"
 */
char* spec_mangle_name(const char* func_name,
                      ConcreteType** param_types,
                      int param_count);

/**
 * Check if a specialization exists for a function call
 *
 * Args:
 *   db: Database
 *   func_name: Function being called
 *   arg_types: Types of arguments (may contain unknown types)
 *   arg_count: Number of arguments
 *
 * Returns:
 *   Matching SpecSignature, or NULL if no match
 *
 * Note:
 *   This is more lenient than spec_db_lookup - it can match
 *   when some argument types are unknown (TYPE_KIND_ANY)
 */
SpecSignature* spec_db_find_match(SpecDB* db,
                                 const char* func_name,
                                 ConcreteType** arg_types,
                                 int arg_count);

/**
 * Get all specializations for a function
 *
 * Args:
 *   db: Database
 *   func_name: Function name
 *   out_count: Output parameter for number of specializations
 *
 * Returns:
 *   Array of specializations (caller must free)
 */
SpecSignature** spec_db_get_all(SpecDB* db,
                               const char* func_name,
                               int* out_count);

/**
 * Mark a specialization as generated
 *
 * Args:
 *   sig: Specialization to mark
 */
void spec_db_mark_generated(SpecSignature* sig);

/**
 * Check if a specialization has been generated
 *
 * Args:
 *   sig: Specialization to check
 *
 * Returns:
 *   true if code has been emitted for this spec
 */
bool spec_db_is_generated(SpecSignature* sig);

/**
 * Get the hash table size
 *
 * Args:
 *   db: Database
 *
 * Returns:
 *   Current hash table size
 */
int spec_db_table_size(SpecDB* db);

/**
 * Get the number of specializations
 *
 * Args:
 *   db: Database
 *
 * Returns:
 *   Number of specializations in database
 */
int spec_db_count(SpecDB* db);

/* ============== Hash Function ============== */

/**
 * Compute hash for a function signature
 *
 * Args:
 *   func_name: Function name
 *   param_types: Parameter types
 *   param_count: Number of parameters
 *
 * Returns:
 *   Hash value
 */
unsigned int spec_hash_signature(const char* func_name,
                                ConcreteType** param_types,
                                int param_count);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_SPEC_DB_H */
