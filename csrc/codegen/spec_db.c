/*
 * spec_db.c - Specialization Database Implementation
 *
 * Implementation of specialization database for tracking function
 * specializations during code generation.
 *
 * Reference: docs/TYPE_SPECIALIZATION_DESIGN.md (Phase 2)
 */

#include "spec_db.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* strdup for C99 */
static char* omni_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}
#define strdup omni_strdup

/* Hash table size (prime number for better distribution) */
#define HASH_TABLE_SIZE 257

/* ============== Hash Function ============== */

unsigned int spec_hash_signature(const char* func_name,
                                ConcreteType** param_types,
                                int param_count) {
    unsigned int hash = 5381;
    if (func_name) {
        for (const char* p = func_name; *p; p++) {
            hash = ((hash << 5) + hash) + (unsigned char)*p;
        }
    }

    /* Incorporate parameter types */
    for (int i = 0; i < param_count; i++) {
        if (param_types && param_types[i]) {
            hash = ((hash << 5) + hash) + (unsigned int)param_types[i]->kind;
            if (param_types[i]->kind == TYPE_KIND_PRIMITIVE) {
                hash = ((hash << 5) + hash) + (unsigned int)param_types[i]->primitive.prim;
            }
        }
    }

    return hash;
}

/* ============== SpecDB API ============== */

SpecDB* spec_db_new(void) {
    SpecDB* db = malloc(sizeof(SpecDB));
    if (!db) return NULL;

    db->table_size = HASH_TABLE_SIZE;
    db->sig_table = calloc(db->table_size, sizeof(SpecSignature*));
    if (!db->sig_table) {
        free(db);
        return NULL;
    }

    db->signatures = NULL;
    db->count = 0;

    return db;
}

void spec_db_free(SpecDB* db) {
    if (!db) return;

    /* Free all signatures */
    SpecSignature* sig = db->signatures;
    while (sig) {
        SpecSignature* next = sig->next;

        free(sig->func_name);
        free(sig->mangled_name);

        /* Free param_types array (but not individual types) */
        free(sig->param_types);

        /* Dec ref on return type */
        if (sig->return_type) {
            concrete_type_dec_ref(sig->return_type);
        }

        free(sig);
        sig = next;
    }

    /* Free hash table */
    free(db->sig_table);
    free(db);
}

char* spec_mangle_name(const char* func_name,
                      ConcreteType** param_types,
                      int param_count) {
    if (!func_name) return NULL;

    /* Calculate required buffer size */
    size_t len = strlen(func_name) + 2;  /* name + "_" + null */
    for (int i = 0; i < param_count; i++) {
        if (param_types && param_types[i]) {
            char* type_str = concrete_type_to_string(param_types[i]);
            if (type_str) {
                len += strlen(type_str) + 1;  /* "_" + type */
                free(type_str);
            }
        }
    }

    /* Build mangled name */
    char* mangled = malloc(len);
    if (!mangled) return NULL;

    strcpy(mangled, func_name);
    strcat(mangled, "_");

    for (int i = 0; i < param_count; i++) {
        if (param_types && param_types[i]) {
            char* type_str = concrete_type_to_string(param_types[i]);
            if (type_str) {
                // REVIEWED:NAIVE
                /* Remove spaces from type string for mangled name */
                char* p = type_str;
                while (*p) {
                    if (*p != ' ' && *p != '[' && *p != ']' && *p != '(' && *p != ')' && *p != '-') {
                        strncat(mangled, p, 1);
                    }
                    p++;
                }
                free(type_str);
            }
        }
    }

    return mangled;
}

static bool signatures_equal(SpecSignature* a,
                            const char* func_name,
                            ConcreteType** param_types,
                            int param_count) {
    if (!a->func_name || !func_name) return false;
    if (strcmp(a->func_name, func_name) != 0) return false;
    if (a->param_count != param_count) return false;

    for (int i = 0; i < param_count; i++) {
        ConcreteType* t1 = a->param_types ? a->param_types[i] : NULL;
        ConcreteType* t2 = param_types ? param_types[i] : NULL;

        if (!concrete_type_equals(t1, t2)) {
            return false;
        }
    }

    return true;
}

