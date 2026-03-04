module lisp;

import std::io;
import main;

// SECTION 2.35: TYPE SYSTEM EVALUATORS
// =============================================================================

/**
 * Create an Instance value with a dedicated owner scope.
 *
 * owner_scope stores field payload lifetimes.
 * wrapper_scope stores the Value wrapper and destructor.
 */
fn Value* make_instance_in_scope(Interp* interp, main::ScopeRegion* wrapper_scope,
    TypeId type_id, Value** fields, usz field_count) {
    main::ScopeRegion* owner_scope = main::scope_create(interp.root_scope);
    if (owner_scope == null) return null;

    Instance* inst = (Instance*)owner_scope.alloc(Instance.sizeof);
    inst.owner_scope = owner_scope;
    inst.type_id = type_id;
    inst.field_count = field_count;
    inst.type_arg_count = 0;

    for (usz i = 0; i < MAX_TYPE_FIELDS; i++) {
        inst.fields[i] = {};
        inst.fields[i].tag = NIL;
    }

    // Copy field payloads into owner_scope so Instance never points into caller scope.
    main::ScopeRegion* saved_scope = interp.current_scope;
    main::ScopeRegion* saved_releasing = interp.releasing_scope;
    interp.current_scope = owner_scope;
    interp.releasing_scope = saved_scope;
    for (usz i = 0; i < field_count && i < MAX_TYPE_FIELDS; i++) {
        if (fields != null && fields[i] != null) {
            Value* copied = boundary_copy_to_parent_site(fields[i], interp, COPY_SITE_GENERIC);
            if (copied != null) inst.fields[i] = *copied;
        }
    }
    interp.current_scope = wrapper_scope;
    interp.releasing_scope = saved_releasing;

    Value* v = interp.alloc_value();
    main::scope_register_dtor(wrapper_scope, (void*)v, &scope_dtor_value);
    v.tag = INSTANCE;
    v.instance_val = inst;
    interp.current_scope = saved_scope;
    return v;
}

fn Value* make_instance(Interp* interp, TypeId type_id, Value** fields, usz field_count) {
    return make_instance_in_scope(interp, interp.current_scope, type_id, fields, field_count);
}

// Root-scope wrapper variant for global constants (nullary union constructors, etc.)
fn Value* make_instance_root(Interp* interp, TypeId type_id, Value** fields, usz field_count) {
    return make_instance_in_scope(interp, interp.root_scope, type_id, fields, field_count);
}

fn void eval_deftype_init_type_info(TypeInfo* info, ExprDefType* dt) {
    info.name = dt.name;
    info.kind = TK_CONCRETE;
    info.field_count = dt.field_count;
    info.type_param_count = dt.type_param_count;
    info.variant_count = 0;
    info.alias_target = INVALID_TYPE_ID;
    info.parent = INVALID_TYPE_ID;
    info.constraint_count = 0;
}

fn void eval_deftype_resolve_parent_and_params(TypeInfo* info, ExprDefType* dt, Interp* interp) {
    info.type_param_count = 0;
    if (dt.type_param_count == 0) return;

    TypeId maybe_parent = interp.types.lookup(dt.type_params[0], &interp.symbols);
    if (maybe_parent != INVALID_TYPE_ID) {
        // First symbol is a known type — it's the parent.
        info.parent = maybe_parent;
        // Remaining symbols are actual type params.
        info.type_param_count = dt.type_param_count - 1;
        for (usz i = 0; i < info.type_param_count; i++) {
            info.type_params[i] = dt.type_params[i + 1];
        }
        return;
    }

    // Not a known type — all are type params, no parent.
    info.type_param_count = dt.type_param_count;
    for (usz i = 0; i < info.type_param_count; i++) {
        info.type_params[i] = dt.type_params[i];
    }
}

fn void eval_deftype_apply_dict_annotation(TypeInfo* info, TypeField* field, TypeFieldInfo* out_field, Interp* interp) {
    // Dict annotation like ^{'T Number} — extract constraints.
    out_field.field_type = INVALID_TYPE_ID;
    out_field.annotation_sym = 0;
    for (usz m = 0; m < field.type_ann.meta_count && info.constraint_count < MAX_TYPE_PARAMS; m++) {
        if (field.type_ann.meta[m].is_int) continue;
        info.constraints[info.constraint_count].param_sym = field.type_ann.meta[m].key;
        info.constraints[info.constraint_count].bound_type =
            interp.types.lookup(field.type_ann.meta[m].sym_value, &interp.symbols);
        // Also store the param_sym as annotation_sym for type-arg inference.
        out_field.annotation_sym = field.type_ann.meta[m].key;
        info.constraint_count++;
    }
}

