/*
 * OmniLisp Analysis Implementation
 *
 * Static analysis passes for ASAP memory management.
 */

#include "analysis.h"
#include "type_id.h"  /* Phase 24: Type ID constants */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============== Context Management ============== */

AnalysisContext* omni_analysis_new(void) {
    AnalysisContext* ctx = malloc(sizeof(AnalysisContext));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(AnalysisContext));

    /* Initialize hash maps for O(1) lookups */
    ctx->var_map = strmap_new();
    ctx->escape_map = strmap_new();
    ctx->owner_map = strmap_new();

    return ctx;
}

static void free_var_usages(VarUsage* u) {
    while (u) {
        VarUsage* next = u->next;
        free(u->name);
        free(u);
        u = next;
    }
}

static void free_escape_info(EscapeInfo* e) {
    while (e) {
        EscapeInfo* next = e->next;
        free(e->name);
        free(e);
        e = next;
    }
}

static void free_owner_info(OwnerInfo* o) {
    while (o) {
        OwnerInfo* next = o->next;
        free(o->name);
        free(o);
        o = next;
    }
}

static void free_shape_info(ShapeInfo* s) {
    while (s) {
        ShapeInfo* next = s->next;
        free(s->type_name);
        for (size_t i = 0; i < s->back_edge_count; i++) {
            free(s->back_edge_fields[i]);
        }
        free(s->back_edge_fields);
        free(s);
        s = next;
    }
}

static void free_reuse_candidates(ReuseCandidate* r) {
    while (r) {
        ReuseCandidate* next = r->next;
        free(r->freed_var);
        free(r->type_name);
        free(r);
        r = next;
    }
}

static void free_regions(RegionInfo* r) {
    while (r) {
        RegionInfo* next = r->next;
        free(r->name);
        for (size_t i = 0; i < r->var_count; i++) {
            free(r->variables[i]);
        }
        free(r->variables);
        free(r);
        r = next;
    }
}

static void free_rc_elision(RCElisionInfo* e) {
    while (e) {
        RCElisionInfo* next = e->next;
        free(e->var_name);
        free(e);
        e = next;
    }
}

static void free_borrows(BorrowInfo* b) {
    while (b) {
        BorrowInfo* next = b->next;
        free(b->borrowed_var);
        free(b->borrow_holder);
        free(b);
        b = next;
    }
}

static void free_tethers(TetherPoint* t) {
    while (t) {
        TetherPoint* next = t->next;
        free(t->tethered_var);
        free(t);
        t = next;
    }
}

static void free_param_summaries(ParamSummary* p) {
    while (p) {
        ParamSummary* next = p->next;
        free(p->name);
        free(p->type_annotation);  /* Free type annotation string if present */
        free(p);
        p = next;
    }
}

static void free_function_summaries(FunctionSummary* f) {
    while (f) {
        FunctionSummary* next = f->next;
        free(f->name);
        free(f->return_type);      /* Free return type annotation if present */
        free_param_summaries(f->params);
        free(f);
        f = next;
    }
}

static void free_thread_locality(ThreadLocalityInfo* t) {
    while (t) {
        ThreadLocalityInfo* next = t->next;
        free(t->var_name);
        free(t);
        t = next;
    }
}

static void free_thread_spawns(ThreadSpawnInfo* s) {
    while (s) {
        ThreadSpawnInfo* next = s->next;
        free(s->thread_id);
        for (size_t i = 0; i < s->captured_count; i++) {
            free(s->captured_vars[i]);
        }
        free(s->captured_vars);
        free(s->capture_locality);
        free(s);
        s = next;
    }
}

/* DIRECTIVE: NO CHANNELS - channel_ops tracking removed */

static void free_components(ComponentInfo* c) {
    while (c) {
        ComponentInfo* next = c->next;
        for (size_t i = 0; i < c->handle_count; i++) {
            free(c->handles[i]);
        }
        free(c->handles);
        free(c);
        c = next;
    }
}

static void free_active_handles(AnalysisContext* ctx) {
    if (!ctx->active_handles) return;
    for (size_t i = 0; i < ctx->handle_count; i++) {
        free(ctx->active_handles[i]);
    }
    free(ctx->active_handles);
    ctx->active_handles = NULL;
    ctx->handle_count = 0;
}

void omni_analysis_free(AnalysisContext* ctx) {
    if (!ctx) return;
    free_var_usages(ctx->var_usages);
    free_escape_info(ctx->escape_info);
    free_owner_info(ctx->owner_info);
    free_shape_info(ctx->shape_info);
    free_components(ctx->components);
    free_active_handles(ctx);
    free_reuse_candidates(ctx->reuse_candidates);
    free_regions(ctx->regions);
    free_rc_elision(ctx->rc_elision);
    free_borrows(ctx->borrows);
    free_tethers(ctx->tethers);
    free_function_summaries(ctx->function_summaries);
    free_thread_locality(ctx->thread_locality);
    free_thread_spawns(ctx->thread_spawns);
    /* DIRECTIVE: NO CHANNELS - channel_ops removed */
    if (ctx->type_registry) {
        omni_type_registry_free(ctx->type_registry);
    }
    omni_free_constructor_owners(ctx);
    /* Free hash maps for O(1) lookups */
    if (ctx->var_map) strmap_free(ctx->var_map);
    if (ctx->escape_map) strmap_free(ctx->escape_map);
    if (ctx->owner_map) strmap_free(ctx->owner_map);
    free(ctx);
}

/* ============== Variable Usage Tracking ============== */

// REVIEWED:OPTIMIZED - O(1) hash lookup instead of O(n) linear scan
static VarUsage* find_or_create_var_usage(AnalysisContext* ctx, const char* name) {
    /* O(1) hash lookup */
    if (ctx->var_map) {
        VarUsage* cached = (VarUsage*)strmap_get(ctx->var_map, name);
        if (cached) return cached;
    }

    VarUsage* u = malloc(sizeof(VarUsage));
    u->name = strdup(name);
    u->flags = VAR_USAGE_NONE;
    u->first_use = -1;
    u->last_use = -1;
    u->def_pos = -1;
    u->is_param = false;
    /* OPTIMIZATION (T-opt-region-metadata-compiler): Initialize type_id */
    u->type_id = TYPE_ID_GENERIC;  /* Default to generic until type inference */
    /* Issue 3 P2: Track which region owns this variable */
    u->region_id = (ctx->current_region) ? ctx->current_region->region_id : 0;
    u->next = ctx->var_usages;
    ctx->var_usages = u;

    /* Add to hash map for O(1) future lookups */
    if (ctx->var_map) {
        strmap_put(ctx->var_map, name, u);
    }
    return u;
}

static void mark_var_read(AnalysisContext* ctx, const char* name) {
    VarUsage* u = find_or_create_var_usage(ctx, name);
    u->flags |= VAR_USAGE_READ;
    if (u->first_use < 0) u->first_use = ctx->position;
    u->last_use = ctx->position;
}

static void mark_var_write(AnalysisContext* ctx, const char* name) {
    VarUsage* u = find_or_create_var_usage(ctx, name);
    u->flags |= VAR_USAGE_WRITE;
    if (u->def_pos < 0) u->def_pos = ctx->position;
}

static void mark_var_captured(AnalysisContext* ctx, const char* name) {
    VarUsage* u = find_or_create_var_usage(ctx, name);
    u->flags |= VAR_USAGE_CAPTURED;
}

static void mark_var_escaped(AnalysisContext* ctx, const char* name) {
    VarUsage* u = find_or_create_var_usage(ctx, name);
    u->flags |= VAR_USAGE_ESCAPED;
}

/* ============== Escape Analysis ============== */

// REVIEWED:OPTIMIZED - O(1) hash lookup instead of O(n) linear scan
static EscapeInfo* find_or_create_escape_info(AnalysisContext* ctx, const char* name) {
    /* O(1) hash lookup */
    if (ctx->escape_map) {
        EscapeInfo* cached = (EscapeInfo*)strmap_get(ctx->escape_map, name);
        if (cached) return cached;
    }

    EscapeInfo* e = malloc(sizeof(EscapeInfo));
    e->name = strdup(name);
    e->escape_class = ESCAPE_NONE;
    e->is_unique = true;
    e->next = ctx->escape_info;
    ctx->escape_info = e;

    /* Add to hash map for O(1) future lookups */
    if (ctx->escape_map) {
        strmap_put(ctx->escape_map, name, e);
    }
    return e;
}

static void set_escape_class(AnalysisContext* ctx, const char* name, EscapeClass ec) {
    EscapeInfo* e = find_or_create_escape_info(ctx, name);
    /* Take the maximum escape class */
    if (ec > e->escape_class) {
        e->escape_class = ec;
    }
}

/* ============== Ownership Analysis ============== */

// REVIEWED:OPTIMIZED - O(1) hash lookup instead of O(n) linear scan
static OwnerInfo* find_or_create_owner_info(AnalysisContext* ctx, const char* name) {
    /* O(1) hash lookup */
    if (ctx->owner_map) {
        OwnerInfo* cached = (OwnerInfo*)strmap_get(ctx->owner_map, name);
        if (cached) return cached;
    }

    OwnerInfo* o = malloc(sizeof(OwnerInfo));
    o->name = strdup(name);
    o->ownership = OWNER_LOCAL;
    o->must_free = true;
    o->free_pos = -1;
    o->is_unique = true;      /* Assume unique until proven otherwise */
    o->is_static_scc = false;
    o->shape = SHAPE_UNKNOWN; /* Will be refined by shape analysis */
    o->alloc_strategy = ALLOC_HEAP; /* Default to heap, refined by escape analysis */
    o->next = ctx->owner_info;
    ctx->owner_info = o;

    /* Add to hash map for O(1) future lookups */
    if (ctx->owner_map) {
        strmap_put(ctx->owner_map, name, o);
    }
    return o;
}

/* ============== Expression Analysis ============== */

static void analyze_expr(AnalysisContext* ctx, OmniValue* expr);

static void analyze_symbol(AnalysisContext* ctx, OmniValue* expr) {
    const char* name = expr->str_val;
    mark_var_read(ctx, name);

    if (ctx->in_lambda) {
        /* Variable might be captured by closure */
        VarUsage* u = find_or_create_var_usage(ctx, name);
        if (u->def_pos < 0 || ctx->scope_depth > 0) {
            /* Defined in outer scope - captured */
            mark_var_captured(ctx, name);
            set_escape_class(ctx, name, ESCAPE_CLOSURE);
        }
    }

    if (ctx->in_return_position) {
        mark_var_escaped(ctx, name);
        set_escape_class(ctx, name, ESCAPE_RETURN);
    }

    ctx->position++;
}

/* Helper: Extract parameter name from a Slot or symbol
 * Returns NULL if not a valid parameter form
 * Supports:
 *   - Plain symbol: x
 *   - Slot array: [x]
 *   - Slot with type: [x {Int}]
 */
