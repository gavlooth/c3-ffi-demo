/*
 * type_env.c - Type Environment Implementation
 *
 * Implementation of type environment for tracking concrete types during analysis.
 * Part of Phase 27: Julia-Level Type Specialization.
 *
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 1)
 */

#include "type_env.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <string.h>

/* strdup is not in C99 standard - provide implementation */
static char* omni_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

#define strdup omni_strdup

/* ============== Singleton Types ============== */

/* Singleton "any" type for unknown/generic types */
static ConcreteType* g_any_type = NULL;

/* ============== Concrete Type Implementation ============== */

ConcreteType* concrete_type_primitive(PrimitiveType prim, int bit_width) {
    ConcreteType* type = malloc(sizeof(ConcreteType));
    if (!type) return NULL;

    type->kind = TYPE_KIND_PRIMITIVE;
    type->primitive.prim = prim;
    type->primitive.bit_width = bit_width;
    type->ref_count = 1;

    return type;
}

ConcreteType* concrete_type_array(ConcreteType* element_type,
                                  int rank,
                                  bool is_mutable) {
    if (!element_type) return NULL;

    ConcreteType* type = malloc(sizeof(ConcreteType));
    if (!type) return NULL;

    type->kind = TYPE_KIND_ARRAY;
    type->array.element_type = element_type;
    type->array.rank = rank;
    type->array.is_mutable = is_mutable;
    type->ref_count = 1;

    /* Take ownership of element_type */
    concrete_type_inc_ref(element_type);

    return type;
}

ConcreteType* concrete_type_closure(ConcreteType** param_types,
                                    int param_count,
                                    ConcreteType* return_type) {
    if (!return_type) return NULL;
    if (param_count > 0 && !param_types) return NULL;

    ConcreteType* type = malloc(sizeof(ConcreteType));
    if (!type) return NULL;

    type->kind = TYPE_KIND_CLOSURE;
    type->closure.param_types = param_types;
    type->closure.param_count = param_count;
    type->closure.return_type = return_type;
    type->ref_count = 1;

    /* Take ownership of all types */
    for (int i = 0; i < param_count; i++) {
        if (param_types[i]) {
            concrete_type_inc_ref(param_types[i]);
        }
    }
    concrete_type_inc_ref(return_type);

    return type;
}

ConcreteType* concrete_type_union(ConcreteType** member_types, int member_count) {
    /* Degenerate cases */
    if (member_count <= 0 || !member_types) {
        return concrete_type_any();
    }
    if (member_count == 1) {
        concrete_type_inc_ref(member_types[0]);
        return member_types[0];
    }

    /* Check if all members are the same type - simplify to single type */
    bool all_same = true;
    for (int i = 1; i < member_count && all_same; i++) {
        if (!concrete_type_equals(member_types[0], member_types[i])) {
            all_same = false;
        }
    }
    if (all_same) {
        concrete_type_inc_ref(member_types[0]);
        return member_types[0];
    }

    /* Create union type */
    ConcreteType* type = malloc(sizeof(ConcreteType));
    if (!type) return concrete_type_any();

    type->kind = TYPE_KIND_UNION;

    // REVIEWED:NAIVE
    /* Copy and deduplicate member types */
    type->type_union.member_types = malloc(sizeof(ConcreteType*) * member_count);
    if (!type->type_union.member_types) {
        free(type);
        return concrete_type_any();
    }

    int unique_count = 0;
    for (int i = 0; i < member_count; i++) {
        bool is_duplicate = false;
        for (int j = 0; j < unique_count; j++) {
            if (concrete_type_equals(type->type_union.member_types[j], member_types[i])) {
                is_duplicate = true;
                break;
            }
        }
        if (!is_duplicate && member_types[i]) {
            type->type_union.member_types[unique_count] = member_types[i];
            concrete_type_inc_ref(member_types[i]);
            unique_count++;
        }
    }

    type->type_union.member_count = unique_count;
    type->ref_count = 1;

    /* If only one unique type after deduplication, simplify */
    if (unique_count == 1) {
        ConcreteType* result = type->type_union.member_types[0];
        concrete_type_inc_ref(result);
        concrete_type_dec_ref(type);
        return result;
    }

    return type;
}

ConcreteType* concrete_type_any(void) {
    if (!g_any_type) {
        g_any_type = malloc(sizeof(ConcreteType));
        if (!g_any_type) return NULL;

        g_any_type->kind = TYPE_KIND_ANY;
        g_any_type->ref_count = 1;  /* Never freed (singleton) */
    }
    return g_any_type;
}

void concrete_type_inc_ref(ConcreteType* type) {
    if (type && type != g_any_type) {
        type->ref_count++;
    }
}