fn void eval_deftype_copy_fields(TypeInfo* info, ExprDefType* dt, Interp* interp) {
    for (usz i = 0; i < dt.field_count; i++) {
        TypeField* src_field = &dt.fields[i];
        TypeFieldInfo* dst_field = &info.fields[i];
        dst_field.name = src_field.name;

        if (!src_field.type_ann.has_annotation) {
            dst_field.field_type = INVALID_TYPE_ID;  // Any
            dst_field.annotation_sym = 0;
            continue;
        }

        if (src_field.type_ann.is_dict) {
            eval_deftype_apply_dict_annotation(info, src_field, dst_field, interp);
            continue;
        }

        dst_field.field_type = interp.types.lookup(src_field.type_ann.base_type, &interp.symbols);
        dst_field.annotation_sym = src_field.type_ann.base_type;
    }
}

fn EvalResult eval_deftype(Expr* expr, Env* env, Interp* interp) {
    ExprDefType* dt = expr.deftype;

    // Build TypeInfo
    TypeInfo info;
    eval_deftype_init_type_info(&info, dt);

    // Disambiguate parent vs type-params from collected symbols.
    // If the first symbol is a registered type, it's the parent; rest are type params.
    // Otherwise, all symbols are type params and there is no parent.
    eval_deftype_resolve_parent_and_params(&info, dt, interp);

    // Copy fields and store original annotation symbol for type-param mapping
    eval_deftype_copy_fields(&info, dt, interp);

    // Register in type registry
    TypeId type_id = interp.types.register_type(info, &interp.symbols);

    // Create constructor primitive in root_scope (lives permanently in global env)
    char[] name_str = interp.symbols.get_name(dt.name);
    Value* constructor = make_primitive(interp, name_str, &prim_type_constructor, (int)dt.field_count);
    constructor.prim_val.tag = (int)type_id;

    // Define constructor in global env
    env_define_with_barrier(interp.global_env, dt.name, constructor, interp);

    // Also make type info available as a value
    Value* type_val = interp.alloc_value();
    type_val.tag = TYPE_INFO;
    type_val.type_info_val = &interp.types.types[(usz)type_id];

    return eval_ok(constructor);
}

/**
 * Generic type constructor primitive.
 * Uses interp.constructor_type_id (set before each call) to know which type to construct.
 */
fn Value* constructor_arity_error(TypeInfo* ti, usz got, Interp* interp) {
    char[256] ebuf;
    char[] type_n = interp.symbols.get_name(ti.name);
    char[] msg = io::bprintf(&ebuf, "%s constructor expects %d argument(s), got %d",
        (ZString)type_n, (int)ti.field_count, (int)got)!!;
    return raise_error(interp, msg);
}

fn bool is_type_param_annotation(TypeInfo* ti, SymbolId annotation_sym) {
    if (annotation_sym == 0) return false;
    for (usz p = 0; p < ti.type_param_count; p++) {
        if (ti.type_params[p] == annotation_sym) return true;
    }
    return false;
}

fn Value* validate_constructor_fields(Value*[] args, TypeInfo* ti, Interp* interp) {
    for (usz i = 0; i < args.len && i < ti.field_count; i++) {
        TypeId expected = ti.fields[i].field_type;
        if (expected == INVALID_TYPE_ID) continue;
        if (is_type_param_annotation(ti, ti.fields[i].annotation_sym)) continue;

        TypeId actual = infer_value_type(args[i], interp);
        if (actual == INVALID_TYPE_ID) continue;
        if (actual != expected && !interp.types.is_subtype(actual, expected)) {
            char[128] ebuf;
            char[] expected_name = interp.symbols.get_name(ti.fields[i].annotation_sym);
            char[] field_name = interp.symbols.get_name(ti.fields[i].name);
            char[] actual_name = interp.symbols.get_name(value_type_name(args[i], interp));
            char[] emsg = io::bprintf(&ebuf, "type error: field '%s' expects %s, got %s",
                (ZString)field_name, (ZString)expected_name, (ZString)actual_name)!!;
            return raise_error(interp, emsg);
        }
    }
    return null;
}

