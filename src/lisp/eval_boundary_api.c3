module lisp;

import main;

// =============================================================================
// SECTION 2.4: FRAME PUSH/POP (audited boundary facade)
// =============================================================================

<*
@require interp != null : "boundary_copy_to_parent: null interp"
*>
fn Value* boundary_copy_to_parent(Value* v, Interp* interp) @inline {
    return copy_to_parent(v, interp);
}

<*
@require interp != null : "boundary_enter_scope: null interp"
@require target_scope != null : "boundary_enter_scope: null target_scope"
*>
fn main::ScopeRegion* boundary_enter_scope(Interp* interp, main::ScopeRegion* target_scope) @inline {
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = target_scope;
    return saved_scope;
}

<*
@require interp != null : "boundary_leave_scope: null interp"
*>
fn void boundary_leave_scope(Interp* interp, main::ScopeRegion* saved_scope) @inline {
    interp.current_scope = saved_scope;
}

<*
@require interp != null : "boundary_push_child_scope: null interp"
*>
fn main::ScopeRegion* boundary_push_child_scope(Interp* interp, main::ScopeRegion** saved_scope_out = null) @inline {
    main::ScopeRegion* saved_scope = interp.current_scope;
    if (saved_scope_out != null) {
        *saved_scope_out = saved_scope;
    }
    main::ScopeRegion* child_scope = main::scope_create(saved_scope);
    if (child_scope == null) {
        return null;
    }
    interp.current_scope = child_scope;
    return child_scope;
}

<*
@require interp != null : "boundary_pop_child_scope: null interp"
*>
fn void boundary_pop_child_scope(
    Interp* interp,
    main::ScopeRegion* saved_scope,
    main::ScopeRegion* child_scope
) @inline {
    interp.current_scope = saved_scope;
    if (child_scope != null) {
        main::scope_release(child_scope);
    }
}

<*
@require interp != null : "boundary_copy_to_parent_site: null interp"
*>
fn Value* boundary_copy_to_parent_site(Value* v, Interp* interp, uint site) @inline {
    return copy_to_parent_site(v, interp, site);
}

<*
@require interp != null : "boundary_copy_to_parent_site_ctx: null interp"
*>
fn Value* boundary_copy_to_parent_site_ctx(Value* v, Interp* interp, uint site, PromotionContext* ctx) @inline {
    return copy_to_parent_site_ctx(v, interp, site, ctx);
}

<*
@require interp != null : "boundary_copy_to_scope_site: null interp"
*>
fn Value* boundary_copy_to_scope_site(
    Value* v,
    Interp* interp,
    main::ScopeRegion* target_scope,
    main::ScopeRegion* releasing_scope,
    uint site
) @inline {
    if (v == null || target_scope == null) return v;
    main::ScopeRegion* saved_scope = interp.current_scope;
    main::ScopeRegion* saved_releasing = interp.releasing_scope;
    interp.current_scope = target_scope;
    interp.releasing_scope = releasing_scope;
    Value* copied = boundary_copy_to_parent_site(v, interp, site);
    interp.releasing_scope = saved_releasing;
    interp.current_scope = saved_scope;
    return copied;
}

<*
@require interp != null : "boundary_alloc_value_in_scope: null interp"
@require target_scope != null : "boundary_alloc_value_in_scope: null target_scope"
*>
fn Value* boundary_alloc_value_in_scope(Interp* interp, main::ScopeRegion* target_scope, bool register_dtor = false) @inline {
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = target_scope;
    Value* v = interp.alloc_value();
    if (register_dtor) {
        main::scope_register_dtor(target_scope, (void*)v, &scope_dtor_value);
    }
    interp.current_scope = saved_scope;
    return v;
}

<*
@require interp != null : "boundary_alloc_value_in_root: null interp"
*>
fn Value* boundary_alloc_value_in_root(Interp* interp, bool register_dtor = false) @inline {
    return boundary_alloc_value_in_scope(interp, interp.root_scope, register_dtor);
}