static OmniValue* extract_param_name(OmniValue* param) {
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

static void analyze_define(AnalysisContext* ctx, OmniValue* expr) {
    /* OmniLisp define forms (Strict Character Calculus):
     *   - Type definitions: (define {abstract Name} [])
     *   - Type definitions: (define ^:parent {Any} {abstract Number} [])
     *   - Type definitions: (define {primitive Float64} [64])
     *   - Type definitions: (define {struct Point} [x {Float}] [y {Float}])
     *   - Type definitions: (define {struct [^:covar T]} {List} [head {T}] [tail {List T}])
     *   - Simple define: (define x value)
     *   - Slot shorthand: (define f x y body)
     *   - Slot syntax: (define f [x] [y] body)
     *   - Slot with types: (define f [x {Int}] [y {String}] body)
     *   - Mixed syntax: (define f x [y {Int}] z body)
     *   - With metadata: (define (with-meta (^:parent {Number}) {abstract Real}) [])
     *
     * NOTE: Scheme-style (define (f x y) body) is NOT supported.
     */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    /*
     * Phase 22: Extract definition-level metadata.
     *
     * OmniLisp uses ^:key markers as *prefix modifiers* in definitions:
     *   (define ^:parent {Any} {abstract Number} [])
     *
     * These are NOT part of the "name/signature" proper; they must be peeled off
     * before we decide whether this is a type definition or a function/value define.
     */
    MetadataEntry* metadata = NULL;

    OmniValue* scan = args;
    while (!omni_is_nil(scan) && omni_is_cell(scan)) {
        OmniValue* item = omni_car(scan);

        /* Check for metadata marker (^:parent, ^:where, etc.). */
        if (!(omni_is_sym(item) && item->str_val &&
              item->str_val[0] == '^' && item->str_val[1] == ':')) {
            break;
        }

        /* Metadata markers are key/value pairs: ^:key <value>. */
        OmniValue* value_cell = omni_cdr(scan);
        if (omni_is_nil(value_cell) || !omni_is_cell(value_cell)) {
            /* Malformed key without a value; stop to avoid crashing. */
            break;
        }

        const char* meta_key = item->str_val + 2; /* Skip "^:" */
        OmniValue* meta_value = omni_car(value_cell);

        /*
         * Deterministic "double metadata" semantics:
         *   We scan left-to-right but PREPEND entries. This makes the last textual
         *   occurrence the first entry in the linked list, and omni_get_metadata()
         *   returns the first match => last-wins.
         */
        MetadataEntry* entry = malloc(sizeof(MetadataEntry));
        entry->key = strdup(meta_key);

        if (strcmp(meta_key, "parent") == 0) entry->type = META_PARENT;
        else if (strcmp(meta_key, "where") == 0) entry->type = META_WHERE;
        else if (strcmp(meta_key, "mutable") == 0) entry->type = META_MUTABLE;
        else if (strcmp(meta_key, "covar") == 0) entry->type = META_COVAR;
        else if (strcmp(meta_key, "contra") == 0) entry->type = META_CONTRA;
        else if (strcmp(meta_key, "seq") == 0) entry->type = META_SEQ;
        else if (strcmp(meta_key, "rec") == 0) entry->type = META_REC;
        else entry->type = META_NONE;

        entry->value = meta_value;
        entry->next = metadata;
        metadata = entry;

        /* Skip both the key and its value. */
        scan = omni_cdr(value_cell);
    }

    /* First non-metadata element is the "name or signature" position. */
    OmniValue* name_or_sig = omni_is_cell(scan) ? omni_car(scan) : omni_nil;
    OmniValue* rest = omni_is_cell(scan) ? omni_cdr(scan) : omni_nil;

    /*
     * Alternate representation: explicit (with-meta ...) wrapper.
     * Example from comment header:
     *   (define (with-meta (^:parent {Number}) {abstract Real}) [])
     */
    if (omni_is_cell(name_or_sig) && omni_is_sym(omni_car(name_or_sig))) {
        OmniValue* first = omni_car(name_or_sig);
        if (strcmp(first->str_val, "with-meta") == 0) {
            /* Extract metadata from (with-meta meta obj) */
            OmniValue* meta_pair = omni_cdr(name_or_sig);
            if (!omni_is_nil(meta_pair) && omni_is_cell(meta_pair)) {
                OmniValue* meta_expr = omni_car(meta_pair);
                MetadataEntry* extracted = omni_extract_metadata(meta_expr);

                /*
                 * Merge extracted metadata in front of prefix metadata.
                 * Since the (with-meta ...) wrapper occurs after any peeled prefix pairs,
                 * this preserves "last-wins" at the definition level.
                 */
                if (extracted) {
                    MetadataEntry* tail = extracted;
                    while (tail->next) tail = tail->next;
                    tail->next = metadata;
                    metadata = extracted;
                }

                /* Get the actual object (name_or_sig without metadata) */
                OmniValue* obj_pair = omni_cdr(meta_pair);
                if (!omni_is_nil(obj_pair) && omni_is_cell(obj_pair)) {
                    name_or_sig = omni_car(obj_pair);
                }
            }
        }
    }

    /* Phase 22: Type Definition Support */
    /* Check if this is a type definition: (define {Kind Name} params...) */
    if (omni_is_type_lit(name_or_sig)) {
        const char* kind = name_or_sig->type_lit.type_name;

        /* Extract metadata that may appear before the type literal */
        /* Example: ^:parent {Number} {abstract Int} [] */
        /* The parser should have already attached metadata to the expr */

        /* Handle abstract types: (define {abstract Name} []) */
        if (strcmp(kind, "abstract") == 0) {
            if (name_or_sig->type_lit.param_count < 1) {
                fprintf(stderr, "Error: abstract type requires a name\n");
                return;
            }
            OmniValue* name_param = name_or_sig->type_lit.params[0];
            const char* type_name = NULL;

            /* Name could be a type literal or symbol */
            if (omni_is_type_lit(name_param)) {
                type_name = name_param->type_lit.type_name;
            } else if (omni_is_sym(name_param)) {
                type_name = name_param->str_val;
            } else {
                fprintf(stderr, "Error: abstract type name must be a symbol or type literal\n");
                return;
            }

            /* Extract parent from metadata if present */
            const char* parent = "Any";  /* Default parent */
            if (metadata) {
                OmniValue* parent_value = omni_get_metadata(metadata, "parent");
                if (parent_value) {
                    if (omni_is_type_lit(parent_value)) {
                        parent = parent_value->type_lit.type_name;
                    } else if (omni_is_sym(parent_value)) {
                        parent = parent_value->str_val;
                    }
                }
            }

            /* Register the abstract type */
            omni_register_abstract_type(ctx, type_name, parent);

            /* Abstract types have no fields */
            ctx->position++;  /* Count the definition */
            return;
        }

        /* Handle primitive types: (define {primitive Name} [bit_width]) */
        if (strcmp(kind, "primitive") == 0) {
            if (name_or_sig->type_lit.param_count < 1) {
                fprintf(stderr, "Error: primitive type requires a name\n");
                return;
            }
            OmniValue* name_param = name_or_sig->type_lit.params[0];
            const char* type_name = NULL;

            if (omni_is_type_lit(name_param)) {
                type_name = name_param->type_lit.type_name;
            } else if (omni_is_sym(name_param)) {
                type_name = name_param->str_val;
            } else {
                fprintf(stderr, "Error: primitive type name must be a symbol or type literal\n");
                return;
            }

            /* Extract bit width from rest (should be an array) */
            int bit_width = 0;
            const char* parent = "Any";  /* Default parent for primitives */

            /* Extract parent from metadata if present */
            if (metadata) {
                OmniValue* parent_value = omni_get_metadata(metadata, "parent");
                if (parent_value) {
                    if (omni_is_type_lit(parent_value)) {
                        parent = parent_value->type_lit.type_name;
                    } else if (omni_is_sym(parent_value)) {
                        parent = parent_value->str_val;
                    }
                }
            }

            if (!omni_is_nil(rest) && omni_is_cell(rest)) {
                OmniValue* width_expr = omni_car(rest);
                if (omni_is_array(width_expr) && width_expr->array.len > 0) {
                    OmniValue* width_val = width_expr->array.data[0];
                    if (omni_is_int(width_val)) {
                        bit_width = (int)width_val->int_val;
                    }
                }
            }

            /* Register the primitive type */
            omni_register_primitive_type(ctx, type_name, parent, bit_width);

            ctx->position++;
            return;
        }

        /* Handle struct types: (define {struct Name} [field1 {Type1}] [field2 {Type2}] ...) */
        /* Or parametric: (define {struct [^:covar T]} {List} [head {T}] [tail {List T}]) */
        if (strcmp(kind, "struct") == 0) {
            if (name_or_sig->type_lit.param_count < 1) {
                fprintf(stderr, "Error: struct type requires a name\n");
                return;
            }
            OmniValue* name_param = name_or_sig->type_lit.params[0];
            const char* type_name = NULL;

            /* Name could be a symbol or type literal */
            if (omni_is_type_lit(name_param)) {
                type_name = name_param->type_lit.type_name;
            } else if (omni_is_sym(name_param)) {
                type_name = name_param->str_val;
            } else if (omni_is_array(name_param)) {
                /* Parametric type: [^:covar T] - extract type params */
                /* The name will be the second parameter to the struct type constructor */
                if (name_or_sig->type_lit.param_count >= 2) {
                    OmniValue* actual_name_param = name_or_sig->type_lit.params[1];
                    if (omni_is_type_lit(actual_name_param)) {
                        type_name = actual_name_param->type_lit.type_name;
                    } else if (omni_is_sym(actual_name_param)) {
                        type_name = actual_name_param->str_val;
                    }
                }
            }

            if (!type_name) {
                fprintf(stderr, "Error: struct type name must be a symbol or type literal\n");
                return;
            }

            /* Check if this is a parametric type by examining name_param */
            if (omni_is_array(name_param) && name_param->array.len >= 1) {
                /* Extract type parameters with variance from array */
                /* Example: [^:covar T] or [^:invariant T] */
                VarianceKind default_variance = VARIANCE_INVARIANT;
                for (size_t i = 0; i < name_param->array.len; i++) {
                    OmniValue* param_elem = name_param->array.data[i];

                    /* Check for variance metadata (^:covar or ^:contra) */
                    if (omni_is_sym(param_elem) && strlen(param_elem->str_val) > 1 &&
                        param_elem->str_val[0] == '^' && param_elem->str_val[1] == ':') {
                        const char* meta_key = param_elem->str_val + 2;  /* Skip "^:" */
                        if (strcmp(meta_key, "covar") == 0 || strcmp(meta_key, "covariant") == 0) {
                            default_variance = VARIANCE_COVARIANT;
                        } else if (strcmp(meta_key, "contra") == 0 || strcmp(meta_key, "contravariant") == 0) {
                            default_variance = VARIANCE_CONTRAVARIANT;
                        } else if (strcmp(meta_key, "invariant") == 0) {
                            default_variance = VARIANCE_INVARIANT;
                        }
                        continue;  /* Skip to next element (the actual type param) */
                    }

                    if (omni_is_sym(param_elem)) {
                        const char* param_name = param_elem->str_val;
                        /* Use the variance set by any preceding metadata */
                        TypeDef* temp_type = omni_get_type(ctx, type_name);
                        if (temp_type) {
                            omni_type_add_param(temp_type, param_name, default_variance);
                        }
                        /* Reset to invariant for next parameter */
                        default_variance = VARIANCE_INVARIANT;
                    }
                }
            }

            /* Register the struct type - pass the rest list which contains field definitions */
            const char* parent = "Any";  /* Default parent */

            /* Extract parent from metadata if present */
            if (metadata) {
                OmniValue* parent_value = omni_get_metadata(metadata, "parent");
                if (parent_value) {
                    if (omni_is_type_lit(parent_value)) {
                        parent = parent_value->type_lit.type_name;
                    } else if (omni_is_sym(parent_value)) {
                        parent = parent_value->str_val;
                    }
                }
            }

            omni_register_struct_type(ctx, type_name, parent, rest);

            ctx->position++;
            return;
        }

        /* Handle union type definitions: (define {TypeName} (union [{Type1} {Type2} ...])) */
        /* Check if rest contains a list starting with 'union */
        if (!omni_is_nil(rest) && omni_is_cell(rest)) {
            OmniValue* value_expr = omni_car(rest);
            if (omni_is_cell(value_expr) && omni_is_sym(omni_car(value_expr))) {
                const char* value_head = omni_car(value_expr)->str_val;
                if (strcmp(value_head, "union") == 0) {
                    /* This is a union type alias: (define {TypeName} (union [...])) */
                    const char* type_name = kind;
                    /* Extract union members from the rest of the list */
                    OmniValue* union_args = omni_cdr(value_expr);
                    if (!omni_is_nil(union_args) && omni_is_cell(union_args) &&
                        omni_is_array(omni_car(union_args))) {
                        OmniValue* members_array = omni_car(union_args);
                        /* Register the union type */
                        omni_register_union_type(ctx, type_name, members_array);
                        ctx->position++;
                        return;
                    }
                }
                /* Handle function type definitions: (define {TypeName} (fn [[params...] {return}])) */
                if (strcmp(value_head, "fn") == 0) {
                    /* This is a function type alias: (define {TypeName} (fn [[...] {...})) */
                    const char* type_name = kind;
                    /* Extract function type signature */
                    OmniValue* fn_args = omni_cdr(value_expr);
                    if (!omni_is_nil(fn_args) && omni_is_cell(fn_args) &&
                        omni_is_array(omni_car(fn_args))) {
                        OmniValue* sig_array = omni_car(fn_args);
                        /* Register the function type */
                        omni_register_function_type(ctx, type_name, sig_array);
                        ctx->position++;
                        return;
                    }
                }
            }
        }

        /* Unknown type kind - might be a simple type alias */
        /* For now, register it as an abstract type with Any parent */
        omni_register_abstract_type(ctx, kind, "Any");
        ctx->position++;
        return;
    }

    if (omni_is_sym(name_or_sig)) {
        /* Check if this is (define f x y body...) - Slot shorthand form
         * vs (define x value) - Simple define form
         *
         * Distinguish by checking if:
         * - rest has multiple elements (params + body)
         * - next elements look like parameters (symbols or arrays)
         */
        if (!omni_is_nil(rest) && omni_is_cell(rest)) {
            OmniValue* maybe_param = omni_car(rest);
            OmniValue* name_val = extract_param_name(maybe_param);

            if (name_val != NULL) {
                /* Slot shorthand: (define f x y body...) or (define f [x] [y] body...) */
                mark_var_write(ctx, name_or_sig->str_val);
                ctx->position++;

                /* Mark parameters until we hit the body (last element or until we see a non-param) */
                /* For now, assume all but last are params, last is body */
                OmniValue* params_list = rest;
                int param_count = 0;

                /* Count parameters (all but last element) */
                while (!omni_is_nil(params_list) && omni_is_cell(params_list)) {
                    if (omni_is_nil(omni_cdr(params_list))) {
                        /* Last element - this is the body, not a parameter */
                        break;
                    }
                    param_count++;
                    params_list = omni_cdr(params_list);
                }

                /* Now process the parameters */
                params_list = rest;
                int i = 0;
                while (!omni_is_nil(params_list) && omni_is_cell(params_list) && i < param_count) {
                    OmniValue* param = omni_car(params_list);
                    OmniValue* param_name = extract_param_name(param);

                    if (param_name) {
                        VarUsage* u = find_or_create_var_usage(ctx, param_name->str_val);
                        u->is_param = true;
                        u->def_pos = ctx->position;
                        ctx->position++;
                    }
                    /* TODO: If param is array with 2 elements [name {Type}], store type info */

                    i++;
                    params_list = omni_cdr(params_list);
                }

                /* Analyze body in return position */
                bool old_return_pos = ctx->in_return_position;
                ctx->in_return_position = true;
                ctx->scope_depth++;

                /* The last element is the body */
                while (!omni_is_nil(params_list) && omni_is_cell(params_list)) {
                    OmniValue* body_expr = omni_car(params_list);
                    ctx->in_return_position = omni_is_nil(omni_cdr(params_list));
                    analyze_expr(ctx, body_expr);
                    params_list = omni_cdr(params_list);
                }

                ctx->scope_depth--;
                ctx->in_return_position = old_return_pos;
                return;
            }
        }

        /* Simple define: (define x value) */
        mark_var_write(ctx, name_or_sig->str_val);
        ctx->position++;

        if (!omni_is_nil(rest)) {
            analyze_expr(ctx, omni_car(rest));
        }
    } else if (omni_is_cell(name_or_sig)) {
        /* Scheme-style syntax (define (name params...) body) is NOT supported in OmniLisp.
         * Use Slot syntax instead:
         *   (define name [x] [y] body)          -- Canonical form
         *   (define name x y body)              -- Shorthand form
         *   (define name [x {Int}] {Int} body)  -- Typed form
         */
        OmniValue* fname = omni_car(name_or_sig);
        const char* fn_name = (omni_is_sym(fname) && fname->str_val) ? fname->str_val : "<unknown>";
        fprintf(stderr, "Error: Scheme-style (define (%s ...) body) syntax is not supported.\n", fn_name);
        fprintf(stderr, "       Use OmniLisp Slot syntax instead:\n");
        fprintf(stderr, "         (define %s [x] [y] body)   ; Canonical\n", fn_name);
        fprintf(stderr, "         (define %s x y body)       ; Shorthand\n", fn_name);
    }
}

/* TESTED */
static void analyze_let(AnalysisContext* ctx, OmniValue* expr) {
    /* Strict Character Calculus let forms:
     *   - List style: (let ((x val) (y val)) body...)
     *   - Slot syntax: (let [x val] [y val] body...)
     *   - Slot with types: (let [x {Int} val] [y {String} val] body...)
     *   - With metadata: (let ^:seq [(x 1) (y x)] ...) for sequential bindings
     *   - With metadata: (let ^:rec [(f (lambda ...))] ...) for recursive bindings
     */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    /* Phase 22: Check for ^:seq or ^:rec metadata */
    bool is_sequential = false;  /* ^:seq - sequential bindings like let* */
    bool is_recursive = false;   /* ^:rec - recursive bindings like letrec */

    OmniValue* first = omni_car(args);
    if (omni_is_sym(first) && strlen(first->str_val) > 1 &&
        first->str_val[0] == '^' && first->str_val[1] == ':') {
        const char* meta_key = first->str_val + 2;  /* Skip "^:" */
        if (strcmp(meta_key, "seq") == 0) {
            is_sequential = true;
        } else if (strcmp(meta_key, "rec") == 0) {
            is_recursive = true;
        }
        /* Skip the metadata marker */
        args = omni_cdr(args);
        if (omni_is_nil(args)) return;
        first = omni_car(args);
    }

    OmniValue* rest = omni_cdr(args);

    /*
     * ^:seq (sequential) and ^:rec (recursive) are handled in codegen:
     *   - ^:seq is the default behavior (each binding sees previous ones)
     *   - ^:rec forward-declares all bindings before initialization
     *
     * Analysis-level handling: Both behave the same for liveness/escape
     * since all bindings are in scope for the body.
     */
    (void)is_sequential;
    (void)is_recursive;

    ctx->scope_depth++;

    /* Slot syntax: (let [x val] [y val] body...) */
    if (omni_is_array(first)) {
        /* Need to find where bindings end and body begins.
         * Scan through list until we find a non-array element.
         * For simplicity, assume all arrays until we run out are bindings,
         * and the remaining elements form the body.
         */
        OmniValue* current = args;  /* Start with first binding */

        /* Process bindings as long as they're arrays */
        while (current && !omni_is_nil(current) && omni_is_cell(current)) {
            OmniValue* elem = omni_car(current);

            if (!omni_is_array(elem)) {
                /* Not an array - we've hit the body */
                break;
            }

            /* Process array binding: [name] or [name type] or [name val] or [name type val] */
            size_t len = omni_array_len(elem);
            if (len >= 1) {
                OmniValue* name_val = omni_array_get(elem, 0);
                if (omni_is_sym(name_val)) {
                    mark_var_write(ctx, name_val->str_val);
                    ctx->position++;
                }

                /* Analyze the value (last element if len >= 2, otherwise it's uninitialized) */
                OmniValue* init_expr = NULL;
                if (len == 2) {
                    /* [name val] */
                    init_expr = omni_array_get(elem, 1);
                    analyze_expr(ctx, init_expr);
                } else if (len == 3) {
                    /* [name type val] - skip type, analyze val */
                    init_expr = omni_array_get(elem, 2);
                    analyze_expr(ctx, init_expr);
                }
                /* len == 1: [name] - uninitialized, don't analyze */

                /* Phase 25: Infer and set type_id for this binding */
                if (init_expr && omni_is_sym(name_val)) {
                    analyze_and_set_type_id(ctx, name_val->str_val, init_expr);
                }
            }

            current = omni_cdr(current);
        }

        /* The remaining elements form the body */
        OmniValue* body = current;
        bool old_return_pos = ctx->in_return_position;
        while (!omni_is_nil(body) && omni_is_cell(body)) {
            ctx->in_return_position = old_return_pos && omni_is_nil(omni_cdr(body));
            analyze_expr(ctx, omni_car(body));
            body = omni_cdr(body);
        }
        ctx->in_return_position = old_return_pos;
    }
    /* Handle list-style bindings ((x 1) (y 2)) - first element is a list */
    else if (omni_is_cell(first)) {
        OmniValue* bindings = first;
        OmniValue* body = rest;

        while (!omni_is_nil(bindings) && omni_is_cell(bindings)) {
            OmniValue* binding = omni_car(bindings);
            if (omni_is_cell(binding)) {
                OmniValue* name = omni_car(binding);
                OmniValue* val = omni_car(omni_cdr(binding));
                if (omni_is_sym(name)) {
                    mark_var_write(ctx, name->str_val);
                    ctx->position++;
                }
                if (val) analyze_expr(ctx, val);

                /* Phase 25: Infer and set type_id for this binding */
                if (val && omni_is_sym(name)) {
                    analyze_and_set_type_id(ctx, name->str_val, val);
                }
            }
            bindings = omni_cdr(bindings);
        }

        /* Analyze body */
        bool old_return_pos = ctx->in_return_position;
        while (!omni_is_nil(body) && omni_is_cell(body)) {
            ctx->in_return_position = old_return_pos && omni_is_nil(omni_cdr(body));
            analyze_expr(ctx, omni_car(body));
            body = omni_cdr(body);
        }
        ctx->in_return_position = old_return_pos;
    }

    ctx->scope_depth--;
}

static void analyze_lambda(AnalysisContext* ctx, OmniValue* expr) {
    /* (lambda (params...) body...) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    OmniValue* params = omni_car(args);
    OmniValue* body = omni_cdr(args);

    bool old_in_lambda = ctx->in_lambda;
    bool old_return_pos = ctx->in_return_position;
    ctx->in_lambda = true;
    ctx->scope_depth++;

    /* Mark parameters */
    if (omni_is_cell(params)) {
        while (!omni_is_nil(params) && omni_is_cell(params)) {
            OmniValue* param = omni_car(params);
            if (omni_is_sym(param)) {
                VarUsage* u = find_or_create_var_usage(ctx, param->str_val);
                u->is_param = true;
                u->def_pos = ctx->position;
                ctx->position++;
            }
            params = omni_cdr(params);
        }
    } else if (omni_is_array(params)) {
        for (size_t i = 0; i < params->array.len; i++) {
            OmniValue* param = params->array.data[i];
            if (omni_is_sym(param)) {
                VarUsage* u = find_or_create_var_usage(ctx, param->str_val);
                u->is_param = true;
                u->def_pos = ctx->position;
                ctx->position++;
            }
        }
    }

    /* Analyze body */
    while (!omni_is_nil(body) && omni_is_cell(body)) {
        ctx->in_return_position = omni_is_nil(omni_cdr(body));
        analyze_expr(ctx, omni_car(body));
        body = omni_cdr(body);
    }

    ctx->scope_depth--;
    ctx->in_lambda = old_in_lambda;
    ctx->in_return_position = old_return_pos;
}

/*
 * desugar_if_to_match - Convert (if cond then else) to (match cond true then false else)
 *
 * This implements the design principle that match is the source of truth for all
 * control flow. The if form is syntactic sugar that desugars to a binary match.
 *
 * Desugaring:
 *   (if cond then-branch else-branch)
 *   =>
 *   (match cond
 *     true then-branch
 *     false else-branch)
 */
static OmniValue* desugar_if_to_match(OmniValue* if_expr) {
    /* Extract components: (if cond then else) */
    OmniValue* args = omni_cdr(if_expr);
    if (omni_is_nil(args)) return if_expr;  /* Invalid if, return as-is */

    OmniValue* cond = omni_car(args);
    args = omni_cdr(args);
    OmniValue* then_branch = omni_is_nil(args) ? omni_nil : omni_car(args);
    args = omni_cdr(args);
    OmniValue* else_branch = omni_is_nil(args) ? omni_nil : omni_car(args);

    /* Build match expression: (match cond true then false else) */
    OmniValue* match_sym = omni_new_sym("match");
    OmniValue* true_sym = omni_new_sym("true");
    OmniValue* false_sym = omni_new_sym("false");

    /* Create clause pairs: (true then) and (false else) */
    OmniValue* true_clause = omni_new_cell(true_sym, omni_new_cell(then_branch, omni_nil));
    OmniValue* false_clause = omni_new_cell(false_sym, omni_new_cell(else_branch, omni_nil));

    /* Build list of clauses: ((true then) (false else)) */
    OmniValue* clauses = omni_new_cell(true_clause, omni_new_cell(false_clause, omni_nil));

    /* Build final match expression: (match cond (true then) (false else)) */
    OmniValue* match_expr = omni_new_cell(match_sym, omni_new_cell(cond, clauses));

    return match_expr;
}

static void analyze_if(AnalysisContext* ctx, OmniValue* expr) {
    /*
     * Phase 26: if → match desugaring
     *
     * The if special form is now syntactic sugar that desugars to a binary match.
     * This unifies control flow optimization and enables the match compiler to
     * emit branchless code for boolean patterns.
     *
     * Before: Separate analyze_if and codegen_if paths
     * After:  Desugar to match, use unified match analysis/codegen
     */
    OmniValue* match_expr = desugar_if_to_match(expr);

    /* Analyze the desugared match expression
     * Note: We analyze it as a regular application since match is handled in codegen
     * The key is that the structure is now (match cond true-expr false-expr ...)
     */
    analyze_expr(ctx, match_expr);
}

static void analyze_application(AnalysisContext* ctx, OmniValue* expr) {
    /* (func arg1 arg2 ...) */
    OmniValue* func = omni_car(expr);
    OmniValue* args = omni_cdr(expr);

    bool old_return_pos = ctx->in_return_position;
    ctx->in_return_position = false;

    analyze_expr(ctx, func);

    /* Type-based dispatch: Look up function signature if available */
    FunctionSummary* func_sig = NULL;
    if (omni_is_sym(func)) {
        func_sig = omni_lookup_function_signature(ctx, func->str_val);
    }

    /* Arguments escape to the function */
    int arg_index = 0;
    OmniValue* current_arg = args;
    while (!omni_is_nil(current_arg) && omni_is_cell(current_arg)) {
        OmniValue* arg = omni_car(current_arg);
        analyze_expr(ctx, arg);

        /* Type checking: If function signature is available, check argument compatibility */
        if (func_sig && arg_index < (int)func_sig->param_count) {
            /* Get the parameter type annotation for this argument */
            ParamSummary* param = omni_get_param_by_index(func_sig, arg_index);
            if (param && param->type_annotation) {
                /* Check if argument is compatible with parameter type */
                bool compatible = omni_check_argument_type_compatibility(
                    ctx, param->type_annotation, arg);

                /* TODO: Store or report type incompatibility */
                /* For now, we just compute it - later tasks will use this for dispatch */
                (void)compatible;  /* Suppress unused warning until we integrate further */
            }
        }

        /* Mark as escaping via argument */
        if (omni_is_sym(arg)) {
            set_escape_class(ctx, arg->str_val, ESCAPE_ARG);
        }

        current_arg = omni_cdr(current_arg);
        arg_index++;
    }

    ctx->in_return_position = old_return_pos;
    ctx->position++;
}

static void analyze_list(AnalysisContext* ctx, OmniValue* expr) {
    if (omni_is_nil(expr)) return;

    OmniValue* head = omni_car(expr);

    /* Check for special forms */
    if (omni_is_sym(head)) {
        const char* name = head->str_val;

        if (strcmp(name, "define") == 0) {
            analyze_define(ctx, expr);
            return;
        }
        if (strcmp(name, "let") == 0 || strcmp(name, "let*") == 0 ||
            strcmp(name, "letrec") == 0) {
            analyze_let(ctx, expr);
            return;
        }
        if (strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0 ||
            strcmp(name, "λ") == 0) {  /* Greek letter lambda (U+03BB) */
            analyze_lambda(ctx, expr);
            return;
        }
        if (strcmp(name, "if") == 0) {
            analyze_if(ctx, expr);
            return;
        }
        if (strcmp(name, "quote") == 0) {
            /* Quoted data - no analysis needed */
            ctx->position++;
            return;
        }
        if (strcmp(name, "set!") == 0) {
            OmniValue* args = omni_cdr(expr);
            if (!omni_is_nil(args)) {
                OmniValue* target = omni_car(args);
                if (omni_is_sym(target)) {
                    mark_var_write(ctx, target->str_val);
                }
                args = omni_cdr(args);
                if (!omni_is_nil(args)) {
                    analyze_expr(ctx, omni_car(args));
                }
            }
            ctx->position++;
            return;
        }
    }

    /* Regular function application */
    analyze_application(ctx, expr);
}

static void analyze_expr(AnalysisContext* ctx, OmniValue* expr) {
    if (!expr || omni_is_nil(expr)) return;

    switch (expr->tag) {
    case OMNI_INT:
    case OMNI_FLOAT:
    case OMNI_CHAR:
    case OMNI_KEYWORD:
        ctx->position++;
        break;

    case OMNI_SYM:
        analyze_symbol(ctx, expr);
        break;

    case OMNI_CELL:
        analyze_list(ctx, expr);
        break;

    case OMNI_ARRAY:
        for (size_t i = 0; i < expr->array.len; i++) {
            analyze_expr(ctx, expr->array.data[i]);
        }
        break;

    case OMNI_DICT:
        for (size_t i = 0; i < expr->dict.len; i++) {
            analyze_expr(ctx, expr->dict.keys[i]);
            analyze_expr(ctx, expr->dict.values[i]);
        }
        break;

    default:
        ctx->position++;
        break;
    }
}

/* ============== Public API ============== */

void omni_analyze(AnalysisContext* ctx, OmniValue* expr) {
    analyze_expr(ctx, expr);
}

void omni_analyze_program(AnalysisContext* ctx, OmniValue** exprs, size_t count) {
    for (size_t i = 0; i < count; i++) {
        analyze_expr(ctx, exprs[i]);
    }
    
    /* Second pass: borrow/tether analysis */
    for (size_t i = 0; i < count; i++) {
        omni_analyze_borrows(ctx, exprs[i]);
    }
}

void omni_analyze_liveness(AnalysisContext* ctx, OmniValue* expr) {
    /* Liveness is computed during general analysis */
    analyze_expr(ctx, expr);
}

void omni_analyze_escape(AnalysisContext* ctx, OmniValue* expr) {
    /* Escape is computed during general analysis */
    analyze_expr(ctx, expr);
}

void omni_analyze_ownership(AnalysisContext* ctx, OmniValue* expr) {
    /* First pass: compute usage and escape */
    analyze_expr(ctx, expr);

    /* Second pass: determine ownership based on escape */
// REVIEWED:NAIVE
    for (VarUsage* u = ctx->var_usages; u; u = u->next) {
        OwnerInfo* o = find_or_create_owner_info(ctx, u->name);

        EscapeInfo* e = NULL;
        for (EscapeInfo* ei = ctx->escape_info; ei; ei = ei->next) {
            if (strcmp(ei->name, u->name) == 0) {
                e = ei;
                break;
            }
        }

        /* Determine uniqueness - non-unique if:
         * 1. Captured by closure (shared with closure env)
         * 2. Escapes via argument (may be aliased)
         * 3. Not marked as is_unique in escape analysis
         */
        o->is_unique = true;
        if (u->flags & VAR_USAGE_CAPTURED) {
            o->is_unique = false;  /* Shared with closure */
        }
        if (e && e->escape_class == ESCAPE_ARG) {
            o->is_unique = false;  /* May be aliased through function call */
        }
        if (e && !e->is_unique) {
            o->is_unique = false;  /* Escape analysis found aliasing */
        }

        /* Determine shape from escape info and type hints */
        if (e) {
            /* Simple heuristic: params tend to be tree-shaped in Lisp */
            o->shape = SHAPE_TREE;
        } else {
            /* Local allocations start as tree-shaped */
            o->shape = SHAPE_TREE;
        }

        if (u->flags & VAR_USAGE_CAPTURED) {
            /* Captured by closure - don't free in parent scope */
            o->ownership = OWNER_TRANSFERRED;
            o->must_free = false;
            o->is_unique = false;  /* Closure shares it */
        } else if (e && e->escape_class >= ESCAPE_RETURN) {
            /* Escapes via return - don't free */
            o->ownership = OWNER_TRANSFERRED;
            o->must_free = false;
        } else if (u->is_param) {
            /* Parameter - borrowed by default */
            o->ownership = OWNER_BORROWED;
            o->must_free = false;
            o->is_unique = false;  /* Caller owns it */
        } else {
            /* Local variable - owned and must free */
            o->ownership = OWNER_LOCAL;
            o->must_free = true;
            o->free_pos = u->last_use;
            /* is_unique stays as determined above */
        }

        /* Determine allocation strategy based on escape class */
        if (e) {
            switch (e->escape_class) {
                case ESCAPE_NONE:
                    /* Stays local - can stack allocate if small enough */
                    o->alloc_strategy = ALLOC_STACK;
                    break;
                case ESCAPE_ARG:
                    /* Escapes via function argument - may be stored
                     * Conservative: use heap unless callee is known to not store */
                    o->alloc_strategy = ALLOC_HEAP;
                    break;
                case ESCAPE_RETURN:
                case ESCAPE_CLOSURE:
                case ESCAPE_GLOBAL:
                    /* Definitely escapes scope - must use heap */
                    o->alloc_strategy = ALLOC_HEAP;
                    break;
            }
        } else {
            /* No escape info - check if captured by closure */
            if (u->flags & VAR_USAGE_CAPTURED) {
                o->alloc_strategy = ALLOC_HEAP;  /* Closure escapes */
            } else if (o->ownership == OWNER_LOCAL && o->is_unique) {
                o->alloc_strategy = ALLOC_STACK;  /* Local unique - can stack */
            } else {
                o->alloc_strategy = ALLOC_HEAP;  /* Conservative default */
            }
        }

        /* Parameters can't be stack-allocated (already exist) */
        if (u->is_param) {
            o->alloc_strategy = ALLOC_HEAP;  /* Not our allocation */
        }
    }
}

/* Back-edge field name patterns (heuristic detection) */
static const char* back_edge_patterns[] = {
    "parent", "prev", "previous", "back", "up", "owner", "container",
    NULL
};

static bool is_back_edge_name(const char* name) {
    for (int i = 0; back_edge_patterns[i]; i++) {
        if (strstr(name, back_edge_patterns[i]) != NULL) {
            return true;
        }
    }
    return false;
}

// REVIEWED:NAIVE
static ShapeInfo* find_or_create_shape_info(AnalysisContext* ctx, const char* type_name) {
    for (ShapeInfo* s = ctx->shape_info; s; s = s->next) {
        if (strcmp(s->type_name, type_name) == 0) return s;
    }

    ShapeInfo* s = malloc(sizeof(ShapeInfo));
    s->type_name = strdup(type_name);
    s->shape = SHAPE_UNKNOWN;
    s->back_edge_fields = NULL;
    s->back_edge_count = 0;
    s->next = ctx->shape_info;
    ctx->shape_info = s;
    return s;
}

static void add_back_edge_field(ShapeInfo* s, const char* field_name) {
    /* Check if already recorded */
    for (size_t i = 0; i < s->back_edge_count; i++) {
        if (strcmp(s->back_edge_fields[i], field_name) == 0) return;
    }

    s->back_edge_fields = realloc(s->back_edge_fields,
                                  (s->back_edge_count + 1) * sizeof(char*));
    s->back_edge_fields[s->back_edge_count++] = strdup(field_name);
}

void omni_analyze_shape(AnalysisContext* ctx, OmniValue* type_def) {
    /* Analyze a type definition for cyclic references
     *
     * Expected form: (defstruct type-name (field1 type1) (field2 type2) ...)
     * or: (deftype type-name ...)
     */
    if (!omni_is_cell(type_def)) return;

    OmniValue* head = omni_car(type_def);
    if (!omni_is_sym(head)) return;

    const char* form = head->str_val;
    if (strcmp(form, "defstruct") != 0 && strcmp(form, "deftype") != 0) {
        return;  /* Not a type definition */
    }

    OmniValue* rest = omni_cdr(type_def);
    if (!omni_is_cell(rest)) return;

    /* Get type name */
    OmniValue* name_val = omni_car(rest);
    if (!omni_is_sym(name_val)) return;
    const char* type_name = name_val->str_val;

    ShapeInfo* shape = find_or_create_shape_info(ctx, type_name);
    shape->shape = SHAPE_TREE;  /* Start optimistic */

    /* Analyze fields */
    OmniValue* fields = omni_cdr(rest);
    bool has_self_ref = false;
    bool has_back_edge = false;

    while (omni_is_cell(fields)) {
        OmniValue* field_def = omni_car(fields);

        if (omni_is_cell(field_def)) {
            OmniValue* field_name_val = omni_car(field_def);
            OmniValue* field_type_val = omni_cdr(field_def);

            if (omni_is_sym(field_name_val)) {
                const char* field_name = field_name_val->str_val;

                /* Check if this is a back-edge by name pattern */
                if (is_back_edge_name(field_name)) {
                    add_back_edge_field(shape, field_name);
                    has_back_edge = true;
                }

                /* Check for self-reference (same type) */
                if (omni_is_cell(field_type_val)) {
                    OmniValue* ft = omni_car(field_type_val);
                    if (omni_is_sym(ft)) {
                        const char* field_type = ft->str_val;
                        if (strcmp(field_type, type_name) == 0) {
                            has_self_ref = true;
                            /* Self-reference with back-edge name → weak */
                            if (is_back_edge_name(field_name)) {
                                add_back_edge_field(shape, field_name);
                            }
                        }
                    }
                }
            }
        }

        fields = omni_cdr(fields);
    }

    /* Determine final shape */
    if (has_back_edge) {
        /* Has back-edge fields → potentially cyclic but we route to weak */
        shape->shape = SHAPE_CYCLIC;
    } else if (has_self_ref) {
        /* Self-referencing but no back-edge pattern → DAG (shared refs) */
        shape->shape = SHAPE_DAG;
    } else {
        /* No self-references → pure tree */
        shape->shape = SHAPE_TREE;
    }
}

/* Helper: (cadr x) = (car (cdr x)) */
static OmniValue* cadr(OmniValue* v) {
    if (!omni_is_cell(v)) return NULL;
    OmniValue* rest = omni_cdr(v);
    if (!omni_is_cell(rest)) return NULL;
    return omni_car(rest);
}

/* Helper: (caddr x) = (car (cdr (cdr x))) */
static OmniValue* caddr(OmniValue* v) {
    if (!omni_is_cell(v)) return NULL;
    OmniValue* rest = omni_cdr(v);
    if (!omni_is_cell(rest)) return NULL;
    rest = omni_cdr(rest);
    if (!omni_is_cell(rest)) return NULL;
    return omni_car(rest);
}

/* Helper: (cdddr x) = (cdr (cdr (cdr x))) */
static OmniValue* cdddr(OmniValue* v) {
    if (!omni_is_cell(v)) return NULL;
    OmniValue* rest = omni_cdr(v);
    if (!omni_is_cell(rest)) return NULL;
    rest = omni_cdr(rest);
    if (!omni_is_cell(rest)) return NULL;
    return omni_cdr(rest);
}

void omni_analyze_reuse(AnalysisContext* ctx, OmniValue* expr) {
    /* Perceus-style reuse analysis: pair frees with subsequent allocations
     *
     * Algorithm:
     * 1. Walk the expression tree in evaluation order
     * 2. Track when variables are freed (last use)
     * 3. When we see an allocation, check if there's a recent free of same size
     * 4. If so, create a reuse candidate
     */
    if (!expr) return;

    /* For primitives, no reuse analysis needed */
    if (omni_is_int(expr) || omni_is_float(expr) ||
        omni_is_char(expr) || omni_is_nil(expr)) {
        return;
    }

    /* For symbols, check if this is the last use (potential free point) */
    if (omni_is_sym(expr)) {
        /* Tracked in liveness analysis - here we just note the position */
        return;
    }

    /* For cells, analyze the form */
    if (!omni_is_cell(expr)) return;

    OmniValue* head = omni_car(expr);
    if (!omni_is_sym(head)) {
        /* Non-symbol head - just recurse */
        omni_analyze_reuse(ctx, head);
        for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
            omni_analyze_reuse(ctx, omni_car(rest));
        }
        return;
    }

    const char* form = head->str_val;

    /* Handle let bindings - key source of reuse opportunities */
    if (strcmp(form, "let") == 0) {
        OmniValue* bindings = cadr(expr);
        OmniValue* body = caddr(expr);

        /* Analyze each binding */
        for (OmniValue* b = bindings; omni_is_cell(b); b = omni_cdr(b)) {
            OmniValue* binding = omni_car(b);
            if (omni_is_cell(binding)) {
                OmniValue* var = omni_car(binding);
                OmniValue* init = cadr(binding);
                if (omni_is_sym(var)) {
                    /* Analyze init expression */
                    omni_analyze_reuse(ctx, init);

                    /* If init is a constructor (cons, mk_int, etc.), track allocation */
                    if (omni_is_cell(init)) {
                        OmniValue* init_head = omni_car(init);
                        if (omni_is_sym(init_head)) {
                            const char* init_form = init_head->str_val;
                            const char* alloc_type = NULL;

                            /* OPTIMIZATION (T-opt-region-metadata-compiler): Type ID tracking */
                            TypeID type_id = TYPE_ID_GENERIC;

                            if (strcmp(init_form, "cons") == 0 ||
                                strcmp(init_form, "pair") == 0 ||
                                strcmp(init_form, "list") == 0) {
                                alloc_type = "Cell";
                                type_id = TYPE_ID_PAIR;
                            } else if (strcmp(init_form, "mk-int") == 0 ||
                                       strcmp(init_form, "int") == 0) {
                                alloc_type = "Int";
                                type_id = TYPE_ID_INT;
                            } else if (strcmp(init_form, "mk-float") == 0 ||
                                       strcmp(init_form, "float") == 0) {
                                alloc_type = "Float";
                                type_id = TYPE_ID_FLOAT;
                            }

                            /* Assign type_id to the variable */
                            if (type_id != TYPE_ID_GENERIC) {
                                omni_set_var_type_id(ctx, var->str_val, type_id);
                            }

                            if (alloc_type) {
                                int alloc_pos = ctx->position++;

                                /* Look for a variable that's being freed before this allocation */
                                for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
                                    if (o->must_free && o->free_pos < alloc_pos &&
                                        o->free_pos >= alloc_pos - 3) {
                                        /* Free happens just before alloc - reuse candidate! */
                                        omni_add_reuse_candidate(ctx, o->name, alloc_type, alloc_pos);
                                        break;  /* Only match one */
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        /* Analyze body */
        omni_analyze_reuse(ctx, body);
        return;
    }

    /* Handle if - both branches may have reuse opportunities */
    if (strcmp(form, "if") == 0) {
        omni_analyze_reuse(ctx, cadr(expr));   /* condition */
        omni_analyze_reuse(ctx, caddr(expr));  /* then */
        OmniValue* else_part = cdddr(expr);
        if (else_part && omni_is_cell(else_part)) {
            omni_analyze_reuse(ctx, omni_car(else_part));  /* else */
        }
        return;
    }

    /* Handle lambda - analyze body */
    if (strcmp(form, "lambda") == 0 || strcmp(form, "fn") == 0 ||
        strcmp(form, "λ") == 0) {
        OmniValue* body = caddr(expr);
        omni_analyze_reuse(ctx, body);
        return;
    }

    /* Default: recurse on all subexpressions */
    for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
        omni_analyze_reuse(ctx, omni_car(rest));
    }
}

/* ============== Query Functions ============== */

// REVIEWED:OPTIMIZED - O(1) hash lookup instead of O(n) linear scan
VarUsage* omni_get_var_usage(AnalysisContext* ctx, const char* name) {
    if (ctx->var_map) {
        return (VarUsage*)strmap_get(ctx->var_map, name);
    }
    /* Fallback to linear scan if hash map not initialized */
    for (VarUsage* u = ctx->var_usages; u; u = u->next) {
        if (strcmp(u->name, name) == 0) return u;
    }
    return NULL;
}

// REVIEWED:OPTIMIZED - O(1) hash lookup instead of O(n) linear scan
EscapeClass omni_get_escape_class(AnalysisContext* ctx, const char* name) {
    if (ctx->escape_map) {
        EscapeInfo* e = (EscapeInfo*)strmap_get(ctx->escape_map, name);
        return e ? e->escape_class : ESCAPE_NONE;
    }
    /* Fallback to linear scan if hash map not initialized */
    for (EscapeInfo* e = ctx->escape_info; e; e = e->next) {
        if (strcmp(e->name, name) == 0) return e->escape_class;
    }
    return ESCAPE_NONE;
}

// REVIEWED:OPTIMIZED - O(1) hash lookup instead of O(n) linear scan
OwnerInfo* omni_get_owner_info(AnalysisContext* ctx, const char* name) {
    if (ctx->owner_map) {
        return (OwnerInfo*)strmap_get(ctx->owner_map, name);
    }
    /* Fallback to linear scan if hash map not initialized */
    for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
        if (strcmp(o->name, name) == 0) return o;
    }
    return NULL;
}

/* ============== Type ID Query Functions (Phase 24) ============== */

/*
 * Get type_id for a variable
 * Returns the TypeID enum value assigned during type inference
 * Returns TYPE_ID_GENERIC (-1) if variable not found or type_id not set
 */
int omni_get_var_type_id(AnalysisContext* ctx, const char* name) {
    VarUsage* u = omni_get_var_usage(ctx, name);
    if (u) {
        return u->type_id;
    }
    return TYPE_ID_GENERIC;  /* Default to generic for unknown variables */
}

/*
 * Set type_id for a variable
 * Called during type inference to assign compile-time type constant
 */
void omni_set_var_type_id(AnalysisContext* ctx, const char* name, int type_id) {
    VarUsage* u = omni_get_var_usage(ctx, name);
    if (u) {
        u->type_id = type_id;
    }
    /* Note: If variable not found, this is a no-op.
     * The variable should already exist from earlier analysis passes. */
}

/* ============== Type Inference Functions (Phase 25) ============== */

/*
 * infer_type_from_expr - Infer TypeID from an expression
 *
 * This function analyzes the init expression and determines what type
 * the resulting object will have. This is used during let binding analysis
 * to assign type_id to variables.
 *
 * Args:
 *   init: The initialization expression
 *
 * Returns:
 *   TypeID enum value, or TYPE_ID_GENERIC if unknown
 */
// REVIEWED:NAIVE
static TypeID infer_type_from_expr(OmniValue* init) {
    if (!init) return TYPE_ID_GENERIC;

    /* Literals */
    if (omni_is_int(init)) return TYPE_ID_INT;
    if (omni_is_float(init)) return TYPE_ID_FLOAT;
    if (omni_is_char(init)) return TYPE_ID_CHAR;
    if (omni_is_nil(init)) return TYPE_ID_PAIR;  /* nil is an empty pair */

    /* Check for constructor calls */
    if (omni_is_cell(init)) {
        OmniValue* head = omni_car(init);
        if (omni_is_sym(head)) {
            const char* form = head->str_val;

            /* Pair/Cons/List constructors */
            if (strcmp(form, "cons") == 0 ||
                strcmp(form, "pair") == 0 ||
                strcmp(form, "list") == 0) {
                return TYPE_ID_PAIR;
            }

            /* Integer constructors */
            if (strcmp(form, "mk-int") == 0 ||
                strcmp(form, "int") == 0) {
                return TYPE_ID_INT;
            }

            /* Float constructors */
            if (strcmp(form, "mk-float") == 0 ||
                strcmp(form, "float") == 0) {
                return TYPE_ID_FLOAT;
            }

            /* Array constructor */
            if (strcmp(form, "array") == 0 ||
                strcmp(form, "mk-array") == 0) {
                return TYPE_ID_ARRAY;
            }

            /* String constructor */
            if (strcmp(form, "str") == 0 ||
                strcmp(form, "string") == 0 ||
                strcmp(form, "mk-string") == 0) {
                return TYPE_ID_STRING;
            }

            /* Symbol constructor */
            if (strcmp(form, "sym") == 0 ||
                strcmp(form, "symbol") == 0 ||
                strcmp(form, "quote") == 0) {
                return TYPE_ID_SYMBOL;
            }

            /* Dict/Map constructor */
            if (strcmp(form, "dict") == 0 ||
                strcmp(form, "map") == 0 ||
                strcmp(form, "mk-dict") == 0) {
                return TYPE_ID_DICT;
            }

            /* Box constructor */
            if (strcmp(form, "box") == 0 ||
                strcmp(form, "ref") == 0) {
                return TYPE_ID_BOX;
            }

            /* DIRECTIVE: NO CHANNELS - channel constructor detection removed */

            /* Thread constructor */
            if (strcmp(form, "thread") == 0 ||
                strcmp(form, "spawn") == 0) {
                return TYPE_ID_THREAD;
            }

            /* Error constructor */
            if (strcmp(form, "error") == 0) {
                return TYPE_ID_ERROR;
            }

            /* Atom constructor */
            if (strcmp(form, "atom") == 0) {
                return TYPE_ID_ATOM;
            }

            /* Tuple constructor */
            if (strcmp(form, "tuple") == 0) {
                return TYPE_ID_TUPLE;
            }
        }

        /* Check if it's a lambda/closure */
        if (omni_is_sym(head) &&
            (strcmp(head->str_val, "lambda") == 0 ||
             strcmp(head->str_val, "fn") == 0 ||
             strcmp(head->str_val, "λ") == 0)) {
            return TYPE_ID_CLOSURE;
        }

        /* Check if it's a function definition */
        if (omni_is_sym(head) && strcmp(head->str_val, "define") == 0) {
            /* (define (name args...) body) */
            if (omni_is_cell(cadr(init))) {
                return TYPE_ID_CLOSURE;
            }
        }
    }

    /* Default: unknown type */
    return TYPE_ID_GENERIC;
}

/*
 * analyze_and_set_type_id - Infer type_id from expression and assign to variable
 *
 * This is called during let binding analysis to set the type_id for each variable.
 *
 * Args:
 *   ctx: Analysis context
 *   var_name: Name of the variable
 *   init: Initialization expression
 *
 * Returns:
 *   The inferred TypeID
 */
TypeID analyze_and_set_type_id(AnalysisContext* ctx, const char* var_name, OmniValue* init) {
    TypeID type_id = infer_type_from_expr(init);
    omni_set_var_type_id(ctx, var_name, type_id);
    return type_id;
}

const char* omni_free_strategy_name(FreeStrategy strategy) {
    switch (strategy) {
        case FREE_STRATEGY_NONE:    return "none";
        case FREE_STRATEGY_UNIQUE:  return "unique";
        case FREE_STRATEGY_TREE:    return "tree";
        case FREE_STRATEGY_RC:      return "rc";
        case FREE_STRATEGY_RC_TREE: return "rc_tree";
        case FREE_STRATEGY_SCC_STATIC: return "scc_static";
        case FREE_STRATEGY_COMPONENT_RELEASE: return "component_release";
        default:                    return "unknown";
    }
}

FreeStrategy omni_get_free_strategy(AnalysisContext* ctx, const char* name) {
    OwnerInfo* o = omni_get_owner_info(ctx, name);
    if (!o) return FREE_STRATEGY_NONE;

    /* Borrowed/transferred - never free */
    if (o->ownership == OWNER_BORROWED || o->ownership == OWNER_TRANSFERRED) {
        return FREE_STRATEGY_NONE;
    }

    /* If not must_free, don't emit a free */
    if (!o->must_free) {
        return FREE_STRATEGY_NONE;
    }

    /* Static SCC collection takes precedence for cyclic data */
    if (o->is_static_scc) {
        // REVIEWED:NAIVE
        /* Check if it's a handle to a static component */
        for (ComponentInfo* ci = ctx->components; ci; ci = ci->next) {
            if (ci->is_static) {
                for (size_t i = 0; i < ci->handle_count; i++) {
                    if (strcmp(ci->handles[i], name) == 0) {
                        return FREE_STRATEGY_COMPONENT_RELEASE;
                    }
                }
            }
        }
        return FREE_STRATEGY_SCC_STATIC;
    }

    /* Determine strategy based on uniqueness and shape */
    if (o->is_unique) {
        /* Single reference - no need for RC check */
        if (o->shape == SHAPE_TREE || o->shape == SHAPE_SCALAR) {
            return FREE_STRATEGY_UNIQUE;
        } else {
            /* DAG/cyclic but unique - still use free_unique for top level */
            return FREE_STRATEGY_UNIQUE;
        }
    }

    /* Shared ownership always uses RC */
    if (o->ownership == OWNER_SHARED) {
        if (o->shape == SHAPE_TREE) {
            return FREE_STRATEGY_RC_TREE;
        }
        return FREE_STRATEGY_RC;
    }

    /* Local ownership, non-unique */
    switch (o->shape) {
        case SHAPE_SCALAR:
            return FREE_STRATEGY_UNIQUE;  /* Scalars are always "unique" */
        case SHAPE_TREE:
            return FREE_STRATEGY_TREE;
        case SHAPE_DAG:
        case SHAPE_CYCLIC:
            return FREE_STRATEGY_RC;
        default:
            /* Unknown shape - conservative RC */
            return FREE_STRATEGY_RC;
    }
}

// TESTED - test_alloc_strategy_name.c
const char* omni_alloc_strategy_name(AllocStrategy strategy) {
    switch (strategy) {
        case ALLOC_HEAP:  return "heap";
        case ALLOC_STACK: return "stack";
        case ALLOC_POOL:  return "pool";
        case ALLOC_ARENA: return "arena";
        default:          return "unknown";
    }
}

AllocStrategy omni_get_alloc_strategy(AnalysisContext* ctx, const char* name) {
    OwnerInfo* o = omni_get_owner_info(ctx, name);
    if (!o) return ALLOC_HEAP;  /* Default to heap for unknown */
    return o->alloc_strategy;
}

bool omni_can_stack_alloc(AnalysisContext* ctx, const char* name) {
    /* Check escape class - only ESCAPE_NONE allows stack allocation */
    EscapeClass escape = omni_get_escape_class(ctx, name);
    if (escape != ESCAPE_NONE) {
        return false;
    }

    /* Check ownership - borrowed refs are already allocated elsewhere */
    OwnerInfo* o = omni_get_owner_info(ctx, name);
    if (o && o->ownership == OWNER_BORROWED) {
        return false;
    }

    /* Check if value is used in a way that requires heap
     * (e.g., stored in a data structure that escapes) */
    VarUsage* u = omni_get_var_usage(ctx, name);
    if (u && (u->flags & VAR_USAGE_ESCAPED)) {
        return false;
    }

    return true;
}

bool omni_should_free_at(AnalysisContext* ctx, const char* name, int position) {
    OwnerInfo* o = omni_get_owner_info(ctx, name);
    if (!o) return false;
    return o->must_free && o->free_pos == position;
}

char** omni_get_frees_at(AnalysisContext* ctx, int position, size_t* out_count) {
    size_t count = 0;
    for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
        if (o->must_free && o->free_pos == position) count++;
    }

    if (count == 0) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    char** names = malloc(count * sizeof(char*));
    size_t i = 0;
    for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
        if (o->must_free && o->free_pos == position) {
            names[i++] = o->name;
        }
    }

    if (out_count) *out_count = count;
    return names;
}

bool omni_is_cyclic_type(AnalysisContext* ctx, const char* type_name) {
    for (ShapeInfo* s = ctx->shape_info; s; s = s->next) {
        if (strcmp(s->type_name, type_name) == 0) {
            return s->shape == SHAPE_CYCLIC;
        }
    }
    return false;
}

char** omni_get_back_edge_fields(AnalysisContext* ctx, const char* type_name, size_t* out_count) {
    for (ShapeInfo* s = ctx->shape_info; s; s = s->next) {
        if (strcmp(s->type_name, type_name) == 0) {
            if (out_count) *out_count = s->back_edge_count;
            return s->back_edge_fields;
        }
    }
    if (out_count) *out_count = 0;
    return NULL;
}

bool omni_is_back_edge_field(AnalysisContext* ctx, const char* type_name, const char* field_name) {
    size_t count;
    char** fields = omni_get_back_edge_fields(ctx, type_name, &count);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(fields[i], field_name) == 0) return true;
    }
    return false;
}

ShapeClass omni_get_type_shape(AnalysisContext* ctx, const char* type_name) {
    for (ShapeInfo* s = ctx->shape_info; s; s = s->next) {
        if (strcmp(s->type_name, type_name) == 0) {
            return s->shape;
        }
    }
    return SHAPE_UNKNOWN;
}

bool omni_is_back_edge_pattern(const char* field_name) {
    return is_back_edge_name(field_name);
}

/* ============== Free Point Computation ============== */

FreePoint* omni_compute_free_points(AnalysisContext* ctx, OmniValue* func) {
    /* Reset and analyze the function */
    ctx->position = 0;
    analyze_expr(ctx, func);
    omni_analyze_ownership(ctx, func);

    /* Group variables by free position */
    FreePoint* points = NULL;

    for (OwnerInfo* o = ctx->owner_info; o; o = o->next) {
        if (!o->must_free) continue;

        /* Find or create FreePoint for this position */
        FreePoint* fp = NULL;
        for (FreePoint* p = points; p; p = p->next) {
            if (p->position == o->free_pos) {
                fp = p;
                break;
            }
        }

        if (!fp) {
            fp = malloc(sizeof(FreePoint));
            fp->position = o->free_pos;
            fp->vars = NULL;
            fp->var_count = 0;
            fp->next = points;
            points = fp;
        }

        /* Add variable to this free point */
        fp->vars = realloc(fp->vars, (fp->var_count + 1) * sizeof(char*));
        fp->vars[fp->var_count++] = o->name;
    }

    return points;
}

void omni_free_points_free(FreePoint* points) {
    while (points) {
        FreePoint* next = points->next;
        free(points->vars);
        free(points);
        points = next;
    }
}

/* ============== Control Flow Graph Implementation ============== */

static CFGNode* cfg_node_new(int id, int node_type) {
    CFGNode* n = malloc(sizeof(CFGNode));
    if (!n) return NULL;
    memset(n, 0, sizeof(CFGNode));
    n->id = id;
    n->node_type = node_type;
    n->position_start = -1;
    n->position_end = -1;
    return n;
}

static void cfg_node_free(CFGNode* n) {
    if (!n) return;
    free(n->successors);
    free(n->predecessors);
    free(n->uses);
    free(n->defs);
    free(n->live_in);
    free(n->live_out);
    free(n);
}

static void cfg_add_edge(CFGNode* from, CFGNode* to) {
    if (!from || !to) return;

    /* Add successor */
    if (from->succ_count >= from->succ_capacity) {
        size_t new_cap = from->succ_capacity == 0 ? 4 : from->succ_capacity * 2;
        from->successors = realloc(from->successors, new_cap * sizeof(CFGNode*));
        from->succ_capacity = new_cap;
    }
    from->successors[from->succ_count++] = to;

    /* Add predecessor */
    if (to->pred_count >= to->pred_capacity) {
        size_t new_cap = to->pred_capacity == 0 ? 4 : to->pred_capacity * 2;
        to->predecessors = realloc(to->predecessors, new_cap * sizeof(CFGNode*));
        to->pred_capacity = new_cap;
    }
    to->predecessors[to->pred_count++] = from;
}

static void cfg_add_node(CFG* cfg, CFGNode* node) {
    if (!cfg || !node) return;
    if (cfg->node_count >= cfg->node_capacity) {
        size_t new_cap = cfg->node_capacity == 0 ? 16 : cfg->node_capacity * 2;
        cfg->nodes = realloc(cfg->nodes, new_cap * sizeof(CFGNode*));
        cfg->node_capacity = new_cap;
    }
    cfg->nodes[cfg->node_count++] = node;
}

// REVIEWED:NAIVE
static void cfg_node_add_use(CFGNode* n, const char* var) {
    if (!n || !var) return;
    /* Check if already present */
    for (size_t i = 0; i < n->use_count; i++) {
        if (strcmp(n->uses[i], var) == 0) return;
    }
    n->uses = realloc(n->uses, (n->use_count + 1) * sizeof(char*));
    n->uses[n->use_count++] = strdup(var);
}

// REVIEWED:NAIVE
static void cfg_node_add_def(CFGNode* n, const char* var) {
    if (!n || !var) return;
    /* Check if already present */
    for (size_t i = 0; i < n->def_count; i++) {
        if (strcmp(n->defs[i], var) == 0) return;
    }
    n->defs = realloc(n->defs, (n->def_count + 1) * sizeof(char*));
    n->defs[n->def_count++] = strdup(var);
}

/* Forward declaration for recursive CFG building */
static CFGNode* build_cfg_expr(CFG* cfg, OmniValue* expr, CFGNode* current);

static CFGNode* build_cfg_if(CFG* cfg, OmniValue* expr, CFGNode* entry) {
    /* (if cond then else) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return entry;

    OmniValue* cond = omni_car(args);
    args = omni_cdr(args);
    OmniValue* then_expr = omni_is_nil(args) ? NULL : omni_car(args);
    args = omni_cdr(args);
    OmniValue* else_expr = omni_is_nil(args) ? NULL : omni_car(args);

    /* Create nodes */
    CFGNode* cond_node = cfg_node_new(cfg->node_count, CFG_BRANCH);
    cfg_add_node(cfg, cond_node);
    cfg_add_edge(entry, cond_node);

    /* Build condition (adds uses to cond_node) */
    build_cfg_expr(cfg, cond, cond_node);

    CFGNode* then_entry = cfg_node_new(cfg->node_count, CFG_BASIC);
    cfg_add_node(cfg, then_entry);
    cfg_add_edge(cond_node, then_entry);

    CFGNode* else_entry = cfg_node_new(cfg->node_count, CFG_BASIC);
    cfg_add_node(cfg, else_entry);
    cfg_add_edge(cond_node, else_entry);

    /* Build branches */
    CFGNode* then_exit = then_expr ? build_cfg_expr(cfg, then_expr, then_entry) : then_entry;
    CFGNode* else_exit = else_expr ? build_cfg_expr(cfg, else_expr, else_entry) : else_entry;

    /* Join node */
    CFGNode* join = cfg_node_new(cfg->node_count, CFG_JOIN);
    cfg_add_node(cfg, join);
    cfg_add_edge(then_exit, join);
    cfg_add_edge(else_exit, join);

    return join;
}

static CFGNode* build_cfg_let(CFG* cfg, OmniValue* expr, CFGNode* entry) {
    /* (let ((x val) (y val)) body...) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return entry;

    OmniValue* bindings = omni_car(args);
    OmniValue* body = omni_cdr(args);

    CFGNode* current = entry;

    /* Process bindings */
    if (omni_is_array(bindings)) {
        for (size_t i = 0; i + 1 < bindings->array.len; i += 2) {
            OmniValue* name = bindings->array.data[i];
            OmniValue* val = bindings->array.data[i + 1];

            CFGNode* bind_node = cfg_node_new(cfg->node_count, CFG_BASIC);
            cfg_add_node(cfg, bind_node);
            cfg_add_edge(current, bind_node);

            if (omni_is_sym(name)) {
                cfg_node_add_def(bind_node, name->str_val);
            }
            build_cfg_expr(cfg, val, bind_node);
            current = bind_node;
        }
    } else if (omni_is_cell(bindings)) {
        while (!omni_is_nil(bindings) && omni_is_cell(bindings)) {
            OmniValue* binding = omni_car(bindings);
            if (omni_is_cell(binding)) {
                OmniValue* name = omni_car(binding);
                OmniValue* val = omni_car(omni_cdr(binding));

                CFGNode* bind_node = cfg_node_new(cfg->node_count, CFG_BASIC);
                cfg_add_node(cfg, bind_node);
                cfg_add_edge(current, bind_node);

                if (omni_is_sym(name)) {
                    cfg_node_add_def(bind_node, name->str_val);
                }
                if (val) build_cfg_expr(cfg, val, bind_node);
                current = bind_node;
            }
            bindings = omni_cdr(bindings);
        }
    }

    /* Process body */
    while (!omni_is_nil(body) && omni_is_cell(body)) {
        current = build_cfg_expr(cfg, omni_car(body), current);
        body = omni_cdr(body);
    }

    return current;
}

static CFGNode* build_cfg_lambda(CFG* cfg, OmniValue* expr, CFGNode* entry) {
    /* Lambda creates a closure - the body is analyzed separately */
    /* For now, just treat it as a value (no control flow) */
    (void)cfg;
    (void)expr;
    return entry;
}

static CFGNode* build_cfg_define(CFG* cfg, OmniValue* expr, CFGNode* entry) {
    /* (define name value) or (define (name params...) body) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return entry;

    OmniValue* name_or_sig = omni_car(args);
    OmniValue* body = omni_cdr(args);

    CFGNode* def_node = cfg_node_new(cfg->node_count, CFG_BASIC);
    cfg_add_node(cfg, def_node);
    cfg_add_edge(entry, def_node);

    if (omni_is_sym(name_or_sig)) {
        cfg_node_add_def(def_node, name_or_sig->str_val);
        if (!omni_is_nil(body)) {
            build_cfg_expr(cfg, omni_car(body), def_node);
        }
    } else if (omni_is_cell(name_or_sig)) {
        OmniValue* fname = omni_car(name_or_sig);
        if (omni_is_sym(fname)) {
            cfg_node_add_def(def_node, fname->str_val);
        }
        /* Function body is analyzed separately */
    }

    return def_node;
}