void concrete_type_dec_ref(ConcreteType* type) {
    if (!type || type == g_any_type) return;

    type->ref_count--;
    if (type->ref_count <= 0) {
        /* Free nested types based on kind */
        switch (type->kind) {
            case TYPE_KIND_ARRAY:
                concrete_type_dec_ref(type->array.element_type);
                break;

            case TYPE_KIND_CLOSURE:
                for (int i = 0; i < type->closure.param_count; i++) {
                    concrete_type_dec_ref(type->closure.param_types[i]);
                }
                free(type->closure.param_types);
                concrete_type_dec_ref(type->closure.return_type);
                break;

            case TYPE_KIND_UNION:
                for (int i = 0; i < type->type_union.member_count; i++) {
                    concrete_type_dec_ref(type->type_union.member_types[i]);
                }
                free(type->type_union.member_types);
                break;

            case TYPE_KIND_PRIMITIVE:
            case TYPE_KIND_ANY:
                /* No nested types */
                break;
        }

        free(type);
    }
}

bool concrete_type_equals(ConcreteType* a, ConcreteType* b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return false;

    switch (a->kind) {
        case TYPE_KIND_PRIMITIVE:
            return a->primitive.prim == b->primitive.prim &&
                   a->primitive.bit_width == b->primitive.bit_width;

        case TYPE_KIND_ARRAY:
            return a->array.rank == b->array.rank &&
                   a->array.is_mutable == b->array.is_mutable &&
                   concrete_type_equals(a->array.element_type,
                                      b->array.element_type);

        case TYPE_KIND_CLOSURE:
            if (a->closure.param_count != b->closure.param_count) {
                return false;
            }
            if (!concrete_type_equals(a->closure.return_type,
                                     b->closure.return_type)) {
                return false;
            }
            for (int i = 0; i < a->closure.param_count; i++) {
                if (!concrete_type_equals(a->closure.param_types[i],
                                         b->closure.param_types[i])) {
                    return false;
                }
            }
            return true;

// REVIEWED:NAIVE
        case TYPE_KIND_UNION:
            if (a->type_union.member_count != b->type_union.member_count) {
                return false;
            }
            /* Check if all members in a exist in b (order-independent) */
            for (int i = 0; i < a->type_union.member_count; i++) {
                bool found = false;
                for (int j = 0; j < b->type_union.member_count; j++) {
                    if (concrete_type_equals(a->type_union.member_types[i],
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

char* concrete_type_to_string(ConcreteType* type) {
    if (!type) {
        return strdup("NULL");
    }

    char buf[256];

    switch (type->kind) {
        case TYPE_KIND_PRIMITIVE:
            snprintf(buf, sizeof(buf), "%s%d",
                     primitive_type_to_string(type->primitive.prim),
                     type->primitive.bit_width);
            break;

        case TYPE_KIND_ARRAY: {
            char* elem_str = concrete_type_to_string(type->array.element_type);
            snprintf(buf, sizeof(buf), "%s%s %dD",
                     type->array.is_mutable ? "Array" : "Vector",
                     elem_str,
                     type->array.rank);
            free(elem_str);
            break;
        }

        case TYPE_KIND_CLOSURE: {
            /* Build parameter list string */
            char params[200] = "";
            int offset = 0;
            for (int i = 0; i < type->closure.param_count; i++) {
                char* param_str = concrete_type_to_string(type->closure.param_types[i]);
                offset += snprintf(params + offset, sizeof(params) - offset,
                                  "%s%s", i > 0 ? " " : "", param_str);
                free(param_str);
            }

            char* ret_str = concrete_type_to_string(type->closure.return_type);
            snprintf(buf, sizeof(buf), "(%s) -> %s", params, ret_str);
            free(ret_str);
            break;
        }

        case TYPE_KIND_UNION: {
            /* Build union type string: T1 | T2 | T3 */
            int offset = 0;
            for (int i = 0; i < type->type_union.member_count && offset < 200; i++) {
                char* member_str = concrete_type_to_string(type->type_union.member_types[i]);
                offset += snprintf(buf + offset, sizeof(buf) - offset,
                                  "%s%s", i > 0 ? " | " : "", member_str);
                free(member_str);
            }
            break;
        }

        case TYPE_KIND_ANY:
            snprintf(buf, sizeof(buf), "Any");
            break;
    }

    return strdup(buf);
}

/* ============== Type Environment Implementation ============== */

TypeEnv* type_env_new(TypeEnv* parent) {
    TypeEnv* env = malloc(sizeof(TypeEnv));
    if (!env) return NULL;

    env->bindings = NULL;
    env->parent = parent;

    return env;
}

void type_env_free(TypeEnv* env) {
    if (!env) return;

    /* Free all bindings in this scope */
    TypeBinding* binding = env->bindings;
    while (binding) {
        TypeBinding* next = binding->next;

        free(binding->var_name);
        concrete_type_dec_ref(binding->type);

        free(binding);
        binding = next;
    }

    free(env);
}

void type_env_bind(TypeEnv* env, const char* var_name, ConcreteType* type) {
    if (!env || !var_name) return;

    /* Check if variable already exists in this scope */
    TypeBinding* binding = env->bindings;
    while (binding) {
        if (strcmp(binding->var_name, var_name) == 0) {
            /* Replace existing binding */
            concrete_type_dec_ref(binding->type);
            binding->type = type;
            return;
        }
        binding = binding->next;
    }

    /* Create new binding */
    binding = malloc(sizeof(TypeBinding));
    if (!binding) return;

    binding->var_name = strdup(var_name);
    binding->type = type;
    binding->next = env->bindings;

    env->bindings = binding;
}

/* TESTED */
// REVIEWED:NAIVE
ConcreteType* type_env_lookup(TypeEnv* env, const char* var_name) {
    if (!env || !var_name) return NULL;

    /* Search current scope */
    TypeBinding* binding = env->bindings;
    while (binding) {
        if (strcmp(binding->var_name, var_name) == 0) {
            return binding->type;
        }
        binding = binding->next;
    }

    /* Search parent scope */
    if (env->parent) {
        return type_env_lookup(env->parent, var_name);
    }

    return NULL;
}

TypeEnv* type_env_push(TypeEnv* parent) {
    return type_env_new(parent);
}

TypeEnv* type_env_pop(TypeEnv* child) {
    if (!child) return NULL;

    TypeEnv* parent = child->parent;

    /* Free child but NOT parent */
    type_env_free(child);

    return parent;
}

bool type_env_defined_in_current_scope(TypeEnv* env, const char* var_name) {
    if (!env || !var_name) return false;

    TypeBinding* binding = env->bindings;
    while (binding) {
        if (strcmp(binding->var_name, var_name) == 0) {
            return true;
        }
        binding = binding->next;
    }

    return false;
}

char** type_env_list_vars(TypeEnv* env, size_t* out_count) {
    if (!env) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    /* Count bindings */
    size_t count = 0;
    TypeBinding* binding = env->bindings;
    while (binding) {
        count++;
        binding = binding->next;
    }

    if (count == 0) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    /* Allocate array */
    char** vars = malloc(sizeof(char*) * (count + 1));
    if (!vars) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    /* Fill array */
    binding = env->bindings;
    for (size_t i = 0; i < count; i++) {
        vars[i] = strdup(binding->var_name);
        binding = binding->next;
    }
    vars[count] = NULL;

    if (out_count) *out_count = count;
    return vars;
}

/* ============== Utility Functions ============== */

PrimitiveType primitive_type_from_string(const char* name) {
    if (!name) return PRIMITIVE_INT64;

    if (strcmp(name, "Int") == 0 ||
        strcmp(name, "Int64") == 0 ||
        strcmp(name, "int") == 0 ||
        strcmp(name, "long") == 0) {
        return PRIMITIVE_INT64;
    }

    if (strcmp(name, "Float") == 0 ||
        strcmp(name, "Float64") == 0 ||
        strcmp(name, "double") == 0) {
        return PRIMITIVE_FLOAT64;
    }

    if (strcmp(name, "Char") == 0 ||
        strcmp(name, "char") == 0) {
        return PRIMITIVE_CHAR;
    }

    if (strcmp(name, "Bool") == 0 ||
        strcmp(name, "bool") == 0) {
        return PRIMITIVE_BOOL;
    }

    return PRIMITIVE_INT64;  /* Default */
}

const char* primitive_type_to_string(PrimitiveType prim) {
    switch (prim) {
        case PRIMITIVE_INT64:   return "Int";
        case PRIMITIVE_FLOAT64: return "Float";
        case PRIMITIVE_CHAR:    return "Char";
        case PRIMITIVE_BOOL:    return "Bool";
        default:               return "Unknown";
    }
}

int primitive_type_bit_width(PrimitiveType prim) {
    switch (prim) {
        case PRIMITIVE_INT64:   return 64;
        case PRIMITIVE_FLOAT64: return 64;
        case PRIMITIVE_CHAR:    return 32;
        case PRIMITIVE_BOOL:    return 1;
        default:               return 64;
    }
}

bool type_is_numeric(ConcreteType* type) {
    if (!type || type->kind != TYPE_KIND_PRIMITIVE) {
        return false;
    }
    return type->primitive.prim == PRIMITIVE_INT64 ||
           type->primitive.prim == PRIMITIVE_FLOAT64;
}

bool type_is_unboxable(ConcreteType* type) {
    if (!type) return false;
    return type->kind == TYPE_KIND_PRIMITIVE;
}