SpecSignature* spec_db_register(SpecDB* db,
                                const char* func_name,
                                ConcreteType** param_types,
                                int param_count,
                                ConcreteType* return_type,
                                bool is_builtin) {
    if (!db || !func_name) return NULL;

    /* Check if already exists */
    SpecSignature* existing = spec_db_lookup(db, func_name, param_types, param_count);
    if (existing) {
        /* Update return type if different */
        if (return_type && !concrete_type_equals(existing->return_type, return_type)) {
            concrete_type_dec_ref(existing->return_type);
            existing->return_type = return_type;
            concrete_type_inc_ref(return_type);
        }
        return existing;
    }

    /* Create new signature */
    SpecSignature* sig = malloc(sizeof(SpecSignature));
    if (!sig) return NULL;

    sig->func_name = strdup(func_name);
    sig->param_types = param_types;  /* Take ownership */
    sig->param_count = param_count;
    sig->return_type = return_type;
    sig->mangled_name = spec_mangle_name(func_name, param_types, param_count);
    sig->is_generated = false;
    sig->is_builtin = is_builtin;
    sig->next = db->signatures;

    /* Increment ref on return type */
    if (return_type) {
        concrete_type_inc_ref(return_type);
    }

    /* Add to linked list */
    db->signatures = sig;
    db->count++;

    /* Add to hash table */
    unsigned int hash = spec_hash_signature(func_name, param_types, param_count);
    unsigned int index = hash % db->table_size;
    sig->next = db->sig_table[index];
    db->sig_table[index] = sig;

    return sig;
}

// REVIEWED:NAIVE
SpecSignature* spec_db_lookup(SpecDB* db,
                              const char* func_name,
                              ConcreteType** param_types,
                              int param_count) {
    if (!db || !func_name) return NULL;

    unsigned int hash = spec_hash_signature(func_name, param_types, param_count);
    unsigned int index = hash % db->table_size;

    SpecSignature* sig = db->sig_table[index];
    while (sig) {
        if (signatures_equal(sig, func_name, param_types, param_count)) {
            return sig;
        }
        sig = sig->next;
    }

    return NULL;
}

// REVIEWED:NAIVE
SpecSignature* spec_db_find_match(SpecDB* db,
                                 const char* func_name,
                                 ConcreteType** arg_types,
                                 int arg_count) {
    if (!db || !func_name) return NULL;

    /* Search all signatures for this function */
    SpecSignature* sig = db->signatures;
    while (sig) {
        if (sig->func_name && strcmp(sig->func_name, func_name) == 0) {
            if (sig->param_count == arg_count) {
                /* Check if parameter types match */
                bool match = true;
                for (int i = 0; i < arg_count && match; i++) {
                    ConcreteType* sig_param = sig->param_types ? sig->param_types[i] : NULL;
                    ConcreteType* arg_type = arg_types ? arg_types[i] : NULL;

                    /* Match if types are equal OR arg type is unknown */
                    if (!concrete_type_equals(sig_param, arg_type) &&
                        arg_type && arg_type->kind != TYPE_KIND_ANY) {
                        match = false;
                    }
                }

                if (match) {
                    return sig;
                }
            }
        }
        sig = sig->next;
    }

    return NULL;
}

// REVIEWED:NAIVE
SpecSignature** spec_db_get_all(SpecDB* db,
                               const char* func_name,
                               int* out_count) {
    if (!db || !func_name) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    /* Count matching signatures */
    int count = 0;
    SpecSignature* sig = db->signatures;
    while (sig) {
        if (sig->func_name && strcmp(sig->func_name, func_name) == 0) {
            count++;
        }
        sig = sig->next;
    }

    if (count == 0) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    /* Allocate array */
    SpecSignature** result = malloc(sizeof(SpecSignature*) * (count + 1));
    if (!result) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    /* Fill array */
    int idx = 0;
    sig = db->signatures;
    while (sig && idx < count) {
        if (sig->func_name && strcmp(sig->func_name, func_name) == 0) {
            result[idx++] = sig;
        }
        sig = sig->next;
    }
    result[count] = NULL;

    if (out_count) *out_count = count;
    return result;
}

void spec_db_mark_generated(SpecSignature* sig) {
    if (sig) {
        sig->is_generated = true;
    }
}

bool spec_db_is_generated(SpecSignature* sig) {
    return sig && sig->is_generated;
}

int spec_db_table_size(SpecDB* db) {
    return db ? db->table_size : 0;
}

int spec_db_count(SpecDB* db) {
    return db ? db->count : 0;
}