static CFGNode* build_cfg_application(CFG* cfg, OmniValue* expr, CFGNode* entry) {
    /* (func arg1 arg2 ...) */
    OmniValue* func = omni_car(expr);
    OmniValue* args = omni_cdr(expr);

    CFGNode* app_node = cfg_node_new(cfg->node_count, CFG_BASIC);
    cfg_add_node(cfg, app_node);
    cfg_add_edge(entry, app_node);

    /* Add function as use */
    if (omni_is_sym(func)) {
        cfg_node_add_use(app_node, func->str_val);
    }

    /* Add arguments as uses */
    while (!omni_is_nil(args) && omni_is_cell(args)) {
        OmniValue* arg = omni_car(args);
        if (omni_is_sym(arg)) {
            cfg_node_add_use(app_node, arg->str_val);
        }
        args = omni_cdr(args);
    }

    return app_node;
}

static CFGNode* build_cfg_expr(CFG* cfg, OmniValue* expr, CFGNode* current) {
    if (!expr || omni_is_nil(expr)) return current;

    if (omni_is_sym(expr)) {
        /* Variable reference - add as use */
        cfg_node_add_use(current, expr->str_val);
        return current;
    }

    if (!omni_is_cell(expr)) {
        /* Literal value - no control flow */
        return current;
    }

    /* List expression */
    OmniValue* head = omni_car(expr);
    if (omni_is_sym(head)) {
        const char* name = head->str_val;

        if (strcmp(name, "if") == 0) {
            return build_cfg_if(cfg, expr, current);
        }
        if (strcmp(name, "let") == 0 || strcmp(name, "let*") == 0 ||
            strcmp(name, "letrec") == 0) {
            return build_cfg_let(cfg, expr, current);
        }
        if (strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0 ||
            strcmp(name, "λ") == 0) {
            return build_cfg_lambda(cfg, expr, current);
        }
        if (strcmp(name, "define") == 0) {
            return build_cfg_define(cfg, expr, current);
        }
        if (strcmp(name, "quote") == 0) {
            return current;  /* Quoted data - no control flow */
        }
        if (strcmp(name, "set!") == 0) {
            OmniValue* args = omni_cdr(expr);
            if (!omni_is_nil(args)) {
                OmniValue* target = omni_car(args);
                if (omni_is_sym(target)) {
                    cfg_node_add_def(current, target->str_val);
                }
                args = omni_cdr(args);
                if (!omni_is_nil(args)) {
                    build_cfg_expr(cfg, omni_car(args), current);
                }
            }
            return current;
        }
    }

    /* Regular function application */
    return build_cfg_application(cfg, expr, current);
}

