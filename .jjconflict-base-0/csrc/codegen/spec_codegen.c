/*
 * spec_codegen.c - Specialized Code Generator Implementation
 *
 * Implementation of specialized code generator for unboxed primitives.
 *
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 3)
 */

#include "spec_codegen.h"
#include "spec_decision.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* strdup for C99 */
static char* omni_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}
#define strdup omni_strdup

/* ============== SpecCodegen API ============== */

SpecCodegenContext* spec_codegen_new(CodeGenContext* base,
                                    SpecDB* spec_db,
                                    TypeEnv* type_env) {
    SpecCodegenContext* ctx = malloc(sizeof(SpecCodegenContext));
    if (!ctx) return NULL;

    ctx->base = base;
    ctx->spec_db = spec_db;
    ctx->type_env = type_env;
    ctx->specialized_output = NULL;
    ctx->spec_count = 0;

    return ctx;
}

void spec_codegen_free(SpecCodegenContext* ctx) {
    if (!ctx) return;
    if (ctx->specialized_output) {
        fclose(ctx->specialized_output);
    }
    free(ctx);
}

/* ============== Helper Functions ============== */

static void emit_spec_raw(SpecCodegenContext* ctx, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(ctx->specialized_output ? ctx->specialized_output : ctx->base->output,
             fmt, args);
    va_end(args);
}

static void emit_spec(SpecCodegenContext* ctx, const char* fmt, ...) {
    /* Indent based on base context */
    for (int i = 0; i < ctx->base->indent_level; i++) {
        emit_spec_raw(ctx, "    ");
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(ctx->specialized_output ? ctx->specialized_output : ctx->base->output,
             fmt, args);
    va_end(args);
}

/* ============== Type Name Utilities ============== */

char* get_c_type_name(ConcreteType* type) {
    if (!type) return strdup("void");

    switch (type->kind) {
        case TYPE_KIND_PRIMITIVE:
            switch (type->primitive.prim) {
                case PRIMITIVE_INT64:
                    return strdup("int64_t");
                case PRIMITIVE_FLOAT64:
                    return strdup("double");
                case PRIMITIVE_CHAR:
                    return strdup("char");
                case PRIMITIVE_BOOL:
                    return strdup("bool");
                default:
                    return strdup("int64_t");
            }

        case TYPE_KIND_ARRAY:
        case TYPE_KIND_CLOSURE:
        case TYPE_KIND_ANY:
        default:
            return strdup("Obj*");
    }
}

char* get_type_default(ConcreteType* type) {
    if (!type) return strdup("0");

    switch (type->kind) {
        case TYPE_KIND_PRIMITIVE:
            switch (type->primitive.prim) {
                case PRIMITIVE_INT64:
                    return strdup("0");
                case PRIMITIVE_FLOAT64:
                    return strdup("0.0");
                case PRIMITIVE_CHAR:
                    return strdup("'\\0'");
                case PRIMITIVE_BOOL:
                    return strdup("false");
                default:
                    return strdup("0");
            }

        case TYPE_KIND_ARRAY:
        case TYPE_KIND_CLOSURE:
        case TYPE_KIND_ANY:
        default:
            return strdup("NULL");
    }
}

/* ============== Function Generation ============== */

char* get_spec_function_name(SpecSignature* sig) {
    if (!sig || !sig->mangled_name) return NULL;
    return strdup(sig->mangled_name);
}

void generate_spec_prologue(SpecCodegenContext* ctx, SpecSignature* sig) {
    if (!sig) return;

    /* Get return type name */
    char* ret_c_type = get_c_type_name(sig->return_type);

    /* Emit function signature */
    emit_spec_raw(ctx, "%s %s(", ret_c_type, sig->mangled_name);

    /* Emit parameters */
    for (int i = 0; i < sig->param_count; i++) {
        if (i > 0) emit_spec_raw(ctx, ", ");

        ConcreteType* param_type = sig->param_types ? sig->param_types[i] : NULL;
        char* param_c_type = get_c_type_name(param_type);

        emit_spec_raw(ctx, "%s param%d", param_c_type, i);

        free(param_c_type);
    }

    emit_spec_raw(ctx, ") {\n");
    free(ret_c_type);

    omni_codegen_indent(ctx->base);
}

void generate_spec_epilogue(SpecCodegenContext* ctx,
                           SpecSignature* sig,
                           const char* result_var) {
    if (!sig) return;

    omni_codegen_dedent(ctx->base);

    /* Emit return statement */
    if (result_var && sig->return_type) {
        emit_spec(ctx, "return %s;\n", result_var);
    }

    emit_spec_raw(ctx, "}\n\n");
}

void generate_specialized_function(SpecCodegenContext* ctx,
                                  SpecSignature* sig,
                                  OmniValue* func_body) {
    if (!sig || !ctx) return;

    /* Emit prologue */
    generate_spec_prologue(ctx, sig);

    /* TODO: Generate function body from AST */
    /* For now, emit a simple implementation */
    if (sig->param_count == 2 && sig->func_name) {
        const char* op = sig->func_name;
        char* result_type = get_c_type_name(sig->return_type);

        emit_spec(ctx, "%s result = ", result_type);

        /* Simple arithmetic for demo */
        if (strcmp(op, "+") == 0) {
            emit_spec_raw(ctx, "param0 + param1;\n");
        } else if (strcmp(op, "-") == 0) {
            emit_spec_raw(ctx, "param0 - param1;\n");
        } else if (strcmp(op, "*") == 0) {
            emit_spec_raw(ctx, "param0 * param1;\n");
        } else if (strcmp(op, "/") == 0) {
            emit_spec_raw(ctx, "param0 / param1;\n");
        } else if (strcmp(op, "<") == 0) {
            emit_spec_raw(ctx, "param0 < param1;\n");
        } else if (strcmp(op, ">") == 0) {
            emit_spec_raw(ctx, "param0 > param1;\n");
        } else if (strcmp(op, "<=") == 0) {
            emit_spec_raw(ctx, "param0 <= param1;\n");
        } else if (strcmp(op, ">=") == 0) {
            emit_spec_raw(ctx, "param0 >= param1;\n");
        } else if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
            emit_spec_raw(ctx, "param0 == param1;\n");
        } else {
            emit_spec_raw(ctx, "param0; /* TODO: implement %s */\n", op);
        }

        free(result_type);

        /* Emit epilogue */
        generate_spec_epilogue(ctx, sig, "result");
    } else {
        /* Generic fallback */
        emit_spec(ctx, "/* TODO: implement %s */\n", sig->func_name);
        generate_spec_epilogue(ctx, sig, NULL);
    }

    /* Mark as generated */
    spec_db_mark_generated(sig);
    ctx->spec_count++;
}

