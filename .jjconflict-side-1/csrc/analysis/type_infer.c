/*
 * type_infer.c - Type Inference Implementation
 *
 * Implementation of type inference for specialization.
 * Part of Phase 27: Julia-Level Type Specialization.
 *
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 1)
 */

#include "type_infer.h"
#include "analysis.h"
#include "../ast/ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============== Helper Functions ============== */

/* Check if OmniValue is a specific tag */
static bool is_tag(OmniValue* expr, OmniTag tag) {
    return expr && expr->tag == tag;
}

/* ============== Literal Type Inference ============== */

ConcreteType* infer_literal(OmniValue* expr) {
    if (!expr) return concrete_type_any();

    switch (expr->tag) {
        case OMNI_INT:
            return concrete_type_primitive(PRIMITIVE_INT64, 64);

        case OMNI_FLOAT:
            return concrete_type_primitive(PRIMITIVE_FLOAT64, 64);

        case OMNI_CHAR:
            return concrete_type_primitive(PRIMITIVE_CHAR, 32);

        case OMNI_STRING:
            /* TODO: String as array of chars */
            return concrete_type_any();

        case OMNI_NIL:
            /* Empty list - could be List(Any) */
            return concrete_type_any();

        default:
            return concrete_type_any();
    }
}

/* ============== Variable Type Inference ============== */

ConcreteType* infer_var(TypeEnv* env, const char* var_name) {
    if (!env || !var_name) return concrete_type_any();

    ConcreteType* type = type_env_lookup(env, var_name);
    if (type) {
        concrete_type_inc_ref(type);
        return type;
    }

    return concrete_type_any();
}

/* ============== Operation Classification ============== */

bool is_arithmetic_op(const char* op) {
    if (!op) return false;

    return strcmp(op, "+") == 0 ||
           strcmp(op, "-") == 0 ||
           strcmp(op, "*") == 0 ||
           strcmp(op, "/") == 0 ||
           strcmp(op, "mod") == 0 ||
           strcmp(op, "quotient") == 0 ||
           strcmp(op, "remainder") == 0 ||
           strcmp(op, "abs") == 0 ||
           strcmp(op, "sqrt") == 0 ||
           strcmp(op, "sin") == 0 ||
           strcmp(op, "cos") == 0 ||
           strcmp(op, "tan") == 0 ||
           strcmp(op, "exp") == 0 ||
           strcmp(op, "log") == 0;
}

bool is_comparison_op(const char* op) {
    if (!op) return false;

    return strcmp(op, "<") == 0 ||
           strcmp(op, ">") == 0 ||
           strcmp(op, "<=") == 0 ||
           strcmp(op, ">=") == 0 ||
           strcmp(op, "=") == 0 ||
           strcmp(op, "!=") == 0;
}

/* ============== Type Compatibility ============== */

bool type_is_compatible(ConcreteType* a, ConcreteType* b) {
    if (!a || !b) return false;
    if (a == b) return true;
    if (a->kind != b->kind) return false;

    /* Same kind - check details */
    switch (a->kind) {
        case TYPE_KIND_PRIMITIVE:
            /* All primitives are compatible with each other (via promotion) */
            return true;

        case TYPE_KIND_ARRAY:
            /* Arrays are compatible if elements are compatible */
            return a->array.rank == b->array.rank &&
                   a->array.is_mutable == b->array.is_mutable &&
                   type_is_compatible(a->array.element_type,
                                     b->array.element_type);

        case TYPE_KIND_CLOSURE:
            /* Closures are compatible if signatures match */
            if (a->closure.param_count != b->closure.param_count) {
                return false;
            }
            for (int i = 0; i < a->closure.param_count; i++) {
                if (!type_is_compatible(a->closure.param_types[i],
                                       b->closure.param_types[i])) {
                    return false;
                }
            }
            return type_is_compatible(a->closure.return_type,
                                     b->closure.return_type);

        case TYPE_KIND_ANY:
            return true;
    }

    return false;
}

bool type_is_primitive(ConcreteType* type, PrimitiveType prim) {
    if (!type || type->kind != TYPE_KIND_PRIMITIVE) {
        return false;
    }
    return type->primitive.prim == prim;
}