CFG* omni_build_cfg(OmniValue* expr) {
    CFG* cfg = malloc(sizeof(CFG));
    if (!cfg) return NULL;
    memset(cfg, 0, sizeof(CFG));

    /* Create entry and exit nodes */
    cfg->entry = cfg_node_new(0, CFG_ENTRY);
    cfg_add_node(cfg, cfg->entry);

    cfg->exit = cfg_node_new(1, CFG_EXIT);
    cfg_add_node(cfg, cfg->exit);

    /* Build CFG for expression */
    CFGNode* last = build_cfg_expr(cfg, expr, cfg->entry);

    /* Connect last node to exit */
    cfg_add_edge(last, cfg->exit);

    return cfg;
}

void omni_cfg_free(CFG* cfg) {
    if (!cfg) return;
    for (size_t i = 0; i < cfg->node_count; i++) {
        cfg_node_free(cfg->nodes[i]);
    }
    free(cfg->nodes);
    free(cfg);
}

/* ============== Liveness Analysis (Backward Dataflow) ============== */

// REVIEWED:NAIVE
static bool string_set_contains(char** set, size_t count, const char* str) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(set[i], str) == 0) return true;
    }
    return false;
}

static bool string_set_add(char*** set, size_t* count, const char* str) {
    if (string_set_contains(*set, *count, str)) return false;
    *set = realloc(*set, (*count + 1) * sizeof(char*));
    (*set)[(*count)++] = strdup(str);
    return true;
}

void omni_compute_liveness(CFG* cfg, AnalysisContext* ctx) {
    if (!cfg || !ctx) return;

    bool changed = true;
    int iterations = 0;
    const int max_iterations = 1000;  /* Safety limit */

    while (changed && iterations++ < max_iterations) {
        changed = false;

        /* Process nodes in reverse order (approximate reverse postorder) */
        for (size_t i = cfg->node_count; i > 0; i--) {
            CFGNode* n = cfg->nodes[i - 1];

            /* live_out = union of successors' live_in */
            for (size_t s = 0; s < n->succ_count; s++) {
                CFGNode* succ = n->successors[s];
                for (size_t v = 0; v < succ->live_in_count; v++) {
                    if (string_set_add(&n->live_out, &n->live_out_count,
                                       succ->live_in[v])) {
                        changed = true;
                    }
                }
            }

            /* live_in = use(n) ∪ (live_out - def(n)) */
            /* First add uses */
            for (size_t u = 0; u < n->use_count; u++) {
                if (string_set_add(&n->live_in, &n->live_in_count, n->uses[u])) {
                    changed = true;
                }
            }

// REVIEWED:NAIVE
            /* Then add live_out - def */
            for (size_t v = 0; v < n->live_out_count; v++) {
                const char* var = n->live_out[v];
                bool is_def = false;
                for (size_t d = 0; d < n->def_count; d++) {
                    if (strcmp(n->defs[d], var) == 0) {
                        is_def = true;
                        break;
                    }
                }
                if (!is_def) {
                    if (string_set_add(&n->live_in, &n->live_in_count, var)) {
                        changed = true;
                    }
                }
            }
        }
    }
}

/* ============== Issue 3 P2: Non-Lexical Region End ============== */

/**
 * omni_compute_region_exit_points - Compute when each region should exit
 *
 * For each region, find the position where all its variables are dead.
 * This is the maximum last_use position among all variables in the region.
 *
 * Returns a linked list of RegionExitPoint structures (caller must free).
 */
RegionExitPoint* omni_compute_region_exit_points(AnalysisContext* ctx) {
    if (!ctx || !ctx->var_usages) {
        return NULL;
    }

    /* First, initialize all region last-use positions to 0 */
    int* region_last_use = NULL;
    size_t region_count = 0;
    int max_region_id = 0;

    /* Scan all variables to find max region ID */
    for (VarUsage* u = ctx->var_usages; u; u = u->next) {
        if (u->region_id > max_region_id) {
            max_region_id = u->region_id;
        }
    }

    if (max_region_id < 0) {
        return NULL;  /* No regions found */
    }

    region_count = (size_t)(max_region_id + 1);
    region_last_use = calloc(region_count, sizeof(int));

    /* Compute last-use for each region */
    for (VarUsage* u = ctx->var_usages; u; u = u->next) {
        if (u->region_id >= 0 && u->last_use >= 0) {
            if (u->last_use > region_last_use[u->region_id]) {
                region_last_use[u->region_id] = u->last_use;
            }
        }
    }

    /* Build exit point list (skip region 0 - that's the function's main region) */
    RegionExitPoint* points = NULL;
    for (size_t i = 1; i < region_count; i++) {
        if (region_last_use[i] > 0) {
            RegionExitPoint* rp = malloc(sizeof(RegionExitPoint));
            rp->region_id = (int)i;
            rp->position = region_last_use[i];
            rp->next = points;
            points = rp;
        }
    }

    free(region_last_use);
    return points;
}

/**
 * omni_region_exit_points_free - Free region exit point list
 */
void omni_region_exit_points_free(RegionExitPoint* points) {
    while (points) {
        RegionExitPoint* next = points->next;
        free(points);
        points = next;
    }
}

char** omni_get_frees_for_node(CFG* cfg, CFGNode* node,
                                AnalysisContext* ctx, size_t* out_count) {
    (void)cfg;  /* Unused for now */

    if (!node || !ctx) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    /* Variables that die at this node:
     * - In live_in but NOT in live_out
     * - AND must_free is true (from ownership analysis)
     */
    size_t count = 0;
    char** to_free = NULL;

    for (size_t i = 0; i < node->live_in_count; i++) {
        const char* var = node->live_in[i];

        /* Check if in live_out */
        if (string_set_contains(node->live_out, node->live_out_count, var)) {
            continue;  /* Still live - don't free */
        }

        /* Check if we own it */
        OwnerInfo* o = omni_get_owner_info(ctx, var);
        if (o && o->must_free) {
            to_free = realloc(to_free, (count + 1) * sizeof(char*));
            to_free[count++] = (char*)var;
        }
    }

    if (out_count) *out_count = count;
    return to_free;
}

void omni_print_cfg(CFG* cfg) {
    if (!cfg) {
        printf("CFG: (null)\n");
        return;
    }

    printf("CFG with %zu nodes:\n", cfg->node_count);
    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* n = cfg->nodes[i];
        const char* type_names[] = {
            "BASIC", "BRANCH", "JOIN", "LOOP_HEAD", "LOOP_EXIT", "ENTRY", "EXIT"
        };
        printf("  Node %d (%s):\n", n->id, type_names[n->node_type]);

        if (n->def_count > 0) {
            printf("    defs: ");
            for (size_t j = 0; j < n->def_count; j++) {
                printf("%s ", n->defs[j]);
            }
            printf("\n");
        }
        if (n->use_count > 0) {
            printf("    uses: ");
            for (size_t j = 0; j < n->use_count; j++) {
                printf("%s ", n->uses[j]);
            }
            printf("\n");
        }
        if (n->live_in_count > 0) {
            printf("    live_in: ");
            for (size_t j = 0; j < n->live_in_count; j++) {
                printf("%s ", n->live_in[j]);
            }
            printf("\n");
        }
        if (n->live_out_count > 0) {
            printf("    live_out: ");
            for (size_t j = 0; j < n->live_out_count; j++) {
                printf("%s ", n->live_out[j]);
            }
            printf("\n");
        }
        if (n->succ_count > 0) {
            printf("    -> ");
            for (size_t j = 0; j < n->succ_count; j++) {
                printf("%d ", n->successors[j]->id);
            }
            printf("\n");
        }
    }
}

CFGFreePoint* omni_compute_cfg_free_points(CFG* cfg, AnalysisContext* ctx) {
    if (!cfg || !ctx) return NULL;

    CFGFreePoint* points = NULL;

    for (size_t i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];
        size_t count;
        char** to_free = omni_get_frees_for_node(cfg, node, ctx, &count);

        if (count > 0) {
            CFGFreePoint* fp = malloc(sizeof(CFGFreePoint));
            fp->node = node;
            fp->vars = to_free;
            fp->var_count = count;
            fp->next = points;
            points = fp;
        }
    }

    return points;
}

void omni_cfg_free_points_free(CFGFreePoint* points) {
    while (points) {
        CFGFreePoint* next = points->next;
        free(points->vars);  /* Don't free individual strings - they're owned by live_in */
        free(points);
        points = next;
    }
}

/* ============== Perceus Reuse Analysis ============== */

size_t omni_type_size(const char* type_name) {
    /* Return size class for types - Perceus matches by size class, not exact size */
    if (!type_name) return 0;

    /* Primitive types - typically 16-32 bytes with header */
    if (strcmp(type_name, "Int") == 0 ||
        strcmp(type_name, "int") == 0 ||
        strcmp(type_name, "long") == 0) {
        return 24;  /* Header (8) + int64 (8) + padding (8) */
    }
    if (strcmp(type_name, "Float") == 0 ||
        strcmp(type_name, "float") == 0 ||
        strcmp(type_name, "double") == 0) {
        return 24;  /* Header (8) + double (8) + padding (8) */
    }
    if (strcmp(type_name, "Bool") == 0 ||
        strcmp(type_name, "bool") == 0) {
        return 16;  /* Header (8) + bool (1) + padding (7) */
    }
    if (strcmp(type_name, "Char") == 0 ||
        strcmp(type_name, "char") == 0) {
        return 16;  /* Header (8) + char (1) + padding (7) */
    }

    /* Pair/Cons cells - two pointers */
    if (strcmp(type_name, "Cell") == 0 ||
        strcmp(type_name, "Cons") == 0 ||
        strcmp(type_name, "Pair") == 0 ||
        strcmp(type_name, "cons") == 0) {
        return 32;  /* Header (8) + car (8) + cdr (8) + padding (8) */
    }

    /* String - variable, use small size class */
    if (strcmp(type_name, "String") == 0 ||
        strcmp(type_name, "Sym") == 0 ||
        strcmp(type_name, "Symbol") == 0 ||
        strcmp(type_name, "string") == 0) {
        return 32;  /* Header (8) + ptr (8) + len (8) + padding (8) */
    }

    /* Default size class */
    return 32;
}

bool omni_can_reuse_for(AnalysisContext* ctx, const char* freed_var,
                        const char* new_type) {
    (void)ctx;  /* May use context for type lookup in future */

    /* Get the type/shape of the freed variable */
    /* For now, match by size class */
    OwnerInfo* o = omni_get_owner_info(ctx, freed_var);
    if (!o) return false;

    /* Can't reuse borrowed or transferred ownership */
    if (o->ownership == OWNER_BORROWED || o->ownership == OWNER_TRANSFERRED) {
        return false;
    }

    /* Can't reuse if not unique (shared ownership) */
    if (!o->is_unique && o->ownership == OWNER_SHARED) {
        return false;
    }

    /* Size class matching - Perceus allows reuse of same-size allocations */
    /* We use the shape to infer the type of freed_var */
    size_t freed_size = 0;
    switch (o->shape) {
        case SHAPE_SCALAR:
            freed_size = 24;  /* Int/Float/Bool/Char */
            break;
        case SHAPE_TREE:
        case SHAPE_DAG:
        case SHAPE_CYCLIC:
            freed_size = 32;  /* Cons cells, structures */
            break;
        default:
            freed_size = 32;  /* Default */
            break;
    }

    size_t new_size = omni_type_size(new_type);
    return freed_size == new_size;
}

void omni_add_reuse_candidate(AnalysisContext* ctx, const char* freed_var,
                              const char* alloc_type, int alloc_pos) {
    /* Find the free position for this variable */
    OwnerInfo* o = omni_get_owner_info(ctx, freed_var);
    if (!o || !o->must_free) return;

    /* Check if size classes match */
    if (!omni_can_reuse_for(ctx, freed_var, alloc_type)) return;

    /* Create reuse candidate */
    ReuseCandidate* rc = malloc(sizeof(ReuseCandidate));
    rc->alloc_pos = alloc_pos;
    rc->free_pos = o->free_pos;
    rc->freed_var = strdup(freed_var);
    rc->type_name = strdup(alloc_type);
    rc->size = omni_type_size(alloc_type);
    rc->can_reuse = true;
    rc->is_consumed = false;
    rc->next = ctx->reuse_candidates;
    ctx->reuse_candidates = rc;
}

ReuseCandidate* omni_get_reuse_at(AnalysisContext* ctx, int alloc_pos) {
    /* Find an available reuse candidate for this allocation position */
    for (ReuseCandidate* rc = ctx->reuse_candidates; rc; rc = rc->next) {
        if (rc->alloc_pos == alloc_pos && rc->can_reuse && !rc->is_consumed) {
            return rc;
        }
    }
    return NULL;
}

/* Mark a reuse candidate as consumed */
void omni_consume_reuse(ReuseCandidate* rc) {
    if (rc) {
        rc->is_consumed = true;
    }
}

/* ============== Region Analysis ============== */

RegionInfo* omni_region_new(AnalysisContext* ctx, const char* name) {
    RegionInfo* r = malloc(sizeof(RegionInfo));
    r->region_id = ctx->next_region_id++;
    r->name = name ? strdup(name) : NULL;
    r->scope_depth = ctx->scope_depth;
    r->start_pos = ctx->position;
    r->end_pos = -1;  /* Set when region ends */
    r->variables = NULL;
    r->var_count = 0;
    r->var_capacity = 0;
    r->external_refcount = 0;
    r->has_escaping_refs = false;
    r->parent = ctx->current_region;
    r->next = ctx->regions;
    ctx->regions = r;
    ctx->current_region = r;
    return r;
}

void omni_region_end(AnalysisContext* ctx) {
    if (ctx->current_region) {
        ctx->current_region->end_pos = ctx->position;
        ctx->current_region = ctx->current_region->parent;
    }
}

void omni_region_add_var(AnalysisContext* ctx, const char* var_name) {
    if (!ctx->current_region) {
        /* Create a default region if none exists */
        omni_region_new(ctx, "default");
    }

    RegionInfo* r = ctx->current_region;

    /* Grow array if needed */
    if (r->var_count >= r->var_capacity) {
        r->var_capacity = r->var_capacity ? r->var_capacity * 2 : 8;
        r->variables = realloc(r->variables, r->var_capacity * sizeof(char*));
    }

    r->variables[r->var_count++] = strdup(var_name);
}

// REVIEWED:NAIVE
RegionInfo* omni_get_var_region(AnalysisContext* ctx, const char* var_name) {
    for (RegionInfo* r = ctx->regions; r; r = r->next) {
        for (size_t i = 0; i < r->var_count; i++) {
            if (strcmp(r->variables[i], var_name) == 0) {
                return r;
            }
        }
    }
    return NULL;
}

bool omni_same_region(AnalysisContext* ctx, const char* var1, const char* var2) {
    RegionInfo* r1 = omni_get_var_region(ctx, var1);
    RegionInfo* r2 = omni_get_var_region(ctx, var2);

    if (!r1 || !r2) return false;
    return r1->region_id == r2->region_id;
}

/* ============== Per-Region External Refcount ============== */

RegionInfo* omni_get_region_by_id(AnalysisContext* ctx, int region_id) {
    for (RegionInfo* r = ctx->regions; r; r = r->next) {
        if (r->region_id == region_id) {
            return r;
        }
    }
    return NULL;
}

void omni_region_inc_external(AnalysisContext* ctx, int region_id) {
    RegionInfo* r = omni_get_region_by_id(ctx, region_id);
    if (r) {
        r->external_refcount++;
    }
}

void omni_region_dec_external(AnalysisContext* ctx, int region_id) {
    RegionInfo* r = omni_get_region_by_id(ctx, region_id);
    if (r && r->external_refcount > 0) {
        r->external_refcount--;
    }
}

int omni_region_get_external(AnalysisContext* ctx, int region_id) {
    RegionInfo* r = omni_get_region_by_id(ctx, region_id);
    return r ? r->external_refcount : 0;
}

bool omni_is_cross_region_ref(AnalysisContext* ctx, const char* src_var, const char* dst_var) {
    RegionInfo* src_region = omni_get_var_region(ctx, src_var);
    RegionInfo* dst_region = omni_get_var_region(ctx, dst_var);

    /* If either isn't in a region, consider it cross-region */
    if (!src_region || !dst_region) return true;

    /* Different regions = cross-region reference */
    return src_region->region_id != dst_region->region_id;
}

void omni_region_mark_escaping(AnalysisContext* ctx, int region_id) {
    RegionInfo* r = omni_get_region_by_id(ctx, region_id);
    if (r) {
        r->has_escaping_refs = true;
    }
}

bool omni_region_can_bulk_free(AnalysisContext* ctx, int region_id) {
    RegionInfo* r = omni_get_region_by_id(ctx, region_id);
    if (!r) return false;

    /* Can bulk free if:
     * 1. No external references
     * 2. No escaping references
     */
    return r->external_refcount == 0 && !r->has_escaping_refs;
}

/* ============== RC Elision Analysis ============== */

const char* omni_rc_elision_name(RCElisionClass elision) {
    switch (elision) {
        case RC_REQUIRED:   return "required";
        case RC_ELIDE_INC:  return "elide_inc";
        case RC_ELIDE_DEC:  return "elide_dec";
        case RC_ELIDE_BOTH: return "elide_both";
        default:            return "unknown";
    }
}

static RCElisionInfo* find_or_create_elision_info(AnalysisContext* ctx, const char* var_name) {
    for (RCElisionInfo* e = ctx->rc_elision; e; e = e->next) {
        if (strcmp(e->var_name, var_name) == 0) return e;
    }

    RCElisionInfo* e = malloc(sizeof(RCElisionInfo));
    e->var_name = strdup(var_name);
    e->elision = RC_REQUIRED;  /* Default: RC required */
    e->region_id = -1;
    e->same_region_refs = false;
    e->next = ctx->rc_elision;
    ctx->rc_elision = e;
    return e;
}

void omni_analyze_rc_elision(AnalysisContext* ctx, OmniValue* expr) {
    /* Analyze RC elision opportunities
     *
     * RC can be elided when:
     * 1. Variable is unique (only reference)
     * 2. Variable is stack-allocated (no heap involvement)
     * 3. All references are within the same region
     * 4. Variable is arena/pool-allocated (bulk free, no individual RC)
     */
    if (!expr) return;

    /* For symbols, check if RC can be elided */
    if (omni_is_sym(expr)) {
        const char* name = expr->str_val;

        /* Check ownership info */
        OwnerInfo* o = omni_get_owner_info(ctx, name);
        if (!o) return;

        RCElisionInfo* e = find_or_create_elision_info(ctx, name);

        /* Get region info */
        RegionInfo* r = omni_get_var_region(ctx, name);
        if (r) {
            e->region_id = r->region_id;
        }

        /* Determine elision class based on ownership and allocation strategy */
        if (o->is_unique) {
            /* Unique reference - can elide both inc and dec */
            e->elision = RC_ELIDE_BOTH;
        } else if (o->alloc_strategy == ALLOC_STACK) {
            /* Stack allocated - can elide both */
            e->elision = RC_ELIDE_BOTH;
        } else if (o->alloc_strategy == ALLOC_ARENA ||
                   o->alloc_strategy == ALLOC_POOL) {
            /* Arena/pool - can elide dec (bulk free) */
            e->elision = RC_ELIDE_DEC;
        } else if (o->ownership == OWNER_BORROWED) {
            /* Borrowed - can elide inc (don't own it) */
            e->elision = RC_ELIDE_INC;
        } else {
            e->elision = RC_REQUIRED;
        }

        return;
    }

    /* For cells, recurse */
    if (!omni_is_cell(expr)) return;

    OmniValue* head = omni_car(expr);

    /* Handle let bindings - create region for let body */
    if (omni_is_sym(head) && strcmp(head->str_val, "let") == 0) {
        /* Create a new region for this let binding */
        omni_region_new(ctx, "let");

        /* Analyze bindings */
        OmniValue* bindings = cadr(expr);
        for (OmniValue* b = bindings; omni_is_cell(b); b = omni_cdr(b)) {
            OmniValue* binding = omni_car(b);
            if (omni_is_cell(binding)) {
                OmniValue* var = omni_car(binding);
                if (omni_is_sym(var)) {
                    /* Add variable to current region */
                    omni_region_add_var(ctx, var->str_val);
                }
                /* Analyze init expression */
                omni_analyze_rc_elision(ctx, cadr(binding));
            }
        }

        /* Analyze body */
        omni_analyze_rc_elision(ctx, caddr(expr));

        /* End region */
        omni_region_end(ctx);
        return;
    }

    /* Default: recurse on all subexpressions */
    omni_analyze_rc_elision(ctx, head);
    for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
        omni_analyze_rc_elision(ctx, omni_car(rest));
    }
}

RCElisionClass omni_get_rc_elision(AnalysisContext* ctx, const char* var_name) {
    for (RCElisionInfo* e = ctx->rc_elision; e; e = e->next) {
        if (strcmp(e->var_name, var_name) == 0) {
            return e->elision;
        }
    }
    return RC_REQUIRED;  /* Default: RC required */
}

bool omni_can_elide_inc_ref(AnalysisContext* ctx, const char* var_name) {
    RCElisionClass elision = omni_get_rc_elision(ctx, var_name);
    return elision == RC_ELIDE_INC || elision == RC_ELIDE_BOTH;
}

bool omni_can_elide_dec_ref(AnalysisContext* ctx, const char* var_name) {
    RCElisionClass elision = omni_get_rc_elision(ctx, var_name);
    return elision == RC_ELIDE_DEC || elision == RC_ELIDE_BOTH;
}

/* ============== Borrow/Tether Analysis ============== */

const char* omni_borrow_kind_name(BorrowKind kind) {
    switch (kind) {
        case BORROW_NONE:      return "none";
        case BORROW_SHARED:    return "shared";
        case BORROW_EXCLUSIVE: return "exclusive";
        case BORROW_LOOP:      return "loop";
        default:               return "unknown";
    }
}

void omni_borrow_start(AnalysisContext* ctx, const char* borrowed_var,
                       const char* holder, BorrowKind kind) {
    BorrowInfo* b = malloc(sizeof(BorrowInfo));
    b->borrowed_var = strdup(borrowed_var);
    b->borrow_holder = holder ? strdup(holder) : NULL;
    b->kind = kind;
    b->start_pos = ctx->position;
    b->end_pos = -1;  /* Set when borrow ends */
    b->needs_tether = (kind == BORROW_LOOP);  /* Loop borrows need tethering */
    b->next = ctx->borrows;
    ctx->borrows = b;

    /* Add tether entry if needed */
    if (b->needs_tether) {
        omni_add_tether(ctx, borrowed_var, true);
    }
}

