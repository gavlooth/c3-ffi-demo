module lisp;

import main;
fn bool env_chain_contains_scope_gen(Env* env, uint scope_gen, Env* stop_at) {
    Env* cur = env;
    while (cur != null && cur != stop_at) {
        if (!cur.persistent && cur.scope_gen == scope_gen) return true;
        cur = cur.parent;
    }
    return false;
}

fn bool is_in_temp_lane(void* ptr, main::ScopeRegion* scope) {
    if (ptr == null || scope == null) return false;
    main::ScopeChunk* chunk = scope.chunks;
    while (chunk != null) {
        char* data = (char*)chunk + main::ScopeChunk.sizeof;
        if ((char*)ptr >= data && (char*)ptr < data + chunk.capacity) return true;
        chunk = chunk.next;
    }
    return false;
}

fn bool env_chain_has_binding_in_scope(Env* env, main::ScopeRegion* scope, Env* stop_at) {
    Env* cur = env;
    while (cur != null && cur != stop_at) {
        for (usz i = 0; i < cur.binding_count; i++) {
            Value* v = cur.bindings[i].value;
            if (v != null && is_in_temp_lane((void*)v, scope)) return true;
        }
        cur = cur.parent;
    }
    return false;
}

fn Value* jit_finalize_scoped_result(
    Value* result,
    Value* original_result,
    Interp* interp,
    main::ScopeRegion* saved_scope,
    main::ScopeRegion* active_scope,
    uint copy_site
) {
    PromotionContext boundary_ctx = {};
    promotion_context_begin(interp, &boundary_ctx, PROMOTION_CTX_BUDGET_RETURN);

    if (active_scope.refcount == 1 && result != null) {
        if (result.scope_gen != active_scope.escape_generation) {
            main::ScopeRegion* promote_saved_scope = boundary_enter_scope(interp, active_scope);
            result = boundary_promote_to_escape(result, interp);
            boundary_leave_scope(interp, promote_saved_scope);
        }
        if (boundary_ctx.aborted) {
            result = boundary_copy_from_releasing_scope(
                original_result,
                interp,
                active_scope,
                copy_site
            );
            main::scope_release(active_scope);
            promotion_context_end(interp, &boundary_ctx);
            return result;
        }
        if (result.scope_gen == active_scope.escape_generation) {
            boundary_try_scope_splice_escapes(saved_scope, active_scope);
            promotion_context_end(interp, &boundary_ctx);
            return result;
        }
    }

    if (result != null && !main::is_in_scope((void*)result, active_scope)) {
        main::scope_release(active_scope);
        promotion_context_end(interp, &boundary_ctx);
        return result;
    }

    if (result != null) {
        result = boundary_copy_from_releasing_scope(result, interp, active_scope, copy_site);
    }
    main::scope_release(active_scope);
    promotion_context_end(interp, &boundary_ctx);
    return result;
}

fn Env* jit_copy_tco_env_chain_for_recycle(
    Env* env,
    Interp* interp,
    main::ScopeRegion* releasing_scope
) {
    main::ScopeRegion* saved_releasing = interp.releasing_scope;
    interp.releasing_scope = releasing_scope;
    PromotionContext bounce_ctx = {};
    promotion_context_begin(interp, &bounce_ctx, PROMOTION_CTX_BUDGET_TCO);
    Env* copied = copy_tco_env_chain(env, interp);
    promotion_context_end(interp, &bounce_ctx);
    interp.releasing_scope = saved_releasing;
    return copied;
}

fn JitFn jit_lookup_or_compile(Expr* expr, Interp* interp) {
    JitFn cached = jit_cache_lookup(expr);
    if (cached != null) return cached;
    cached = jit_compile(expr, interp);
    if (cached == null) return null;
    jit_cache_store(expr, cached);
    return cached;
}

fn Value* jit_prepare_tco_recycle(
    Env** env_io,
    Env* saved_env,
    Interp* interp
) {
    main::ScopeRegion* cs = interp.current_scope;
    if (cs != interp.tco_recycle_scope || cs.refcount != 1) return null;

    bool chain_touches_releasing_scope =
        env_chain_contains_scope_gen(*env_io, cs.generation, interp.global_env);
    bool bindings_touch_releasing_scope =
        env_chain_has_binding_in_scope(*env_io, cs, interp.global_env);

    if (!chain_touches_releasing_scope &&
        !bindings_touch_releasing_scope) {
        // Fast path: loop env graph is detached from recyclable TEMP lane.
        // Recycle TEMP lane in-place and keep env as-is.
        main::scope_reset_temp_lane(cs);
        interp.current_scope = cs;
        interp.tco_recycle_scope = cs;
        return null;
    }

    // Fallback: allocate fresh call_scope and copy full env chain.
    main::ScopeRegion* fresh = main::scope_create(cs.parent);
    if (fresh == null) {
        interp.jit_env = saved_env;
        return raise_error(interp, "jit: failed to allocate TCO recycle scope");
    }
    interp.current_scope = fresh;
    interp.tco_recycle_scope = fresh;  // Update: fresh is now the recyclable scope
    *env_io = jit_copy_tco_env_chain_for_recycle(*env_io, interp, cs);
    main::scope_release(cs);
    return null;
}

