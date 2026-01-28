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
            /* Phase 1: String literal type propagation
             * Strings are represented as arrays of characters.
             * For now, we use a specialized String type (Array Char) */
            return concrete_type_array(
                concrete_type_primitive(PRIMITIVE_CHAR, 32),
                1,    /* rank = 1 (vector) */
                false /* immutable */
            );

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

// REVIEWED:NAIVE
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

// REVIEWED:NAIVE
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

// REVIEWED:NAIVE
        case TYPE_KIND_UNION:
            /* Unions are compatible if all members are compatible */
            if (a->type_union.member_count != b->type_union.member_count) {
                return false;
            }
            for (int i = 0; i < a->type_union.member_count; i++) {
                bool found = false;
                for (int j = 0; j < b->type_union.member_count; j++) {
                    if (type_is_compatible(a->type_union.member_types[i],
                                          b->type_union.member_types[j])) {
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            }
            return true;

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

/*
 * type_is_subtype: Check if type_a is a subtype of type_b
 *
 * Implements proper subtype checking with variance support for parametric types.
 *
 * Subtype rules:
 *   1. Any type is a supertype of all types: T <: Any
 *   2. Numeric hierarchy: Int <: Number, Float <: Number
 *   3. Array types are covariant: Array{T} <: Array{S} if T <: S
 *   4. Function types use contravariant params, covariant return:
 *      Fn{[A...], R} <: Fn{[B...], S} if B[i] <: A[i] and R <: S
 *   5. Union subtyping: T <: Union{T, U, ...} and Union{T1, T2} <: U if all members <: U
 */
bool type_is_subtype(ConcreteType* a, ConcreteType* b) {
    /* Handle NULL cases */
    if (!a || !b) return false;

    /* Same type pointer is always subtype */
    if (a == b) return true;

    /* Rule 1: Everything is a subtype of Any */
    if (b->kind == TYPE_KIND_ANY) return true;

    /* If a is Any, it's only a subtype of itself (handled above) */
    if (a->kind == TYPE_KIND_ANY) return false;

    /* Rule 5a: T <: Union{..., T, ...} if T is a member of the union */
    if (b->kind == TYPE_KIND_UNION) {
        for (int i = 0; i < b->type_union.member_count; i++) {
            if (type_is_subtype(a, b->type_union.member_types[i])) {
                return true;
            }
        }
        return false;
    }

    /* Rule 5b: Union{T1, T2} <: U if T1 <: U and T2 <: U */
    if (a->kind == TYPE_KIND_UNION) {
        for (int i = 0; i < a->type_union.member_count; i++) {
            if (!type_is_subtype(a->type_union.member_types[i], b)) {
                return false;
            }
        }
        return true;
    }

    /* Different kinds (after union handling) - check for specific cases */
    if (a->kind != b->kind) {
        return false;
    }

    /* Same kind - check structural subtyping based on kind */
    switch (a->kind) {
        case TYPE_KIND_PRIMITIVE: {
            /* Rule 2: Numeric hierarchy */
            /* Int64 and Float64 are siblings, not subtypes of each other */
            /* But both are subtypes of a conceptual "Number" type */
            /* Since we don't have a Number primitive, we check exact match */
            return a->primitive.prim == b->primitive.prim;
        }

        case TYPE_KIND_ARRAY: {
            /* Rule 3: Arrays are covariant in element type */
            /* Array{T} <: Array{S} if T <: S */
            /* Also require same rank and mutability compatibility */
            /* Immutable is subtype of mutable for read-only contexts */
            if (a->array.rank != b->array.rank) {
                return false;
            }
            /* Mutable array is NOT a subtype of immutable array (would break immutability) */
            /* Immutable array CAN be a subtype of mutable if treated as read-only */
            if (a->array.is_mutable && !b->array.is_mutable) {
                return false; /* Can't treat mutable as immutable (aliasing issues) */
            }
            return type_is_subtype(a->array.element_type, b->array.element_type);
        }

        case TYPE_KIND_CLOSURE: {
            /* Rule 4: Function subtyping with contravariant params and covariant return */
            /* Fn{[A...], R} <: Fn{[B...], S} if:
             *   - same param count
             *   - B[i] <: A[i] for all i (contravariant)
             *   - R <: S (covariant)
             */
            if (a->closure.param_count != b->closure.param_count) {
                return false;
            }
            /* Contravariant parameter check: b's params must be subtypes of a's params */
            for (int i = 0; i < a->closure.param_count; i++) {
                if (!type_is_subtype(b->closure.param_types[i], a->closure.param_types[i])) {
                    return false;
                }
            }
            /* Covariant return type check */
            return type_is_subtype(a->closure.return_type, b->closure.return_type);
        }

        case TYPE_KIND_UNION:
            /* Already handled above */
            return false;

        case TYPE_KIND_ANY:
            /* Already handled above */
            return true;
    }

    return false;
}

ConcreteType* type_common_type(ConcreteType* a, ConcreteType* b) {
    if (!a || !b) return concrete_type_any();
    if (concrete_type_equals(a, b)) {
        concrete_type_inc_ref(a);
        return a;
    }

    /* If one is Any, result is Any */
    if (a->kind == TYPE_KIND_ANY || b->kind == TYPE_KIND_ANY) {
        return concrete_type_any();
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

    /* For different types, create a union type */
    /* First, flatten if either is already a union */
    int total_members = 0;
    if (a->kind == TYPE_KIND_UNION) {
        total_members += a->type_union.member_count;
    } else {
        total_members += 1;
    }
    if (b->kind == TYPE_KIND_UNION) {
        total_members += b->type_union.member_count;
    } else {
        total_members += 1;
    }

    ConcreteType** members = malloc(sizeof(ConcreteType*) * total_members);
    if (!members) return concrete_type_any();

    int idx = 0;
    if (a->kind == TYPE_KIND_UNION) {
        for (int i = 0; i < a->type_union.member_count; i++) {
            members[idx++] = a->type_union.member_types[i];
        }
    } else {
        members[idx++] = a;
    }
    if (b->kind == TYPE_KIND_UNION) {
        for (int i = 0; i < b->type_union.member_count; i++) {
            members[idx++] = b->type_union.member_types[i];
        }
    } else {
        members[idx++] = b;
    }

    ConcreteType* result = concrete_type_union(members, total_members);
    free(members);
    return result;
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

    /* Handle special forms by name */
    if (func->tag == OMNI_SYM && func->str_val) {
        const char* name = func->str_val;

        /* (if condition then-expr else-expr) */
        if (strcmp(name, "if") == 0 && is_tag(args, OMNI_CELL)) {
            OmniValue* condition = args->cell.car;
            OmniValue* rest = args->cell.cdr;
            OmniValue* then_branch = NULL;
            OmniValue* else_branch = NULL;

            if (is_tag(rest, OMNI_CELL)) {
                then_branch = rest->cell.car;
                OmniValue* else_part = rest->cell.cdr;
                if (is_tag(else_part, OMNI_CELL)) {
                    else_branch = else_part->cell.car;
                }
            }

            return infer_if(ctx, env, condition, then_branch, else_branch);
        }

        /* (let ((var val) ...) body) */
        if (strcmp(name, "let") == 0 && is_tag(args, OMNI_CELL)) {
            OmniValue* bindings = args->cell.car;
            OmniValue* body_list = args->cell.cdr;
            OmniValue* body = is_tag(body_list, OMNI_CELL) ? body_list->cell.car : NULL;
            return infer_let(ctx, env, bindings, body);
        }

        /* (match value [pattern result] ...) */
        if (strcmp(name, "match") == 0 && is_tag(args, OMNI_CELL)) {
            return infer_match(ctx, env, args);
        }

        /* (lambda (params) body) or (fn [params] body) */
        if ((strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0) &&
            is_tag(args, OMNI_CELL)) {
            OmniValue* params = args->cell.car;
            OmniValue* body_list = args->cell.cdr;
            OmniValue* body = is_tag(body_list, OMNI_CELL) ? body_list->cell.car : NULL;
            return infer_lambda(ctx, env, params, body);
        }
    }

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
    /* Phase 1: Lambda expression type propagation
     *
     * Syntax: (lambda (x y z) body) or (fn [x y z] body)
     *
     * We infer parameter types from:
     * 1. Type annotations: [x {Int} y {Float}]
     * 2. Usage in the body (advanced, not implemented yet)
     *
     * Return type is inferred from the body expression.
     */

    /* Count parameters and extract types */
    int param_count = 0;
    ConcreteType** param_types = NULL;

    if (params) {
        /* First pass: count parameters */
        if (is_tag(params, OMNI_CELL)) {
            /* List-style: (x y z) */
            OmniValue* p = params;
            while (p && is_tag(p, OMNI_CELL)) {
                OmniValue* param = p->cell.car;
                if (is_tag(param, OMNI_SYM)) {
                    param_count++;
                }
                p = p->cell.cdr;
            }
        } else if (is_tag(params, OMNI_ARRAY)) {
            /* Slot syntax: [x {Int} y {Float}] */
            for (size_t i = 0; i < params->array.len; i++) {
                OmniValue* elem = params->array.data[i];
                if (is_tag(elem, OMNI_SYM)) {
                    param_count++;
                }
            }
        }

        /* Allocate parameter types array */
        if (param_count > 0) {
            param_types = malloc(sizeof(ConcreteType*) * param_count);
            for (int i = 0; i < param_count; i++) {
                param_types[i] = NULL;
            }

            /* Second pass: extract types */
            int idx = 0;
            ConcreteType* pending_type = NULL;

            if (is_tag(params, OMNI_CELL)) {
                /* List-style: default to Any */
                for (int i = 0; i < param_count; i++) {
                    param_types[i] = concrete_type_any();
                }
            } else if (is_tag(params, OMNI_ARRAY)) {
                /* Slot syntax with possible type annotations */
                for (size_t i = 0; i < params->array.len; i++) {
                    OmniValue* elem = params->array.data[i];
                    if (is_tag(elem, OMNI_SYM)) {
                        /* This is a parameter name */
                        /* Check if next element is a type annotation */
                        if (i + 1 < params->array.len &&
                            is_tag(params->array.data[i + 1], OMNI_TYPE_LIT)) {
                            OmniValue* type_lit = params->array.data[i + 1];
                            param_types[idx] = infer_type_literal(type_lit);
                            i++;  /* Skip the type literal */
                        } else {
                            param_types[idx] = concrete_type_any();
                        }
                        idx++;
                    }
                }
            }
        }
    }

    /* Create scope for parameter bindings */
    TypeEnv* lambda_env = type_env_push(env);

    /* Bind parameters to their types */
    int idx = 0;
    if (params) {
        if (is_tag(params, OMNI_CELL)) {
            OmniValue* p = params;
            while (p && is_tag(p, OMNI_CELL) && idx < param_count) {
                OmniValue* param = p->cell.car;
                if (is_tag(param, OMNI_SYM) && param->str_val) {
                    concrete_type_inc_ref(param_types[idx]);
                    type_env_bind(lambda_env, param->str_val, param_types[idx]);
                    idx++;
                }
                p = p->cell.cdr;
            }
        } else if (is_tag(params, OMNI_ARRAY)) {
            for (size_t i = 0; i < params->array.len && idx < param_count; i++) {
                OmniValue* elem = params->array.data[i];
                if (is_tag(elem, OMNI_SYM) && elem->str_val) {
                    concrete_type_inc_ref(param_types[idx]);
                    type_env_bind(lambda_env, elem->str_val, param_types[idx]);
                    idx++;
                }
            }
        }
    }

    /* Infer return type from body */
    ConcreteType* return_type = body
        ? infer_expr(ctx, lambda_env, body)
        : concrete_type_any();

    /* Create closure type */
    ConcreteType* result = concrete_type_closure(param_types, param_count, return_type);

    /* Clean up - closure type takes ownership of param_types */
    type_env_free(lambda_env);

    return result;
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
    /* Phase 1: Let binding type inference with scope tracking
     *
     * Syntax: (let ((var1 expr1) (var2 expr2) ...) body)
     *
     * We create a new scope, infer the type of each binding expression,
     * and add the variable to the new scope with its inferred type.
     */
    if (!body) return concrete_type_any();
    if (!bindings) return infer_expr(ctx, env, body);

    /* Create new scope for let bindings */
    TypeEnv* let_env = type_env_push(env);

    /* Process binding pairs */
    OmniValue* binding_list = bindings;
    while (binding_list && is_tag(binding_list, OMNI_CELL)) {
        OmniValue* binding_pair = binding_list->cell.car;

        if (is_tag(binding_pair, OMNI_CELL)) {
            OmniValue* var = binding_pair->cell.car;
            OmniValue* value_expr = NULL;

            /* Get value expression from (var value) or (var) */
            if (is_tag(binding_pair->cell.cdr, OMNI_CELL)) {
                value_expr = binding_pair->cell.cdr->cell.car;
            }

            if (is_tag(var, OMNI_SYM) && var->str_val) {
                /* Infer type of value expression */
                ConcreteType* value_type = value_expr
                    ? infer_expr(ctx, let_env, value_expr)
                    : concrete_type_any();

                /* Bind variable to its inferred type */
                type_env_bind(let_env, var->str_val, value_type);
            }
        } else if (is_tag(binding_pair, OMNI_ARRAY)) {
            /* Slot syntax: [var value] */
            if (binding_pair->array.len >= 1) {
                OmniValue* var = binding_pair->array.data[0];
                OmniValue* value_expr = binding_pair->array.len >= 2
                    ? binding_pair->array.data[1]
                    : NULL;

                if (is_tag(var, OMNI_SYM) && var->str_val) {
                    ConcreteType* value_type = value_expr
                        ? infer_expr(ctx, let_env, value_expr)
                        : concrete_type_any();
                    type_env_bind(let_env, var->str_val, value_type);
                }
            }
        }

        binding_list = binding_list->cell.cdr;
    }

    /* Infer body type with the new scope */
    ConcreteType* result = infer_expr(ctx, let_env, body);

    /* Pop the scope */
    type_env_free(let_env);

    return result;
}

ConcreteType* infer_match(AnalysisContext* ctx,
                         TypeEnv* env,
                         OmniValue* args) {
    /*
     * Infer the type of a match expression.
     *
     * Syntax: (match value
     *           [pattern1 result1]
     *           [pattern2 result2]
     *           ...)
     *
     * Or old style: (match value (pattern1) result1 (pattern2) result2 ...)
     *
     * The type of a match is the common type of all branch results.
     * If branches have different types, we return Any (union type).
     */
    if (!args || !is_tag(args, OMNI_CELL)) {
        return concrete_type_any();
    }

    /* Skip the value expression - we don't need it for type inference */
    OmniValue* clauses = args->cell.cdr;

    /* Collect all result types */
    ConcreteType* common_type = NULL;
    int branch_count = 0;

    while (clauses && is_tag(clauses, OMNI_CELL)) {
        OmniValue* clause = clauses->cell.car;
        OmniValue* result_expr = NULL;

        if (is_tag(clause, OMNI_ARRAY)) {
            /* Array-based clause: [pattern result] or [pattern :when guard result] */
            size_t len = clause->array.len;
            if (len >= 2) {
                /* Result is the last element */
                result_expr = clause->array.data[len - 1];
            }
        } else if (is_tag(clause, OMNI_CELL)) {
            /* Old style: (pattern result) */
            OmniValue* result_part = clause->cell.cdr;
            if (is_tag(result_part, OMNI_CELL)) {
                result_expr = result_part->cell.car;
            }
        } else {
            /* Old style interleaved: pattern result pattern result ... */
            /* The current item might be a pattern, get the next for result */
            OmniValue* next = clauses->cell.cdr;
            if (is_tag(next, OMNI_CELL)) {
                result_expr = next->cell.car;
                /* Skip both pattern and result */
                clauses = next;
            }
        }

        if (result_expr) {
            ConcreteType* branch_type = infer_expr(ctx, env, result_expr);
            branch_count++;

            if (!common_type) {
                common_type = branch_type;
            } else {
                ConcreteType* merged = type_common_type(common_type, branch_type);
                concrete_type_dec_ref(common_type);
                concrete_type_dec_ref(branch_type);
                common_type = merged;
            }
        }

        clauses = clauses->cell.cdr;
    }

    if (!common_type) {
        return concrete_type_any();
    }

    return common_type;
}

ConcreteType* infer_type_literal(OmniValue* type_lit) {
    /*
     * Parse type literals to ConcreteType.
     *
     * Supported type literals:
     *   {Int}         -> PRIMITIVE_INT64
     *   {Int64}       -> PRIMITIVE_INT64
     *   {Float}       -> PRIMITIVE_FLOAT64
     *   {Float64}     -> PRIMITIVE_FLOAT64
     *   {Char}        -> PRIMITIVE_CHAR
     *   {Bool}        -> PRIMITIVE_BOOL
     *   {String}      -> Array Char (immutable)
     *   {Array T}     -> Array of T (mutable)
     *   {Vector T}    -> Array of T (immutable)
     *   {Any}         -> TYPE_KIND_ANY
     *   {Nothing}     -> TYPE_KIND_ANY (unit type)
     *   {Fn [T1 T2] R} -> Closure type
     */
    if (!type_lit || type_lit->tag != OMNI_TYPE_LIT) {
        return concrete_type_any();
    }

    const char* name = type_lit->type_lit.type_name;
    if (!name) {
        return concrete_type_any();
    }

    /* Primitive types */
    if (strcmp(name, "Int") == 0 || strcmp(name, "Int64") == 0) {
        return concrete_type_primitive(PRIMITIVE_INT64, 64);
    }
    if (strcmp(name, "Float") == 0 || strcmp(name, "Float64") == 0) {
        return concrete_type_primitive(PRIMITIVE_FLOAT64, 64);
    }
    if (strcmp(name, "Char") == 0) {
        return concrete_type_primitive(PRIMITIVE_CHAR, 32);
    }
    if (strcmp(name, "Bool") == 0) {
        return concrete_type_primitive(PRIMITIVE_BOOL, 1);
    }

    /* String is Array Char (immutable) */
    if (strcmp(name, "String") == 0) {
        return concrete_type_array(
            concrete_type_primitive(PRIMITIVE_CHAR, 32),
            1,      /* rank = 1 (vector) */
            false   /* immutable */
        );
    }

    /* Any and Nothing */
    if (strcmp(name, "Any") == 0 || strcmp(name, "Nothing") == 0) {
        return concrete_type_any();
    }

    /* Array and Vector - parameterized types */
    if (strcmp(name, "Array") == 0 || strcmp(name, "Vector") == 0) {
        bool is_mutable = (strcmp(name, "Array") == 0);
        ConcreteType* element_type = concrete_type_any();

        /* Get element type from first parameter */
        if (type_lit->type_lit.param_count > 0 && type_lit->type_lit.params) {
            OmniValue* param = type_lit->type_lit.params[0];
            if (param) {
                if (param->tag == OMNI_TYPE_LIT) {
                    element_type = infer_type_literal(param);
                } else if (param->tag == OMNI_SYM && param->str_val) {
                    /* Simple type name as symbol: {Array Int} */
                    if (strcmp(param->str_val, "Int") == 0 ||
                        strcmp(param->str_val, "Int64") == 0) {
                        element_type = concrete_type_primitive(PRIMITIVE_INT64, 64);
                    } else if (strcmp(param->str_val, "Float") == 0 ||
                               strcmp(param->str_val, "Float64") == 0) {
                        element_type = concrete_type_primitive(PRIMITIVE_FLOAT64, 64);
                    } else if (strcmp(param->str_val, "Char") == 0) {
                        element_type = concrete_type_primitive(PRIMITIVE_CHAR, 32);
                    } else if (strcmp(param->str_val, "Bool") == 0) {
                        element_type = concrete_type_primitive(PRIMITIVE_BOOL, 1);
                    }
                }
            }
        }

        return concrete_type_array(element_type, 1, is_mutable);
    }

    /* Function type: {Fn [Param1 Param2] ReturnType} */
    if (strcmp(name, "Fn") == 0 || strcmp(name, "Function") == 0) {
        ConcreteType** param_types = NULL;
        int param_count = 0;
        ConcreteType* return_type = concrete_type_any();

        /* First parameter is an array of parameter types */
        if (type_lit->type_lit.param_count >= 1 && type_lit->type_lit.params) {
            OmniValue* params_arr = type_lit->type_lit.params[0];
            if (params_arr && params_arr->tag == OMNI_ARRAY) {
                param_count = (int)params_arr->array.len;
                if (param_count > 0) {
                    param_types = malloc(sizeof(ConcreteType*) * param_count);
                    for (int i = 0; i < param_count; i++) {
                        OmniValue* p = params_arr->array.data[i];
                        if (p && p->tag == OMNI_TYPE_LIT) {
                            param_types[i] = infer_type_literal(p);
                        } else if (p && p->tag == OMNI_SYM && p->str_val) {
                            /* Simple type name */
                            OmniValue temp = { .tag = OMNI_TYPE_LIT };
                            temp.type_lit.type_name = p->str_val;
                            temp.type_lit.params = NULL;
                            temp.type_lit.param_count = 0;
                            param_types[i] = infer_type_literal(&temp);
                        } else {
                            param_types[i] = concrete_type_any();
                        }
                    }
                }
            }
        }

        /* Second parameter is return type */
        if (type_lit->type_lit.param_count >= 2 && type_lit->type_lit.params) {
            OmniValue* ret = type_lit->type_lit.params[1];
            if (ret && ret->tag == OMNI_TYPE_LIT) {
                return_type = infer_type_literal(ret);
            } else if (ret && ret->tag == OMNI_SYM && ret->str_val) {
                OmniValue temp = { .tag = OMNI_TYPE_LIT };
                temp.type_lit.type_name = ret->str_val;
                temp.type_lit.params = NULL;
                temp.type_lit.param_count = 0;
                return_type = infer_type_literal(&temp);
            }
        }

        return concrete_type_closure(param_types, param_count, return_type);
    }

    /* Unknown type - return Any */
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