void omni_borrow_end(AnalysisContext* ctx, const char* borrowed_var) {
    for (BorrowInfo* b = ctx->borrows; b; b = b->next) {
        if (strcmp(b->borrowed_var, borrowed_var) == 0 && b->end_pos == -1) {
            b->end_pos = ctx->position;

            /* Add tether exit if needed */
            if (b->needs_tether) {
                omni_add_tether(ctx, borrowed_var, false);
            }
            return;
        }
    }
}

bool omni_is_borrowed(AnalysisContext* ctx, const char* var_name) {
    for (BorrowInfo* b = ctx->borrows; b; b = b->next) {
        if (strcmp(b->borrowed_var, var_name) == 0 && b->end_pos == -1) {
            return true;  /* Active borrow */
        }
    }
    return false;
}

BorrowInfo* omni_get_borrow_info(AnalysisContext* ctx, const char* var_name) {
    for (BorrowInfo* b = ctx->borrows; b; b = b->next) {
        if (strcmp(b->borrowed_var, var_name) == 0) {
            return b;
        }
    }
    return NULL;
}

void omni_add_tether(AnalysisContext* ctx, const char* var_name, bool is_entry) {
    TetherPoint* tp = malloc(sizeof(TetherPoint));
    tp->position = ctx->position;
    tp->tethered_var = strdup(var_name);
    tp->is_entry = is_entry;
    tp->elided = false;
    tp->next = ctx->tethers;
    ctx->tethers = tp;
}

TetherPoint** omni_get_tethers_at(AnalysisContext* ctx, int position, size_t* count) {
    /* Count tethers at this position */
    size_t n = 0;
    for (TetherPoint* t = ctx->tethers; t; t = t->next) {
        if (t->position == position) n++;
    }

    if (n == 0) {
        if (count) *count = 0;
        return NULL;
    }

    TetherPoint** result = malloc(n * sizeof(TetherPoint*));
    size_t i = 0;
    for (TetherPoint* t = ctx->tethers; t; t = t->next) {
        if (t->position == position) {
            result[i++] = t;
        }
    }

    if (count) *count = n;
    return result;
}

bool omni_needs_tether(AnalysisContext* ctx, const char* var_name, int position) {
    for (BorrowInfo* b = ctx->borrows; b; b = b->next) {
        if (strcmp(b->borrowed_var, var_name) == 0 &&
            b->needs_tether &&
            b->start_pos <= position &&
            (b->end_pos == -1 || b->end_pos >= position)) {
            return true;
        }
    }
    return false;
}

void omni_analyze_borrows(AnalysisContext* ctx, OmniValue* expr) {
    /* Analyze borrow patterns, especially in loops
     *
     * Patterns we detect:
     * 1. (for-each var coll body) - coll is borrowed for duration of loop
     * 2. (loop ((i 0)) (if (< i (length xs)) ... )) - xs is borrowed
     * 3. (map f xs) - xs is borrowed during map
     */
    if (!expr) return;

    /* Skip primitives */
    if (omni_is_int(expr) || omni_is_float(expr) ||
        omni_is_char(expr) || omni_is_nil(expr) || omni_is_sym(expr)) {
        return;
    }

    if (!omni_is_cell(expr)) return;

    OmniValue* head = omni_car(expr);
    if (!omni_is_sym(head)) {
        /* Recurse on all subexpressions */
        for (OmniValue* rest = expr; omni_is_cell(rest); rest = omni_cdr(rest)) {
            omni_analyze_borrows(ctx, omni_car(rest));
        }
        return;
    }

    const char* form = head->str_val;

    /* Detect loop forms */
    if (strcmp(form, "for-each") == 0 || strcmp(form, "foreach") == 0) {
        /* (for-each var coll body) */
        OmniValue* var = cadr(expr);
        OmniValue* coll = caddr(expr);
        OmniValue* body = caddr(omni_cdr(expr));  /* cadddr */

        if (omni_is_sym(coll)) {
            /* Borrow the collection for the loop */
            ctx->in_loop = true;
            omni_borrow_start(ctx, coll->str_val,
                             omni_is_sym(var) ? var->str_val : NULL,
                             BORROW_LOOP);

            /* Analyze body */
            omni_analyze_borrows(ctx, body);

            /* End borrow */
            omni_borrow_end(ctx, coll->str_val);
            ctx->in_loop = false;
        }
        return;
    }

    if (strcmp(form, "map") == 0 || strcmp(form, "filter") == 0 ||
        strcmp(form, "fold") == 0 || strcmp(form, "reduce") == 0) {
        /* (map f xs) - xs is borrowed */
        OmniValue* args = omni_cdr(expr);
        for (OmniValue* a = args; omni_is_cell(a); a = omni_cdr(a)) {
            OmniValue* arg = omni_car(a);
            if (omni_is_sym(arg)) {
                /* Each collection arg is borrowed */
                omni_borrow_start(ctx, arg->str_val, NULL, BORROW_LOOP);
                omni_borrow_end(ctx, arg->str_val);  /* Implicit end */
            }
        }
        return;
    }

// REVIEWED:NAIVE
    if (strcmp(form, "while") == 0 || strcmp(form, "loop") == 0) {
        /* Mark as in loop for any borrowed variables */
        ctx->in_loop = true;
        for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
            omni_analyze_borrows(ctx, omni_car(rest));
        }
        ctx->in_loop = false;
        return;
    }

    /* Default: recurse */
    for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
        omni_analyze_borrows(ctx, omni_car(rest));
    }
}

/* ============== Interprocedural Summaries ============== */

const char* omni_param_ownership_name(ParamOwnership ownership) {
    switch (ownership) {
        case PARAM_BORROWED:    return "borrowed";
        case PARAM_CONSUMED:    return "consumed";
        case PARAM_PASSTHROUGH: return "passthrough";
        case PARAM_CAPTURED:    return "captured";
        default:                return "unknown";
    }
}

const char* omni_return_ownership_name(ReturnOwnership ownership) {
    switch (ownership) {
        case RETURN_FRESH:       return "fresh";
        case RETURN_PASSTHROUGH: return "passthrough";
        case RETURN_BORROWED:    return "borrowed";
        case RETURN_NONE:        return "none";
        default:                 return "unknown";
    }
}

static FunctionSummary* find_or_create_function_summary(AnalysisContext* ctx, const char* func_name) {
    for (FunctionSummary* f = ctx->function_summaries; f; f = f->next) {
        if (strcmp(f->name, func_name) == 0) return f;
    }

    FunctionSummary* f = malloc(sizeof(FunctionSummary));
    f->name = strdup(func_name);
    f->params = NULL;
    f->param_count = 0;
    f->return_ownership = RETURN_FRESH;
    f->return_param_index = -1;
    f->allocates = false;
    f->has_side_effects = false;
    f->next = ctx->function_summaries;
    ctx->function_summaries = f;
    return f;
}

static ParamSummary* add_param_summary(FunctionSummary* func, const char* param_name,
                                     const char* type_annotation) {
    ParamSummary* p = malloc(sizeof(ParamSummary));
    p->name = strdup(param_name);
    p->type_annotation = type_annotation ? strdup(type_annotation) : NULL;
    p->ownership = PARAM_BORROWED;  /* Default: borrowed */
    p->passthrough_index = -1;
    p->next = func->params;
    func->params = p;
    func->param_count++;
    return p;
}

static ParamSummary* get_param_by_name(FunctionSummary* func, const char* param_name) {
    for (ParamSummary* p = func->params; p; p = p->next) {
        if (strcmp(p->name, param_name) == 0) return p;
    }
    return NULL;
}

ParamSummary* omni_get_param_by_index(FunctionSummary* func, int index) {
    int i = func->param_count - 1;  /* Params are prepended, so reverse order */
    for (ParamSummary* p = func->params; p; p = p->next) {
        if (i == index) return p;
        i--;
    }
    return NULL;
}

/* Helper to get param index from name */
static int get_param_index(FunctionSummary* func, const char* param_name) {
    int i = func->param_count - 1;
    for (ParamSummary* p = func->params; p; p = p->next) {
        if (strcmp(p->name, param_name) == 0) return i;
        i--;
    }
    return -1;
}

/* Analyze body to determine parameter usage */
static void analyze_body_for_summary(AnalysisContext* ctx, FunctionSummary* func, OmniValue* body, bool in_return_pos);

static void analyze_body_for_summary(AnalysisContext* ctx, FunctionSummary* func, OmniValue* body, bool in_return_pos) {
    if (!body || omni_is_nil(body)) return;

    /* Symbol - check if it's a parameter being returned */
    if (omni_is_sym(body)) {
        if (in_return_pos) {
            int idx = get_param_index(func, body->str_val);
            if (idx >= 0) {
                /* Parameter is returned directly - passthrough */
                func->return_ownership = RETURN_PASSTHROUGH;
                func->return_param_index = idx;
                ParamSummary* p = get_param_by_name(func, body->str_val);
                if (p) p->ownership = PARAM_PASSTHROUGH;
            }
        }
        return;
    }

    if (!omni_is_cell(body)) return;

    OmniValue* head = omni_car(body);
    if (!omni_is_sym(head)) {
        /* Non-symbol head - recurse */
        for (OmniValue* rest = body; omni_is_cell(rest); rest = omni_cdr(rest)) {
            analyze_body_for_summary(ctx, func, omni_car(rest), false);
        }
        return;
    }

    const char* form = head->str_val;

    /* Check for allocation forms */
    if (strcmp(form, "cons") == 0 || strcmp(form, "list") == 0 ||
        strcmp(form, "vector") == 0 || strcmp(form, "make") == 0 ||
        strcmp(form, "mk-int") == 0 || strcmp(form, "mk-float") == 0 ||
        strcmp(form, "new") == 0) {
        func->allocates = true;
        if (in_return_pos) {
            func->return_ownership = RETURN_FRESH;
        }
    }

    /* Check for side effects */
    if (strcmp(form, "set!") == 0 || strcmp(form, "display") == 0 ||
        strcmp(form, "print") == 0 || strcmp(form, "write") == 0 ||
        strcmp(form, "send!") == 0 || strcmp(form, "put!") == 0) {
        func->has_side_effects = true;
    }

    /* Check for consuming operations - these consume their arguments */
    if (strcmp(form, "free") == 0 || strcmp(form, "free!") == 0) {
        /* Argument is consumed */
        OmniValue* arg = cadr(body);
        if (omni_is_sym(arg)) {
            ParamSummary* p = get_param_by_name(func, arg->str_val);
            if (p) p->ownership = PARAM_CONSUMED;
        }
    }

    /* Handle if - analyze branches */
    if (strcmp(form, "if") == 0) {
        analyze_body_for_summary(ctx, func, cadr(body), false);     /* condition */
        analyze_body_for_summary(ctx, func, caddr(body), in_return_pos);  /* then */
        OmniValue* else_branch = cdddr(body);
        if (omni_is_cell(else_branch)) {
            analyze_body_for_summary(ctx, func, omni_car(else_branch), in_return_pos);
        }
        return;
    }

    /* Handle let - check for captures */
    if (strcmp(form, "let") == 0 || strcmp(form, "let*") == 0) {
        OmniValue* bindings = cadr(body);
        /* Check if any param is captured in a binding */
        for (OmniValue* b = bindings; omni_is_cell(b); b = omni_cdr(b)) {
            OmniValue* binding = omni_car(b);
            if (omni_is_cell(binding)) {
                OmniValue* init = cadr(binding);
                if (omni_is_cell(init)) {
                    OmniValue* init_head = omni_car(init);
                    if (omni_is_sym(init_head) &&
                        (strcmp(init_head->str_val, "lambda") == 0 ||
                         strcmp(init_head->str_val, "fn") == 0)) {
                        /* Lambda - check for captured params */
                        for (OmniValue* rest = init; omni_is_cell(rest); rest = omni_cdr(rest)) {
                            OmniValue* elem = omni_car(rest);
                            if (omni_is_sym(elem)) {
                                ParamSummary* p = get_param_by_name(func, elem->str_val);
                                if (p) p->ownership = PARAM_CAPTURED;
                            }
                        }
                    }
                }
            }
        }
        /* Analyze body */
        OmniValue* let_body = caddr(body);
        analyze_body_for_summary(ctx, func, let_body, in_return_pos);
        return;
    }

    /* Handle lambda - check for captured params */
    if (strcmp(form, "lambda") == 0 || strcmp(form, "fn") == 0) {
        /* Check if any parameter is referenced inside lambda */
        OmniValue* lambda_body = caddr(body);
        for (OmniValue* rest = lambda_body; omni_is_cell(rest); rest = omni_cdr(rest)) {
            OmniValue* elem = omni_car(rest);
            if (omni_is_sym(elem)) {
                ParamSummary* p = get_param_by_name(func, elem->str_val);
                if (p) {
                    p->ownership = PARAM_CAPTURED;
                    func->has_side_effects = true;  /* Closure creation is a side effect */
                }
            }
        }
        return;
    }

    /* Handle begin/progn - last expression is in return position */
    if (strcmp(form, "begin") == 0 || strcmp(form, "progn") == 0) {
        OmniValue* exprs = omni_cdr(body);
        while (omni_is_cell(exprs)) {
            OmniValue* e = omni_car(exprs);
            bool is_last = omni_is_nil(omni_cdr(exprs));
            analyze_body_for_summary(ctx, func, e, is_last && in_return_pos);
            exprs = omni_cdr(exprs);
        }
        return;
    }

    /* Default: recurse on all subexpressions */
    for (OmniValue* rest = omni_cdr(body); omni_is_cell(rest); rest = omni_cdr(rest)) {
        analyze_body_for_summary(ctx, func, omni_car(rest), false);
    }
}

void omni_analyze_function_summary(AnalysisContext* ctx, OmniValue* func_def) {
    /*
     * Analyze a function definition and create its summary.
     *
     * Expected forms:
     * (define (name params...) body...)
     * (define name (lambda (params...) body...))
     * (defn name (params...) body...)
     */
    if (!omni_is_cell(func_def)) return;

    OmniValue* head = omni_car(func_def);
    if (!omni_is_sym(head)) return;

    const char* form = head->str_val;
    const char* func_name = NULL;
    OmniValue* params = NULL;
    OmniValue* body = NULL;

    if (strcmp(form, "define") == 0) {
        OmniValue* name_or_sig = cadr(func_def);

        if (omni_is_cell(name_or_sig)) {
            /* (define (name params...) body...) */
            OmniValue* fname = omni_car(name_or_sig);
            if (!omni_is_sym(fname)) return;
            func_name = fname->str_val;
            params = omni_cdr(name_or_sig);
            body = caddr(func_def);
        } else if (omni_is_sym(name_or_sig)) {
            /* (define name (lambda (params...) body...)) */
            func_name = name_or_sig->str_val;
            OmniValue* val = caddr(func_def);
            if (omni_is_cell(val)) {
                OmniValue* val_head = omni_car(val);
                if (omni_is_sym(val_head) &&
                    (strcmp(val_head->str_val, "lambda") == 0 ||
                     strcmp(val_head->str_val, "fn") == 0)) {
                    params = cadr(val);
                    body = caddr(val);
                }
            }
        }
    } else if (strcmp(form, "defn") == 0) {
        /* (defn name (params...) body...) */
        OmniValue* name_val = cadr(func_def);
        if (!omni_is_sym(name_val)) return;
        func_name = name_val->str_val;
        params = caddr(func_def);
        body = caddr(omni_cdr(func_def));  /* cadddr */
    } else if (strcmp(form, "lambda") == 0 || strcmp(form, "fn") == 0) {
        /* Anonymous lambda - use position-based name */
        func_name = "<lambda>";
        params = cadr(func_def);
        body = caddr(func_def);
    } else {
        return;  /* Not a function definition */
    }

    if (!func_name) return;

    /* Create function summary */
    FunctionSummary* summary = find_or_create_function_summary(ctx, func_name);

    /*
     * Phase 22: Extract ^:where constraints for diagonal dispatch
     *
     * Scan function definition for metadata markers like:
     *   (define ^:where [T {Number}] ...)
     */
    OmniValue* scan = omni_cdr(func_def);  /* Skip 'define' */
    while (omni_is_cell(scan)) {
        OmniValue* item = omni_car(scan);
        if (omni_is_sym(item) && item->str_val &&
            strlen(item->str_val) > 2 &&
            item->str_val[0] == '^' && item->str_val[1] == ':') {

            const char* meta_key = item->str_val + 2;
            OmniValue* value_cell = omni_cdr(scan);

            if (strcmp(meta_key, "where") == 0 && omni_is_cell(value_cell)) {
                OmniValue* where_value = omni_car(value_cell);
                /* Parse constraints and attach to summary */
                summary->constraints = omni_parse_where_constraints(where_value);
                break;
            }
            /* Skip key and value */
            scan = omni_cdr(value_cell);
        } else {
            break;
        }
    }

    /* Add parameter summaries - supports both plain symbols and Slot syntax */
    if (omni_is_cell(params)) {
        for (OmniValue* p = params; omni_is_cell(p); p = omni_cdr(p)) {
            OmniValue* param = omni_car(p);

            /* Extract parameter name (handles both symbols and Slot syntax) */
            OmniValue* param_name_val = NULL;
            if (omni_is_sym(param)) {
                param_name_val = param;
            } else if (omni_is_array(param)) {
                /* Slot syntax: [x] or [x {Type}] - first element is the name */
                if (param->array.len > 0) {
                    param_name_val = param->array.data[0];
                }
            }

            if (param_name_val && omni_is_sym(param_name_val)) {
                /* Extract type annotation from Slot syntax */
                char* type_annotation = omni_extract_type_annotation(param);
                add_param_summary(summary, param_name_val->str_val, type_annotation);
            }
        }
    } else if (omni_is_array(params)) {
        for (size_t i = 0; i < params->array.len; i++) {
            OmniValue* param = params->array.data[i];

            /* Extract parameter name (handles both symbols and Slot syntax) */
            OmniValue* param_name_val = NULL;
            if (omni_is_sym(param)) {
                param_name_val = param;
            } else if (omni_is_array(param)) {
                /* Slot syntax: [x] or [x {Type}] - first element is the name */
                if (param->array.len > 0) {
                    param_name_val = param->array.data[0];
                }
            }

            if (param_name_val && omni_is_sym(param_name_val)) {
                /* Extract type annotation from Slot syntax */
                char* type_annotation = omni_extract_type_annotation(param);
                add_param_summary(summary, param_name_val->str_val, type_annotation);
            }
        }
    }

    /* Analyze body for ownership patterns */
    if (body) {
        analyze_body_for_summary(ctx, summary, body, true);
    }

    /* If body is nil/empty, return RETURN_NONE */
    if (!body || omni_is_nil(body)) {
        summary->return_ownership = RETURN_NONE;
    }
}

FunctionSummary* omni_get_function_summary(AnalysisContext* ctx, const char* func_name) {
    for (FunctionSummary* f = ctx->function_summaries; f; f = f->next) {
        if (strcmp(f->name, func_name) == 0) return f;
    }
    return NULL;
}

ParamOwnership omni_get_param_ownership(AnalysisContext* ctx, const char* func_name,
                                        const char* param_name) {
    FunctionSummary* f = omni_get_function_summary(ctx, func_name);
    if (!f) return PARAM_BORROWED;  /* Default: borrowed */

    for (ParamSummary* p = f->params; p; p = p->next) {
        if (strcmp(p->name, param_name) == 0) {
            return p->ownership;
        }
    }
    return PARAM_BORROWED;
}

ReturnOwnership omni_get_return_ownership(AnalysisContext* ctx, const char* func_name) {
    FunctionSummary* f = omni_get_function_summary(ctx, func_name);
    if (!f) return RETURN_FRESH;  /* Default: fresh allocation */
    return f->return_ownership;
}

bool omni_function_consumes_param(AnalysisContext* ctx, const char* func_name,
                                  const char* param_name) {
    ParamOwnership ownership = omni_get_param_ownership(ctx, func_name, param_name);
    return ownership == PARAM_CONSUMED;
}

bool omni_caller_should_free_arg(AnalysisContext* ctx, const char* func_name,
                                 int arg_index) {
    FunctionSummary* f = omni_get_function_summary(ctx, func_name);
    if (!f) return true;  /* Default: caller should free (borrowed semantics) */

    ParamSummary* p = omni_get_param_by_index(f, arg_index);
    if (!p) return true;

    /* Caller should NOT free if:
     * - Parameter is consumed (callee frees)
     * - Parameter is captured (callee takes ownership)
     * - Parameter is passthrough (returned to caller)
     */
    switch (p->ownership) {
        case PARAM_CONSUMED:
        case PARAM_CAPTURED:
            return false;  /* Callee takes ownership */
        case PARAM_PASSTHROUGH:
            /* Caller gets it back via return */
            return false;
        case PARAM_BORROWED:
        default:
            return true;  /* Caller keeps ownership */
    }
}

/* ============== Concurrency Ownership Inference ============== */

const char* omni_thread_locality_name(ThreadLocality locality) {
    switch (locality) {
        case THREAD_LOCAL:     return "local";
        case THREAD_SHARED:    return "shared";
        case THREAD_TRANSFER:  return "transfer";
        case THREAD_IMMUTABLE: return "immutable";
        default:               return "unknown";
    }
}

/* DIRECTIVE: NO CHANNELS - omni_channel_op_name removed */

static ThreadLocalityInfo* find_or_create_locality_info(AnalysisContext* ctx, const char* var_name) {
    for (ThreadLocalityInfo* t = ctx->thread_locality; t; t = t->next) {
        if (strcmp(t->var_name, var_name) == 0) return t;
    }

    ThreadLocalityInfo* t = malloc(sizeof(ThreadLocalityInfo));
    t->var_name = strdup(var_name);
    t->locality = THREAD_LOCAL;  /* Default: thread-local */
    t->thread_id = ctx->current_thread_id;
    t->needs_atomic_rc = false;
    t->next = ctx->thread_locality;
    ctx->thread_locality = t;
    return t;
}

void omni_mark_thread_local(AnalysisContext* ctx, const char* var_name, int thread_id) {
    ThreadLocalityInfo* t = find_or_create_locality_info(ctx, var_name);
    t->locality = THREAD_LOCAL;
    t->thread_id = thread_id;
    t->needs_atomic_rc = false;
}

void omni_mark_thread_shared(AnalysisContext* ctx, const char* var_name) {
    ThreadLocalityInfo* t = find_or_create_locality_info(ctx, var_name);
    t->locality = THREAD_SHARED;
    t->thread_id = -1;  /* Shared = not bound to single thread */
    t->needs_atomic_rc = true;  /* Shared data needs atomic RC */
}

ThreadLocality omni_get_thread_locality(AnalysisContext* ctx, const char* var_name) {
    for (ThreadLocalityInfo* t = ctx->thread_locality; t; t = t->next) {
        if (strcmp(t->var_name, var_name) == 0) {
            return t->locality;
        }
    }
    return THREAD_LOCAL;  /* Default: thread-local */
}

bool omni_needs_atomic_rc(AnalysisContext* ctx, const char* var_name) {
    for (ThreadLocalityInfo* t = ctx->thread_locality; t; t = t->next) {
        if (strcmp(t->var_name, var_name) == 0) {
            return t->needs_atomic_rc;
        }
    }
    return false;  /* Default: no atomic RC needed */
}

/* DIRECTIVE: NO CHANNELS
 * Channel transfer tracking functions removed:
 * - omni_is_channel_transferred
 * - omni_record_channel_send
 * - omni_record_channel_recv
 * Use algebraic effects for structured concurrency instead.
 */

void omni_record_thread_spawn(AnalysisContext* ctx, const char* thread_id,
                              char** captured_vars, size_t count) {
    ThreadSpawnInfo* spawn = malloc(sizeof(ThreadSpawnInfo));
    spawn->spawn_pos = ctx->position;
    spawn->thread_id = strdup(thread_id);
    spawn->captured_count = count;
    spawn->captured_vars = NULL;
    spawn->capture_locality = NULL;

    if (count > 0) {
        spawn->captured_vars = malloc(count * sizeof(char*));
        spawn->capture_locality = malloc(count * sizeof(ThreadLocality));
        for (size_t i = 0; i < count; i++) {
            spawn->captured_vars[i] = strdup(captured_vars[i]);

            /* Captured variables become shared by default */
            ThreadLocalityInfo* t = find_or_create_locality_info(ctx, captured_vars[i]);
            t->locality = THREAD_SHARED;
            t->needs_atomic_rc = true;
            spawn->capture_locality[i] = THREAD_SHARED;
        }
    }

    spawn->next = ctx->thread_spawns;
    ctx->thread_spawns = spawn;
}

/* DIRECTIVE: NO CHANNELS - omni_should_free_after_send removed */

// REVIEWED:NAIVE
ThreadSpawnInfo** omni_get_threads_capturing(AnalysisContext* ctx, const char* var_name,
                                             size_t* count) {
    /* Count matching spawns */
    size_t n = 0;
    for (ThreadSpawnInfo* spawn = ctx->thread_spawns; spawn; spawn = spawn->next) {
        for (size_t i = 0; i < spawn->captured_count; i++) {
            if (strcmp(spawn->captured_vars[i], var_name) == 0) {
                n++;
                break;
            }
        }
    }

    if (n == 0) {
        if (count) *count = 0;
        return NULL;
    }

    ThreadSpawnInfo** result = malloc(n * sizeof(ThreadSpawnInfo*));
    size_t idx = 0;
    for (ThreadSpawnInfo* spawn = ctx->thread_spawns; spawn; spawn = spawn->next) {
        for (size_t i = 0; i < spawn->captured_count; i++) {
            if (strcmp(spawn->captured_vars[i], var_name) == 0) {
                result[idx++] = spawn;
                break;
            }
        }
    }

    if (count) *count = n;
    return result;
}