// JIT-aware eval replacement with trampoline TCO.
// Compiles sub-expressions with cache, executes with env context.
// When a tail-call helper sets jit_tco_bounce, loops instead of recursing.
<* @require interp != null : "jit_eval: null interp" *>
fn Value* jit_eval(Expr* expr, Env* env, Interp* interp) {
    Env* saved_env = interp.jit_env;

    for (;;) {
        if (expr == null) {
            interp.jit_env = saved_env;
            return make_nil(interp);
        }

        JitFn cached = jit_lookup_or_compile(expr, interp);
        if (cached == null) {
            // JIT can't compile this expression
            interp.jit_env = saved_env;
            return raise_error(interp, "JIT compilation failed");
        }

        // Execute with env context.
        interp.jit_env = env;
        interp.flags.jit_tco_bounce = false;
        Value* result = cached(interp);

        // Check for TCO bounce.
        if (interp.flags.jit_tco_bounce) {
            // Tail call — loop with new expr/env instead of recursing
            expr = interp.jit_tco_expr;
            env = interp.jit_tco_env;
            interp.flags.jit_tco_bounce = false;

            // TCO scope recycling: only when current_scope is the scope created by
            // jit_eval_in_call_scope (not a scope from handle clause or other context).
            // Guards: scope is marked recyclable, nothing escaped (RC=1).
            // Phase 4b: copy_tco_env_chain copies the FULL chain to global_env so the
            // fresh scope is independent of any env_scope freed during scope_release.
            Value* recycle_err = jit_prepare_tco_recycle(&env, saved_env, interp);
            if (recycle_err != null) {
                return recycle_err;
            }

            continue;
        }

        interp.jit_env = saved_env;
        return result;
    }
}

// Single-scope wrapper: evaluates closure body in ONE child scope.
// Used for non-loop function calls. No TCO recycle scope is set.
// Avoids the overhead of two scopes (result_scope + call_scope).
fn Value* jit_eval_in_single_scope(Expr* body, Env* call_env, Interp* interp) {
    main::ScopeRegion* saved_scope = null;
    main::ScopeRegion* call_scope = boundary_push_child_scope(interp, &saved_scope);
    if (call_scope == null) {
        return raise_error(interp, "jit: failed to allocate call scope");
    }

    Value* result = jit_eval(body, call_env, interp);

    main::ScopeRegion* active_scope = interp.current_scope;

    interp.current_scope = saved_scope;
    return jit_finalize_scoped_result(
        result,
        result,
        interp,
        saved_scope,
        active_scope,
        COPY_SITE_JIT_SINGLE_SCOPE_RETURN
    );
}

// Scope-per-call wrapper: evaluates closure body in a child scope.
// Unified lane model: one scope per call with TEMP+ESCAPE lanes.
fn Value* jit_eval_in_call_scope(Expr* body, Env* call_env, Interp* interp, bool enable_escape_env) {
    main::ScopeRegion* saved_scope = null;

    // Unified one-scope lane model: TEMP recycles, ESCAPE splices.
    main::ScopeRegion* call_scope = boundary_push_child_scope(interp, &saved_scope);
    if (call_scope == null) {
        return raise_error(interp, "jit: failed to allocate call scope");
    }

    bool saved_escape_env = interp.escape_env_mode;
    if (enable_escape_env) {
        interp.escape_env_mode = true;
    }

    // Mark call_scope as eligible for TCO recycling.
    main::ScopeRegion* saved_recycle = interp.tco_recycle_scope;
    interp.tco_recycle_scope = call_scope;

    Value* result = jit_eval(body, call_env, interp);

    // After jit_eval, current_scope may differ from call_scope (TCO recycling)
    main::ScopeRegion* active_scope = interp.current_scope;

    // Restore saved state
    interp.tco_recycle_scope = saved_recycle;
    interp.escape_env_mode = saved_escape_env;
    interp.current_scope = saved_scope;
    return jit_finalize_scoped_result(
        result,
        result,
        interp,
        saved_scope,
        active_scope,
        COPY_SITE_JIT_CALL_SCOPE_STEP2
    );
}

/// Copy env frames from `src` up to (but not including) `stop_at` into current_scope.
/// Copy env frames from `src` up to (but not including) `global_env` into current_scope.
/// Used at TCO bounce to preserve the next iteration's env while releasing old scope.
/// Phase 4b: copies the FULL chain (including env_scope frames from closures) so the
/// fresh scope is independent of any env_scope that might be freed during scope_release.
fn Env* copy_tco_env_chain(Env* src, Interp* interp) {
    if (src == null || src == interp.global_env) return src;

    // Persistent env nodes (mutable boxes in root_region) are kept as-is.
    // Their parent chain still needs fixing up since it may point to env_scope frames.
    if (src.persistent) {
        src.parent = copy_tco_env_chain(src.parent, interp);
        return src;
    }

    // Recursive: copy parent chain first (bottom-up)
    Env* parent_copy = copy_tco_env_chain(src.parent, interp);

    // Create new env frame in current_scope (already switched to fresh scope)
    Env* dst = make_env(interp, parent_copy);
    for (usz i = 0; i < src.binding_count; i++) {
        Value* sv = src.bindings[i].value;
        Value* v = boundary_copy_value_if_owned_by_scope(sv, interp, interp.releasing_scope, COPY_SITE_JIT_TCO_FRAME_COPY);
        env_define_with_barrier(dst, src.bindings[i].name, v, interp);
    }
    // Rebuild hash table if source had one
    if (src.hash_table != null) {
        dst.build_hash_table();
    }
    return dst;
}
