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

fn bool boundary_try_scope_splice_escapes(main::ScopeRegion* parent, main::ScopeRegion* child) @inline {
    if (parent == null || child == null) return false;
    assert(parent != child, "boundary_try_scope_splice_escapes: parent and child must differ");
    main::scope_splice_escapes(parent, child);
    return true;
}
