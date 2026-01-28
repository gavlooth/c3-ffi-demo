/*
 * OmniLisp Code Generator Implementation
 *
 * Generates C99 + POSIX code with ASAP memory management.
 * Supports Region-RC: Region lifecycle, transmigration, and tethering.
 */

#include "codegen.h"
#include "region_codegen.h"  /* Region-RC code generation extensions */
#include "../analysis/region_inference.h"  /* Region inference pass */
#include "../analysis/type_id.h"  /* Phase 24: Type ID constants */
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/* ============== Phase 27: Type Specialization Includes ============== */

#include "spec_db.h"        /* Specialization database */
#include "../analysis/type_env.h"     /* Type environment */
#include "../analysis/type_infer.h"   /* Type inference */

/* ============== Phase 27: strdup Fix for C99 ============== */

/* strdup is not part of C99 standard, provide our own implementation */
static char* omni_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

/* ============== Context Management ============== */

CodeGenContext* omni_codegen_new(FILE* output) {
    CodeGenContext* ctx = malloc(sizeof(CodeGenContext));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(CodeGenContext));
    ctx->output = output;
    ctx->current_captures = NULL;
    /* Initialize hash maps for O(1) lookups */
    ctx->symbols.map = strmap_new();
    ctx->defined_functions.map = strmap_new();
    ctx->resumption_symbols = strmap_new();  /* Phase 22: effect resumptions */
    ctx->typed_methods.generics_map = strmap_new();  /* Phase 19: multiple dispatch */
    return ctx;
}

CodeGenContext* omni_codegen_new_buffer(void) {
    CodeGenContext* ctx = malloc(sizeof(CodeGenContext));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(CodeGenContext));
    ctx->output_capacity = 4096;
    ctx->output_buffer = malloc(ctx->output_capacity);
    ctx->output_buffer[0] = '\0';
    ctx->current_captures = NULL;
    /* Initialize hash maps for O(1) lookups */
    ctx->symbols.map = strmap_new();
    ctx->defined_functions.map = strmap_new();
    ctx->resumption_symbols = strmap_new();  /* Phase 22: effect resumptions */
    ctx->typed_methods.generics_map = strmap_new();  /* Phase 19: multiple dispatch */
    return ctx;
}

void omni_codegen_free(CodeGenContext* ctx) {
    if (!ctx) return;

    for (size_t i = 0; i < ctx->symbols.count; i++) {
        free(ctx->symbols.names[i]);
        free(ctx->symbols.c_names[i]);
    }
    free(ctx->symbols.names);
    free(ctx->symbols.c_names);
    if (ctx->symbols.map) strmap_free(ctx->symbols.map);

    for (size_t i = 0; i < ctx->forward_decls.count; i++) {
        free(ctx->forward_decls.decls[i]);
    }
    free(ctx->forward_decls.decls);

    for (size_t i = 0; i < ctx->lambda_defs.count; i++) {
        free(ctx->lambda_defs.defs[i]);
    }
    free(ctx->lambda_defs.defs);

    /* Free function tracking for multiple dispatch */
    for (size_t i = 0; i < ctx->defined_functions.count; i++) {
        free(ctx->defined_functions.names[i]);
    }
    free(ctx->defined_functions.names);
    free(ctx->defined_functions.definition_count);
    if (ctx->defined_functions.map) strmap_free(ctx->defined_functions.map);

    /* Phase 19: Free typed methods for multiple dispatch */
    for (size_t i = 0; i < ctx->typed_methods.count; i++) {
        free(ctx->typed_methods.omni_names[i]);
        free(ctx->typed_methods.mangled_names[i]);
        if (ctx->typed_methods.param_types[i]) {
            for (int j = 0; j < ctx->typed_methods.param_counts[i]; j++) {
                free(ctx->typed_methods.param_types[i][j]);
            }
            free(ctx->typed_methods.param_types[i]);
        }
    }
    free(ctx->typed_methods.omni_names);
    free(ctx->typed_methods.mangled_names);
    free(ctx->typed_methods.param_types);
    free(ctx->typed_methods.param_counts);
    if (ctx->typed_methods.generics_map) strmap_free(ctx->typed_methods.generics_map);

    if (ctx->analysis) {
        omni_analysis_free(ctx->analysis);
    }

    /* Phase 27: Clean up specialization resources */
    if (ctx->spec_db) {
        spec_db_free(ctx->spec_db);
    }
    if (ctx->type_env) {
        type_env_free(ctx->type_env);
    }

    /* Issue 3 P2: Clean up non-lexical region end tracking */
    free(ctx->region_exited);

    free(ctx->output_buffer);
    free(ctx);
}

char* omni_codegen_get_output(CodeGenContext* ctx) {
    if (ctx->output_buffer) {
        return strdup(ctx->output_buffer);
    }
    return NULL;
}

void omni_codegen_set_runtime(CodeGenContext* ctx, const char* path) {
    ctx->runtime_path = path;
    ctx->use_runtime = (path != NULL);
}

/* ============== Output Helpers ============== */

static void buffer_append(CodeGenContext* ctx, const char* s) {
    size_t len = strlen(s);
    while (ctx->output_size + len + 1 > ctx->output_capacity) {
        ctx->output_capacity *= 2;
        ctx->output_buffer = realloc(ctx->output_buffer, ctx->output_capacity);
    }
    memcpy(ctx->output_buffer + ctx->output_size, s, len + 1);
    ctx->output_size += len;
}

void omni_codegen_emit_raw(CodeGenContext* ctx, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    /* Check if output would exceed static buffer - use dynamic allocation */
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    char stack_buf[4096];
    char* buf = stack_buf;
    char* heap_buf = NULL;

    if (needed >= (int)sizeof(stack_buf)) {
        /* Need dynamic allocation for large output */
        heap_buf = malloc(needed + 1);
        if (heap_buf) {
            buf = heap_buf;
            vsnprintf(buf, needed + 1, fmt, args);
        } else {
            /* Fallback to truncated output */
            vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
        }
    } else {
        vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
    }
    va_end(args);

    if (ctx->output) {
        fputs(buf, ctx->output);
    } else if (ctx->output_buffer) {
        buffer_append(ctx, buf);
    }

    free(heap_buf);
}

void omni_codegen_emit(CodeGenContext* ctx, const char* fmt, ...) {
    /* Emit indentation */
    for (int i = 0; i < ctx->indent_level; i++) {
        omni_codegen_emit_raw(ctx, "    ");
    }

    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (ctx->output) {
        fputs(buf, ctx->output);
    } else if (ctx->output_buffer) {
        buffer_append(ctx, buf);
    }
}

void omni_codegen_indent(CodeGenContext* ctx) {
    ctx->indent_level++;
}

void omni_codegen_dedent(CodeGenContext* ctx) {
    if (ctx->indent_level > 0) ctx->indent_level--;
}

/* ============== Name Mangling ============== */

// TESTED - test_codegen_mangle.c
char* omni_codegen_mangle(const char* name) {
    size_t len = strlen(name);
    char* result = malloc(len * 5 + 8);  /* Worst case expansion */
    char* p = result;

    *p++ = 'o';
    *p++ = '_';

    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (isalnum((unsigned char)c)) {
            *p++ = c;
        } else {
            switch (c) {
            case '+': *p++ = '_'; *p++ = 'a'; *p++ = 'd'; *p++ = 'd'; break;
            case '-': *p++ = '_'; *p++ = 's'; *p++ = 'u'; *p++ = 'b'; break;
            case '*': *p++ = '_'; *p++ = 'm'; *p++ = 'u'; *p++ = 'l'; break;
            case '/': *p++ = '_'; *p++ = 'd'; *p++ = 'i'; *p++ = 'v'; break;
            case '=': *p++ = '_'; *p++ = 'e'; *p++ = 'q'; break;
            case '<': *p++ = '_'; *p++ = 'l'; *p++ = 't'; break;
            case '>': *p++ = '_'; *p++ = 'g'; *p++ = 't'; break;
            case '?': *p++ = '_'; *p++ = 'p'; break;
            case '!': *p++ = '_'; *p++ = 'b'; break;
            case '.': *p++ = '_'; *p++ = 'd'; break;
            case '_': *p++ = '_'; *p++ = '_'; break;
            default: *p++ = '_'; break;
            }
        }
    }
    *p = '\0';
    return result;
}

char* omni_codegen_temp(CodeGenContext* ctx) {
    char* result = malloc(32);
    snprintf(result, 32, "_t%d", ctx->temp_counter++);
    return result;
}

char* omni_codegen_label(CodeGenContext* ctx) {
    char* result = malloc(32);
    snprintf(result, 32, "_L%d", ctx->label_counter++);
    return result;
}

/* ============== Forward Declarations ============== */

void omni_codegen_add_forward_decl(CodeGenContext* ctx, const char* decl) {
    if (ctx->forward_decls.count >= ctx->forward_decls.capacity) {
        ctx->forward_decls.capacity = ctx->forward_decls.capacity ? ctx->forward_decls.capacity * 2 : 16;
        ctx->forward_decls.decls = realloc(ctx->forward_decls.decls,
                                           ctx->forward_decls.capacity * sizeof(char*));
    }
    ctx->forward_decls.decls[ctx->forward_decls.count++] = strdup(decl);
}

void omni_codegen_add_lambda_def(CodeGenContext* ctx, const char* def) {
    if (ctx->lambda_defs.count >= ctx->lambda_defs.capacity) {
        ctx->lambda_defs.capacity = ctx->lambda_defs.capacity ? ctx->lambda_defs.capacity * 2 : 16;
        ctx->lambda_defs.defs = realloc(ctx->lambda_defs.defs,
                                        ctx->lambda_defs.capacity * sizeof(char*));
    }
    ctx->lambda_defs.defs[ctx->lambda_defs.count++] = strdup(def);
}

/* ============== Symbol Table ============== */

// REVIEWED:OPTIMIZED - O(1) hash lookup instead of O(n) linear scan
static const char* lookup_symbol(CodeGenContext* ctx, const char* name) {
    /* O(1) hash lookup */
    if (ctx->symbols.map) {
        return (const char*)strmap_get(ctx->symbols.map, name);
    }
    /* Fallback to linear scan if hash map not initialized */
    for (size_t i = 0; i < ctx->symbols.count; i++) {
        if (strcmp(ctx->symbols.names[i], name) == 0) {
            return ctx->symbols.c_names[i];
        }
    }
    return NULL;
}

static void register_symbol(CodeGenContext* ctx, const char* name, const char* c_name) {
    if (ctx->symbols.count >= ctx->symbols.capacity) {
        ctx->symbols.capacity = ctx->symbols.capacity ? ctx->symbols.capacity * 2 : 16;
        ctx->symbols.names = realloc(ctx->symbols.names, ctx->symbols.capacity * sizeof(char*));
        ctx->symbols.c_names = realloc(ctx->symbols.c_names, ctx->symbols.capacity * sizeof(char*));
    }
    char* c_name_copy = strdup(c_name);
    ctx->symbols.names[ctx->symbols.count] = strdup(name);
    ctx->symbols.c_names[ctx->symbols.count] = c_name_copy;
    ctx->symbols.count++;

    /* Add to hash map for O(1) future lookups */
    if (ctx->symbols.map) {
        strmap_put(ctx->symbols.map, name, c_name_copy);
    }
}

/* ============== Perceus Reuse & Lobster RC Elision Integration ==============
 *
 * This section integrates the static analysis results for:
 * 1. Perceus-style memory reuse (FBIP - Functional But In-Place)
 * 2. Lobster-style RC elision (skip redundant inc_ref/dec_ref)
 *
 * The analysis passes in csrc/analysis/ compute:
 * - ReuseCandidate: pairs of (free, alloc) where memory can be reused
 * - RCElisionInfo: variables where RC operations can be skipped
 *
 * This code wires those analysis results into the code generator.
 *
 * References:
 * - Perceus: Garbage Free Reference Counting with Reuse (PLDI 2021)
 * - Lobster: aardappel.github.io/lobster/memory_management.html
 */

/* Get reuse candidate for current allocation position */
static ReuseCandidate* codegen_get_reuse(CodeGenContext* ctx) {
    if (!ctx->analysis) return NULL;
    return omni_get_reuse_at(ctx->analysis, ctx->current_pos);
}

/* Check if we can elide inc_ref for a variable */
static bool codegen_can_elide_inc(CodeGenContext* ctx, const char* var_name) {
    if (!ctx->analysis) return false;
    return omni_can_elide_inc_ref(ctx->analysis, var_name);
}

/* Check if we can elide dec_ref for a variable */
static bool codegen_can_elide_dec(CodeGenContext* ctx, const char* var_name) {
    if (!ctx->analysis) return false;
    return omni_can_elide_dec_ref(ctx->analysis, var_name);
}

/* Get RC elision class for a variable (for debug/comments) */
static RCElisionClass codegen_get_elision_class(CodeGenContext* ctx, const char* var_name) {
    if (!ctx->analysis) return RC_REQUIRED;
    return omni_get_rc_elision(ctx->analysis, var_name);
}

/*
 * Emit allocation with Perceus reuse optimization.
 *
 * If analysis found a reuse candidate (a variable being freed that has
 * compatible size), use REUSE_OR_NEW_* macro instead of mk_*_region.
 *
 * This transforms:
 *   (let ((x (cons 1 2)))
 *     (let ((y (cons 3 4)))  ;; x is dead here
 *       y))
 *
 * From:
 *   Obj* x = mk_cell_region(r, mk_int(1), mk_int(2));
 *   Obj* y = mk_cell_region(r, mk_int(3), mk_int(4));  // New allocation
 *   dec_ref(x);  // Free x
 *
 * To:
 *   Obj* x = mk_cell_region(r, mk_int(1), mk_int(2));
 *   Obj* y = REUSE_OR_NEW_CELL(r, x, mk_int(3), mk_int(4));  // Reuse x's memory!
 *   // No dec_ref needed - memory reused
 */
static bool codegen_emit_reuse_int(CodeGenContext* ctx, int64_t value) {
    ReuseCandidate* reuse = codegen_get_reuse(ctx);
    if (reuse && reuse->can_reuse && !reuse->is_consumed) {
        /* Check size compatibility (Int = 24 bytes typically) */
        if (strcmp(reuse->type_name, "Int") == 0 ||
            strcmp(reuse->type_name, "Float") == 0) {
            char* c_name = omni_codegen_mangle(reuse->freed_var);
            omni_codegen_emit_raw(ctx, "REUSE_OR_NEW_INT(_local_region, %s, %ld)",
                                  c_name, (long)value);
            free(c_name);
            omni_consume_reuse(reuse);
            return true;
        }
    }
    return false;
}

static bool codegen_emit_reuse_float(CodeGenContext* ctx, double value) {
    ReuseCandidate* reuse = codegen_get_reuse(ctx);
    if (reuse && reuse->can_reuse && !reuse->is_consumed) {
        if (strcmp(reuse->type_name, "Int") == 0 ||
            strcmp(reuse->type_name, "Float") == 0) {
            char* c_name = omni_codegen_mangle(reuse->freed_var);
            omni_codegen_emit_raw(ctx, "REUSE_OR_NEW_FLOAT(_local_region, %s, %f)",
                                  c_name, value);
            free(c_name);
            omni_consume_reuse(reuse);
            return true;
        }
    }
    return false;
}

static bool codegen_emit_reuse_cell(CodeGenContext* ctx, const char* car_expr, const char* cdr_expr) {
    ReuseCandidate* reuse = codegen_get_reuse(ctx);
    if (reuse && reuse->can_reuse && !reuse->is_consumed) {
        if (strcmp(reuse->type_name, "Cell") == 0) {
            char* c_name = omni_codegen_mangle(reuse->freed_var);
            omni_codegen_emit_raw(ctx, "REUSE_OR_NEW_CELL(_local_region, %s, %s, %s)",
                                  c_name, car_expr, cdr_expr);
            free(c_name);
            omni_consume_reuse(reuse);
            return true;
        }
    }
    return false;
}

/*
 * Emit dec_ref with Lobster-style RC elision.
 *
 * If analysis proved this variable doesn't need RC tracking, emit
 * a comment instead of the actual dec_ref call.
 *
 * Elision conditions:
 * - RC_ELIDE_BOTH: Variable is unique or stack-allocated (skip both inc/dec)
 * - RC_ELIDE_DEC: Variable is arena/pool-allocated (bulk free, skip dec)
 * - RC_ELIDE_INC: Variable is borrowed (don't own it, skip inc)
 * - RC_REQUIRED: Must use RC (shared heap reference)
 */
static void codegen_emit_dec_ref(CodeGenContext* ctx, const char* var_name, const char* c_name) {
    if (codegen_can_elide_dec(ctx, var_name)) {
        RCElisionClass elision = codegen_get_elision_class(ctx, var_name);
        const char* reason = omni_rc_elision_name(elision);
        omni_codegen_emit(ctx, "/* RC_ELIDED: dec_ref(%s) - %s */\n", c_name, reason);
    } else {
        omni_codegen_emit(ctx, "if (%s) dec_ref(%s);\n", c_name, c_name);
    }
}

/*
 * Emit inc_ref with Lobster-style RC elision.
 */
static void codegen_emit_inc_ref(CodeGenContext* ctx, const char* var_name, const char* c_name) {
    if (codegen_can_elide_inc(ctx, var_name)) {
        RCElisionClass elision = codegen_get_elision_class(ctx, var_name);
        const char* reason = omni_rc_elision_name(elision);
        omni_codegen_emit(ctx, "/* RC_ELIDED: inc_ref(%s) - %s */\n", c_name, reason);
    } else {
        omni_codegen_emit(ctx, "if (%s) inc_ref(%s);\n", c_name, c_name);
    }
}

/* ============== Closure Capture Collection ============== */

/*
 * Issue 1 P2: Closure Capture Collection
 *
 * This section implements capture collection for proper closure semantics.
 * When a lambda references variables from outer scopes, those variables
 * become "captures" that must be explicitly passed to the closure.
 *
 * Without proper captures, closures that outlive their defining scope
 * would reference dangling memory (use-after-free).
 */

/* Structure to hold collected captures during AST traversal */
typedef struct CaptureList {
    char** names;       /* Lisp symbol names */
    char** c_names;     /* Mangled C names */
    size_t count;
    size_t capacity;
    StrMap* map;        /* Optimization: O(1) capture lookup */
} CaptureList;

static void capture_list_init(CaptureList* list) {
    list->names = NULL;
    list->c_names = NULL;
    list->count = 0;
    list->capacity = 0;
    list->map = strmap_new();  /* Initialize hash map */
}

static void capture_list_free(CaptureList* list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->names[i]);
        free(list->c_names[i]);
    }
    free(list->names);
    free(list->c_names);
    if (list->map) strmap_free(list->map);
}

// REVIEWED:OPTIMIZED - O(1) hash lookup instead of O(n) linear scan
static bool capture_list_contains(CaptureList* list, const char* name) {
    if (list->map) {
        return strmap_contains(list->map, name);
    }
    /* Fallback to linear scan if hash map not initialized */
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->names[i], name) == 0) return true;
    }
    return false;
}

static void capture_list_add(CaptureList* list, const char* name, const char* c_name) {
    if (capture_list_contains(list, name)) return;  /* Already captured */

    if (list->count >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : 8;
        list->names = realloc(list->names, list->capacity * sizeof(char*));
        list->c_names = realloc(list->c_names, list->capacity * sizeof(char*));
    }
    list->names[list->count] = strdup(name);
    list->c_names[list->count] = strdup(c_name);
    list->count++;

    /* Add to hash map for O(1) future lookups */
    if (list->map) {
        strmap_put(list->map, name, (void*)1);  /* Use as hash set */
    }
}

/* Check if a symbol name is a parameter of the lambda */
static bool is_lambda_param(OmniValue* params, const char* name) {
    if (!params || !name) return false;

    if (omni_is_cell(params)) {
        /* List-style: (lambda (x y) body) */
        OmniValue* p = params;
        while (!omni_is_nil(p) && omni_is_cell(p)) {
            OmniValue* param = omni_car(p);
            if (omni_is_sym(param) && strcmp(param->str_val, name) == 0) {
                return true;
            }
            p = omni_cdr(p);
        }
    } else if (omni_is_array(params)) {
        /* Slot syntax: (fn [x y] body) */
        size_t len = omni_array_len(params);
        for (size_t i = 0; i < len; i++) {
            OmniValue* param = omni_array_get(params, i);
            if (omni_is_type_lit(param)) continue;
            if (omni_is_sym(param) && strcmp(param->str_val, name) == 0) {
                return true;
            }
        }
    }
    return false;
}

/* Forward declaration for recursive collect */
static void collect_free_vars_expr(OmniValue* expr, OmniValue* params,
                                   OmniValue* excluded_params,
                                   CodeGenContext* outer_ctx, CaptureList* captures);

/* Collect free variables from a list of expressions.
 * excluded_params: chain of all lambda params in scope (linked via cons cells)
 */
static void collect_free_vars_list(OmniValue* list, OmniValue* params,
                                   OmniValue* excluded_params,
                                   CodeGenContext* outer_ctx, CaptureList* captures) {
    while (!omni_is_nil(list) && omni_is_cell(list)) {
        collect_free_vars_expr(omni_car(list), params, excluded_params, outer_ctx, captures);
        list = omni_cdr(list);
    }
}

/* Check if a name is in the excluded params chain */
static bool is_excluded_param(OmniValue* excluded_params, const char* name) {
    while (!omni_is_nil(excluded_params) && omni_is_cell(excluded_params)) {
        OmniValue* param_list = omni_car(excluded_params);
        if (is_lambda_param(param_list, name)) return true;
        excluded_params = omni_cdr(excluded_params);
    }
    return false;
}

/* Collect free variables from an expression
 *
 * A free variable is one that:
 * 1. Is a symbol reference
 * 2. Is NOT a parameter of this lambda OR any enclosing lambda (excluded_params)
 * 3. IS in the outer scope's symbol table (outer_ctx)
 *
 * This identifies variables that need to be captured.
 *
 * excluded_params: linked list of param lists from enclosing lambdas.
 * This prevents capturing variables that are params of intermediate lambdas.
 */
static void collect_free_vars_expr(OmniValue* expr, OmniValue* params,
                                   OmniValue* excluded_params,
                                   CodeGenContext* outer_ctx, CaptureList* captures) {
    if (!expr) return;

    switch (expr->tag) {
        case OMNI_SYM: {
            const char* name = expr->str_val;
            /* Skip if it's a parameter of current lambda */
            if (is_lambda_param(params, name)) return;
            /* Skip if it's a parameter of any enclosing lambda in the chain */
            if (is_excluded_param(excluded_params, name)) return;
            /* Skip if it's a primitive/built-in (not in symbol table) */
            const char* c_name = lookup_symbol(outer_ctx, name);
            if (!c_name) return;
            /* This is a free variable - add to captures */
            capture_list_add(captures, name, c_name);
            break;
        }

        case OMNI_CELL: {
            /* List - could be a function call or special form */
            OmniValue* first = omni_car(expr);

            /* Handle nested lambda forms - need transitive capture collection */
            if (omni_is_sym(first)) {
                const char* sym = first->str_val;
                if (strcmp(sym, "lambda") == 0 || strcmp(sym, "fn") == 0 ||
                    strcmp(sym, "λ") == 0) {
                    /* Issue 1 P2: Transitive captures.
                     * When a nested lambda references a variable from an outer scope,
                     * we need to capture it in this lambda too, to pass it along.
                     *
                     * Key insight: We extend outer_ctx with current lambda's params
                     * so nested lambdas can find them. BUT we also add ALL params
                     * (current + nested) to excluded_params so we don't capture
                     * params that belong to intermediate lambdas.
                     *
                     * Example: (fn [start] (fn [step] (fn [] (+ start step))))
                     * - outer collects for middle: finds `start` needed, but `start`
                     *   is outer's param → excluded → not captured by outer
                     * - middle collects for inner: finds `start`, `step` needed
                     *   - `step` is middle's param → excluded → not captured by middle
                     *   - `start` found in extended_ctx → captured by middle
                     */
                    OmniValue* nested_args = omni_cdr(expr);
                    if (!omni_is_nil(nested_args)) {
                        OmniValue* nested_params = omni_car(nested_args);
                        OmniValue* nested_body = omni_cdr(nested_args);

                        /* Create extended context with current lambda's params.
                         * This allows nested lambdas to find our params as capturable vars.
                         */
                        CodeGenContext* extended_ctx = omni_codegen_new_buffer();
                        /* Copy existing symbols from outer_ctx */
                        for (size_t i = 0; i < outer_ctx->symbols.count; i++) {
                            register_symbol(extended_ctx, outer_ctx->symbols.names[i],
                                           outer_ctx->symbols.c_names[i]);
                        }
                        /* Add current lambda's params to extended context */
                        if (omni_is_array(params)) {
                            size_t len = omni_array_len(params);
                            for (size_t i = 0; i < len; i++) {
                                OmniValue* param = omni_array_get(params, i);
                                if (omni_is_type_lit(param)) continue;
                                if (omni_is_sym(param)) {
                                    char* c_name = omni_codegen_mangle(param->str_val);
                                    register_symbol(extended_ctx, param->str_val, c_name);
                                    free(c_name);
                                }
                            }
                        } else if (omni_is_cell(params)) {
                            OmniValue* p = params;
                            while (!omni_is_nil(p) && omni_is_cell(p)) {
                                OmniValue* param = omni_car(p);
                                if (omni_is_sym(param)) {
                                    char* c_name = omni_codegen_mangle(param->str_val);
                                    register_symbol(extended_ctx, param->str_val, c_name);
                                    free(c_name);
                                }
                                p = omni_cdr(p);
                            }
                        }

                        /* Build extended excluded_params chain:
                         * (current_params . (nested_params . existing_excluded))
                         * This ensures we don't capture params from any lambda in chain.
                         */
                        OmniValue* new_excluded = omni_new_cell(nested_params,
                            omni_new_cell(params, excluded_params));

                        /* Recurse with extended context and exclusions */
                        collect_free_vars_list(nested_body, nested_params,
                                              new_excluded, extended_ctx, captures);

                        omni_codegen_free(extended_ctx);
                    }
                    return;
                }
                /* Handle let bindings specially - don't capture the bound vars */
                if (strcmp(sym, "let") == 0) {
                    OmniValue* args = omni_cdr(expr);
                    if (!omni_is_nil(args)) {
                        OmniValue* bindings = omni_car(args);
                        OmniValue* body = omni_cdr(args);
                        /* Process binding values (they can reference captures) */
                        if (omni_is_array(bindings)) {
                            size_t len = omni_array_len(bindings);
                            for (size_t i = 1; i < len; i += 2) {
                                collect_free_vars_expr(omni_array_get(bindings, i),
                                                       params, excluded_params,
                                                       outer_ctx, captures);
                            }
                        }
                        /* Process body */
                        collect_free_vars_list(body, params, excluded_params,
                                              outer_ctx, captures);
                    }
                    return;
                }
            }

            /* Recurse into all elements */
            collect_free_vars_list(expr, params, excluded_params, outer_ctx, captures);
            break;
        }

        case OMNI_ARRAY: {
            /* Array literal - recurse into elements */
            size_t len = omni_array_len(expr);
            for (size_t i = 0; i < len; i++) {
                collect_free_vars_expr(omni_array_get(expr, i), params, excluded_params,
                                      outer_ctx, captures);
            }
            break;
        }

        default:
            /* Literals (int, float, string, etc.) have no free variables */
            break;
    }
}

/*
 * collect_lambda_captures - Collect all captured variables from a lambda
 *
 * Returns a CaptureList containing all outer-scope variables referenced
 * in the lambda body that are not parameters.
 */
static CaptureList collect_lambda_captures(OmniValue* lambda_expr, CodeGenContext* outer_ctx) {
    CaptureList captures;
    capture_list_init(&captures);

    if (!lambda_expr) return captures;

    /* Get parameters and body */
    OmniValue* args = omni_cdr(lambda_expr);
    OmniValue* params = omni_car(args);
    OmniValue* body = omni_cdr(args);

    /* Collect free variables from body.
     * excluded_params starts as NULL - only the current lambda's params are filtered.
     */
    collect_free_vars_list(body, params, NULL, outer_ctx, &captures);

    return captures;
}

/* ============== Runtime Header ============== */

void omni_codegen_runtime_header(CodeGenContext* ctx) {
    omni_codegen_emit_raw(ctx, "/* Generated by OmniLisp Compiler */\n");
    omni_codegen_emit_raw(ctx, "/* ASAP Memory Management - Compile-Time Free Injection */\n\n");

    if (ctx->use_runtime && ctx->runtime_path) {
        /* Use angle brackets - the -I flag provides the include path */
        omni_codegen_emit_raw(ctx, "#include <omni.h>\n");
        omni_codegen_emit_raw(ctx, "#include <dlfcn.h>  /* Phase 0: FFI ccall support */\n\n");
        /* Compatibility macros for runtime */
        /*
         * Runtime list representation:
         *   The external runtime uses NULL to represent the empty list / nil.
         *
         * IMPORTANT:
         *   Do NOT construct a "sentinel pair" for NIL (e.g., mk_pair(NULL, NULL)).
         *   That creates a real pair node and breaks list semantics (printing `()`
         *   becomes `(() . ())`, list_length becomes 1, etc.).
         */
        omni_codegen_emit_raw(ctx, "#define NIL NULL\n");
        omni_codegen_emit_raw(ctx, "#define NOTHING mk_nothing()\n");
        omni_codegen_emit_raw(ctx, "#define omni_print(o) prim_print(o)\n");
        omni_codegen_emit_raw(ctx, "#define car(o) obj_car(o)\n");
        /* Type objects - extern declarations for runtime type system */
        omni_codegen_emit_raw(ctx, "\n/* Runtime Type Objects */\n");
        omni_codegen_emit_raw(ctx, "extern Obj* o_Int;    /* Int type object */\n");
        omni_codegen_emit_raw(ctx, "extern Obj* o_String; /* String type object */\n");
        omni_codegen_emit_raw(ctx, "extern Obj* o_Any;    /* Any type object */\n");
        omni_codegen_emit_raw(ctx, "extern Obj* o_Nothing; /* Nothing type object */\n");
        omni_codegen_emit_raw(ctx, "\n");

        /* Effect system declarations (Phase 22: Algebraic Effects) */
        omni_codegen_emit_raw(ctx, "/* Effect System (Phase 22: Algebraic Effects) */\n");
        omni_codegen_emit_raw(ctx, "/* Forward declarations for effect types */\n");
        omni_codegen_emit_raw(ctx, "typedef struct EffectType EffectType;\n");
        omni_codegen_emit_raw(ctx, "typedef struct Handler Handler;\n");
        omni_codegen_emit_raw(ctx, "typedef struct HandlerClause HandlerClause;\n");
        omni_codegen_emit_raw(ctx, "typedef struct RecoveryProtocol RecoveryProtocol;\n");
        omni_codegen_emit_raw(ctx, "/* Effect primitives */\n");
        omni_codegen_emit_raw(ctx, "extern void effect_init(void);\n");
        omni_codegen_emit_raw(ctx, "extern Obj* prim_perform(Obj* effect_name, Obj* payload);\n");
        omni_codegen_emit_raw(ctx, "extern Obj* prim_raise(Obj* message);\n");
        omni_codegen_emit_raw(ctx, "extern Obj* prim_resume(Obj* resumption, Obj* value);\n");
        omni_codegen_emit_raw(ctx, "extern Obj* prim_yield(Obj* value);\n");
        omni_codegen_emit_raw(ctx, "extern Handler* handler_push(HandlerClause* clauses, Obj* return_clause, Obj* env);\n");
        omni_codegen_emit_raw(ctx, "extern void handler_pop(void);\n");
        omni_codegen_emit_raw(ctx, "extern EffectType* effect_type_find(const char* name);\n");
        omni_codegen_emit_raw(ctx, "extern EffectType* effect_type_register(const char* name, Obj* payload_type, RecoveryProtocol* recovery);\n");
        omni_codegen_emit_raw(ctx, "extern HandlerClause* handler_clause_add(HandlerClause* clauses, EffectType* type, Obj* handler_fn);\n");
        omni_codegen_emit_raw(ctx, "extern Obj* effect_handle(Obj* body, HandlerClause* clauses, Obj* return_clause, Obj* env);\n");
        omni_codegen_emit_raw(ctx, "\n");
        omni_codegen_emit_raw(ctx, "#define cdr(o) obj_cdr(o)\n");
        omni_codegen_emit_raw(ctx, "#define mk_cell(a, b) mk_pair(a, b)\n");
        omni_codegen_emit_raw(ctx, "#define prim_cons(a, b) mk_pair(a, b)\n\n");
    } else {
        /* Embedded minimal runtime */
        omni_codegen_emit_raw(ctx, "#include <stdio.h>\n");
        omni_codegen_emit_raw(ctx, "#include <stdlib.h>\n");
        omni_codegen_emit_raw(ctx, "#include <string.h>\n");
        omni_codegen_emit_raw(ctx, "#include <stdint.h>\n");
        omni_codegen_emit_raw(ctx, "#include <stdbool.h>\n");
        omni_codegen_emit_raw(ctx, "#include <pthread.h>\n");
        omni_codegen_emit_raw(ctx, "#include <dlfcn.h>  /* Phase 0: FFI ccall support */\n\n");

        /* Value type */
        omni_codegen_emit_raw(ctx, "typedef enum {\n");
        omni_codegen_emit_raw(ctx, "    T_INT, T_SYM, T_CELL, T_NIL, T_NOTHING, T_PRIM, T_LAMBDA, T_CODE, T_ERROR\n");
        omni_codegen_emit_raw(ctx, "} Tag;\n\n");

        omni_codegen_emit_raw(ctx, "struct Obj;\n");
        omni_codegen_emit_raw(ctx, "typedef struct Obj* (*PrimFn)(struct Obj*, struct Obj*);\n\n");

        omni_codegen_emit_raw(ctx, "typedef struct Obj {\n");
        omni_codegen_emit_raw(ctx, "    Tag tag;\n");
        omni_codegen_emit_raw(ctx, "    int rc;  /* Reference count */\n");
        omni_codegen_emit_raw(ctx, "    union {\n");
        omni_codegen_emit_raw(ctx, "        int64_t i;\n");
        omni_codegen_emit_raw(ctx, "        char* s;\n");
        omni_codegen_emit_raw(ctx, "        struct { struct Obj* car; struct Obj* cdr; } cell;\n");
        omni_codegen_emit_raw(ctx, "        PrimFn prim;\n");
        omni_codegen_emit_raw(ctx, "        struct { struct Obj* params; struct Obj* body; struct Obj* env; } lam;\n");
        omni_codegen_emit_raw(ctx, "    };\n");
        omni_codegen_emit_raw(ctx, "} Obj;\n\n");

        /* Nil singleton */
        omni_codegen_emit_raw(ctx, "static Obj _nil = { .tag = T_NIL, .rc = 1 };\n");
        omni_codegen_emit_raw(ctx, "#define NIL (&_nil)\n");
        omni_codegen_emit_raw(ctx, "static Obj _nothing = { .tag = T_NOTHING, .rc = 1 };\n");
        omni_codegen_emit_raw(ctx, "#define NOTHING (&_nothing)\n\n");

        /* ========== Region-RC Type Definitions ========== */
        omni_codegen_emit_raw(ctx, "/* Region-RC: Region-based memory management */\n");
        omni_codegen_emit_raw(ctx, "typedef struct Arena Arena;  /* Opaque arena type */\n");
        omni_codegen_emit_raw(ctx, "\n");
        omni_codegen_emit_raw(ctx, "typedef struct Region {\n");
        omni_codegen_emit_raw(ctx, "    Arena arena;            /* Physical storage (bump allocator) */\n");
        omni_codegen_emit_raw(ctx, "    int external_rc;        /* Strong refs from OTHER regions/stack (atomic) */\n");
        omni_codegen_emit_raw(ctx, "    int tether_count;       /* Temporary \"borrows\" by threads (atomic) */\n");
        omni_codegen_emit_raw(ctx, "    bool scope_alive;       /* True if the semantic scope is still active */\n");
        omni_codegen_emit_raw(ctx, "} Region;\n");
        omni_codegen_emit_raw(ctx, "\n");
        omni_codegen_emit_raw(ctx, "/* Region-RC forward declarations */\n");
        omni_codegen_emit_raw(ctx, "struct Region* region_create(void);\n");
        omni_codegen_emit_raw(ctx, "void region_exit(struct Region* r);\n");
        omni_codegen_emit_raw(ctx, "void region_destroy_if_dead(struct Region* r);\n");
        omni_codegen_emit_raw(ctx, "void* transmigrate(void* root, struct Region* src_region, struct Region* dest_region);\n");
        omni_codegen_emit_raw(ctx, "void* region_alloc(struct Region* r, size_t size);\n");
        omni_codegen_emit_raw(ctx, "void region_tether_start(struct Region* r);\n");
        omni_codegen_emit_raw(ctx, "void region_tether_end(struct Region* r);\n");
        omni_codegen_emit_raw(ctx, "\n");

        /* Region-RC: Region-aware constructors (allocate from region) */
        omni_codegen_emit_raw(ctx, "/* Region-aware constructors */\n");
        omni_codegen_emit_raw(ctx, "static Obj* mk_int_region(struct Region* r, int64_t i) {\n");
        omni_codegen_emit_raw(ctx, "    Obj* o = region_alloc(r, sizeof(Obj));\n");
        omni_codegen_emit_raw(ctx, "    o->tag = T_INT; o->rc = 1; o->i = i;\n");
        omni_codegen_emit_raw(ctx, "    return o;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static Obj* mk_sym_region(struct Region* r, const char* s) {\n");
        omni_codegen_emit_raw(ctx, "    Obj* o = region_alloc(r, sizeof(Obj));\n");
        omni_codegen_emit_raw(ctx, "    o->tag = T_SYM; o->rc = 1; o->s = strdup(s);\n");
        omni_codegen_emit_raw(ctx, "    return o;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static Obj* mk_bool_region(struct Region* r, int v) {\n");
        omni_codegen_emit_raw(ctx, "    return mk_sym_region(r, v ? \"true\" : \"false\");\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static Obj* mk_cell_region(struct Region* r, Obj* car, Obj* cdr) {\n");
        omni_codegen_emit_raw(ctx, "    Obj* o = region_alloc(r, sizeof(Obj));\n");
        omni_codegen_emit_raw(ctx, "    o->tag = T_CELL; o->rc = 1;\n");
        omni_codegen_emit_raw(ctx, "    o->cell.car = car; o->cell.cdr = cdr;\n");
        omni_codegen_emit_raw(ctx, "    return o;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static Obj* mk_float_region(struct Region* r, double f) {\n");
        omni_codegen_emit_raw(ctx, "    Obj* o = region_alloc(r, sizeof(Obj));\n");
        omni_codegen_emit_raw(ctx, "    o->tag = T_FLOAT; o->rc = 1; o->f = f;\n");
        omni_codegen_emit_raw(ctx, "    return o;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        /* Convenience wrappers for backward compatibility */
        omni_codegen_emit_raw(ctx, "/* Backward compatibility wrappers */\n");
        omni_codegen_emit_raw(ctx, "static Obj* mk_int(int64_t i) { return mk_int_region(_local_region, i); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* mk_sym(const char* s) { return mk_sym_region(_local_region, s); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* mk_string(const char* s) { return mk_string_cstr_region(_local_region, s); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* mk_cell(Obj* a, Obj* b) { return mk_cell_region(_local_region, a, b); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* mk_float(double f) { return mk_float_region(_local_region, f); }\n\n");

        /* Stack allocation macros (escape-aware allocation) */
        omni_codegen_emit_raw(ctx, "/* Stack-allocated objects - no free needed, auto-cleanup at scope exit */\n");
        omni_codegen_emit_raw(ctx, "#define STACK_INT(name, val) \\\n");
        omni_codegen_emit_raw(ctx, "    Obj _stack_##name = { .tag = T_INT, .rc = 1, .i = (val) }; \\\n");
        omni_codegen_emit_raw(ctx, "    Obj* name = &_stack_##name\n\n");

        omni_codegen_emit_raw(ctx, "#define STACK_CELL(name, car_val, cdr_val) \\\n");
        omni_codegen_emit_raw(ctx, "    Obj _stack_##name = { .tag = T_CELL, .rc = 1, .cell = { (car_val), (cdr_val) } }; \\\n");
        omni_codegen_emit_raw(ctx, "    Obj* name = &_stack_##name\n\n");

        /* Helper to check if an object is stack-allocated (for debug/safety) */
        omni_codegen_emit_raw(ctx, "#define IS_STACK_OBJ(o) ((o) && (o)->rc == -1)\n");
        omni_codegen_emit_raw(ctx, "#define MARK_STACK(o) ((o)->rc = -1)\n\n");

        /* Stack-friendly constructors that initialize existing memory */
        omni_codegen_emit_raw(ctx, "static void init_int(Obj* o, int64_t i) {\n");
        omni_codegen_emit_raw(ctx, "    o->tag = T_INT; o->rc = -1; o->i = i;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static void init_cell(Obj* o, Obj* car, Obj* cdr) {\n");
        omni_codegen_emit_raw(ctx, "    o->tag = T_CELL; o->rc = -1;\n");
        omni_codegen_emit_raw(ctx, "    o->cell.car = car; o->cell.cdr = cdr;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        /* Accessors */
        omni_codegen_emit_raw(ctx, "#define car(o) ((o)->cell.car)\n");
        omni_codegen_emit_raw(ctx, "#define cdr(o) ((o)->cell.cdr)\n");
        omni_codegen_emit_raw(ctx, "#define is_nil(o) ((o) == NIL || ((o) && (o)->tag == T_NIL))\n\n");

        /* Reference counting and ownership-aware free strategies */
        omni_codegen_emit_raw(ctx, "static void inc_ref(Obj* o) { if (o && o != NIL && o != NOTHING) o->rc++; }\n");
        omni_codegen_emit_raw(ctx, "static void dec_ref(Obj* o);\n");
        omni_codegen_emit_raw(ctx, "static void free_tree(Obj* o);\n\n");

        /* free_unique: Known single reference, no RC check needed */
        omni_codegen_emit_raw(ctx, "static void free_unique(Obj* o) {\n");
        omni_codegen_emit_raw(ctx, "    if (!o || o == NIL || o == NOTHING) return;\n");
        omni_codegen_emit_raw(ctx, "    switch (o->tag) {\n");
        omni_codegen_emit_raw(ctx, "    case T_SYM: free(o->s); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_CELL: free_unique(o->cell.car); free_unique(o->cell.cdr); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_LAMBDA: free_unique(o->lam.params); free_unique(o->lam.body); free_unique(o->lam.env); break;\n");
        omni_codegen_emit_raw(ctx, "    default: break;\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    free(o);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        /* free_tree: Tree-shaped, recursive free (still checks RC for shared children) */
        omni_codegen_emit_raw(ctx, "static void free_tree(Obj* o) {\n");
        omni_codegen_emit_raw(ctx, "    if (!o || o == NIL || o == NOTHING) return;\n");
        omni_codegen_emit_raw(ctx, "    if (o->rc > 1) { o->rc--; return; } /* Shared child - dec only */\n");
        omni_codegen_emit_raw(ctx, "    switch (o->tag) {\n");
        omni_codegen_emit_raw(ctx, "    case T_SYM: free(o->s); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_CELL: free_tree(o->cell.car); free_tree(o->cell.cdr); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_LAMBDA: free_tree(o->lam.params); free_tree(o->lam.body); free_tree(o->lam.env); break;\n");
        omni_codegen_emit_raw(ctx, "    default: break;\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    free(o);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        /* free_obj: Standard RC-based free (dec_ref alias) */
        omni_codegen_emit_raw(ctx, "static void free_obj(Obj* o) {\n");
        omni_codegen_emit_raw(ctx, "    if (!o || o == NIL || o == NOTHING) return;\n");
        omni_codegen_emit_raw(ctx, "    if (--o->rc > 0) return;\n");
        omni_codegen_emit_raw(ctx, "    switch (o->tag) {\n");
        omni_codegen_emit_raw(ctx, "    case T_SYM: free(o->s); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_CELL: free_obj(o->cell.car); free_obj(o->cell.cdr); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_LAMBDA: free_obj(o->lam.params); free_obj(o->lam.body); free_obj(o->lam.env); break;\n");
        omni_codegen_emit_raw(ctx, "    default: break;\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    free(o);\n");
        omni_codegen_emit_raw(ctx, "}\n");
        omni_codegen_emit_raw(ctx, "static void dec_ref(Obj* o) { free_obj(o); }\n\n");

        /* Weak references for back-edges (break cycles) */
        omni_codegen_emit_raw(ctx, "/* Weak reference: does NOT prevent deallocation.\n");
        omni_codegen_emit_raw(ctx, " * Used for back-edges (parent, prev, etc.) to break cycles.\n");
        omni_codegen_emit_raw(ctx, " * Weak refs are NOT followed during free (no recursive free).\n");
        omni_codegen_emit_raw(ctx, " * Weak refs are auto-nullified when target is freed.\n");
        omni_codegen_emit_raw(ctx, " */\n");
        omni_codegen_emit_raw(ctx, "typedef struct WeakRef {\n");
        omni_codegen_emit_raw(ctx, "    Obj** slot;           /* Pointer to the weak field in the owner */\n");
        omni_codegen_emit_raw(ctx, "    struct WeakRef* next; /* Next weak ref pointing to same target */\n");
        omni_codegen_emit_raw(ctx, "} WeakRef;\n\n");

        omni_codegen_emit_raw(ctx, "/* Weak ref list head stored in target object (or separate table) */\n");
        omni_codegen_emit_raw(ctx, "static WeakRef* _weak_refs = NULL; /* Global list for simplicity */\n\n");

        omni_codegen_emit_raw(ctx, "static void weak_ref_register(Obj** slot, Obj* target) {\n");
        omni_codegen_emit_raw(ctx, "    (void)target; /* For table-based lookup, would use target */\n");
        omni_codegen_emit_raw(ctx, "    WeakRef* wr = malloc(sizeof(WeakRef));\n");
        omni_codegen_emit_raw(ctx, "    wr->slot = slot;\n");
        omni_codegen_emit_raw(ctx, "    wr->next = _weak_refs;\n");
        omni_codegen_emit_raw(ctx, "    _weak_refs = wr;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static void weak_refs_nullify(Obj* target) {\n");
        omni_codegen_emit_raw(ctx, "    /* Called when target is about to be freed - nullify all weak refs */\n");
        omni_codegen_emit_raw(ctx, "    WeakRef** prev = &_weak_refs;\n");
        omni_codegen_emit_raw(ctx, "    WeakRef* wr = _weak_refs;\n");
        omni_codegen_emit_raw(ctx, "    while (wr) {\n");
        omni_codegen_emit_raw(ctx, "        if (*(wr->slot) == target) {\n");
        omni_codegen_emit_raw(ctx, "            *(wr->slot) = NULL; /* Nullify the weak reference */\n");
        omni_codegen_emit_raw(ctx, "            *prev = wr->next;\n");
        omni_codegen_emit_raw(ctx, "            WeakRef* to_free = wr;\n");
        omni_codegen_emit_raw(ctx, "            wr = wr->next;\n");
        omni_codegen_emit_raw(ctx, "            free(to_free);\n");
        omni_codegen_emit_raw(ctx, "        } else {\n");
        omni_codegen_emit_raw(ctx, "            prev = &wr->next;\n");
        omni_codegen_emit_raw(ctx, "            wr = wr->next;\n");
        omni_codegen_emit_raw(ctx, "        }\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Set a back-edge field (weak reference) */\n");
        omni_codegen_emit_raw(ctx, "#define SET_WEAK(owner, field, target) do { \\\n");
        omni_codegen_emit_raw(ctx, "    (owner)->field = (target); \\\n");
        omni_codegen_emit_raw(ctx, "    if (target) weak_ref_register(&(owner)->field, target); \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Get a back-edge field (may be NULL if target was freed) */\n");
        omni_codegen_emit_raw(ctx, "#define GET_WEAK(owner, field) ((owner)->field)\n\n");

        /* Perceus reuse functions - reuse freed memory for new allocations */
        omni_codegen_emit_raw(ctx, "/* Perceus Reuse: In-place mutation for functional-style updates.\n");
        omni_codegen_emit_raw(ctx, " * When we know an object will be freed immediately before a new allocation\n");
        omni_codegen_emit_raw(ctx, " * of the same size, we can reuse its memory instead of free+malloc.\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "/* Reuse an object's memory for an integer */\n");
        omni_codegen_emit_raw(ctx, "static Obj* reuse_as_int(struct Region* r, Obj* old, int64_t val) {\n");
        omni_codegen_emit_raw(ctx, "    if (!old || old == NIL || old == NOTHING) return mk_int_region(r, val);\n");
        omni_codegen_emit_raw(ctx, "    /* Clear old content if needed */\n");
        omni_codegen_emit_raw(ctx, "    if (old->tag == T_SYM && old->s) free(old->s);\n");
        omni_codegen_emit_raw(ctx, "    else if (old->tag == T_CELL) {\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.car);\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.cdr);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    old->tag = T_INT;\n");
        omni_codegen_emit_raw(ctx, "    old->i = val;\n");
        omni_codegen_emit_raw(ctx, "    old->rc = 1;\n");
        omni_codegen_emit_raw(ctx, "    return old;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Reuse an object's memory for a cell/cons */\n");
        omni_codegen_emit_raw(ctx, "static Obj* reuse_as_cell(struct Region* r, Obj* old, Obj* car, Obj* cdr) {\n");
        omni_codegen_emit_raw(ctx, "    if (!old || old == NIL || old == NOTHING) return mk_cell_region(r, car, cdr);\n");
        omni_codegen_emit_raw(ctx, "    /* Clear old content if needed */\n");
        omni_codegen_emit_raw(ctx, "    if (old->tag == T_SYM && old->s) free(old->s);\n");
        omni_codegen_emit_raw(ctx, "    else if (old->tag == T_CELL) {\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.car);\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.cdr);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    old->tag = T_CELL;\n");
        omni_codegen_emit_raw(ctx, "    old->cell.car = car; inc_ref(car);\n");
        omni_codegen_emit_raw(ctx, "    old->cell.cdr = cdr; inc_ref(cdr);\n");
        omni_codegen_emit_raw(ctx, "    old->rc = 1;\n");
        omni_codegen_emit_raw(ctx, "    return old;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Reuse an object's memory for a float */\n");
        omni_codegen_emit_raw(ctx, "static Obj* reuse_as_float(struct Region* r, Obj* old, double val) {\n");
        omni_codegen_emit_raw(ctx, "    if (!old || old == NIL || old == NOTHING) return mk_float_region(r, val);\n");
        omni_codegen_emit_raw(ctx, "    /* Clear old content if needed */\n");
        omni_codegen_emit_raw(ctx, "    if (old->tag == T_SYM && old->s) free(old->s);\n");
        omni_codegen_emit_raw(ctx, "    else if (old->tag == T_CELL) {\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.car);\n");
        omni_codegen_emit_raw(ctx, "        free_obj(old->cell.cdr);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    old->tag = T_FLOAT;\n");
        omni_codegen_emit_raw(ctx, "    old->f = val;\n");
        omni_codegen_emit_raw(ctx, "    old->rc = 1;\n");
        omni_codegen_emit_raw(ctx, "    return old;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Check if object can be reused (unique, about to be freed) */\n");
        omni_codegen_emit_raw(ctx, "#define CAN_REUSE(o) ((o) && (o) != NIL && (o) != NOTHING && (o)->rc == 1)\n\n");

        omni_codegen_emit_raw(ctx, "/* Conditional reuse macro - falls back to fresh alloc if can't reuse */\n");
        omni_codegen_emit_raw(ctx, "#define REUSE_OR_NEW_INT(r, old, val) \\\n");
        omni_codegen_emit_raw(ctx, "    (CAN_REUSE(old) ? reuse_as_int((r), (old), (val)) : mk_int_region((r), (val)))\n\n");

        omni_codegen_emit_raw(ctx, "#define REUSE_OR_NEW_CELL(r, old, car, cdr) \\\n");
        omni_codegen_emit_raw(ctx, "    (CAN_REUSE(old) ? reuse_as_cell((r), (old), (car), (cdr)) : mk_cell_region((r), (car), (cdr)))\n\n");

        omni_codegen_emit_raw(ctx, "#define REUSE_OR_NEW_FLOAT(r, old, val) \\\n");
        omni_codegen_emit_raw(ctx, "    (CAN_REUSE(old) ? reuse_as_float((r), (old), (val)) : mk_float_region((r), (val)))\n\n");

        /* RC Elision: Skip reference counting for objects with known lifetimes */
        omni_codegen_emit_raw(ctx, "/* RC Elision: Conditional inc/dec based on analysis.\n");
        omni_codegen_emit_raw(ctx, " * When analysis proves RC operations are unnecessary, we skip them:\n");
        omni_codegen_emit_raw(ctx, " * - Unique references: no other refs exist\n");
        omni_codegen_emit_raw(ctx, " * - Stack-allocated: lifetime is scope-bound\n");
        omni_codegen_emit_raw(ctx, " * - Arena/pool: bulk free, no individual tracking\n");
        omni_codegen_emit_raw(ctx, " * - Same region: all refs die together\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "/* Conditional inc_ref - may be elided */\n");
        omni_codegen_emit_raw(ctx, "#define INC_REF_IF_NEEDED(o, can_elide) \\\n");
        omni_codegen_emit_raw(ctx, "    do { if (!(can_elide)) inc_ref(o); } while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Conditional dec_ref - may be elided */\n");
        omni_codegen_emit_raw(ctx, "#define DEC_REF_IF_NEEDED(o, can_elide) \\\n");
        omni_codegen_emit_raw(ctx, "    do { if (!(can_elide)) dec_ref(o); } while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* No-op for elided RC operations (for clarity in generated code) */\n");
        omni_codegen_emit_raw(ctx, "#define RC_ELIDED() ((void)0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Region-local reference: no RC needed within same region */\n");
        omni_codegen_emit_raw(ctx, "#define REGION_LOCAL_REF(o) (o)  /* No inc_ref needed */\n\n");

        /* Per-Region External Refcount */
        omni_codegen_emit_raw(ctx, "/* Per-Region External Refcount: Track references into a region.\n");
        omni_codegen_emit_raw(ctx, " * Instead of per-object RC, track external refs to the region.\n");
        omni_codegen_emit_raw(ctx, " * When external_refcount == 0 and scope ends, bulk free entire region.\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "typedef struct Region {\n");
        omni_codegen_emit_raw(ctx, "    int id;\n");
        omni_codegen_emit_raw(ctx, "    int external_refcount;  /* Refs from outside this region */\n");
        omni_codegen_emit_raw(ctx, "    void* arena;            /* Arena allocator for this region */\n");
        omni_codegen_emit_raw(ctx, "    struct Region* parent;  /* Enclosing region */\n");
        omni_codegen_emit_raw(ctx, "} Region;\n\n");

        omni_codegen_emit_raw(ctx, "static struct Region* _current_region = NULL;\n\n");

        omni_codegen_emit_raw(ctx, "static struct Region* region_new(int id) {\n");
        omni_codegen_emit_raw(ctx, "    struct Region* r = malloc(sizeof(Region));\n");
        omni_codegen_emit_raw(ctx, "    r->id = id;\n");
        omni_codegen_emit_raw(ctx, "    r->external_refcount = 0;\n");
        omni_codegen_emit_raw(ctx, "    r->arena = NULL;  /* Could use arena allocator */\n");
        omni_codegen_emit_raw(ctx, "    r->parent = _current_region;\n");
        omni_codegen_emit_raw(ctx, "    _current_region = r;\n");
        omni_codegen_emit_raw(ctx, "    return r;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static void region_end(struct Region* r) {\n");
        omni_codegen_emit_raw(ctx, "    if (!r) return;\n");
        omni_codegen_emit_raw(ctx, "    _current_region = r->parent;\n");
        omni_codegen_emit_raw(ctx, "    /* If no external refs, could bulk-free arena here */\n");
        omni_codegen_emit_raw(ctx, "    if (r->external_refcount == 0) {\n");
        omni_codegen_emit_raw(ctx, "        /* Safe to bulk free all objects in region */\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    free(r);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "#define REGION_INC_EXTERNAL(r) do { if (r) (r)->external_refcount++; } while(0)\n");
        omni_codegen_emit_raw(ctx, "#define REGION_DEC_EXTERNAL(r) do { if (r) (r)->external_refcount--; } while(0)\n");
        omni_codegen_emit_raw(ctx, "#define REGION_CAN_BULK_FREE(r) ((r) && (r)->external_refcount == 0)\n\n");

        /* Borrow/Tether: Keep objects alive during loop iteration */
        omni_codegen_emit_raw(ctx, "/* Borrow/Tether: Keep borrowed objects alive.\n");
        omni_codegen_emit_raw(ctx, " * When iterating over a collection, the collection must stay alive.\n");
        omni_codegen_emit_raw(ctx, " * Tethering increments RC at loop entry, decrements at loop exit.\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "/* Tether an object to keep it alive during a borrow */\n");
        omni_codegen_emit_raw(ctx, "#define TETHER(o) do { if (o) inc_ref(o); } while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Release a tether when borrow ends */\n");
        omni_codegen_emit_raw(ctx, "#define UNTETHER(o) do { if (o) dec_ref(o); } while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Borrow a collection for loop iteration */\n");
        omni_codegen_emit_raw(ctx, "#define BORROW_FOR_LOOP(coll) TETHER(coll)\n\n");

        omni_codegen_emit_raw(ctx, "/* End loop borrow */\n");
        omni_codegen_emit_raw(ctx, "#define END_LOOP_BORROW(coll) UNTETHER(coll)\n\n");

        omni_codegen_emit_raw(ctx, "/* Scoped tether - automatically releases at scope end */\n");
        omni_codegen_emit_raw(ctx, "#define SCOPED_TETHER_DECL(name, o) \\\n");
        omni_codegen_emit_raw(ctx, "    Obj* name##_tethered = (o); \\\n");
        omni_codegen_emit_raw(ctx, "    TETHER(name##_tethered)\n\n");

        omni_codegen_emit_raw(ctx, "#define SCOPED_TETHER_END(name) \\\n");
        omni_codegen_emit_raw(ctx, "    UNTETHER(name##_tethered)\n\n");

        /* Interprocedural Ownership Annotations */
        omni_codegen_emit_raw(ctx, "/* Interprocedural Summaries: Ownership annotations for function boundaries.\n");
        omni_codegen_emit_raw(ctx, " * These annotations guide the compiler/reader about ownership transfer.\n");
        omni_codegen_emit_raw(ctx, " * PARAM_BORROWED: Caller keeps ownership, callee borrows.\n");
        omni_codegen_emit_raw(ctx, " * PARAM_CONSUMED: Callee takes ownership, will free.\n");
        omni_codegen_emit_raw(ctx, " * PARAM_PASSTHROUGH: Param passes through to return value.\n");
        omni_codegen_emit_raw(ctx, " * PARAM_CAPTURED: Param is captured in closure/data structure.\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "/* Parameter ownership annotations (for documentation) */\n");
        omni_codegen_emit_raw(ctx, "#define PARAM_BORROWED(p) (p)      /* Borrowed: caller keeps ownership */\n");
        omni_codegen_emit_raw(ctx, "#define PARAM_CONSUMED(p) (p)      /* Consumed: callee takes ownership */\n");
        omni_codegen_emit_raw(ctx, "#define PARAM_PASSTHROUGH(p) (p)   /* Passthrough: returned to caller */\n");
        omni_codegen_emit_raw(ctx, "#define PARAM_CAPTURED(p) (p)      /* Captured: stored in closure/struct */\n\n");

        omni_codegen_emit_raw(ctx, "/* Return ownership annotations */\n");
        omni_codegen_emit_raw(ctx, "#define RETURN_FRESH(v) (v)        /* Fresh allocation, caller must free */\n");
        omni_codegen_emit_raw(ctx, "#define RETURN_PASSTHROUGH(v) (v)  /* Returns a parameter, no new alloc */\n");
        omni_codegen_emit_raw(ctx, "#define RETURN_BORROWED(v) (v)     /* Borrowed ref, don't free */\n");
        omni_codegen_emit_raw(ctx, "#define RETURN_NONE() NOTHING      /* Returns nothing */\n\n");

        omni_codegen_emit_raw(ctx, "/* Caller-side ownership handling */\n");
        omni_codegen_emit_raw(ctx, "#define CALL_CONSUMED(arg, call_expr) \\\n");
        omni_codegen_emit_raw(ctx, "    ({ Obj* _result = (call_expr); /* arg ownership transferred */ _result; })\n\n");

        omni_codegen_emit_raw(ctx, "#define CALL_BORROWED(arg, call_expr) \\\n");
        omni_codegen_emit_raw(ctx, "    ({ Obj* _result = (call_expr); /* caller still owns arg */ _result; })\n\n");

        omni_codegen_emit_raw(ctx, "/* Function summary declaration macro */\n");
        omni_codegen_emit_raw(ctx, "#define FUNC_SUMMARY(name, ret_own, allocs, side_effects) \\\n");
        omni_codegen_emit_raw(ctx, "    /* Summary: name returns ret_own, allocates: allocs, side_effects: side_effects */\n\n");

        omni_codegen_emit_raw(ctx, "/* Ownership transfer assertion (debug builds) */\n");
        omni_codegen_emit_raw(ctx, "#ifndef NDEBUG\n");
        omni_codegen_emit_raw(ctx, "#define ASSERT_OWNED(o) do { \\\n");
        omni_codegen_emit_raw(ctx, "    if ((o) && (o) != NIL && (o) != NOTHING && (o)->rc < 1) { \\\n");
        omni_codegen_emit_raw(ctx, "        fprintf(stderr, \"Ownership error: %%p has rc=%%d\\n\", (void*)(o), (o)->rc); \\\n");
        omni_codegen_emit_raw(ctx, "    } \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n");
        omni_codegen_emit_raw(ctx, "#else\n");
        omni_codegen_emit_raw(ctx, "#define ASSERT_OWNED(o) ((void)0)\n");
        omni_codegen_emit_raw(ctx, "#endif\n\n");

        /* Concurrency Ownership Inference */
        omni_codegen_emit_raw(ctx, "/* Concurrency Ownership: Thread-safe reference counting.\n");
        omni_codegen_emit_raw(ctx, " * THREAD_LOCAL: Data stays in one thread, no sync needed.\n");
        omni_codegen_emit_raw(ctx, " * THREAD_SHARED: Data accessed by multiple threads, needs atomic RC.\n");
        omni_codegen_emit_raw(ctx, " * THREAD_TRANSFER: Data transferred between fibers, ownership moves.\n");
        omni_codegen_emit_raw(ctx, " * DIRECTIVE: NO CHANNELS - Use algebraic effects for concurrency.\n");
        omni_codegen_emit_raw(ctx, " */\n\n");

        omni_codegen_emit_raw(ctx, "/* Atomic reference counting for shared data */\n");
        omni_codegen_emit_raw(ctx, "#ifdef __STDC_NO_ATOMICS__\n");
        omni_codegen_emit_raw(ctx, "/* Fallback for systems without C11 atomics - use mutex */\n");
        omni_codegen_emit_raw(ctx, "static pthread_mutex_t _rc_mutex = PTHREAD_MUTEX_INITIALIZER;\n");
        omni_codegen_emit_raw(ctx, "#define ATOMIC_INC_REF(o) do { \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_lock(&_rc_mutex); \\\n");
        omni_codegen_emit_raw(ctx, "    if ((o) && (o) != NIL && (o) != NOTHING) (o)->rc++; \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_unlock(&_rc_mutex); \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n\n");
        omni_codegen_emit_raw(ctx, "#define ATOMIC_DEC_REF(o) do { \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_lock(&_rc_mutex); \\\n");
        omni_codegen_emit_raw(ctx, "    if ((o) && (o) != NIL && (o) != NOTHING) { \\\n");
        omni_codegen_emit_raw(ctx, "        if (--(o)->rc <= 0) { \\\n");
        omni_codegen_emit_raw(ctx, "            pthread_mutex_unlock(&_rc_mutex); \\\n");
        omni_codegen_emit_raw(ctx, "            free_obj(o); \\\n");
        omni_codegen_emit_raw(ctx, "        } else { \\\n");
        omni_codegen_emit_raw(ctx, "            pthread_mutex_unlock(&_rc_mutex); \\\n");
        omni_codegen_emit_raw(ctx, "        } \\\n");
        omni_codegen_emit_raw(ctx, "    } else { pthread_mutex_unlock(&_rc_mutex); } \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n");
        omni_codegen_emit_raw(ctx, "#else\n");
        omni_codegen_emit_raw(ctx, "/* Using __atomic builtins for GCC/Clang compatibility */\n");
        omni_codegen_emit_raw(ctx, "#define ATOMIC_INC_REF(o) do { \\\n");
        omni_codegen_emit_raw(ctx, "    if ((o) && (o) != NIL && (o) != NOTHING) __atomic_add_fetch(&(o)->rc, 1, __ATOMIC_SEQ_CST); \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n\n");
        omni_codegen_emit_raw(ctx, "#define ATOMIC_DEC_REF(o) do { \\\n");
        omni_codegen_emit_raw(ctx, "    if ((o) && (o) != NIL && (o) != NOTHING) { \\\n");
        omni_codegen_emit_raw(ctx, "        if (__atomic_sub_fetch(&(o)->rc, 1, __ATOMIC_SEQ_CST) <= 0) { \\\n");
        omni_codegen_emit_raw(ctx, "            free_obj(o); \\\n");
        omni_codegen_emit_raw(ctx, "        } \\\n");
        omni_codegen_emit_raw(ctx, "    } \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n");
        omni_codegen_emit_raw(ctx, "#endif\n\n");

        omni_codegen_emit_raw(ctx, "/* Thread locality annotations */\n");
        omni_codegen_emit_raw(ctx, "#define THREAD_LOCAL_VAR(v) (v)      /* No sync needed */\n");
        omni_codegen_emit_raw(ctx, "#define THREAD_SHARED_VAR(v) (v)     /* Uses atomic RC */\n");
        omni_codegen_emit_raw(ctx, "#define THREAD_TRANSFER_VAR(v) (v)   /* Ownership moves */\n\n");

        /* DIRECTIVE: NO CHANNELS - Channel struct, channel_new, channel_send,
         * channel_recv, channel_close, channel_free, and ownership macros removed.
         * Use algebraic effects for structured concurrency instead. */

        omni_codegen_emit_raw(ctx, "/* Thread spawn with captured variable handling */\n");
        omni_codegen_emit_raw(ctx, "#define SPAWN_THREAD(fn, arg) do { \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_t _thread; \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_create(&_thread, NULL, fn, arg); \\\n");
        omni_codegen_emit_raw(ctx, "    pthread_detach(_thread); \\\n");
        omni_codegen_emit_raw(ctx, "} while(0)\n\n");

        omni_codegen_emit_raw(ctx, "/* Mark variable as shared (needs atomic RC) */\n");
        omni_codegen_emit_raw(ctx, "#define MARK_SHARED(v) ((void)0)  /* Analysis marker, no runtime cost */\n\n");

        omni_codegen_emit_raw(ctx, "/* Conditional RC based on thread locality analysis */\n");
        omni_codegen_emit_raw(ctx, "#define INC_REF_FOR_THREAD(o, needs_atomic) \\\n");
        omni_codegen_emit_raw(ctx, "    do { if (needs_atomic) ATOMIC_INC_REF(o); else inc_ref(o); } while(0)\n\n");

        omni_codegen_emit_raw(ctx, "#define DEC_REF_FOR_THREAD(o, needs_atomic) \\\n");
        omni_codegen_emit_raw(ctx, "    do { if (needs_atomic) ATOMIC_DEC_REF(o); else dec_ref(o); } while(0)\n\n");

        /* Print */
        omni_codegen_emit_raw(ctx, "static void print_obj(Obj* o) {\n");
        omni_codegen_emit_raw(ctx, "    if (!o || o == NOTHING) { printf(\"nothing\"); return; }\n");
        omni_codegen_emit_raw(ctx, "    if (is_nil(o)) { printf(\"()\"); return; }\n");
        omni_codegen_emit_raw(ctx, "    switch (o->tag) {\n");
        omni_codegen_emit_raw(ctx, "    case T_INT: printf(\"%%ld\", (long)o->i); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_SYM: printf(\"%%s\", o->s); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_NOTHING: printf(\"nothing\"); break;\n");
        omni_codegen_emit_raw(ctx, "    case T_CELL:\n");
        omni_codegen_emit_raw(ctx, "        printf(\"(\");\n");
        omni_codegen_emit_raw(ctx, "        while (!is_nil(o)) {\n");
        omni_codegen_emit_raw(ctx, "            print_obj(car(o));\n");
        omni_codegen_emit_raw(ctx, "            o = cdr(o);\n");
        omni_codegen_emit_raw(ctx, "            if (!is_nil(o)) printf(\" \");\n");
        omni_codegen_emit_raw(ctx, "        }\n");
        omni_codegen_emit_raw(ctx, "        printf(\")\");\n");
        omni_codegen_emit_raw(ctx, "        break;\n");
        omni_codegen_emit_raw(ctx, "    default: printf(\"#<unknown>\"); break;\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n");
        omni_codegen_emit_raw(ctx, "#define omni_print(o) print_obj(o)\n\n");

        /* Primitives - Region-RC: All allocations go through _local_region */
        omni_codegen_emit_raw(ctx, "/* Primitive operations - allocate results in _local_region */\n");
        omni_codegen_emit_raw(ctx, "/* Helper: Check if value is a float */\n");
        omni_codegen_emit_raw(ctx, "static int is_float_obj(Obj* o) { return o && IS_BOXED(o) && o->tag == TAG_FLOAT; }\n");
        omni_codegen_emit_raw(ctx, "/* Helper: Extract float value */\n");
        omni_codegen_emit_raw(ctx, "static double obj_to_float_val(Obj* o) { return is_float_obj(o) ? o->f : (double)o->i; }\n\n");

        omni_codegen_emit_raw(ctx, "static Obj* prim_add(Obj* a, Obj* b) {\n");
        omni_codegen_emit_raw(ctx, "    if (is_float_obj(a) || is_float_obj(b)) {\n");
        omni_codegen_emit_raw(ctx, "        return mk_float_region(_local_region, obj_to_float_val(a) + obj_to_float_val(b));\n");
        omni_codegen_emit_raw(ctx, "    } else {\n");
        omni_codegen_emit_raw(ctx, "        return mk_int_region(_local_region, a->i + b->i);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n");

        omni_codegen_emit_raw(ctx, "static Obj* prim_sub(Obj* a, Obj* b) {\n");
        omni_codegen_emit_raw(ctx, "    if (is_float_obj(a) || is_float_obj(b)) {\n");
        omni_codegen_emit_raw(ctx, "        return mk_float_region(_local_region, obj_to_float_val(a) - obj_to_float_val(b));\n");
        omni_codegen_emit_raw(ctx, "    } else {\n");
        omni_codegen_emit_raw(ctx, "        return mk_int_region(_local_region, a->i - b->i);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n");

        omni_codegen_emit_raw(ctx, "static Obj* prim_mul(Obj* a, Obj* b) {\n");
        omni_codegen_emit_raw(ctx, "    if (is_float_obj(a) || is_float_obj(b)) {\n");
        omni_codegen_emit_raw(ctx, "        return mk_float_region(_local_region, obj_to_float_val(a) * obj_to_float_val(b));\n");
        omni_codegen_emit_raw(ctx, "    } else {\n");
        omni_codegen_emit_raw(ctx, "        return mk_int_region(_local_region, a->i * b->i);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n");

        omni_codegen_emit_raw(ctx, "static Obj* prim_div(Obj* a, Obj* b) {\n");
        omni_codegen_emit_raw(ctx, "    if (is_float_obj(a) || is_float_obj(b)) {\n");
        omni_codegen_emit_raw(ctx, "        return mk_float_region(_local_region, obj_to_float_val(a) / obj_to_float_val(b));\n");
        omni_codegen_emit_raw(ctx, "    } else {\n");
        omni_codegen_emit_raw(ctx, "        return mk_int_region(_local_region, a->i / b->i);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n");

        omni_codegen_emit_raw(ctx, "static Obj* prim_mod(Obj* a, Obj* b) { return mk_int_region(_local_region, a->i %% b->i); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_lt(Obj* a, Obj* b) {\n");
        omni_codegen_emit_raw(ctx, "    if (is_float_obj(a) || is_float_obj(b)) {\n");
        omni_codegen_emit_raw(ctx, "        return mk_bool_region(_local_region, obj_to_float_val(a) < obj_to_float_val(b) ? 1 : 0);\n");
        omni_codegen_emit_raw(ctx, "    } else {\n");
        omni_codegen_emit_raw(ctx, "        return mk_bool_region(_local_region, a->i < b->i ? 1 : 0);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_gt(Obj* a, Obj* b) {\n");
        omni_codegen_emit_raw(ctx, "    if (is_float_obj(a) || is_float_obj(b)) {\n");
        omni_codegen_emit_raw(ctx, "        return mk_bool_region(_local_region, obj_to_float_val(a) > obj_to_float_val(b) ? 1 : 0);\n");
        omni_codegen_emit_raw(ctx, "    } else {\n");
        omni_codegen_emit_raw(ctx, "        return mk_bool_region(_local_region, a->i > b->i ? 1 : 0);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_le(Obj* a, Obj* b) {\n");
        omni_codegen_emit_raw(ctx, "    if (is_float_obj(a) || is_float_obj(b)) {\n");
        omni_codegen_emit_raw(ctx, "        return mk_bool_region(_local_region, obj_to_float_val(a) <= obj_to_float_val(b) ? 1 : 0);\n");
        omni_codegen_emit_raw(ctx, "    } else {\n");
        omni_codegen_emit_raw(ctx, "        return mk_bool_region(_local_region, a->i <= b->i ? 1 : 0);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_ge(Obj* a, Obj* b) {\n");
        omni_codegen_emit_raw(ctx, "    if (is_float_obj(a) || is_float_obj(b)) {\n");
        omni_codegen_emit_raw(ctx, "        return mk_bool_region(_local_region, obj_to_float_val(a) >= obj_to_float_val(b) ? 1 : 0);\n");
        omni_codegen_emit_raw(ctx, "    } else {\n");
        omni_codegen_emit_raw(ctx, "        return mk_bool_region(_local_region, a->i >= b->i ? 1 : 0);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_eq(Obj* a, Obj* b) {\n");
        omni_codegen_emit_raw(ctx, "    if (is_float_obj(a) || is_float_obj(b)) {\n");
        omni_codegen_emit_raw(ctx, "        return mk_bool_region(_local_region, obj_to_float_val(a) == obj_to_float_val(b) ? 1 : 0);\n");
        omni_codegen_emit_raw(ctx, "    } else {\n");
        omni_codegen_emit_raw(ctx, "        return mk_bool_region(_local_region, a->i == b->i ? 1 : 0);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "}\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_cons(Obj* a, Obj* b) { inc_ref(a); inc_ref(b); return mk_cell_region(_local_region, a, b); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_car(Obj* lst) { return is_nil(lst) ? NIL : car(lst); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_cdr(Obj* lst) { return is_nil(lst) ? NIL : cdr(lst); }\n");
        omni_codegen_emit_raw(ctx, "static Obj* prim_null(Obj* o) { return mk_bool_region(_local_region, is_nil(o) ? 1 : 0); }\n");
        omni_codegen_emit_raw(ctx, "static int is_truthy(Obj* o) {\n");
        omni_codegen_emit_raw(ctx, "    if (!o) return 0;\n");
        omni_codegen_emit_raw(ctx, "    if (o == NOTHING) return 0;\n");
        omni_codegen_emit_raw(ctx, "    if (o->tag == T_SYM && o->s && strcmp(o->s, \"false\") == 0) return 0;\n");
        omni_codegen_emit_raw(ctx, "    return 1;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");
    }
}

/* ============== Expression Compilation ============== */

static void codegen_expr(CodeGenContext* ctx, OmniValue* expr);

static void codegen_int(CodeGenContext* ctx, OmniValue* expr) {
    ctx->current_pos++;  /* Track allocation position for reuse analysis */
    /* Perceus reuse: try to reuse memory from a recently freed variable */
    if (!codegen_emit_reuse_int(ctx, expr->int_val)) {
        omni_codegen_emit_raw(ctx, "mk_int(%ld)", (long)expr->int_val);
    }
}

static void codegen_float(CodeGenContext* ctx, OmniValue* expr) {
    ctx->current_pos++;  /* Track allocation position for reuse analysis */
    /* Perceus reuse: try to reuse memory from a recently freed variable */
    if (!codegen_emit_reuse_float(ctx, expr->float_val)) {
        omni_codegen_emit_raw(ctx, "mk_float_region(_local_region, %f)", expr->float_val);
    }
}

static void codegen_sym(CodeGenContext* ctx, OmniValue* expr) {
    const char* c_name = lookup_symbol(ctx, expr->str_val);
    if (c_name) {
        omni_codegen_emit_raw(ctx, "%s", c_name);
    } else {
        /* Check for primitives */
        const char* name = expr->str_val;

        /* Phase 5.3: Check for qualified symbol (module/symbol or module.symbol) */
        const char* sep = strchr(name, '/');
        if (!sep) sep = strchr(name, '.');
        if (sep && sep != name && *(sep + 1) != '\0') {
            /* Qualified symbol - resolve at runtime via module system */
            omni_codegen_emit_raw(ctx, "prim_resolve(mk_string(\"%s\"))", name);
            return;
        }

        if (strcmp(name, "true") == 0) {
            omni_codegen_emit_raw(ctx, "mk_bool(1)");
        } else if (strcmp(name, "false") == 0) {
            omni_codegen_emit_raw(ctx, "mk_bool(0)");
        } else if (strcmp(name, "+") == 0) omni_codegen_emit_raw(ctx, "prim_add");
        else if (strcmp(name, "-") == 0) omni_codegen_emit_raw(ctx, "prim_sub");
        else if (strcmp(name, "*") == 0) omni_codegen_emit_raw(ctx, "prim_mul");
        else if (strcmp(name, "/") == 0) omni_codegen_emit_raw(ctx, "prim_div");
        else if (strcmp(name, "%%") == 0) omni_codegen_emit_raw(ctx, "prim_mod");
        else if (strcmp(name, "<") == 0) omni_codegen_emit_raw(ctx, "prim_lt");
        else if (strcmp(name, ">") == 0) omni_codegen_emit_raw(ctx, "prim_gt");
        else if (strcmp(name, "<=") == 0) omni_codegen_emit_raw(ctx, "prim_le");
        else if (strcmp(name, ">=") == 0) omni_codegen_emit_raw(ctx, "prim_ge");
        else if (strcmp(name, "=") == 0) omni_codegen_emit_raw(ctx, "prim_eq");
        else if (strcmp(name, "kind=?") == 0) omni_codegen_emit_raw(ctx, "prim_kind_eq");
        else if (strcmp(name, "cons") == 0) omni_codegen_emit_raw(ctx, "prim_cons");
        else if (strcmp(name, "car") == 0) omni_codegen_emit_raw(ctx, "prim_car");
        else if (strcmp(name, "cdr") == 0) omni_codegen_emit_raw(ctx, "prim_cdr");
        else if (strcmp(name, "null?") == 0) omni_codegen_emit_raw(ctx, "prim_null");
        else if (strcmp(name, "type?") == 0) omni_codegen_emit_raw(ctx, "prim_type_is");
        else if (strcmp(name, "type-of") == 0) omni_codegen_emit_raw(ctx, "prim_type_of");
        else if (strcmp(name, "typeof") == 0) omni_codegen_emit_raw(ctx, "prim_value_to_type");
        else if (strcmp(name, "value->type") == 0) omni_codegen_emit_raw(ctx, "prim_value_to_type");
        else if (strcmp(name, "Int") == 0) omni_codegen_emit_raw(ctx, "o_Int");
        else if (strcmp(name, "String") == 0) omni_codegen_emit_raw(ctx, "o_String");
        else if (strcmp(name, "Any") == 0) omni_codegen_emit_raw(ctx, "o_Any");
        else if (strcmp(name, "Nothing") == 0) omni_codegen_emit_raw(ctx, "o_Nothing");
        /* String operations */
        else if (strcmp(name, "string-length") == 0) omni_codegen_emit_raw(ctx, "prim_string_length");
        else if (strcmp(name, "string-split") == 0) omni_codegen_emit_raw(ctx, "prim_string_split");
        else if (strcmp(name, "string-join") == 0) omni_codegen_emit_raw(ctx, "prim_string_join");
        else if (strcmp(name, "string-replace") == 0) omni_codegen_emit_raw(ctx, "prim_string_replace");
        else if (strcmp(name, "string-trim") == 0) omni_codegen_emit_raw(ctx, "prim_string_trim");
        else if (strcmp(name, "string-trim-left") == 0) omni_codegen_emit_raw(ctx, "prim_string_trim_left");
        else if (strcmp(name, "string-trim-right") == 0) omni_codegen_emit_raw(ctx, "prim_string_trim_right");
        else if (strcmp(name, "string-upcase") == 0) omni_codegen_emit_raw(ctx, "prim_string_upcase");
        else if (strcmp(name, "string-downcase") == 0) omni_codegen_emit_raw(ctx, "prim_string_lowcase");
        else if (strcmp(name, "string-capitalize") == 0) omni_codegen_emit_raw(ctx, "prim_string_capitalize");
        else if (strcmp(name, "string-concat") == 0) omni_codegen_emit_raw(ctx, "prim_string_concat");
        else if (strcmp(name, "string-substr") == 0) omni_codegen_emit_raw(ctx, "prim_string_substr");
        else if (strcmp(name, "string-contains?") == 0) omni_codegen_emit_raw(ctx, "prim_string_contains");
        else if (strcmp(name, "string-index-of") == 0) omni_codegen_emit_raw(ctx, "prim_string_index_of");
        else if (strcmp(name, "string=?") == 0) omni_codegen_emit_raw(ctx, "prim_string_equals");
        else if (strcmp(name, "string-compare") == 0) omni_codegen_emit_raw(ctx, "prim_string_compare");
        else if (strcmp(name, "string-starts-with") == 0) omni_codegen_emit_raw(ctx, "prim_string_starts_with");
        else if (strcmp(name, "string-ends-with") == 0) omni_codegen_emit_raw(ctx, "prim_string_ends_with");
        /* Regex operations (POSIX) */
        else if (strcmp(name, "re-match") == 0) omni_codegen_emit_raw(ctx, "prim_re_match");
        else if (strcmp(name, "re-find-all") == 0) omni_codegen_emit_raw(ctx, "prim_re_find_all");
        else if (strcmp(name, "re-split") == 0) omni_codegen_emit_raw(ctx, "prim_re_split");
        else if (strcmp(name, "re-replace") == 0) omni_codegen_emit_raw(ctx, "prim_re_replace");
        else if (strcmp(name, "re-fullmatch") == 0) omni_codegen_emit_raw(ctx, "prim_re_fullmatch");
        /* Documentation & Introspection */
        else if (strcmp(name, "doc") == 0) omni_codegen_emit_raw(ctx, "prim_doc");
        else if (strcmp(name, "source") == 0) omni_codegen_emit_raw(ctx, "prim_source");
        /* Module system */
        else if (strcmp(name, "module-ref") == 0) omni_codegen_emit_raw(ctx, "prim_module_ref");
        else if (strcmp(name, "module-exports") == 0) omni_codegen_emit_raw(ctx, "prim_module_exports");
        else if (strcmp(name, "module-list") == 0) omni_codegen_emit_raw(ctx, "prim_module_list");
        else if (strcmp(name, "module-get") == 0) omni_codegen_emit_raw(ctx, "prim_module_get");
        else if (strcmp(name, "module-begin") == 0) omni_codegen_emit_raw(ctx, "prim_module_begin");
        else if (strcmp(name, "module-end") == 0) omni_codegen_emit_raw(ctx, "prim_module_end");
        /* Predicates */
        else if (strcmp(name, "nothing?") == 0) omni_codegen_emit_raw(ctx, "prim_nothing_p");
        else if (strcmp(name, "not") == 0) omni_codegen_emit_raw(ctx, "prim_not");
        else {
            char* mangled = omni_codegen_mangle(name);
            omni_codegen_emit_raw(ctx, "%s", mangled);
            free(mangled);
        }
    }
}

/* Helper: escape a string for C string literal */
static char* escape_string_for_c(const char* s) {
    if (!s) return strdup("");

    /* First pass: calculate needed size */
    size_t len = 0;
    for (const char* p = s; *p; p++) {
        switch (*p) {
            case '\n': case '\r': case '\t': case '\\': case '"':
                len += 2;  /* \n, \r, \t, \\, \" */
                break;
            default:
                if ((unsigned char)*p < 32) {
                    len += 4;  /* \xNN */
                } else {
                    len += 1;
                }
                break;
        }
    }

    char* result = malloc(len + 1);
    if (!result) return strdup(s);

    char* out = result;
    for (const char* p = s; *p; p++) {
        switch (*p) {
            case '\n': *out++ = '\\'; *out++ = 'n'; break;
            case '\r': *out++ = '\\'; *out++ = 'r'; break;
            case '\t': *out++ = '\\'; *out++ = 't'; break;
            case '\\': *out++ = '\\'; *out++ = '\\'; break;
            case '"':  *out++ = '\\'; *out++ = '"'; break;
            default:
                if ((unsigned char)*p < 32) {
                    sprintf(out, "\\x%02x", (unsigned char)*p);
                    out += 4;
                } else {
                    *out++ = *p;
                }
                break;
        }
    }
    *out = '\0';
    return result;
}

static void codegen_string(CodeGenContext* ctx, OmniValue* expr) {
    /* Emit string literal using mk_string wrapper (proper TAG_STRING support) */
    /* T-wire-string-literal-01: mk_string wrapper uses mk_string_cstr_region(_local_region, ...) */
    char* escaped = escape_string_for_c(expr->str_val);
    omni_codegen_emit_raw(ctx, "mk_string(\"%s\")", escaped);
    free(escaped);
}

/* T-wire-fmt-string-01: Format string codegen */
/* Generate code for format string interpolation: (fmt-string "Hello $name") */
/* Supports $var, ${var} and ${expr} syntax */
static void codegen_fmt_string(CodeGenContext* ctx, OmniValue* expr) {
    /* Format string form: (fmt-string "template") */
    /* Template contains $var or ${expr} placeholders to interpolate */

    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* template = omni_car(args);
    if (!omni_is_string(template)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    const char* fmt = template->str_val;
    if (!fmt) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    /* Create a temporary buffer variable for the result */
    char* result_var = omni_codegen_temp(ctx);

    /* First, scan the format string to count interpolations */
    int interp_count = 0;
    size_t fmt_len = strlen(fmt);
    for (size_t i = 0; i < fmt_len; i++) {
        if (fmt[i] == '$') {
            interp_count++;
        }
    }

    if (interp_count == 0) {
        /* No interpolations - just a plain string */
        omni_codegen_emit_raw(ctx, "mk_string(\"%s\")", fmt);
        free(result_var);
        return;
    }

// REVIEWED:NAIVE
    /* For format strings with interpolation, use a simple approach:
     * - Start with the first literal part
     * - For each interpolation, append the variable value
     * - This generates code like: prim_strcat(mk_string("Hello "), prim_str(name))
     * - Wrap everything in a block expression ({ ... })
     */

    omni_codegen_emit_raw(ctx, "({\n");  /* Start block expression */
    omni_codegen_indent(ctx);

    char* current_var = result_var;

    /* Process the format string */
    size_t i = 0;
    int first_segment = 1;

    while (i < fmt_len) {
        if (fmt[i] == '$' && i + 1 < fmt_len) {
            /* Found interpolation marker */
            i++;  /* Skip '$' */

            /* Check for ${expr} syntax */
            if (fmt[i] == '{') {
                /* Find closing brace */
                size_t start = i + 1;
                size_t end = start;
                while (end < fmt_len && fmt[end] != '}') {
                    end++;
                }

                if (end >= fmt_len) {
                    /* Unclosed brace - treat as literal */
                    i = start - 1;
                    continue;
                }

                /* Extract the expression/variable name */
                size_t expr_len = end - start;
                char* var_name = malloc(expr_len + 1);
                memcpy(var_name, &fmt[start], expr_len);
                var_name[expr_len] = '\0';

                /* Look up the variable in the symbol table to get its C name */
                const char* c_var_name = lookup_symbol(ctx, var_name);
                int should_free_c_name = 0;
                if (!c_var_name) {
                    /* Variable not found in symbol table - use mangled name as fallback */
                    c_var_name = omni_codegen_mangle(var_name);
                    should_free_c_name = 1;
                }

                /* Generate code to convert value to string and append */
                if (!first_segment) {
                    /* Append to previous segment */
                    char* next_var = omni_codegen_temp(ctx);
                    omni_codegen_emit(ctx, "Obj* %s = prim_strcat(%s, prim_str(", next_var, current_var);
                    omni_codegen_emit_raw(ctx, "%s", c_var_name);  /* Variable reference */
                    omni_codegen_emit_raw(ctx, "));\n");
                    free(current_var);
                    current_var = next_var;
                } else {
                    /* First segment - create the initial string */
                    omni_codegen_emit(ctx, "Obj* %s = prim_str(", current_var);
                    omni_codegen_emit_raw(ctx, "%s", c_var_name);  /* Variable reference */
                    omni_codegen_emit_raw(ctx, ");\n");
                }

                /* Clean up */
                free(var_name);
                if (should_free_c_name) {
                    free((void*)c_var_name);
                }

                i = end + 1;  /* Skip past '}' */
                first_segment = 0;
            } else {
                /* $var syntax - extract variable name */
                size_t start = i;
                while (i < fmt_len && (isalnum(fmt[i]) || fmt[i] == '_')) {
                    i++;
                }

                size_t var_len = i - start;
                char* var_name = malloc(var_len + 1);
                memcpy(var_name, &fmt[start], var_len);
                var_name[var_len] = '\0';

                /* Look up the variable in the symbol table to get its C name */
                const char* c_var_name = lookup_symbol(ctx, var_name);
                int should_free_c_name = 0;
                if (!c_var_name) {
                    /* Variable not found in symbol table - use mangled name as fallback */
                    c_var_name = omni_codegen_mangle(var_name);
                    should_free_c_name = 1;
                }

                /* Generate code to convert value to string and append */
                if (!first_segment) {
                    /* Append to previous segment */
                    char* next_var = omni_codegen_temp(ctx);
                    omni_codegen_emit(ctx, "Obj* %s = prim_strcat(%s, prim_str(", next_var, current_var);
                    omni_codegen_emit_raw(ctx, "%s", c_var_name);  /* Variable reference */
                    omni_codegen_emit_raw(ctx, "));\n");
                    free(current_var);
                    current_var = next_var;
                } else {
                    /* First segment - create the initial string */
                    omni_codegen_emit(ctx, "Obj* %s = prim_str(", current_var);
                    omni_codegen_emit_raw(ctx, "%s", c_var_name);  /* Variable reference */
                    omni_codegen_emit_raw(ctx, ");\n");
                }

                /* Clean up */
                free(var_name);
                if (should_free_c_name) {
                    free((void*)c_var_name);
                }
                first_segment = 0;
            }
        } else {
            /* Literal text - accumulate until next $ or end */
            size_t start = i;
            while (i < fmt_len && fmt[i] != '$') {
                i++;
            }

            size_t lit_len = i - start;
            if (lit_len > 0) {
                /* Allocate buffer for literal segment */
                char* literal = malloc(lit_len + 1);
                memcpy(literal, &fmt[start], lit_len);
                literal[lit_len] = '\0';

                if (!first_segment) {
                    /* Append literal to current result */
                    char* next_var = omni_codegen_temp(ctx);
                    omni_codegen_emit(ctx, "Obj* %s = prim_strcat(%s, mk_string(\"%s\"));\n",
                                     next_var, current_var, literal);
                    free(current_var);
                    current_var = next_var;
                } else {
                    /* First segment - start with literal */
                    omni_codegen_emit(ctx, "Obj* %s = mk_string(\"%s\");\n", current_var, literal);
                }
                free(literal);
                first_segment = 0;
            }
        }
    }

    /* Emit the final result variable and close the block */
    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "%s; })  /* end format string block */\n", current_var);
    free(current_var);
}

/* T-wire-pika-exec-04: Runtime pattern matching codegen */
/* Generate code for pattern matching: (match-pattern input pattern) */
/*
 * Syntax:
 *   (match-pattern <input-string> <pattern-string>)
 *
 * Examples:
 *   (match-pattern "hello world" "hello")    ; => "hello"
 *   (match-pattern "123 abc" "[0-9]+")        ; => "123"
 *   (match-pattern "test" "xyz")              ; => nil (no match)
 *
 * Returns:
 *   - Matched substring if pattern matches
 *   - nil if no match
 */
static void codegen_match_pattern(CodeGenContext* ctx, OmniValue* expr) {
    /* Match pattern form: (match-pattern input pattern) */
    /* input: string to match against */
    /* pattern: pattern string (supports basic regex-like syntax) */

    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args) || omni_is_nil(omni_cdr(args))) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    /* Extract input expression */
    OmniValue* input_expr = omni_car(args);
    /* Extract pattern expression */
    OmniValue* pattern_expr = omni_car(omni_cdr(args));

    /* Generate code to evaluate input and pattern arguments */
    /* Both must be strings at runtime */

    omni_codegen_emit_raw(ctx, "prim_match_pattern(");
    codegen_expr(ctx, input_expr);
    omni_codegen_emit_raw(ctx, ", ");
    codegen_expr(ctx, pattern_expr);
    omni_codegen_emit_raw(ctx, ")");
}

/* T-wire-pika-compile-04: Pattern compilation codegen */
/* Generate code for pattern compilation: (compile-pattern pattern) */
static void codegen_compile_pattern(CodeGenContext* ctx, OmniValue* expr) {
    /* Compile pattern form: (compile-pattern <pattern-string>) */
    /* Example: (compile-pattern "[0-9]+") */

    /* Get the pattern argument */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* pattern_expr = omni_car(args);
    if (!pattern_expr) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    /* Generate code to call prim_compile_pattern */
    omni_codegen_emit_raw(ctx, "prim_compile_pattern(");
    codegen_expr(ctx, pattern_expr);
    omni_codegen_emit_raw(ctx, ")");
}

static void codegen_dict(CodeGenContext* ctx, OmniValue* expr) {
    /* Generate dict literal: #{ k1 v1 k2 v2 } */
    char* dict_var = omni_codegen_temp(ctx);

    /* Use GCC statement expression */
    omni_codegen_emit_raw(ctx, "({ Obj* %s = mk_dict_region(_local_region); ", dict_var);

    for (size_t i = 0; i < expr->dict.len; i++) {
        OmniValue* key = expr->dict.keys[i];
        OmniValue* val = expr->dict.values[i];

        omni_codegen_emit_raw(ctx, "dict_set(%s, ", dict_var);
        codegen_expr(ctx, key);
        omni_codegen_emit_raw(ctx, ", ");
        codegen_expr(ctx, val);
        omni_codegen_emit_raw(ctx, "); ");
    }

    omni_codegen_emit_raw(ctx, "%s; })", dict_var);
    free(dict_var);
}

/* Issue 24: Set literal codegen #set{1 2 3} */
static void codegen_set(CodeGenContext* ctx, OmniValue* expr) {
    /* Generate set literal: #set{ elem1 elem2 elem3 } */
    char* set_var = omni_codegen_temp(ctx);

    /* Use GCC statement expression */
    omni_codegen_emit_raw(ctx, "({ Obj* %s = mk_set_region(_local_region); ", set_var);

    for (size_t i = 0; i < expr->set.len; i++) {
        OmniValue* elem = expr->set.data[i];

        omni_codegen_emit_raw(ctx, "set_add(%s, ", set_var);
        codegen_expr(ctx, elem);
        omni_codegen_emit_raw(ctx, "); ");
    }

    omni_codegen_emit_raw(ctx, "%s; })", set_var);
    free(set_var);
}

/* T-codegen-array-01: Array literal codegen */
/* Generate code to create an array from literal syntax [elem1 elem2 ...]
 * Uses GCC statement expressions ({...}) to allow array literals in expression context.
 */
static void codegen_array(CodeGenContext* ctx, OmniValue* expr) {
    size_t len = omni_array_len(expr);

    /* Create a temporary variable name for the array */
    char* arr_var = omni_codegen_temp(ctx);

    /* Use GCC statement expression to allow multi-statement code in expression position */
    omni_codegen_emit_raw(ctx, "({ Obj* %s = mk_array_region(_local_region, %zu); ", arr_var, len);

    /* Fill the array with elements using array_push */
    for (size_t i = 0; i < len; i++) {
        OmniValue* elem = omni_array_get(expr, i);
        omni_codegen_emit_raw(ctx, "array_push(%s, ", arr_var);
        codegen_expr(ctx, elem);
        omni_codegen_emit_raw(ctx, "); ");
    }

    /* Return the array as the expression result */
    omni_codegen_emit_raw(ctx, "%s; })", arr_var);
    free(arr_var);
}

/* Forward declaration for mutual recursion */
static void codegen_type_lit(CodeGenContext* ctx, OmniValue* expr);
static void codegen_kind_splice(CodeGenContext* ctx, OmniValue* expr);

/* Helper: Generate code for a type parameter.
 * Type parameters in Kind literals may be:
 *   - Symbols (type names): Int -> mk_kind("Int", NULL, 0)
 *   - Type literals: {List [Int]} -> recursive mk_kind call
 *   - Array slots [T]: Slot containing type names, convert to Kind(s)
 */
static void codegen_type_param(CodeGenContext* ctx, OmniValue* param) {
    if (omni_is_sym(param)) {
        /* Symbol reference to a type - look up or create Kind */
        omni_codegen_emit_raw(ctx, "mk_kind(\"%s\", NULL, 0)", param->str_val);
    } else if (omni_is_type_lit(param)) {
        /* Nested type literal - recursive codegen */
        codegen_type_lit(ctx, param);
    } else if (omni_is_array(param)) {
        /* Array slot [T] or [T1 T2] - convert contents to Kind(s)
         * In Character Calculus, [] inside {} represents a type slot.
         * [Int] -> mk_kind("Int", NULL, 0)
         * [Int Float] -> multiple params (flattened)
         */
        size_t len = omni_array_len(param);
        if (len == 1) {
            /* Single element: [Int] -> just the type */
            OmniValue* elem = omni_array_get(param, 0);
            codegen_type_param(ctx, elem);
        } else if (len > 1) {
            /* Multiple elements: create tuple or handle specially */
            /* For now, just use the first element */
            OmniValue* elem = omni_array_get(param, 0);
            codegen_type_param(ctx, elem);
        } else {
            /* Empty slot [] -> Unit type */
            omni_codegen_emit_raw(ctx, "mk_kind(\"Unit\", NULL, 0)");
        }
    } else {
        /* Fallback: emit as regular expression */
        codegen_expr(ctx, param);
    }
}

static void codegen_type_lit(CodeGenContext* ctx, OmniValue* expr) {
    if (expr->type_lit.param_count == 0) {
        omni_codegen_emit_raw(ctx, "mk_kind(\"%s\", NULL, 0)", expr->type_lit.type_name);
    } else {
        /* Use C99 compound literal to create parameter array inline.
         * This makes the entire expression valid as an r-value.
         */
        omni_codegen_emit_raw(ctx, "mk_kind(\"%s\", (Obj*[]){", expr->type_lit.type_name);
        for (size_t i = 0; i < expr->type_lit.param_count; i++) {
            if (i > 0) omni_codegen_emit_raw(ctx, ", ");
            codegen_type_param(ctx, expr->type_lit.params[i]);
        }
        omni_codegen_emit_raw(ctx, "}, %zu)", expr->type_lit.param_count);
    }
}

/*
 * codegen_kind_splice - Generate code for {#kind expr}
 *
 * Issue 18 P0: Kind splice allows computing kinds at compile time.
 *
 * Strategy:
 * 1. If expr is a symbol (Int, String, etc.) → emit as mk_kind directly
 * 2. If expr is (typeof literal) → compute type at compile time
 * 3. Otherwise → emit runtime prim_value_to_type call
 */
static void codegen_kind_splice(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* splice_expr = expr->kind_splice.expr;

    if (!splice_expr || omni_is_nil(splice_expr)) {
        /* Empty splice → Any type */
        omni_codegen_emit_raw(ctx, "mk_kind(\"Any\", NULL, 0)");
        return;
    }

    /* Case 1: Direct symbol reference like Int, String, Array */
    if (omni_is_sym(splice_expr)) {
        const char* name = splice_expr->str_val;
        omni_codegen_emit_raw(ctx, "mk_kind(\"%s\", NULL, 0)", name);
        return;
    }

    /* Case 2: Type literal inside splice (redundant but valid) */
    if (omni_is_type_lit(splice_expr)) {
        codegen_type_lit(ctx, splice_expr);
        return;
    }

    /* Case 3: (typeof expr) - compile-time type computation for literals */
    if (omni_is_cell(splice_expr)) {
        OmniValue* head = omni_car(splice_expr);
        if (omni_is_sym(head) && strcmp(head->str_val, "typeof") == 0) {
            OmniValue* args = omni_cdr(splice_expr);
            if (!omni_is_nil(args)) {
                OmniValue* arg = omni_car(args);

                /* Compile-time evaluation for literals */
                if (omni_is_int(arg)) {
                    omni_codegen_emit_raw(ctx, "mk_kind(\"Int\", NULL, 0)");
                    return;
                }
                if (omni_is_float(arg)) {
                    omni_codegen_emit_raw(ctx, "mk_kind(\"Float\", NULL, 0)");
                    return;
                }
                if (omni_is_string(arg)) {
                    omni_codegen_emit_raw(ctx, "mk_kind(\"String\", NULL, 0)");
                    return;
                }
                if (omni_is_char(arg)) {
                    omni_codegen_emit_raw(ctx, "mk_kind(\"Char\", NULL, 0)");
                    return;
                }
                if (omni_is_array(arg)) {
                    omni_codegen_emit_raw(ctx, "mk_kind(\"Array\", NULL, 0)");
                    return;
                }
                if (omni_is_dict(arg)) {
                    omni_codegen_emit_raw(ctx, "mk_kind(\"Dict\", NULL, 0)");
                    return;
                }
                if (omni_is_sym(arg)) {
                    if (strcmp(arg->str_val, "true") == 0 || strcmp(arg->str_val, "false") == 0) {
                        omni_codegen_emit_raw(ctx, "mk_kind(\"Bool\", NULL, 0)");
                        return;
                    }
                    if (strcmp(arg->str_val, "nil") == 0) {
                        omni_codegen_emit_raw(ctx, "mk_kind(\"Nil\", NULL, 0)");
                        return;
                    }
                }
                if (omni_is_nothing(arg)) {
                    omni_codegen_emit_raw(ctx, "mk_kind(\"Nothing\", NULL, 0)");
                    return;
                }

                /* Non-literal: emit runtime prim_value_to_type call */
                omni_codegen_emit_raw(ctx, "prim_value_to_type(");
                codegen_expr(ctx, arg);
                omni_codegen_emit_raw(ctx, ")");
                return;
            }
        }
    }

    /* Default case: emit runtime evaluation */
    omni_codegen_emit_raw(ctx, "prim_value_to_type(");
    codegen_expr(ctx, splice_expr);
    omni_codegen_emit_raw(ctx, ")");
}

static void codegen_quote(CodeGenContext* ctx, OmniValue* expr) {
    /* (quote x) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* val = omni_car(args);

    if (omni_is_nil(val)) {
        omni_codegen_emit_raw(ctx, "NIL");
    } else if (omni_is_int(val)) {
        omni_codegen_emit_raw(ctx, "mk_int(%ld)", (long)val->int_val);
    } else if (omni_is_nothing(val)) {
        omni_codegen_emit_raw(ctx, "NOTHING");
    } else if (omni_is_sym(val)) {
        if (strcmp(val->str_val, "true") == 0) {
            omni_codegen_emit_raw(ctx, "mk_bool(1)");
        } else if (strcmp(val->str_val, "false") == 0) {
            omni_codegen_emit_raw(ctx, "mk_bool(0)");
        } else {
            omni_codegen_emit_raw(ctx, "mk_sym(\"%s\")", val->str_val);
        }
    } else if (omni_is_cell(val)) {
        /* Build list at runtime */
        omni_codegen_emit_raw(ctx, "mk_cell(");
        codegen_quote(ctx, omni_list2(omni_new_sym("quote"), omni_car(val)));
        omni_codegen_emit_raw(ctx, ", ");
        codegen_quote(ctx, omni_list2(omni_new_sym("quote"), omni_cdr(val)));
        omni_codegen_emit_raw(ctx, ")");
    } else {
        omni_codegen_emit_raw(ctx, "NIL");
    }
}

static void codegen_if(CodeGenContext* ctx, OmniValue* expr) {
    /* (if cond then else) */
    OmniValue* args = omni_cdr(expr);
    OmniValue* cond = omni_car(args);
    args = omni_cdr(args);
    OmniValue* then_expr = omni_is_nil(args) ? NULL : omni_car(args);
    args = omni_cdr(args);
    OmniValue* else_expr = omni_is_nil(args) ? NULL : omni_car(args);

    omni_codegen_emit_raw(ctx, "(is_truthy(");
    codegen_expr(ctx, cond);
    omni_codegen_emit_raw(ctx, ") ? (");
    if (then_expr) codegen_expr(ctx, then_expr);
    else omni_codegen_emit_raw(ctx, "NOTHING");
    omni_codegen_emit_raw(ctx, ") : (");
    if (else_expr) codegen_expr(ctx, else_expr);
    else omni_codegen_emit_raw(ctx, "NOTHING");
    omni_codegen_emit_raw(ctx, "))");
}

/* TESTED */
static void codegen_let(CodeGenContext* ctx, OmniValue* expr) {
    /* Strict Character Calculus let forms:
     *   - List style: (let ((x val) (y val)) body...)
     *   - Slot syntax: (let [x val] [y val] body...)
     *   - Slot with types: (let [x {Int} val] [y {String} val] body...)
     */
    OmniValue* args = omni_cdr(expr);
    OmniValue* first = omni_car(args);
    OmniValue* rest = omni_cdr(args);

    omni_codegen_emit_raw(ctx, "({\n");
    omni_codegen_indent(ctx);

    /* Slot syntax: (let [x val] [y val] body...) */
    if (omni_is_array(first)) {
        /* Process all array bindings */
        OmniValue* current = args;
        while (current && !omni_is_nil(current) && omni_is_cell(current)) {
            OmniValue* elem = omni_car(current);

            if (!omni_is_array(elem)) {
                /* Not an array - we've hit the body */
                break;
            }

            /* Process array binding: [name val] or [name type val] */
            size_t len = omni_array_len(elem);
            if (len >= 2) {
                OmniValue* name_val = omni_array_get(elem, 0);
                if (omni_is_sym(name_val)) {
                    char* c_name = omni_codegen_mangle(name_val->str_val);
                    omni_codegen_emit(ctx, "Obj* %s = ", c_name);

                    /* Get the value - last element */
                    if (len == 2) {
                        codegen_expr(ctx, omni_array_get(elem, 1));
                    } else if (len == 3) {
                        /* [name type val] - skip type at index 1, use val at index 2 */
                        codegen_expr(ctx, omni_array_get(elem, 2));
                    }

                    omni_codegen_emit_raw(ctx, ";\n");
                    register_symbol(ctx, name_val->str_val, c_name);
                    free(c_name);
                }
            }

            current = omni_cdr(current);
        }

        /* Emit body */
        OmniValue* result = NULL;
        OmniValue* body = current;
        while (body && !omni_is_nil(body) && omni_is_cell(body)) {
            result = omni_car(body);
            body = omni_cdr(body);
            if (!omni_is_nil(body)) {
                omni_codegen_emit(ctx, "");
                codegen_expr(ctx, result);
                omni_codegen_emit_raw(ctx, ";\n");
            }
        }

        if (result) {
            omni_codegen_emit(ctx, "");
            codegen_expr(ctx, result);
            omni_codegen_emit_raw(ctx, ";\n");
        }
    } else if (omni_is_cell(first)) {
        /* List-style bindings: ((x 1) (y 2)) */
        OmniValue* bindings = first;
        OmniValue* body = rest;

        while (!omni_is_nil(bindings) && omni_is_cell(bindings)) {
            OmniValue* binding = omni_car(bindings);
            if (omni_is_cell(binding)) {
                OmniValue* name = omni_car(binding);
                OmniValue* val = omni_car(omni_cdr(binding));
                if (omni_is_sym(name)) {
                    char* c_name = omni_codegen_mangle(name->str_val);
                    omni_codegen_emit(ctx, "Obj* %s = ", c_name);
                    codegen_expr(ctx, val);
                    omni_codegen_emit_raw(ctx, ";\n");
                    register_symbol(ctx, name->str_val, c_name);
                    free(c_name);
                }
            }
            bindings = omni_cdr(bindings);
        }

        /* Emit body */
        OmniValue* result = NULL;
        while (!omni_is_nil(body) && omni_is_cell(body)) {
            result = omni_car(body);
            body = omni_cdr(body);
            if (!omni_is_nil(body)) {
                omni_codegen_emit(ctx, "");
                codegen_expr(ctx, result);
                omni_codegen_emit_raw(ctx, ";\n");
            }
        }

        if (result) {
            omni_codegen_emit(ctx, "");
            codegen_expr(ctx, result);
            omni_codegen_emit_raw(ctx, ";\n");
        }
    }

    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "})");
}

/*
 * is_lambda_expr - Check if an expression is a lambda form
 *
 * Returns true for: (lambda ...), (fn ...), (λ ...)
 * Note: Arrow syntax (-> ...) has been superseded by (fn ...)
 */
static bool is_lambda_expr(OmniValue* expr) {
    if (!omni_is_cell(expr)) return false;
    OmniValue* head = omni_car(expr);
    if (!omni_is_sym(head)) return false;
    const char* name = head->str_val;
    return (strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0 ||
            strcmp(name, "λ") == 0);
}

/*
 * count_lambda_params - Count the number of parameters in a lambda expression
 *
 * Supports: (lambda (params...) body) and (fn [params...] body)
 */
static int count_lambda_params(OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    OmniValue* params = omni_car(args);
    int count = 0;

    if (omni_is_cell(params)) {
        /* List-style params: (lambda (x y) ...) */
        OmniValue* p = params;
        while (!omni_is_nil(p) && omni_is_cell(p)) {
            if (omni_is_sym(omni_car(p))) count++;
            p = omni_cdr(p);
        }
    } else if (omni_is_array(params)) {
        /* Array-style params: (fn [x y] ...) */
        size_t len = omni_array_len(params);
        for (size_t i = 0; i < len; i++) {
            OmniValue* param = omni_array_get(params, i);
            if (omni_is_type_lit(param)) continue;
            if (omni_is_sym(param)) count++;
        }
    }
    return count;
}

/* Forward declaration */
static void codegen_lambda(CodeGenContext* ctx, OmniValue* expr);

/*
 * codegen_lambda_as_closure - Generate a lambda as a closure object for HOFs
 *
 * Issue 2 P1: When passing a lambda to a HOF, we need to wrap it in a closure
 * object because the runtime HOFs expect Obj* (closure objects), not function pointers.
 *
 * Issue 1 P2: Closures with captures now collect free variables from the lambda
 * body and pass them to mk_closure_region. The runtime's store barrier
 * (omni_store_repair) handles escape repair for captured values.
 *
 * This generates:
 * 1. The static lambda function (via codegen_lambda side effects)
 * 2. A trampoline function that adapts the signature and passes captures
 * 3. A call to mk_closure_region to create the closure object with captures
 */
static void codegen_lambda_as_closure(CodeGenContext* ctx, OmniValue* lambda_expr) {
    /* Issue 1 P2: Collect captured variables from the lambda */
    CaptureList captures = collect_lambda_captures(lambda_expr, ctx);

    /* Get the lambda ID that will be generated */
    int lambda_id = ctx->lambda_counter;
    int arity = count_lambda_params(lambda_expr);

    /* Generate the static lambda function (this increments lambda_counter) */
    /* We need to capture the emitted function call start and replace it */
    char fn_name[64];
    snprintf(fn_name, sizeof(fn_name), "_lambda_%d", lambda_id);
    char tramp_name[64];
    snprintf(tramp_name, sizeof(tramp_name), "_lambda_%d_tramp", lambda_id);

    /* Generate trampoline function definition
     * Issue 1 P2: Trampoline passes _captures through to lambda.
     * The lambda function itself unpacks captures.
     */
    char tramp_def[4096];
    char* p = tramp_def;
    p += sprintf(p, "static Obj* %s(struct Region* _caller_region, Obj** _captures, Obj** _args, int _argc) {\n", tramp_name);
    p += sprintf(p, "    (void)_argc;\n");
    /* Lambda function receives _captures as 2nd parameter */
    p += sprintf(p, "    return %s(_caller_region, _captures", fn_name);
    for (int i = 0; i < arity; i++) {
        p += sprintf(p, ", _args[%d]", i);
    }
    p += sprintf(p, ");\n");
    p += sprintf(p, "}");

    /* Issue 1 P2: Add forward declarations for lambda and trampoline.
     * This ensures static functions can reference them even if they
     * appear later in the file.
     */
    char fwd_decl[256];
    snprintf(fwd_decl, sizeof(fwd_decl),
             "static Obj* %s(struct Region*, Obj**, Obj**, int);", tramp_name);
    omni_codegen_add_forward_decl(ctx, fwd_decl);

    /* Add trampoline to lambda definitions (it will be emitted before main) */
    /* First, generate the lambda itself (adds to lambda_defs) */
    CodeGenContext* tmp = omni_codegen_new_buffer();
    tmp->lambda_counter = ctx->lambda_counter;
    /* Copy symbol table */
    for (size_t i = 0; i < ctx->symbols.count; i++) {
        register_symbol(tmp, ctx->symbols.names[i], ctx->symbols.c_names[i]);
    }

    /* Issue 1 P2: Pass captures to codegen_lambda so it generates
     * the _captures parameter and unpacking code */
    tmp->current_captures = &captures;

    /* Generate lambda - this will emit "_lambda_N(_local_region" to tmp buffer */
    codegen_lambda(tmp, lambda_expr);

    /* Update lambda_counter in main context */
    ctx->lambda_counter = tmp->lambda_counter;

    /* Copy lambda definitions from tmp to main context */
    for (size_t i = 0; i < tmp->lambda_defs.count; i++) {
        omni_codegen_add_lambda_def(ctx, tmp->lambda_defs.defs[i]);
    }

    /* Add trampoline definition */
    omni_codegen_add_lambda_def(ctx, tramp_def);

    omni_codegen_free(tmp);

    /* Issue 1 P2: Emit closure creation with captures
     *
     * If there are captures, we need to:
     * 1. Build a temporary array of captured values
     * 2. Pass it to mk_closure_region
     *
     * The runtime's store barrier (omni_store_repair) in mk_closure_region
     * will handle escape repair for each captured value.
     */
    if (captures.count > 0) {
        /* Build captures array inline */
        omni_codegen_emit_raw(ctx, "({\n");
        omni_codegen_emit_raw(ctx, "        /* Issue 1 P2: Build captures array for closure */\n");
        omni_codegen_emit_raw(ctx, "        Obj* _cap_arr[%zu] = {", captures.count);
        for (size_t i = 0; i < captures.count; i++) {
            if (i > 0) omni_codegen_emit_raw(ctx, ", ");
            omni_codegen_emit_raw(ctx, "%s", captures.c_names[i]);
        }
        omni_codegen_emit_raw(ctx, "};\n");
        omni_codegen_emit_raw(ctx, "        mk_closure_region(_local_region, %s, _cap_arr, %zu, %d);\n",
                             tramp_name, captures.count, arity);
        omni_codegen_emit_raw(ctx, "    })");
    } else {
        /* No captures - original simple form */
        omni_codegen_emit_raw(ctx, "mk_closure_region(_local_region, %s, NULL, 0, %d)", tramp_name, arity);
    }

    capture_list_free(&captures);
}

static void codegen_lambda(CodeGenContext* ctx, OmniValue* expr) {
    /* Generate lambda as a static function with region lifecycle
     *
     * Supports: (lambda (params...) body) and (fn [params...] body)
     * Note: Arrow syntax (-> ...) has been superseded by (fn ...)
     */
    int lambda_id = ctx->lambda_counter++;

    OmniValue* args = omni_cdr(expr);
    OmniValue* params = omni_car(args);
    OmniValue* body = omni_cdr(args);

    /* Generate lambda function name */
    char fn_name[64];
    snprintf(fn_name, sizeof(fn_name), "_lambda_%d", lambda_id);

    /* Build function definition into a buffer */
    char def[16384];  /* Increased buffer for region management code */
    char* p = def;

    /* Issue 1 P2: Check if we have captures to handle */
    CaptureList* captures = (CaptureList*)ctx->current_captures;

    /* Function signature: Include Region* _caller_region as first parameter,
     * Obj** _captures as second parameter (if has captures) */
    p += sprintf(p, "static Obj* %s(struct Region* _caller_region, Obj** _captures", fn_name);

    /* Parameters - register them before generating body */
    bool first_param = false;  /* Already have _caller_region and _captures */

    if (omni_is_cell(params)) {
        /* List-style: (lambda (x y) body) */
        OmniValue* param_list = params;
        while (!omni_is_nil(param_list) && omni_is_cell(param_list)) {
            if (!first_param) p += sprintf(p, ", ");
            first_param = false;
            OmniValue* param = omni_car(param_list);
            if (omni_is_sym(param)) {
                char* c_name = omni_codegen_mangle(param->str_val);
                p += sprintf(p, "Obj* %s", c_name);
                register_symbol(ctx, param->str_val, c_name);
                free(c_name);
            }
            param_list = omni_cdr(param_list);
        }
    } else if (omni_is_array(params)) {
        /* Slot syntax: (fn [x y] body) - array with params */
        size_t len = omni_array_len(params);
        for (size_t i = 0; i < len; i++) {
            OmniValue* param = omni_array_get(params, i);
            /* Skip type annotations {Type} */
            if (omni_is_type_lit(param)) continue;
            if (omni_is_sym(param)) {
                if (!first_param) p += sprintf(p, ", ");
                first_param = false;
                char* c_name = omni_codegen_mangle(param->str_val);
                p += sprintf(p, "Obj* %s", c_name);
                register_symbol(ctx, param->str_val, c_name);
                free(c_name);
            }
        }
    }
    p += sprintf(p, ") {\n");

    /* Region-RC: Create local region at function entry */
    p += sprintf(p, "    /* Region-RC: Create local region for allocations */\n");
    p += sprintf(p, "    struct Region* _local_region = region_create();\n");
    /* Issue 2 P4.2: Assign lifetime_rank based on caller region (outlives depth) */
    p += sprintf(p, "    omni_region_set_lifetime_rank(_local_region, omni_region_get_lifetime_rank(_caller_region) + 1);\n");
    /* Issue 2 P4.3b: Assign parent linkage (outlives ancestry) */
    p += sprintf(p, "    omni_region_set_parent(_local_region, _caller_region);\n");
    p += sprintf(p, "    \n");

    /* Region-RC: Emit tethering for parameters from outer regions
     * Note: This is a simplified version that tethers all parameters.
     * A more sophisticated implementation would analyze which parameters
     * come from outer regions and only tether those.
     */
    p += sprintf(p, "    /* Region-RC: Tether parameters from outer regions */\n");
    p += sprintf(p, "    /* (All parameters are assumed to come from _caller_region) */\n");
    p += sprintf(p, "    region_tether_start(_caller_region);  /* Keep caller region alive */\n");
    p += sprintf(p, "    \n");

    /* Issue 1 P2: Unpack captures into local variables */
    if (captures && captures->count > 0) {
        p += sprintf(p, "    /* Issue 1 P2: Unpack %zu captured variable(s) */\n", captures->count);
        for (size_t i = 0; i < captures->count; i++) {
            p += sprintf(p, "    Obj* %s = _captures[%zu];\n", captures->c_names[i], i);
            /* Register the captured variable in symbol table for body generation */
            register_symbol(ctx, captures->names[i], captures->c_names[i]);
        }
        p += sprintf(p, "    \n");
    } else {
        p += sprintf(p, "    (void)_captures;  /* No captures */\n");
        p += sprintf(p, "    \n");
    }

    /* Generate body - find last expression for return */
    OmniValue* result = NULL;
    OmniValue* body_iter = body;
    while (!omni_is_nil(body_iter) && omni_is_cell(body_iter)) {
        result = omni_car(body_iter);
        body_iter = omni_cdr(body_iter);
    }

    /* Generate body using a temp context to capture output */
    if (result) {
        CodeGenContext* tmp = omni_codegen_new_buffer();
        tmp->indent_level = 1;
        tmp->lambda_counter = ctx->lambda_counter;
        /* Copy symbol table */
        for (size_t i = 0; i < ctx->symbols.count; i++) {
            register_symbol(tmp, ctx->symbols.names[i], ctx->symbols.c_names[i]);
        }

        /* Region-RC: Transmigrate return value and cleanup */
        p += sprintf(p, "    /* Compute result in local region */\n");
        p += sprintf(p, "    Obj* _result = ");

        /* Generate the result expression */
        omni_codegen_emit(tmp, "");
        codegen_expr(tmp, result);
        omni_codegen_emit_raw(tmp, ";\n");

        /* Issue 1 P2: Propagate nested lambda definitions back to outer context.
         * If the body contains lambdas (e.g., returning a closure), their
         * definitions are added to tmp->lambda_defs and must be copied to ctx.
         */
        ctx->lambda_counter = tmp->lambda_counter;
        for (size_t i = 0; i < tmp->lambda_defs.count; i++) {
            omni_codegen_add_lambda_def(ctx, tmp->lambda_defs.defs[i]);
        }

	        /* Get the result expression code */
	        char* result_code = omni_codegen_get_output(tmp);
	        if (result_code) {
	            /* Strip trailing newline for cleaner code */
	            size_t len = strlen(result_code);
	            if (len > 0 && result_code[len-1] == '\n') {
	                result_code[len-1] = '\0';
	            }
	            p += sprintf(p, "%s", result_code);
	            free(result_code);
	        }

	        /*
	         * Issue 1 P2 regression fix:
	         *
	         * `tmp` is reused to emit escape-repair statements via
	         * omni_codegen_escape_repair(). If we do not clear the buffer,
	         * the later omni_codegen_get_output(tmp) will include both the
	         * return-expression statement and the escape-repair statements.
	         *
	         * That would re-emit the return expression as a standalone
	         * statement in the lambda body, duplicating evaluation (and
	         * duplicating side effects like (print ...)).
	         */
	        if (tmp->output_buffer) {
	            tmp->output_size = 0;
	            tmp->output_buffer[0] = '\0';
	        }

	        /* Issue 1 P2: Emit escape repair (transmigrate or retain based on strategy) */
	        /* Select strategy based on environment variable or analysis */
	        EscapeRepairStrategy lambda_strategy = omni_choose_escape_repair_strategy(ctx, "_result");
	        omni_codegen_escape_repair(tmp, "_result", "_caller_region", lambda_strategy);

        /* Get the escape repair code */
        char* repair_code = omni_codegen_get_output(tmp);
        if (repair_code) {
            p += sprintf(p, "%s", repair_code);
            free(repair_code);
        }
        omni_codegen_free(tmp);

        p += sprintf(p, "    \n");

        /* Issue 3 P2: Non-lexical region end (straight-line only) */
        /* Note: Region could exit earlier if all variables are dead */
        /* Full implementation requires position tracking during codegen */
        p += sprintf(p, "    /* ISSUE 3 P2: Non-lexical region end - region could exit here if all vars dead */\n");

        p += sprintf(p, "    /* Region-RC: Exit local region (mark scope as inactive) */\n");
        p += sprintf(p, "    region_exit(_local_region);\n");
        p += sprintf(p, "    region_destroy_if_dead(_local_region);\n");
        p += sprintf(p, "    \n");
        p += sprintf(p, "    /* Region-RC: Release tether on caller region */\n");
        p += sprintf(p, "    region_tether_end(_caller_region);\n");
        p += sprintf(p, "    \n");
        p += sprintf(p, "    return _result;  /* Repaired by omni_codegen_escape_repair() */\n");
    } else {
        p += sprintf(p, "    /* Region-RC: Exit local region */\n");
        p += sprintf(p, "    region_exit(_local_region);\n");
        p += sprintf(p, "    region_destroy_if_dead(_local_region);\n");
        p += sprintf(p, "    \n");
        p += sprintf(p, "    /* Region-RC: Release tether on caller region */\n");
        p += sprintf(p, "    region_tether_end(_caller_region);\n");
        p += sprintf(p, "    \n");
        p += sprintf(p, "    return NOTHING;\n");
    }

    p += sprintf(p, "}");

    /* Add to lambda definitions */
    omni_codegen_add_lambda_def(ctx, def);

    /* Emit function name at call site with region and captures parameters.
     * Issue 1 P2: All lambdas now take _captures as 2nd parameter.
     * For inline application (not via closure), pass NULL for _captures.
     */
    omni_codegen_emit_raw(ctx, "%s(_local_region, NULL", fn_name);
}

/* ============== Multiple Dispatch Support ============== */

// REVIEWED:OPTIMIZED - O(1) hash lookup instead of O(n) linear scan
/*
 * check_function_defined - Check if a function has been defined before.
 * Returns the number of times the function has been defined (0 if not defined).
 */
static int check_function_defined(CodeGenContext* ctx, const char* c_name) {
    /* O(1) hash lookup - map stores index+1 (so 0 means not found) */
    if (ctx->defined_functions.map) {
        size_t idx_plus_one = (size_t)strmap_get(ctx->defined_functions.map, c_name);
        if (idx_plus_one > 0) {
            return ctx->defined_functions.definition_count[idx_plus_one - 1];
        }
        return 0;
    }
    /* Fallback to linear scan if hash map not initialized */
    for (size_t i = 0; i < ctx->defined_functions.count; i++) {
        if (strcmp(ctx->defined_functions.names[i], c_name) == 0) {
            return ctx->defined_functions.definition_count[i];
        }
    }
    return 0;
}

// REVIEWED:OPTIMIZED - O(1) hash lookup instead of O(n) linear scan
/*
 * track_function_definition - Record that a function has been defined.
 * Increments the definition count for the function.
 */
static void track_function_definition(CodeGenContext* ctx, const char* c_name) {
    /* O(1) hash lookup - map stores index+1 */
    if (ctx->defined_functions.map) {
        size_t idx_plus_one = (size_t)strmap_get(ctx->defined_functions.map, c_name);
        if (idx_plus_one > 0) {
            ctx->defined_functions.definition_count[idx_plus_one - 1]++;
            return;
        }
    } else {
        /* Fallback to linear scan if hash map not initialized */
        for (size_t i = 0; i < ctx->defined_functions.count; i++) {
            if (strcmp(ctx->defined_functions.names[i], c_name) == 0) {
                ctx->defined_functions.definition_count[i]++;
                return;
            }
        }
    }

    /* Add new entry */
    if (ctx->defined_functions.count >= ctx->defined_functions.capacity) {
        size_t new_capacity = ctx->defined_functions.capacity == 0 ? 16 : ctx->defined_functions.capacity * 2;
        ctx->defined_functions.names = realloc(ctx->defined_functions.names, new_capacity * sizeof(char*));
        ctx->defined_functions.definition_count = realloc(ctx->defined_functions.definition_count, new_capacity * sizeof(int));
        ctx->defined_functions.capacity = new_capacity;
    }

    ctx->defined_functions.names[ctx->defined_functions.count] = strdup(c_name);
    ctx->defined_functions.definition_count[ctx->defined_functions.count] = 1;

    /* Add to hash map for O(1) future lookups - store index+1 */
    if (ctx->defined_functions.map) {
        strmap_put(ctx->defined_functions.map, c_name, (void*)(ctx->defined_functions.count + 1));
    }

    ctx->defined_functions.count++;
}

/*
 * Phase 19: Track typed method for multiple dispatch.
 *
 * Records a typed function definition (e.g., (define add [x {Int}] [y {Int}] ...))
 * to enable generic function construction.
 *
 * Args:
 *   ctx         - CodeGen context
 *   omni_name   - OmniLisp function name (e.g., "add")
 *   mangled_name - C function name (e.g., "o_add__Int_Int")
 *   param_types - Array of type name strings (e.g., ["Int", "Int"])
 *   param_count - Number of parameters
 */
static void track_typed_method(CodeGenContext* ctx, const char* omni_name,
                               const char* mangled_name, const char** param_types,
                               int param_count) {
    /* Grow arrays if needed */
    if (ctx->typed_methods.count >= ctx->typed_methods.capacity) {
        size_t new_capacity = ctx->typed_methods.capacity == 0 ? 16 : ctx->typed_methods.capacity * 2;
        ctx->typed_methods.omni_names = realloc(ctx->typed_methods.omni_names, new_capacity * sizeof(char*));
        ctx->typed_methods.mangled_names = realloc(ctx->typed_methods.mangled_names, new_capacity * sizeof(char*));
        ctx->typed_methods.param_types = realloc(ctx->typed_methods.param_types, new_capacity * sizeof(char**));
        ctx->typed_methods.param_counts = realloc(ctx->typed_methods.param_counts, new_capacity * sizeof(int));
        ctx->typed_methods.capacity = new_capacity;
    }

    /* Store the method */
    size_t idx = ctx->typed_methods.count;
    ctx->typed_methods.omni_names[idx] = strdup(omni_name);
    ctx->typed_methods.mangled_names[idx] = strdup(mangled_name);
    ctx->typed_methods.param_counts[idx] = param_count;

    /* Copy parameter types */
    if (param_count > 0 && param_types) {
        ctx->typed_methods.param_types[idx] = malloc(param_count * sizeof(char*));
        for (int i = 0; i < param_count; i++) {
            ctx->typed_methods.param_types[idx][i] = strdup(param_types[i] ? param_types[i] : "Any");
        }
    } else {
        ctx->typed_methods.param_types[idx] = NULL;
    }

    /* Track in generics_map: first occurrence of each omni_name */
    if (ctx->typed_methods.generics_map) {
        void* existing = strmap_get(ctx->typed_methods.generics_map, omni_name);
        if (!existing) {
            /* First method for this generic - store index+1 */
            strmap_put(ctx->typed_methods.generics_map, omni_name, (void*)(idx + 1));
        }
    }

    ctx->typed_methods.count++;
}

/*
 * Phase 19: Check if a function name has multiple typed definitions.
 * Returns the number of methods registered for this generic function name.
 */
static int count_generic_methods(CodeGenContext* ctx, const char* omni_name) {
    int count = 0;
    for (size_t i = 0; i < ctx->typed_methods.count; i++) {
        if (strcmp(ctx->typed_methods.omni_names[i], omni_name) == 0) {
            count++;
        }
    }
    return count;
}

/*
 * Phase 19: Generate a unique mangled name for a typed method.
 * For (define add [x {Int}] [y {Int}] ...), generates "o_add__Int_Int".
 *
 * Returns: Newly allocated string (caller must free)
 */
static char* generate_typed_mangled_name(const char* omni_name, const char** type_names, int count) {
    /* Calculate needed size */
    size_t size = strlen("o_") + strlen(omni_name) + 3; /* "__" prefix + null */
    for (int i = 0; i < count; i++) {
        size += strlen(type_names[i] ? type_names[i] : "Any") + 1; /* underscore separator */
    }

    char* result = malloc(size);
    if (!result) return NULL;

    /* Build the name: o_<name>__<Type1>_<Type2>_... */
    strcpy(result, "o_");
    strcat(result, omni_name);
    strcat(result, "__");
    for (int i = 0; i < count; i++) {
        if (i > 0) strcat(result, "_");
        strcat(result, type_names[i] ? type_names[i] : "Any");
    }

    return result;
}

/*
 * Phase 19: Check if a function name has multiple typed methods registered.
 * A function is "generic" if it has at least 2 typed method definitions.
 * Returns true if the function should use omni_generic_invoke for dispatch.
 */
static bool is_generic_function(CodeGenContext* ctx, const char* omni_name) {
    if (!ctx || !ctx->typed_methods.generics_map || !omni_name) return false;

    int* index_ptr = (int*)strmap_get(ctx->typed_methods.generics_map, omni_name);
    if (!index_ptr) return false;

    /* Count how many methods share this omni_name */
    int method_count = 0;
    for (size_t i = 0; i < ctx->typed_methods.count; i++) {
        if (strcmp(ctx->typed_methods.omni_names[i], omni_name) == 0) {
            method_count++;
        }
    }

    /* Only use generic dispatch if there are at least 2 methods */
    return method_count >= 2;
}

/* Helper: Check if a symbol is a metadata key (e.g., ^:effects, ^:parent)
 * Issue 9 P3: Metadata keys are not valid parameter names
 */
static bool is_metadata_key(OmniValue* sym) {
    if (!omni_is_sym(sym)) return false;
    const char* name = sym->str_val;
    return name && name[0] == '^' && name[1] == ':';
}

/* Helper: Extract parameter name from a Slot or symbol for codegen
 * Returns NULL if not a valid parameter form
 * Supports:
 *   - Plain symbol: x
 *   - Slot array: [x]
 *   - Slot with type: [x {Int}] (ignores the type)
 * Issue 9 P3: Excludes metadata keys (^:effects, ^:parent, etc.)
 *
 * OmniLisp Character Calculus:
 *   [] = Slot (data/parameters)
 *   {} = Kind (types)
 *   () = Flow (execution) - NEVER a parameter form
 *   ^  = Metadata only (^:mutable, ^:where) - NOT type annotations
 */
static OmniValue* codegen_extract_param_name(OmniValue* param) {
    if (omni_is_sym(param)) {
        /* Plain symbol: x (but not metadata keys like ^:effects) */
        if (is_metadata_key(param)) return NULL;
        return param;
    }
    if (omni_is_array(param) && param->array.len > 0) {
        /* Slot: [x] or [x {Type}] - first element is the name */
        OmniValue* first = omni_array_get(param, 0);
        if (omni_is_sym(first) && !is_metadata_key(first)) {
            return first;
        }
    }
    /* Cells like (module-ref ...) are NEVER parameter forms in OmniLisp */
    return NULL;
}

/*
 * Extract the type annotation from a parameter slot.
 * For [x {Int}], returns the "{Int}" type literal.
 * For [x] or plain x, returns NULL.
 *
 * Multiple Dispatch Support (Phase 19):
 * This extracts type information to enable Julia-style multiple dispatch.
 * When a function has multiple definitions with different parameter types,
 * each definition becomes a method in a generic function.
 */
static OmniValue* codegen_extract_param_type(OmniValue* param) {
    if (omni_is_array(param) && param->array.len > 1) {
        /* Slot: [x {Type}] - second element is the type */
        OmniValue* second = omni_array_get(param, 1);
        if (omni_is_type_lit(second)) {
            return second;
        }
    }
    return NULL;  /* No type annotation */
}

/*
 * Extract the type name string from a type literal.
 * For {Int}, returns "Int".
 * For {List {Int}}, returns "List" (base type only for now).
 */
static const char* codegen_type_name(OmniValue* type_lit) {
    if (!type_lit || !omni_is_type_lit(type_lit)) return NULL;

    /* Type literal is {...} - internal representation stores name */
    if (omni_is_cell(type_lit)) {
        OmniValue* first = omni_car(type_lit);
        if (omni_is_sym(first)) {
            return first->str_val;
        }
    }
    /* Simple type like {Int} might be stored differently */
    if (type_lit->str_val) {
        return type_lit->str_val;
    }
    return NULL;
}

/* Helper: Check if a define expression is a function definition
 * Returns true for:
 *   - Traditional Scheme style: (define (f x y) body)
 *   - Slot syntax: (define f [x] [y] body)
 *   - Typed slot syntax: (define f [x {Int}] [y {Int}] {Int} body)
 * Returns false for:
 *   - Variable defines: (define x 42)
 *   - Type alias defines: (define T1 {Int})
 */
static bool is_function_define(OmniValue* expr) {
    if (!omni_is_cell(expr) || !omni_is_sym(omni_car(expr))) return false;

    const char* head = omni_car(expr)->str_val;

    if (strcmp(head, "define") != 0) return false;

    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return false;

    OmniValue* name_or_sig = omni_car(args);

    /* Traditional Scheme style: (define (f x y) body) */
    if (omni_is_cell(name_or_sig)) {
        return true;
    }

    /* Slot syntax: (define f [x] [y] body) */
    if (omni_is_sym(name_or_sig)) {
        OmniValue* rest = omni_cdr(args);
        if (!omni_is_nil(rest)) {
            OmniValue* maybe_param = omni_car(rest);
            OmniValue* param_name = codegen_extract_param_name(maybe_param);
            if (param_name != NULL) {
                return true;
            }
        }
    }

    return false;
}

static void codegen_define(CodeGenContext* ctx, OmniValue* expr) {
    /* OmniLisp define forms (Strict Character Calculus):
     *   - Simple define: (define x value)
     *   - Slot shorthand: (define f x y body)
     *   - Slot syntax: (define f [x] [y] body)
     *   - Slot with types: (define f [x {Int}] [y {String}] body)
     *   - Mixed syntax: (define f x [y {Int}] z body)
     *
     * NOTE: Scheme-style (define (f x y) body) is NOT supported.
     */
    OmniValue* args = omni_cdr(expr);
    OmniValue* first = omni_car(args);
    OmniValue* rest = omni_cdr(args);

    /* Check if this is a defn form */
    bool is_defn = omni_is_cell(expr) && omni_is_sym(omni_car(expr)) &&
                   strcmp(omni_car(expr)->str_val, "defn") == 0;

    if (omni_is_sym(first)) {
        /* For defn forms, skip over return type annotations to find first parameter */
        OmniValue* param_scan = rest;
        if (is_defn) {
            while (!omni_is_nil(param_scan) && omni_is_cell(param_scan)) {
                OmniValue* item = omni_car(param_scan);
                if (!omni_is_type_lit(item)) break;  /* Found non-type element */
                param_scan = omni_cdr(param_scan);
            }
        }

        OmniValue* next = omni_is_nil(param_scan) ? NULL : omni_car(param_scan);

        /* Check for new Slot syntax: (define f [x] [y] body) or (define f x y body)
         * Also handles defn form: (defn f ^RetType (x ^Type) body) */
        if (next && !omni_is_nil(param_scan)) {
            OmniValue* maybe_param = next;
            OmniValue* param_name = codegen_extract_param_name(maybe_param);

            /* If we can extract a parameter name from the second element, this is Slot syntax */
            if (param_name != NULL) {
                OmniValue* fname = first;
                char* c_name = omni_codegen_mangle(fname->str_val);

                /* Slot-syntax function: emit like traditional Scheme-style function.
                 * Just a static function, no closure machinery. Calls are direct.
                 * Only process during first pass - skip in main pass.
                 */
                if (!ctx->first_pass) {
                    /* Main pass: nothing to emit for functions */
                    free(c_name);
                    return;
                }

                /* Count parameters and extract types for multiple dispatch
                 * Issue 9 P3: Skip metadata keys (^:effects) and their values
                 * Phase 19: Also extract type annotations for generic functions
                 */
                size_t param_count = 0;
                OmniValue* params_iter = rest;
                bool skip_next = false;  /* Skip value after metadata key */

                /* First pass: count parameters */
                while (!omni_is_nil(params_iter) && omni_is_cell(params_iter)) {
                    if (omni_is_nil(omni_cdr(params_iter))) {
                        /* Last element - this is the body, not a parameter */
                        break;
                    }
                    OmniValue* item = omni_car(params_iter);
                    if (skip_next) {
                        /* Skip the value following a metadata key (e.g., effect list) */
                        skip_next = false;
                    } else if (is_metadata_key(item)) {
                        /* Skip metadata key and mark next item to skip too */
                        skip_next = true;
                    } else if (omni_is_type_lit(item)) {
                        /* Skip return type annotations {Int} */
                    } else {
                        param_count++;
                    }
                    params_iter = omni_cdr(params_iter);
                }

                /* Phase 19: Extract type annotations for multiple dispatch */
                const char** type_names = NULL;
                bool has_typed_params = false;
                if (param_count > 0) {
                    type_names = malloc(param_count * sizeof(const char*));
                    memset(type_names, 0, param_count * sizeof(const char*));

                    params_iter = rest;
                    skip_next = false;
                    size_t type_idx = 0;
                    while (!omni_is_nil(params_iter) && omni_is_cell(params_iter) && type_idx < param_count) {
                        if (omni_is_nil(omni_cdr(params_iter))) break; /* Last element is body */
                        OmniValue* item = omni_car(params_iter);
                        if (skip_next) {
                            skip_next = false;
                        } else if (is_metadata_key(item)) {
                            skip_next = true;
                        } else if (omni_is_type_lit(item)) {
                            /* Skip return type annotations */
                        } else {
                            /* This is a parameter - check for type annotation */
                            OmniValue* type_lit = codegen_extract_param_type(item);
                            if (type_lit) {
                                const char* type_name = codegen_type_name(type_lit);
                                type_names[type_idx] = type_name;
                                has_typed_params = true;
                            }
                            type_idx++;
                        }
                        params_iter = omni_cdr(params_iter);
                    }
                }

                /* Phase 19: Generate unique mangled name for typed methods */
                char* final_c_name;
                if (has_typed_params) {
                    final_c_name = generate_typed_mangled_name(fname->str_val, type_names, (int)param_count);
                    /* Track as typed method for generic function construction */
                    track_typed_method(ctx, fname->str_val, final_c_name, type_names, (int)param_count);
                } else {
                    final_c_name = c_name;
                    c_name = NULL;  /* Prevent double-free */
                }

                /* Clean up type_names array (strings are owned by OmniValue, don't free them) */
                free(type_names);

                /* Register symbol and track as static function */
                register_symbol(ctx, fname->str_val, final_c_name);
                track_function_definition(ctx, final_c_name);

                omni_codegen_emit(ctx, "static Obj* %s(Region* _caller_region", final_c_name);

                /* Emit parameters - skip metadata and type annotations
                 * Issue 9 P3: Skip ^:effects and its value
                 */
                params_iter = rest;
                skip_next = false;
                size_t emitted = 0;
                while (!omni_is_nil(params_iter) && omni_is_cell(params_iter) && emitted < param_count) {
                    OmniValue* param = omni_car(params_iter);
                    if (skip_next) {
                        skip_next = false;
                        params_iter = omni_cdr(params_iter);
                        continue;
                    }
                    if (is_metadata_key(param)) {
                        skip_next = true;
                        params_iter = omni_cdr(params_iter);
                        continue;
                    }
                    if (omni_is_type_lit(param)) {
                        /* Skip return type annotations */
                        params_iter = omni_cdr(params_iter);
                        continue;
                    }
                    OmniValue* p_name = codegen_extract_param_name(param);
                    if (p_name) {
                        char* param_c_name = omni_codegen_mangle(p_name->str_val);
                        omni_codegen_emit_raw(ctx, ", Obj* %s", param_c_name);
                        register_symbol(ctx, p_name->str_val, param_c_name);
                        free(param_c_name);
                        emitted++;
                    }
                    params_iter = omni_cdr(params_iter);
                }

                omni_codegen_emit_raw(ctx, ") {\n");
                omni_codegen_indent(ctx);

                /* Region-RC prologue */
                omni_codegen_emit(ctx, "struct Region* _local_region = region_create();\n");
                omni_codegen_emit(ctx, "omni_region_set_lifetime_rank(_local_region, omni_region_get_lifetime_rank(_caller_region) + 1);\n");
                omni_codegen_emit(ctx, "omni_region_set_parent(_local_region, _caller_region);\n");
                omni_codegen_emit(ctx, "region_tether_start(_caller_region);\n\n");

                /* Body - last element */
                OmniValue* body = params_iter;  /* Points to last element */
                OmniValue* last_expr = NULL;
                while (!omni_is_nil(body) && omni_is_cell(body)) {
                    last_expr = omni_car(body);
                    body = omni_cdr(body);
                }

                if (last_expr) {
                    omni_codegen_emit(ctx, "Obj* _result = ");
                    codegen_expr(ctx, last_expr);
                    omni_codegen_emit_raw(ctx, ";\n");
                    EscapeRepairStrategy strategy = omni_choose_escape_repair_strategy(ctx, "_result");
                    omni_codegen_escape_repair(ctx, "_result", "_caller_region", strategy);
                    omni_codegen_emit(ctx, "region_exit(_local_region);\n");
                    omni_codegen_emit(ctx, "region_destroy_if_dead(_local_region);\n");
                    omni_codegen_emit(ctx, "region_tether_end(_caller_region);\n");
                    omni_codegen_emit(ctx, "return _result;\n");
                } else {
                    omni_codegen_emit(ctx, "region_exit(_local_region);\n");
                    omni_codegen_emit(ctx, "region_destroy_if_dead(_local_region);\n");
                    omni_codegen_emit(ctx, "region_tether_end(_caller_region);\n");
                    omni_codegen_emit(ctx, "return NOTHING;\n");
                }

                omni_codegen_dedent(ctx);
                omni_codegen_emit(ctx, "}\n\n");
                free(final_c_name);
                return;
            }
        }

        /* OLD ARRAY SYNTAX REMOVED: (define name [args] body...) is no longer supported.
         * Use slot syntax: (define name [x] [y] body) or traditional: (define (name x y) body)
         * This block was ~220 lines of dead code that handled the deprecated syntax.
         */

        /* Variable define: (define name value) or (define name {Type} value)
         * Issue 1 P2: Global variables are declared at file scope during first pass.
         * Here in main pass we only emit the assignment.
         */
        char* c_name = omni_codegen_mangle(first->str_val);

        /* Check if this is a global (already declared at file scope) or local variable */
        const char* existing = lookup_symbol(ctx, first->str_val);
        bool is_global = (existing != NULL && strcmp(existing, c_name) == 0);

        if (is_global) {
            /* Global variable - only emit assignment, declaration is at file scope */
            omni_codegen_emit(ctx, "%s = ", c_name);
        } else {
            /* Local variable - emit declaration + initialization */
            omni_codegen_emit(ctx, "Obj* %s = ", c_name);
        }

        if (!omni_is_nil(rest)) {
            OmniValue* maybe_type = omni_car(rest);
            if (omni_is_type_lit(maybe_type)) {
                /* Typed variable: (define name {Type} value)
                 * Skip the type annotation, use the value after it */
                OmniValue* value_rest = omni_cdr(rest);
                if (!omni_is_nil(value_rest)) {
                    codegen_expr(ctx, omni_car(value_rest));
                } else {
                    omni_codegen_emit_raw(ctx, "NOTHING");
                }
            } else {
                /* Simple variable: (define name value) */
                codegen_expr(ctx, maybe_type);
            }
        } else {
            omni_codegen_emit_raw(ctx, "NOTHING");
        }
        omni_codegen_emit_raw(ctx, ";\n");

        if (!is_global) {
            register_symbol(ctx, first->str_val, c_name);
        }
        free(c_name);
    } else if (omni_is_cell(first)) {
        /* Scheme-style syntax (define (name params...) body) is NOT supported in OmniLisp.
         * Use Slot syntax instead:
         *   (define name [x] [y] body)          -- Canonical form
         *   (define name x y body)              -- Shorthand form
         *   (define name [x {Int}] {Int} body)  -- Typed form
         */
        OmniValue* fname = omni_car(first);
        const char* fn_name = (omni_is_sym(fname) && fname->str_val) ? fname->str_val : "<unknown>";
        fprintf(stderr, "Error: Scheme-style (define (%s ...) body) syntax is not supported.\n", fn_name);
        fprintf(stderr, "       Use OmniLisp Slot syntax instead:\n");
        fprintf(stderr, "         (define %s [x] [y] body)   ; Canonical\n", fn_name);
        fprintf(stderr, "         (define %s x y body)       ; Shorthand\n", fn_name);
        return;
    }
}

/* ============== New Special Form Implementations ============== */

static void codegen_apply(CodeGenContext* ctx, OmniValue* expr);

/* ============== Pattern Variable Extraction (CP-5.2) ============== */

/*
 * PatternVarList - Compile-time list of pattern variable names
 *
 * Used to extract variable names from patterns so codegen can emit
 * bindings for them in the match clause scope.
 */
#define MAX_PATTERN_VARS 64
typedef struct {
    const char* names[MAX_PATTERN_VARS];
    int count;
} PatternVarList;

/* Initialize pattern var list */
static void pattern_var_list_init(PatternVarList* list) {
    list->count = 0;
}

/* Add a variable name if not already present */
static void pattern_var_list_add(PatternVarList* list, const char* name) {
    if (!name || list->count >= MAX_PATTERN_VARS) return;
    /* Check if already present */
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->names[i], name) == 0) return;
    }
    list->names[list->count++] = name;
}

/* Check if a symbol is a reserved pattern keyword */
static int is_reserved_pattern_sym(const char* sym) {
    return strcmp(sym, "_") == 0 ||
           strcmp(sym, "nil") == 0 ||
           strcmp(sym, "true") == 0 ||
           strcmp(sym, "false") == 0 ||
           strcmp(sym, "as") == 0 ||
           strcmp(sym, "&") == 0 ||
           strcmp(sym, "when") == 0;
}

/*
 * extract_pattern_vars - Recursively collect variable names from a pattern
 *
 * Pattern types:
 * - Symbol: Add to list if not reserved keyword
 * - Array: Recursively process elements, handle 'as' and '&' patterns
 * - List: Recursively process elements
 * - Literals: Ignored (no variables)
 */
static void extract_pattern_vars(OmniValue* pattern, PatternVarList* vars) {
    if (!pattern) return;

    /* Symbol pattern - add if not reserved */
    if (omni_is_sym(pattern)) {
        const char* sym = pattern->str_val;
        if (!is_reserved_pattern_sym(sym)) {
            pattern_var_list_add(vars, sym);
        }
        return;
    }

    /* Array pattern */
    if (omni_is_array(pattern)) {
        size_t len = pattern->array.len;

        /* Check for 'as' pattern: [inner as name] */
        if (len == 3) {
            OmniValue* middle = pattern->array.data[1];
            if (omni_is_sym(middle) && strcmp(middle->str_val, "as") == 0) {
                /* Extract vars from inner pattern */
                extract_pattern_vars(pattern->array.data[0], vars);
                /* Add the 'as' name */
                OmniValue* name = pattern->array.data[2];
                if (omni_is_sym(name) && !is_reserved_pattern_sym(name->str_val)) {
                    pattern_var_list_add(vars, name->str_val);
                }
                return;
            }
        }

        /* Check for rest pattern: [... & rest_var] */
        int rest_pos = -1;
        for (size_t i = 0; i < len; i++) {
            OmniValue* elem = pattern->array.data[i];
            if (omni_is_sym(elem) && strcmp(elem->str_val, "&") == 0) {
                rest_pos = i;
                break;
            }
        }

        if (rest_pos >= 0) {
            /* Extract vars from elements before & */
            for (int i = 0; i < rest_pos; i++) {
                extract_pattern_vars(pattern->array.data[i], vars);
            }
            /* Add rest variable */
            if (rest_pos + 1 < (int)len) {
                OmniValue* rest_var = pattern->array.data[rest_pos + 1];
                if (omni_is_sym(rest_var) && !is_reserved_pattern_sym(rest_var->str_val)) {
                    pattern_var_list_add(vars, rest_var->str_val);
                }
            }
            return;
        }

        /* Regular array - extract from all elements */
        for (size_t i = 0; i < len; i++) {
            extract_pattern_vars(pattern->array.data[i], vars);
        }
        return;
    }

    /* List pattern - extract from all elements */
    if (omni_is_cell(pattern)) {
        OmniValue* curr = pattern;
        while (omni_is_cell(curr)) {
            extract_pattern_vars(omni_car(curr), vars);
            curr = omni_cdr(curr);
        }
        return;
    }

    /* Literals - no variables */
}

/* Check if pattern has any binding variables */
static int pattern_has_bindings(OmniValue* pattern) {
    PatternVarList vars;
    pattern_var_list_init(&vars);
    extract_pattern_vars(pattern, &vars);
    return vars.count > 0;
}

/* ============== End Pattern Variable Extraction ============== */

/* Pattern matching: (match expr pattern1 result1 pattern2 result2 ... else-result) */
static void codegen_match(CodeGenContext* ctx, OmniValue* expr) {
    /* (match value (Some x) x (None) default) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    /* Get the value to match on */
    OmniValue* value_expr = omni_car(args);
    OmniValue* clauses = omni_cdr(args);

    /*
     * Phase 26: Binary Boolean Match Optimization
     *
     * Detect the pattern: (match cond true then-expr false else-expr)
     * This is the desugared form of (if cond then-expr else-expr)
     *
     * When detected, emit optimized branchless ternary operator.
     * This is faster than generic pattern matching.
     */
    if (omni_is_cell(clauses)) {
        OmniValue* first_clause = omni_car(clauses);
        OmniValue* rest = omni_cdr(clauses);

        if (omni_is_cell(rest)) {
            OmniValue* second_clause = omni_car(rest);

            /* Check if this is a binary boolean match: (true X) (false Y) */
            if (omni_is_cell(first_clause) && omni_is_cell(second_clause)) {
                OmniValue* first_pattern = omni_car(first_clause);
                OmniValue* first_result = omni_car(omni_cdr(first_clause));
                OmniValue* second_pattern = omni_car(second_clause);
                OmniValue* second_result = omni_car(omni_cdr(second_clause));

                /* Check for (true expr1) (false expr2) pattern */
                bool is_true_false_match =
                    omni_is_sym(first_pattern) && strcmp(first_pattern->str_val, "true") == 0 &&
                    omni_is_sym(second_pattern) && strcmp(second_pattern->str_val, "false") == 0;

                /* Check for (false expr1) (true expr2) pattern (reversed) */
                bool is_false_true_match =
                    omni_is_sym(first_pattern) && strcmp(first_pattern->str_val, "false") == 0 &&
                    omni_is_sym(second_pattern) && strcmp(second_pattern->str_val, "true") == 0;

                if (is_true_false_match) {
                    /* Emit optimized ternary: cond ? then-expr : else-expr */
                    omni_codegen_emit_raw(ctx, "({ /* binary boolean match (optimized) */\n");
                    omni_codegen_indent(ctx);
                    omni_codegen_emit_raw(ctx, "Obj* _match_result = is_truthy(");
                    codegen_expr(ctx, value_expr);
                    omni_codegen_emit_raw(ctx, ") ? (");
                    codegen_expr(ctx, first_result);
                    omni_codegen_emit_raw(ctx, ") : (");
                    codegen_expr(ctx, second_result);
                    omni_codegen_emit_raw(ctx, ");\n");
                    omni_codegen_emit(ctx, "_match_result;\n");
                    omni_codegen_dedent(ctx);
                    omni_codegen_emit_raw(ctx, "})\n");
                    return;
                }

                if (is_false_true_match) {
                    /* Emit reversed ternary: cond ? else-expr : then-expr */
                    omni_codegen_emit_raw(ctx, "({ /* binary boolean match (reversed, optimized) */\n");
                    omni_codegen_indent(ctx);
                    omni_codegen_emit_raw(ctx, "Obj* _match_result = is_truthy(");
                    codegen_expr(ctx, value_expr);
                    omni_codegen_emit_raw(ctx, ") ? (");
                    codegen_expr(ctx, second_result);
                    omni_codegen_emit_raw(ctx, ") : (");
                    codegen_expr(ctx, first_result);
                    omni_codegen_emit_raw(ctx, ");\n");
                    omni_codegen_emit(ctx, "_match_result;\n");
                    omni_codegen_dedent(ctx);
                    omni_codegen_emit_raw(ctx, "})\n");
                    return;
                }
            }
        }
    }

    /* Emit the value expression */
    omni_codegen_emit(ctx, "({ /* match */\n");
    omni_codegen_indent(ctx);
    omni_codegen_emit(ctx, "Obj* _match_value = ");
    codegen_expr(ctx, value_expr);
    omni_codegen_emit_raw(ctx, ";\n");
    /* Declare result variable, initialize to NIL in case nothing matches */
    omni_codegen_emit(ctx, "Obj* _result = NIL;\n");
    omni_codegen_emit_raw(ctx, "\n");

    /*
     * Match clause processing - supports two syntaxes:
     *
     * NEW (array-based clauses):
     *   (match value [pattern result] [pattern :when guard result] ...)
     *   - [pattern result]           -> 2 elements
     *   - [pattern :when guard result] -> 4 elements
     *
     * LEGACY (alternating pairs):
     *   (match value pattern1 result1 pattern2 result2 ...)
     *   - Backward compatibility for existing code
     */
    size_t clauses_processed = 0;
    OmniValue* c = clauses;

    while (!omni_is_nil(c) && omni_is_cell(c)) {
        OmniValue* clause_or_pattern = omni_car(c);
        c = omni_cdr(c);

        OmniValue* pattern = NULL;
        OmniValue* guard_expr = NULL;
        OmniValue* result_expr = NULL;

        /*
         * Detect clause format:
         * - If array with 2+ elements containing result -> new array-based clause
         * - If list (cell) with 2+ elements -> list-based clause (pattern result)
         * - Otherwise -> legacy alternating pattern-result pairs
         */
        if (omni_is_array(clause_or_pattern) && clause_or_pattern->array.len >= 2) {
            /*
             * Array-based clause: [pattern result] or [pattern :when guard result]
             *
             * Format 1: [pattern result]
             *   array[0] = pattern
             *   array[1] = result
             *
             * Format 2: [pattern :when guard result]
             *   array[0] = pattern
             *   array[1] = :when (keyword)
             *   array[2] = guard expression
             *   array[3] = result
             */
            OmniValue* arr = clause_or_pattern;

            /* Check for :when guard syntax at position 1 */
            if (arr->array.len >= 4) {
                OmniValue* maybe_when = arr->array.data[1];
                /* Check for :when keyword (quoted symbol 'when) */
                bool is_when_keyword = false;

                if (omni_is_sym(maybe_when) && strcmp(maybe_when->str_val, "when") == 0) {
                    is_when_keyword = true;
                }
                /* Also check for (quote when) form */
                if (omni_is_cell(maybe_when)) {
                    OmniValue* head = omni_car(maybe_when);
                    if (omni_is_sym(head) && strcmp(head->str_val, "quote") == 0) {
                        OmniValue* quoted = omni_car(omni_cdr(maybe_when));
                        if (omni_is_sym(quoted) && strcmp(quoted->str_val, "when") == 0) {
                            is_when_keyword = true;
                        }
                    }
                }

                if (is_when_keyword) {
                    /* [pattern :when guard result] */
                    pattern = arr->array.data[0];
                    guard_expr = arr->array.data[2];
                    result_expr = arr->array.data[3];
                } else {
                    /* [pattern result] - first two elements */
                    pattern = arr->array.data[0];
                    result_expr = arr->array.data[1];
                }
            } else {
                /* [pattern result] */
                pattern = arr->array.data[0];
                result_expr = arr->array.data[1];
            }
        } else if (omni_is_cell(clause_or_pattern)) {
            /*
             * List-based clause: (pattern result) or (pattern :when guard result)
             * Common syntax: (match value (pat1 res1) (pat2 res2) ...)
             */
            OmniValue* clause_list = clause_or_pattern;
            pattern = omni_car(clause_list);
            OmniValue* rest = omni_cdr(clause_list);

            /* Check for :when guard syntax: (pattern :when guard result) */
            if (!omni_is_nil(rest) && omni_is_cell(rest)) {
                OmniValue* second = omni_car(rest);
                bool is_when_keyword = false;

                if (omni_is_sym(second) && strcmp(second->str_val, "when") == 0) {
                    is_when_keyword = true;
                }
                /* Also check for (quote when) form */
                if (omni_is_cell(second)) {
                    OmniValue* head = omni_car(second);
                    if (omni_is_sym(head) && strcmp(head->str_val, "quote") == 0) {
                        OmniValue* quoted = omni_car(omni_cdr(second));
                        if (omni_is_sym(quoted) && strcmp(quoted->str_val, "when") == 0) {
                            is_when_keyword = true;
                        }
                    }
                }

                if (is_when_keyword) {
                    /* (pattern :when guard result) */
                    OmniValue* guard_rest = omni_cdr(rest);
                    if (!omni_is_nil(guard_rest) && omni_is_cell(guard_rest)) {
                        guard_expr = omni_car(guard_rest);
                        OmniValue* result_rest = omni_cdr(guard_rest);
                        if (!omni_is_nil(result_rest) && omni_is_cell(result_rest)) {
                            result_expr = omni_car(result_rest);
                        }
                    }
                } else {
                    /* (pattern result) - second element is the result */
                    result_expr = second;
                }
            }
        } else {
            /*
             * Legacy alternating pairs: pattern result pattern result ...
             * clause_or_pattern is the pattern, next element is result
             */
            pattern = clause_or_pattern;
            if (!omni_is_nil(c) && omni_is_cell(c)) {
                result_expr = omni_car(c);
                c = omni_cdr(c);
            } else {
                /* Odd number of elements - skip this malformed clause */
                continue;
            }

            /* Legacy guard detection: [pattern-elements & guard-expr] */
            if (omni_is_array(pattern)) {
                for (size_t i = 0; i < pattern->array.len; i++) {
                    OmniValue* elem = pattern->array.data[i];
                    if (omni_is_sym(elem) && strcmp(elem->str_val, "&") == 0) {
                        if (i + 1 < pattern->array.len) {
                            guard_expr = pattern->array.data[i + 1];
                        }
                        break;
                    }
                }
            }
        }

        if (!pattern || !result_expr) continue;

        /* Emit pattern comment */
        if (guard_expr) {
            omni_codegen_emit(ctx, "/* pattern: ");
            codegen_expr(ctx, pattern);
            omni_codegen_emit_raw(ctx, " :when ");
            codegen_expr(ctx, guard_expr);
            omni_codegen_emit_raw(ctx, " */\n");
        } else {
            omni_codegen_emit(ctx, "/* pattern: ");
            codegen_expr(ctx, pattern);
            omni_codegen_emit_raw(ctx, " */\n");
        }

        /* Emit else for non-first clauses */
        if (clauses_processed > 0) {
            omni_codegen_emit_raw(ctx, "else ");
        }

        /* Handle wildcard pattern */
        if (omni_is_sym(pattern) && strcmp(pattern->str_val, "_") == 0) {
            omni_codegen_emit(ctx, "{\n");
            omni_codegen_indent(ctx);
            omni_codegen_emit(ctx, "/* wildcard */\n");
            omni_codegen_emit(ctx, "_result = ");
            codegen_expr(ctx, result_expr);
            omni_codegen_emit_raw(ctx, ";\n");
            omni_codegen_dedent(ctx);
            omni_codegen_emit_raw(ctx, "}\n");
        } else {
            /* Extract pattern variables */
            PatternVarList vars;
            pattern_var_list_init(&vars);
            extract_pattern_vars(pattern, &vars);

            if (vars.count > 0) {
                /*
                 * Pattern has bindings - use prim_pattern_match
                 * This returns a dict of {name: value} or NULL if no match
                 */
                omni_codegen_emit(ctx, "{\n");
                omni_codegen_indent(ctx);
                omni_codegen_emit(ctx, "Obj* _bindings = prim_pattern_match(");
                codegen_expr(ctx, pattern);
                omni_codegen_emit_raw(ctx, ", _match_value);\n");

                omni_codegen_emit(ctx, "if (_bindings");

                /* Add guard condition if present */
                if (guard_expr) {
                    omni_codegen_emit_raw(ctx, " && is_truthy(");
                    codegen_expr(ctx, guard_expr);
                    omni_codegen_emit_raw(ctx, ")");
                }

                omni_codegen_emit_raw(ctx, ") {\n");
                omni_codegen_indent(ctx);

                /* Emit variable bindings from dict using name-based lookup */
                for (int i = 0; i < vars.count; i++) {
                    omni_codegen_emit(ctx, "Obj* %s = dict_get_by_name(_bindings, \"%s\");\n",
                                      vars.names[i], vars.names[i]);
                }

                omni_codegen_emit(ctx, "_result = ");
                codegen_expr(ctx, result_expr);
                omni_codegen_emit_raw(ctx, ";\n");
                omni_codegen_dedent(ctx);
                omni_codegen_emit(ctx, "}\n");
                omni_codegen_dedent(ctx);
                omni_codegen_emit(ctx, "}\n");
            } else {
                /* No bindings - use simple is_pattern_match */
                omni_codegen_emit(ctx, "if (is_pattern_match(");
                codegen_expr(ctx, pattern);
                omni_codegen_emit_raw(ctx, ", _match_value)");

                /* Add guard condition if present */
                if (guard_expr) {
                    omni_codegen_emit_raw(ctx, " && is_truthy(");
                    codegen_expr(ctx, guard_expr);
                    omni_codegen_emit_raw(ctx, ")");
                }

                omni_codegen_emit_raw(ctx, ") {\n");
                omni_codegen_indent(ctx);
                omni_codegen_emit(ctx, "_result = ");
                codegen_expr(ctx, result_expr);
                omni_codegen_emit_raw(ctx, ";\n");
                omni_codegen_dedent(ctx);
                omni_codegen_emit(ctx, "}\n");
            }
        }

        clauses_processed++;
    }

    /* In a statement expression, the last expression becomes the return value */
    omni_codegen_emit(ctx, "_result;  /* match result */\n");
    omni_codegen_dedent(ctx);
    omni_codegen_emit_raw(ctx, "})\n");
}

/* ============================================================
 * Module System Code Generation (Phase 5.3)
 * ============================================================ */

/*
 * Helper to check if value is a quoted symbol with given name.
 *
 * In OmniLisp, :key is syntactic sugar for 'key (quoted symbol).
 * Checks for (quote sym) form. Also accepts plain symbols.
 */
static int is_quoted_symbol(OmniValue* val, const char* name) {
    if (!val) return 0;

    /* Check for plain symbol */
    if (omni_is_sym(val)) {
        const char* str = val->str_val;
        if (!str) return 0;
        if (strcmp(str, name) == 0) return 1;
        return 0;
    }

    /* Check for (quote <name>) form - how :key is parsed */
    if (omni_is_cell(val)) {
        OmniValue* head = omni_car(val);
        if (omni_is_sym(head) && head->str_val && strcmp(head->str_val, "quote") == 0) {
            OmniValue* rest = omni_cdr(val);
            if (omni_is_cell(rest)) {
                OmniValue* quoted_sym = omni_car(rest);
                if (omni_is_sym(quoted_sym) && quoted_sym->str_val) {
                    return strcmp(quoted_sym->str_val, name) == 0;
                }
            }
        }
    }

    return 0;
}

/*
 * Helper to generate array literal from list of symbols
 * Emits: ({Obj* _syms[N] = {...}; Obj* _arr = mk_array(N); ...})
 */
static void codegen_symbol_array(CodeGenContext* ctx, OmniValue* list) {
    /* Count elements */
    int count = 0;
    OmniValue* scan = list;
    while (!omni_is_nil(scan) && omni_is_cell(scan)) {
        count++;
        scan = omni_cdr(scan);
    }

    /* Handle array syntax [a b c] */
    if (!omni_is_cell(list) && omni_is_array(list)) {
        OmniValue** arr = list->array.data;
        count = list->array.len;
        omni_codegen_emit(ctx, "({Obj* _syms[%d] = {", count);
        for (int i = 0; i < count; i++) {
            if (i > 0) omni_codegen_emit_raw(ctx, ", ");
            if (omni_is_sym(arr[i])) {
                omni_codegen_emit(ctx, "mk_sym(\"%s\")", arr[i]->str_val);
            } else {
                omni_codegen_emit_raw(ctx, "NIL");
            }
        }
        omni_codegen_emit(ctx, "}; Obj* _arr = mk_array(%d); ", count);
        omni_codegen_emit(ctx, "for(int _i=0;_i<%d;_i++) array_push(_arr,_syms[_i]); _arr;})", count);
        return;
    }

    /* Handle list syntax (a b c) */
    if (count == 0) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    omni_codegen_emit(ctx, "({Obj* _syms[%d] = {", count);
    scan = list;
    int idx = 0;
    while (!omni_is_nil(scan) && omni_is_cell(scan)) {
        OmniValue* elem = omni_car(scan);
        if (idx > 0) omni_codegen_emit_raw(ctx, ", ");
        if (omni_is_sym(elem)) {
            omni_codegen_emit(ctx, "mk_sym(\"%s\")", elem->str_val);
        } else {
            omni_codegen_emit_raw(ctx, "NIL");
        }
        idx++;
        scan = omni_cdr(scan);
    }
    omni_codegen_emit(ctx, "}; Obj* _arr = mk_array(%d); ", count);
    omni_codegen_emit(ctx, "for(int _i=0;_i<%d;_i++) array_push(_arr,_syms[_i]); _arr;})", count);
}

/*
 * codegen_defmodule - Generate code for module definition
 *
 * Syntax: (defmodule name
 *           (:export sym1 sym2 ...)
 *           (:import module1 module2 ...)
 *           body...)
 *
 * Example:
 *   (defmodule math
 *     (:export add sub mul)
 *     (define (add a b) (+ a b))
 *     (define (sub a b) (- a b))
 *     (define (mul a b) (* a b)))
 *
 * Generates:
 *   ({
 *     prim_module_begin(mk_sym("math"));
 *     // ... body definitions ...
 *     // ... export statements ...
 *     prim_module_end();
 *   })
 */
static void codegen_defmodule(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        fprintf(stderr, "defmodule: missing module name\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    /* Get module name */
    OmniValue* name = omni_car(args);
    if (!omni_is_sym(name)) {
        fprintf(stderr, "defmodule: module name must be a symbol\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }
    const char* module_name = name->str_val;

    /* First pass: find exports and imports */
    OmniValue* exports = NULL;
    OmniValue* imports = NULL;

    OmniValue* scan = omni_cdr(args);
    while (!omni_is_nil(scan) && omni_is_cell(scan)) {
        OmniValue* form = omni_car(scan);

        if (omni_is_cell(form)) {
            OmniValue* head = omni_car(form);
            if (omni_is_sym(head)) {
                const char* head_name = head->str_val;
                if (strcmp(head_name, ":export") == 0 || strcmp(head_name, "export") == 0) {
                    exports = omni_cdr(form);
                }
                if (strcmp(head_name, ":import") == 0 || strcmp(head_name, "import") == 0) {
                    imports = omni_cdr(form);
                }
            }
        }
        scan = omni_cdr(scan);
    }

    /* Generate module block */
    omni_codegen_emit_raw(ctx, "({\n");
    omni_codegen_indent(ctx);

    /* Begin module */
    omni_codegen_emit(ctx, "prim_module_begin(mk_sym(\"%s\"));\n", module_name);

    /* Process imports - handle both simple and advanced forms */
    if (imports) {
        OmniValue* imp = imports;
        while (!omni_is_nil(imp) && omni_is_cell(imp)) {
            OmniValue* import_spec = omni_car(imp);

            if (omni_is_sym(import_spec)) {
                /* Simple symbol: (import module1 module2) */
                omni_codegen_emit(ctx, "prim_import(mk_sym(\"%s\"), NULL);\n",
                                  import_spec->str_val);
            } else if (omni_is_cell(import_spec)) {
                /* Nested form: (import (module :only [...])) */
                OmniValue* mod_name = omni_car(import_spec);
                OmniValue* opts = omni_cdr(import_spec);

                if (!omni_is_sym(mod_name)) {
                    imp = omni_cdr(imp);
                    continue;
                }

                int handled = 0;
                while (!omni_is_nil(opts) && omni_is_cell(opts)) {
                    OmniValue* keyword = omni_car(opts);
                    opts = omni_cdr(opts);

                    if (omni_is_nil(opts)) break;
                    OmniValue* value = omni_car(opts);
                    opts = omni_cdr(opts);

                    if (is_quoted_symbol(keyword, "only")) {
                        omni_codegen_emit(ctx, "prim_import_only(mk_sym(\"%s\"), ",
                                          mod_name->str_val);
                        codegen_symbol_array(ctx, value);
                        omni_codegen_emit_raw(ctx, ");\n");
                        handled = 1;
                        break;
                    } else if (is_quoted_symbol(keyword, "as")) {
                        if (omni_is_sym(value)) {
                            omni_codegen_emit(ctx, "prim_import_as(mk_sym(\"%s\"), mk_sym(\"%s\"));\n",
                                              mod_name->str_val, value->str_val);
                            handled = 1;
                        }
                        break;
                    } else if (is_quoted_symbol(keyword, "except")) {
                        omni_codegen_emit(ctx, "prim_import_except(mk_sym(\"%s\"), ",
                                          mod_name->str_val);
                        codegen_symbol_array(ctx, value);
                        omni_codegen_emit_raw(ctx, ");\n");
                        handled = 1;
                        break;
                    }
                }

                if (!handled) {
                    /* Nested form without options */
                    omni_codegen_emit(ctx, "prim_import(mk_sym(\"%s\"), NULL);\n",
                                      mod_name->str_val);
                }
            }
            imp = omni_cdr(imp);
        }
    }

    /* Second pass: generate body definitions (skip export/import forms) */
    OmniValue* rest = omni_cdr(args);
    while (!omni_is_nil(rest) && omni_is_cell(rest)) {
        OmniValue* form = omni_car(rest);

        /* Skip export/import declarations */
        if (omni_is_cell(form)) {
            OmniValue* head = omni_car(form);
            if (omni_is_sym(head)) {
                const char* head_name = head->str_val;
                if (strcmp(head_name, ":export") == 0 || strcmp(head_name, "export") == 0 ||
                    strcmp(head_name, ":import") == 0 || strcmp(head_name, "import") == 0) {
                    rest = omni_cdr(rest);
                    continue;
                }
            }
        }

        codegen_expr(ctx, form);
        omni_codegen_emit_raw(ctx, ";\n");
        rest = omni_cdr(rest);
    }

    /* Generate exports */
    if (exports) {
        OmniValue* exp = exports;
        while (!omni_is_nil(exp) && omni_is_cell(exp)) {
            OmniValue* export_sym = omni_car(exp);
            if (omni_is_sym(export_sym)) {
                const char* sym_name = export_sym->str_val;
                /* Export the symbol - value is looked up from current scope */
                omni_codegen_emit(ctx, "prim_export(mk_sym(\"%s\"), %s);\n",
                                  sym_name, sym_name);
            }
            exp = omni_cdr(exp);
        }
    }

    /* End module */
    omni_codegen_emit(ctx, "prim_module_end();\n");

    omni_codegen_dedent(ctx);
    omni_codegen_emit_raw(ctx, "})\n");
}

/*
 * codegen_import - Generate code for import statement
 *
 * Syntax variants:
 *   (import module-name)                    -> simple import all
 *   (import module-name :only [a b])        -> selective import
 *   (import module-name :as alias)          -> aliased import
 *   (import module-name :except [x y])      -> exclusion import
 *   (import (module-name :only [a b]))      -> nested syntax
 *
 * Generates appropriate prim_import_* call
 */
static void codegen_import(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        fprintf(stderr, "import: missing module name\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* first = omni_car(args);

    /* Check for nested syntax: (import (module :only [...])) */
    if (omni_is_cell(first)) {
        OmniValue* module_name = omni_car(first);
        OmniValue* rest = omni_cdr(first);

        if (!omni_is_sym(module_name)) {
            fprintf(stderr, "import: nested form must start with module name\n");
            omni_codegen_emit_raw(ctx, "NIL");
            return;
        }

        /* Parse options in nested form */
        while (!omni_is_nil(rest) && omni_is_cell(rest)) {
            OmniValue* keyword = omni_car(rest);
            rest = omni_cdr(rest);

            if (omni_is_nil(rest)) break;
            OmniValue* value = omni_car(rest);
            rest = omni_cdr(rest);

            if (is_quoted_symbol(keyword, "only")) {
                omni_codegen_emit(ctx, "prim_import_only(mk_sym(\"%s\"), ",
                                  module_name->str_val);
                codegen_symbol_array(ctx, value);
                omni_codegen_emit_raw(ctx, ")");
                return;
            } else if (is_quoted_symbol(keyword, "as")) {
                if (omni_is_sym(value)) {
                    omni_codegen_emit(ctx, "prim_import_as(mk_sym(\"%s\"), mk_sym(\"%s\"))",
                                      module_name->str_val, value->str_val);
                } else {
                    fprintf(stderr, "import :as requires symbol alias\n");
                    omni_codegen_emit_raw(ctx, "NIL");
                }
                return;
            } else if (is_quoted_symbol(keyword, "except")) {
                omni_codegen_emit(ctx, "prim_import_except(mk_sym(\"%s\"), ",
                                  module_name->str_val);
                codegen_symbol_array(ctx, value);
                omni_codegen_emit_raw(ctx, ")");
                return;
            }
        }

        /* Simple nested form without options */
        omni_codegen_emit(ctx, "prim_import(mk_sym(\"%s\"), NULL)", module_name->str_val);
        return;
    }

    /* Non-nested syntax: (import module :keyword value ...) */
    OmniValue* module_name = first;
    if (!omni_is_sym(module_name) && !omni_is_string(module_name)) {
        fprintf(stderr, "import: module name must be a symbol or string\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* rest = omni_cdr(args);

    /* Parse keyword options */
    while (!omni_is_nil(rest) && omni_is_cell(rest)) {
        OmniValue* keyword = omni_car(rest);
        rest = omni_cdr(rest);

        if (omni_is_nil(rest)) break;
        OmniValue* value = omni_car(rest);
        rest = omni_cdr(rest);

        if (is_quoted_symbol(keyword, "only")) {
            omni_codegen_emit(ctx, "prim_import_only(mk_sym(\"%s\"), ",
                              module_name->str_val);
            codegen_symbol_array(ctx, value);
            omni_codegen_emit_raw(ctx, ")");
            return;
        } else if (is_quoted_symbol(keyword, "as")) {
            if (omni_is_sym(value)) {
                omni_codegen_emit(ctx, "prim_import_as(mk_sym(\"%s\"), mk_sym(\"%s\"))",
                                  module_name->str_val, value->str_val);
            } else {
                fprintf(stderr, "import :as requires symbol alias\n");
                omni_codegen_emit_raw(ctx, "NIL");
            }
            return;
        } else if (is_quoted_symbol(keyword, "except")) {
            omni_codegen_emit(ctx, "prim_import_except(mk_sym(\"%s\"), ",
                              module_name->str_val);
            codegen_symbol_array(ctx, value);
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
    }

    /* Simple import without options */
    omni_codegen_emit(ctx, "prim_import(mk_sym(\"%s\"), NULL)", module_name->str_val);
}

/*
 * codegen_export - Generate code for export statement
 *
 * Syntax: (export sym value) or (export 'sym value)
 *         (export sym)  ; exports current binding
 *
 * Generates: prim_export(mk_sym("sym"), value)
 */
static void codegen_export(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        fprintf(stderr, "export: missing symbol name\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* sym_name = omni_car(args);

    /* Handle quoted symbols: (export 'sym value) -> (quote sym) */
    if (omni_is_cell(sym_name)) {
        OmniValue* head = omni_car(sym_name);
        if (omni_is_sym(head) && head->str_val && strcmp(head->str_val, "quote") == 0) {
            OmniValue* rest = omni_cdr(sym_name);
            if (omni_is_cell(rest)) {
                sym_name = omni_car(rest);
            }
        }
    }

    if (!omni_is_sym(sym_name)) {
        fprintf(stderr, "export: first argument must be a symbol\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* rest = omni_cdr(args);
    OmniValue* value = omni_is_nil(rest) ? NULL : omni_car(rest);
    if (!value) {
        /* Export current binding */
        omni_codegen_emit(ctx, "prim_export(mk_sym(\"%s\"), %s)",
                          sym_name->str_val, sym_name->str_val);
    } else {
        /* Export with explicit value */
        omni_codegen_emit(ctx, "prim_export(mk_sym(\"%s\"), ",
                          sym_name->str_val);
        codegen_expr(ctx, value);
        omni_codegen_emit_raw(ctx, ")");
    }
}

/*
 * codegen_require - Generate code for require statement
 *
 * Syntax: (require module-name) or (require 'module-name)
 *
 * Generates: prim_require(mk_sym("module-name"))
 */
static void codegen_require(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        fprintf(stderr, "require: missing module name\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* module_name = omni_car(args);

    /* Handle quoted symbols: (require 'module-name) -> (quote module-name) */
    if (omni_is_cell(module_name)) {
        OmniValue* head = omni_car(module_name);
        if (omni_is_sym(head) && head->str_val && strcmp(head->str_val, "quote") == 0) {
            OmniValue* rest = omni_cdr(module_name);
            if (omni_is_cell(rest)) {
                module_name = omni_car(rest);
            }
        }
    }

    if (!omni_is_sym(module_name) && !(omni_is_string(module_name))) {
        fprintf(stderr, "require: module name must be a symbol or string\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    omni_codegen_emit(ctx, "prim_require(mk_sym(\"%s\"))", module_name->str_val);
}

/* ============================================================
 * FFI Code Generation (Phase 0: ccall support)
 * ============================================================ */

/*
 * codegen_ccall - Generate code for foreign function calls
 *
 * Syntax: (ccall "library.so" "function_name" [arg1 {Type1} arg2 {Type2} ...] {ReturnType})
 *
 * Type mappings:
 *   {CInt}     -> int      -> obj_to_cint()    -> mk_int()
 *   {CDouble}  -> double   -> obj_to_cdouble() -> mk_float()
 *   {CString}  -> char*    -> obj_to_cstring() -> mk_string()
 *   {CPtr}     -> void*    -> obj_to_cptr()    -> mk_cptr()
 *   {CSize}    -> size_t   -> obj_to_csize()   -> mk_int()
 *   {Nothing}  -> void     -> N/A              -> NOTHING
 *
 * Generated code pattern:
 *   ({
 *       static void* _ffi_lib_N = NULL;
 *       static void* _ffi_fn_N = NULL;
 *       if (!_ffi_lib_N) {
 *           _ffi_lib_N = dlopen("library.so", RTLD_NOW);
 *           _ffi_fn_N = dlsym(_ffi_lib_N, "function_name");
 *       }
 *       C_type _ffi_arg_0 = obj_to_ctype(arg_expr);
 *       ...
 *       C_return _ffi_result = ((C_return(*)(C_arg_types...))_ffi_fn_N)(args...);
 *       mk_result(_ffi_result);
 *   })
 */
static void codegen_ccall(CodeGenContext* ctx, OmniValue* expr) {
    int ffi_id = ctx->ffi_counter++;

    /* Parse: (ccall "lib" "fn" [args...] {RetType}) */
    OmniValue* rest = omni_cdr(expr);
    if (omni_is_nil(rest)) {
        fprintf(stderr, "ccall: missing library name\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* lib_name = omni_car(rest);
    if (!omni_is_string(lib_name)) {
        fprintf(stderr, "ccall: library name must be a string\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    rest = omni_cdr(rest);
    if (omni_is_nil(rest)) {
        fprintf(stderr, "ccall: missing function name\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* fn_name = omni_car(rest);
    if (!omni_is_string(fn_name)) {
        fprintf(stderr, "ccall: function name must be a string\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    rest = omni_cdr(rest);
    if (omni_is_nil(rest)) {
        fprintf(stderr, "ccall: missing arguments array\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* args_array = omni_car(rest);
    if (!omni_is_array(args_array)) {
        fprintf(stderr, "ccall: arguments must be an array\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    rest = omni_cdr(rest);
    if (omni_is_nil(rest)) {
        fprintf(stderr, "ccall: missing return type\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* ret_type = omni_car(rest);
    if (!omni_is_type_lit(ret_type)) {
        fprintf(stderr, "ccall: return type must be a type literal {Type}\n");
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    /* Parse arguments array: [val1 {Type1} val2 {Type2} ...] */
    size_t arr_len = omni_array_len(args_array);
    size_t arg_count = 0;
    OmniValue** arg_exprs = NULL;
    const char** arg_ctypes = NULL;
    const char** arg_converters = NULL;

    if (arr_len > 0) {
        /* Count argument pairs (value, type) */
        arg_exprs = malloc(sizeof(OmniValue*) * (arr_len / 2 + 1));
        arg_ctypes = malloc(sizeof(char*) * (arr_len / 2 + 1));
        arg_converters = malloc(sizeof(char*) * (arr_len / 2 + 1));

        for (size_t i = 0; i < arr_len; i++) {
            OmniValue* elem = omni_array_get(args_array, i);
            if (omni_is_type_lit(elem)) {
                /* This is a type annotation for previous arg */
                const char* type_name = elem->type_lit.type_name;
                if (strcmp(type_name, "CInt") == 0) {
                    arg_ctypes[arg_count - 1] = "int";
                    arg_converters[arg_count - 1] = "obj_to_cint";
                } else if (strcmp(type_name, "CDouble") == 0) {
                    arg_ctypes[arg_count - 1] = "double";
                    arg_converters[arg_count - 1] = "obj_to_cdouble";
                } else if (strcmp(type_name, "CString") == 0) {
                    arg_ctypes[arg_count - 1] = "const char*";
                    arg_converters[arg_count - 1] = "obj_to_cstring";
                } else if (strcmp(type_name, "CPtr") == 0) {
                    arg_ctypes[arg_count - 1] = "void*";
                    arg_converters[arg_count - 1] = "obj_to_cptr";
                } else if (strcmp(type_name, "CSize") == 0) {
                    arg_ctypes[arg_count - 1] = "size_t";
                    arg_converters[arg_count - 1] = "obj_to_csize";
                } else {
                    /* Default to int */
                    arg_ctypes[arg_count - 1] = "int";
                    arg_converters[arg_count - 1] = "obj_to_cint";
                }
            } else {
                /* This is an argument value */
                arg_exprs[arg_count] = elem;
                arg_ctypes[arg_count] = "int";  /* Default, will be overwritten */
                arg_converters[arg_count] = "obj_to_cint";
                arg_count++;
            }
        }
    }

    /* Determine return type */
    const char* ret_ctype = "int";
    const char* ret_wrapper = "mk_int";
    bool void_return = false;
    const char* ret_type_name = ret_type->type_lit.type_name;

    if (strcmp(ret_type_name, "CInt") == 0) {
        ret_ctype = "int";
        ret_wrapper = "mk_int";
    } else if (strcmp(ret_type_name, "CDouble") == 0) {
        ret_ctype = "double";
        ret_wrapper = "mk_float";
    } else if (strcmp(ret_type_name, "CString") == 0) {
        ret_ctype = "char*";
        ret_wrapper = "mk_string";
    } else if (strcmp(ret_type_name, "CPtr") == 0) {
        ret_ctype = "void*";
        ret_wrapper = "mk_cptr";
    } else if (strcmp(ret_type_name, "CSize") == 0) {
        ret_ctype = "size_t";
        ret_wrapper = "mk_int";
    } else if (strcmp(ret_type_name, "Nothing") == 0) {
        ret_ctype = "void";
        void_return = true;
    }

    /* Generate code */
    omni_codegen_emit_raw(ctx, "({\n");
    omni_codegen_indent(ctx);

    /* Result variable for statement expression */
    omni_codegen_emit(ctx, "Obj* _ffi_expr_%d = NOTHING;\n", ffi_id);

    /* Static library and function handle caching */
    omni_codegen_emit(ctx, "static void* _ffi_lib_%d = NULL;\n", ffi_id);
    omni_codegen_emit(ctx, "static void* _ffi_fn_%d = NULL;\n", ffi_id);
    omni_codegen_emit(ctx, "if (!_ffi_lib_%d) {\n", ffi_id);
    omni_codegen_indent(ctx);
    omni_codegen_emit(ctx, "_ffi_lib_%d = dlopen(\"%s\", RTLD_NOW);\n", ffi_id, lib_name->str_val);
    omni_codegen_emit(ctx, "if (_ffi_lib_%d) _ffi_fn_%d = dlsym(_ffi_lib_%d, \"%s\");\n",
                      ffi_id, ffi_id, ffi_id, fn_name->str_val);
    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "}\n");

    /* Check if library/function loaded - only proceed if successful */
    omni_codegen_emit(ctx, "if (!_ffi_fn_%d) {\n", ffi_id);
    omni_codegen_indent(ctx);
    /* Issue 19: Return proper FFI error condition instead of NOTHING */
    omni_codegen_emit(ctx, "_ffi_expr_%d = mk_ffi_load_error(\"%s\", \"%s\");\n",
                      ffi_id, lib_name->str_val, fn_name->str_val);
    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "} else {\n");
    omni_codegen_indent(ctx);

    /* Convert arguments */
    for (size_t i = 0; i < arg_count; i++) {
        omni_codegen_emit(ctx, "%s _ffi_arg_%d_%zu = %s(",
                          arg_ctypes[i], ffi_id, i, arg_converters[i]);
        codegen_expr(ctx, arg_exprs[i]);
        omni_codegen_emit_raw(ctx, ");\n");
    }

    /* Build function pointer cast */
    omni_codegen_emit(ctx, "");
    if (!void_return) {
        omni_codegen_emit_raw(ctx, "%s _ffi_result_%d = ", ret_ctype, ffi_id);
    }

    /* Cast function pointer: (return_type(*)(arg_types...)) */
    omni_codegen_emit_raw(ctx, "((%s(*)(", ret_ctype);
    for (size_t i = 0; i < arg_count; i++) {
        if (i > 0) omni_codegen_emit_raw(ctx, ", ");
        omni_codegen_emit_raw(ctx, "%s", arg_ctypes[i]);
    }
    if (arg_count == 0) {
        omni_codegen_emit_raw(ctx, "void");
    }
    omni_codegen_emit_raw(ctx, "))_ffi_fn_%d)(", ffi_id);

    /* Call with arguments */
    for (size_t i = 0; i < arg_count; i++) {
        if (i > 0) omni_codegen_emit_raw(ctx, ", ");
        omni_codegen_emit_raw(ctx, "_ffi_arg_%d_%zu", ffi_id, i);
    }
    omni_codegen_emit_raw(ctx, ");\n");

    /* Wrap return value and store in result variable */
    if (void_return) {
        omni_codegen_emit(ctx, "_ffi_expr_%d = NOTHING;\n", ffi_id);
    } else {
        omni_codegen_emit(ctx, "_ffi_expr_%d = %s(_ffi_result_%d);\n", ffi_id, ret_wrapper, ffi_id);
    }

    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "}\n");

    /* Statement expression result */
    omni_codegen_emit(ctx, "_ffi_expr_%d;\n", ffi_id);

    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "})");

    /* Cleanup */
    free(arg_exprs);
    free((void*)arg_ctypes);
    free((void*)arg_converters);
}

/* ============================================================
 * Effect System Code Generation (Phase 22: Algebraic Effects)
 * ============================================================ */

/*
 * codegen_handle - Generate code for effect handler blocks
 *
 * Syntax: (handle body
 *           (EffectName (payload resume) handler-body)
 *           ...
 *           [(return (value) return-body)])
 *
 * Example:
 *   (handle
 *     (perform 'Ask nil)
 *     (Ask (payload resume)
 *       (resume 42)))
 *
 * Generates:
 *   ({
 *     effect_init();
 *     HandlerClause* _clauses = NULL;
 *     // Build clauses from handler lambdas...
 *     handler_push(_clauses, return_clause, env);
 *     Obj* _result = body;
 *     handler_pop();
 *     _result;
 *   })
 */
/*
 * codegen_handler_closure - Generate a static handler function and return closure
 *
 * Handler closures have the signature:
 *   Obj* fn(Region*, Obj** captures, Obj** args, int argc)
 * where args[0] = payload, args[1] = resume (for effect handlers)
 * or args[0] = value (for return handlers)
 *
 * @param ctx        CodeGen context
 * @param params     Parameter list (e.g., (payload resume) or (value))
 * @param body       Handler body expression
 * @param name       Name prefix for the generated function
 * @param arity      Number of parameters (2 for effect handlers, 1 for return)
 */
static void codegen_handler_closure(CodeGenContext* ctx, OmniValue* params,
                                     OmniValue* body, const char* name, int arity) {
    int handler_id = ctx->lambda_counter++;
    char fn_name[64];
    snprintf(fn_name, sizeof(fn_name), "_handler_%s_%d", name, handler_id);

    /* Generate handler body into a temporary buffer */
    CodeGenContext* tmp = omni_codegen_new_buffer();
    tmp->lambda_counter = ctx->lambda_counter;

    /* Copy symbol table */
    for (size_t i = 0; i < ctx->symbols.count; i++) {
        register_symbol(tmp, ctx->symbols.names[i], ctx->symbols.c_names[i]);
    }

    /* Register handler parameters as symbols */
    int param_idx = 0;
    if (omni_is_cell(params)) {
        OmniValue* param_list = params;
        while (!omni_is_nil(param_list) && omni_is_cell(param_list)) {
            OmniValue* param = omni_car(param_list);
            if (omni_is_sym(param)) {
                char c_name[64];
                snprintf(c_name, sizeof(c_name), "_arg_%d", param_idx);
                register_symbol(tmp, param->str_val, c_name);
                /* Mark second param (index 1) as resumption for effect handlers (arity 2) */
                if (param_idx == 1 && arity == 2) {
                    strmap_put(tmp->resumption_symbols, c_name, (void*)1);
                }
            }
            param_list = omni_cdr(param_list);
            param_idx++;
        }
    } else if (omni_is_array(params)) {
        for (size_t i = 0; i < params->array.len; i++) {
            OmniValue* param = params->array.data[i];
            if (omni_is_sym(param)) {
                char c_name[64];
                snprintf(c_name, sizeof(c_name), "_arg_%d", (int)i);
                register_symbol(tmp, param->str_val, c_name);
                /* Mark second param (index 1) as resumption for effect handlers (arity 2) */
                if (i == 1 && arity == 2) {
                    strmap_put(tmp->resumption_symbols, c_name, (void*)1);
                }
            }
            param_idx++;
        }
    }

    /* Generate the body code */
    codegen_expr(tmp, body);

    /* Update lambda counter in main context */
    ctx->lambda_counter = tmp->lambda_counter;

    /* Copy any nested lambda definitions */
    for (size_t i = 0; i < tmp->lambda_defs.count; i++) {
        omni_codegen_add_lambda_def(ctx, tmp->lambda_defs.defs[i]);
    }

    /* Build the static handler function */
    char def[16384];
    char* p = def;
    p += sprintf(p, "static Obj* %s(struct Region* _caller_region, Obj** _captures, Obj** _args, int _argc) {\n", fn_name);
    p += sprintf(p, "    (void)_captures; (void)_argc; (void)_caller_region;\n");

    /* Bind parameters from args array */
    param_idx = 0;
    if (omni_is_cell(params)) {
        OmniValue* param_list = params;
        while (!omni_is_nil(param_list) && omni_is_cell(param_list)) {
            OmniValue* param = omni_car(param_list);
            if (omni_is_sym(param)) {
                p += sprintf(p, "    Obj* _arg_%d = _args[%d];\n", param_idx, param_idx);
            }
            param_list = omni_cdr(param_list);
            param_idx++;
        }
    } else if (omni_is_array(params)) {
        for (size_t i = 0; i < params->array.len; i++) {
            OmniValue* param = params->array.data[i];
            if (omni_is_sym(param)) {
                p += sprintf(p, "    Obj* _arg_%d = _args[%d];\n", (int)i, (int)i);
            }
            param_idx++;
        }
    }

    p += sprintf(p, "    return %s;\n", tmp->output_buffer);
    p += sprintf(p, "}");

    /* Add handler function to lambda definitions */
    omni_codegen_add_lambda_def(ctx, def);

    omni_codegen_free(tmp);

    /* Emit closure creation at call site */
    omni_codegen_emit_raw(ctx, "mk_closure_region(_local_region, %s, NULL, 0, %d)", fn_name, arity);
}

/*
 * codegen_body_thunk - Generate a static function for the handle body
 *
 * Creates a 0-argument closure that evaluates the body expression.
 * This is needed because effect_handle expects a thunk.
 */
static void codegen_body_thunk(CodeGenContext* ctx, OmniValue* body) {
    int thunk_id = ctx->lambda_counter++;
    char fn_name[64];
    snprintf(fn_name, sizeof(fn_name), "_body_thunk_%d", thunk_id);

    /* Generate body into a temporary buffer */
    CodeGenContext* tmp = omni_codegen_new_buffer();
    tmp->lambda_counter = ctx->lambda_counter;

    /* Copy symbol table */
    for (size_t i = 0; i < ctx->symbols.count; i++) {
        register_symbol(tmp, ctx->symbols.names[i], ctx->symbols.c_names[i]);
    }

    /* Generate the body code */
    codegen_expr(tmp, body);

    /* Update lambda counter in main context */
    ctx->lambda_counter = tmp->lambda_counter;

    /* Copy any nested lambda definitions */
    for (size_t i = 0; i < tmp->lambda_defs.count; i++) {
        omni_codegen_add_lambda_def(ctx, tmp->lambda_defs.defs[i]);
    }

    /* Build the static thunk function */
    char def[16384];
    char* p = def;
    p += sprintf(p, "static Obj* %s(struct Region* _caller_region, Obj** _captures, Obj** _args, int _argc) {\n", fn_name);
    p += sprintf(p, "    (void)_captures; (void)_argc; (void)_args;\n");
    p += sprintf(p, "    struct Region* _local_region = _caller_region;\n");  /* Alias for codegen compatibility */
    p += sprintf(p, "    return %s;\n", tmp->output_buffer);
    p += sprintf(p, "}");

    /* Add thunk function to lambda definitions */
    omni_codegen_add_lambda_def(ctx, def);

    omni_codegen_free(tmp);

    /* Emit closure creation at call site */
    omni_codegen_emit_raw(ctx, "mk_closure_region(_local_region, %s, NULL, 0, 0)", fn_name);
}

static void codegen_handle(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    /* First arg is the body to evaluate */
    OmniValue* body = omni_car(args);
    OmniValue* clauses = omni_cdr(args);

    omni_codegen_emit_raw(ctx, "({ /* handle */\n");
    omni_codegen_indent(ctx);

    /* Initialize effect system */
    omni_codegen_emit(ctx, "effect_init();\n");
    omni_codegen_emit(ctx, "HandlerClause* _h_clauses = NULL;\n");
    omni_codegen_emit(ctx, "Obj* _h_return_clause = NULL;\n");
    omni_codegen_emit_raw(ctx, "\n");

    /* Process each handler clause: (EffectName (payload resume) body) */
    while (!omni_is_nil(clauses) && omni_is_cell(clauses)) {
        OmniValue* clause = omni_car(clauses);
        clauses = omni_cdr(clauses);

        if (!omni_is_cell(clause)) continue;

        OmniValue* effect_name = omni_car(clause);
        OmniValue* rest = omni_cdr(clause);

        if (!omni_is_sym(effect_name)) continue;
        const char* name = effect_name->str_val;

        /* Check for return clause: (return (value) body) */
        if (strcmp(name, "return") == 0) {
            /* Return clause takes 1 arg: value */
            OmniValue* params = omni_car(rest);
            OmniValue* body_rest = omni_cdr(rest);
            OmniValue* ret_body = omni_is_cell(body_rest) ? omni_car(body_rest) : body_rest;

            omni_codegen_emit(ctx, "/* return clause */\n");
            omni_codegen_emit(ctx, "_h_return_clause = ");
            codegen_handler_closure(ctx, params, ret_body, "return", 1);
            omni_codegen_emit_raw(ctx, ";\n");
            continue;
        }

        /* Effect clause: (EffectName (payload resume) handler-body) */
        OmniValue* params = omni_car(rest);
        OmniValue* handler_body_list = omni_cdr(rest);
        OmniValue* handler_body = omni_is_cell(handler_body_list) ?
                                  omni_car(handler_body_list) : handler_body_list;

        omni_codegen_emit(ctx, "/* handler for effect: %s */\n", name);
        omni_codegen_emit(ctx, "{\n");
        omni_codegen_indent(ctx);

        /* Find effect type */
        omni_codegen_emit(ctx, "EffectType* _eff_type = effect_type_find(\"%s\");\n", name);
        omni_codegen_emit(ctx, "if (!_eff_type) {\n");
        omni_codegen_indent(ctx);
        omni_codegen_emit(ctx, "_eff_type = effect_type_register(\"%s\", NULL, NULL);\n", name);
        omni_codegen_dedent(ctx);
        omni_codegen_emit(ctx, "}\n");

        /* Build handler closure: takes 2 args (payload, resume) */
        omni_codegen_emit(ctx, "Obj* _handler_fn = ");
        codegen_handler_closure(ctx, params, handler_body, name, 2);
        omni_codegen_emit_raw(ctx, ";\n");

        /* Add clause to list */
        omni_codegen_emit(ctx, "_h_clauses = handler_clause_add(_h_clauses, _eff_type, _handler_fn);\n");

        omni_codegen_dedent(ctx);
        omni_codegen_emit(ctx, "}\n");
    }

    omni_codegen_emit_raw(ctx, "\n");

    /* Generate body as a thunk and call effect_handle */
    omni_codegen_emit(ctx, "Obj* _body_thunk = ");
    codegen_body_thunk(ctx, body);
    omni_codegen_emit_raw(ctx, ";\n");
    omni_codegen_emit(ctx, "Obj* _h_result = effect_handle(_body_thunk, _h_clauses, _h_return_clause, NULL);\n");
    omni_codegen_emit_raw(ctx, "\n");

    /* Return result */
    omni_codegen_emit(ctx, "_h_result;  /* handle result */\n");
    omni_codegen_dedent(ctx);
    omni_codegen_emit_raw(ctx, "})\n");
}

/*
 * codegen_perform - Generate code for performing an effect
 *
 * Syntax: (perform 'EffectName payload)
 * Syntax: (perform effect-type-obj payload)
 *
 * Generates: prim_perform(mk_sym("EffectName"), payload)
 */
static void codegen_perform(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* effect_name = omni_car(args);
    OmniValue* payload = omni_cdr(args);
    OmniValue* payload_expr = omni_is_cell(payload) ? omni_car(payload) : NULL;

    omni_codegen_emit_raw(ctx, "prim_perform(");
    codegen_expr(ctx, effect_name);
    omni_codegen_emit_raw(ctx, ", ");
    if (payload_expr) {
        codegen_expr(ctx, payload_expr);
    } else {
        omni_codegen_emit_raw(ctx, "NIL");
    }
    omni_codegen_emit_raw(ctx, ")");
}

/*
 * codegen_raise - Generate code for raising a Fail effect
 *
 * Syntax: (raise message)
 *
 * Generates: prim_raise(message)
 */
static void codegen_raise(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    OmniValue* message = omni_is_cell(args) ? omni_car(args) : NULL;

    omni_codegen_emit_raw(ctx, "prim_raise(");
    if (message) {
        codegen_expr(ctx, message);
    } else {
        omni_codegen_emit_raw(ctx, "mk_string_cstr(\"error\")");
    }
    omni_codegen_emit_raw(ctx, ")");
}

/*
 * codegen_resume - Generate code for resuming a suspended computation
 *
 * Syntax: (resume resumption value) - explicit resumption object
 * Syntax: (resume value) - within handler, uses bound 'resume' variable
 *
 * Generates: prim_resume(resumption, value)
 *
 * When called with 1 argument inside a handler body, looks up 'resume'
 * in the symbol table to find the bound resumption object.
 */
static void codegen_resume(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* first_arg = omni_car(args);
    OmniValue* rest = omni_cdr(args);
    OmniValue* second_arg = omni_is_cell(rest) ? omni_car(rest) : NULL;

    omni_codegen_emit_raw(ctx, "prim_resume(");

    if (second_arg) {
        /* Two-arg form: (resume resumption value) */
        codegen_expr(ctx, first_arg);
        omni_codegen_emit_raw(ctx, ", ");
        codegen_expr(ctx, second_arg);
    } else {
        /* One-arg form: (resume value) - look up 'resume' in scope */
        const char* resume_var = lookup_symbol(ctx, "resume");
        if (resume_var) {
            /* Found 'resume' bound in scope (e.g., handler parameter) */
            omni_codegen_emit_raw(ctx, "%s, ", resume_var);
            codegen_expr(ctx, first_arg);
        } else {
            /* No 'resume' in scope - treat first_arg as resumption, value as NIL */
            codegen_expr(ctx, first_arg);
            omni_codegen_emit_raw(ctx, ", NIL");
        }
    }

    omni_codegen_emit_raw(ctx, ")");
}

/*
 * codegen_yield - Generate code for yielding a value (generator effect)
 *
 * Syntax: (yield value)
 *
 * Generates: prim_yield(value)
 */
static void codegen_yield(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    OmniValue* value = omni_is_cell(args) ? omni_car(args) : NULL;

    omni_codegen_emit_raw(ctx, "prim_yield(");
    if (value) {
        codegen_expr(ctx, value);
    } else {
        omni_codegen_emit_raw(ctx, "NIL");
    }
    omni_codegen_emit_raw(ctx, ")");
}

/*
 * Phase 5.1: Timer/Timeout Operations
 */

/*
 * codegen_timer_after - Generate code for timer creation
 *
 * Syntax: (timer-after ms)
 * Generates: timer_after(ms)
 *
 * Creates a Promise that resolves after the specified milliseconds.
 */
static void codegen_timer_after(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    OmniValue* ms_expr = omni_is_cell(args) ? omni_car(args) : NULL;

    omni_codegen_emit_raw(ctx, "((Obj*)timer_after(");
    if (ms_expr) {
        /* Convert Obj* to uint64_t */
        omni_codegen_emit_raw(ctx, "(uint64_t)obj_to_int(");
        codegen_expr(ctx, ms_expr);
        omni_codegen_emit_raw(ctx, ")");
    } else {
        omni_codegen_emit_raw(ctx, "0");
    }
    omni_codegen_emit_raw(ctx, "))");
}

/*
 * codegen_await_timeout - Generate code for awaiting with timeout
 *
 * Syntax: (await-timeout promise ms)
 * Generates: await_timeout(promise, ms)
 *
 * Awaits a Promise with a timeout. Returns the result if the promise
 * resolves before the timeout, or NULL if the timeout expires first.
 */
static void codegen_await_timeout(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* promise_expr = omni_car(args);
    args = omni_cdr(args);
    OmniValue* ms_expr = omni_is_cell(args) ? omni_car(args) : NULL;

    omni_codegen_emit_raw(ctx, "await_timeout((Promise*)");
    codegen_expr(ctx, promise_expr);
    omni_codegen_emit_raw(ctx, ", ");
    if (ms_expr) {
        omni_codegen_emit_raw(ctx, "(uint64_t)obj_to_int(");
        codegen_expr(ctx, ms_expr);
        omni_codegen_emit_raw(ctx, ")");
    } else {
        omni_codegen_emit_raw(ctx, "0");
    }
    omni_codegen_emit_raw(ctx, ")");
}

/* Mutation operator: (set! var value) */
static void codegen_set_bang(CodeGenContext* ctx, OmniValue* expr) {
    /* (set! x 10) - modify a binding
     *
     * Issue 1 P2: ESCAPE_GLOBAL detection.
     * When storing to a variable that may be in a longer-lived scope (global),
     * we need to ensure the value survives region destruction.
     *
     * Strategy: Use transmigrate to copy the value to the global region if needed.
     * transmigrate handles the case where src == dst as a no-op.
     *
     * Note: For local variables within the same function, transmigrate is
     * unnecessary overhead, but it's safe and the runtime optimizes same-region
     * transfers to no-ops.
     */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args) || omni_is_nil(omni_cdr(args))) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* var = omni_car(args);
    OmniValue* value = omni_cdr(args);
    if (!omni_is_nil(value)) {
        value = omni_car(value);
    }

    if (!omni_is_sym(var)) {
        codegen_apply(ctx, expr);  /* Fall back to function call */
        return;
    }

    char* c_name = omni_codegen_mangle(var->str_val);

    /* Issue 1 P2: Check if we're inside a function (not at top level).
     * At top level, _local_region IS the global region, so no transmigration needed.
     * Inside functions, we emit transmigrate to ensure global stores are safe.
     *
     * We detect "inside function" by checking if first_pass is false AND
     * we're not in the main codegen pass (indicated by use_runtime being true
     * and being in a function body).
     *
     * For safety, we always emit transmigrate when using runtime - the runtime
     * will optimize same-region transfers to no-ops.
     */
    if (ctx->use_runtime) {
        /* Safe global store: transmigrate to global region if needed */
        omni_codegen_emit_raw(ctx, "(%s = transmigrate(", c_name);
        codegen_expr(ctx, value);
        omni_codegen_emit_raw(ctx, ", _local_region, omni_get_global_region()))");
    } else {
        /* No runtime - simple assignment */
        omni_codegen_emit_raw(ctx, "(%s = ", c_name);
        codegen_expr(ctx, value);
        omni_codegen_emit_raw(ctx, ")");
    }
    free(c_name);
}

/* Mutation operator: (put! obj.field value) */
static void codegen_put_bang(CodeGenContext* ctx, OmniValue* expr) {
    /* (put! obj.field value) - modify a slot/path */
    /* Supports deep path mutation: (put! obj.field.subfield value) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args) || omni_is_nil(omni_cdr(args))) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* path = omni_car(args);
    OmniValue* value = omni_cdr(args);
    if (!omni_is_nil(value)) {
        value = omni_car(value);
    }

    omni_codegen_emit(ctx, "/* put! - deep path mutation */\n");

    /* Check if path is a symbol (e.g., "data.user.address.city") */
    if (omni_is_sym(path)) {
        /* Use runtime helper for deep path mutation */
        char* c_name = omni_codegen_mangle(path->str_val);

        omni_codegen_emit(ctx, "/* Deep path mutation: %s */\n", path->str_val);
        omni_codegen_emit(ctx, "%s = prim_deep_put(%s, \"%s\", ", c_name, c_name, path->str_val);
        codegen_expr(ctx, value);
        omni_codegen_emit_raw(ctx, ");\n");
        omni_codegen_emit(ctx, "return %s;\n", c_name);

        free(c_name);
    } else {
        /* For complex path expressions, fall back to evaluation */
        omni_codegen_emit(ctx, "/* Complex path expression - not yet supported */\n");
        codegen_expr(ctx, path);
        omni_codegen_emit_raw(ctx, ";\n");
        omni_codegen_emit_raw(ctx, "return NIL;");
    }
}

/* Mutation operator: (update! coll key val) - in-place update */
static void codegen_update_bang(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    omni_codegen_emit_raw(ctx, "prim_update_bang(");
    codegen_expr(ctx, omni_car(args));  /* coll */
    omni_codegen_emit_raw(ctx, ", ");

    OmniValue* rest = omni_cdr(args);
    if (!omni_is_nil(rest)) {
        codegen_expr(ctx, omni_car(rest));  /* key */
        omni_codegen_emit_raw(ctx, ", ");
        OmniValue* rest2 = omni_cdr(rest);
        if (!omni_is_nil(rest2)) {
            codegen_expr(ctx, omni_car(rest2));  /* val */
        } else {
            omni_codegen_emit_raw(ctx, "NULL");
        }
    } else {
        omni_codegen_emit_raw(ctx, "NULL, NULL");
    }
    omni_codegen_emit_raw(ctx, ")");
}

/* Functional operator: (update coll key val) - copy-on-write */
static void codegen_update(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    omni_codegen_emit_raw(ctx, "prim_update(");
    codegen_expr(ctx, omni_car(args));  /* coll */
    omni_codegen_emit_raw(ctx, ", ");

    OmniValue* rest = omni_cdr(args);
    if (!omni_is_nil(rest)) {
        codegen_expr(ctx, omni_car(rest));  /* key */
        omni_codegen_emit_raw(ctx, ", ");
        OmniValue* rest2 = omni_cdr(rest);
        if (!omni_is_nil(rest2)) {
            codegen_expr(ctx, omni_car(rest2));  /* val */
        } else {
            omni_codegen_emit_raw(ctx, "NULL");
        }
    } else {
        omni_codegen_emit_raw(ctx, "NULL, NULL");
    }
    omni_codegen_emit_raw(ctx, ")");
}

/* ============== Phase 1: Type Specialization for Binary Ops ============== */

/*
 * Operand type for specialization decisions.
 * Used to determine if we can generate specialized unboxed code.
 */
typedef enum {
    OPTYPE_UNKNOWN,   /* Type not statically known */
    OPTYPE_INT,       /* Known to be integer */
    OPTYPE_FLOAT,     /* Known to be float */
    OPTYPE_BOOL,      /* Known to be boolean */
} OperandType;

/*
 * Determine the static type of an expression.
 * Returns OPTYPE_UNKNOWN if type cannot be determined at compile time.
 */
static OperandType get_operand_type(OmniValue* expr) {
    if (!expr) return OPTYPE_UNKNOWN;

    /* Literal integers */
    if (omni_is_int(expr)) return OPTYPE_INT;

    /* Literal floats */
    if (omni_is_float(expr)) return OPTYPE_FLOAT;

    /* Boolean literals (symbols true/false) */
    if (omni_is_sym(expr)) {
        if (strcmp(expr->str_val, "true") == 0 || strcmp(expr->str_val, "false") == 0) {
            return OPTYPE_BOOL;
        }
    }

    /* For nested arithmetic expressions, recursively check */
    if (omni_is_cell(expr) && !omni_is_nil(expr)) {
        OmniValue* func = omni_car(expr);
        if (omni_is_sym(func)) {
            const char* name = func->str_val;
            /* Arithmetic ops on two ints return int */
            if (strcmp(name, "+") == 0 || strcmp(name, "-") == 0 ||
                strcmp(name, "*") == 0 || strcmp(name, "mod") == 0 ||
                strcmp(name, "%") == 0) {
                OmniValue* args = omni_cdr(expr);
                if (!omni_is_nil(args) && !omni_is_nil(omni_cdr(args))) {
                    OperandType t1 = get_operand_type(omni_car(args));
                    OperandType t2 = get_operand_type(omni_car(omni_cdr(args)));
                    /* Int op Int => Int; Float op anything or anything op Float => Float */
                    if (t1 == OPTYPE_FLOAT || t2 == OPTYPE_FLOAT) return OPTYPE_FLOAT;
                    if (t1 == OPTYPE_INT && t2 == OPTYPE_INT) return OPTYPE_INT;
                }
            }
            /* Division always returns float in OmniLisp */
            if (strcmp(name, "/") == 0) return OPTYPE_FLOAT;
            /* Comparison ops return bool */
            if (strcmp(name, "<") == 0 || strcmp(name, ">") == 0 ||
                strcmp(name, "<=") == 0 || strcmp(name, ">=") == 0 ||
                strcmp(name, "=") == 0 || strcmp(name, "!=") == 0) {
                return OPTYPE_BOOL;
            }
        }
    }

    return OPTYPE_UNKNOWN;
}

/*
 * Get the specialized primitive function name for a binary operation.
 * Returns NULL if no specialization is available.
 */
static const char* get_specialized_binop(const char* op, OperandType t1, OperandType t2) {
    /* Only specialize for Int-Int and Float-Float for now */
    if (t1 == OPTYPE_INT && t2 == OPTYPE_INT) {
        if (strcmp(op, "+") == 0) return "prim_add_Int_Int";
        if (strcmp(op, "-") == 0) return "prim_sub_Int_Int";
        if (strcmp(op, "*") == 0) return "prim_mul_Int_Int";
        if (strcmp(op, "/") == 0) return "prim_div_Int_Int";
        if (strcmp(op, "%") == 0 || strcmp(op, "mod") == 0) return "prim_mod_Int_Int";
        if (strcmp(op, "<") == 0) return "prim_lt_Int_Int";
        if (strcmp(op, ">") == 0) return "prim_gt_Int_Int";
        if (strcmp(op, "<=") == 0) return "prim_le_Int_Int";
        if (strcmp(op, ">=") == 0) return "prim_ge_Int_Int";
        if (strcmp(op, "=") == 0) return "prim_eq_Int_Int";
    }
    if (t1 == OPTYPE_FLOAT && t2 == OPTYPE_FLOAT) {
        if (strcmp(op, "+") == 0) return "prim_add_Float_Float";
        if (strcmp(op, "-") == 0) return "prim_sub_Float_Float";
        if (strcmp(op, "*") == 0) return "prim_mul_Float_Float";
        if (strcmp(op, "/") == 0) return "prim_div_Float_Float";
        if (strcmp(op, "<") == 0) return "prim_lt_Float_Float";
        if (strcmp(op, ">") == 0) return "prim_gt_Float_Float";
        if (strcmp(op, "<=") == 0) return "prim_le_Float_Float";
        if (strcmp(op, ">=") == 0) return "prim_ge_Float_Float";
        if (strcmp(op, "=") == 0) return "prim_eq_Float_Float";
    }
    /* Mixed int-float */
    if (t1 == OPTYPE_INT && t2 == OPTYPE_FLOAT) {
        if (strcmp(op, "+") == 0) return "prim_add_Int_Float";
        if (strcmp(op, "-") == 0) return "prim_sub_Int_Float";
        if (strcmp(op, "*") == 0) return "prim_mul_Int_Float";
        if (strcmp(op, "/") == 0) return "prim_div_Int_Float";
    }
    if (t1 == OPTYPE_FLOAT && t2 == OPTYPE_INT) {
        if (strcmp(op, "+") == 0) return "prim_add_Float_Int";
        if (strcmp(op, "-") == 0) return "prim_sub_Float_Int";
        if (strcmp(op, "*") == 0) return "prim_mul_Float_Int";
        if (strcmp(op, "/") == 0) return "prim_div_Float_Int";
    }
    return NULL;
}

/*
 * Get the boxing function for the result of a specialized operation.
 */
static const char* get_result_boxer(const char* op, OperandType t1, OperandType t2) {
    /* Comparison ops always return bool */
    if (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
        strcmp(op, "=") == 0 || strcmp(op, "!=") == 0) {
        return "box_bool";
    }
    /* Division returns float */
    if (strcmp(op, "/") == 0) {
        return "box_float";
    }
    /* Mixed operations return float */
    if (t1 == OPTYPE_FLOAT || t2 == OPTYPE_FLOAT) {
        return "box_float";
    }
    /* Int operations return int */
    return "box_int";
}

/*
 * Get the unboxing function for an operand type.
 */
static const char* get_unboxer(OperandType t) {
    switch (t) {
        case OPTYPE_INT:   return "unbox_int";
        case OPTYPE_FLOAT: return "unbox_float";
        case OPTYPE_BOOL:  return "unbox_bool";
        default:           return "unbox_int";  /* Default fallback */
    }
}

/*
 * Try to generate specialized code for a binary operation.
 * Returns true if specialization was successful, false otherwise.
 *
 * Specialized pattern:
 *   box_int(prim_add_Int_Int(unbox_int(a), unbox_int(b)))
 */
static bool try_specialized_binop(CodeGenContext* ctx, const char* op,
                                   OmniValue* a, OmniValue* b) {
    OperandType t1 = get_operand_type(a);
    OperandType t2 = get_operand_type(b);

    /* Only specialize if both types are known */
    if (t1 == OPTYPE_UNKNOWN || t2 == OPTYPE_UNKNOWN) {
        return false;
    }

    const char* spec_func = get_specialized_binop(op, t1, t2);
    if (!spec_func) {
        return false;
    }

    const char* boxer = get_result_boxer(op, t1, t2);
    const char* unbox1 = get_unboxer(t1);
    const char* unbox2 = get_unboxer(t2);

    /* Generate: boxer(spec_func(unbox1(a), unbox2(b))) */
    omni_codegen_emit_raw(ctx, "%s(%s(%s(", boxer, spec_func, unbox1);
    codegen_expr(ctx, a);
    omni_codegen_emit_raw(ctx, "), %s(", unbox2);
    codegen_expr(ctx, b);
    omni_codegen_emit_raw(ctx, ")))");

    return true;
}

static void codegen_apply(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* func = omni_car(expr);
    OmniValue* args = omni_cdr(expr);

    /* Check for binary operators */
    if (omni_is_sym(func)) {
        const char* name = func->str_val;
        bool is_binop = (strcmp(name, "+") == 0 || strcmp(name, "-") == 0 ||
                         strcmp(name, "*") == 0 || strcmp(name, "/") == 0 ||
                         strcmp(name, "%") == 0 || strcmp(name, "<") == 0 ||
                         strcmp(name, ">") == 0 || strcmp(name, "<=") == 0 ||
                         strcmp(name, ">=") == 0 || strcmp(name, "=") == 0);

        if (is_binop && !omni_is_nil(args) && !omni_is_nil(omni_cdr(args))) {
            OmniValue* a = omni_car(args);
            OmniValue* b = omni_car(omni_cdr(args));

            /* Phase 1: Try specialized code generation first */
            if (try_specialized_binop(ctx, name, a, b)) {
                return;  /* Specialization succeeded */
            }

            /* Fall back to generic primitive */
            codegen_sym(ctx, func);
            omni_codegen_emit_raw(ctx, "(");
            codegen_expr(ctx, a);
            omni_codegen_emit_raw(ctx, ", ");
            codegen_expr(ctx, b);
            omni_codegen_emit_raw(ctx, ")");
            return;
        }

        /* Check for display/print */
        if (strcmp(name, "display") == 0 || strcmp(name, "print") == 0) {
            omni_codegen_emit_raw(ctx, "(omni_print(");
            if (!omni_is_nil(args)) codegen_expr(ctx, omni_car(args));
            else omni_codegen_emit_raw(ctx, "NIL");
            omni_codegen_emit_raw(ctx, "), NOTHING)");
            return;
        }

        /* Check for range - takes a long argument, not Obj* */
        if (strcmp(name, "range") == 0) {
            omni_codegen_emit_raw(ctx, "prim_range(obj_to_int(");
            if (!omni_is_nil(args)) codegen_expr(ctx, omni_car(args));
            else omni_codegen_emit_raw(ctx, "mk_int(0)");
            omni_codegen_emit_raw(ctx, "))");
            return;
        }

        /* Check for println - variadic print with newline */
        if (strcmp(name, "println") == 0) {
            /* Build argument list for prim_println using nested mk_pair calls */
            omni_codegen_emit_raw(ctx, "(prim_println(");

            /* If no args, pass NULL */
            if (omni_is_nil(args)) {
                omni_codegen_emit_raw(ctx, "NULL");
            } else {
                /* Build list using nested mk_pair calls */
                /* Emit: mk_pair(arg1, mk_pair(arg2, mk_pair(arg3, NULL))) */
                omni_codegen_emit_raw(ctx, "mk_pair(");
                codegen_expr(ctx, omni_car(args));

                OmniValue* current = omni_cdr(args);
                while (!omni_is_nil(current)) {
                    omni_codegen_emit_raw(ctx, ", mk_pair(");
                    codegen_expr(ctx, omni_car(current));
                    current = omni_cdr(current);
                }
                /* Close all the mk_pair calls and end with NULL */
                omni_codegen_emit_raw(ctx, ", NULL");
                /* Close each mk_pair - one for each arg */
                current = args;
                while (!omni_is_nil(current)) {
                    omni_codegen_emit_raw(ctx, ")");
                    current = omni_cdr(current);
                }
            }

            omni_codegen_emit_raw(ctx, "), NOTHING)");
            return;
        }

        if (strcmp(name, "newline") == 0) {
            omni_codegen_emit_raw(ctx, "(printf(\"\\n\"), NOTHING)");
            return;
        }

        /* ============== File I/O Operations ============== */
        if (strcmp(name, "file-read") == 0 || strcmp(name, "slurp") == 0) {
            omni_codegen_emit_raw(ctx, "prim_file_read(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* path */
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "file-write") == 0 || strcmp(name, "spit") == 0) {
            omni_codegen_emit_raw(ctx, "prim_file_write(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* path */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* content */
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "file-append") == 0) {
            omni_codegen_emit_raw(ctx, "prim_file_append(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* path */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* content */
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "file-exists?") == 0 || strcmp(name, "file-exists") == 0) {
            omni_codegen_emit_raw(ctx, "prim_file_exists(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* path */
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "file-delete") == 0) {
            omni_codegen_emit_raw(ctx, "prim_file_delete(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* path */
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "stdin-read-line") == 0 || strcmp(name, "read-line") == 0) {
            omni_codegen_emit_raw(ctx, "prim_stdin_read_line()");
            return;
        }
        if (strcmp(name, "stdout-write") == 0) {
            omni_codegen_emit_raw(ctx, "prim_stdout_write(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* content */
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }

        /* Issue 2 P1: Region-aware higher-order functions
         * These HOFs call closures internally, so they need the caller's region
         * to properly propagate region context to the closure.
         *
         * When the function argument is an inline lambda, we generate a trampoline
         * to adapt the static lambda's signature to ClosureFnRegion.
         */
        if (strcmp(name, "map") == 0) {
            omni_codegen_emit_raw(ctx, "list_map_region(_local_region, ");
            if (!omni_is_nil(args)) {
                OmniValue* fn_arg = omni_car(args);
                if (is_lambda_expr(fn_arg)) {
                    codegen_lambda_as_closure(ctx, fn_arg);  /* Generate closure wrapper */
                } else {
                    codegen_expr(ctx, fn_arg);  /* Regular expression (closure obj) */
                }
                omni_codegen_emit_raw(ctx, ", ");
                if (!omni_is_nil(omni_cdr(args))) {
                    codegen_expr(ctx, omni_car(omni_cdr(args)));  /* xs */
                } else {
                    omni_codegen_emit_raw(ctx, "NIL");
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "filter") == 0) {
            omni_codegen_emit_raw(ctx, "list_filter_region(_local_region, ");
            if (!omni_is_nil(args)) {
                OmniValue* fn_arg = omni_car(args);
                if (is_lambda_expr(fn_arg)) {
                    codegen_lambda_as_closure(ctx, fn_arg);  /* Generate closure wrapper */
                } else {
                    codegen_expr(ctx, fn_arg);  /* Regular expression (closure obj) */
                }
                omni_codegen_emit_raw(ctx, ", ");
                if (!omni_is_nil(omni_cdr(args))) {
                    codegen_expr(ctx, omni_car(omni_cdr(args)));  /* xs */
                } else {
                    omni_codegen_emit_raw(ctx, "NIL");
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "take-while") == 0) {
            omni_codegen_emit_raw(ctx, "prim_take_while(");
            if (!omni_is_nil(args)) {
                OmniValue* fn_arg = omni_car(args);
                if (is_lambda_expr(fn_arg)) {
                    codegen_lambda_as_closure(ctx, fn_arg);  /* Generate closure wrapper */
                } else {
                    codegen_expr(ctx, fn_arg);  /* Regular expression (closure obj) */
                }
                omni_codegen_emit_raw(ctx, ", ");
                if (!omni_is_nil(omni_cdr(args))) {
                    codegen_expr(ctx, omni_car(omni_cdr(args)));  /* coll */
                } else {
                    omni_codegen_emit_raw(ctx, "NIL");
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "drop-while") == 0) {
            omni_codegen_emit_raw(ctx, "prim_drop_while(");
            if (!omni_is_nil(args)) {
                OmniValue* fn_arg = omni_car(args);
                if (is_lambda_expr(fn_arg)) {
                    codegen_lambda_as_closure(ctx, fn_arg);  /* Generate closure wrapper */
                } else {
                    codegen_expr(ctx, fn_arg);  /* Regular expression (closure obj) */
                }
                omni_codegen_emit_raw(ctx, ", ");
                if (!omni_is_nil(omni_cdr(args))) {
                    codegen_expr(ctx, omni_car(omni_cdr(args)));  /* coll */
                } else {
                    omni_codegen_emit_raw(ctx, "NIL");
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "fold") == 0 || strcmp(name, "foldl") == 0) {
            omni_codegen_emit_raw(ctx, "list_fold_region(_local_region, ");
            if (!omni_is_nil(args)) {
                OmniValue* fn_arg = omni_car(args);
                if (is_lambda_expr(fn_arg)) {
                    codegen_lambda_as_closure(ctx, fn_arg);  /* Generate closure wrapper */
                } else {
                    codegen_expr(ctx, fn_arg);  /* Regular expression (closure obj) */
                }
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* init */
                    omni_codegen_emit_raw(ctx, ", ");
                    OmniValue* rest2 = omni_cdr(rest);
                    if (!omni_is_nil(rest2)) {
                        codegen_expr(ctx, omni_car(rest2));  /* xs */
                    } else {
                        omni_codegen_emit_raw(ctx, "NIL");
                    }
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "foldr") == 0) {
            omni_codegen_emit_raw(ctx, "list_foldr_region(_local_region, ");
            if (!omni_is_nil(args)) {
                OmniValue* fn_arg = omni_car(args);
                if (is_lambda_expr(fn_arg)) {
                    codegen_lambda_as_closure(ctx, fn_arg);  /* Generate closure wrapper */
                } else {
                    codegen_expr(ctx, fn_arg);  /* Regular expression (closure obj) */
                }
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* init */
                    omni_codegen_emit_raw(ctx, ", ");
                    OmniValue* rest2 = omni_cdr(rest);
                    if (!omni_is_nil(rest2)) {
                        codegen_expr(ctx, omni_car(rest2));  /* xs */
                    } else {
                        omni_codegen_emit_raw(ctx, "NIL");
                    }
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        /* ============== Array Collection Operations ============== */
        if (strcmp(name, "array-sort") == 0) {
            omni_codegen_emit_raw(ctx, "prim_array_sort(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* arr */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    OmniValue* cmp_arg = omni_car(rest);
                    if (is_lambda_expr(cmp_arg)) {
                        codegen_lambda_as_closure(ctx, cmp_arg);  /* comparator */
                    } else {
                        codegen_expr(ctx, cmp_arg);
                    }
                } else {
                    omni_codegen_emit_raw(ctx, "NULL");  /* default comparator */
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "array-reverse") == 0) {
            omni_codegen_emit_raw(ctx, "prim_array_reverse(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* arr */
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "array-find") == 0) {
            omni_codegen_emit_raw(ctx, "prim_array_find(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* arr */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    OmniValue* pred_arg = omni_car(rest);
                    if (is_lambda_expr(pred_arg)) {
                        codegen_lambda_as_closure(ctx, pred_arg);  /* predicate */
                    } else {
                        codegen_expr(ctx, pred_arg);
                    }
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "array-find-index") == 0) {
            omni_codegen_emit_raw(ctx, "prim_array_find_index(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* arr */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    OmniValue* pred_arg = omni_car(rest);
                    if (is_lambda_expr(pred_arg)) {
                        codegen_lambda_as_closure(ctx, pred_arg);  /* predicate */
                    } else {
                        codegen_expr(ctx, pred_arg);
                    }
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "array-copy") == 0) {
            omni_codegen_emit_raw(ctx, "prim_array_copy(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* arr */
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        /* ============== Dict Collection Operations ============== */
        if (strcmp(name, "dict-keys") == 0) {
            omni_codegen_emit_raw(ctx, "dict_keys(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* dict */
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "dict-values") == 0) {
            omni_codegen_emit_raw(ctx, "dict_values(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* dict */
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "dict-entries") == 0) {
            omni_codegen_emit_raw(ctx, "dict_entries(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* dict */
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "dict-merge") == 0) {
            omni_codegen_emit_raw(ctx, "dict_merge(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* base */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* overlay */
                } else {
                    omni_codegen_emit_raw(ctx, "NULL");
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "dict-has-key?") == 0 || strcmp(name, "dict-has-key") == 0) {
            omni_codegen_emit_raw(ctx, "dict_has_key(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* dict */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* key */
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "dict-remove") == 0) {
            omni_codegen_emit_raw(ctx, "dict_remove(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* dict */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* key */
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "dict-copy") == 0) {
            omni_codegen_emit_raw(ctx, "prim_dict_copy(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* dict */
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        /* ============== Update Operators (Phase 4.2) ============== */
        if (strcmp(name, "update") == 0) {
            omni_codegen_emit_raw(ctx, "prim_update(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* coll */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* key */
                    omni_codegen_emit_raw(ctx, ", ");
                    OmniValue* rest2 = omni_cdr(rest);
                    if (!omni_is_nil(rest2)) {
                        codegen_expr(ctx, omni_car(rest2));  /* val */
                    } else {
                        omni_codegen_emit_raw(ctx, "NULL");
                    }
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "update!") == 0) {
            omni_codegen_emit_raw(ctx, "prim_update_bang(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* coll */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* key */
                    omni_codegen_emit_raw(ctx, ", ");
                    OmniValue* rest2 = omni_cdr(rest);
                    if (!omni_is_nil(rest2)) {
                        codegen_expr(ctx, omni_car(rest2));  /* val */
                    } else {
                        omni_codegen_emit_raw(ctx, "NULL");
                    }
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "update-in") == 0) {
            omni_codegen_emit_raw(ctx, "prim_update_in(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* coll */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* path */
                    omni_codegen_emit_raw(ctx, ", ");
                    OmniValue* rest2 = omni_cdr(rest);
                    if (!omni_is_nil(rest2)) {
                        codegen_expr(ctx, omni_car(rest2));  /* val */
                    } else {
                        omni_codegen_emit_raw(ctx, "NULL");
                    }
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "update-in!") == 0) {
            omni_codegen_emit_raw(ctx, "prim_update_in_bang(");
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* coll */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* path */
                    omni_codegen_emit_raw(ctx, ", ");
                    OmniValue* rest2 = omni_cdr(rest);
                    if (!omni_is_nil(rest2)) {
                        codegen_expr(ctx, omni_car(rest2));  /* val */
                    } else {
                        omni_codegen_emit_raw(ctx, "NULL");
                    }
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
    }

    /* Regular function call with Region-RC support */
    /* Check if function is a user-defined function (needs region parameter)
     *
     * Issue 1 P2: Distinguish between:
     * - Static functions: (define (f x) ...) -> call directly with region param
     * - Closure variables: (define f (fn [x] ...)) -> call via call_closure_region
     *
     * Static functions are tracked via track_function_definition.
     * Closure variables are Obj* values passed to call_closure_region.
     */
    bool is_user_function = false;
    bool is_static_function = false;
    if (omni_is_sym(func)) {
        const char* c_name = lookup_symbol(ctx, func->str_val);
        /* User-defined functions start with "o_" (mangled name) */
        if (c_name && strncmp(c_name, "o_", 2) == 0) {
            is_user_function = true;
            /* Check if this is a static function (tracked via define (f x) form) */
            if (check_function_defined(ctx, c_name) > 0) {
                is_static_function = true;
            }
        }
    }

    /* If it's a lambda call, codegen_lambda already emitted "(func_name(_local_region"
    * We just need to add the arguments and close the parentheses
     */
    if (omni_is_sym(func)) {
        const char* c_name = lookup_symbol(ctx, func->str_val);
        if (c_name && strncmp(c_name, "_lambda_", 8) == 0) {
            /* Lambda call - region already passed, just add arguments */
            while (!omni_is_nil(args) && omni_is_cell(args)) {
                omni_codegen_emit_raw(ctx, ", ");
                codegen_expr(ctx, omni_car(args));
                args = omni_cdr(args);
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        /* Phase 22: Check if calling a resumption (effect handler resume param) */
        if (c_name && ctx->resumption_symbols &&
            strmap_get(ctx->resumption_symbols, c_name)) {
            /* Resumption call: (resume value) -> prim_resume(resume, value) */
            omni_codegen_emit_raw(ctx, "prim_resume(%s, ", c_name);
            if (!omni_is_nil(args) && omni_is_cell(args)) {
                codegen_expr(ctx, omni_car(args));
            } else {
                omni_codegen_emit_raw(ctx, "NIL");
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
    }

    /* Check for inline lambda application: ((lambda ...) args...) */
    if (omni_is_cell(func)) {
        OmniValue* func_head = omni_car(func);
        if (omni_is_sym(func_head)) {
            const char* fn_name = func_head->str_val;
            if (strcmp(fn_name, "lambda") == 0 || strcmp(fn_name, "fn") == 0 ||
                strcmp(fn_name, "λ") == 0) {
                /* Inline lambda application */
                /* codegen_lambda emits _lambda_N(_local_region */
                codegen_expr(ctx, func);
                /* Now add the arguments */
                while (!omni_is_nil(args) && omni_is_cell(args)) {
                    omni_codegen_emit_raw(ctx, ", ");
                    codegen_expr(ctx, omni_car(args));
                    args = omni_cdr(args);
                }
                omni_codegen_emit_raw(ctx, ")");
                return;
            }
        }
    }

        /* Regular function call */

        /* Issue 1 P2: Distinguish static functions from closure variables.
         * - Static functions: (define (f x) ...) -> call directly with region param
         * - Closure variables: (define f (fn [x] ...)) -> call via call_closure_region
         */
        if (is_static_function) {
            /* Phase 19: Check if this is a generic function with multiple dispatch */
            const char* func_name = func->str_val;
            if (is_generic_function(ctx, func_name)) {
                /* Generic function - use omni_generic_invoke for runtime dispatch
                 * Note: The generic global uses the OmniLisp name: _generic_<name>
                 */

                /* Count arguments first */
                int arg_count = 0;
                OmniValue* arg_iter = args;
                while (!omni_is_nil(arg_iter) && omni_is_cell(arg_iter)) {
                    arg_count++;
                    arg_iter = omni_cdr(arg_iter);
                }

                if (arg_count == 0) {
                    omni_codegen_emit_raw(ctx, "omni_generic_invoke(_generic_%s, NULL, 0)", func_name);
                } else {
                    /* Build args array in statement expression */
                    omni_codegen_emit_raw(ctx, "({\n");
                    omni_codegen_emit_raw(ctx, "        Obj* _dispatch_args[%d] = {", arg_count);
                    bool first_a = true;
                    while (!omni_is_nil(args) && omni_is_cell(args)) {
                        if (!first_a) omni_codegen_emit_raw(ctx, ", ");
                        first_a = false;
                        codegen_expr(ctx, omni_car(args));
                        args = omni_cdr(args);
                    }
                    omni_codegen_emit_raw(ctx, "};\n");
                    omni_codegen_emit_raw(ctx, "        omni_generic_invoke(_generic_%s, _dispatch_args, %d);\n", func_name, arg_count);
                    omni_codegen_emit_raw(ctx, "    })");
                }
            } else {
                /* Static function - call directly with region parameter */
                codegen_expr(ctx, func);
                omni_codegen_emit_raw(ctx, "(_local_region");
                while (!omni_is_nil(args) && omni_is_cell(args)) {
                    omni_codegen_emit_raw(ctx, ", ");
                    codegen_expr(ctx, omni_car(args));
                    args = omni_cdr(args);
                }
                omni_codegen_emit_raw(ctx, ")");
            }
        } else if (is_user_function) {
            /* Closure variable - invoke via call_closure_region */
            /* Count arguments */
            int arg_count = 0;
            OmniValue* arg_iter = args;
            while (!omni_is_nil(arg_iter) && omni_is_cell(arg_iter)) {
                arg_count++;
                arg_iter = omni_cdr(arg_iter);
            }

            /* Generate: call_closure_region(_local_region, closure, args_array, count) */
            if (arg_count == 0) {
                omni_codegen_emit_raw(ctx, "call_closure_region(_local_region, ");
                codegen_expr(ctx, func);
                omni_codegen_emit_raw(ctx, ", NULL, 0)");
            } else {
                /* Use a statement expression to build args array */
                omni_codegen_emit_raw(ctx, "({\n");
                omni_codegen_emit_raw(ctx, "        Obj* _closure_args[%d] = {", arg_count);
                bool first_a = true;
                while (!omni_is_nil(args) && omni_is_cell(args)) {
                    if (!first_a) omni_codegen_emit_raw(ctx, ", ");
                    first_a = false;
                    codegen_expr(ctx, omni_car(args));
                    args = omni_cdr(args);
                }
                omni_codegen_emit_raw(ctx, "};\n");
                omni_codegen_emit_raw(ctx, "        call_closure_region(_local_region, ");
                codegen_expr(ctx, func);
                omni_codegen_emit_raw(ctx, ", _closure_args, %d);\n", arg_count);
                omni_codegen_emit_raw(ctx, "    })");
            }
        } else {
            /* Built-in function - direct call, no region parameter */
            codegen_expr(ctx, func);
            omni_codegen_emit_raw(ctx, "(");
            bool first_a = true;
            while (!omni_is_nil(args) && omni_is_cell(args)) {
                if (!first_a) omni_codegen_emit_raw(ctx, ", ");
                first_a = false;
                codegen_expr(ctx, omni_car(args));
                args = omni_cdr(args);
            }
            omni_codegen_emit_raw(ctx, ")");
        }

    }

static void codegen_list(CodeGenContext* ctx, OmniValue* expr) {
    if (omni_is_nil(expr)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* head = omni_car(expr);

    /* Check for special forms */
    if (omni_is_sym(head)) {
        const char* name = head->str_val;

        if (strcmp(name, "quote") == 0) {
            codegen_quote(ctx, expr);
            return;
        }
        if (strcmp(name, "if") == 0) {
            codegen_if(ctx, expr);
            return;
        }
        if (strcmp(name, "let") == 0 || strcmp(name, "let*") == 0) {
            codegen_let(ctx, expr);
            return;
        }
        if (strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0 ||
            strcmp(name, "λ") == 0) {  /* Lambda forms */
            /* Issue 1 P2: Use closure wrapper for lambda-as-value.
             * codegen_lambda emits incomplete call "_lambda_N(_local_region"
             * expecting immediate application. When lambda is used as a value
             * (returned, stored, passed), we need a closure object instead.
             */
            codegen_lambda_as_closure(ctx, expr);
            return;
        }
        if (strcmp(name, "define") == 0) {
            codegen_define(ctx, expr);
            return;
        }
        if (strcmp(name, "match") == 0) {
            /* Pattern matching - generate switch-like structure */
            codegen_match(ctx, expr);
            return;
        }
        /* Mutation operators */
        if (strcmp(name, "set!") == 0) {
            /* Modify a binding */
            codegen_set_bang(ctx, expr);
            return;
        }
        if (strcmp(name, "put!") == 0) {
            /* Modify a slot/path */
            codegen_put_bang(ctx, expr);
            return;
        }
        if (strcmp(name, "update!") == 0) {
            /* Transform in-place */
            codegen_update_bang(ctx, expr);
            return;
        }
        if (strcmp(name, "update") == 0) {
            /* Functional transform */
            codegen_update(ctx, expr);
            return;
        }
        if (strcmp(name, "update-in") == 0) {
            /* Nested path copy-on-write update */
            omni_codegen_emit_raw(ctx, "prim_update_in(");
            OmniValue* args = omni_cdr(expr);
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* coll */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* path */
                    omni_codegen_emit_raw(ctx, ", ");
                    OmniValue* rest2 = omni_cdr(rest);
                    if (!omni_is_nil(rest2)) {
                        codegen_expr(ctx, omni_car(rest2));  /* val */
                    } else {
                        omni_codegen_emit_raw(ctx, "NULL");
                    }
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "update-in!") == 0) {
            /* Nested path in-place update */
            omni_codegen_emit_raw(ctx, "prim_update_in_bang(");
            OmniValue* args = omni_cdr(expr);
            if (!omni_is_nil(args)) {
                codegen_expr(ctx, omni_car(args));  /* coll */
                omni_codegen_emit_raw(ctx, ", ");
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    codegen_expr(ctx, omni_car(rest));  /* path */
                    omni_codegen_emit_raw(ctx, ", ");
                    OmniValue* rest2 = omni_cdr(rest);
                    if (!omni_is_nil(rest2)) {
                        codegen_expr(ctx, omni_car(rest2));  /* val */
                    } else {
                        omni_codegen_emit_raw(ctx, "NULL");
                    }
                }
            }
            omni_codegen_emit_raw(ctx, ")");
            return;
        }
        if (strcmp(name, "do") == 0 || strcmp(name, "begin") == 0) {
            OmniValue* body = omni_cdr(expr);
            omni_codegen_emit_raw(ctx, "({\n");
            omni_codegen_indent(ctx);
            OmniValue* result = NULL;
            while (!omni_is_nil(body) && omni_is_cell(body)) {
                result = omni_car(body);
                body = omni_cdr(body);
                omni_codegen_emit(ctx, "");
                codegen_expr(ctx, result);
                omni_codegen_emit_raw(ctx, ";\n");
            }
            omni_codegen_dedent(ctx);
            omni_codegen_emit(ctx, "})");
            return;
        }
        /* List constructor: (list a b c) -> nested cons cells */
        if (strcmp(name, "list") == 0) {
            OmniValue* args = omni_cdr(expr);
            if (omni_is_nil(args)) {
                omni_codegen_emit_raw(ctx, "NIL");
            } else {
                /* Generate nested cons calls from right to left:
                 * (list a b c) -> prim_cons(a, prim_cons(b, prim_cons(c, NIL)))
                 */
                int depth = 0;
                OmniValue* curr = args;
                while (!omni_is_nil(curr) && omni_is_cell(curr)) {
                    omni_codegen_emit_raw(ctx, "prim_cons(");
                    codegen_expr(ctx, omni_car(curr));
                    omni_codegen_emit_raw(ctx, ", ");
                    curr = omni_cdr(curr);
                    depth++;
                }
                omni_codegen_emit_raw(ctx, "NIL");
                for (int i = 0; i < depth; i++) {
                    omni_codegen_emit_raw(ctx, ")");
                }
            }
            return;
        }
        /* T-wire-fmt-string-01: Format string interpolation */
        if (strcmp(name, "fmt-string") == 0) {
            codegen_fmt_string(ctx, expr);
            return;
        }
        /* T-wire-pika-exec-04: Runtime pattern matching */
        if (strcmp(name, "match-pattern") == 0) {
            codegen_match_pattern(ctx, expr);
            return;
        }
        /* T-wire-pika-compile-04: Pattern compilation */
        if (strcmp(name, "compile-pattern") == 0) {
            codegen_compile_pattern(ctx, expr);
            return;
        }
        /* Phase 22: Effect system special forms */
        if (strcmp(name, "handle") == 0) {
            codegen_handle(ctx, expr);
            return;
        }
        if (strcmp(name, "perform") == 0) {
            codegen_perform(ctx, expr);
            return;
        }
        if (strcmp(name, "raise") == 0) {
            codegen_raise(ctx, expr);
            return;
        }
        if (strcmp(name, "resume") == 0) {
            codegen_resume(ctx, expr);
            return;
        }
        if (strcmp(name, "yield") == 0) {
            codegen_yield(ctx, expr);
            return;
        }
        /* Phase 5.3: Module system special forms */
        if (strcmp(name, "defmodule") == 0 || strcmp(name, "module") == 0) {
            codegen_defmodule(ctx, expr);
            return;
        }
        if (strcmp(name, "import") == 0) {
            codegen_import(ctx, expr);
            return;
        }
        if (strcmp(name, "export") == 0) {
            codegen_export(ctx, expr);
            return;
        }
        if (strcmp(name, "require") == 0) {
            codegen_require(ctx, expr);
            return;
        }
        /* Phase 0: FFI ccall special form */
        if (strcmp(name, "ccall") == 0) {
            codegen_ccall(ctx, expr);
            return;
        }
        /* Phase 5.1: Timer/Timeout operations */
        if (strcmp(name, "timer-after") == 0) {
            codegen_timer_after(ctx, expr);
            return;
        }
        if (strcmp(name, "await-timeout") == 0) {
            codegen_await_timeout(ctx, expr);
            return;
        }
    }

    /* Function application */
    codegen_apply(ctx, expr);
}

static void codegen_expr(CodeGenContext* ctx, OmniValue* expr) {
    if (!expr || omni_is_nil(expr)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    switch (expr->tag) {
    case OMNI_INT:
        codegen_int(ctx, expr);
        break;
    case OMNI_FLOAT:
        codegen_float(ctx, expr);
        break;
    case OMNI_SYM:
        codegen_sym(ctx, expr);
        break;
    case OMNI_STRING:
        codegen_string(ctx, expr);
        break;
    case OMNI_KEYWORD:
        /* Generate keyword: :foo -> mk_keyword("foo") */
        omni_codegen_emit_raw(ctx, "mk_keyword(\"%s\")", expr->str_val ? expr->str_val : "");
        break;
    case OMNI_TYPE_LIT:
        codegen_type_lit(ctx, expr);
        break;
    case OMNI_KIND_SPLICE:
        codegen_kind_splice(ctx, expr);
        break;
    case OMNI_NOTHING:
        omni_codegen_emit_raw(ctx, "NOTHING");
        break;
    case OMNI_CELL:
        codegen_list(ctx, expr);
        break;
    case OMNI_ARRAY:
        codegen_array(ctx, expr);
        break;
    case OMNI_DICT:
        codegen_dict(ctx, expr);
        break;
    case OMNI_SET:
        codegen_set(ctx, expr);
        break;
    default:
        omni_codegen_emit_raw(ctx, "NIL");
        break;
    }
}

/* ============== Phase 19: Multiple Dispatch Generation ============== */

/*
 * Emit wrapper function that adapts static function signature to ClosureFn.
 *
 * Static functions have signature: Obj* func(Region*, Obj*, Obj*, ...)
 * ClosureFn has signature: Obj* func(Obj** captures, Obj** args, int argc)
 *
 * The wrapper unpacks args and calls the static function.
 */
static void emit_method_wrapper(CodeGenContext* ctx, const char* wrapper_name,
                                const char* static_func_name, int param_count) {
    omni_codegen_emit_raw(ctx, "/* Multiple dispatch wrapper for %s */\n", static_func_name);
    omni_codegen_emit_raw(ctx, "static Obj* %s(Obj** captures, Obj** args, int argc) {\n", wrapper_name);
    omni_codegen_emit_raw(ctx, "    (void)captures; (void)argc;\n");
    omni_codegen_emit_raw(ctx, "    return %s(omni_get_global_region()", static_func_name);
    for (int i = 0; i < param_count; i++) {
        omni_codegen_emit_raw(ctx, ", args[%d]", i);
    }
    omni_codegen_emit_raw(ctx, ");\n");
    omni_codegen_emit_raw(ctx, "}\n\n");
}

/*
 * Emit global variables for generic function objects.
 */
static void emit_generic_globals(CodeGenContext* ctx) {
    if (ctx->typed_methods.count == 0) return;

    omni_codegen_emit_raw(ctx, "/* Phase 19: Global generic function objects */\n");

    /* Track which generic names we've seen */
    StrMap* seen = strmap_new();
    for (size_t i = 0; i < ctx->typed_methods.count; i++) {
        const char* omni_name = ctx->typed_methods.omni_names[i];
        if (strmap_get(seen, omni_name)) continue;
        strmap_put(seen, omni_name, (void*)1);

        /* Generate a global variable name */
        char global_name[256];
        snprintf(global_name, sizeof(global_name), "_generic_%s", omni_name);
        omni_codegen_emit_raw(ctx, "static Obj* %s = NULL;\n", global_name);
    }
    strmap_free(seen);
    omni_codegen_emit_raw(ctx, "\n");
}

/*
 * Emit method wrapper functions for all typed methods.
 */
static void emit_method_wrappers(CodeGenContext* ctx) {
    if (ctx->typed_methods.count == 0) return;

    omni_codegen_emit_raw(ctx, "/* Phase 19: Method wrappers for multiple dispatch */\n\n");
    for (size_t i = 0; i < ctx->typed_methods.count; i++) {
        char wrapper_name[256];
        snprintf(wrapper_name, sizeof(wrapper_name), "_wrap_%s", ctx->typed_methods.mangled_names[i]);
        emit_method_wrapper(ctx, wrapper_name,
                           ctx->typed_methods.mangled_names[i],
                           ctx->typed_methods.param_counts[i]);
    }
}

/*
 * Emit __init_generics() function that creates generic functions and registers methods.
 */
static void emit_init_generics(CodeGenContext* ctx) {
    if (ctx->typed_methods.count == 0) return;

    omni_codegen_emit_raw(ctx, "/* Phase 19: Initialize generic functions and register methods */\n");
    omni_codegen_emit_raw(ctx, "static void __init_generics(void) {\n");

    /* Track which generic names we've already created */
    StrMap* created = strmap_new();

    for (size_t i = 0; i < ctx->typed_methods.count; i++) {
        const char* omni_name = ctx->typed_methods.omni_names[i];
        const char* mangled_name = ctx->typed_methods.mangled_names[i];
        int param_count = ctx->typed_methods.param_counts[i];
        char** param_types = ctx->typed_methods.param_types[i];

        /* Create generic function if not already created */
        if (!strmap_get(created, omni_name)) {
            strmap_put(created, omni_name, (void*)1);
            omni_codegen_emit_raw(ctx, "    /* Create generic function: %s */\n", omni_name);
            omni_codegen_emit_raw(ctx, "    _generic_%s = mk_generic(\"%s\");\n", omni_name, omni_name);
        }

        /* Register this method */
        omni_codegen_emit_raw(ctx, "    /* Register method: %s */\n", mangled_name);
        omni_codegen_emit_raw(ctx, "    {\n");

        /* Create Kind objects for parameter types */
        for (int j = 0; j < param_count; j++) {
            const char* type_name = (param_types && param_types[j]) ? param_types[j] : "Any";
            omni_codegen_emit_raw(ctx, "        Obj* _kind_%d = mk_kind(\"%s\", NULL, 0);\n", j, type_name);
        }

        /* Create the kinds array */
        if (param_count > 0) {
            omni_codegen_emit_raw(ctx, "        Obj* _kinds[] = {");
            for (int j = 0; j < param_count; j++) {
                if (j > 0) omni_codegen_emit_raw(ctx, ", ");
                omni_codegen_emit_raw(ctx, "_kind_%d", j);
            }
            omni_codegen_emit_raw(ctx, "};\n");
        }

        /* Register the method with wrapper function */
        omni_codegen_emit_raw(ctx, "        generic_add_method(_generic_%s, %s, %d, (ClosureFn)_wrap_%s);\n",
                            omni_name,
                            param_count > 0 ? "_kinds" : "NULL",
                            param_count,
                            mangled_name);
        omni_codegen_emit_raw(ctx, "    }\n");
    }

    strmap_free(created);
    omni_codegen_emit_raw(ctx, "}\n\n");
}

/*
 * Emit all multiple dispatch support code.
 */
static void emit_multiple_dispatch_support(CodeGenContext* ctx) {
    if (ctx->typed_methods.count == 0) return;

    ctx->has_generics = true;  /* Mark that __init_generics() needs to be called */

    omni_codegen_emit_raw(ctx, "\n/* ============== Phase 19: Multiple Dispatch Support ============== */\n\n");
    emit_generic_globals(ctx);
    emit_method_wrappers(ctx);
    emit_init_generics(ctx);
}

/* ============== Main Generation ============== */

void omni_codegen_expr(CodeGenContext* ctx, OmniValue* expr) {
    codegen_expr(ctx, expr);
}

void omni_codegen_main(CodeGenContext* ctx, OmniValue** exprs, size_t count) {
    omni_codegen_emit(ctx, "int main(void) {\n");
    omni_codegen_indent(ctx);

    /* Region-RC: Create global region for main's scope */
    omni_codegen_emit(ctx, "/* Region-RC: Create global region for main() */\n");
    omni_codegen_emit(ctx, "struct Region* _local_region = region_create();\n");
    /* Issue 2 P4.2: main() has no caller, so rank stays at default 0 (root/global) */
    omni_codegen_emit(ctx, "/* _local_region->lifetime_rank = 0; (already set by region_create) */\n");
    omni_codegen_emit(ctx, "\n");

    /* Initialize runtime type objects */
    omni_codegen_emit(ctx, "/* Initialize type objects for dispatch */\n");
    omni_codegen_emit(ctx, "omni_init_type_objects();\n");
    omni_codegen_emit(ctx, "\n");

    /* Phase 19: Initialize generic functions for multiple dispatch */
    if (ctx->has_generics) {
        omni_codegen_emit(ctx, "/* Initialize generic functions for multiple dispatch */\n");
        omni_codegen_emit(ctx, "__init_generics();\n");
        omni_codegen_emit(ctx, "\n");
    }

    /*
     * Issue 3 P2 (Non-lexical region end, straight-line):
     *
     * Constructive criticism / reality check:
     * This code generator currently routes most allocations through
     * `_local_region` (e.g. `mk_int()` / `mk_cell()` wrappers).
     * That means we cannot safely emit `region_exit(_local_region)` "early"
     * and then keep compiling more top-level expressions that allocate into
     * `_local_region`.
     *
     * The minimal safe form (and the one we can test deterministically) is:
     * - After each top-level expression is evaluated, printed, and freed,
     *   exit+destroy the region and then create a fresh region for the next
     *   expression. This bounds retention without requiring intra-expression
     *   position tracking or new allocation regions.
     *
     * This is still CTRR/CTRR-adjacent behavior: reclamation points are
     * compiler-inserted, deterministic, and do not scan the heap.
     */
    size_t exec_total = 0;
    for (size_t i = 0; i < count; i++) {
        OmniValue* expr = exprs[i];

        if (omni_is_cell(expr) && omni_is_sym(omni_car(expr)) &&
            strcmp(omni_car(expr)->str_val, "define") == 0) {
            /* Defines are not printed, so don't count them */
            continue;
        }
        exec_total++;
    }

    size_t exec_index = 0;
    for (size_t i = 0; i < count; i++) {
        OmniValue* expr = exprs[i];

        /* Check if it's a function define - skip in main (already emitted at file scope) */
        if (is_function_define(expr)) {
            continue;
        }

        /* Check if it's a variable define - emit directly without print wrapper */
        bool is_var_define = omni_is_cell(expr) && omni_is_sym(omni_car(expr)) &&
                             strcmp(omni_car(expr)->str_val, "define") == 0;
        if (is_var_define) {
            /* Variable define - emit the definition statement directly */
            codegen_expr(ctx, expr);
            continue;
        }

        /* Regular expression - emit in main with print wrapper */
        omni_codegen_emit(ctx, "{\n");
        omni_codegen_indent(ctx);
        omni_codegen_emit(ctx, "Obj* _result = ");
        codegen_expr(ctx, expr);
        omni_codegen_emit_raw(ctx, ";\n");
        omni_codegen_emit(ctx, "omni_print(_result);\n");
        omni_codegen_emit(ctx, "printf(\"\\n\");\n");
        omni_codegen_emit(ctx, "free_obj(_result);\n");
        omni_codegen_dedent(ctx);
        omni_codegen_emit(ctx, "}\n");

        exec_index++;
        if (exec_index < exec_total) {
            omni_codegen_emit(ctx,
                "/* ISSUE 3 P2: Non-lexical main() region end */\n");
            omni_codegen_emit(ctx, "region_exit(_local_region);\n");
            omni_codegen_emit(ctx, "region_destroy_if_dead(_local_region);\n");
            omni_codegen_emit(ctx,
                "_local_region = region_create();\n");
            omni_codegen_emit(ctx,
                "/* _local_region->lifetime_rank = 0; (already set by region_create) */\n");
        }
    }

    /* Region-RC: Cleanup global region before exit */
    omni_codegen_emit(ctx, "\n");
    omni_codegen_emit(ctx, "/* Region-RC: Cleanup global region */\n");
    omni_codegen_emit(ctx, "region_exit(_local_region);\n");
    omni_codegen_emit(ctx, "region_destroy_if_dead(_local_region);\n");

    omni_codegen_emit(ctx, "return 0;\n");
    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "}\n");
}

/* Set shared mode for module compilation */
void omni_codegen_set_shared_mode(CodeGenContext* ctx, const char* module_name) {
    ctx->shared_mode = true;
    ctx->module_name = module_name;
}

/*
 * Helper: Extract function name and arity from a function define expression.
 * Returns true if successful, sets *out_name and *out_arity.
 * Caller must NOT free *out_name (points into AST).
 */
static bool extract_function_info(OmniValue* expr, const char** out_name, int* out_arity) {
    if (!is_function_define(expr)) return false;

    OmniValue* args = omni_cdr(expr);
    OmniValue* first = omni_car(args);
    OmniValue* rest = omni_cdr(args);

    if (!omni_is_sym(first)) return false;

    *out_name = first->str_val;

    /* Count parameters - all but last element are parameters (skip metadata and types) */
    int param_count = 0;
    OmniValue* params_iter = rest;
    bool skip_next = false;
    while (!omni_is_nil(params_iter) && omni_is_cell(params_iter)) {
        if (omni_is_nil(omni_cdr(params_iter))) {
            /* Last element - this is the body, not a parameter */
            break;
        }
        OmniValue* item = omni_car(params_iter);
        if (skip_next) {
            skip_next = false;
        } else if (is_metadata_key(item)) {
            skip_next = true;
        } else if (omni_is_type_lit(item)) {
            /* Skip return type annotations */
        } else {
            param_count++;
        }
        params_iter = omni_cdr(params_iter);
    }

    *out_arity = param_count;
    return true;
}

/*
 * Generate module init function for shared library mode.
 *
 * Instead of main(), this generates:
 *   void _omni_module_<name>_init(void)
 *
 * The init function:
 *   - Creates module region
 *   - Evaluates top-level definitions (registers them in module)
 *   - Does NOT print expression results
 *   - Returns exported symbols via module registration
 *   - Wraps functions as closures and exports them
 */
void omni_codegen_module_init(CodeGenContext* ctx, OmniValue** exprs, size_t count) {
    /* First pass: Generate trampoline wrappers for all function defines.
     * These need to be emitted before the module init function so they're available.
     */
    for (size_t i = 0; i < count; i++) {
        const char* fn_name = NULL;
        int arity = 0;
        if (extract_function_info(exprs[i], &fn_name, &arity)) {
            char* c_name = omni_codegen_mangle(fn_name);

            /* Generate trampoline wrapper function.
             * Signature: Obj* _export_<c_name>(Region* r, Obj** captures, Obj** args, int argc)
             * Body: return <c_name>(r, args[0], args[1], ...);
             */
            char tramp_def[2048];
            char* p = tramp_def;
            p += sprintf(p, "/* Export wrapper for %s */\n", fn_name);
            p += sprintf(p, "static Obj* _export_%s(struct Region* _caller_region, Obj** _captures, Obj** _args, int _argc) {\n", c_name);
            p += sprintf(p, "    (void)_captures; (void)_argc;\n");
            p += sprintf(p, "    return %s(_caller_region", c_name);
            for (int j = 0; j < arity; j++) {
                p += sprintf(p, ", _args[%d]", j);
            }
            p += sprintf(p, ");\n");
            p += sprintf(p, "}");

            /* Add forward declaration */
            char fwd_decl[256];
            snprintf(fwd_decl, sizeof(fwd_decl),
                     "static Obj* _export_%s(struct Region*, Obj**, Obj**, int);", c_name);
            omni_codegen_add_forward_decl(ctx, fwd_decl);

            /* Add trampoline to lambda definitions */
            omni_codegen_add_lambda_def(ctx, tramp_def);

            free(c_name);
        }
    }
    /* Generate module init function name */
    omni_codegen_emit(ctx, "/* Module initialization function - called via dlopen */\n");
    omni_codegen_emit(ctx, "__attribute__((constructor))\n");
    omni_codegen_emit(ctx, "void _omni_module_%s_init(void) {\n",
                      ctx->module_name ? ctx->module_name : "anonymous");
    omni_codegen_indent(ctx);

    /* Region-RC: Create module region */
    omni_codegen_emit(ctx, "/* Region-RC: Create region for module scope */\n");
    omni_codegen_emit(ctx, "struct Region* _local_region = region_create();\n");
    omni_codegen_emit(ctx, "\n");

    /* Initialize runtime type objects */
    omni_codegen_emit(ctx, "/* Initialize type objects for dispatch */\n");
    omni_codegen_emit(ctx, "omni_init_type_objects();\n");
    omni_codegen_emit(ctx, "\n");

    /* Phase 19: Initialize generic functions for multiple dispatch */
    if (ctx->has_generics) {
        omni_codegen_emit(ctx, "/* Initialize generic functions for multiple dispatch */\n");
        omni_codegen_emit(ctx, "__init_generics();\n");
        omni_codegen_emit(ctx, "\n");
    }

    /* Emit module registration call */
    omni_codegen_emit(ctx, "/* Register module */\n");
    omni_codegen_emit(ctx, "prim_module_begin(mk_sym(\"%s\"));\n",
                      ctx->module_name ? ctx->module_name : "anonymous");
    omni_codegen_emit(ctx, "\n");

    /* Process expressions - defines only, skip printing */
    for (size_t i = 0; i < count; i++) {
        OmniValue* expr = exprs[i];

        /* Check if it's a function define - skip (already emitted at file scope) */
        if (is_function_define(expr)) {
            continue;
        }

        /* Skip module-related declarations - already handled by module init */
        if (omni_is_cell(expr) && omni_is_sym(omni_car(expr))) {
            const char* head = omni_car(expr)->str_val;
            if (strcmp(head, "module") == 0 || strcmp(head, "defmodule") == 0 ||
                strcmp(head, "module-begin") == 0 || strcmp(head, "module-end") == 0) {
                continue;
            }
        }

        /* Check if it's a variable define - emit the definition */
        bool is_var_define = omni_is_cell(expr) && omni_is_sym(omni_car(expr)) &&
                             strcmp(omni_car(expr)->str_val, "define") == 0;
        if (is_var_define) {
            codegen_expr(ctx, expr);
            continue;
        }

        /* Non-define expressions: evaluate but don't print in module mode */
        omni_codegen_emit(ctx, "{\n");
        omni_codegen_indent(ctx);
        omni_codegen_emit(ctx, "Obj* _result = ");
        codegen_expr(ctx, expr);
        omni_codegen_emit_raw(ctx, ";\n");
        omni_codegen_emit(ctx, "(void)_result; /* Module: discard non-define result */\n");
        omni_codegen_dedent(ctx);
        omni_codegen_emit(ctx, "}\n");
    }

    /* Export all functions as closures */
    omni_codegen_emit(ctx, "\n");
    omni_codegen_emit(ctx, "/* Export functions as closures */\n");
    for (size_t i = 0; i < count; i++) {
        const char* fn_name = NULL;
        int arity = 0;
        if (extract_function_info(exprs[i], &fn_name, &arity)) {
            char* c_name = omni_codegen_mangle(fn_name);
            omni_codegen_emit(ctx, "prim_export(mk_sym(\"%s\"), mk_closure_region(_local_region, _export_%s, NULL, 0, %d));\n",
                              fn_name, c_name, arity);
            free(c_name);
        }
    }

    /* End module definition */
    omni_codegen_emit(ctx, "\n");
    omni_codegen_emit(ctx, "/* End module definition */\n");
    omni_codegen_emit(ctx, "prim_module_end();\n");
    omni_codegen_emit(ctx, "\n");
    omni_codegen_emit(ctx, "/* Module region persists - not destroyed here */\n");
    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "}\n");
}

void omni_codegen_program(CodeGenContext* ctx, OmniValue** exprs, size_t count) {
    /* Initialize analysis */
    ctx->analysis = omni_analysis_new();
    omni_analyze_program(ctx->analysis, exprs, count);

    /* Region-RC: Run region inference pass */
    /* Create a compiler context wrapper for region inference */
    CompilerCtx rcg_ctx = { .analysis = ctx->analysis };
    infer_regions(&rcg_ctx);

    /* Emit runtime header */
    omni_codegen_runtime_header(ctx);

    /* Issue 1 P2: First pass to buffer - collect static functions and forward declarations.
     * We buffer the first pass output so we can emit forward declarations BEFORE
     * static functions. This ensures lambdas referenced in static functions are declared.
     */
    CodeGenContext* first_pass_ctx = omni_codegen_new_buffer();
    first_pass_ctx->analysis = ctx->analysis;
    first_pass_ctx->lambda_counter = ctx->lambda_counter;
    first_pass_ctx->first_pass = true;
    first_pass_ctx->use_runtime = ctx->use_runtime;  /* Issue 1 P2: Inherit runtime flag */

    for (size_t i = 0; i < count; i++) {
        OmniValue* expr = exprs[i];
        if (!omni_is_cell(expr) || !omni_is_sym(omni_car(expr))) continue;

        const char* head = omni_car(expr)->str_val;

        /* Handle defn forms - always function definitions */
        if (strcmp(head, "defn") == 0) {
            codegen_define(first_pass_ctx, expr);
            continue;
        }

        /* Handle define forms */
        if (strcmp(head, "define") == 0) {
            OmniValue* args = omni_cdr(expr);
            OmniValue* name_or_sig = omni_car(args);

            /* Only emit function defines at top level */
            if (omni_is_cell(name_or_sig)) {
                /* Scheme-style (define (f x y) body) is not supported - error in codegen_define */
                codegen_define(first_pass_ctx, expr);
            } else if (omni_is_sym(name_or_sig)) {
                /* Check for slot-syntax: (define f [x] [y] body) or (define f x y body) */
                OmniValue* rest = omni_cdr(args);
                bool is_function = false;
                if (!omni_is_nil(rest)) {
                    OmniValue* maybe_param = omni_car(rest);
                    OmniValue* param_name = codegen_extract_param_name(maybe_param);
                    if (param_name != NULL) {
                        /* This is a slot-syntax function definition */
                        codegen_define(first_pass_ctx, expr);
                        is_function = true;
                    }
                }

                /* Issue 1 P2: Emit global variable declarations at file scope */
                if (!is_function) {
                    /* Simple variable: (define name value) or (define name {Type} value)
                     * Emit declaration at file scope so static functions can access it.
                     * Initialization will happen in main().
                     */
                    char* c_name = omni_codegen_mangle(name_or_sig->str_val);
                    omni_codegen_emit_raw(first_pass_ctx, "static Obj* %s = NIL;\n", c_name);
                    register_symbol(first_pass_ctx, name_or_sig->str_val, c_name);
                    free(c_name);
                }
            }
        }
    }

    /* Copy state from first pass context to main context */
    ctx->lambda_counter = first_pass_ctx->lambda_counter;
    for (size_t i = 0; i < first_pass_ctx->symbols.count; i++) {
        register_symbol(ctx, first_pass_ctx->symbols.names[i], first_pass_ctx->symbols.c_names[i]);
    }
    for (size_t i = 0; i < first_pass_ctx->defined_functions.count; i++) {
        track_function_definition(ctx, first_pass_ctx->defined_functions.names[i]);
    }
    for (size_t i = 0; i < first_pass_ctx->lambda_defs.count; i++) {
        omni_codegen_add_lambda_def(ctx, first_pass_ctx->lambda_defs.defs[i]);
    }
    for (size_t i = 0; i < first_pass_ctx->forward_decls.count; i++) {
        omni_codegen_add_forward_decl(ctx, first_pass_ctx->forward_decls.decls[i]);
    }

    /* Phase 19: Copy typed_methods for multiple dispatch */
    for (size_t i = 0; i < first_pass_ctx->typed_methods.count; i++) {
        track_typed_method(ctx,
                          first_pass_ctx->typed_methods.omni_names[i],
                          first_pass_ctx->typed_methods.mangled_names[i],
                          (const char**)first_pass_ctx->typed_methods.param_types[i],
                          first_pass_ctx->typed_methods.param_counts[i]);
    }
    /* Phase 19: Set has_generics flag based on typed method count */
    ctx->has_generics = (ctx->typed_methods.count > 0);

    /* Get first pass output before freeing */
    char* first_pass_code = omni_codegen_get_output(first_pass_ctx);
    first_pass_ctx->analysis = NULL;  /* Don't free analysis */
    omni_codegen_free(first_pass_ctx);

    ctx->first_pass = false;

    /* Generate main() to a buffer first to collect lambdas */
    CodeGenContext* main_ctx = omni_codegen_new_buffer();
    main_ctx->analysis = ctx->analysis;
    main_ctx->lambda_counter = ctx->lambda_counter;
    /* Copy symbol table */
    for (size_t i = 0; i < ctx->symbols.count; i++) {
        register_symbol(main_ctx, ctx->symbols.names[i], ctx->symbols.c_names[i]);
    }
    /* Issue 1 P2: Copy defined_functions table so static function calls work */
    for (size_t i = 0; i < ctx->defined_functions.count; i++) {
        track_function_definition(main_ctx, ctx->defined_functions.names[i]);
    }
    /* Phase 19: Copy typed_methods for generic dispatch in main() */
    for (size_t i = 0; i < ctx->typed_methods.count; i++) {
        track_typed_method(main_ctx,
                          ctx->typed_methods.omni_names[i],
                          ctx->typed_methods.mangled_names[i],
                          (const char**)ctx->typed_methods.param_types[i],
                          ctx->typed_methods.param_counts[i]);
    }
    /* Phase 19: Copy has_generics flag for __init_generics() call */
    main_ctx->has_generics = ctx->has_generics;

    /* Generate main() or module_init() depending on mode */
    if (ctx->shared_mode) {
        main_ctx->shared_mode = true;
        main_ctx->module_name = ctx->module_name;
        omni_codegen_module_init(main_ctx, exprs, count);
    } else {
        omni_codegen_main(main_ctx, exprs, count);
    }
    char* main_code = omni_codegen_get_output(main_ctx);

    /* Collect lambdas generated during main */
    for (size_t i = 0; i < main_ctx->lambda_defs.count; i++) {
        omni_codegen_add_lambda_def(ctx, main_ctx->lambda_defs.defs[i]);
    }

    /* Don't free analysis from temp context */
    main_ctx->analysis = NULL;
    omni_codegen_free(main_ctx);

    /* Issue 1 P2: Emit in correct order for forward references:
     * 1. Forward declarations (lambda trampolines, etc.)
     * 2. First pass code (global vars + static functions)
     * 3. Lambda definitions
     * 4. Main function
     */

    /* Emit forward declarations */
    for (size_t i = 0; i < ctx->forward_decls.count; i++) {
        omni_codegen_emit_raw(ctx, "%s\n", ctx->forward_decls.decls[i]);
    }
    if (ctx->forward_decls.count > 0) {
        omni_codegen_emit_raw(ctx, "\n");
    }

    /* Emit first pass code (global variables and static functions) */
    if (first_pass_code) {
        omni_codegen_emit_raw(ctx, "%s", first_pass_code);
        free(first_pass_code);
    }

    /* Phase 19: Emit multiple dispatch support (wrappers, generics, init) */
    emit_multiple_dispatch_support(ctx);

    /* Emit lambda definitions */
    for (size_t i = 0; i < ctx->lambda_defs.count; i++) {
        omni_codegen_emit_raw(ctx, "%s\n\n", ctx->lambda_defs.defs[i]);
    }

    /* Emit main function */
    if (main_code) {
        omni_codegen_emit_raw(ctx, "%s", main_code);
        free(main_code);
    }
}

/* ============== ASAP Memory Management ============== */

/* Emit free call using the appropriate ownership-driven strategy */
static void emit_ownership_free(CodeGenContext* ctx, const char* var_name, const char* c_name) {
    if (!ctx->analysis) {
        /* No analysis - fall back to RC-based free */
        omni_codegen_emit(ctx, "free_obj(%s);\n", c_name);
        return;
    }

    FreeStrategy strategy = omni_get_free_strategy(ctx->analysis, var_name);
    const char* strategy_name = omni_free_strategy_name(strategy);

    switch (strategy) {
        case FREE_STRATEGY_NONE:
            /* Don't emit a free - borrowed/transferred */
            omni_codegen_emit(ctx, "/* %s: %s (no free) */\n", c_name, strategy_name);
            break;

        case FREE_STRATEGY_UNIQUE:
            /* Single reference - no RC check needed */
            omni_codegen_emit(ctx, "free_unique(%s); /* %s */\n", c_name, strategy_name);
            break;

        case FREE_STRATEGY_TREE:
            /* Tree-shaped, may have shared children */
            omni_codegen_emit(ctx, "free_tree(%s); /* %s */\n", c_name, strategy_name);
            break;

        case FREE_STRATEGY_RC:
        case FREE_STRATEGY_RC_TREE:
            /* Shared/DAG/cyclic - use RC (with Lobster-style elision check) */
            if (codegen_can_elide_dec(ctx, var_name)) {
                RCElisionClass elision = codegen_get_elision_class(ctx, var_name);
                const char* reason = omni_rc_elision_name(elision);
                omni_codegen_emit(ctx, "/* RC_ELIDED: dec_ref(%s) - %s (%s) */\n",
                                  c_name, reason, strategy_name);
            } else {
                omni_codegen_emit(ctx, "dec_ref(%s); /* %s */\n", c_name, strategy_name);
            }
            break;

        case FREE_STRATEGY_SCC_STATIC:
            /* Static collection of known SCC */
            omni_codegen_emit(ctx, "free_scc_static(%s); /* %s */\n", c_name, strategy_name);
            break;

        case FREE_STRATEGY_COMPONENT_RELEASE:
            /* release_handle for component */
            omni_codegen_emit(ctx, "sym_release_handle(%s->comp); /* %s */\n", c_name, strategy_name);
            break;

        default:
            /* Unknown - conservative RC (with elision check) */
            if (codegen_can_elide_dec(ctx, var_name)) {
                RCElisionClass elision = codegen_get_elision_class(ctx, var_name);
                const char* reason = omni_rc_elision_name(elision);
                omni_codegen_emit(ctx, "/* RC_ELIDED: dec_ref(%s) - %s */\n", c_name, reason);
            } else {
                omni_codegen_emit(ctx, "dec_ref(%s); /* %s */\n", c_name, strategy_name);
            }
            break;
    }
}

void omni_codegen_emit_tethers(CodeGenContext* ctx, int position) {
    if (!ctx->analysis) return;

    /* Check for tether entries */
    size_t tether_count;
    TetherPoint** tethers = omni_get_tethers_at(ctx->analysis, position, &tether_count);
    
    for (size_t i = 0; i < tether_count; i++) {
        TetherPoint* tp = tethers[i];
        if (tp->elided) {
            /* Still emit comment for debugging */
            omni_codegen_emit(ctx, "/* Tether %s elided (handle dominant) */\n", tp->tethered_var);
            continue;
        }
        const char* c_name = lookup_symbol(ctx, tp->tethered_var);
        if (c_name) {
            if (tp->is_entry) {
                omni_codegen_emit(ctx, "SymTetherToken _tether_%s = sym_tether_begin(%s->comp);\n", c_name, c_name);
            } else {
                omni_codegen_emit(ctx, "sym_tether_end(_tether_%s);\n", c_name);
            }
        }
    }
    
    if (tethers) free(tethers);
}

void omni_codegen_emit_frees(CodeGenContext* ctx, int position) {
    if (!ctx->analysis) return;

    /* Issue 1 P2: Emit region releases at last-use positions */
    omni_codegen_emit_region_releases_at_pos(ctx, position);

    size_t count;
    char** vars = omni_get_frees_at(ctx->analysis, position, &count);

    for (size_t i = 0; i < count; i++) {
        const char* c_name = lookup_symbol(ctx, vars[i]);
        if (c_name) {
            emit_ownership_free(ctx, vars[i], c_name);
        }
    }

    free(vars);
}

/* ============== Issue 3 P2: Non-Lexical Region End ============== */

/**
 * omni_codegen_emit_region_exits - Emit region_exit() calls at last-use positions
 *
 * Issue 3 P2: Non-lexical region end for straight-line code.
 *
 * For straight-line code, we compute when all variables in the main region
 * become dead. However, since current codegen doesn't track positions during
 * emission, we add a comment showing where the region could have exited
 * earlier (at last-use position instead of function end).
 *
 * Future phases will extend this to actually emit region_exit() early by
 * tracking position during codegen and inserting exit calls at the right point.
 */
// REVIEWED:NAIVE
void omni_codegen_emit_region_exits(CodeGenContext* ctx, int position) {
    if (!ctx->analysis || !ctx->analysis->var_usages) {
        return;  /* No analysis data */
    }

    /* Compute last-use position of main region (_local_region) */
    int main_region_last_use = -1;
    for (VarUsage* u = ctx->analysis->var_usages; u; u = u->next) {
        /* Only consider variables allocated in main region (region_id = 0) */
        if (u->region_id == 0 && u->last_use > main_region_last_use) {
            main_region_last_use = u->last_use;
        }
    }

    /* For P2 (straight-line only), we emit a comment showing where region could exit */
    /* Actual early exit emission requires position tracking during codegen */
    if (main_region_last_use >= 0 && position >= main_region_last_use) {
        if (!ctx->region_exited) {
            /* Mark that we've processed region exit */
            ctx->region_exited = calloc(1, sizeof(int));
            ctx->region_exited[0] = 1;
            
            /* Note: This comment is emitted at the point where all variables are dead */
            omni_codegen_emit(ctx, 
                "/* ISSUE 3 P2: All region variables dead at pos %d ", 
                main_region_last_use);
            omni_codegen_emit(ctx, "- region could exit here (non-lexical end) */\n");
        }
    }
}

/* ============== CFG-Based Code Generation ============== */

/*
 * Emit tethers for a specific CFG node.
 */
void omni_codegen_emit_cfg_tethers(CodeGenContext* ctx, CFGNode* node) {
    if (!ctx || !node || !ctx->analysis) return;

    /* Emit tethers at node start */
    omni_codegen_emit_tethers(ctx, node->position_start);
    
    /* Issue 1 P2: Emit region releases at last-use positions */
    omni_codegen_emit_region_releases_at_pos(ctx, node->position_start);

    /* If node has a different end position, emit those too 
     * (though usually tethers are at boundaries) */
    if (node->position_end != node->position_start) {
        omni_codegen_emit_tethers(ctx, node->position_end);
        /* Issue 1 P2: Also emit region releases at end position */
        omni_codegen_emit_region_releases_at_pos(ctx, node->position_end);
    }
}

void omni_codegen_emit_cfg_frees(CodeGenContext* ctx, CFG* cfg, CFGNode* node) {
    if (!ctx || !cfg || !node || !ctx->analysis) return;

    size_t count;
    char** to_free = omni_get_frees_for_node(cfg, node, ctx->analysis, &count);

    for (size_t i = 0; i < count; i++) {
        const char* c_name = lookup_symbol(ctx, to_free[i]);
        if (c_name) {
            FreeStrategy strategy = omni_get_free_strategy(ctx->analysis, to_free[i]);
            const char* strategy_name = omni_free_strategy_name(strategy);

            switch (strategy) {
                case FREE_STRATEGY_NONE:
                    omni_codegen_emit(ctx, "/* CFG node %d: %s - %s (no free) */\n",
                                      node->id, c_name, strategy_name);
                    break;

                case FREE_STRATEGY_UNIQUE:
                    omni_codegen_emit(ctx, "free_unique(%s); /* CFG node %d: %s */\n",
                                      c_name, node->id, strategy_name);
                    break;

                case FREE_STRATEGY_TREE:
                    omni_codegen_emit(ctx, "free_tree(%s); /* CFG node %d: %s */\n",
                                      c_name, node->id, strategy_name);
                    break;

                case FREE_STRATEGY_RC:
                case FREE_STRATEGY_RC_TREE:
                    /* Lobster-style RC elision for CFG-aware frees */
                    if (codegen_can_elide_dec(ctx, to_free[i])) {
                        RCElisionClass elision = codegen_get_elision_class(ctx, to_free[i]);
                        const char* reason = omni_rc_elision_name(elision);
                        omni_codegen_emit(ctx, "/* RC_ELIDED: dec_ref(%s) - %s (CFG node %d) */\n",
                                          c_name, reason, node->id);
                    } else {
                        omni_codegen_emit(ctx, "dec_ref(%s); /* %s */\n", c_name, strategy_name);
                    }
                    break;

                case FREE_STRATEGY_SCC_STATIC:
                    omni_codegen_emit(ctx, "free_scc_static(%s); /* %s */\n", c_name, strategy_name);
                    break;

                case FREE_STRATEGY_COMPONENT_RELEASE:
                    omni_codegen_emit(ctx, "sym_release_handle(%s->comp); /* %s */\n", c_name, strategy_name);
                    break;

                default:
                    /* Lobster-style RC elision for unknown strategy */
                    if (codegen_can_elide_dec(ctx, to_free[i])) {
                        RCElisionClass elision = codegen_get_elision_class(ctx, to_free[i]);
                        const char* reason = omni_rc_elision_name(elision);
                        omni_codegen_emit(ctx, "/* RC_ELIDED: dec_ref(%s) - %s */\n", c_name, reason);
                    } else {
                        omni_codegen_emit(ctx, "dec_ref(%s); /* %s */\n", c_name, strategy_name);
                    }
                    break;
            }
        }
    }

    free(to_free);
}

/*
 * Generate code for expression with CFG-aware free placement.
 *
 * The strategy is:
 * 1. Build CFG for the expression
 * 2. Run ownership analysis
 * 3. Compute liveness on the CFG
 * 4. Generate code, emitting frees at each CFG node where variables die
 */
void omni_codegen_with_cfg(CodeGenContext* ctx, OmniValue* expr) {
    if (!ctx || !expr) return;

    /* Create analysis context if needed */
    if (!ctx->analysis) {
        ctx->analysis = omni_analysis_new();
    }

    /* Run ownership analysis */
    omni_analyze_ownership(ctx->analysis, expr);

    /* Build CFG */
    CFG* cfg = omni_build_cfg(expr);
    if (!cfg) {
        /* Fallback to non-CFG generation */
        omni_codegen_expr(ctx, expr);
        return;
    }

    /* Compute liveness */
    omni_compute_liveness(cfg, ctx->analysis);

    /* Run static cycle analysis */
    omni_analyze_static_symmetric(ctx->analysis, cfg);

    /* Group SCCs into components */
    omni_analyze_components(ctx->analysis, cfg);

    /* Optimize tethers (Tether Elision) */
    omni_optimize_tethers(ctx->analysis, cfg);

    /* Compute free points */
    CFGFreePoint* free_points = omni_compute_cfg_free_points(cfg, ctx->analysis);

    /* For now, just emit the comment showing what would be freed
     * Full integration would require restructuring codegen_expr to be CFG-aware */
    omni_codegen_emit(ctx, "/* CFG-aware free placement active */\n");

    /* Print free point information as comments */
    for (CFGFreePoint* fp = free_points; fp; fp = fp->next) {
        if (fp->var_count > 0) {
            omni_codegen_emit(ctx, "/* Node %d frees: ", fp->node->id);
            for (size_t i = 0; i < fp->var_count; i++) {
                omni_codegen_emit_raw(ctx, "%s ", fp->vars[i]);
            }
            omni_codegen_emit_raw(ctx, "*/\n");
        }
    }

    /* Print tether information as comments */
    for (size_t i = 0; i < cfg->node_count; i++) {
        omni_codegen_emit_cfg_tethers(ctx, cfg->nodes[i]);
    }

    /* Generate the actual code */
    omni_codegen_expr(ctx, expr);

    /* Cleanup */
    omni_cfg_free_points_free(free_points);
    omni_cfg_free(cfg);
}

/* ============== User Type Code Generation ============== */

void omni_codegen_type_struct(CodeGenContext* ctx, TypeDef* type) {
    if (!ctx || !type) return;

    omni_codegen_emit_raw(ctx, "/* User-defined type: %s */\n", type->name);
    omni_codegen_emit_raw(ctx, "typedef struct %s {\n", type->name);

    /* Standard header fields */
    omni_codegen_emit_raw(ctx, "    int tag;        /* Type tag for RTTI */\n");
    omni_codegen_emit_raw(ctx, "    int rc;         /* Reference count */\n");
    omni_codegen_emit_raw(ctx, "    int gen;        /* Generation for GenRef/IPGE */\n");

    /* User fields */
    for (size_t i = 0; i < type->field_count; i++) {
        TypeField* f = &type->fields[i];
        const char* strength_comment = "";
        if (f->strength == FIELD_WEAK) {
            strength_comment = " /* weak - no RC */";
        } else if (f->strength == FIELD_BORROWED) {
            strength_comment = " /* borrowed */";
        }
        omni_codegen_emit_raw(ctx, "    Obj* %s;%s\n", f->name, strength_comment);
    }

    omni_codegen_emit_raw(ctx, "} %s;\n\n", type->name);

    /* Type tag constant */
    omni_codegen_emit_raw(ctx, "#define TAG_%s (%d)\n\n",
                          type->name, 100 + (int)(ctx->temp_counter++));
}

void omni_codegen_type_constructor(CodeGenContext* ctx, TypeDef* type) {
    if (!ctx || !type) return;

    /* Function signature: mk_TypeName(field1, field2, ...) */
    omni_codegen_emit_raw(ctx, "/* Constructor for %s */\n", type->name);
    omni_codegen_emit_raw(ctx, "static %s* mk_%s(", type->name, type->name);

    for (size_t i = 0; i < type->field_count; i++) {
        if (i > 0) omni_codegen_emit_raw(ctx, ", ");
        omni_codegen_emit_raw(ctx, "Obj* %s", type->fields[i].name);
    }
    if (type->field_count == 0) {
        omni_codegen_emit_raw(ctx, "void");
    }
    omni_codegen_emit_raw(ctx, ") {\n");

    /* Allocate */
    omni_codegen_emit_raw(ctx, "    %s* obj = malloc(sizeof(%s));\n", type->name, type->name);
    omni_codegen_emit_raw(ctx, "    obj->tag = TAG_%s;\n", type->name);
    omni_codegen_emit_raw(ctx, "    obj->rc = 1;\n");
    omni_codegen_emit_raw(ctx, "    obj->gen = 0;\n");

    /* Initialize fields */
    for (size_t i = 0; i < type->field_count; i++) {
        TypeField* f = &type->fields[i];
        if (f->strength == FIELD_WEAK) {
            /* Weak field - no inc_ref, just set */
            omni_codegen_emit_raw(ctx, "    obj->%s = %s; /* weak: no inc_ref */\n",
                                  f->name, f->name);
        } else {
            /* Strong field - inc_ref */
            omni_codegen_emit_raw(ctx, "    obj->%s = %s; if (%s) inc_ref(%s);\n",
                                  f->name, f->name, f->name, f->name);
        }
    }

    omni_codegen_emit_raw(ctx, "    return obj;\n");
    omni_codegen_emit_raw(ctx, "}\n\n");
}

void omni_codegen_type_accessor(CodeGenContext* ctx, TypeDef* type, TypeField* field) {
    if (!ctx || !type || !field) return;

    /* TypeName_field(obj) -> Obj* */
    omni_codegen_emit_raw(ctx, "/* Accessor for %s.%s */\n", type->name, field->name);
    omni_codegen_emit_raw(ctx, "static Obj* %s_%s(%s* obj) {\n",
                          type->name, field->name, type->name);
    omni_codegen_emit_raw(ctx, "    return obj ? obj->%s : NIL;\n", field->name);
    omni_codegen_emit_raw(ctx, "}\n\n");
}

void omni_codegen_type_mutator(CodeGenContext* ctx, TypeDef* type, TypeField* field) {
    if (!ctx || !type || !field) return;

    /* set_TypeName_field(obj, value) */
    omni_codegen_emit_raw(ctx, "/* Mutator for %s.%s */\n", type->name, field->name);
    omni_codegen_emit_raw(ctx, "static void set_%s_%s(%s* obj, Obj* val) {\n",
                          type->name, field->name, type->name);
    omni_codegen_emit_raw(ctx, "    if (!obj) return;\n");

    if (field->strength == FIELD_WEAK) {
        /* Weak field - use SET_WEAK macro */
        omni_codegen_emit_raw(ctx, "    /* Weak field: register for nullification */\n");
        omni_codegen_emit_raw(ctx, "    SET_WEAK(obj, %s, val);\n", field->name);
    } else {
        /* Strong field - standard RC management */
        omni_codegen_emit_raw(ctx, "    Obj* old = obj->%s;\n", field->name);
        omni_codegen_emit_raw(ctx, "    obj->%s = val;\n", field->name);
        omni_codegen_emit_raw(ctx, "    if (val) inc_ref(val);\n");
        omni_codegen_emit_raw(ctx, "    if (old) dec_ref(old);\n");
    }

    omni_codegen_emit_raw(ctx, "}\n\n");
}

void omni_codegen_type_release(CodeGenContext* ctx, TypeDef* type) {
    if (!ctx || !type) return;

    /* release_TypeName(obj) - called when RC hits 0 */
    omni_codegen_emit_raw(ctx, "/* Release function for %s */\n", type->name);
    omni_codegen_emit_raw(ctx, "static void release_%s(%s* obj) {\n",
                          type->name, type->name);
    omni_codegen_emit_raw(ctx, "    if (!obj) return;\n");

    /* Nullify weak refs pointing to this object */
    omni_codegen_emit_raw(ctx, "    weak_refs_nullify((Obj*)obj);\n");

    /* Release strong fields only */
    for (size_t i = 0; i < type->field_count; i++) {
        TypeField* f = &type->fields[i];
        if (f->strength == FIELD_STRONG) {
            omni_codegen_emit_raw(ctx, "    if (obj->%s) dec_ref(obj->%s);\n",
                                  f->name, f->name);
        } else {
            omni_codegen_emit_raw(ctx, "    /* skip %s: %s */\n",
                                  f->name, omni_field_strength_name(f->strength));
        }
    }

    omni_codegen_emit_raw(ctx, "    free(obj);\n");
    omni_codegen_emit_raw(ctx, "}\n\n");
}

void omni_codegen_type_full(CodeGenContext* ctx, TypeDef* type) {
    if (!ctx || !type) return;

    omni_codegen_emit_raw(ctx, "/* ========== Type: %s ========== */\n\n", type->name);

    /* Generate struct */
    omni_codegen_type_struct(ctx, type);

    /* Generate constructor */
    omni_codegen_type_constructor(ctx, type);

    /* Generate accessors and mutators for each field */
    for (size_t i = 0; i < type->field_count; i++) {
        omni_codegen_type_accessor(ctx, type, &type->fields[i]);
        if (type->fields[i].is_mutable) {
            omni_codegen_type_mutator(ctx, type, &type->fields[i]);
        }
    }

    /* Generate release function */
    omni_codegen_type_release(ctx, type);
}

void omni_codegen_all_types(CodeGenContext* ctx) {
    if (!ctx || !ctx->analysis || !ctx->analysis->type_registry) return;

    TypeRegistry* reg = ctx->analysis->type_registry;

    omni_codegen_emit_raw(ctx, "/* ========== User-Defined Types ========== */\n\n");

    for (TypeDef* t = reg->types; t; t = t->next) {
        omni_codegen_type_full(ctx, t);
    }
}

/* ============== Shape-Aware Memory Management ============== */

void omni_codegen_shape_aware_free(CodeGenContext* ctx, TypeDef* type) {
    if (!ctx || !type) return;

    omni_codegen_emit_raw(ctx, "/* Shape-aware free for %s (shape: ", type->name);

    switch (type->shape) {
        case SHAPE_TREE:
            omni_codegen_emit_raw(ctx, "TREE) */\n");
            /* Generate tree-recursive free */
            omni_codegen_emit_raw(ctx, "static void free_tree_%s(%s* obj) {\n",
                                  type->name, type->name);
            omni_codegen_emit_raw(ctx, "    if (!obj) return;\n");

            /* Recursively free children first, then self */
            for (size_t i = 0; i < type->field_count; i++) {
                TypeField* f = &type->fields[i];
                if (f->strength == FIELD_STRONG && f->type_name) {
                    /* Check if field is a user type */
                    if (ctx->analysis) {
                        TypeDef* field_type = omni_get_type(ctx->analysis, f->type_name);
                        if (field_type && field_type->shape == SHAPE_TREE) {
                            omni_codegen_emit_raw(ctx, "    free_tree_%s((%s*)obj->%s);\n",
                                                  f->type_name, f->type_name, f->name);
                        } else {
                            omni_codegen_emit_raw(ctx, "    if (obj->%s) dec_ref(obj->%s);\n",
                                                  f->name, f->name);
                        }
                    } else {
                        omni_codegen_emit_raw(ctx, "    if (obj->%s) dec_ref(obj->%s);\n",
                                              f->name, f->name);
                    }
                }
            }
            omni_codegen_emit_raw(ctx, "    free(obj);\n");
            omni_codegen_emit_raw(ctx, "}\n\n");
            break;

        case SHAPE_DAG:
            omni_codegen_emit_raw(ctx, "DAG) */\n");
            /* DAG uses standard dec_ref */
            omni_codegen_emit_raw(ctx, "/* DAG shape: use standard release_%s (dec_ref) */\n\n",
                                  type->name);
            break;

        case SHAPE_CYCLIC:
            omni_codegen_emit_raw(ctx, "CYCLIC) */\n");
            if (type->has_cycles) {
                /* For cyclic types, generate arena-based allocation helper */
                omni_codegen_emit_raw(ctx, "/* CYCLIC shape: use arena allocation or weak refs */\n");
                omni_codegen_emit_raw(ctx, "static %s* arena_alloc_%s(Arena* arena",
                                      type->name, type->name);
                for (size_t i = 0; i < type->field_count; i++) {
                    omni_codegen_emit_raw(ctx, ", Obj* %s", type->fields[i].name);
                }
                omni_codegen_emit_raw(ctx, ") {\n");
                omni_codegen_emit_raw(ctx, "    %s* obj = arena_alloc(arena, sizeof(%s));\n",
                                      type->name, type->name);
                omni_codegen_emit_raw(ctx, "    obj->tag = TAG_%s;\n", type->name);
                omni_codegen_emit_raw(ctx, "    obj->rc = 1;\n");
                omni_codegen_emit_raw(ctx, "    obj->gen = 0;\n");
                for (size_t i = 0; i < type->field_count; i++) {
                    TypeField* f = &type->fields[i];
                    omni_codegen_emit_raw(ctx, "    obj->%s = %s;\n", f->name, f->name);
                }
                omni_codegen_emit_raw(ctx, "    return obj;\n");
                omni_codegen_emit_raw(ctx, "}\n\n");
            }
            break;

        default:
            omni_codegen_emit_raw(ctx, "UNKNOWN) */\n");
            omni_codegen_emit_raw(ctx, "/* Unknown shape: use standard release_%s */\n\n",
                                  type->name);
            break;
    }
}

void omni_codegen_shape_helpers(CodeGenContext* ctx) {
    if (!ctx) return;

    omni_codegen_emit_raw(ctx, "/* ========== Shape-Aware Memory Helpers ========== */\n\n");

    /* Arena type definition */
    omni_codegen_emit_raw(ctx, "/* Arena allocator for cyclic structures */\n");
    omni_codegen_emit_raw(ctx, "typedef struct ArenaBlock {\n");
    omni_codegen_emit_raw(ctx, "    void* data;\n");
    omni_codegen_emit_raw(ctx, "    size_t size;\n");
    omni_codegen_emit_raw(ctx, "    size_t used;\n");
    omni_codegen_emit_raw(ctx, "    struct ArenaBlock* next;\n");
    omni_codegen_emit_raw(ctx, "} ArenaBlock;\n\n");

    omni_codegen_emit_raw(ctx, "typedef struct Arena {\n");
    omni_codegen_emit_raw(ctx, "    ArenaBlock* blocks;\n");
    omni_codegen_emit_raw(ctx, "    size_t default_size;\n");
    omni_codegen_emit_raw(ctx, "} Arena;\n\n");

    /* Arena functions */
    omni_codegen_emit_raw(ctx, "static Arena* arena_create(size_t block_size) {\n");
    omni_codegen_emit_raw(ctx, "    Arena* a = malloc(sizeof(Arena));\n");
    omni_codegen_emit_raw(ctx, "    a->blocks = NULL;\n");
    omni_codegen_emit_raw(ctx, "    a->default_size = block_size > 0 ? block_size : 4096;\n");
    omni_codegen_emit_raw(ctx, "    return a;\n");
    omni_codegen_emit_raw(ctx, "}\n\n");

    omni_codegen_emit_raw(ctx, "static void* arena_alloc(Arena* a, size_t size) {\n");
    omni_codegen_emit_raw(ctx, "    /* Align to 8 bytes */\n");
    omni_codegen_emit_raw(ctx, "    size = (size + 7) & ~7;\n");
    omni_codegen_emit_raw(ctx, "    /* Find block with space */\n");
    omni_codegen_emit_raw(ctx, "    for (ArenaBlock* b = a->blocks; b; b = b->next) {\n");
    omni_codegen_emit_raw(ctx, "        if (b->size - b->used >= size) {\n");
    omni_codegen_emit_raw(ctx, "            void* ptr = (char*)b->data + b->used;\n");
    omni_codegen_emit_raw(ctx, "            b->used += size;\n");
    omni_codegen_emit_raw(ctx, "            return ptr;\n");
    omni_codegen_emit_raw(ctx, "        }\n");
    omni_codegen_emit_raw(ctx, "    }\n");
    omni_codegen_emit_raw(ctx, "    /* Allocate new block */\n");
    omni_codegen_emit_raw(ctx, "    size_t bsize = size > a->default_size ? size : a->default_size;\n");
    omni_codegen_emit_raw(ctx, "    ArenaBlock* b = malloc(sizeof(ArenaBlock));\n");
    omni_codegen_emit_raw(ctx, "    b->data = malloc(bsize);\n");
    omni_codegen_emit_raw(ctx, "    b->size = bsize;\n");
    omni_codegen_emit_raw(ctx, "    b->used = size;\n");
    omni_codegen_emit_raw(ctx, "    b->next = a->blocks;\n");
    omni_codegen_emit_raw(ctx, "    a->blocks = b;\n");
    omni_codegen_emit_raw(ctx, "    return b->data;\n");
    omni_codegen_emit_raw(ctx, "}\n\n");

    omni_codegen_emit_raw(ctx, "static void arena_destroy(Arena* a) {\n");
    omni_codegen_emit_raw(ctx, "    ArenaBlock* b = a->blocks;\n");
    omni_codegen_emit_raw(ctx, "    while (b) {\n");
    omni_codegen_emit_raw(ctx, "        ArenaBlock* next = b->next;\n");
    omni_codegen_emit_raw(ctx, "        free(b->data);\n");
    omni_codegen_emit_raw(ctx, "        free(b);\n");
    omni_codegen_emit_raw(ctx, "        b = next;\n");
    omni_codegen_emit_raw(ctx, "    }\n");
    omni_codegen_emit_raw(ctx, "    free(a);\n");
    omni_codegen_emit_raw(ctx, "}\n\n");

    /* Generate shape-aware free for all types */
    if (ctx->analysis && ctx->analysis->type_registry) {
        for (TypeDef* t = ctx->analysis->type_registry->types; t; t = t->next) {
            omni_codegen_shape_aware_free(ctx, t);
        }
    }
}

void omni_codegen_emit_shape_free(CodeGenContext* ctx, const char* var_name,
                                   const char* type_name) {
    if (!ctx || !var_name || !type_name) return;

    /* Look up type shape */
    ShapeClass shape = SHAPE_UNKNOWN;
    if (ctx->analysis) {
        TypeDef* type = omni_get_type(ctx->analysis, type_name);
        if (type) {
            shape = type->shape;
        }
    }

    switch (shape) {
        case SHAPE_TREE:
            /* Use tree-recursive free */
            omni_codegen_emit(ctx, "free_tree_%s((%s*)%s);\n",
                              type_name, type_name, var_name);
            break;

        case SHAPE_DAG:
        case SHAPE_CYCLIC:
            /* Use standard dec_ref (weak refs handle cycles) */
            omni_codegen_emit(ctx, "if (%s) dec_ref(%s); /* %s */\n",
                              var_name, var_name,
                              shape == SHAPE_CYCLIC ? "cyclic" : "dag");
            break;

        default:
            /* Fall back to generic dec_ref */
            omni_codegen_emit(ctx, "if (%s) dec_ref(%s);\n", var_name, var_name);
            break;
    }
}

/* ============== Phase 15: Branch-Level Region Narrowing Codegen ============== */

/*
 * This section implements code generation for scoped escape analysis.
 * Variables that don't escape their branch/let scope can be allocated
 * on the stack or in a scratch arena instead of the parent RC region.
 */

/*
 * omni_codegen_emit_narrowed_alloc - Emit allocation based on scoped escape analysis
 *
 * If the variable doesn't escape its current scope (ESCAPE_TARGET_NONE),
 * emit a stack allocation. Otherwise, use the default region allocation.
 */
void omni_codegen_emit_narrowed_alloc(CodeGenContext* ctx, const char* var_name,
                                      const char* type_name) {
    if (!ctx || !var_name) return;

    /* Check if we have scoped escape analysis */
    if (!ctx->analysis || !ctx->analysis->current_scope) {
        /* No scoped analysis - use default region allocation */
        omni_codegen_emit(ctx, "%s %s = region_alloc(_local_region, sizeof(%s));\n",
                          type_name, var_name, type_name);
        return;
    }

    /* Get escape target for this variable */
    EscapeTarget escape = omni_scope_get_escape_target(ctx->analysis, var_name);

    if (escape == ESCAPE_TARGET_NONE) {
        /* Variable doesn't escape - use stack allocation */
        omni_codegen_emit(ctx, "/* narrowed: stack-allocated (non-escaping) */\n");
        omni_codegen_emit(ctx, "%s _stack_%s = {0};\n", type_name, var_name);
        omni_codegen_emit(ctx, "%s* %s = &_stack_%s;\n", type_name, var_name, var_name);
    } else {
        /* Variable escapes - use region allocation */
        omni_codegen_emit(ctx, "/* narrowed: region-allocated (escaping to %s) */\n",
                          omni_escape_target_name(escape));
        omni_codegen_emit(ctx, "%s* %s = region_alloc(_local_region, sizeof(%s));\n",
                          type_name, var_name, type_name);
    }
}

/*
 * omni_codegen_emit_scope_cleanup - Emit cleanup code for scope exit
 *
 * Frees variables that don't escape the scope. Called at the end of
 * if branches, let bodies, etc.
 */
void omni_codegen_emit_scope_cleanup(CodeGenContext* ctx, ScopeInfo* scope) {
    if (!ctx || !scope) return;

    /* Get variables that need cleanup */
    size_t cleanup_count = 0;
    char** cleanup_vars = omni_scope_get_cleanup_vars(scope, &cleanup_count);

    if (cleanup_count == 0 || !cleanup_vars) {
        return;
    }

    omni_codegen_emit(ctx, "/* Phase 15: scope cleanup for %d variables */\n",
                      (int)cleanup_count);

    for (size_t i = 0; i < cleanup_count; i++) {
        const char* var_name = cleanup_vars[i];

        /* Get the shape info for this variable */
        ScopedVarInfo* var_info = omni_scope_find_var_in_scope(scope, var_name);
        if (var_info) {
            switch (var_info->shape) {
                case SHAPE_TREE:
                    /* Tree-shaped: use free_tree */
                    omni_codegen_emit(ctx, "free_tree(%s); /* tree cleanup */\n", var_name);
                    break;

                case SHAPE_DAG:
                case SHAPE_CYCLIC:
                    /* These should have escaped - shouldn't be in cleanup list */
                    omni_codegen_emit(ctx, "/* unexpected: %s has shape %s in cleanup */\n",
                                      var_name,
                                      var_info->shape == SHAPE_DAG ? "DAG" : "CYCLIC");
                    break;

                default:
                    /* Unknown shape - use conservative cleanup (with RC elision check) */
                    if (codegen_can_elide_dec(ctx, var_name)) {
                        RCElisionClass elision = codegen_get_elision_class(ctx, var_name);
                        const char* reason = omni_rc_elision_name(elision);
                        omni_codegen_emit(ctx, "/* RC_ELIDED: dec_ref(%s) - %s */\n", var_name, reason);
                    } else {
                        omni_codegen_emit(ctx, "if (%s) dec_ref(%s);\n", var_name, var_name);
                    }
                    break;
            }
        }

        free(cleanup_vars[i]);
    }

    free(cleanup_vars);
}

/*
 * omni_codegen_if_narrowed - Generate code for 'if' with branch-level narrowing
 *
 * Each branch gets its own scope. Non-escaping variables in each branch
 * are stack-allocated and cleaned up at the end of the branch.
 *
 * We use the AST node to scope mapping created during analysis to find
 * the correct scope for each branch.
 *
 * Note: This generates a ternary expression where each branch is self-contained.
 * For complex branches with cleanup needs, consider using block statements instead.
 */
void omni_codegen_if_narrowed(CodeGenContext* ctx, OmniValue* expr) {
    if (!ctx || !expr) return;

    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    OmniValue* cond = omni_car(args);
    args = omni_cdr(args);
    OmniValue* then_branch = omni_is_nil(args) ? NULL : omni_car(args);
    args = omni_cdr(args);
    OmniValue* else_branch = omni_is_nil(args) ? NULL : omni_car(args);

    /* Find the then and else branch scopes using the AST node mapping */
    ScopeInfo* then_scope = NULL;
    ScopeInfo* else_scope = NULL;

    if (ctx->analysis) {
        if (then_branch) {
            then_scope = omni_scope_find_by_ast_node(ctx->analysis, then_branch);
        }
        if (else_branch) {
            else_scope = omni_scope_find_by_ast_node(ctx->analysis, else_branch);
        }
    }

    omni_codegen_emit_raw(ctx, "(is_truthy(");
    codegen_expr(ctx, cond);
    omni_codegen_emit_raw(ctx, ") ? (");

    /* Generate then branch with scope tracking */
    if (then_branch) {
        /* Save current scope and set to then scope */
        ScopeInfo* saved_scope = ctx->analysis ? ctx->analysis->current_scope : NULL;
        if (ctx->analysis) ctx->analysis->current_scope = then_scope;

        codegen_expr(ctx, then_branch);

        /* Restore scope */
        if (ctx->analysis) ctx->analysis->current_scope = saved_scope;
    } else {
        omni_codegen_emit_raw(ctx, "NOTHING");
    }

    omni_codegen_emit_raw(ctx, ") : (");

    /* Generate else branch with scope tracking */
    if (else_branch) {
        /* Save current scope and set to else scope */
        ScopeInfo* saved_scope = ctx->analysis ? ctx->analysis->current_scope : NULL;
        if (ctx->analysis) ctx->analysis->current_scope = else_scope;

        codegen_expr(ctx, else_branch);

        /* Restore scope */
        if (ctx->analysis) ctx->analysis->current_scope = saved_scope;
    } else {
        omni_codegen_emit_raw(ctx, "NOTHING");
    }

    omni_codegen_emit_raw(ctx, "))");
}

/*
 * omni_codegen_let_narrowed - Generate code for 'let' with scope-aware allocation
 *
 * Variables in the let that don't escape the let body are stack-allocated.
 * Cleanup is emitted at the end of the let block.
 *
 * We use the AST node to scope mapping created during analysis to find
 * the correct scope for the let expression.
 */
void omni_codegen_let_narrowed(CodeGenContext* ctx, OmniValue* expr) {
    if (!ctx || !expr) return;

    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    OmniValue* first = omni_car(args);

    omni_codegen_emit_raw(ctx, "({ /* narrowed let */\n");
    omni_codegen_indent(ctx);

    /* Find the let scope using the AST node mapping */
    ScopeInfo* let_scope = NULL;
    if (ctx->analysis) {
        let_scope = omni_scope_find_by_ast_node(ctx->analysis, expr);
    }

    OmniValue* body = NULL;

    /* Emit bindings with narrowed allocation */
    if (omni_is_array(first)) {
        /* Slot syntax: (let [x val] [y val] body...) */
        OmniValue* current = args;
        while (current && !omni_is_nil(current) && omni_is_cell(current)) {
            OmniValue* elem = omni_car(current);

            if (!omni_is_array(elem)) {
                /* Not an array - we've hit the body */
                break;
            }

            /* Process array binding: [name val] or [name type val] */
            size_t len = omni_array_len(elem);
            if (len >= 2) {
                OmniValue* name = omni_array_get(elem, 0);
                OmniValue* val = (len == 2) ? omni_array_get(elem, 1) : omni_array_get(elem, 2);

                if (omni_is_sym(name)) {
                    char* c_name = omni_codegen_mangle(name->str_val);

                    /* Check if this variable escapes */
                    EscapeTarget escape = ESCAPE_TARGET_GLOBAL;  /* Conservative default */
                    if (ctx->analysis) {
                        escape = omni_scope_get_escape_target(ctx->analysis, name->str_val);
                    }

                    if (escape == ESCAPE_TARGET_NONE) {
                        /* Stack allocate */
                        omni_codegen_emit(ctx, "Obj _stack_%s = {0};\n", c_name);
                        omni_codegen_emit(ctx, "Obj* %s = &_stack_%s; /* narrowed: non-escaping */\n",
                                          c_name, c_name);
                    } else {
                        /* Region allocate */
                        omni_codegen_emit(ctx, "Obj* %s = region_alloc(_local_region, sizeof(Obj)); /* narrowed: escaping to %s */\n",
                                          c_name, omni_escape_target_name(escape));
                    }

                    omni_codegen_emit(ctx, "%s = ", c_name);
                    codegen_expr(ctx, val);
                    omni_codegen_emit_raw(ctx, ";\n");

                    register_symbol(ctx, name->str_val, c_name);
                    free(c_name);
                }
            }

            current = omni_cdr(current);
        }
        body = current;
    } else if (omni_is_cell(first)) {
        /* List-style: ((x 1) (y 2)) */
        OmniValue* bindings = first;
        body = omni_cdr(args);
        while (!omni_is_nil(bindings) && omni_is_cell(bindings)) {
            OmniValue* binding = omni_car(bindings);
            if (omni_is_cell(binding)) {
                OmniValue* name = omni_car(binding);
                OmniValue* val = omni_car(omni_cdr(binding));
                if (omni_is_sym(name)) {
                    char* c_name = omni_codegen_mangle(name->str_val);

                    /* Check if this variable escapes */
                    EscapeTarget escape = ESCAPE_TARGET_GLOBAL;  /* Conservative default */
                    if (ctx->analysis) {
                        escape = omni_scope_get_escape_target(ctx->analysis, name->str_val);
                    }

                    if (escape == ESCAPE_TARGET_NONE) {
                        /* OPTIMIZATION (T-opt-region-metadata-escape-alloc): Stack allocation for non-escaping variables */
                        omni_codegen_emit(ctx, "/* Stack allocation: non-escaping variable */\n");
                        omni_codegen_emit(ctx, "Obj _stack_%s = {0};\n", c_name);
                        omni_codegen_emit(ctx, "Obj* %s = &_stack_%s; /* escape: none, alloc: stack */\n",
                                          c_name, c_name);

                        /* Initialize the value */
                        omni_codegen_emit(ctx, "%s = ", c_name);
                        codegen_expr(ctx, val);
                        omni_codegen_emit_raw(ctx, ";\n");
                    } else {
                        /* OPTIMIZATION (T-opt-region-metadata-codegen): Use typed allocation with metadata */
                        int type_id = omni_codegen_get_var_type_id(ctx, name->str_val);

                        if (type_id != TYPE_ID_GENERIC) {
                            /* Use alloc_obj_typed() with compile-time type constant */
                            /* This will automatically use inline buffer when appropriate based on metadata */
                            bool can_inline = type_id_can_inline(type_id);
                            size_t threshold = type_id_inline_threshold(type_id);

                            omni_codegen_emit(ctx, "/* Region allocation: type=%s, escape=%s",
                                            type_id_to_name(type_id),
                                            omni_escape_target_name(escape));
                            if (can_inline) {
                                omni_codegen_emit_raw(ctx, ", inline: yes (threshold=%zu) */\n", threshold);
                            } else {
                                omni_codegen_emit_raw(ctx, ", inline: no */\n");
                            }

                            omni_codegen_emit_typed_alloc(ctx, c_name, "_local_region", type_id);

                            /* Initialize the value */
                            omni_codegen_emit(ctx, "%s = ", c_name);
                            codegen_expr(ctx, val);
                            omni_codegen_emit_raw(ctx, ";\n");
                        } else {
                            /* Fallback to generic region allocation */
                            omni_codegen_emit(ctx, "/* Region allocation: unknown type, escape=%s */\n",
                                            omni_escape_target_name(escape));
                            omni_codegen_emit(ctx, "Obj* %s = region_alloc(_local_region, sizeof(Obj));\n",
                                              c_name);

                            /* Initialize the value */
                            omni_codegen_emit(ctx, "%s = ", c_name);
                            codegen_expr(ctx, val);
                            omni_codegen_emit_raw(ctx, ";\n");
                        }
                    }

                    register_symbol(ctx, name->str_val, c_name);
                    free(c_name);
                }
            }
            bindings = omni_cdr(bindings);
        }
    }

    /* Emit body */
    OmniValue* result = NULL;
    while (!omni_is_nil(body) && omni_is_cell(body)) {
        result = omni_car(body);
        body = omni_cdr(body);
        if (!omni_is_nil(body)) {
            omni_codegen_emit(ctx, "");
            codegen_expr(ctx, result);
            omni_codegen_emit_raw(ctx, ";\n");
        }
    }

    /* Last expression is the result */
    if (result) {
        omni_codegen_emit(ctx, "");
        codegen_expr(ctx, result);
        omni_codegen_emit_raw(ctx, ";\n");
    }

    /* Emit scope cleanup for non-escaping variables */
    if (let_scope) {
        omni_codegen_emit_scope_cleanup(ctx, let_scope);
    }

    omni_codegen_dedent(ctx);
    omni_codegen_emit(ctx, "})");
}

/* ============== Phase 24: Region-Level Metadata Codegen ============== */

/*
 * Emit allocation using alloc_obj_typed() with type_id
 *
 * Generates: Obj* var = alloc_obj_typed(region, TYPE_ID_XXX);
 */
void omni_codegen_emit_typed_alloc(CodeGenContext* ctx,
                                   const char* var_name,
                                   const char* region_name,
                                   int type_id) {
    if (!ctx || !var_name) return;

    /* Use default region if none specified */
    const char* region = region_name ? region_name : "_local_region";

    /* Convert type_id to constant name for codegen */
    const char* type_const = "TYPE_ID_GENERIC";
    switch (type_id) {
        case 0:  type_const = "TYPE_ID_INT";         break;
        case 1:  type_const = "TYPE_ID_FLOAT";       break;
        case 2:  type_const = "TYPE_ID_CHAR";        break;
        case 3:  type_const = "TYPE_ID_PAIR";        break;
        case 4:  type_const = "TYPE_ID_ARRAY";       break;
        case 5:  type_const = "TYPE_ID_STRING";      break;
        case 6:  type_const = "TYPE_ID_SYMBOL";      break;
        case 7:  type_const = "TYPE_ID_DICT";        break;
        case 8:  type_const = "TYPE_ID_CLOSURE";     break;
        case 9:  type_const = "TYPE_ID_BOX";         break;
        case 10: type_const = "TYPE_ID_CHANNEL";     break;
        case 11: type_const = "TYPE_ID_THREAD";      break;
        case 12: type_const = "TYPE_ID_ERROR";       break;
        case 13: type_const = "TYPE_ID_ATOM";        break;
        case 14: type_const = "TYPE_ID_TUPLE";       break;
        case 15: type_const = "TYPE_ID_NAMED_TUPLE"; break;
        case 16: type_const = "TYPE_ID_GENERIC";     break;
        case 17: type_const = "TYPE_ID_KIND";        break;
        case 18: type_const = "TYPE_ID_NOTHING";     break;
        default: type_const = "TYPE_ID_GENERIC";     break;
    }

    /* Emit the allocation call */
    omni_codegen_emit(ctx, "Obj* %s = alloc_obj_typed(%s, %s);\n",
                      var_name, region, type_const);
}

/*
 * Get type_id for a variable from analysis context
 *
 * Returns the TypeID enum value assigned during type inference,
 * or TYPE_ID_GENERIC (-1) if unknown.
 */
int omni_codegen_get_var_type_id(CodeGenContext* ctx, const char* var_name) {
    if (!ctx || !ctx->analysis || !var_name) {
        return TYPE_ID_GENERIC;
    }

    /* Query analysis context for type_id */
    return omni_get_var_type_id(ctx->analysis, var_name);
}

/* ============== Phase 27: Type Specialization API Implementation ============== */

/*
 * Initialize type specialization for code generation.
 *
 * Creates SpecDB and TypeEnv for tracking specializations and types.
 */
bool omni_codegen_init_specialization(CodeGenContext* ctx) {
    if (!ctx) return false;

    /* Create specialization database */
    ctx->spec_db = spec_db_new();
    if (!ctx->spec_db) return false;

    /* Create type environment */
    ctx->type_env = type_env_new(NULL);
    if (!ctx->type_env) {
        spec_db_free(ctx->spec_db);
        ctx->spec_db = NULL;
        return false;
    }

    /* Register common primitive specializations */
    /* These will be generated on-demand, but we register them here for tracking */

    /* Arithmetic operations */
    /* add_Int_Int, add_Float_Float, etc. are registered in spec_db on-demand */

    return true;
}

/*
 * Clean up type specialization resources.
 */
void omni_codegen_cleanup_specialization(CodeGenContext* ctx) {
    if (!ctx) return;

    if (ctx->spec_db) {
        spec_db_free(ctx->spec_db);
        ctx->spec_db = NULL;
    }

    if (ctx->type_env) {
        type_env_free(ctx->type_env);
        ctx->type_env = NULL;
    }
}

/*
 * Enable or disable type specialization.
 */
void omni_codegen_set_specialization(CodeGenContext* ctx, bool enable) {
    if (!ctx) return;
    ctx->enable_specialization = enable;
}

/*
 * Get the type of an expression during code generation.
 *
 * Uses the type environment to look up variable types and infer expression types.
 */
ConcreteType* omni_codegen_get_expr_type(CodeGenContext* ctx, OmniValue* expr) {
    if (!ctx || !ctx->type_env || !expr) {
        /* Return NULL to indicate unknown type */
        return NULL;
    }

    /* Use type inference to determine expression type */
    /* This bridges to the type_infer module */
    return infer_expr(ctx->analysis, ctx->type_env, expr);
}

/*
 * Register a function specialization.
 *
 * Records that a specialized version of a function exists for specific parameter types.
 */
SpecSignature* omni_codegen_register_specialization(CodeGenContext* ctx,
                                                   const char* func_name,
                                                   ConcreteType** param_types,
                                                   int param_count,
                                                   ConcreteType* return_type,
                                                   bool is_builtin) {
    if (!ctx || !ctx->spec_db || !func_name) {
        return NULL;
    }

    /* Register specialization in database */
    return spec_db_register(ctx->spec_db, func_name, param_types, param_count,
                           return_type, is_builtin);
}

/*
 * Check if a specialized version exists for a function call.
 */
SpecSignature* omni_codegen_lookup_specialization(CodeGenContext* ctx,
                                                const char* func_name,
                                                ConcreteType** arg_types,
                                                int arg_count) {
    if (!ctx || !ctx->spec_db || !func_name) {
        return NULL;
    }

    /* Look up specialization in database */
    return spec_db_lookup(ctx->spec_db, func_name, arg_types, arg_count);
}

/*
 * Generate code for a function call with type dispatch.
 *
 * Automatically chooses between specialized and generic versions based on argument types.
 */
void omni_codegen_dispatch_call(CodeGenContext* ctx,
                              const char* func_name,
                              OmniValue** args,
                              ConcreteType** arg_types,
                              int arg_count) {
    if (!ctx || !func_name) return;

    /* If specialization is disabled, use generic call */
    if (!ctx->enable_specialization) {
        /* Emit generic function call */
        /* TODO: Generate generic call code */
        return;
    }

    /* If arg_types not provided, try to infer them */
    ConcreteType* inferred_types[arg_count];
    if (!arg_types) {
        for (int i = 0; i < arg_count; i++) {
            inferred_types[i] = omni_codegen_get_expr_type(ctx, args[i]);
        }
        arg_types = inferred_types;
    }

    /* Check if specialized version exists */
    SpecSignature* spec = omni_codegen_lookup_specialization(ctx, func_name,
                                                             arg_types, arg_count);

    if (spec && spec->mangled_name) {
        /* Emit specialized function call */
        /* TODO: Generate specialized call with unboxing/unboxing */
        char* temp_result = omni_codegen_temp(ctx);
        omni_codegen_emit(ctx, "Obj* %s = %s(", temp_result, spec->mangled_name);

        /* Emit arguments (unboxed if needed) */
        for (int i = 0; i < arg_count; i++) {
            if (i > 0) omni_codegen_emit_raw(ctx, ", ");

            /* TODO: Generate unboxing code if argument is unboxed in specialized function */
            /* For now, emit boxed argument */
            char* temp_arg = omni_codegen_temp(ctx);
            omni_codegen_expr(ctx, args[i]);
            omni_codegen_emit_raw(ctx, "%s", temp_arg);
            free(temp_arg);
        }

        omni_codegen_emit_raw(ctx, ");\n");
        free(temp_result);
    } else {
        /* Emit generic function call */
        /* TODO: Generate generic call code */
        char* temp_result = omni_codegen_temp(ctx);
        omni_codegen_emit(ctx, "Obj* %s = apply(", temp_result);

        /* Emit function name */
        omni_codegen_emit_raw(ctx, "%s", func_name);

        /* Emit arguments */
        for (int i = 0; i < arg_count; i++) {
            omni_codegen_emit_raw(ctx, ", ");
            char* temp_arg = omni_codegen_temp(ctx);
            omni_codegen_expr(ctx, args[i]);
            omni_codegen_emit_raw(ctx, "%s", temp_arg);
            free(temp_arg);
        }

        omni_codegen_emit_raw(ctx, ");\n");
        free(temp_result);
    }
}