fn void infer_instance_type_args(Instance* inst, TypeInfo* ti, Value*[] args, Interp* interp) {
    inst.type_arg_count = ti.type_param_count;
    for (usz p = 0; p < ti.type_param_count && p < MAX_TYPE_PARAMS; p++) {
        SymbolId param_sym = ti.type_params[p];
        inst.type_args[p] = INVALID_TYPE_ID;
        for (usz f = 0; f < ti.field_count; f++) {
            if (ti.fields[f].annotation_sym == param_sym && f < args.len) {
                inst.type_args[p] = infer_value_type(args[f], interp);
                break;
            }
        }
    }
}

fn Value* validate_instance_constraints(Instance* inst, TypeInfo* ti, Interp* interp) {
    for (usz c = 0; c < ti.constraint_count; c++) {
        SymbolId param = ti.constraints[c].param_sym;
        TypeId bound = ti.constraints[c].bound_type;
        if (bound == INVALID_TYPE_ID) continue;
        for (usz p = 0; p < ti.type_param_count; p++) {
            if (ti.type_params[p] == param) {
                TypeId inferred = inst.type_args[p];
                if (inferred != INVALID_TYPE_ID) {
                    if (inferred != bound && !interp.types.is_subtype(inferred, bound)) {
                        char[128] ebuf;
                        char[] param_name = interp.symbols.get_name(param);
                        char[] bound_name = interp.symbols.get_name(interp.types.types[(usz)bound].name);
                        char[] actual_name = interp.symbols.get_name(interp.types.types[(usz)inferred].name);
                        char[] emsg = io::bprintf(&ebuf, "type constraint violated: %s must be subtype of %s, got %s",
                            (ZString)param_name, (ZString)bound_name, (ZString)actual_name)!!;
                        return raise_error(interp, emsg);
                    }
                }
                break;
            }
        }
    }
    return null;
}

fn Value* prim_type_constructor(Value*[] args, Env* env, Interp* interp) {
    TypeId type_id = (TypeId)interp.constructor_type_id;
    TypeInfo* ti = interp.types.get(type_id);
    if (ti == null) return make_error(interp, "internal error: type not found in constructor");

    if (args.len != ti.field_count) {
        return constructor_arity_error(ti, args.len, interp);
    }

    Value* field_error = validate_constructor_fields(args, ti, interp);
    if (field_error != null) return field_error;

    Value*[MAX_TYPE_FIELDS] fields;
    for (usz i = 0; i < args.len && i < MAX_TYPE_FIELDS; i++) {
        fields[i] = args[i];
    }
    Value* result = make_instance(interp, type_id, &fields, args.len);

    if (ti.type_param_count > 0 && result.instance_val != null) {
        Instance* inst = result.instance_val;
        infer_instance_type_args(inst, ti, args, interp);
        Value* constraint_error = validate_instance_constraints(inst, ti, interp);
        if (constraint_error != null) return constraint_error;
    }

    return result;
}

fn EvalResult eval_defabstract(Expr* expr, Env* env, Interp* interp) {
    ExprDefAbstract* da = &expr.defabstract;

    TypeInfo info;
    info.name = da.name;
    info.kind = TK_ABSTRACT;
    info.field_count = 0;
    info.type_param_count = 0;
    info.constraint_count = 0;
    info.variant_count = 0;
    info.alias_target = INVALID_TYPE_ID;

    if (da.has_parent) {
        info.parent = interp.types.lookup(da.parent, &interp.symbols);
        if (info.parent == INVALID_TYPE_ID) {
            char[256] ebuf;
            char[] msg = io::bprintf(&ebuf, "unknown parent type '%s'",
                (ZString)interp.symbols.get_name(da.parent))!!;
            return eval_error(msg);
        }
    } else {
        info.parent = INVALID_TYPE_ID;
    }

    TypeId type_id = interp.types.register_type(info, &interp.symbols);

    // Return type name as symbol
    Value* result = make_symbol(interp, da.name);
    return eval_ok(result);
}