ConcreteType* type_common_type(ConcreteType* a, ConcreteType* b) {
    if (!a || !b) return concrete_type_any();
    if (concrete_type_equals(a, b)) {
        concrete_type_inc_ref(a);
        return a;
    }

    /* Numeric promotion rules */
    if (type_is_numeric(a) && type_is_numeric(b)) {
        /* Float wins over Int */
        if (a->primitive.prim == PRIMITIVE_FLOAT64 ||
            b->primitive.prim == PRIMITIVE_FLOAT64) {
            return concrete_type_primitive(PRIMITIVE_FLOAT64, 64);
        }
        /* Both Int */
        return concrete_type_primitive(PRIMITIVE_INT64, 64);
    }

    return concrete_type_any();
}

/* ============== Binary Operation Type Inference ============== */

ConcreteType* compute_binop_result(const char* op,
                                   ConcreteType* left_type,
                                   ConcreteType* right_type) {
    if (!op) return concrete_type_any();
    if (!left_type || !right_type) return concrete_type_any();

    /* Comparison operations always return Bool */
    if (is_comparison_op(op)) {
        return concrete_type_primitive(PRIMITIVE_BOOL, 1);
    }

    /* Arithmetic operations */
    if (is_arithmetic_op(op)) {
        if (type_is_numeric(left_type) && type_is_numeric(right_type)) {
            /* Result is the "common" type (promote Int+Float to Float) */
            if (left_type->primitive.prim == PRIMITIVE_FLOAT64 ||
                right_type->primitive.prim == PRIMITIVE_FLOAT64) {
                return concrete_type_primitive(PRIMITIVE_FLOAT64, 64);
            }
            return concrete_type_primitive(PRIMITIVE_INT64, 64);
        }
    }

    return concrete_type_any();
}

ConcreteType* infer_binop(AnalysisContext* ctx,
                         TypeEnv* env,
                         const char* op,
                         OmniValue* left,
                         OmniValue* right) {
    if (!op) return concrete_type_any();

    /* Infer operand types */
    ConcreteType* left_type = infer_expr(ctx, env, left);
    ConcreteType* right_type = infer_expr(ctx, env, right);

    /* Compute result type */
    ConcreteType* result = compute_binop_result(op, left_type, right_type);

    concrete_type_dec_ref(left_type);
    concrete_type_dec_ref(right_type);

    return result;
}

/* ============== Expression Type Inference ============== */

ConcreteType* infer_expr(AnalysisContext* ctx, TypeEnv* env, OmniValue* expr) {
    if (!expr) return concrete_type_any();

    switch (expr->tag) {
        case OMNI_INT:
        case OMNI_FLOAT:
        case OMNI_CHAR:
        case OMNI_STRING:
        case OMNI_NIL:
            return infer_literal(expr);

        case OMNI_SYM:
            return infer_var(env, expr->str_val);

        case OMNI_CELL: {
            /* Function application: (func arg1 arg2 ...) */
            OmniValue* func_expr = expr->cell.car;
            OmniValue* args = expr->cell.cdr;

            return infer_apply(ctx, env, func_expr, args);
        }

        case OMNI_LAMBDA:
        case OMNI_REC_LAMBDA: {
            return infer_lambda(ctx, env,
                              expr->lambda.params,
                              expr->lambda.body);
        }

        default:
            return concrete_type_any();
    }
}

ConcreteType* infer_apply(AnalysisContext* ctx,
                         TypeEnv* env,
                         OmniValue* func,
                         OmniValue* args) {
    if (!func) return concrete_type_any();

    /* First, infer the type of the function expression */
    ConcreteType* func_type = infer_expr(ctx, env, func);

    /* If we know it's a closure, return its return type */
    if (func_type && func_type->kind == TYPE_KIND_CLOSURE) {
        ConcreteType* return_type = func_type->closure.return_type;
        concrete_type_inc_ref(return_type);
        concrete_type_dec_ref(func_type);
        return return_type;
    }

    concrete_type_dec_ref(func_type);

    /* For primitives, try to infer from arguments */
    if (func->tag == OMNI_SYM && is_tag(args, OMNI_CELL)) {
        const char* op = func->str_val;

        /* Binary operation */
        OmniValue* left = args->cell.car;
        OmniValue* rest = args->cell.cdr;

        if (is_tag(rest, OMNI_CELL)) {
            OmniValue* right = rest->cell.car;
            return infer_binop(ctx, env, op, left, right);
        }
    }

    return concrete_type_any();
}