void omni_analyze_concurrency(AnalysisContext* ctx, OmniValue* expr) {
    /*
     * Analyze concurrency patterns:
     * - (spawn ...) or (thread ...) - thread creation
     * - (go ...) - goroutine-style spawn
     * - (async ...) - async block
     * - (atom ...) - atomic reference
     *
     * DIRECTIVE: NO CHANNELS - channel send/recv analysis removed.
     * Use algebraic effects for structured concurrency instead.
     */
    if (!expr) return;

    /* Skip primitives */
    if (omni_is_int(expr) || omni_is_float(expr) ||
        omni_is_char(expr) || omni_is_nil(expr) || omni_is_sym(expr)) {
        return;
    }

    if (!omni_is_cell(expr)) return;

    OmniValue* head = omni_car(expr);
    if (!omni_is_sym(head)) {
        /* Recurse on non-symbol head */
        for (OmniValue* rest = expr; omni_is_cell(rest); rest = omni_cdr(rest)) {
            omni_analyze_concurrency(ctx, omni_car(rest));
        }
        return;
    }

    const char* form = head->str_val;

    /* Detect thread spawn forms */
    if (strcmp(form, "spawn") == 0 || strcmp(form, "thread") == 0 ||
        strcmp(form, "go") == 0 || strcmp(form, "async") == 0) {

        /* Generate thread ID */
        char thread_id[32];
        static int thread_counter = 0;
        snprintf(thread_id, sizeof(thread_id), "thread_%d", thread_counter++);

        /* Collect captured variables from the body */
        /* For now, just scan for symbols in the body */
        OmniValue* body = omni_cdr(expr);
        char** captured = NULL;
        size_t captured_count = 0;
        size_t captured_cap = 0;

        for (OmniValue* b = body; omni_is_cell(b); b = omni_cdr(b)) {
            OmniValue* e = omni_car(b);
            if (omni_is_sym(e)) {
                /* Check if it's a variable from outer scope */
                VarUsage* u = omni_get_var_usage(ctx, e->str_val);
                if (u && u->def_pos < ctx->position) {
                    /* Variable defined before spawn - captured */
                    if (captured_count >= captured_cap) {
                        captured_cap = captured_cap ? captured_cap * 2 : 8;
                        captured = realloc(captured, captured_cap * sizeof(char*));
                    }
                    captured[captured_count++] = e->str_val;
                }
            }
        }

        omni_record_thread_spawn(ctx, thread_id, captured, captured_count);
        free(captured);

        /* Analyze body in new thread context */
        int old_thread = ctx->current_thread_id;
        ctx->current_thread_id = thread_counter - 1;
        for (OmniValue* b = body; omni_is_cell(b); b = omni_cdr(b)) {
            omni_analyze_concurrency(ctx, omni_car(b));
        }
        ctx->current_thread_id = old_thread;
        return;
    }

    /* DIRECTIVE: NO CHANNELS - channel send/recv detection removed */

    /* Detect let bindings - recurse into body */
    if (strcmp(form, "let") == 0) {
        OmniValue* body = caddr(expr);
        omni_analyze_concurrency(ctx, body);
        return;
    }

    /* Detect atomic operations */
    if (strcmp(form, "atom") == 0 || strcmp(form, "atomic") == 0) {
        /* (atom initial-value) - creates atomic reference */
        OmniValue* init = cadr(expr);
        if (omni_is_sym(init)) {
            /* The atom contents are shared */
            omni_mark_thread_shared(ctx, init->str_val);
        }
        return;
    }

    if (strcmp(form, "swap!") == 0 || strcmp(form, "reset!") == 0 ||
        strcmp(form, "compare-and-swap!") == 0) {
        /* Atomic modification - value becomes shared */
        OmniValue* atom = cadr(expr);
        OmniValue* value = caddr(expr);
        if (omni_is_sym(value)) {
            omni_mark_thread_shared(ctx, value->str_val);
        }
        (void)atom;  /* Atom itself is already marked shared */
        return;
    }

    /* Default: recurse on all subexpressions */
    for (OmniValue* rest = omni_cdr(expr); omni_is_cell(rest); rest = omni_cdr(rest)) {
        omni_analyze_concurrency(ctx, omni_car(rest));
    }
}

/* ============== Type Registry Implementation ============== */

TypeRegistry* omni_type_registry_new(void) {
    TypeRegistry* reg = malloc(sizeof(TypeRegistry));
    if (!reg) return NULL;
    memset(reg, 0, sizeof(TypeRegistry));
    return reg;
}

void omni_type_registry_free(TypeRegistry* reg) {
    if (!reg) return;

    /* Free types */
    TypeDef* t = reg->types;
    while (t) {
        TypeDef* next = t->next;
        free(t->name);
        for (size_t i = 0; i < t->field_count; i++) {
            free(t->fields[i].name);
            free(t->fields[i].type_name);
        }
        free(t->fields);
        free(t);
        t = next;
    }

    /* Free edges */
    OwnershipEdge* e = reg->edges;
    while (e) {
        OwnershipEdge* next = e->next;
        free(e->from_type);
        free(e->from_field);
        free(e->to_type);
        free(e);
        e = next;
    }

    free(reg);
}

// REVIEWED:NAIVE
static TypeDef* find_or_create_type_def(TypeRegistry* reg, const char* name) {
    for (TypeDef* t = reg->types; t; t = t->next) {
        if (strcmp(t->name, name) == 0) return t;
    }

    TypeDef* t = malloc(sizeof(TypeDef));
    memset(t, 0, sizeof(TypeDef));
    t->name = strdup(name);
    t->field_capacity = 8;
    t->fields = malloc(t->field_capacity * sizeof(TypeField));
    t->next = reg->types;
    reg->types = t;
    reg->type_count++;
    return t;
}

TypeDef* omni_register_type(AnalysisContext* ctx, OmniValue* type_def) {
    /*
     * Expected form: (deftype TypeName
     *                   (field1 Type1)
     *                   (field2 Type2 :weak)
     *                   ...)
     * or: (defstruct TypeName ...)
     */
    if (!ctx->type_registry) {
        ctx->type_registry = omni_type_registry_new();
    }

    if (!omni_is_cell(type_def)) return NULL;

    OmniValue* head = omni_car(type_def);
    if (!omni_is_sym(head)) return NULL;

    const char* form = head->str_val;
    if (strcmp(form, "deftype") != 0 && strcmp(form, "defstruct") != 0) {
        return NULL;
    }

    OmniValue* rest = omni_cdr(type_def);
    if (!omni_is_cell(rest)) return NULL;

    /* Get type name */
    OmniValue* name_val = omni_car(rest);
    if (!omni_is_sym(name_val)) return NULL;
    const char* type_name = name_val->str_val;

    TypeDef* type = find_or_create_type_def(ctx->type_registry, type_name);

    /* Parse fields */
    OmniValue* fields = omni_cdr(rest);
    int field_idx = 0;

    while (omni_is_cell(fields)) {
        OmniValue* field_def = omni_car(fields);

        if (omni_is_cell(field_def)) {
            OmniValue* field_name_val = omni_car(field_def);

            if (omni_is_sym(field_name_val)) {
                const char* field_name = field_name_val->str_val;
                const char* field_type = NULL;
                FieldStrength strength = FIELD_STRONG;
                bool is_mutable = true;

                /* Parse field type and modifiers */
                OmniValue* field_rest = omni_cdr(field_def);
                if (omni_is_cell(field_rest)) {
                    OmniValue* ft = omni_car(field_rest);
                    if (omni_is_sym(ft)) {
                        field_type = ft->str_val;
                    }

                    /* Check for modifiers like :weak, :borrowed, :immutable */
                    OmniValue* mods = omni_cdr(field_rest);
                    while (omni_is_cell(mods)) {
                        OmniValue* mod = omni_car(mods);
                        if (omni_is_sym(mod) || omni_is_keyword(mod)) {
                            const char* mod_str = mod->str_val;
                            if (strcmp(mod_str, ":weak") == 0 || strcmp(mod_str, "weak") == 0) {
                                strength = FIELD_WEAK;
                            } else if (strcmp(mod_str, ":borrowed") == 0 || strcmp(mod_str, "borrowed") == 0) {
                                strength = FIELD_BORROWED;
                            } else if (strcmp(mod_str, ":immutable") == 0 || strcmp(mod_str, "immutable") == 0) {
                                is_mutable = false;
                            }
                        }
                        mods = omni_cdr(mods);
                    }
                }

                /* Auto-detect back-edge by naming convention if not explicitly weak */
                if (strength == FIELD_STRONG && is_back_edge_name(field_name)) {
                    strength = FIELD_WEAK;
                }

                omni_type_add_field(type, field_name, field_type, strength, is_mutable);
                field_idx++;
            }
        }

        fields = omni_cdr(fields);
    }

    /* Also run shape analysis */
    omni_analyze_shape(ctx, type_def);

    return type;
}

// REVIEWED:NAIVE
TypeDef* omni_get_type(AnalysisContext* ctx, const char* name) {
    if (!ctx->type_registry) return NULL;

    for (TypeDef* t = ctx->type_registry->types; t; t = t->next) {
        if (strcmp(t->name, name) == 0) return t;
    }
    return NULL;
}

// REVIEWED:NAIVE
TypeField* omni_get_type_field(TypeDef* type, const char* field_name) {
    if (!type) return NULL;

    for (size_t i = 0; i < type->field_count; i++) {
        if (strcmp(type->fields[i].name, field_name) == 0) {
            return &type->fields[i];
        }
    }
    return NULL;
}

void omni_type_add_field(TypeDef* type, const char* name, const char* field_type,
                         FieldStrength strength, bool is_mutable) {
    if (!type) return;

    if (type->field_count >= type->field_capacity) {
        type->field_capacity *= 2;
        type->fields = realloc(type->fields, type->field_capacity * sizeof(TypeField));
    }

    TypeField* f = &type->fields[type->field_count];
    f->name = strdup(name);
    f->type_name = field_type ? strdup(field_type) : NULL;
    f->strength = strength;
    f->is_mutable = is_mutable;
    f->index = (int)type->field_count;
    type->field_count++;
}

void omni_build_ownership_graph(AnalysisContext* ctx) {
    if (!ctx->type_registry) return;
    if (ctx->type_registry->graph_built) return;

    TypeRegistry* reg = ctx->type_registry;

// REVIEWED:NAIVE
    /* Build edges from each type's fields */
    for (TypeDef* t = reg->types; t; t = t->next) {
        for (size_t i = 0; i < t->field_count; i++) {
            TypeField* f = &t->fields[i];
            if (!f->type_name) continue;

            /* Check if field type is a registered type */
            TypeDef* target = omni_get_type(ctx, f->type_name);
            if (!target) continue;

            /* Add edge */
            OwnershipEdge* e = malloc(sizeof(OwnershipEdge));
            e->from_type = strdup(t->name);
            e->from_field = strdup(f->name);
            e->to_type = strdup(f->type_name);
            e->is_back_edge = (f->strength == FIELD_WEAK);
            e->next = reg->edges;
            reg->edges = e;
        }
    }

    reg->graph_built = true;
}

/* DFS state for cycle detection */
typedef struct {
    char** visited;
    size_t visited_count;
    char** stack;
    size_t stack_count;
} CycleDetectState;

// REVIEWED:NAIVE
static bool in_array(const char* name, char** arr, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(arr[i], name) == 0) return true;
    }
    return false;
}

static bool dfs_find_cycle(AnalysisContext* ctx, const char* type_name, CycleDetectState* state) {
    if (in_array(type_name, state->stack, state->stack_count)) {
        return true;  /* Cycle found */
    }

    if (in_array(type_name, state->visited, state->visited_count)) {
        return false;  /* Already processed */
    }

    /* Push to stack */
    state->stack = realloc(state->stack, (state->stack_count + 1) * sizeof(char*));
    state->stack[state->stack_count++] = (char*)type_name;

    /* Visit neighbors */
    for (OwnershipEdge* e = ctx->type_registry->edges; e; e = e->next) {
        if (strcmp(e->from_type, type_name) == 0 && !e->is_back_edge) {
            if (dfs_find_cycle(ctx, e->to_type, state)) {
                return true;
            }
        }
    }

    /* Pop from stack */
    state->stack_count--;

    /* Mark visited */
    state->visited = realloc(state->visited, (state->visited_count + 1) * sizeof(char*));
    state->visited[state->visited_count++] = (char*)type_name;

    return false;
}

void omni_analyze_back_edges(AnalysisContext* ctx) {
    if (!ctx->type_registry) return;
    if (ctx->type_registry->back_edges_analyzed) return;

    omni_build_ownership_graph(ctx);

    TypeRegistry* reg = ctx->type_registry;

    /* For each type, check if removing weak edges prevents cycles */
    for (TypeDef* t = reg->types; t; t = t->next) {
        CycleDetectState state = {0};

        /* Check if type participates in cycles */
        if (dfs_find_cycle(ctx, t->name, &state)) {
            t->has_cycles = true;
            t->shape = SHAPE_CYCLIC;

            /* Mark second reference to same type as back-edge */
            int same_type_count = 0;
            for (size_t i = 0; i < t->field_count; i++) {
                TypeField* f = &t->fields[i];
                if (f->type_name && strcmp(f->type_name, t->name) == 0) {
                    same_type_count++;
                    if (same_type_count > 1 && f->strength == FIELD_STRONG) {
                        /* Mark as back-edge */
                        f->strength = FIELD_WEAK;

                        /* Update shape info */
                        ShapeInfo* shape = find_or_create_shape_info(ctx, t->name);
                        add_back_edge_field(shape, f->name);

                        /* Update edge in graph */
                        // REVIEWED:NAIVE
                        for (OwnershipEdge* e = reg->edges; e; e = e->next) {
                            if (strcmp(e->from_type, t->name) == 0 &&
                                strcmp(e->from_field, f->name) == 0) {
                                e->is_back_edge = true;
                                break;
                            }
                        }
                    }
                }
            }
        }

        free(state.visited);
        free(state.stack);
    }

    reg->back_edges_analyzed = true;
}

bool omni_is_weak_field(AnalysisContext* ctx, const char* type_name, const char* field_name) {
    TypeDef* type = omni_get_type(ctx, type_name);
    if (!type) return false;

    TypeField* field = omni_get_type_field(type, field_name);
    if (!field) return false;

    return field->strength == FIELD_WEAK;
}

FieldStrength omni_get_field_strength(AnalysisContext* ctx, const char* type_name,
                                       const char* field_name) {
    TypeDef* type = omni_get_type(ctx, type_name);
    if (!type) return FIELD_STRONG;

    TypeField* field = omni_get_type_field(type, field_name);
    if (!field) return FIELD_STRONG;

    return field->strength;
}

const char* omni_field_strength_name(FieldStrength strength) {
    switch (strength) {
        case FIELD_STRONG: return "strong";
        case FIELD_WEAK: return "weak";
        case FIELD_BORROWED: return "borrowed";
        default: return "unknown";
    }
}

/* ============== Constructor-Level Ownership Tracking ============== */

void omni_register_constructor_owner(AnalysisContext* ctx, const char* var_name,
                                      const char* type_name, int pos) {
    if (!ctx || !var_name || !type_name) return;

    ConstructorOwnership* owner = malloc(sizeof(ConstructorOwnership));
    owner->var_name = strdup(var_name);
    owner->type_name = strdup(type_name);
    owner->construct_pos = pos;
    owner->scope_id = ctx->next_scope_id;
    owner->is_primary_owner = true;
    owner->next = ctx->constructor_owners;
    ctx->constructor_owners = owner;
}

bool omni_is_primary_owner(AnalysisContext* ctx, const char* var_name) {
    if (!ctx || !var_name) return false;

    for (ConstructorOwnership* o = ctx->constructor_owners; o; o = o->next) {
        if (strcmp(o->var_name, var_name) == 0) {
            return o->is_primary_owner;
        }
    }
    return false;
}

const char* omni_get_constructed_type(AnalysisContext* ctx, const char* var_name) {
    if (!ctx || !var_name) return NULL;

    for (ConstructorOwnership* o = ctx->constructor_owners; o; o = o->next) {
        if (strcmp(o->var_name, var_name) == 0) {
            return o->type_name;
        }
    }
    return NULL;
}

void omni_register_field_assignment(AnalysisContext* ctx, const char* target_var,
                                     const char* field_name, const char* value_var,
                                     int pos) {
    if (!ctx || !target_var || !field_name) return;

    FieldAssignment* assign = malloc(sizeof(FieldAssignment));
    assign->target_var = strdup(target_var);
    assign->field_name = strdup(field_name);
    assign->value_var = value_var ? strdup(value_var) : NULL;
    assign->assign_pos = pos;
    assign->is_back_edge = omni_is_back_edge_pattern(field_name);
    assign->is_weak_assign = false;

    /* Check if this should be a weak assignment:
     * 1. Field is a back-edge pattern, OR
     * 2. Value is already owned by another variable
     */
    if (assign->is_back_edge) {
        assign->is_weak_assign = true;
    } else if (value_var) {
        /* Check if value_var is already owned by someone else */
        for (ConstructorOwnership* o = ctx->constructor_owners; o; o = o->next) {
            if (strcmp(o->var_name, value_var) == 0 && o->is_primary_owner) {
                /* value_var is already owned - this should be a weak reference */
                assign->is_weak_assign = true;
                break;
            }
        }
    }

    assign->next = ctx->field_assignments;
    ctx->field_assignments = assign;
}

bool omni_is_weak_assignment(AnalysisContext* ctx, const char* target_var,
                              const char* field_name, const char* value_var) {
    if (!ctx) return false;

    /* First check if we've already analyzed this assignment */
    for (FieldAssignment* a = ctx->field_assignments; a; a = a->next) {
        if (strcmp(a->target_var, target_var) == 0 &&
            strcmp(a->field_name, field_name) == 0) {
            if (a->value_var && value_var && strcmp(a->value_var, value_var) == 0) {
                return a->is_weak_assign;
            }
        }
    }

    /* Otherwise, check the rules directly */
    if (omni_is_back_edge_pattern(field_name)) {
        return true;
    }

    if (value_var && omni_is_primary_owner(ctx, value_var)) {
        return true;
    }

    return false;
}

static void free_constructor_ownership(ConstructorOwnership* owner) {
    while (owner) {
        ConstructorOwnership* next = owner->next;
        free(owner->var_name);
        free(owner->type_name);
        free(owner);
        owner = next;
    }
}

static void free_field_assignments(FieldAssignment* assign) {
    while (assign) {
        FieldAssignment* next = assign->next;
        free(assign->target_var);
        free(assign->field_name);
        free(assign->value_var);
        free(assign);
        assign = next;
    }
}

void omni_free_constructor_owners(AnalysisContext* ctx) {
    if (!ctx) return;
    free_constructor_ownership(ctx->constructor_owners);
    ctx->constructor_owners = NULL;
    free_field_assignments(ctx->field_assignments);
    ctx->field_assignments = NULL;
}

/* ============== Phase 15: Scoped Escape Analysis ============== */

/*
 * This section implements branch-level region narrowing.
 * Variables defined in non-escaping branches can be allocated on the stack
 * or in a scratch arena instead of the parent RC-managed region.
 */

/* ============== Debug/Utility Functions ============== */

const char* omni_escape_target_name(EscapeTarget target) {
    switch (target) {
        case ESCAPE_TARGET_NONE:   return "NONE";
        case ESCAPE_TARGET_PARENT: return "PARENT";
        case ESCAPE_TARGET_RETURN: return "RETURN";
        case ESCAPE_TARGET_GLOBAL: return "GLOBAL";
        default:                   return "UNKNOWN";
    }
}

/* ============== Scope Tree Management ============== */

/*
 * omni_scope_enter - Enter a new scope (branch, let body, etc.)
 *
 * Creates a new ScopeInfo node and links it into the scope tree.
 * Called when entering:
 *   - if/then branch
 *   - if/else branch
 *   - let body
 *   - lambda body
 *   - loop body
 */
ScopeInfo* omni_scope_enter(AnalysisContext* ctx, const char* scope_type) {
    if (!ctx) return NULL;

    ScopeInfo* scope = malloc(sizeof(ScopeInfo));
    memset(scope, 0, sizeof(ScopeInfo));

    scope->scope_id = ctx->next_scope_id_counter++;
    scope->start_position = ctx->position;
    scope->end_position = -1;  /* Will be set on exit */
    scope->variables = NULL;
    scope->parent = ctx->current_scope;
    scope->children = NULL;
    scope->next_sibling = NULL;

    /* Calculate depth */
    if (ctx->current_scope) {
        scope->scope_depth = ctx->current_scope->scope_depth + 1;

        /* Add to parent's children list */
        ScopeInfo* sibling = ctx->current_scope->children;
        if (!sibling) {
            ctx->current_scope->children = scope;
        } else {
            while (sibling->next_sibling) {
                sibling = sibling->next_sibling;
            }
            sibling->next_sibling = scope;
        }
    } else {
        scope->scope_depth = 0;

        /* This is the root scope */
        if (!ctx->root_scope) {
            ctx->root_scope = scope;
        }
    }

    /* Update current scope */
    ctx->current_scope = scope;

    return scope;
}

/*
 * omni_scope_exit - Exit the current scope
 *
 * Finalizes the current scope and moves back to the parent.
 * Called when exiting a branch, let body, etc.
 */
void omni_scope_exit(AnalysisContext* ctx) {
    if (!ctx || !ctx->current_scope) return;

    /* Set end position */
    ctx->current_scope->end_position = ctx->position;

    /* Move to parent scope */
    ctx->current_scope = ctx->current_scope->parent;
}

/*
 * omni_get_current_scope - Get the current scope during analysis
 */
ScopeInfo* omni_get_current_scope(AnalysisContext* ctx) {
    return ctx ? ctx->current_scope : NULL;
}

/* ============== Scoped Variable Tracking ============== */

/*
 * omni_scope_add_var - Add a variable to the current scope
 *
 * Creates a ScopedVarInfo entry for the variable in the current scope.
 * Initially marked as ESCAPE_TARGET_NONE (doesn't escape).
 */
ScopedVarInfo* omni_scope_add_var(AnalysisContext* ctx, const char* var_name,
                                  int def_position, bool is_param) {
    if (!ctx || !ctx->current_scope || !var_name) return NULL;

    /* Check if variable already exists in this scope */
    for (ScopedVarInfo* v = ctx->current_scope->variables; v; v = v->next) {
        if (strcmp(v->var_name, var_name) == 0) {
            return v;  /* Already exists */
        }
    }

    ScopedVarInfo* var = malloc(sizeof(ScopedVarInfo));
    var->var_name = strdup(var_name);
    var->defining_scope_depth = ctx->current_scope->scope_depth;
    var->escape_target = ESCAPE_TARGET_NONE;  /* Initially doesn't escape */
    var->def_position = def_position;
    var->is_param = is_param;
    var->needs_cleanup = !is_param;  /* Parameters don't need cleanup */
    var->shape = SHAPE_UNKNOWN;
    var->next = ctx->current_scope->variables;

    ctx->current_scope->variables = var;

    return var;
}

/*
 * omni_scope_mark_escape - Mark a variable as escaping to a specific target
 *
 * Updates the escape target for a variable, taking the maximum
 * (i.e., if already marked as ESCAPE_TARGET_RETURN, don't downgrade to ESCAPE_TARGET_PARENT).
 */
void omni_scope_mark_escape(AnalysisContext* ctx, const char* var_name,
                            EscapeTarget target) {
    if (!ctx || !var_name) return;

    /* Search for the variable in the scope tree (starting from current) */
    ScopedVarInfo* var = omni_scope_find_var(ctx, var_name);
    if (var) {
        /* Take the maximum escape target */
        if (target > var->escape_target) {
            var->escape_target = target;
        }
    }
}

/*
 * omni_scope_get_escape_target - Get the escape target for a variable
 */
EscapeTarget omni_scope_get_escape_target(AnalysisContext* ctx, const char* var_name) {
    if (!ctx || !var_name) return ESCAPE_TARGET_GLOBAL;  /* Conservative default */

    ScopedVarInfo* var = omni_scope_find_var(ctx, var_name);
    if (var) {
        return var->escape_target;
    }

    return ESCAPE_TARGET_GLOBAL;  /* Unknown variable - assume it escapes */
}

/*
 * omni_scope_get_defining_scope - Find the scope where a variable was defined
 */
ScopeInfo* omni_scope_get_defining_scope(AnalysisContext* ctx, const char* var_name) {
    if (!ctx || !var_name) return NULL;

    /* Search up the scope tree */
    for (ScopeInfo* scope = ctx->current_scope; scope; scope = scope->parent) {
        for (ScopedVarInfo* v = scope->variables; v; v = v->next) {
            if (strcmp(v->var_name, var_name) == 0) {
                return scope;
            }
        }
    }

    return NULL;
}

/* ============== Scoped Variable Query Functions ============== */

/*
 * omni_scope_find_var - Find a variable by name (searches up the scope tree)
 *
 * Searches from the current scope up through parent scopes.
 * Returns NULL if not found.
 */
ScopedVarInfo* omni_scope_find_var(AnalysisContext* ctx, const char* var_name) {
    if (!ctx || !var_name) return NULL;

    /* Search up the scope tree */
    for (ScopeInfo* scope = ctx->current_scope; scope; scope = scope->parent) {
        for (ScopedVarInfo* v = scope->variables; v; v = v->next) {
            if (strcmp(v->var_name, var_name) == 0) {
                return v;
            }
        }
    }

    return NULL;
}