/* ============== Unboxed Operations ============== */

void generate_unboxed_binop(SpecCodegenContext* ctx,
                           const char* op,
                           const char* left,
                           const char* right,
                           ConcreteType* result_type,
                           const char* result_var) {
    if (!op || !left || !right || !result_var) return;

    char* c_type = get_c_type_name(result_type);
    emit_spec(ctx, "%s %s = ", c_type, result_var);
    free(c_type);

    /* Emit the operation */
    if (strcmp(op, "+") == 0) {
        emit_spec_raw(ctx, "%s + %s;\n", left, right);
    } else if (strcmp(op, "-") == 0) {
        emit_spec_raw(ctx, "%s - %s;\n", left, right);
    } else if (strcmp(op, "*") == 0) {
        emit_spec_raw(ctx, "%s * %s;\n", left, right);
    } else if (strcmp(op, "/") == 0) {
        emit_spec_raw(ctx, "%s / %s;\n", left, right);
    } else if (strcmp(op, "mod") == 0 || strcmp(op, "%") == 0) {
        emit_spec_raw(ctx, "%s %% %s;\n", left, right);
    } else if (strcmp(op, "<") == 0) {
        emit_spec_raw(ctx, "%s < %s;\n", left, right);
    } else if (strcmp(op, ">") == 0) {
        emit_spec_raw(ctx, "%s > %s;\n", left, right);
    } else if (strcmp(op, "<=") == 0) {
        emit_spec_raw(ctx, "%s <= %s;\n", left, right);
    } else if (strcmp(op, ">=") == 0) {
        emit_spec_raw(ctx, "%s >= %s;\n", left, right);
    } else if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
        emit_spec_raw(ctx, "%s == %s;\n", left, right);
    } else if (strcmp(op, "!=") == 0) {
        emit_spec_raw(ctx, "%s != %s;\n", left, right);
    } else {
        emit_spec_raw(ctx, "%s /* op: %s */;\n", left, op);
    }
}

/* ============== Box/Unbox Operations ============== */

bool type_needs_boxing(ConcreteType* type) {
    return type_is_unboxable(type);
}

