/*
 * OmniLisp Code Generator
 *
 * Generates C99 + POSIX code with ASAP memory management.
 * - Emits standalone C programs
 * - Injects free_obj() calls based on analysis
 * - Supports closures, continuations, and concurrency
 */

#ifndef OMNILISP_CODEGEN_H
#define OMNILISP_CODEGEN_H

#include "../ast/ast.h"
#include "../analysis/analysis.h"
#include "../util/strmap.h"
#include <stdio.h>
#include <stdbool.h>

/* ============== Phase 27: Type Specialization Headers ============== */
/* These must be included before CodeGenContext because it uses Phase 27 types */
#include "spec_db.h"
#include "../analysis/type_env.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Code Generator State ============== */

typedef struct CodeGenContext {
    /* Output stream */
    FILE* output;
    char* output_buffer;      /* For in-memory generation */
    size_t output_size;
    size_t output_capacity;

    /* Analysis context */
    AnalysisContext* analysis;

    /* Generation state */
    int indent_level;
    int temp_counter;
    int label_counter;
    int lambda_counter;

    /* Symbol table for generated names */
    struct {
        char** names;
        char** c_names;
        size_t count;
        size_t capacity;
        StrMap* map;              /* Optimization: O(1) symbol lookup */
    } symbols;

    /* Forward declarations needed */
    struct {
        char** decls;
        size_t count;
        size_t capacity;
    } forward_decls;

    /* Lambda (closure) definitions */
    struct {
        char** defs;
        size_t count;
        size_t capacity;
    } lambda_defs;

    /* Function definition tracking for multiple dispatch */
    /* Tracks which functions have been defined to detect redefinitions */
    struct {
        char** names;          /* Function names (mangled) */
        int* definition_count; /* Number of times each function was defined */
        size_t count;
        size_t capacity;
        StrMap* map;           /* Optimization: O(1) function lookup (stores index) */
    } defined_functions;

    /* Flags */
    bool in_tail_position;
    bool generating_header;
    bool use_runtime;         /* Use external runtime library */
    const char* runtime_path;
    bool first_pass;          /* True during first pass (emit funcs only, no init code) */

    /* Phase 27: Type Specialization */
    SpecDB* spec_db;           /* Database of function specializations */
    TypeEnv* type_env;         /* Type environment for tracking types */
    bool enable_specialization;  /* Enable type specialization (default: true) */

    /* Current source position for debug info / retention insertion */
    int current_pos;

    /* Issue 3 P2: Non-lexical region end */
    int* region_exited;        /* Track which regions have been exited */

    /* Current captures for lambda generation (pointer to CaptureList in codegen.c) */
    void* current_captures;    /* Captures for current lambda being generated */

    /* Phase 0: FFI counter for unique ccall site identifiers */
    int ffi_counter;           /* Unique ID for FFI call sites (dlopen/dlsym caching) */

    /* Module compilation mode */
    bool shared_mode;          /* Generate shared library init function instead of main() */
    const char* module_name;   /* Module name for init function (e.g., "mymodule" -> _omni_module_mymodule_init) */

    /* Phase 22: Track resumption symbols for effect handlers */
    StrMap* resumption_symbols;  /* c_names that are resumption objects (effect handlers) */
} CodeGenContext;

/* ============== Code Generator API ============== */

/* Create a new code generator writing to a file */
CodeGenContext* omni_codegen_new(FILE* output);

/* Create a new code generator writing to memory */
CodeGenContext* omni_codegen_new_buffer(void);

/* Free code generator resources */
void omni_codegen_free(CodeGenContext* ctx);

/* Get generated code as string (for buffer mode) */
char* omni_codegen_get_output(CodeGenContext* ctx);

/* Set external runtime path */
void omni_codegen_set_runtime(CodeGenContext* ctx, const char* path);

/* ============== Code Generation ============== */

/* Generate a complete C program from parsed expressions */
void omni_codegen_program(CodeGenContext* ctx, OmniValue** exprs, size_t count);

/* Generate code for a single expression */
void omni_codegen_expr(CodeGenContext* ctx, OmniValue* expr);

/* Generate the runtime header (types, macros, etc.) */
void omni_codegen_runtime_header(CodeGenContext* ctx);

/* Generate the main function wrapper */
void omni_codegen_main(CodeGenContext* ctx, OmniValue** exprs, size_t count);

/* Generate module init function for shared library mode */
void omni_codegen_module_init(CodeGenContext* ctx, OmniValue** exprs, size_t count);

/* Set shared mode for module compilation */
void omni_codegen_set_shared_mode(CodeGenContext* ctx, const char* module_name);

/* ============== Expression Compilation ============== */

/* Generate code for a define expression */
void omni_codegen_define(CodeGenContext* ctx, OmniValue* expr);

/* Generate code for a let expression */
void omni_codegen_let(CodeGenContext* ctx, OmniValue* expr);

/* Generate code for a lambda expression */
void omni_codegen_lambda(CodeGenContext* ctx, OmniValue* expr);

/* Generate code for an if expression */
void omni_codegen_if(CodeGenContext* ctx, OmniValue* expr);