/*
 * omni_scope_find_var_in_scope - Find a variable in a specific scope
 */
ScopedVarInfo* omni_scope_find_var_in_scope(ScopeInfo* scope, const char* var_name) {
    if (!scope || !var_name) return NULL;

    for (ScopedVarInfo* v = scope->variables; v; v = v->next) {
        if (strcmp(v->var_name, var_name) == 0) {
            return v;
        }
    }

    return NULL;
}

/*
 * omni_scope_get_cleanup_vars - Get variables that need cleanup at scope exit
 *
 * Returns an array of variable names that should be freed/cleaned up
 * when exiting this scope. Only includes variables that:
 *   1. Don't escape (ESCAPE_TARGET_NONE)
 *   2. Are not parameters
 *   3. Have needs_cleanup = true
 */
char** omni_scope_get_cleanup_vars(ScopeInfo* scope, size_t* out_count) {
    if (!scope) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    // REVIEWED:NAIVE
    /* Count variables that need cleanup */
    size_t count = 0;
    for (ScopedVarInfo* v = scope->variables; v; v = v->next) {
        if (v->needs_cleanup && v->escape_target == ESCAPE_TARGET_NONE && !v->is_param) {
            count++;
        }
    }

    if (count == 0) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    /* Allocate array */
    char** vars = malloc(count * sizeof(char*));
    size_t i = 0;

    for (ScopedVarInfo* v = scope->variables; v; v = v->next) {
        if (v->needs_cleanup && v->escape_target == ESCAPE_TARGET_NONE && !v->is_param) {
            vars[i++] = strdup(v->var_name);
        }
    }

    if (out_count) *out_count = count;
    return vars;
}

/* ============== Scope Tree Cleanup ============== */

static void free_scoped_var(ScopedVarInfo* var) {
    while (var) {
        ScopedVarInfo* next = var->next;
        free(var->var_name);
        free(var);
        var = next;
    }
}

void omni_scope_free(ScopeInfo* scope) {
    if (!scope) return;

    /* Free children first (recursive) */
    ScopeInfo* child = scope->children;
    while (child) {
        ScopeInfo* next_child = child->next_sibling;
        omni_scope_free(child);
        child = next_child;
    }

    /* Free variables */
    free_scoped_var(scope->variables);

    free(scope);
}

void omni_scope_free_tree(AnalysisContext* ctx) {
    if (!ctx) return;

    if (ctx->root_scope) {
        omni_scope_free(ctx->root_scope);
        ctx->root_scope = NULL;
    }

    ctx->current_scope = NULL;
}

/* ============== Debug Printing ============== */

static void indent_for_depth(int depth) {
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }
}

static void print_scope_tree(ScopeInfo* scope, int depth) {
    if (!scope) return;

    indent_for_depth(depth);
    printf("Scope %d (depth %d, pos %d-%d):\n",
           scope->scope_id, scope->scope_depth,
           scope->start_position, scope->end_position);

    /* Print variables */
    for (ScopedVarInfo* v = scope->variables; v; v = v->next) {
        indent_for_depth(depth + 1);
        printf("  %s: escape=%s, param=%d, cleanup=%d\n",
               v->var_name,
               omni_escape_target_name(v->escape_target),
               v->is_param,
               v->needs_cleanup);
    }

    /* Print children */
    for (ScopeInfo* child = scope->children; child; child = child->next_sibling) {
        print_scope_tree(child, depth + 1);
    }
}

void omni_scope_print_tree(AnalysisContext* ctx) {
    if (!ctx || !ctx->root_scope) {
        printf("(no scope tree)\n");
        return;
    }

    printf("=== Scope Tree ===\n");
    print_scope_tree(ctx->root_scope, 0);
    printf("==================\n");
}

/* ============== AST Node to Scope Mapping ============== */

/*
 * omni_scope_map_ast_node - Store a mapping from AST node to scope.
 *
 * This is called during analysis when we enter a new scope for a specific
 * AST node (e.g., the then branch of an if). Later during codegen,
 * we can look up the scope for an AST node.
 */
void omni_scope_map_ast_node(AnalysisContext* ctx, OmniValue* node, ScopeInfo* scope) {
    if (!ctx || !node || !scope) return;

    /* Check if a mapping already exists for this node */
    for (ASTNodeScopeMap* map = ctx->ast_scope_map; map; map = map->next) {
        if (map->ast_node == node) {
            map->scope = scope;  /* Update existing mapping */
            return;
        }
    }

    /* Create new mapping */
    ASTNodeScopeMap* map = malloc(sizeof(ASTNodeScopeMap));
    map->ast_node = node;
    map->scope = scope;
    map->next = ctx->ast_scope_map;
    ctx->ast_scope_map = map;
}

/*
 * omni_scope_find_by_ast_node - Find the scope associated with an AST node.
 *
 * During code generation, we look up the scope that was created for a
 * specific AST node (e.g., the then branch of an if expression).
 */
ScopeInfo* omni_scope_find_by_ast_node(AnalysisContext* ctx, OmniValue* node) {
    if (!ctx || !node) return NULL;

    for (ASTNodeScopeMap* map = ctx->ast_scope_map; map; map = map->next) {
        if (map->ast_node == node) {
            return map->scope;
        }
    }

    return NULL;
}

/*
 * omni_scope_free_ast_map - Free the AST node to scope mapping.
 */
void omni_scope_free_ast_map(AnalysisContext* ctx) {
    if (!ctx) return;

    ASTNodeScopeMap* map = ctx->ast_scope_map;
    while (map) {
        ASTNodeScopeMap* next = map->next;
        free(map);
        map = next;
    }

    ctx->ast_scope_map = NULL;
}

/* ============== Scope Lookup by Position ============== */

/*
 * Helper: Recursively search for a scope containing the given position.
 * Returns the deepest (most specific) scope that contains the position.
 */
static ScopeInfo* find_scope_containing(ScopeInfo* scope, int position) {
    if (!scope) return NULL;

    /* Check if this scope contains the position */
    if (position < scope->start_position ||
        (scope->end_position >= 0 && position > scope->end_position)) {
        return NULL;
    }

    /* Search children first - return the deepest matching scope */
    for (ScopeInfo* child = scope->children; child; child = child->next_sibling) {
        ScopeInfo* result = find_scope_containing(child, position);
        if (result) return result;
    }

    /* This scope contains the position and no deeper scope does */
    return scope;
}

/*
 * omni_scope_find_by_position - Find the scope that contains a given position.
 *
 * During code generation, we need to find which scope was active during
 * analysis for a given AST node. This function looks up the scope by
 * the node's position in the source.
 */
ScopeInfo* omni_scope_find_by_position(AnalysisContext* ctx, int position) {
    if (!ctx || !ctx->root_scope) return NULL;

    return find_scope_containing(ctx->root_scope, position);
}

/* ============== Scoped Escape Analysis Entry Point ============== */

/*
 * Forward declarations for the recursive analysis functions
 */
static void analyze_expr_scoped(AnalysisContext* ctx, OmniValue* expr);
static void analyze_if_scoped(AnalysisContext* ctx, OmniValue* expr);
static void analyze_let_scoped(AnalysisContext* ctx, OmniValue* expr);
static void analyze_lambda_scoped(AnalysisContext* ctx, OmniValue* expr);
static void analyze_symbol_scoped(AnalysisContext* ctx, OmniValue* expr);
static void analyze_application_scoped(AnalysisContext* ctx, OmniValue* expr);

/*
 * omni_analyze_scoped_escape - Main entry point for scoped escape analysis
 *
 * This is a separate analysis pass that builds the scope tree and tracks
 * which variables escape their defining scope. Results are used by the
 * code generator to implement "Region Narrowing".
 */
void omni_analyze_scoped_escape(AnalysisContext* ctx, OmniValue* expr) {
    if (!ctx || !expr) return;

    /* Initialize scope tree if not already initialized */
    if (!ctx->root_scope) {
        /* Create root scope for the function/program */
        omni_scope_enter(ctx, "root");
    }

    /* Run the analysis */
    analyze_expr_scoped(ctx, expr);
}

/*
 * analyze_expr_scoped - Analyze an expression with scope tracking
 */
static void analyze_expr_scoped(AnalysisContext* ctx, OmniValue* expr) {
    if (!expr || omni_is_nil(expr)) {
        ctx->position++;
        return;
    }

    switch (expr->tag) {
        case OMNI_INT:
        case OMNI_FLOAT:
        case OMNI_CHAR:
        case OMNI_KEYWORD:
            ctx->position++;
            break;

        case OMNI_SYM:
            analyze_symbol_scoped(ctx, expr);
            break;

        case OMNI_CELL:
            /* Check for special forms */
            if (omni_is_cell(expr) && omni_is_sym(omni_car(expr))) {
                const char* head = omni_car(expr)->str_val;

                if (strcmp(head, "if") == 0) {
                    analyze_if_scoped(ctx, expr);
                    return;
                }
                if (strcmp(head, "let") == 0 || strcmp(head, "let*") == 0 ||
                    strcmp(head, "letrec") == 0) {
                    analyze_let_scoped(ctx, expr);
                    return;
                }
                if (strcmp(head, "lambda") == 0 || strcmp(head, "fn") == 0) {
                    analyze_lambda_scoped(ctx, expr);
                    return;
                }
            }
            /* Regular application */
            analyze_application_scoped(ctx, expr);
            break;

        case OMNI_ARRAY:
            for (size_t i = 0; i < expr->array.len; i++) {
                analyze_expr_scoped(ctx, expr->array.data[i]);
            }
            break;

        case OMNI_DICT:
            for (size_t i = 0; i < expr->dict.len; i++) {
                analyze_expr_scoped(ctx, expr->dict.keys[i]);
                analyze_expr_scoped(ctx, expr->dict.values[i]);
            }
            break;

        default:
            ctx->position++;
            break;
    }
}

/*
 * analyze_symbol_scoped - Analyze a symbol reference
 *
 * When we see a symbol being used, we check if it's escaping to the
 * current position. For example, if we're in return position and
 * see a variable, that variable escapes via return.
 */
static void analyze_symbol_scoped(AnalysisContext* ctx, OmniValue* expr) {
    const char* name = expr->str_val;

    /* If in return position, mark as escaping via return */
    if (ctx->in_return_position) {
        omni_scope_mark_escape(ctx, name, ESCAPE_TARGET_RETURN);
    }

    ctx->position++;
}

/*
 * analyze_if_scoped - Analyze an if expression with scope tracking
 *
 * Key insight: Each branch gets its own scope. Variables defined in
 * a branch that don't escape that branch can be stack-allocated.
 *
 * We also map the branch AST nodes to their scopes so that during
 * codegen we can look up the correct scope for each branch.
 */
static void analyze_if_scoped(AnalysisContext* ctx, OmniValue* expr) {
    /* (if cond then else) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    OmniValue* cond = omni_car(args);
    args = omni_cdr(args);
    OmniValue* then_branch = omni_is_nil(args) ? NULL : omni_car(args);
    args = omni_cdr(args);
    OmniValue* else_branch = omni_is_nil(args) ? NULL : omni_car(args);

    bool old_return_pos = ctx->in_return_position;
    ctx->in_return_position = false;

    /* Analyze condition in current scope */
    analyze_expr_scoped(ctx, cond);

    /* Analyze then branch in its own scope */
    if (then_branch) {
        ctx->in_return_position = old_return_pos;
        omni_scope_enter(ctx, "if-then");
        /* Map the then branch AST node to this scope */
        omni_scope_map_ast_node(ctx, then_branch, ctx->current_scope);
        analyze_expr_scoped(ctx, then_branch);
        omni_scope_exit(ctx);
    }

    /* Analyze else branch in its own scope */
    if (else_branch) {
        ctx->in_return_position = old_return_pos;
        omni_scope_enter(ctx, "if-else");
        /* Map the else branch AST node to this scope */
        omni_scope_map_ast_node(ctx, else_branch, ctx->current_scope);
        analyze_expr_scoped(ctx, else_branch);
        omni_scope_exit(ctx);
    }

    ctx->in_return_position = old_return_pos;
}

/*
 * analyze_let_scoped - Analyze a let expression with scope tracking
 *
 * The let body gets its own scope. Variables defined in the let are
 * tracked in that scope. If they don't escape the let body, they can
 * be cleaned up at the end of the let.
 *
 * Strict Character Calculus forms:
 *   - List style: (let ((x val) (y val)) body...)
 *   - Slot syntax: (let [x val] [y val] body...)
 *
 * We map the let expression AST node to its scope so that during
 * codegen we can look up the correct scope.
 */
static void analyze_let_scoped(AnalysisContext* ctx, OmniValue* expr) {
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    OmniValue* first = omni_car(args);

    /* Enter new scope for the let */
    omni_scope_enter(ctx, "let");
    /* Map the let expression to this scope */
    omni_scope_map_ast_node(ctx, expr, ctx->current_scope);

    OmniValue* body = NULL;

    /* Slot syntax: (let [x val] [y val] body...) */
    if (omni_is_array(first)) {
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
                OmniValue* val = (len == 2) ? omni_array_get(elem, 1)
                                            : omni_array_get(elem, 2);
                if (omni_is_sym(name)) {
                    int def_pos = ctx->position++;
                    omni_scope_add_var(ctx, name->str_val, def_pos, false);
                    analyze_expr_scoped(ctx, val);
                }
            }
            current = omni_cdr(current);
        }
        body = current;
    }
    /* List style: (let ((x val) (y val)) body...) */
    else if (omni_is_cell(first)) {
        OmniValue* bindings = first;
        while (!omni_is_nil(bindings) && omni_is_cell(bindings)) {
            OmniValue* binding = omni_car(bindings);

            if (omni_is_cell(binding)) {
                OmniValue* name = omni_car(binding);
                OmniValue* val = omni_cdr(binding);
                if (omni_is_cell(val)) val = omni_car(val);  /* cadr */

                if (omni_is_sym(name)) {
                    int def_pos = ctx->position++;
                    omni_scope_add_var(ctx, name->str_val, def_pos, false);
                    if (val) analyze_expr_scoped(ctx, val);
                }
            }

            bindings = omni_cdr(bindings);
        }
        body = omni_cdr(args);
    }

    /* Analyze body in return position (last expression) */
    bool old_return_pos = ctx->in_return_position;
    while (body && !omni_is_nil(body) && omni_is_cell(body)) {
        ctx->in_return_position = old_return_pos && omni_is_nil(omni_cdr(body));
        analyze_expr_scoped(ctx, omni_car(body));
        body = omni_cdr(body);
    }
    ctx->in_return_position = old_return_pos;

    /* Exit let scope */
    omni_scope_exit(ctx);
}

/*
 * analyze_lambda_scoped - Analyze a lambda expression
 *
 * Lambda parameters are added to a new scope for the lambda body.
 * Variables from outer scopes that are referenced are marked as
 * escaping to a closure.
 *
 * We map the lambda expression to its scope for codegen lookup.
 */
static void analyze_lambda_scoped(AnalysisContext* ctx, OmniValue* expr) {
    /* (lambda (params...) body...) */
    OmniValue* args = omni_cdr(expr);
    if (omni_is_nil(args)) return;

    OmniValue* params = omni_car(args);
    OmniValue* body = omni_cdr(args);

    /* Enter new scope for the lambda */
    omni_scope_enter(ctx, "lambda");
    /* Map the lambda expression to this scope */
    omni_scope_map_ast_node(ctx, expr, ctx->current_scope);

    /* Mark that we're in a lambda (for closure capture detection) */
    bool old_in_lambda = ctx->in_lambda;
    ctx->in_lambda = true;

    /* Add parameters to scope */
    if (omni_is_cell(params)) {
        while (!omni_is_nil(params) && omni_is_cell(params)) {
            OmniValue* param = omni_car(params);
            if (omni_is_sym(param)) {
                int def_pos = ctx->position++;
                omni_scope_add_var(ctx, param->str_val, def_pos, true);  /* is_param = true */
            }
            params = omni_cdr(params);
        }
    }

    /* Analyze body */
    bool old_return_pos = ctx->in_return_position;
    while (!omni_is_nil(body) && omni_is_cell(body)) {
        ctx->in_return_position = omni_is_nil(omni_cdr(body));
        analyze_expr_scoped(ctx, omni_car(body));
        body = omni_cdr(body);
    }
    ctx->in_return_position = old_return_pos;

    ctx->in_lambda = old_in_lambda;

    /* Exit lambda scope */
    omni_scope_exit(ctx);
}

/*
 * analyze_application_scoped - Analyze a function application
 *
 * Arguments passed to a function escape to that function.
 * We mark them as ESCAPE_TARGET_PARENT (escaping to caller scope).
 */
static void analyze_application_scoped(AnalysisContext* ctx, OmniValue* expr) {
    /* (func arg1 arg2 ...) */
    OmniValue* func = omni_car(expr);
    OmniValue* args = omni_cdr(expr);

    bool old_return_pos = ctx->in_return_position;
    ctx->in_return_position = false;

    /* Analyze function */
    analyze_expr_scoped(ctx, func);

    /* Analyze arguments - they escape to the function */
    while (!omni_is_nil(args) && omni_is_cell(args)) {
        OmniValue* arg = omni_car(args);

        /* If argument is a symbol, mark it as escaping */
        if (omni_is_sym(arg)) {
            omni_scope_mark_escape(ctx, arg->str_val, ESCAPE_TARGET_PARENT);
        } else {
            analyze_expr_scoped(ctx, arg);
        }

        args = omni_cdr(args);
    }

    ctx->in_return_position = old_return_pos;
    ctx->position++;
}

/* ============== Phase 19: Julia-Aligned Type System Implementation ============== */

/*
 * Metadata Extraction and Processing
 * Handles ^:key metadata attached to definitions.
 */

/* Extract metadata from a form (returns linked list of MetadataEntry) */
MetadataEntry* omni_extract_metadata(OmniValue* form) {
    if (!form || !omni_is_cell(form)) return NULL;

    MetadataEntry* head = NULL;
    OmniValue* current = form;

    while (!omni_is_nil(current) && omni_is_cell(current)) {
        OmniValue* item = omni_car(current);

        /* Check if this item is metadata: ^:key value */
        if (omni_is_cell(item) && omni_is_sym(omni_car(item))) {
            OmniValue* key_sym = omni_car(item);
            const char* key_str = key_sym->str_val;

            /* Check for metadata marker (^) */
            if (key_str && key_str[0] == '^' && key_str[1] == ':') {
                const char* meta_key = key_str + 2;  /* Skip "^:" */
                OmniValue* meta_value = omni_car(omni_cdr(item));

                /* Determine metadata type */
                MetaType meta_type = META_NONE;
                if (strcmp(meta_key, "parent") == 0) meta_type = META_PARENT;
                else if (strcmp(meta_key, "where") == 0) meta_type = META_WHERE;
                else if (strcmp(meta_key, "mutable") == 0) meta_type = META_MUTABLE;
                else if (strcmp(meta_key, "covar") == 0) meta_type = META_COVAR;
                else if (strcmp(meta_key, "contra") == 0) meta_type = META_CONTRA;
                else if (strcmp(meta_key, "seq") == 0) meta_type = META_SEQ;
                else if (strcmp(meta_key, "rec") == 0) meta_type = META_REC;

                /* Create metadata entry */
                MetadataEntry* entry = malloc(sizeof(MetadataEntry));
                entry->type = meta_type;
                entry->key = strdup(meta_key);
                entry->value = meta_value;
                /*
                 * Canonical "double metadata" policy: last-wins.
                 *
                 * We implement this by PREPENDING entries as we traverse the
                 * metadata list left-to-right. This makes later textual entries
                 * appear earlier in the linked list, and omni_get_metadata()
                 * (which returns the first match) will pick the last occurrence.
                 *
                 * NOTE:
                 *   This reverses the order of the metadata list. Today, metadata
                 *   is treated as an unordered set of key/value annotations, so
                 *   preserving source order is not required.
                 */
                entry->next = head;
                head = entry;
            }
        }

        current = omni_cdr(current);
    }

    return head;
}

/* Free metadata list */
void omni_free_metadata(MetadataEntry* metadata) {
    while (metadata) {
        MetadataEntry* next = metadata->next;
        free(metadata->key);
        free(metadata);
        metadata = next;
    }
}

/* Get metadata value by key */
OmniValue* omni_get_metadata(MetadataEntry* metadata, const char* key) {
    while (metadata) {
        if (strcmp(metadata->key, key) == 0) {
            return metadata->value;
        }
        metadata = metadata->next;
    }
    return NULL;
}

/* Check if metadata contains a specific key */
bool omni_has_metadata(MetadataEntry* metadata, const char* key) {
    return omni_get_metadata(metadata, key) != NULL;
}

/*
 * Type Hierarchy Functions
 * Handle inheritance and subtyping relationships.
 */

/* Set parent type for a type definition */
void omni_type_set_parent(TypeDef* type, const char* parent_name) {
    if (!type) return;
    if (type->parent) free(type->parent);
    type->parent = parent_name ? strdup(parent_name) : NULL;
}

/* Get parent type definition */
TypeDef* omni_type_get_parent(AnalysisContext* ctx, TypeDef* type) {
    if (!type || !type->parent) return NULL;
    if (!ctx->type_registry) return NULL;

    /* Search for parent in registry */
    for (TypeDef* t = ctx->type_registry->types; t; t = t->next) {
        if (strcmp(t->name, type->parent) == 0) {
            return t;
        }
    }
    return NULL;
}

/* Check if type_a is a subtype of type_b */
bool omni_type_is_subtype(AnalysisContext* ctx, const char* type_a, const char* type_b) {
    if (!type_a || !type_b) return false;
    if (strcmp(type_a, type_b) == 0) return true;

    /* Check for Any type (all types are subtypes of Any) */
    if (strcmp(type_b, "Any") == 0) return true;

    if (!ctx->type_registry) return false;

    /* Get type definition for type_a */
    TypeDef* type_def = omni_get_type(ctx, type_a);
    if (!type_def) return false;

    /* Check parent chain */
    if (type_def->parent) {
        /* Direct parent match */
        if (strcmp(type_def->parent, type_b) == 0) return true;

        /* Recursive check up the hierarchy */
        return omni_type_is_subtype(ctx, type_def->parent, type_b);
    }

    /* No explicit parent means it's a direct subtype of Any */
    /* So if type_b is "Any", this should return true (already handled above) */
    /* If we reach here and type has no parent, it's only a subtype of Any */
    return false;
}

/*
 * Phase 22: Bottom Type Support
 *
 * {bottom} represents a computation that never returns (diverges).
 * Properties:
 *   - bottom is a subtype of EVERY type (can be assigned anywhere)
 *   - No type is a subtype of bottom (nothing can be assigned to bottom)
 *   - Functions returning bottom never return (abort, loop forever, throw)
 *   - Code after a call to a bottom-returning function is unreachable
 */

/* Check if a type name represents the bottom type */
bool omni_is_bottom_type(const char* type_name) {
    if (!type_name) return false;
    return strcmp(type_name, "bottom") == 0 ||
           strcmp(type_name, "Bottom") == 0 ||
           strcmp(type_name, "Nothing") == 0 ||
           strcmp(type_name, "!") == 0;  /* Rust-style "never" type */
}

/* Check if a function returns bottom (never returns) */
bool omni_function_returns_bottom(AnalysisContext* ctx, const char* func_name) {
    FunctionSummary* summary = omni_get_function_summary(ctx, func_name);
    if (!summary || !summary->return_type) return false;
    return omni_is_bottom_type(summary->return_type);
}

/*
 * omni_is_subtype_with_bottom - Enhanced subtype check with bottom support
 *
 * Bottom is a subtype of everything, so:
 *   bottom <: T for any T
 *   T </: bottom for any T != bottom
 */
bool omni_is_subtype_with_bottom(AnalysisContext* ctx, const char* type_a, const char* type_b) {
    /* Bottom is subtype of everything */
    if (omni_is_bottom_type(type_a)) return true;

    /* Nothing is subtype of bottom (except bottom itself) */
    if (omni_is_bottom_type(type_b)) return omni_is_bottom_type(type_a);

    /* Otherwise use normal subtype check */
    return omni_type_is_subtype(ctx, type_a, type_b);
}

/*
 * Phase 22: Variance-Aware Parametric Subtype Checking
 *
 * Check if a parametric type like List{Int} is a subtype of List{Number}.
 * Uses the variance annotations on the type definition.
 *
 * Rules:
 *   - Covariant (+): T <: S implies C{T} <: C{S}
 *   - Contravariant (-): T <: S implies C{S} <: C{T}
 *   - Invariant (default): C{T} <: C{S} only if T = S
 */
bool omni_is_parametric_subtype(AnalysisContext* ctx, TypeDef* base_type,
                                const char** params_a, size_t count_a,
                                const char** params_b, size_t count_b) {
    if (!base_type || count_a != count_b) return false;

    /* Check each type parameter according to its variance */
    for (size_t i = 0; i < count_a; i++) {
        const char* param_a = params_a[i];
        const char* param_b = params_b[i];

        if (strcmp(param_a, param_b) == 0) continue;  /* Same type, always OK */

        /* Get variance for this type parameter position */
        VarianceKind variance = VARIANCE_INVARIANT;
        if (i < base_type->field_count && base_type->fields) {
            variance = base_type->fields[i].variance;
        }

        switch (variance) {
            case VARIANCE_COVARIANT:
                /* T <: S implies C{T} <: C{S} */
                if (!omni_type_is_subtype(ctx, param_a, param_b)) {
                    return false;
                }
                break;

            case VARIANCE_CONTRAVARIANT:
                /* T <: S implies C{S} <: C{T} (reversed) */
                if (!omni_type_is_subtype(ctx, param_b, param_a)) {
                    return false;
                }
                break;

            case VARIANCE_INVARIANT:
            default:
                /* Types must be identical for invariant positions */
                return false;
        }
    }

    return true;
}