<*
@require interp != null : "boundary_make_env_in_scope: null interp"
@require target_scope != null : "boundary_make_env_in_scope: null target_scope"
*>
fn Env* boundary_make_env_in_scope(Interp* interp, main::ScopeRegion* target_scope, Env* parent) @inline {
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = target_scope;
    Env* env = make_env(interp, parent);
    interp.current_scope = saved_scope;
    return env;
}

<*
@require interp != null : "boundary_make_env_in_root: null interp"
*>
fn Env* boundary_make_env_in_root(Interp* interp, Env* parent) @inline {
    return boundary_make_env_in_scope(interp, interp.root_scope, parent);
}

<*
@require interp != null : "boundary_env_extend_in_scope: null interp"
@require target_scope != null : "boundary_env_extend_in_scope: null target_scope"
@require env != null : "boundary_env_extend_in_scope: null env"
*>
fn Env* boundary_env_extend_in_scope(
    Interp* interp,
    main::ScopeRegion* target_scope,
    Env* env,
    SymbolId name,
    Value* value
) @inline {
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = target_scope;
    Env* out = env.extend(interp, name, value);
    interp.current_scope = saved_scope;
    return out;
}

<*
@require interp != null : "boundary_env_extend_in_root: null interp"
@require env != null : "boundary_env_extend_in_root: null env"
*>
fn Env* boundary_env_extend_in_root(Interp* interp, Env* env, SymbolId name, Value* value) @inline {
    return boundary_env_extend_in_scope(interp, interp.root_scope, env, name, value);
}

<*
@require interp != null : "boundary_promote_to_escape: null interp"
*>
fn Value* boundary_promote_to_escape(Value* v, Interp* interp) @inline {
    return promote_to_escape(v, interp);
}

<*
@require interp != null : "boundary_promote_to_root_site: null interp"
*>
fn Value* boundary_promote_to_root_site(Value* v, Interp* interp, uint site) @inline {
    return promote_to_root_site(v, interp, site);
}

<*
@require interp != null : "boundary_promote_to_root: null interp"
*>
fn Value* boundary_promote_to_root(Value* v, Interp* interp) @inline {
    return promote_to_root(v, interp);
}

<*
@require interp != null : "boundary_copy_value_if_owned_by_scope: null interp"
*>
fn Value* boundary_copy_value_if_owned_by_scope(Value* v, Interp* interp, main::ScopeRegion* source_scope, uint site) @inline {
    return copy_value_if_owned_by_scope(v, interp, source_scope, site);
}

<*
@require interp != null : "boundary_copy_from_releasing_scope: null interp"
*>
fn Value* boundary_copy_from_releasing_scope(Value* v, Interp* interp, main::ScopeRegion* releasing_scope, uint site) @inline {
    if (releasing_scope == null) return v;
    main::ScopeRegion* saved_releasing = interp.releasing_scope;
    interp.releasing_scope = releasing_scope;
    Value* copied = boundary_copy_value_if_owned_by_scope(v, interp, releasing_scope, site);
    interp.releasing_scope = saved_releasing;
    return copied;
}

<*
@require interp != null : "boundary_copy_env_to_scope: null interp"
*>
fn Env* boundary_copy_env_to_scope(Env* env, Interp* interp, usz depth = 0, PromotionContext* ctx = null) @inline {
    return copy_env_to_scope(env, interp, depth, ctx);
}

<*
@require interp != null : "boundary_copy_env_to_target_scope: null interp"
@require target_scope != null : "boundary_copy_env_to_target_scope: null target_scope"
*>
fn Env* boundary_copy_env_to_target_scope(
    Env* env,
    Interp* interp,
    main::ScopeRegion* target_scope,
    usz depth = 0,
    PromotionContext* ctx = null
) @inline {
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = target_scope;
    Env* copied = boundary_copy_env_to_scope(env, interp, depth, ctx);
    interp.current_scope = saved_scope;
    return copied;
}

fn bool boundary_try_scope_splice_escapes(main::ScopeRegion* parent, main::ScopeRegion* child) @inline {
    if (parent == null || child == null) return false;
    assert(parent != child, "boundary_try_scope_splice_escapes: parent and child must differ");
    main::scope_splice_escapes(parent, child);
    return true;
}