/* Generate code for a function application */
void omni_codegen_apply(CodeGenContext* ctx, OmniValue* expr);

/* Generate code for a quote expression */
void omni_codegen_quote(CodeGenContext* ctx, OmniValue* expr);

/* ============== Utilities ============== */

/* Mangle a symbol name for C */
char* omni_codegen_mangle(const char* name);

/* Generate a fresh temporary variable name */
char* omni_codegen_temp(CodeGenContext* ctx);

/* Generate a fresh label name */
char* omni_codegen_label(CodeGenContext* ctx);

/* Emit indented text */
void omni_codegen_emit(CodeGenContext* ctx, const char* fmt, ...);

/* Emit text without indentation */
void omni_codegen_emit_raw(CodeGenContext* ctx, const char* fmt, ...);

/* Increase/decrease indentation */
void omni_codegen_indent(CodeGenContext* ctx);
void omni_codegen_dedent(CodeGenContext* ctx);

/* Register a forward declaration */
void omni_codegen_add_forward_decl(CodeGenContext* ctx, const char* decl);

/* Register a lambda definition */
void omni_codegen_add_lambda_def(CodeGenContext* ctx, const char* def);

/* ============== ASAP Memory Management ============== */

/* Emit free_obj calls for variables at given position */
void omni_codegen_emit_frees(CodeGenContext* ctx, int position);

/* Issue 3 P2: Emit region_exit calls at last-use positions (non-lexical ends) */
void omni_codegen_emit_region_exits(CodeGenContext* ctx, int position);

/* ============== Phase 24: Region-Level Metadata Codegen ============== */

/*
 * Emit allocation using alloc_obj_typed() with type_id
 *
 * Generates: Obj* var = alloc_obj_typed(region, TYPE_ID_XXX);
 *
 * Args:
 *   ctx: Codegen context
 *   var_name: Variable name to allocate
 *   region_name: Region to allocate from (e.g., "_local_region")
 *   type_id: TypeID enum value (compile-time constant)
 *
 * Example:
 *   omni_codegen_emit_typed_alloc(ctx, "x", "_local_region", TYPE_ID_INT);
 *   // Generates: Obj* x = alloc_obj_typed(_local_region, TYPE_ID_INT);
 */
void omni_codegen_emit_typed_alloc(CodeGenContext* ctx,
                                   const char* var_name,
                                   const char* region_name,
                                   int type_id);

/*
 * Get type_id for a variable from analysis context
 *
 * Returns the TypeID enum value assigned during type inference,
 * or TYPE_ID_GENERIC if unknown.
 */
int omni_codegen_get_var_type_id(CodeGenContext* ctx, const char* var_name);

/* ============== User Type Code Generation ============== */

/* Generate C struct definition for a user type */
void omni_codegen_type_struct(CodeGenContext* ctx, TypeDef* type);

/* Generate constructor function for a user type (mk-TypeName) */
void omni_codegen_type_constructor(CodeGenContext* ctx, TypeDef* type);

/* Generate field accessor for a type (TypeName-field) */
void omni_codegen_type_accessor(CodeGenContext* ctx, TypeDef* type, TypeField* field);

/* Generate field mutator for a type (set-TypeName-field!)
 * - Uses SET_WEAK for weak fields
 * - Uses inc_ref/dec_ref for strong fields
 */
void omni_codegen_type_mutator(CodeGenContext* ctx, TypeDef* type, TypeField* field);

/* Generate release function for a type (release_TypeName)
 * - Decrements RC for strong fields only
 * - Skips weak fields (they're borrowed, not owned)
 */
void omni_codegen_type_release(CodeGenContext* ctx, TypeDef* type);

/* Generate all code for a user type (struct + constructor + accessors + mutators + release) */
void omni_codegen_type_full(CodeGenContext* ctx, TypeDef* type);

/* Generate type definitions for all registered types */
void omni_codegen_all_types(CodeGenContext* ctx);

/* ============== CFG-Based Code Generation ============== */

/*
 * Generate code with CFG-aware free placement.
 * This uses liveness analysis to free variables at their last use point
 * on each control flow path, rather than at scope exit.
 *
 * Example:
 *   (let ((x (mk-obj)))
 *     (if cond
 *       (use x)     ;; x freed here on true branch
 *       (other)))   ;; x freed here on false branch (if unused)
 *
 * Instead of:
 *   (let ((x (mk-obj)))
 *     (if cond
 *       (use x)
 *       (other))
 *     ;; x freed here at scope end (too late!)
 */
void omni_codegen_with_cfg(CodeGenContext* ctx, OmniValue* expr);

/*
 * Emit frees for a specific CFG node.
 * Called during CFG-aware code generation.
 */
void omni_codegen_emit_cfg_frees(CodeGenContext* ctx, CFG* cfg, CFGNode* node);

/* ============== Shape-Aware Memory Management ============== */

/*
 * Generate shape-aware free function for a type.
 * Strategy depends on the type's shape:
 *   - TREE: Simple recursive free (free_tree)
 *   - DAG: Reference counting (dec_ref)
 *   - CYCLIC + frozen: SCC-based RC
 *   - CYCLIC + mutable: Auto-weak back-edges + dec_ref
 *   - Unknown: Arena allocation fallback
 */