char* get_unbox_function(ConcreteType* type) {
    if (!type) return strdup("unbox_any");

    switch (type->kind) {
        case TYPE_KIND_PRIMITIVE:
            switch (type->primitive.prim) {
                case PRIMITIVE_INT64:
                    return strdup("unbox_int");
                case PRIMITIVE_FLOAT64:
                    return strdup("unbox_float");
                case PRIMITIVE_CHAR:
                    return strdup("unbox_char");
                case PRIMITIVE_BOOL:
                    return strdup("unbox_bool");
                default:
                    return strdup("unbox_int");
            }

        default:
            return strdup("unbox_any");
    }
}

char* get_box_function(ConcreteType* type) {
    if (!type) return strdup("box_any");

    switch (type->kind) {
        case TYPE_KIND_PRIMITIVE:
            switch (type->primitive.prim) {
                case PRIMITIVE_INT64:
                    return strdup("box_int");
                case PRIMITIVE_FLOAT64:
                    return strdup("box_float");
                case PRIMITIVE_CHAR:
                    return strdup("box_char");
                case PRIMITIVE_BOOL:
                    return strdup("box_bool");
                default:
                    return strdup("box_int");
            }

        default:
            return strdup("box_any");
    }
}

void generate_unbox(SpecCodegenContext* ctx,
                   const char* obj_var,
                   ConcreteType* type,
                   const char* unboxed_var) {
    if (!obj_var || !type || !unboxed_var) return;

    char* c_type = get_c_type_name(type);
    char* unbox_fn = get_unbox_function(type);

    emit_spec(ctx, "%s %s = %s(%s);\n", c_type, unboxed_var, unbox_fn, obj_var);

    free(c_type);
    free(unbox_fn);
}

void generate_box(SpecCodegenContext* ctx,
                 const char* unboxed_var,
                 ConcreteType* type,
                 const char* obj_var) {
    if (!unboxed_var || !type || !obj_var) return;

    char* box_fn = get_box_function(type);

    emit_spec(ctx, "Obj* %s = %s(%s);\n", obj_var, box_fn, unboxed_var);

    free(box_fn);
}

void generate_unbox_decl(SpecCodegenContext* ctx, ConcreteType* type) {
    if (!type) return;

    char* c_type = get_c_type_name(type);
    char* unbox_fn = get_unbox_function(type);

    emit_spec_raw(ctx, "%s %s(Obj* obj);\n", c_type, unbox_fn);

    free(c_type);
    free(unbox_fn);
}

void generate_box_decl(SpecCodegenContext* ctx, ConcreteType* type) {
    if (!type) return;

    char* box_fn = get_box_function(type);

    emit_spec_raw(ctx, "Obj* %s(%s value);\n", box_fn, get_c_type_name(type));

    free(box_fn);
}

/* ============== Call Generation ============== */

void generate_specialized_call(SpecCodegenContext* ctx,
                              SpecSignature* sig,
                              const char** args,
                              const char* result_var) {
    if (!sig || !args) return;

    char* ret_type = get_c_type_name(sig->return_type);

    if (result_var) {
        emit_spec(ctx, "%s %s = ", ret_type, result_var);
    }

    emit_spec_raw(ctx, "%s(", sig->mangled_name);

    for (int i = 0; i < sig->param_count; i++) {
        if (i > 0) emit_spec_raw(ctx, ", ");
        emit_spec_raw(ctx, "%s", args[i]);
    }

    emit_spec_raw(ctx, ");\n");

    free(ret_type);
}

void generate_generic_call(SpecCodegenContext* ctx,
                          const char* func_name,
                          const char** args,
                          int arg_count,
                          const char* result_var) {
    if (!func_name || !args) return;

    if (result_var) {
        emit_spec(ctx, "Obj* %s = ", result_var);
    }

    emit_spec_raw(ctx, "%s(", func_name);

    for (int i = 0; i < arg_count; i++) {
        if (i > 0) emit_spec_raw(ctx, ", ");
        emit_spec_raw(ctx, "%s", args[i]);
    }

    emit_spec_raw(ctx, ");\n");
}

void generate_dispatch_call(SpecCodegenContext* ctx,
                           const char* func_name,
                           const char** args,
                           ConcreteType** arg_types,
                           int arg_count,
                           const char* result_var) {
    if (!ctx->spec_db) {
        generate_generic_call(ctx, func_name, args, arg_count, result_var);
        return;
    }

    /* Try to find a matching specialization */
    SpecSignature* sig = spec_db_find_match(ctx->spec_db, func_name,
                                           arg_types, arg_count);

    if (sig) {
        generate_specialized_call(ctx, sig, args, result_var);
    } else {
        generate_generic_call(ctx, func_name, args, arg_count, result_var);
    }
}
