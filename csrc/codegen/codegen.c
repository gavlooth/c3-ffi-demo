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
#include "spec_decision.h"  /* Specialization decision policies */
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
    return ctx;
}

CodeGenContext* omni_codegen_new_buffer(void) {
    CodeGenContext* ctx = malloc(sizeof(CodeGenContext));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(CodeGenContext));
    ctx->output_capacity = 4096;
    ctx->output_buffer = malloc(ctx->output_capacity);
    ctx->output_buffer[0] = '\0';
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

static const char* lookup_symbol(CodeGenContext* ctx, const char* name) {
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
    ctx->symbols.names[ctx->symbols.count] = strdup(name);
    ctx->symbols.c_names[ctx->symbols.count] = strdup(c_name);
    ctx->symbols.count++;
}

/* ============== Runtime Header ============== */

void omni_codegen_runtime_header(CodeGenContext* ctx) {
    omni_codegen_emit_raw(ctx, "/* Generated by OmniLisp Compiler */\n");
    omni_codegen_emit_raw(ctx, "/* ASAP Memory Management - Compile-Time Free Injection */\n\n");

    if (ctx->use_runtime && ctx->runtime_path) {
        omni_codegen_emit_raw(ctx, "#include \"%s/include/omni.h\"\n\n", ctx->runtime_path);
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
        omni_codegen_emit_raw(ctx, "#include <pthread.h>\n\n");

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
        omni_codegen_emit_raw(ctx, " * THREAD_TRANSFER: Data transferred via channel, ownership moves.\n");
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

        omni_codegen_emit_raw(ctx, "/* Channel operations - ownership transfer semantics */\n");
        omni_codegen_emit_raw(ctx, "typedef struct Channel {\n");
        omni_codegen_emit_raw(ctx, "    Obj** buffer;\n");
        omni_codegen_emit_raw(ctx, "    size_t capacity;\n");
        omni_codegen_emit_raw(ctx, "    size_t head, tail, count;\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_t mutex;\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_t not_empty;\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_t not_full;\n");
        omni_codegen_emit_raw(ctx, "    int closed;\n");
        omni_codegen_emit_raw(ctx, "} Channel;\n\n");

        omni_codegen_emit_raw(ctx, "static Channel* channel_new(size_t capacity) {\n");
        omni_codegen_emit_raw(ctx, "    Channel* c = malloc(sizeof(Channel));\n");
        omni_codegen_emit_raw(ctx, "    c->buffer = malloc(capacity * sizeof(Obj*));\n");
        omni_codegen_emit_raw(ctx, "    c->capacity = capacity;\n");
        omni_codegen_emit_raw(ctx, "    c->head = c->tail = c->count = 0;\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_init(&c->mutex, NULL);\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_init(&c->not_empty, NULL);\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_init(&c->not_full, NULL);\n");
        omni_codegen_emit_raw(ctx, "    c->closed = 0;\n");
        omni_codegen_emit_raw(ctx, "    return c;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Send transfers ownership - sender must NOT free after */\n");
        omni_codegen_emit_raw(ctx, "static void channel_send(Channel* c, Obj* value) {\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_lock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    while (c->count == c->capacity && !c->closed) {\n");
        omni_codegen_emit_raw(ctx, "        pthread_cond_wait(&c->not_full, &c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    if (!c->closed) {\n");
        omni_codegen_emit_raw(ctx, "        c->buffer[c->tail] = value;  /* Ownership transfers */\n");
        omni_codegen_emit_raw(ctx, "        c->tail = (c->tail + 1) %% c->capacity;\n");
        omni_codegen_emit_raw(ctx, "        c->count++;\n");
        omni_codegen_emit_raw(ctx, "        pthread_cond_signal(&c->not_empty);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_unlock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Recv receives ownership - receiver must free when done */\n");
        omni_codegen_emit_raw(ctx, "static Obj* channel_recv(Channel* c) {\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_lock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    while (c->count == 0 && !c->closed) {\n");
        omni_codegen_emit_raw(ctx, "        pthread_cond_wait(&c->not_empty, &c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    Obj* value = NIL;\n");
        omni_codegen_emit_raw(ctx, "    if (c->count > 0) {\n");
        omni_codegen_emit_raw(ctx, "        value = c->buffer[c->head];  /* Ownership transfers */\n");
        omni_codegen_emit_raw(ctx, "        c->head = (c->head + 1) %% c->capacity;\n");
        omni_codegen_emit_raw(ctx, "        c->count--;\n");
        omni_codegen_emit_raw(ctx, "        pthread_cond_signal(&c->not_full);\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_unlock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    return value;\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static void channel_close(Channel* c) {\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_lock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    c->closed = 1;\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_broadcast(&c->not_empty);\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_broadcast(&c->not_full);\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_unlock(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "static void channel_free(Channel* c) {\n");
        omni_codegen_emit_raw(ctx, "    if (!c) return;\n");
        omni_codegen_emit_raw(ctx, "    /* Free any remaining items in buffer */\n");
        omni_codegen_emit_raw(ctx, "    while (c->count > 0) {\n");
        omni_codegen_emit_raw(ctx, "        free_obj(c->buffer[c->head]);\n");
        omni_codegen_emit_raw(ctx, "        c->head = (c->head + 1) %% c->capacity;\n");
        omni_codegen_emit_raw(ctx, "        c->count--;\n");
        omni_codegen_emit_raw(ctx, "    }\n");
        omni_codegen_emit_raw(ctx, "    free(c->buffer);\n");
        omni_codegen_emit_raw(ctx, "    pthread_mutex_destroy(&c->mutex);\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_destroy(&c->not_empty);\n");
        omni_codegen_emit_raw(ctx, "    pthread_cond_destroy(&c->not_full);\n");
        omni_codegen_emit_raw(ctx, "    free(c);\n");
        omni_codegen_emit_raw(ctx, "}\n\n");

        omni_codegen_emit_raw(ctx, "/* Ownership transfer macros */\n");
        omni_codegen_emit_raw(ctx, "#define SEND_OWNERSHIP(ch, val) do { channel_send(ch, val); /* val no longer owned */ } while(0)\n");
        omni_codegen_emit_raw(ctx, "#define RECV_OWNERSHIP(ch, var) do { var = channel_recv(ch); /* var now owned */ } while(0)\n\n");

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
    omni_codegen_emit_raw(ctx, "mk_int(%ld)", (long)expr->int_val);
}

static void codegen_float(CodeGenContext* ctx, OmniValue* expr) {
    /* Generate float literals using mk_float_region */
    omni_codegen_emit_raw(ctx, "mk_float_region(_local_region, %f)", expr->float_val);
}

static void codegen_sym(CodeGenContext* ctx, OmniValue* expr) {
    const char* c_name = lookup_symbol(ctx, expr->str_val);
    if (c_name) {
        omni_codegen_emit_raw(ctx, "%s", c_name);
    } else {
        /* Check for primitives */
        const char* name = expr->str_val;
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
        else if (strcmp(name, "cons") == 0) omni_codegen_emit_raw(ctx, "prim_cons");
        else if (strcmp(name, "car") == 0) omni_codegen_emit_raw(ctx, "prim_car");
        else if (strcmp(name, "cdr") == 0) omni_codegen_emit_raw(ctx, "prim_cdr");
        else if (strcmp(name, "null?") == 0) omni_codegen_emit_raw(ctx, "prim_null");
        else if (strcmp(name, "type?") == 0) omni_codegen_emit_raw(ctx, "prim_type_is");
        else if (strcmp(name, "Int") == 0) omni_codegen_emit_raw(ctx, "o_Int");
        else if (strcmp(name, "String") == 0) omni_codegen_emit_raw(ctx, "o_String");
        else if (strcmp(name, "Any") == 0) omni_codegen_emit_raw(ctx, "o_Any");
        else if (strcmp(name, "Nothing") == 0) omni_codegen_emit_raw(ctx, "o_Nothing");
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

static void codegen_lambda(CodeGenContext* ctx, OmniValue* expr) {
    /* Generate lambda as a static function with region lifecycle */
    int lambda_id = ctx->lambda_counter++;

    OmniValue* head = omni_car(expr);
    OmniValue* args = omni_cdr(expr);

    /* Check if this is -> syntax (multiple slot params) */
    bool is_arrow_syntax = omni_is_sym(head) && strcmp(head->str_val, "->") == 0;

    OmniValue* body = NULL;

    /* Generate lambda function name */
    char fn_name[64];
    snprintf(fn_name, sizeof(fn_name), "_lambda_%d", lambda_id);

    /* Build function definition into a buffer */
    char def[16384];  /* Increased buffer for region management code */
    char* p = def;

    /* Function signature: Include Region* _caller_region as first parameter */
    p += sprintf(p, "static Obj* %s(struct Region* _caller_region", fn_name);

    /* Parameters - register them before generating body */
    bool first_param = false;  /* Already have _caller_region */

    if (is_arrow_syntax) {
        /* Arrow syntax: (-> [x] [y] ... body) - multiple slots then body */
        OmniValue* rest = args;
        while (!omni_is_nil(rest) && omni_is_cell(rest)) {
            OmniValue* elem = omni_car(rest);
            if (!omni_is_array(elem)) {
                /* First non-array is the body */
                body = rest;
                break;
            }
            /* Process this slot array for params */
            size_t len = omni_array_len(elem);
            for (size_t i = 0; i < len; i++) {
                OmniValue* param = omni_array_get(elem, i);
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
            rest = omni_cdr(rest);
        }
    } else {
        /* Standard lambda/fn syntax */
        OmniValue* params = omni_car(args);
        body = omni_cdr(args);

        OmniValue* param_list = params;
        if (omni_is_cell(param_list)) {
            /* List-style: (lambda (x y) body) */
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
            /* Slot syntax: (lambda [x] body) - single array with one param */
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

    /* Emit function name at call site with region parameter */
    omni_codegen_emit_raw(ctx, "%s(_local_region", fn_name);
}

/* ============== Multiple Dispatch Support ============== */

/*
 * check_function_defined - Check if a function has been defined before.
 * Returns the number of times the function has been defined (0 if not defined).
 */
static int check_function_defined(CodeGenContext* ctx, const char* c_name) {
    for (size_t i = 0; i < ctx->defined_functions.count; i++) {
        if (strcmp(ctx->defined_functions.names[i], c_name) == 0) {
            return ctx->defined_functions.definition_count[i];
        }
    }
    return 0;
}

/*
 * track_function_definition - Record that a function has been defined.
 * Increments the definition count for the function.
 */
static void track_function_definition(CodeGenContext* ctx, const char* c_name) {
    /* Check if already tracked */
    for (size_t i = 0; i < ctx->defined_functions.count; i++) {
        if (strcmp(ctx->defined_functions.names[i], c_name) == 0) {
            ctx->defined_functions.definition_count[i]++;
            return;
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
    ctx->defined_functions.count++;
}

/* Helper: Extract parameter name from a Slot or symbol for codegen
 * Returns NULL if not a valid parameter form
 * Supports:
 *   - Plain symbol: x
 *   - Slot array: [x]
 *   - Slot with type: [x {Int}] (ignores the type)
 */
static OmniValue* codegen_extract_param_name(OmniValue* param) {
    if (omni_is_sym(param)) {
        /* Plain symbol: x */
        return param;
    }
    if (omni_is_array(param) && param->array.len > 0) {
        /* Slot: [x] or [x {Type}] - first element is the name */
        OmniValue* first = omni_array_get(param, 0);
        if (omni_is_sym(first)) {
            return first;
        }
    }
    return NULL;
}

/* Helper: Check if a define expression is a function definition
 * Returns true for:
 *   - Traditional Scheme style: (define (f x y) body)
 *   - Slot syntax: (define f [x] [y] body)
 * Returns false for:
 *   - Variable defines: (define x 42)
 *   - Type alias defines: (define T1 {Int})
 */
static bool is_function_define(OmniValue* expr) {
    if (!omni_is_cell(expr) || !omni_is_sym(omni_car(expr))) return false;
    if (strcmp(omni_car(expr)->str_val, "define") != 0) return false;

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
    /* Multiple forms supported:
     *   - Simple define: (define x value)
     *   - Traditional function: (define (f x y) body)
     *   - Slot shorthand: (define f x y body)
     *   - Slot syntax: (define f [x] [y] body)
     *   - Slot with types: (define f [x {Int}] [y {String}] body)
     *   - Mixed syntax: (define f x [y {Int}] z body)
     * NOTE: Old array syntax (define f [x y] body) is no longer supported.
     */
    OmniValue* args = omni_cdr(expr);
    OmniValue* first = omni_car(args);
    OmniValue* rest = omni_cdr(args);

    if (omni_is_sym(first)) {
        OmniValue* next = omni_car(rest);

        /* Check for new Slot syntax: (define f [x] [y] body) or (define f x y body) */
        if (next && !omni_is_nil(rest)) {
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

                /* Count parameters - all but last element are parameters */
                size_t param_count = 0;
                OmniValue* params_iter = rest;
                while (!omni_is_nil(params_iter) && omni_is_cell(params_iter)) {
                    if (omni_is_nil(omni_cdr(params_iter))) {
                        /* Last element - this is the body, not a parameter */
                        break;
                    }
                    param_count++;
                    params_iter = omni_cdr(params_iter);
                }

                /* Register symbol and emit static function */
                register_symbol(ctx, fname->str_val, c_name);

                omni_codegen_emit(ctx, "static Obj* %s(Region* _caller_region", c_name);

                /* Emit parameters */
                params_iter = rest;
                for (size_t i = 0; i < param_count; i++) {
                    OmniValue* param = omni_car(params_iter);
                    OmniValue* p_name = codegen_extract_param_name(param);
                    if (p_name) {
                        char* param_c_name = omni_codegen_mangle(p_name->str_val);
                        omni_codegen_emit_raw(ctx, ", Obj* %s", param_c_name);
                        register_symbol(ctx, p_name->str_val, param_c_name);
                        free(param_c_name);
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
                free(c_name);
                return;
            }
        }

        /* OLD ARRAY SYNTAX REMOVED: (define name [args] body...) is no longer supported.
         * Use slot syntax: (define name [x] [y] body) or traditional: (define (name x y) body)
         * This block was ~220 lines of dead code that handled the deprecated syntax.
         */

        /* Variable define: (define name value) or (define name {Type} value) */
        char* c_name = omni_codegen_mangle(first->str_val);
        omni_codegen_emit(ctx, "Obj* %s = ", c_name);
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
        register_symbol(ctx, first->str_val, c_name);
        free(c_name);
    } else if (omni_is_cell(first)) {
        /* Scheme style: (define (name params...) body...) */
        OmniValue* fname = omni_car(first);
        OmniValue* params_val = omni_cdr(first);
        if (!omni_is_sym(fname)) return;

        char* c_name = omni_codegen_mangle(fname->str_val);
        int definition_count = check_function_defined(ctx, c_name);
        track_function_definition(ctx, c_name);
        register_symbol(ctx, fname->str_val, c_name);

        /* For redefinitions, use a unique internal name */
        char* impl_name;
        if (definition_count > 0) {
            impl_name = malloc(strlen(c_name) + 32);
            sprintf(impl_name, "%s_method_%d", c_name, definition_count);
        } else {
            impl_name = strdup(c_name);
        }

        /* Collect parameter names and kinds - supports Slot syntax */
        size_t param_count = omni_list_len(params_val);
        OmniValue** param_names = malloc(param_count * sizeof(OmniValue*));
        OmniValue** param_kinds = malloc(param_count * sizeof(OmniValue*));

        OmniValue* p_iter = params_val;
        for (size_t i = 0; i < param_count; i++) {
            OmniValue* p = omni_car(p_iter);
            if (omni_is_sym(p)) {
                param_names[i] = p;
                param_kinds[i] = NULL; /* Defaults to Any */
            } else if (omni_is_cell(p) && omni_is_sym(omni_car(p))) {
                /* Specialized parameter: (x {Int}) */
                param_names[i] = omni_car(p);
                param_kinds[i] = omni_car(omni_cdr(p));
            } else {
                /* Try to extract from Slot: [x] or [x {Int}] */
                OmniValue* extracted = codegen_extract_param_name(p);
                if (extracted) {
                    param_names[i] = extracted;
                    param_kinds[i] = NULL;
                } else {
                    param_names[i] = omni_new_sym("unused");
                    param_kinds[i] = NULL;
                }
            }
            p_iter = omni_cdr(p_iter);
        }

        /* Emit function implementation */
        omni_codegen_emit(ctx, "static Obj* %s(struct Region* _caller_region", c_name);
        p_iter = params_val;
        while (!omni_is_nil(p_iter) && omni_is_cell(p_iter)) {
            OmniValue* p = omni_car(p_iter);
            if (omni_is_sym(p)) {
                char* pn = omni_codegen_mangle(p->str_val);
                omni_codegen_emit_raw(ctx, ", Obj* %s", pn);
                register_symbol(ctx, p->str_val, pn);
                free(pn);
            } else {
                /* Try to extract from Slot */
                OmniValue* extracted = codegen_extract_param_name(p);
                if (extracted) {
                    char* pn = omni_codegen_mangle(extracted->str_val);
                    omni_codegen_emit_raw(ctx, ", Obj* %s", pn);
                    register_symbol(ctx, extracted->str_val, pn);
                    free(pn);
                }
            }
            p_iter = omni_cdr(p_iter);
        }
        omni_codegen_emit_raw(ctx, ") {\n");
        omni_codegen_indent(ctx);
        omni_codegen_emit(ctx, "struct Region* _local_region = region_create();\n");
        /* Issue 2 P4.2: Assign lifetime_rank based on caller region (outlives depth) */
        omni_codegen_emit(ctx, "omni_region_set_lifetime_rank(_local_region, omni_region_get_lifetime_rank(_caller_region) + 1);\n");
        /* Issue 2 P4.3b: Assign parent linkage (outlives ancestry) */
        omni_codegen_emit(ctx, "omni_region_set_parent(_local_region, _caller_region);\n");
        omni_codegen_emit(ctx, "region_tether_start(_caller_region);\n");
        OmniValue* last = NULL;
        OmniValue* b = rest;
        while (!omni_is_nil(b) && omni_is_cell(b)) {
            last = omni_car(b); b = omni_cdr(b);
            if (!omni_is_nil(b)) { omni_codegen_emit(ctx, ""); codegen_expr(ctx, last); omni_codegen_emit_raw(ctx, ";\n"); }
        }
        if (last) {
            omni_codegen_emit(ctx, "Obj* _res = "); codegen_expr(ctx, last); omni_codegen_emit_raw(ctx, ";\n");
            /* Issue 1 P2: Emit escape repair at return boundary */
            /* Select strategy based on environment variable or analysis */
            EscapeRepairStrategy lambda_strategy = omni_choose_escape_repair_strategy(ctx, "_res");
            omni_codegen_escape_repair(ctx, "_res", "_caller_region", lambda_strategy);
            omni_codegen_emit(ctx, "region_exit(_local_region); region_destroy_if_dead(_local_region); region_tether_end(_caller_region);\n");
            omni_codegen_emit(ctx, "return _res;  /* Repaired by omni_codegen_escape_repair() */\n");
        } else {
            omni_codegen_emit(ctx, "region_exit(_local_region); region_destroy_if_dead(_local_region); region_tether_end(_caller_region); return NOTHING;\n");
        }
        omni_codegen_dedent(ctx); omni_codegen_emit(ctx, "}\n\n");
        free(c_name);
    }
}

/* ============== New Special Form Implementations ============== */

static void codegen_apply(CodeGenContext* ctx, OmniValue* expr);

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

    /* Count clauses (pairs of pattern-result) */
    size_t clause_count = 0;
    OmniValue* c = clauses;
    while (!omni_is_nil(c) && omni_is_cell(c)) {
        clause_count++;
        c = omni_cdr(c);
    }
    if (clause_count % 2 != 0) clause_count--;  /* Handle odd number (else clause) */

    /* Emit the value expression */
    omni_codegen_emit(ctx, "({ /* match */\n");
    omni_codegen_indent(ctx);
    omni_codegen_emit(ctx, "Obj* _match_value = ");
    codegen_expr(ctx, value_expr);
    omni_codegen_emit_raw(ctx, ";\n");
    /* Declare result variable, initialize to NIL in case nothing matches */
    omni_codegen_emit(ctx, "Obj* _result = NIL;\n");
    omni_codegen_emit_raw(ctx, "\n");

    /* Generate if-else chain for pattern matching */
    size_t pairs_processed = 0;
    c = clauses;
    while (!omni_is_nil(c) && omni_is_cell(c)) {
        OmniValue* pattern = omni_car(c);
        c = omni_cdr(c);

        if (!omni_is_nil(c) && omni_is_cell(c)) {
            OmniValue* result_expr = omni_car(c);
            c = omni_cdr(c);

            /*
             * Phase 26: Guard Detection (& syntax)
             *
             * Check if pattern contains a guard: [pattern-elements & guard-expr]
             *
             * Examples:
             * - [n {Int} & (> n 10)]     -> guard is (> n 10)
             * - [x y & (> x y)]          -> guard is (> x y)
             * - [& (empty? nums)]         -> guard is (empty? nums)
             *
             * Note: Guards are detected but full guard evaluation requires
             * binding support which is a future enhancement.
             */
            OmniValue* guard_expr = NULL;

            /* Check if pattern is an array with & guard */
            if (omni_is_array(pattern)) {
                /* Scan array for & symbol */
                for (size_t i = 0; i < pattern->array.len; i++) {
                    OmniValue* elem = pattern->array.data[i];
                    if (omni_is_sym(elem) && strcmp(elem->str_val, "&") == 0) {
                        /* Found & guard - everything after is the guard expression */
                        if (i + 1 < pattern->array.len) {
                            /* Build guard expression from remaining elements */
                            /* For now, just take the next element as the guard */
                            guard_expr = pattern->array.data[i + 1];

                            /* Emit comment about guard */
                            omni_codegen_emit(ctx, "/* pattern: ");
                            codegen_expr(ctx, pattern);
                            omni_codegen_emit_raw(ctx, " (with guard) */\n");
                        }
                        break;
                    }
                }
            }

            if (!guard_expr) {
                /* No guard - emit regular pattern comment */
                omni_codegen_emit(ctx, "/* pattern: ");
                codegen_expr(ctx, pattern);
                omni_codegen_emit_raw(ctx, " */\n");
            }

            /* Emit the else or start of if chain */
            if (pairs_processed > 0) {
                omni_codegen_emit_raw(ctx, "else ");
            }

            if (omni_is_sym(pattern) && strcmp(pattern->str_val, "_") == 0) {
                /* Wildcard - always matches, no if needed */
                omni_codegen_emit(ctx, "{\n");
                omni_codegen_indent(ctx);
                omni_codegen_emit(ctx, "/* wildcard */\n");
                omni_codegen_emit(ctx, "_result = ");
                codegen_expr(ctx, result_expr);
                omni_codegen_emit_raw(ctx, ";\n");
                omni_codegen_dedent(ctx);
                omni_codegen_emit_raw(ctx, "}\n");
            } else {
                /* Regular pattern - use is_pattern_match */
                omni_codegen_emit(ctx, "if (is_pattern_match(");
                codegen_expr(ctx, pattern);
                omni_codegen_emit_raw(ctx, ", _match_value)");

                /* If there's a guard, add && condition to the if statement */
                if (guard_expr) {
                    omni_codegen_emit_raw(ctx, " && is_truthy(");
                    codegen_expr(ctx, guard_expr);
                    omni_codegen_emit_raw(ctx, ") /* guard */");
                }

                omni_codegen_emit_raw(ctx, ") {\n");
                omni_codegen_indent(ctx);

                omni_codegen_emit(ctx, "_result = ");
                codegen_expr(ctx, result_expr);
                omni_codegen_emit_raw(ctx, ";\n");

                omni_codegen_dedent(ctx);
                omni_codegen_emit_raw(ctx, "}\n");
            }

            pairs_processed++;
        }
    }

    /* In a statement expression, the last expression becomes the return value */
    omni_codegen_emit(ctx, "_result;  /* match result */\n");
    omni_codegen_dedent(ctx);
    omni_codegen_emit_raw(ctx, "})\n");
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
            /* Return clause - build a lambda for it */
            OmniValue* params = omni_car(rest);
            OmniValue* body_rest = omni_cdr(rest);
            OmniValue* ret_body = omni_is_cell(body_rest) ? omni_car(body_rest) : body_rest;

            omni_codegen_emit(ctx, "/* return clause */\n");
            omni_codegen_emit(ctx, "_h_return_clause = ");

            /* Build a lambda: (fn (params) ret_body) */
            omni_codegen_emit_raw(ctx, "mk_closure_simple(\"return_handler\", ");
            codegen_expr(ctx, params);
            omni_codegen_emit_raw(ctx, ", ");
            codegen_expr(ctx, ret_body);
            omni_codegen_emit_raw(ctx, ");\n");
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

        /* Build handler lambda: (fn (payload resume) handler-body) */
        omni_codegen_emit(ctx, "Obj* _handler_fn = ");

        /* For now, emit the handler as an inline lambda */
        /* This creates a lambda capturing the handler body */
        omni_codegen_emit_raw(ctx, "mk_closure_simple(\"%s_handler\", ", name);
        codegen_expr(ctx, params);
        omni_codegen_emit_raw(ctx, ", ");
        codegen_expr(ctx, handler_body);
        omni_codegen_emit_raw(ctx, ");\n");

        /* Add clause to list */
        omni_codegen_emit(ctx, "_h_clauses = handler_clause_add(_h_clauses, _eff_type, _handler_fn);\n");

        omni_codegen_dedent(ctx);
        omni_codegen_emit(ctx, "}\n");
    }

    omni_codegen_emit_raw(ctx, "\n");

    /* Push handler */
    omni_codegen_emit(ctx, "handler_push(_h_clauses, _h_return_clause, NULL);\n");
    omni_codegen_emit_raw(ctx, "\n");

    /* Evaluate body in handler scope */
    omni_codegen_emit(ctx, "Obj* _h_result = ");
    codegen_expr(ctx, body);
    omni_codegen_emit_raw(ctx, ";\n");
    omni_codegen_emit_raw(ctx, "\n");

    /* Pop handler */
    omni_codegen_emit(ctx, "handler_pop();\n");

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
 * Syntax: (resume resumption value)
 *
 * Generates: prim_resume(resumption, value)
 */
static void codegen_resume(CodeGenContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* resumption = omni_car(args);
    OmniValue* value_rest = omni_cdr(args);
    OmniValue* value = omni_is_cell(value_rest) ? omni_car(value_rest) : NULL;

    omni_codegen_emit_raw(ctx, "prim_resume(");
    codegen_expr(ctx, resumption);
    omni_codegen_emit_raw(ctx, ", ");
    if (value) {
        codegen_expr(ctx, value);
    } else {
        omni_codegen_emit_raw(ctx, "NIL");
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

/* Mutation operator: (set! var value) */
static void codegen_set_bang(CodeGenContext* ctx, OmniValue* expr) {
    /* (set! x 10) - modify a binding */
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
    omni_codegen_emit_raw(ctx, "(%s = ", c_name);
    codegen_expr(ctx, value);
    omni_codegen_emit_raw(ctx, ")");
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

/* Mutation operator: (update! obj.field f) */
static void codegen_update_bang(CodeGenContext* ctx, OmniValue* expr) {
    /* (update! obj.field inc) - transform in-place */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args) || omni_is_nil(omni_cdr(args))) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* path = omni_car(args);
    OmniValue* func = omni_cdr(args);
    if (!omni_is_nil(func)) {
        func = omni_car(func);
    }

    omni_codegen_emit(ctx, "/* update! */\n");
    omni_codegen_emit(ctx, "/* TODO: Implement in-place update */\n");
    omni_codegen_emit_raw(ctx, "return NIL;");
}

/* Functional operator: (update obj.field f) */
static void codegen_update(CodeGenContext* ctx, OmniValue* expr) {
    /* (update obj.field inc) - functional transform, returns new object */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args) || omni_is_nil(omni_cdr(args))) {
        omni_codegen_emit_raw(ctx, "NIL");
        return;
    }

    OmniValue* path = omni_car(args);
    OmniValue* func = omni_cdr(args);
    if (!omni_is_nil(func)) {
        func = omni_car(func);
    }

    omni_codegen_emit(ctx, "/* update */\n");
    omni_codegen_emit(ctx, "/* TODO: Implement functional update */\n");
    omni_codegen_emit_raw(ctx, "return NIL;");
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
    }

    /* Regular function call with Region-RC support */
    /* Check if function is a user-defined function (needs region parameter) */
    bool is_user_function = false;
    if (omni_is_sym(func)) {
        const char* c_name = lookup_symbol(ctx, func->str_val);
        /* User-defined functions start with "o_" (mangled name) */
        if (c_name && strncmp(c_name, "o_", 2) == 0) {
            is_user_function = true;
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
    }

    /* Check for inline lambda application: ((lambda ...) args...) */
    if (omni_is_cell(func)) {
        OmniValue* func_head = omni_car(func);
        if (omni_is_sym(func_head)) {
            const char* fn_name = func_head->str_val;
            if (strcmp(fn_name, "lambda") == 0 || strcmp(fn_name, "fn") == 0 ||
                strcmp(fn_name, "->") == 0 || strcmp(fn_name, "λ") == 0) {
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

        codegen_expr(ctx, func);

        omni_codegen_emit_raw(ctx, "(");

    

        /* Region-RC: Pass _local_region as first argument for user-defined functions */

        if (is_user_function) {

            omni_codegen_emit_raw(ctx, "_local_region");

            if (!omni_is_nil(args)) {

                omni_codegen_emit_raw(ctx, ", ");

            }

            bool first_a = true;

            while (!omni_is_nil(args) && omni_is_cell(args)) {

                if (!first_a) omni_codegen_emit_raw(ctx, ", ");

                first_a = false;

                codegen_expr(ctx, omni_car(args));

                args = omni_cdr(args);

            }

        } else {

            /* Built-in function - no region parameter */

            bool first_a = true;

            while (!omni_is_nil(args) && omni_is_cell(args)) {

                if (!first_a) omni_codegen_emit_raw(ctx, ", ");

                first_a = false;

                codegen_expr(ctx, omni_car(args));

                args = omni_cdr(args);

            }

        }

        omni_codegen_emit_raw(ctx, ")");

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
            strcmp(name, "λ") == 0 || strcmp(name, "->") == 0) {  /* Lambda forms */
            codegen_lambda(ctx, expr);
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
    case OMNI_TYPE_LIT:
        codegen_type_lit(ctx, expr);
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
    default:
        omni_codegen_emit_raw(ctx, "NIL");
        break;
    }
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

    /* First pass: collect defines and emit as top-level functions */
    ctx->first_pass = true;
    for (size_t i = 0; i < count; i++) {
        OmniValue* expr = exprs[i];
        if (omni_is_cell(expr) && omni_is_sym(omni_car(expr)) &&
            strcmp(omni_car(expr)->str_val, "define") == 0) {
            OmniValue* args = omni_cdr(expr);
            OmniValue* name_or_sig = omni_car(args);

            /* Only emit function defines at top level */
            if (omni_is_cell(name_or_sig)) {
                /* Traditional Scheme style: (define (f x y) body) */
                codegen_define(ctx, expr);
            } else if (omni_is_sym(name_or_sig)) {
                /* Check for slot-syntax: (define f [x] [y] body) or (define f x y body) */
                OmniValue* rest = omni_cdr(args);
                if (!omni_is_nil(rest)) {
                    OmniValue* maybe_param = omni_car(rest);
                    OmniValue* param_name = codegen_extract_param_name(maybe_param);
                    if (param_name != NULL) {
                        /* This is a slot-syntax function definition */
                        codegen_define(ctx, expr);
                    }
                }
            }
        }
    }
    ctx->first_pass = false;

    /* Generate main() to a buffer first to collect lambdas */
    CodeGenContext* main_ctx = omni_codegen_new_buffer();
    main_ctx->analysis = ctx->analysis;
    main_ctx->lambda_counter = ctx->lambda_counter;
    /* Copy symbol table */
    for (size_t i = 0; i < ctx->symbols.count; i++) {
        register_symbol(main_ctx, ctx->symbols.names[i], ctx->symbols.c_names[i]);
    }
    omni_codegen_main(main_ctx, exprs, count);
    char* main_code = omni_codegen_get_output(main_ctx);

    /* Collect lambdas generated during main */
    for (size_t i = 0; i < main_ctx->lambda_defs.count; i++) {
        omni_codegen_add_lambda_def(ctx, main_ctx->lambda_defs.defs[i]);
    }

    /* Don't free analysis from temp context */
    main_ctx->analysis = NULL;
    omni_codegen_free(main_ctx);

    /* Emit forward declarations */
    for (size_t i = 0; i < ctx->forward_decls.count; i++) {
        omni_codegen_emit_raw(ctx, "%s\n", ctx->forward_decls.decls[i]);
    }
    if (ctx->forward_decls.count > 0) {
        omni_codegen_emit_raw(ctx, "\n");
    }

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
            /* Shared/DAG/cyclic - use RC */
            omni_codegen_emit(ctx, "dec_ref(%s); /* %s */\n", c_name, strategy_name);
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
            /* Unknown - conservative RC */
            omni_codegen_emit(ctx, "dec_ref(%s); /* %s */\n", c_name, strategy_name);
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
                    omni_codegen_emit(ctx, "dec_ref(%s); /* %s */\n", c_name, strategy_name);
                    break;

                case FREE_STRATEGY_SCC_STATIC:
                    omni_codegen_emit(ctx, "free_scc_static(%s); /* %s */\n", c_name, strategy_name);
                    break;

                case FREE_STRATEGY_COMPONENT_RELEASE:
                    omni_codegen_emit(ctx, "sym_release_handle(%s->comp); /* %s */\n", c_name, strategy_name);
                    break;

                default:
                    omni_codegen_emit(ctx, "dec_ref(%s); /* %s */\n", c_name, strategy_name);
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
                    /* Unknown shape - use conservative cleanup */
                    omni_codegen_emit(ctx, "if (%s) dec_ref(%s);\n", var_name, var_name);
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