void omni_codegen_shape_aware_free(CodeGenContext* ctx, TypeDef* type);

/*
 * Generate the runtime helpers for shape-aware memory management:
 *   - free_tree_TypeName: For tree-shaped types
 *   - arena_alloc/arena_destroy: For cyclic structures
 *   - scc_decref: For frozen cyclic structures
 */
void omni_codegen_shape_helpers(CodeGenContext* ctx);

/*
 * Emit the appropriate deallocation call based on type shape.
 * Called when generating code that frees a value of a known type.
 */
void omni_codegen_emit_shape_free(CodeGenContext* ctx, const char* var_name,
                                   const char* type_name);

/* ============== Phase 15: Branch-Level Region Narrowing ============== */

/*
 * Emit allocation based on scoped escape analysis.
 * If the variable doesn't escape its scope (ESCAPE_TARGET_NONE),
 * emit a stack allocation. Otherwise, use the default region allocation.
 */
void omni_codegen_emit_narrowed_alloc(CodeGenContext* ctx, const char* var_name,
                                      const char* type_name);

/*
 * Emit cleanup code for scope exit.
 * Frees variables that don't escape the scope. Called at the end of
 * if branches, let bodies, etc.
 */
void omni_codegen_emit_scope_cleanup(CodeGenContext* ctx, ScopeInfo* scope);

/*
 * Generate code for 'if' with branch-level narrowing.
 * Each branch gets its own scope. Non-escaping variables in each branch
 * are stack-allocated and cleaned up at the end of the branch.
 */
void omni_codegen_if_narrowed(CodeGenContext* ctx, OmniValue* expr);

/*
 * Generate code for 'let' with scope-aware allocation.
 * Variables in the let that don't escape the let body are stack-allocated.
 * Cleanup is emitted at the end of the let block.
 */
void omni_codegen_let_narrowed(CodeGenContext* ctx, OmniValue* expr);

/* ============== Phase 27: Type Specialization Integration ============== */

/*
 * Initialize type specialization for code generation.
 *
 * Creates SpecDB and TypeEnv for tracking specializations and types.
 * Call this at the start of code generation to enable specialization.
 *
 * Args:
 *   ctx: Codegen context to initialize
 *
 * Returns:
 *   true on success, false on failure
 */
bool omni_codegen_init_specialization(CodeGenContext* ctx);

/*
 * Clean up type specialization resources.
 *
 * Frees SpecDB and TypeEnv. Call after code generation completes.
 *
 * Args:
 *   ctx: Codegen context to clean up
 */
void omni_codegen_cleanup_specialization(CodeGenContext* ctx);

/*
 * Enable or disable type specialization.
 *
 * Args:
 *   ctx: Codegen context
 *   enable: true to enable, false to disable
 */
void omni_codegen_set_specialization(CodeGenContext* ctx, bool enable);

/*
 * Generate code for a function call with type dispatch.
 *
 * Automatically chooses between specialized and generic versions
 * based on argument types.
 *
 * Args:
 *   ctx: Codegen context
 *   func_name: Function name
 *   args: Argument expressions
 *   arg_types: Known types of arguments (can be NULL for unknown)
 *   arg_count: Number of arguments
 *
 * Emits:
 *   Specialized call if types known and specialization exists
 *   Generic call otherwise
 */
void omni_codegen_dispatch_call(CodeGenContext* ctx,
                              const char* func_name,
                              OmniValue** args,
                              ConcreteType** arg_types,
                              int arg_count);

/*
 * Get the type of an expression during code generation.
 *
 * Uses the type environment to look up variable types and
 * infer expression types.
 *
 * Args:
 *   ctx: Codegen context
 *   expr: Expression to analyze
 *
 * Returns:
 *   Inferred type (borrowed reference, valid while type_env exists)
 */
ConcreteType* omni_codegen_get_expr_type(CodeGenContext* ctx, OmniValue* expr);

/*
 * Register a function specialization.
 *
 * Records that a specialized version of a function exists for
 * specific parameter types.
 *
 * Args:
 *   ctx: Codegen context
 *   func_name: Function name
 *   param_types: Parameter types
 *   param_count: Number of parameters
 *   return_type: Return type
 *   is_builtin: true for builtin primitives
 *
 * Returns:
 *   SpecSignature for the specialization
 */
SpecSignature* omni_codegen_register_specialization(CodeGenContext* ctx,
                                                   const char* func_name,
                                                   ConcreteType** param_types,
                                                   int param_count,
                                                   ConcreteType* return_type,
                                                   bool is_builtin);

/*
 * Check if a specialized version exists for a function call.
 *
 * Args:
 *   ctx: Codegen context
 *   func_name: Function name
 *   arg_types: Argument types
 *   arg_count: Number of arguments
 *
 * Returns:
 *   SpecSignature if specialization exists, NULL otherwise
 */
SpecSignature* omni_codegen_lookup_specialization(CodeGenContext* ctx,
                                                const char* func_name,
                                                ConcreteType** arg_types,
                                                int arg_count);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_CODEGEN_H */