ConcreteType* infer_lambda(AnalysisContext* ctx,
                          TypeEnv* env,
                          OmniValue* params,
                          OmniValue* body) {
    /* TODO: Full lambda type inference with param types */
    /* For now, return a generic closure type */
    return concrete_type_closure(NULL, 0, concrete_type_any());
}

ConcreteType* infer_if(AnalysisContext* ctx,
                      TypeEnv* env,
                      OmniValue* condition,
                      OmniValue* then_branch,
                      OmniValue* else_branch) {
    if (!then_branch) return concrete_type_any();

    /* Infer types of both branches */
    ConcreteType* then_type = infer_expr(ctx, env, then_branch);
    ConcreteType* else_type = concrete_type_any();

    if (else_branch) {
        else_type = infer_expr(ctx, env, else_branch);
    }

    /* Return the common type */
    ConcreteType* result = type_common_type(then_type, else_type);

    concrete_type_dec_ref(then_type);
    concrete_type_dec_ref(else_type);

    return result;
}

ConcreteType* infer_let(AnalysisContext* ctx,
                       TypeEnv* env,
                       OmniValue* bindings,
                       OmniValue* body) {
    /* bindings should be a list of (var value) pairs */
    if (!bindings || !body) return infer_expr(ctx, env, body);

    /* TODO: Process binding pairs and add to environment */
    /* For now, just infer body type */
    return infer_expr(ctx, env, body);
}

ConcreteType* infer_type_literal(OmniValue* type_lit) {
    /* Type literals like {Int}, {Float}, etc. */
    /* TODO: Parse type literal and return corresponding ConcreteType */
    return concrete_type_any();
}

/* ============== Utility Functions ============== */

ConcreteType* tag_to_concrete_type(OmniTag tag) {
    switch (tag) {
        case OMNI_INT:
            return concrete_type_primitive(PRIMITIVE_INT64, 64);

        case OMNI_FLOAT:
            return concrete_type_primitive(PRIMITIVE_FLOAT64, 64);

        case OMNI_CHAR:
            return concrete_type_primitive(PRIMITIVE_CHAR, 32);

        case OMNI_STRING:
            /* TODO: Array of Char */
            return concrete_type_any();

        case OMNI_NIL:
            return concrete_type_any();

        case OMNI_ARRAY:
            /* TODO: Typed array */
            return concrete_type_any();

        case OMNI_LAMBDA:
        case OMNI_REC_LAMBDA:
            return concrete_type_closure(NULL, 0, concrete_type_any());

        default:
            return concrete_type_any();
    }
}

/* ============== Debug Functions ============== */

void debug_print_type(ConcreteType* type, const char* prefix) {
    if (!prefix) prefix = "";

    if (!type) {
        printf("%s(NULL)\n", prefix);
        return;
    }

    char* type_str = concrete_type_to_string(type);
    printf("%s%s\n", prefix, type_str);
    free(type_str);
}

void debug_print_env(TypeEnv* env, const char* prefix) {
    if (!prefix) prefix = "";

    if (!env) {
        printf("%s(NULL environment)\n", prefix);
        return;
    }

    printf("%sTypeEnvironment:\n", prefix);

    int indent = (int)strlen(prefix) + 2;
    char indent_str[64];
    snprintf(indent_str, sizeof(indent_str), "%*s", indent, "");

    TypeBinding* binding = env->bindings;
    while (binding) {
        char* type_str = concrete_type_to_string(binding->type);
        printf("%s%s -> %s\n", indent_str, binding->var_name, type_str);
        free(type_str);
        binding = binding->next;
    }

    if (env->parent) {
        printf("%s(parent:\n", indent_str);
        debug_print_env(env->parent, indent_str);
        printf("%s)\n", indent_str);
    }
}