fn EvalResult eval_defunion(Expr* expr, Env* env, Interp* interp) {
    ExprDefUnion* du = expr.defunion;

    // Register the union type itself
    TypeInfo union_info;
    union_info.name = du.name;
    union_info.kind = TK_UNION;
    union_info.parent = INVALID_TYPE_ID;
    union_info.field_count = 0;
    union_info.type_param_count = du.type_param_count;
    union_info.constraint_count = 0;
    union_info.variant_count = du.variant_count;
    union_info.alias_target = INVALID_TYPE_ID;
    for (usz i = 0; i < du.type_param_count; i++) {
        union_info.type_params[i] = du.type_params[i];
    }
    for (usz i = 0; i < du.variant_count; i++) {
        union_info.variants[i] = du.variants[i];
    }

    TypeId union_id = interp.types.register_type(union_info, &interp.symbols);

    // Register each variant as its own type (with union as parent)
    for (usz i = 0; i < du.variant_count; i++) {
        UnionVariant* variant = &du.variants[i];

        TypeInfo var_info;
        var_info.name = variant.name;
        var_info.kind = TK_CONCRETE;
        var_info.parent = union_id;  // variant is subtype of union
        var_info.field_count = variant.field_count;
        var_info.type_param_count = 0;
        var_info.constraint_count = 0;
        var_info.variant_count = 0;
        var_info.alias_target = INVALID_TYPE_ID;
        for (usz j = 0; j < variant.field_count; j++) {
            var_info.fields[j].name = variant.fields[j];
            var_info.fields[j].field_type = INVALID_TYPE_ID;
        }

        TypeId var_id = interp.types.register_type(var_info, &interp.symbols);

        // Create constructor for this variant
        if (variant.field_count == 0) {
            // Nullary variant: define as a constant instance in root_scope
            Value* inst = make_instance_root(interp, var_id, null, 0);
            env_define_with_barrier(interp.global_env, variant.name, inst, interp);
        } else {
            // Variant with fields: define as constructor primitive (make_primitive uses root_scope)
            char[] vname = interp.symbols.get_name(variant.name);
            Value* ctor = make_primitive(interp, vname, &prim_type_constructor, (int)variant.field_count);
            ctor.prim_val.tag = (int)var_id;
            env_define_with_barrier(interp.global_env, variant.name, ctor, interp);
        }
    }

    Value* result = make_symbol(interp, du.name);
    return eval_ok(result);
}

fn EvalResult eval_defalias(Expr* expr, Env* env, Interp* interp) {
    ExprDefAlias* da = &expr.defalias;

    TypeInfo info;
    info.name = da.name;
    info.kind = TK_ALIAS;
    info.parent = INVALID_TYPE_ID;
    info.field_count = 0;
    info.type_param_count = 0;
    info.constraint_count = 0;
    info.variant_count = 0;

    // Resolve target
    if (da.target.has_annotation) {
        info.alias_target = interp.types.lookup(da.target.base_type, &interp.symbols);
    } else {
        info.alias_target = INVALID_TYPE_ID;
    }

    TypeId type_id = interp.types.register_type(info, &interp.symbols);

    Value* result = make_symbol(interp, da.name);
    return eval_ok(result);
}

fn EvalResult eval_defeffect(Expr* expr, Env* env, Interp* interp) {
    ExprDefEffect* de = &expr.defeffect;

    TypeInfo info;
    info.name = de.name;
    info.kind = TK_EFFECT;
    info.parent = INVALID_TYPE_ID;
    info.type_param_count = 0;
    info.constraint_count = 0;
    info.variant_count = 0;
    info.alias_target = INVALID_TYPE_ID;

    if (de.has_arg_type) {
        info.field_count = 1;
        info.fields[0].name = interp.symbols.intern("arg");
        if (de.arg_type.has_annotation) {
            TypeId resolved = interp.types.lookup(de.arg_type.base_type, &interp.symbols);
            info.fields[0].field_type = resolved;
            info.fields[0].annotation_sym = de.arg_type.base_type;
        } else {
            info.fields[0].field_type = INVALID_TYPE_ID;
            info.fields[0].annotation_sym = (SymbolId)0;
        }
    } else {
        info.field_count = 0;
    }

    TypeId type_id = interp.types.register_type(info, &interp.symbols);

    return eval_ok(make_nil(interp));
}

// =============================================================================