/*
 * Get variance for a type parameter at a given index
 */
VarianceKind omni_get_type_param_variance_by_index(TypeDef* type, size_t index) {
    if (!type || index >= type->field_count || !type->fields) {
        return VARIANCE_INVARIANT;
    }
    return type->fields[index].variance;
}

/* ============== Phase 22: ^:where Constraint Functions ============== */

/*
 * omni_parse_where_constraints - Parse ^:where [T {Bound}] constraints
 *
 * Input: OmniValue representing [T {Bound}] or [[T1 {B1}] [T2 {B2}] ...]
 * Output: Linked list of TypeConstraint
 *
 * Syntax:
 *   ^:where [T {Number}]           - Single constraint
 *   ^:where [[T {Number}] [U]]     - Multiple constraints
 */
TypeConstraint* omni_parse_where_constraints(OmniValue* where_value) {
    if (!where_value) return NULL;

    TypeConstraint* head = NULL;
    TypeConstraint** tail = &head;

    /* Handle array of constraints: [[T {Bound}] [U {Bound}]] */
    if (omni_is_array(where_value)) {
        size_t len = omni_array_len(where_value);

        /* Check if first element is also an array (multiple constraints) */
        if (len > 0) {
            OmniValue* first = omni_array_get(where_value, 0);
            if (omni_is_array(first)) {
                /* Multiple constraints: [[T {Bound}] [U {Bound}]] */
                for (size_t i = 0; i < len; i++) {
                    OmniValue* constraint_arr = omni_array_get(where_value, i);
                    if (!omni_is_array(constraint_arr)) continue;

                    size_t clen = omni_array_len(constraint_arr);
                    if (clen < 1) continue;

                    TypeConstraint* tc = calloc(1, sizeof(TypeConstraint));
                    if (!tc) continue;

                    /* First element: type variable name */
                    OmniValue* name_val = omni_array_get(constraint_arr, 0);
                    if (omni_is_sym(name_val) && name_val->str_val) {
                        tc->name = strdup(name_val->str_val);
                    }

                    /* Second element (optional): type bound */
                    if (clen >= 2) {
                        OmniValue* bound_val = omni_array_get(constraint_arr, 1);
                        if (omni_is_type_lit(bound_val) && bound_val->str_val) {
                            tc->bound = strdup(bound_val->str_val);
                        }
                    }

                    tc->variance = 0; /* Invariant by default */
                    tc->next = NULL;
                    *tail = tc;
                    tail = &tc->next;
                }
            } else {
                /* Single constraint: [T {Bound}] */
                TypeConstraint* tc = calloc(1, sizeof(TypeConstraint));
                if (tc) {
                    /* First element: type variable name */
                    OmniValue* name_val = omni_array_get(where_value, 0);
                    if (omni_is_sym(name_val) && name_val->str_val) {
                        tc->name = strdup(name_val->str_val);
                    }

                    /* Second element (optional): type bound */
                    if (len >= 2) {
                        OmniValue* bound_val = omni_array_get(where_value, 1);
                        if (omni_is_type_lit(bound_val) && bound_val->str_val) {
                            tc->bound = strdup(bound_val->str_val);
                        }
                    }

                    tc->variance = 0;
                    tc->next = NULL;
                    *tail = tc;
                }
            }
        }
    }

    return head;
}

/*
 * omni_check_constraints - Check if argument types satisfy diagonal dispatch constraints
 *
 * For diagonal dispatch, all parameters with the same type variable must have
 * the same concrete type, and that type must be a subtype of the bound.
 *
 * Example: ^:where [T {Number}] with params [x {T}] [y {T}]
 *   - If x is Int32 and y is Int32: OK (same type, subtype of Number)
 *   - If x is Int32 and y is Float64: FAIL (different concrete types)
 *   - If x is String: FAIL (not subtype of Number)
 */
bool omni_check_constraints(AnalysisContext* ctx, TypeConstraint* constraints,
                            const char** arg_types, size_t arg_count) {
    if (!constraints) return true; /* No constraints = always valid */
    (void)arg_count; /* May be used for future diagonal dispatch */

    /* For each constraint, verify bound is satisfied */
    for (TypeConstraint* tc = constraints; tc; tc = tc->next) {
        if (!tc->name) continue;

        /*
         * In diagonal dispatch, we'd need to:
         * 1. Find all params with type {T}
         * 2. Check they all have the same concrete type
         * 3. Check that concrete type is subtype of bound
         *
         * For now, we just verify that if a bound exists,
         * any explicitly provided type is a subtype.
         */
        if (tc->bound && arg_types && arg_types[0]) {
            /* Basic check: first arg type must satisfy bound */
            if (!omni_type_is_subtype(ctx, arg_types[0], tc->bound)) {
                return false;
            }
        }
    }

    return true;
}

/*
 * omni_free_constraints - Free a TypeConstraint linked list
 */
void omni_free_constraints(TypeConstraint* constraints) {
    while (constraints) {
        TypeConstraint* next = constraints->next;
        free(constraints->name);
        free(constraints->bound);
        free(constraints);
        constraints = next;
    }
}

/* Compute specificity score for method dispatch (higher = more specific) */
int omni_compute_specificity(AnalysisContext* ctx, TypeDef* type) {
    if (!type) return 0;

    int score = 0;

    /* Concrete types are more specific than abstract */
    if (!type->is_abstract) score += 100;

    /* Primitives are more specific than composites */
    if (type->is_primitive) score += 50;

    /* Count depth in type hierarchy (deeper = more specific) */
    int depth = 0;
    TypeDef* current = type;
    while (current->parent) {
        depth++;
        TypeDef* parent = omni_type_get_parent(ctx, current);
        if (!parent) break;
        current = parent;
    }
    score += depth * 10;

    return score;
}

/* Register a type with full metadata support */
TypeDef* omni_register_type_with_metadata(AnalysisContext* ctx, OmniValue* type_def,
                                          MetadataEntry* metadata) {
    if (!ctx->type_registry) {
        ctx->type_registry = omni_type_registry_new();
    }

    /* Extract type name from first element */
    if (!omni_is_cell(type_def)) return NULL;
    OmniValue* name_obj = omni_car(omni_cdr(type_def));
    if (!omni_is_sym(name_obj)) return NULL;

    const char* type_name = name_obj->str_val;

    /* Check for abstract type marker */
    bool is_abstract = omni_has_metadata(metadata, "abstract");

    /* Check for primitive type marker */
    bool is_primitive = omni_has_metadata(metadata, "primitive");

    /* Create or get type definition */
    TypeDef* type = malloc(sizeof(TypeDef));
    memset(type, 0, sizeof(TypeDef));
    type->name = strdup(type_name);
    type->is_abstract = is_abstract;
    type->is_primitive = is_primitive;
    type->field_capacity = 8;
    type->fields = malloc(type->field_capacity * sizeof(TypeField));

    /* Extract parent from metadata */
    OmniValue* parent_value = omni_get_metadata(metadata, "parent");
    if (parent_value && omni_is_sym(parent_value)) {
        type->parent = strdup(parent_value->str_val);
    }

    /* Extract bit width for primitives */
    OmniValue* width_value = omni_car(omni_cdr(type_def));
    if (is_primitive && width_value && omni_is_int(width_value)) {
        type->bit_width = width_value->int_val;
    }

    /* Add to registry */
    type->next = ctx->type_registry->types;
    ctx->type_registry->types++;
    ctx->type_registry->type_count++;

    return type;
}

/* Register a primitive type with bit width */
TypeDef* omni_register_primitive_type(AnalysisContext* ctx, const char* name,
                                      const char* parent, int bit_width) {
    if (!ctx->type_registry) {
        ctx->type_registry = omni_type_registry_new();
    }

    TypeDef* type = malloc(sizeof(TypeDef));
    memset(type, 0, sizeof(TypeDef));
    type->name = strdup(name);
    type->is_primitive = true;
    type->bit_width = bit_width;
    if (parent) type->parent = strdup(parent);

    type->next = ctx->type_registry->types;
    ctx->type_registry->types = type;
    ctx->type_registry->type_count++;

    return type;
}

/* Register an abstract type */
TypeDef* omni_register_abstract_type(AnalysisContext* ctx, const char* name,
                                     const char* parent) {
    if (!ctx->type_registry) {
        ctx->type_registry = omni_type_registry_new();
    }

    TypeDef* type = malloc(sizeof(TypeDef));
    memset(type, 0, sizeof(TypeDef));
    type->name = strdup(name);
    type->is_abstract = true;
    if (parent) type->parent = strdup(parent);

    type->next = ctx->type_registry->types;
    ctx->type_registry->types = type;
    ctx->type_registry->type_count++;

    return type;
}

/* Register a struct type with fields */
TypeDef* omni_register_struct_type(AnalysisContext* ctx, const char* name,
                                   const char* parent, OmniValue* fields) {
    if (!ctx->type_registry) {
        ctx->type_registry = omni_type_registry_new();
    }

    TypeDef* type = malloc(sizeof(TypeDef));
    memset(type, 0, sizeof(TypeDef));
    type->name = strdup(name);
    type->is_abstract = false;
    type->is_primitive = false;
    if (parent) type->parent = strdup(parent);

    type->field_capacity = 8;
    type->fields = malloc(type->field_capacity * sizeof(TypeField));

    /* Phase 22: Parse fields from new [name {Type}] array syntax */
    if (fields && omni_is_cell(fields)) {
        /* Check if this is the new array syntax or old alternating pairs */
        OmniValue* first = omni_car(fields);
        if (omni_is_array(first)) {
            /* New syntax: each field is [name {Type}] array */
            OmniValue* f = fields;
            while (!omni_is_nil(f) && omni_is_cell(f)) {
                OmniValue* field_array = omni_car(f);

                if (omni_is_array(field_array) && field_array->array.len >= 1) {
                    const char* field_name = NULL;
                    const char* field_type = NULL;

                    /* Extract field name (first element) */
                    OmniValue* name_elem = field_array->array.data[0];
                    if (omni_is_sym(name_elem)) {
                        field_name = name_elem->str_val;
                    }

                    /* Extract field type (second element, if present) */
                    if (field_array->array.len >= 2) {
                        OmniValue* type_elem = field_array->array.data[1];
                        if (omni_is_type_lit(type_elem)) {
                            field_type = type_elem->type_lit.type_name;
                        } else if (omni_is_sym(type_elem)) {
                            field_type = type_elem->str_val;
                        }
                    }

                    if (field_name) {
                        TypeField tf;
                        tf.name = strdup(field_name);
                        tf.type_name = field_type ? strdup(field_type) : NULL;
                        tf.strength = FIELD_STRONG;
                        tf.is_mutable = false;
                        tf.index = type->field_count;
                        tf.variance = VARIANCE_INVARIANT;

                        if (type->field_count >= type->field_capacity) {
                            type->field_capacity *= 2;
                            type->fields = realloc(type->fields,
                                                  type->field_capacity * sizeof(TypeField));
                        }
                        type->fields[type->field_count++] = tf;
                    }
                }

                f = omni_cdr(f);
            }
        } else {
            /* Old syntax: alternating name/type pairs */
            OmniValue* f = fields;
            while (!omni_is_nil(f) && omni_is_cell(f)) {
                OmniValue* field_name = omni_car(f);
                OmniValue* field_type = omni_car(omni_cdr(f));

                if (omni_is_sym(field_name)) {
                    TypeField tf;
                    tf.name = strdup(field_name->str_val);
                    tf.type_name = (field_type && omni_is_sym(field_type)) ?
                                   strdup(field_type->str_val) : NULL;
                    tf.strength = FIELD_STRONG;
                    tf.is_mutable = false;
                    tf.index = type->field_count;
                    tf.variance = VARIANCE_INVARIANT;

                    if (type->field_count >= type->field_capacity) {
                        type->field_capacity *= 2;
                        type->fields = realloc(type->fields,
                                              type->field_capacity * sizeof(TypeField));
                    }
                    type->fields[type->field_count++] = tf;
                }

                f = omni_cdr(omni_cdr(f));  /* Skip name and type */
            }
        }
    }

    type->next = ctx->type_registry->types;
    ctx->type_registry->types = type;
    ctx->type_registry->type_count++;

    return type;
}

/* ============== Phase 22: Union and Function Type Registration ============== */

/*
 * Register a union type
 * A union type represents a value that can be any of several types
 * Example: (define {IntOrString} (union [{Int32} {String}]))
 */
TypeDef* omni_register_union_type(AnalysisContext* ctx, const char* name,
                                  OmniValue* members_array) {
    if (!ctx->type_registry) {
        ctx->type_registry = omni_type_registry_new();
    }

    TypeDef* type = malloc(sizeof(TypeDef));
    memset(type, 0, sizeof(TypeDef));
    type->name = strdup(name);
    type->is_abstract = false;
    type->is_primitive = false;
    type->parent = strdup("Any");  /* Unions are subtypes of Any */

    /* Store union members as fields (for now, reuse the field structure) */
    type->field_capacity = 8;
    type->fields = malloc(type->field_capacity * sizeof(TypeField));

    if (members_array && omni_is_array(members_array)) {
        for (size_t i = 0; i < members_array->array.len; i++) {
            OmniValue* member = members_array->array.data[i];
            const char* member_type = NULL;

            if (omni_is_type_lit(member)) {
                member_type = member->type_lit.type_name;
            } else if (omni_is_sym(member)) {
                member_type = member->str_val;
            }

            if (member_type) {
                TypeField tf;
                tf.name = strdup(member_type);  /* Store type name as field name for now */
                tf.type_name = strdup(member_type);
                tf.strength = FIELD_STRONG;
                tf.is_mutable = false;
                tf.index = type->field_count;
                tf.variance = VARIANCE_INVARIANT;

                if (type->field_count >= type->field_capacity) {
                    type->field_capacity *= 2;
                    type->fields = realloc(type->fields,
                                          type->field_capacity * sizeof(TypeField));
                }
                type->fields[type->field_count++] = tf;
            }
        }
    }

    type->next = ctx->type_registry->types;
    ctx->type_registry->types = type;
    ctx->type_registry->type_count++;

    return type;
}

/*
 * Register a function type
 * A function type represents the signature of a function
 * Example: (define {IntToInt} (fn [[{Int32}] {Int32}]))
 */
TypeDef* omni_register_function_type(AnalysisContext* ctx, const char* name,
                                     OmniValue* sig_array) {
    if (!ctx->type_registry) {
        ctx->type_registry = omni_type_registry_new();
    }

    TypeDef* type = malloc(sizeof(TypeDef));
    memset(type, 0, sizeof(TypeDef));
    type->name = strdup(name);
    type->is_abstract = false;
    type->is_primitive = false;
    type->parent = strdup("Function");  /* Function types are subtypes of Function */

    /* Parse function signature: [[param1 {Type1} ...] {ReturnType}] */
    type->field_capacity = 8;
    type->fields = malloc(type->field_capacity * sizeof(TypeField));

    if (sig_array && omni_is_array(sig_array) && sig_array->array.len >= 2) {
        /* First element: parameter types array */
        OmniValue* params = sig_array->array.data[0];
        /* Last element: return type */
        OmniValue* return_type = sig_array->array.data[sig_array->array.len - 1];

        /* Store return type as a special field */
        const char* return_type_str = NULL;
        if (omni_is_type_lit(return_type)) {
            return_type_str = return_type->type_lit.type_name;
        } else if (omni_is_sym(return_type)) {
            return_type_str = return_type->str_val;
        }

        if (return_type_str) {
            TypeField tf;
            tf.name = strdup("return");
            tf.type_name = strdup(return_type_str);
            tf.strength = FIELD_STRONG;
            tf.is_mutable = false;
            tf.index = 0;
            tf.variance = VARIANCE_COVARIANT;  /* Return type is covariant */

            type->fields[type->field_count++] = tf;
        }

        /* Store parameter types */
        if (omni_is_array(params)) {
            for (size_t i = 0; i < params->array.len; i++) {
                OmniValue* param = params->array.data[i];
                const char* param_type = NULL;

                if (omni_is_type_lit(param)) {
                    param_type = param->type_lit.type_name;
                } else if (omni_is_sym(param)) {
                    param_type = param->str_val;
                }

                if (param_type) {
                    TypeField tf;
                    char param_name[32];
                    snprintf(param_name, sizeof(param_name), "param%zu", i);
                    tf.name = strdup(param_name);
                    tf.type_name = strdup(param_type);
                    tf.strength = FIELD_STRONG;
                    tf.is_mutable = false;
                    tf.index = type->field_count;
                    tf.variance = VARIANCE_CONTRAVARIANT;  /* Param types are contravariant */

                    if (type->field_count >= type->field_capacity) {
                        type->field_capacity *= 2;
                        type->fields = realloc(type->fields,
                                              type->field_capacity * sizeof(TypeField));
                    }
                    type->fields[type->field_count++] = tf;
                }
            }
        }
    }

    type->next = ctx->type_registry->types;
    ctx->type_registry->types = type;
    ctx->type_registry->type_count++;

    return type;
}

/*
 * Parametric Type Functions
 * Handle type parameters and variance.
 */

/* Add type parameter to a type definition */
void omni_type_add_param(TypeDef* type, const char* param_name, VarianceKind variance) {
    if (!type || !param_name) return;
    (void)variance;  /* TODO: Store variance info per parameter */

    /* Allocate or expand type params array using realloc */
    size_t new_count = type->type_param_count + 1;
    char** new_params = realloc(type->type_params, new_count * sizeof(char*));
    if (!new_params) return;  /* Allocation failed */

    type->type_params = new_params;
    type->type_params[type->type_param_count] = strdup(param_name);
    type->type_param_count = new_count;
}

/* Get variance of a type parameter */
VarianceKind omni_type_get_param_variance(TypeDef* type, const char* param_name) {
    if (!type || !param_name) return VARIANCE_INVARIANT;

    for (size_t i = 0; i < type->type_param_count; i++) {
        if (strcmp(type->type_params[i], param_name) == 0) {
            /* Check if this param has variance metadata */
            if (type->metadata) {
                MetadataEntry* meta = type->metadata;
                while (meta) {
                    if ((strcmp(meta->key, param_name) == 0) &&
                        (omni_is_sym(meta->value) &&
                         strcmp(meta->value->str_val, "+") == 0)) {
                        return VARIANCE_COVARIANT;
                    }
                    if ((strcmp(meta->key, param_name) == 0) &&
                        (omni_is_sym(meta->value) &&
                         strcmp(meta->value->str_val, "-") == 0)) {
                        return VARIANCE_CONTRAVARIANT;
                    }
                    meta = meta->next;
                }
            }
            return VARIANCE_INVARIANT;
        }
    }

    return VARIANCE_INVARIANT;
}

/* Create a parametric type instance (e.g., (List Int)) */
TypeDef* omni_make_parametric_instance(AnalysisContext* ctx, const char* base_type,
                                       char** type_args, size_t arg_count) {
    if (!ctx->type_registry) return NULL;

    /* Find base type */
    TypeDef* base = omni_get_type(ctx, base_type);
    if (!base) return NULL;

    // REVIEWED:OPTIMIZED - dynamic allocation instead of fixed 256-byte buffer
    /* Create instance type with mangled name */
    size_t name_len = strlen(base_type) + 1;  /* base_type + "_" */
    for (size_t i = 0; i < arg_count; i++) {
        name_len += strlen(type_args[i]) + 1;  /* + "_" separator */
    }
    char* instance_name = malloc(name_len + 1);
    if (!instance_name) return NULL;

    char* pos = instance_name;
    pos += sprintf(pos, "%s_", base_type);
    for (size_t i = 0; i < arg_count; i++) {
        if (i > 0) pos += sprintf(pos, "_");
        pos += sprintf(pos, "%s", type_args[i]);
    }

    /* Check if instance already exists */
    TypeDef* existing = omni_get_type(ctx, instance_name);
    if (existing) {
        free(instance_name);
        return existing;
    }

    /* Create new instance */
    TypeDef* instance = malloc(sizeof(TypeDef));
    memset(instance, 0, sizeof(TypeDef));
    instance->name = strdup(instance_name);
    free(instance_name);  /* strdup made a copy, free the original */
    instance->parent = base->parent ? strdup(base->parent) : NULL;
    instance->is_abstract = base->is_abstract;
    instance->is_primitive = base->is_primitive;
    instance->bit_width = base->bit_width;

    /* Copy fields and substitute type parameters */
    instance->field_capacity = base->field_capacity;
    instance->fields = malloc(instance->field_capacity * sizeof(TypeField));
// REVIEWED:NAIVE
    for (size_t i = 0; i < base->field_count; i++) {
        instance->fields[i] = base->fields[i];
        if (instance->fields[i].name) {
            instance->fields[i].name = strdup(base->fields[i].name);
        }
        if (instance->fields[i].type_name) {
            /* Check if type is a parameter */
            for (size_t j = 0; j < arg_count; j++) {
                if (base->type_params &&
                    j < base->type_param_count &&
                    strcmp(base->type_params[j], base->fields[i].type_name) == 0) {
                    instance->fields[i].type_name = strdup(type_args[j]);
                    break;
                }
            }
            if (instance->fields[i].type_name == base->fields[i].type_name) {
                instance->fields[i].type_name = strdup(base->fields[i].type_name);
            }
        }
    }
    instance->field_count = base->field_count;

    /* Add to registry */
    instance->next = ctx->type_registry->types;
    ctx->type_registry->types = instance;
    ctx->type_registry->type_count++;

    return instance;
}

/* ============== Type-Based Dispatch Support (Phase 19) ============== */

/*
 * omni_lookup_function_signature: Look up a function's signature by name
 *
 * Args:
 *   ctx - Analysis context
 *   func_name - Name of the function to look up
 *
 * Returns: FunctionSummary* if found, NULL otherwise
 */
// REVIEWED:NAIVE
FunctionSummary* omni_lookup_function_signature(AnalysisContext* ctx, const char* func_name) {
    if (!ctx || !func_name) return NULL;

    FunctionSummary* f = ctx->function_summaries;
    while (f) {
        if (strcmp(f->name, func_name) == 0) {
            return f;
        }
        f = f->next;
    }
    return NULL;
}

/*
 * extract_type_annotation: Extract type annotation from an AST node
 *
 * Args:
 *   param_node - AST node (could be a Slot [x {Type}] or bare symbol)
 *
 * Returns: Type annotation string (e.g., "Int", "String") or NULL if not annotated
 *
 * Examples:
 *   [x {Int}] => "Int"
 *   [x]      => NULL
 *   x        => NULL
 */
char* omni_extract_type_annotation(OmniValue* param_node) {
    if (!param_node) return NULL;

    /* Check if it's a Slot (array) with type annotation */
    if (omni_is_array(param_node) && param_node->array.len > 0) {
        /* Slot format: [name] or [name {Type}] */
        /* Check if second element is a type annotation {Type} */
        if (param_node->array.len >= 2 &&
            omni_is_type_lit(param_node->array.data[1])) {
            /* Return type name from type literal */
            return param_node->array.data[1]->type_lit.type_name;
        }
    }

    return NULL;
}

// TESTED - test_argument_type_compatibility.c
/*
 * omni_check_argument_type_compatibility: Check if argument type is compatible with parameter type
 *
 * Args:
 *   ctx - Analysis context
 *   param_type - Expected parameter type (e.g., "Int")
 *   arg_node - Argument AST node
 *
 * Returns: true if compatible, false otherwise
 */
bool omni_check_argument_type_compatibility(AnalysisContext* ctx, const char* param_type, OmniValue* arg_node) {
    if (!param_type) return true;  /* No type constraint = any type OK */
    if (!arg_node) return false;

    /* If parameter expects Any, always compatible */
    if (strcmp(param_type, "Any") == 0) {
        return true;
    }

    /* For symbols, check if they have type annotations and do subtype checking */
    if (omni_is_sym(arg_node)) {
        /* Check for boolean literals first */
        const char* name = arg_node->str_val;
        if (strcmp(param_type, "Bool") == 0) {
            if (strcmp(name, "true") == 0 || strcmp(name, "false") == 0) {
                return true;
            }
        }

        /* Look up variable's type annotation from analysis context */
        /* TODO: This would require tracking variable types during analysis */
        /* For now, we can't do subtype checking on untyped symbols */
        /* Be conservative and allow the call */
        return true;
    }

    /* For literals, do exact type matching */
    /* If parameter expects Int, check if argument is an integer literal */
    if (strcmp(param_type, "Int") == 0) {
        return omni_is_int(arg_node);
    }

    /* If parameter expects String, check if argument is a string literal */
    if (strcmp(param_type, "String") == 0) {
        return omni_is_string(arg_node);
    }

    /* If parameter expects Bool, check if argument is a boolean */
    if (strcmp(param_type, "Bool") == 0) {
        /* Allow integers 0 and 1 as booleans (truthy/falsy values) */
        if (omni_is_int(arg_node)) {
            int val = arg_node->int_val;
            return (val == 0 || val == 1);
        }
        return false;
    }

    /* Subtype checking: Check if parameter type is a supertype of a known argument type */
    /* For example: param_type="Number", arg is Int -> check if Int <: Number */
    /* This would require inferring arg_node's type, which is not fully implemented yet */

    /* TODO: Add more sophisticated type inference and subtype checking */
    /* For now, be conservative and allow the check */
    return true;
}
