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
#include <stdio.h>
#include <stdbool.h>

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
    } defined_functions;

    /* Flags */
    bool in_tail_position;
    bool generating_header;
    bool use_runtime;         /* Use external runtime library */
    const char* runtime_path;
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

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_CODEGEN_H */
