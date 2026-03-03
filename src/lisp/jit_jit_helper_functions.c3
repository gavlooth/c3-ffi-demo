module lisp;

import std::io;
import std::collections::list;
import main;
// =============================================================================
// SECTION 3: JIT HELPER FUNCTIONS
// =============================================================================
//
// These are called BY JIT-compiled code via finishi. They have simple
// calling conventions (pointer/long args, pointer return) that Lightning
// can handle directly.

fn Value* jit_make_int(Interp* interp, long n) {
    return make_int(interp, n);
}

fn Value* jit_make_nil(Interp* interp) {
    return make_nil(interp);
}

fn Value* jit_lookup_global(Interp* interp, uint sym_id) {
    SymbolId name = (SymbolId)sym_id;
    Value* v = interp.global_env.lookup(name);
    if (v != null) return v;
    char[256] ebuf;
    char[] msg = io::bprintf(&ebuf, "unbound variable '%s'",
        (ZString)interp.symbols.get_name(name))!!;
    return raise_error(interp, msg);
}

fn usz format_arity_error(char[] buf, usz expected, usz got) {
    char[] prefix = "arity mismatch: expected ";
    usz pos = 0;
    for (usz i = 0; i < prefix.len && pos < buf.len - 1; i++) buf[pos++] = prefix[i];
    // Write expected count
    char[20] num_buf;
    char[] num_str = int_to_string((long)expected, &num_buf);
    for (usz i = 0; i < num_str.len && pos < buf.len - 1; i++) buf[pos++] = num_str[i];
    char[] mid = " argument(s), got ";
    for (usz i = 0; i < mid.len && pos < buf.len - 1; i++) buf[pos++] = mid[i];
    num_str = int_to_string((long)got, &num_buf);
    for (usz i = 0; i < num_str.len && pos < buf.len - 1; i++) buf[pos++] = num_str[i];
    buf[pos] = 0;
    return pos;
}

fn Value* jit_apply_value_impl(Value* func, Value* arg, Interp* interp) {
    if (func == null) {
        if ((uint)interp.last_call_name != 0) {
            char[256] ebuf;
            char[] msg = io::bprintf(&ebuf, "'%s' is not defined\n  hint: did you (load ...) or (import ...) it?",
                (ZString)interp.symbols.get_name(interp.last_call_name))!!;
            return raise_error(interp, msg);
        }
        return raise_error(interp, "called value is nil — not a function");
    }

    // Propagate errors from function evaluation (e.g. unbound variable)
    if (func.tag == ERROR) return func;

    switch (func.tag) {
        case CLOSURE: {
            assert(func.closure_val != null, "jit_apply_value_impl: func.closure_val is null");
            if (!func.closure_val.has_param && !func.closure_val.has_rest) {
                return jit_eval_in_single_scope(func.closure_val.body, func.closure_val.env, interp);
            }
            if (func.closure_val.has_rest) {
                usz pc = func.closure_val.param_count;
                if (pc == 0) {
                    Env* new_env = func.closure_val.env;
                    Value* rest_list = make_cons(interp, arg, make_nil(interp));
                    new_env = new_env.extend(interp, func.closure_val.rest_param, rest_list);
                    return jit_eval_in_single_scope(func.closure_val.body, new_env, interp);
                } else if (pc == 1) {
                    Env* new_env = func.closure_val.env;
                    new_env = new_env.extend(interp, func.closure_val.params[0], arg);
                    new_env = new_env.extend(interp, func.closure_val.rest_param, make_nil(interp));
                    return jit_eval_in_single_scope(func.closure_val.body, new_env, interp);
                } else {
                    return raise_error(interp, "too few arguments for variadic lambda");
                }
            }
            // Arity check: non-variadic multi-param closure called with 1 arg
            if (func.closure_val.param_count > 1) {
                char[128] buf;
                usz len = format_arity_error(&buf, func.closure_val.param_count, 1);
                return raise_error(interp, buf[:len]);
            }
            assert(func.closure_val.env != null, "jit_apply_value_impl: env is null");
            Env* new_env = func.closure_val.env.extend(interp, func.closure_val.param, arg);
            return jit_eval_in_single_scope(func.closure_val.body, new_env, interp);
        }

        case PRIMITIVE: {
            // Set constructor type_id (needed for type constructor primitives)
            interp.constructor_type_id = func.prim_val.tag;
            EvalResult r = apply_primitive(func, arg, interp);
            if (r.error.has_error) return make_error(interp, r.error.message[:256]);
            return r.value;
        }

        case PARTIAL_PRIM: {
            EvalResult r = apply_partial(func, arg, interp);
            if (r.error.has_error) return make_error(interp, r.error.message[:256]);
            return r.value;
        }

        case CONTINUATION: {
            EvalResult r = jit_apply_continuation_impl(func, arg, interp);
            if (r.error.has_error) return make_error(interp, r.error.message[:256]);
            return r.value;
        }

        case METHOD_TABLE: {
            Value*[1] args = { arg };
            MethodTable* mt = func.method_table_val;
            Value* resolved = find_best_method(mt, args[:1], interp);
            if (resolved != null && resolved.tag == ERROR) return resolved;
            if (resolved == null && mt.fallback != null) {
                resolved = mt.fallback;
            }
            if (resolved == null) {
                return format_dispatch_error(mt, args[:1], interp);
            }
            return jit_apply_value(resolved, arg, interp);
        }

        case SYMBOL: {
            // Symbol-as-function: ('name dict) => (ref dict 'name)
            // Call prim_ref directly — avoids recursion through jit_apply_value
            Value*[2] ref_args;
            ref_args[0] = arg;
            ref_args[1] = func;
            return prim_ref(ref_args[..], null, interp);
        }

        default:
            char[256] ebuf2;
            char[] type_name = interp.symbols.get_name(value_type_name(func, interp));
            if ((uint)interp.last_call_name != 0) {
                char[] msg2 = io::bprintf(&ebuf2, "'%s' is %s, not a function",
                    (ZString)interp.symbols.get_name(interp.last_call_name),
                    (ZString)type_name)!!;
                return raise_error(interp, msg2);
            }
            char[] msg2 = io::bprintf(&ebuf2, "called value is %s, not a function",
                (ZString)type_name)!!;
            return raise_error(interp, msg2);
    }
}

<* @require func != null : "jit_apply_value called with null func" *>
fn Value* jit_apply_value(Value* func, Value* arg, Interp* interp) {
    interp.eval_depth++;
    if (interp.eval_depth > interp.max_eval_depth) {
        $if DEBUG_BUILD:
            io::eprintfn("[debug] Stack overflow at depth %d (max %d)", interp.eval_depth, interp.max_eval_depth);
        $endif
        interp.eval_depth--;
        return make_error(interp, "stack overflow: maximum eval depth exceeded");
    }
    Value* result = jit_apply_value_impl(func, arg, interp);
    interp.eval_depth--;
    return result;
}

// Tail-call variant: sets TCO bounce fields for CLOSURE instead of recursing.
// ONLY called when the result goes directly to R0 as the function return value.
fn Value* jit_apply_value_tail(Value* func, Value* arg, Interp* interp) {
    if (func == null) {
        if ((uint)interp.last_call_name != 0) {
            char[256] ebuf;
            char[] msg = io::bprintf(&ebuf, "'%s' is not defined\n  hint: did you (load ...) or (import ...) it?",
                (ZString)interp.symbols.get_name(interp.last_call_name))!!;
            return raise_error(interp, msg);
        }
        return raise_error(interp, "called value is nil — not a function");
    }

    // Propagate errors from function evaluation (e.g. unbound variable)
    if (func.tag == ERROR) return func;

    if (func.tag == CLOSURE) {
        Env* new_env;
        if (!func.closure_val.has_param && !func.closure_val.has_rest) {
            new_env = func.closure_val.env;
        } else if (func.closure_val.has_rest) {
            usz pc = func.closure_val.param_count;
            if (pc == 0) {
                new_env = func.closure_val.env;
                Value* rest_list = make_cons(interp, arg, make_nil(interp));
                new_env = new_env.extend(interp, func.closure_val.rest_param, rest_list);
            } else if (pc == 1) {
                new_env = func.closure_val.env;
                new_env = new_env.extend(interp, func.closure_val.params[0], arg);
                new_env = new_env.extend(interp, func.closure_val.rest_param, make_nil(interp));
            } else {
                return raise_error(interp, "too few arguments for variadic lambda");
            }
        } else if (func.closure_val.param_count > 1) {
            // Arity check: non-variadic multi-param closure called with 1 arg
            char[128] buf;
            usz len = format_arity_error(&buf, func.closure_val.param_count, 1);
            return raise_error(interp, buf[:len]);
        } else {
            new_env = func.closure_val.env.extend(interp, func.closure_val.param, arg);
        }
        interp.jit_tco_expr = func.closure_val.body;
        interp.jit_tco_env = new_env;
        interp.flags.jit_tco_bounce = true;
        return jit_tco_sentinel();
    }

    // Non-closure: normal apply (no TCO needed for primitives/continuations)
    return jit_apply_value(func, arg, interp);
}

fn Value* jit_eval_fallback(Expr* expr, Env* env, Interp* interp) {
    return jit_eval(expr, env, interp);
}

/// Recursively check if an Expr tree contains E_SHIFT.
/// Used to detect named-let bodies that capture continuations,
/// which need normal env chain copying (can't use escape-scope env).
fn bool expr_contains_shift(Expr* e) {
    if (e == null) return false;
    if (e.tag == E_SHIFT) return true;
    switch (e.tag) {
        case E_IF:
            return expr_contains_shift(e.if_expr.test) ||
                   expr_contains_shift(e.if_expr.then_branch) ||
                   expr_contains_shift(e.if_expr.else_branch);
        case E_LET:
            return expr_contains_shift(e.let_expr.init) ||
                   expr_contains_shift(e.let_expr.body);
        case E_LAMBDA:
            // Lambda creates a closure boundary — shift inside captures
            // the lambda's context, not the named-let's. Safe to skip.
            return false;
        case E_BEGIN:
            for (usz i = 0; i < e.begin.expr_count; i++) {
                if (expr_contains_shift(e.begin.exprs[i])) return true;
            }
            return false;
        case E_CALL:
            if (expr_contains_shift(e.call.func)) return true;
            for (usz i = 0; i < e.call.arg_count; i++) {
                if (expr_contains_shift(e.call.args[i])) return true;
            }
            return false;
        case E_APP:
            return expr_contains_shift(e.app.func) ||
                   expr_contains_shift(e.app.arg);
        case E_MATCH:
            if (expr_contains_shift(e.match.scrutinee)) return true;
            for (usz i = 0; i < e.match.clause_count; i++) {
                if (expr_contains_shift(e.match.clauses[i].result)) return true;
            }
            return false;
        case E_AND:
            return expr_contains_shift(e.and_expr.left) ||
                   expr_contains_shift(e.and_expr.right);
        case E_OR:
            return expr_contains_shift(e.or_expr.left) ||
                   expr_contains_shift(e.or_expr.right);
        case E_SET:
            return expr_contains_shift(e.set_expr.value);
        case E_DEFINE:
            return expr_contains_shift(e.define.value);
        case E_PERFORM:
            return expr_contains_shift(e.perform.arg);
        case E_RESOLVE:
            return expr_contains_shift(e.resolve.value);
        case E_RESET:
            // shift inside reset is delimited — doesn't escape to named-let scope
            return false;
        case E_HANDLE:
            // Handle body runs on its own StackCtx — shift inside is delimited
            return false;
        default:
            return false;
    }
}

fn void jit_warm_pattern_cache(Pattern* pat, Interp* interp) {
    if (pat == null) return;
    switch (pat.tag) {
        case PAT_CONS:
            jit_warm_pattern_cache(pat.car_pat, interp);
            jit_warm_pattern_cache(pat.cdr_pat, interp);
        case PAT_SEQ:
            for (usz i = 0; i < pat.elem_count; i++) {
                jit_warm_pattern_cache(pat.elements[i], interp);
            }
        case PAT_CONSTRUCTOR:
            for (usz i = 0; i < pat.ctor_sub_count; i++) {
                jit_warm_pattern_cache(pat.ctor_sub_patterns[i], interp);
            }
        case PAT_GUARD:
            jit_warm_expr_cache(pat.guard_pred, interp);
            jit_warm_pattern_cache(pat.guard_sub, interp);
        default:
            break;
    }
}

fn void jit_warm_expr_cache(Expr* expr, Interp* interp) {
    if (expr == null || interp == null) return;

    JitFn cached = jit_cache_lookup(expr);
    if (cached == null) {
        JitFn compiled = jit_compile(expr, interp);
        if (compiled != null) {
            jit_cache_store(expr, compiled);
        }
    }

    switch (expr.tag) {
        case E_LAMBDA:
            jit_warm_expr_cache(expr.lambda.body, interp);
        case E_APP:
            jit_warm_expr_cache(expr.app.func, interp);
            jit_warm_expr_cache(expr.app.arg, interp);
        case E_IF:
            jit_warm_expr_cache(expr.if_expr.test, interp);
            jit_warm_expr_cache(expr.if_expr.then_branch, interp);
            jit_warm_expr_cache(expr.if_expr.else_branch, interp);
        case E_LET:
            jit_warm_expr_cache(expr.let_expr.init, interp);
            jit_warm_expr_cache(expr.let_expr.body, interp);
        case E_DEFINE:
            jit_warm_expr_cache(expr.define.value, interp);
        case E_RESET:
            jit_warm_expr_cache(expr.reset.body, interp);
        case E_SHIFT:
            jit_warm_expr_cache(expr.shift.body, interp);
        case E_PERFORM:
            jit_warm_expr_cache(expr.perform.arg, interp);
        case E_HANDLE:
            jit_warm_expr_cache(expr.handle.body, interp);
            for (usz i = 0; i < expr.handle.clause_count; i++) {
                jit_warm_expr_cache(expr.handle.clauses[i].handler_body, interp);
            }
        case E_RESOLVE:
            jit_warm_expr_cache(expr.resolve.value, interp);
        case E_INDEX:
            jit_warm_expr_cache(expr.index.collection, interp);
            jit_warm_expr_cache(expr.index.index, interp);
        case E_MATCH:
            jit_warm_expr_cache(expr.match.scrutinee, interp);
            for (usz i = 0; i < expr.match.clause_count; i++) {
                jit_warm_pattern_cache(expr.match.clauses[i].pattern, interp);
                jit_warm_expr_cache(expr.match.clauses[i].result, interp);
            }
        case E_AND:
            jit_warm_expr_cache(expr.and_expr.left, interp);
            jit_warm_expr_cache(expr.and_expr.right, interp);
        case E_OR:
            jit_warm_expr_cache(expr.or_expr.left, interp);
            jit_warm_expr_cache(expr.or_expr.right, interp);
        case E_CALL:
            jit_warm_expr_cache(expr.call.func, interp);
            for (usz i = 0; i < expr.call.arg_count; i++) {
                jit_warm_expr_cache(expr.call.args[i], interp);
            }
        case E_BEGIN:
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                jit_warm_expr_cache(expr.begin.exprs[i], interp);
            }
        case E_SET:
            jit_warm_expr_cache(expr.set_expr.value, interp);
        case E_QUASIQUOTE:
            jit_warm_expr_cache(expr.quasiquote.body, interp);
        case E_UNQUOTE:
            jit_warm_expr_cache(expr.unquote.body, interp);
        case E_UNQUOTE_SPLICING:
            jit_warm_expr_cache(expr.unquote_splicing.body, interp);
        case E_MODULE:
            for (usz i = 0; i < expr.module_expr.body_count; i++) {
                jit_warm_expr_cache(expr.module_expr.body[i], interp);
            }
        case E_FFI_LIB:
            jit_warm_expr_cache(expr.ffi_lib.path_expr, interp);
        default:
            break;
    }
}

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

        // 1. Check cache
        JitFn cached = jit_cache_lookup(expr);
        if (cached == null) {
            // 2. Compile
            cached = jit_compile(expr, interp);
            if (cached == null) {
                // JIT can't compile this expression
                interp.jit_env = saved_env;
                return raise_error(interp, "JIT compilation failed");
            }
            jit_cache_store(expr, cached);
        }

        // 3. Execute with env context
        interp.jit_env = env;
        interp.flags.jit_tco_bounce = false;
        Value* result = cached(interp);

        // 4. Check for TCO bounce
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
            main::ScopeRegion* cs = interp.current_scope;
            if (cs == interp.tco_recycle_scope && cs.refcount == 1) {
                bool chain_touches_releasing_scope =
                    env_chain_contains_scope_gen(env, cs.generation, interp.global_env);
                bool bindings_touch_releasing_scope =
                    env_chain_has_binding_in_scope(env, cs, interp.global_env);

                if (!chain_touches_releasing_scope &&
                    !bindings_touch_releasing_scope) {
                    // Fast path: loop env graph is detached from recyclable TEMP lane.
                    // Recycle TEMP lane in-place and keep env as-is.
                    main::scope_reset_temp_lane(cs);
                    interp.current_scope = cs;
                    interp.tco_recycle_scope = cs;
                } else {
                    // Fallback: allocate fresh call_scope and copy full env chain.
                    main::ScopeRegion* fresh = main::scope_create(cs.parent);
                    interp.current_scope = fresh;
                    interp.tco_recycle_scope = fresh;  // Update: fresh is now the recyclable scope
                    interp.releasing_scope = cs;       // Tell copy_to_parent what's being released
                    PromotionContext bounce_ctx = {};
                    promotion_context_begin(interp, &bounce_ctx, PROMOTION_CTX_BUDGET_TCO);
                    env = copy_tco_env_chain(env, interp);
                    promotion_context_end(interp, &bounce_ctx);
                    interp.releasing_scope = null;
                    main::scope_release(cs);
                }
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
    main::ScopeRegion* saved_scope = interp.current_scope;
    main::ScopeRegion* call_scope = main::scope_create(saved_scope);
    interp.current_scope = call_scope;

    Value* result = jit_eval(body, call_env, interp);

    main::ScopeRegion* active_scope = interp.current_scope;

    interp.current_scope = saved_scope;
    PromotionContext boundary_ctx = {};
    promotion_context_begin(interp, &boundary_ctx, PROMOTION_CTX_BUDGET_RETURN);
    Value* original_result = result;
    if (active_scope.refcount == 1 && result != null) {
        // Try to upgrade TEMP-rooted returns into ESCAPE lane and splice O(1).
        if (result.scope_gen != active_scope.escape_generation) {
            interp.current_scope = active_scope;
            result = promote_to_escape(result, interp);
            interp.current_scope = saved_scope;
        }
        if (boundary_ctx.aborted) {
            interp.releasing_scope = active_scope;
            result = copy_value_if_owned_by_scope(original_result, interp, active_scope, COPY_SITE_JIT_SINGLE_SCOPE_RETURN);
            interp.releasing_scope = null;
            main::scope_release(active_scope);
            promotion_context_end(interp, &boundary_ctx);
            return result;
        }
        if (result.scope_gen == active_scope.escape_generation) {
            main::scope_splice_escapes(saved_scope, active_scope);
            promotion_context_end(interp, &boundary_ctx);
            return result;
        }
    }

    // Fast-path for non-owned values (already in surviving scope tree).
    if (result != null && !main::is_in_scope((void*)result, active_scope)) {
        main::scope_release(active_scope);
        promotion_context_end(interp, &boundary_ctx);
        return result;
    }

    // Fallback: explicit copy + release.
    interp.releasing_scope = active_scope;
    if (result != null) {
        result = copy_value_if_owned_by_scope(result, interp, active_scope, COPY_SITE_JIT_SINGLE_SCOPE_RETURN);
    }
    interp.releasing_scope = null;
    main::scope_release(active_scope);
    promotion_context_end(interp, &boundary_ctx);

    return result;
}

// Scope-per-call wrapper: evaluates closure body in a child scope.
// Unified lane model: one scope per call with TEMP+ESCAPE lanes.
fn Value* jit_eval_in_call_scope(Expr* body, Env* call_env, Interp* interp) {
    main::ScopeRegion* saved_scope = interp.current_scope;

    // Unified one-scope lane model: TEMP recycles, ESCAPE splices.
    main::ScopeRegion* call_scope = main::scope_create(saved_scope);
    interp.current_scope = call_scope;

    bool saved_escape_env = interp.escape_env_mode;
    interp.escape_env_mode = true;

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
    PromotionContext boundary_ctx = {};
    promotion_context_begin(interp, &boundary_ctx, PROMOTION_CTX_BUDGET_RETURN);
    Value* original_result = result;
    if (active_scope.refcount == 1 && result != null) {
        if (result.scope_gen != active_scope.escape_generation) {
            interp.current_scope = active_scope;
            result = promote_to_escape(result, interp);
            interp.current_scope = saved_scope;
        }
        if (boundary_ctx.aborted) {
            interp.releasing_scope = active_scope;
            result = copy_value_if_owned_by_scope(original_result, interp, active_scope, COPY_SITE_JIT_CALL_SCOPE_STEP2);
            interp.releasing_scope = null;
            main::scope_release(active_scope);
            promotion_context_end(interp, &boundary_ctx);
            return result;
        }
        if (result.scope_gen == active_scope.escape_generation) {
            main::scope_splice_escapes(saved_scope, active_scope);
            promotion_context_end(interp, &boundary_ctx);
            return result;
        }
    }

    if (result != null && !main::is_in_scope((void*)result, active_scope)) {
        main::scope_release(active_scope);
        promotion_context_end(interp, &boundary_ctx);
        return result;
    }

    interp.releasing_scope = active_scope;
    if (result != null) {
        result = copy_value_if_owned_by_scope(result, interp, active_scope, COPY_SITE_JIT_CALL_SCOPE_STEP2);
    }
    interp.releasing_scope = null;
    main::scope_release(active_scope);
    promotion_context_end(interp, &boundary_ctx);

    return result;
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
        Value* v = copy_value_if_owned_by_scope(sv, interp, interp.releasing_scope, COPY_SITE_JIT_TCO_FRAME_COPY);
        env_define_with_barrier(dst, src.bindings[i].name, v, interp);
    }
    // Rebuild hash table if source had one
    if (src.hash_table != null) {
        dst.build_hash_table();
    }
    return dst;
}

// Adapter: converts jit_eval's Value* return to EvalResult.
// Used to mechanically replace eval() calls in EvalResult-returning functions.
fn EvalResult jit_eval_to_result(Expr* expr, Env* env, Interp* interp) {
    Value* v = jit_eval(expr, env, interp);
    if (v != null && v.tag == ERROR) {
        return eval_error_expr(v.str_chars[:v.str_len], expr);
    }
    return eval_ok(v);
}

// Variable lookup that checks jit_env first (for sub-expression evaluation with local bindings)
fn Value* jit_lookup_var(Interp* interp, uint sym_id) {
    SymbolId name = (SymbolId)sym_id;
    // Check JIT env first (set during jit_eval sub-expression evaluation)
    if (interp.jit_env != null) {
        Value* v = interp.jit_env.lookup(name);
        if (v != null) return v;
    }
    // Fall back to global
    Value* v = interp.global_env.lookup(name);
    if (v != null) return v;
    char[256] ebuf;
    char[] msg = io::bprintf(&ebuf, "unbound variable '%s'",
        (ZString)interp.symbols.get_name(name))!!;
    return raise_error(interp, msg);
}

// TCO sentinel — returned by helpers to signal "bounce, don't recurse"
Value g_jit_tco_sentinel;

fn Value* jit_tco_sentinel() {
    return &g_jit_tco_sentinel;
}

// Execute a JitFn with trampoline TCO handling.
// Must be used whenever calling a JitFn from outside jit_eval (e.g., run(), test harness).
fn Value* jit_exec(JitFn f, Interp* interp) {
    Value* result = f(interp);
    while (interp.flags.jit_tco_bounce) {
        interp.flags.jit_tco_bounce = false;
        result = jit_eval(interp.jit_tco_expr, interp.jit_tco_env, interp);
    }
    return result;
}

// Returns jit_env if set (sub-expression context), else global_env
fn Env* jit_get_env(Interp* interp) {
    if (interp.jit_env != null) return interp.jit_env;
    return interp.global_env;
}

fn bool jit_is_falsy(Value* v, Interp* interp) {
    return is_falsy(v, interp);
}

fn Value* jit_make_true(Interp* interp) {
    return make_symbol(interp, interp.sym_true);
}

fn Value* jit_make_false(Interp* interp) {
    return make_symbol(interp, interp.sym_false);
}

// Lambda/closure creation helper — called from JIT-compiled code.
// Mirrors eval_lambda from eval.c3: creates a closure value from an E_LAMBDA Expr*.
// Phase 4b: Closures allocated in current_scope (not root_scope). Env copied to a
// standalone env_scope (refcounted). Temporary closures freed when scope releases.
fn Value* jit_make_closure_from_expr(Interp* interp, Expr* expr, Env* env) {
    main::ScopeRegion* creation_scope = interp.current_scope;
    bool need_env_copy = (creation_scope != interp.root_scope);

    // Check for zero-arg lambda (sentinel param)
    if ((uint)expr.lambda.param == 0xFFFFFFFF && !expr.lambda.has_rest) {
        Value* closure = make_closure_no_param(interp, expr.lambda.body, env);
        if (need_env_copy) {
            main::ScopeRegion* env_scope = main::scope_create(null);
            interp.current_scope = env_scope;
            closure.closure_val.env = copy_env_to_scope(env, interp);
            closure.closure_val.env_scope = env_scope;
            interp.current_scope = creation_scope;
        }
        return closure;
    }

    // Create closure in current_scope
    Value* closure = make_closure(interp, expr.lambda.param, expr.lambda.body, env);

    // Copy variadic info from ExprLambda to Closure
    closure.closure_val.param_count = expr.lambda.param_count;
    if (expr.lambda.param_count > 0) {
        closure.closure_val.params = (SymbolId*)interp.current_scope.alloc(SymbolId.sizeof * expr.lambda.param_count);
        for (usz i = 0; i < expr.lambda.param_count; i++) {
            closure.closure_val.params[i] = expr.lambda.params[i];
        }
    }
    closure.closure_val.has_rest = expr.lambda.has_rest;
    closure.closure_val.rest_param = expr.lambda.rest_param;

    // For variadic with zero fixed params: (lambda (.. args) body)
    if (expr.lambda.has_rest && expr.lambda.param_count == 0) {
        closure.closure_val.has_param = false;
        closure.closure_val.param = 0;
    }

    // Build type signature if lambda has typed params
    closure.closure_val.has_typed_params = expr.lambda.has_typed_params;
    closure.closure_val.type_sig = null;
    if (expr.lambda.has_typed_params) {
        MethodSignature* sig = (MethodSignature*)interp.current_scope.alloc(MethodSignature.sizeof);
        sig.param_count = expr.lambda.param_count;
        sig.constraint_count = 0;
        usz ann_count = expr.lambda.param_count;
        if (ann_count > 8) ann_count = 8;
        for (usz i = 0; i < ann_count; i++) {
            TypeAnnotation* ann = &expr.lambda.param_annotations[i];
            if (ann.has_annotation) {
                if (ann.has_val_literal) {
                    sig.param_types[i] = INVALID_TYPE_ID;
                    sig.val_literals[i] = ann.val_literal;
                    sig.has_val_literal[i] = true;
                } else if (ann.is_dict) {
                    sig.param_types[i] = INVALID_TYPE_ID;
                    sig.has_val_literal[i] = false;
                    for (usz m = 0; m < ann.meta_count && sig.constraint_count < MAX_TYPE_PARAMS; m++) {
                        if (!ann.meta[m].is_int) {
                            sig.constraints[sig.constraint_count].param_sym = ann.meta[m].key;
                            sig.constraints[sig.constraint_count].bound_type =
                                interp.types.lookup(ann.meta[m].sym_value, &interp.symbols);
                            sig.constraint_count++;
                        }
                    }
                } else {
                    sig.param_types[i] = interp.types.lookup(ann.base_type, &interp.symbols);
                    sig.has_val_literal[i] = false;
                }
            } else {
                sig.param_types[i] = INVALID_TYPE_ID;
                sig.has_val_literal[i] = false;
            }
        }
        closure.closure_val.type_sig = sig;
    }

    // Copy env to standalone env_scope if in a non-root scope/frame
    if (need_env_copy) {
        main::ScopeRegion* env_scope = main::scope_create(null);
        interp.current_scope = env_scope;
        closure.closure_val.env = copy_env_to_scope(env, interp);
        closure.closure_val.env_scope = env_scope;
        interp.current_scope = creation_scope;
    }

    return closure;
}

// Recursive let helper — called from JIT-compiled code.
// Mirrors the is_recursive branch of eval_let from eval.c3.
// Creates placeholder, extends env, evaluates init, patches closure, evaluates body.
fn Value* jit_eval_let_rec(Interp* interp, Expr* expr, Env* env) {
    // 1. Create placeholder
    Value* placeholder = make_nil(interp);

    // 2. Extend env with name bound to placeholder
    Env* rec_env = env.extend(interp, expr.let_expr.name, placeholder);

    // 3. JIT-compile init in the extended env (so it can reference itself)
    Value* actual_value = jit_eval(expr.let_expr.init, rec_env, interp);
    if (actual_value != null && actual_value.tag == ERROR) return actual_value;

    // 4. Patch: if closure, create self-reference in its env
    if (is_closure(actual_value)) {
        if (actual_value.closure_val.env_scope != null) {
            // Non-root closure: create weak self-reference in env_scope.
            // Deep copy the closure struct into env_scope so it survives even if call_scope dies.
            main::ScopeRegion* saved_scope = interp.current_scope;
            interp.current_scope = actual_value.closure_val.env_scope;
            
            Value* self_ref = interp.alloc_value();
            self_ref.tag = CLOSURE;
            
            Closure* copied_closure = (Closure*)interp.current_scope.alloc(Closure.sizeof);
            *copied_closure = *actual_value.closure_val;
            copied_closure.scope_gen = interp.current_scope.generation;
            
            if (copied_closure.param_count > 0) {
                copied_closure.params = (SymbolId*)interp.current_scope.alloc(SymbolId.sizeof * copied_closure.param_count);
                for (usz i = 0; i < copied_closure.param_count; i++) {
                    copied_closure.params[i] = actual_value.closure_val.params[i];
                }
            } else {
                copied_closure.params = null;
            }
            
            if (copied_closure.type_sig != null) {
                copied_closure.type_sig = (MethodSignature*)interp.current_scope.alloc(MethodSignature.sizeof);
                *copied_closure.type_sig = *actual_value.closure_val.type_sig;
            }
            
            self_ref.closure_val = copied_closure;
            
            // NO refcount bump on env_scope (self-reference cycle prevention)
            // NO dtor registration — data is in env_scope chunks, freed automatically
            
            interp.current_scope = saved_scope;

            // Patch self-reference in the COPIED env (inside env_scope)
            Env* closure_env = actual_value.closure_val.env;
            for (usz i = 0; i < closure_env.binding_count; i++) {
                if ((uint)closure_env.bindings[i].name == (uint)expr.let_expr.name) {
                    closure_env.bindings[i].value = self_ref;
                    break;
                }
            }
        } else {
            // Root-scope closure: direct assignment (permanent, no copy needed)
            actual_value.closure_val.env = rec_env;
        }
    }

    // Update placeholder binding in rec_env
    for (usz i = 0; i < rec_env.binding_count; i++) {
        if ((uint)rec_env.bindings[i].name == (uint)expr.let_expr.name) {
            rec_env.bindings[i].value = actual_value;
            break;
        }
    }

    // 5. Evaluate body in its own call scope.
    // Named-let desugars to (let ^rec (loop (lambda ...)) (loop init...)).
    // The body (loop init...) triggers TCO bounces for the loop iterations.
    // Using jit_eval_in_call_scope gives each named-let its own scope, so
    // nested named-lets (e.g., flatten's loop calling loop2) don't recycle
    // the outer loop's scope — each loop's tco_recycle_scope is independent.

    // Escape-env optimization: if the loop body doesn't contain shift,
    // enable escape_env_mode so loop env frames use the current scope ESCAPE lane
    // and survive TEMP-lane resets during TCO bounces.
    bool body_has_shift = false;
    if (is_closure(actual_value)) {
        Expr* lambda_body = expr.let_expr.init;
        if (lambda_body != null && lambda_body.tag == E_LAMBDA) {
            body_has_shift = expr_contains_shift(lambda_body.lambda.body);
        }
    }
    bool saved_escape_env = interp.escape_env_mode;
    if (!body_has_shift) {
        interp.escape_env_mode = true;
    }
    Value* result = jit_eval_in_call_scope(expr.let_expr.body, rec_env, interp);
    interp.escape_env_mode = saved_escape_env;
    return result;
}

// set! helper — called from JIT-compiled code.
// Evaluates to the set value, mirrors E_SET in eval().
fn Value* jit_eval_set(Interp* interp, SymbolId name, Value* value, Env* env) {
    // When in child scope/frame, promote value to root so it survives scope release.
    // Env bindings (closures, globals) live in root scope; storing a child-scope
    // value would create a dangling pointer.
    Value* stored = value;
    if (interp.current_scope != interp.root_scope) {
        main::ScopeRegion* saved_scope_ctp = interp.current_scope;
        interp.current_scope = interp.root_scope;
        interp.releasing_scope = saved_scope_ctp;
        stored = copy_to_parent_site(value, interp, COPY_SITE_JIT_EVAL_SET);
        interp.releasing_scope = null;
        interp.current_scope = saved_scope_ctp;
    }
    // Try local env first, then global
    if (catch err = env_set_with_barrier(env, name, stored, interp)) {
        if (interp.global_env == null) return raise_error(interp, "set!: unbound variable");
        if (catch err2 = env_set_with_barrier(interp.global_env, name, stored, interp)) {
            return raise_error(interp, "set!: unbound variable");
        }
    }
    return stored;
}

// set! dot-path helper — handles (set! obj.field value) mutations.
// Mirrors the dot-path E_SET logic from eval.c3 lines 496-561.
fn Value* jit_eval_set_path(Interp* interp, Expr* expr, Value* value, Env* env) {
    // Resolve root object
    Value* current = env.lookup(expr.set_expr.path_segments[0]);
    if (current == null) current = interp.global_env.lookup(expr.set_expr.path_segments[0]);
    if (current == null) return raise_error(interp, "set!: unbound path root");

    // Traverse intermediate segments
    for (usz si = 1; si < expr.set_expr.path_segment_count - 1; si++) {
        if (current != null && current.tag == INSTANCE && current.instance_val != null) {
            TypeInfo* ti = interp.types.get(current.instance_val.type_id);
            bool found = false;
            if (ti != null) {
                for (usz fi = 0; fi < ti.field_count; fi++) {
                    if (ti.fields[fi].name == expr.set_expr.path_segments[si]) {
                        if (fi < current.instance_val.field_count) {
                            current = current.instance_val.fields[fi];
                            found = true;
                            break;
                        }
                    }
                }
            }
            if (!found) return raise_error(interp, "set!: field not found in path");
        } else if (current != null && current.tag == CONS) {
            SymbolId seg = expr.set_expr.path_segments[si];
            if ((uint)seg == (uint)interp.sym_car) { current = current.cons_val.car; }
            else if ((uint)seg == (uint)interp.sym_cdr) { current = current.cons_val.cdr; }
            else { return raise_error(interp, "set!: cons only supports .car and .cdr"); }
        } else {
            return raise_error(interp, "set!: path segment is not an instance or cons");
        }
    }

    // Mutate the last field
    SymbolId last_seg = expr.set_expr.path_segments[expr.set_expr.path_segment_count - 1];

    // Promote value to root scope — cons cells and instances live in root_scope,
    // so storing a child-scope value would create a dangling pointer after scope release.
    value = promote_to_root(value, interp);

    // Cons cell mutation
    if (current != null && current.tag == CONS) {
        if ((uint)last_seg == (uint)interp.sym_car) {
            current.cons_val.car = value;
            return value;
        }
        if ((uint)last_seg == (uint)interp.sym_cdr) {
            current.cons_val.cdr = value;
            return value;
        }
        return raise_error(interp, "set!: cons only supports .car and .cdr");
    }

    // Instance field mutation
    if (current != null && current.tag == INSTANCE && current.instance_val != null) {
        TypeInfo* ti = interp.types.get(current.instance_val.type_id);
        if (ti != null) {
            for (usz fi = 0; fi < ti.field_count; fi++) {
                if (ti.fields[fi].name == last_seg) {
                    if (fi < current.instance_val.field_count) {
                        current.instance_val.fields[fi] = value;
                        return value;
                    }
                }
            }
        }
        return raise_error(interp, "set!: field not found");
    }
    return raise_error(interp, "set!: target is not an instance or cons");
}

// define helper — called from JIT-compiled code.
// Full define with typed dispatch support, mirrors eval_define from eval.c3.
<* @require interp != null : "jit_eval_define: null interp"
   @require value != null : "jit_eval_define: null value" *>
fn Value* jit_eval_define(Interp* interp, SymbolId name, Value* value) {
    // Copy to root scope/region for globals (so value survives scope release)
    Value* stored_val = value;
    if (interp.current_scope != interp.root_scope) {
        main::ScopeRegion* saved_scope_ctp = interp.current_scope;
        interp.current_scope = interp.root_scope;
        interp.releasing_scope = saved_scope_ctp;
        stored_val = copy_to_parent_site(value, interp, COPY_SITE_JIT_EVAL_DEFINE);
        interp.releasing_scope = null;
        interp.current_scope = saved_scope_ctp;
    }

    // Check for typed closure → method table dispatch
    if (stored_val != null && stored_val.tag == CLOSURE && stored_val.closure_val.has_typed_params) {
        Value* existing = interp.global_env.lookup(name);
        if (existing != null && existing.tag == METHOD_TABLE) {
            MethodTable* mt = existing.method_table_val;
            // Grow entries array if needed
            if (mt.entry_count >= mt.capacity) {
                usz new_cap = mt.capacity * 2;
                MethodEntry* new_entries = (MethodEntry*)mem::malloc(MethodEntry.sizeof * new_cap);
                for (usz ei = 0; ei < mt.entry_count; ei++) new_entries[ei] = mt.entries[ei];
                mem::free(mt.entries);
                mt.entries = new_entries;
                mt.capacity = new_cap;
            }
            mt.entries[mt.entry_count].sig = *stored_val.closure_val.type_sig;
            mt.entries[mt.entry_count].implementation = stored_val;
            mt.entry_count++;
            return existing;
        } else if (existing != null && existing.tag == CLOSURE) {
            MethodTable* mt = (MethodTable*)mem::malloc(MethodTable.sizeof);
            mt.name = name;
            mt.entries = (MethodEntry*)mem::malloc(MethodEntry.sizeof * METHOD_INITIAL_CAPACITY);
            mt.capacity = METHOD_INITIAL_CAPACITY;
            mt.entry_count = 0;
            mt.fallback = existing;
            mt.entries[0].sig = *stored_val.closure_val.type_sig;
            mt.entries[0].implementation = stored_val;
            mt.entry_count = 1;
            main::ScopeRegion* saved_mt_scope = interp.current_scope;
            interp.current_scope = interp.root_scope;
            Value* mt_val = interp.alloc_value();
            main::scope_register_dtor(interp.root_scope, (void*)mt_val, &scope_dtor_value);
            interp.current_scope = saved_mt_scope;
            mt_val.tag = METHOD_TABLE;
            mt_val.method_table_val = mt;

            env_define_with_barrier(interp.global_env, name, mt_val, interp);
            return mt_val;
        } else if (existing != null && existing.tag == PRIMITIVE) {
            MethodTable* mt = (MethodTable*)mem::malloc(MethodTable.sizeof);
            mt.name = name;
            mt.entries = (MethodEntry*)mem::malloc(MethodEntry.sizeof * METHOD_INITIAL_CAPACITY);
            mt.capacity = METHOD_INITIAL_CAPACITY;
            mt.entry_count = 0;
            mt.fallback = existing;
            mt.entries[0].sig = *stored_val.closure_val.type_sig;
            mt.entries[0].implementation = stored_val;
            mt.entry_count = 1;
            main::ScopeRegion* saved_mt_scope = interp.current_scope;
            interp.current_scope = interp.root_scope;
            Value* mt_val = interp.alloc_value();
            main::scope_register_dtor(interp.root_scope, (void*)mt_val, &scope_dtor_value);
            interp.current_scope = saved_mt_scope;
            mt_val.tag = METHOD_TABLE;
            mt_val.method_table_val = mt;

            env_define_with_barrier(interp.global_env, name, mt_val, interp);
            return mt_val;
        } else {
            MethodTable* mt = (MethodTable*)mem::malloc(MethodTable.sizeof);
            mt.name = name;
            mt.entries = (MethodEntry*)mem::malloc(MethodEntry.sizeof * METHOD_INITIAL_CAPACITY);
            mt.capacity = METHOD_INITIAL_CAPACITY;
            mt.entries[0].sig = *stored_val.closure_val.type_sig;
            mt.entries[0].implementation = stored_val;
            mt.entry_count = 1;
            mt.fallback = null;
            main::ScopeRegion* saved_mt_scope = interp.current_scope;
            interp.current_scope = interp.root_scope;
            Value* mt_val = interp.alloc_value();
            main::scope_register_dtor(interp.root_scope, (void*)mt_val, &scope_dtor_value);
            interp.current_scope = saved_mt_scope;
            mt_val.tag = METHOD_TABLE;
            mt_val.method_table_val = mt;

            env_define_with_barrier(interp.global_env, name, mt_val, interp);
            return mt_val;
        }
    }

    // Check if we're adding an untyped fallback to an existing method table
    Value* existing_for_fallback = interp.global_env.lookup(name);
    if (existing_for_fallback != null && existing_for_fallback.tag == METHOD_TABLE) {
        existing_for_fallback.method_table_val.fallback = stored_val;
        return existing_for_fallback;
    }

    // Tag closure with its bound name for display
    if (stored_val != null && stored_val.tag == CLOSURE) {
        stored_val.closure_val.name = name;
    }

    // Normal (untyped) define
    env_define_with_barrier(interp.global_env, name, stored_val, interp);
    return stored_val;
}

// Environment extension helper — called from JIT-compiled code.
// Extends an env with a new binding for capturing let-locals in lambdas.
fn Env* jit_env_extend(Interp* interp, Env* env, SymbolId name, Value* value) {
    return env.extend(interp, name, value);
}

// Mutable box allocation in root_region — called from JIT-compiled code.
// Creates a persistent Env node that survives child region cleanup and
// is preserved (not copied) by deep_copy_env. This ensures closures and
// the enclosing JIT scope share the same mutable box.
<* @require interp != null : "jit_env_extend_root: null interp" *>
fn Env* jit_env_extend_root(Interp* interp, Env* env, SymbolId name, Value* value) {
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = interp.root_scope;
    interp.releasing_scope = saved_scope;
    // Promote the initial value to root_scope
    Value* promoted = copy_to_parent_site(value, interp, COPY_SITE_JIT_ENV_EXTEND_ROOT);
    interp.releasing_scope = null;
    Env* box = env.extend(interp, name, promoted);
    box.persistent = true;
    interp.current_scope = saved_scope;
    return box;
}

// JIT-native quasiquote — no eval() dependency.
fn Value* jit_eval_quasiquote(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = jit_qq_impl(expr.quasiquote.body, env, interp, 0);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn EvalResult jit_qq_impl(Expr* tmpl, Env* env, Interp* interp, usz depth) {
    if (depth > 64) return eval_error("quasiquote nesting too deep (max 64)");
    if (tmpl == null) return eval_ok(make_nil(interp));

    switch (tmpl.tag) {
        case E_UNQUOTE: {
            if (depth == 0) {
                return jit_eval_to_result(tmpl.unquote.body, env, interp);
            }
            EvalResult inner = jit_qq_impl(tmpl.unquote.body, env, interp, depth - 1);
            if (inner.error.has_error) return inner;
            Value* sym = make_symbol(interp, interp.sym_unquote);
            Value* rest = make_cons(interp, inner.value, make_nil(interp));
            return eval_ok(make_cons(interp, sym, rest));
        }
        case E_UNQUOTE_SPLICING: {
            if (depth == 0) {
                return eval_error(",@ in non-list context");
            }
            EvalResult inner = jit_qq_impl(tmpl.unquote_splicing.body, env, interp, depth - 1);
            if (inner.error.has_error) return inner;
            Value* sym = make_symbol(interp, interp.sym_unquote_splicing);
            Value* rest = make_cons(interp, inner.value, make_nil(interp));
            return eval_ok(make_cons(interp, sym, rest));
        }
        case E_QUASIQUOTE: {
            EvalResult inner = jit_qq_impl(tmpl.quasiquote.body, env, interp, depth + 1);
            if (inner.error.has_error) return inner;
            Value* sym = make_symbol(interp, interp.sym_quasiquote);
            Value* rest = make_cons(interp, inner.value, make_nil(interp));
            return eval_ok(make_cons(interp, sym, rest));
        }
        case E_LIT:
            return eval_ok(tmpl.lit.value);
        case E_VAR:
            return eval_ok(make_symbol(interp, tmpl.var_expr.name));
        case E_CALL:
            return jit_qq_expand_call(tmpl, env, interp, depth);
        case E_APP: {
            EvalResult f = jit_qq_impl(tmpl.app.func, env, interp, depth);
            if (f.error.has_error) return f;
            EvalResult a = jit_qq_impl(tmpl.app.arg, env, interp, depth);
            if (a.error.has_error) return a;
            Value* rest = make_cons(interp, a.value, make_nil(interp));
            return eval_ok(make_cons(interp, f.value, rest));
        }
        case E_QUOTE:
            return eval_ok(tmpl.quote.datum);
        default:
            return eval_ok(make_nil(interp));
    }
}

fn EvalResult jit_qq_expand_elements(Expr*[] elements, Env* env, Interp* interp, usz depth) {
    Value* result = make_nil(interp);
    for (usz i = elements.len; i > 0; i--) {
        Expr* elem = elements[i - 1];
        if (elem.tag == E_UNQUOTE_SPLICING && depth == 0) {
            EvalResult splice_result = jit_eval_to_result(elem.unquote_splicing.body, env, interp);
            if (splice_result.error.has_error) return splice_result;
            Value* spliced = splice_result.value;
            if (is_nil(spliced)) {
                // Nothing to splice
            } else if (is_cons(spliced)) {
                Value*[64] items;
                usz item_count = 0;
                Value* tmp = spliced;
                while (is_cons(tmp) && item_count < 64) {
                    items[item_count] = tmp.cons_val.car;
                    item_count++;
                    tmp = tmp.cons_val.cdr;
                }
                if (is_cons(tmp)) {
                    return eval_error("quasiquote splice: too many items (max 64)");
                }
                for (usz j = item_count; j > 0; j--) {
                    result = make_cons(interp, items[j - 1], result);
                }
            } else {
                return eval_error(",@ value is not a list");
            }
        } else {
            EvalResult expanded = jit_qq_impl(elem, env, interp, depth);
            if (expanded.error.has_error) return expanded;
            result = make_cons(interp, expanded.value, result);
        }
    }
    return eval_ok(result);
}

fn EvalResult jit_qq_expand_call(Expr* tmpl, Env* env, Interp* interp, usz depth) {
    usz total = tmpl.call.arg_count + 1;
    Expr*[17] elements;
    elements[0] = tmpl.call.func;
    for (usz i = 0; i < tmpl.call.arg_count; i++) {
        elements[i + 1] = tmpl.call.args[i];
    }

    Value* result = make_nil(interp);
    for (usz i = total; i > 0; i--) {
        Expr* elem = elements[i - 1];
        if (elem.tag == E_UNQUOTE_SPLICING && depth == 0) {
            EvalResult splice_result = jit_eval_to_result(elem.unquote_splicing.body, env, interp);
            if (splice_result.error.has_error) return splice_result;
            Value* spliced = splice_result.value;
            if (is_nil(spliced)) {
                // Nothing to splice
            } else if (is_cons(spliced)) {
                Value*[64] items;
                usz item_count = 0;
                Value* tmp = spliced;
                while (is_cons(tmp) && item_count < 64) {
                    items[item_count] = tmp.cons_val.car;
                    item_count++;
                    tmp = tmp.cons_val.cdr;
                }
                if (is_cons(tmp)) {
                    return eval_error("quasiquote splice: too many items (max 64)");
                }
                for (usz j = item_count; j > 0; j--) {
                    result = make_cons(interp, items[j - 1], result);
                }
            } else {
                return eval_error(",@ value is not a list");
            }
        } else {
            EvalResult expanded = jit_qq_impl(elem, env, interp, depth);
            if (expanded.error.has_error) return expanded;
            result = make_cons(interp, expanded.value, result);
        }
    }
    return eval_ok(result);
}

// define-macro helper — called from JIT-compiled code.
// Delegates to the interpreter's eval_define_macro.
fn Value* jit_eval_define_macro(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_define_macro(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

// JIT-native effects — no eval() dependency.
fn Value* jit_exec_reset(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = jit_reset_impl(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn Value* jit_exec_shift(Interp* interp, Expr* expr, Env* env) {
    // Returns Value* directly (fits in RAX, no hidden pointer).
    // EvalResult is >16 bytes, so the System V ABI uses a hidden stack pointer.
    // After stack_ctx_clone that pointer is stale.
    return jit_shift_impl(expr, env, interp);
}

fn Value* jit_exec_handle(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = jit_handle_impl(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn Value* jit_exec_perform(Interp* interp, Expr* expr, Env* env) {
    return jit_signal_impl(expr, env, interp);
}

// =============================================================================
// STACK-CONTEXT-BASED RESET/SHIFT
// =============================================================================

/**
 * Shared state between jit_reset_impl (parent) and jit_shift_impl (body context).
 * Lives on the parent's stack frame. The body reads/writes via interp.current_reset_state.
 */
struct ResetState {
    Expr*   body;         // Reset body expression
    Env*    env;          // Environment at reset time
    Interp* interp;       // Interpreter (for jit_eval)
    bool    shifted;      // Set true by shift before suspending
    Expr*   shift_body;   // Shift's body expression (to be eval'd in parent)
    Env*    shift_env;    // Environment for shift body (includes k binding)
}

/**
 * Entry function for the reset body context.
 * Runs the reset body on a separate stack. On normal completion, stores
 * the result in g_current_stack_ctx.result. On shift, the context is suspended
 * and this function's frame is preserved on the context's stack.
 */
fn void reset_entry(void* arg) {
    ResetState* state = (ResetState*)arg;
    Value* v = jit_eval(state.body, state.env, state.interp);
    main::g_current_stack_ctx.result = v;
}

/**
 * Stack-context-based reset: runs body on a separate stack.
 *
 * If the body completes normally, returns its value.
 * If the body calls (shift k BODY), the context suspends and BODY is
 * evaluated in the parent context with k bound to the suspended context.
 * The shift body's return value becomes the reset's return value.
 */
fn EvalResult jit_reset_impl(Expr* expr, Env* env, Interp* interp) {
    // Precompile reset body on parent stack to avoid cache-miss compilation on switched stacks.
    jit_warm_expr_cache(expr.reset.body, interp);

    main::StackCtx* ctx = main::stack_ctx_create(&interp.stack_ctx_pool);
    if (ctx == null) return eval_error("stack engine: failed to create context for reset");

    ResetState state;
    state.body = expr.reset.body;
    state.env = env;
    state.interp = interp;
    state.shifted = false;
    state.shift_body = null;
    state.shift_env = null;

    main::stack_ctx_init(ctx, &reset_entry, &state);

    // Save/restore the reset state pointer for nesting
    void* saved_state = interp.current_reset_state;
    interp.current_reset_state = &state;

    // Save/restore interp state across context switch
    SavedInterpState saved;
    save_interp_state(interp, &saved);

    main::StackContext parent_ctx;
    main::stack_ctx_switch_to(ctx, &parent_ctx);

    interp.current_reset_state = saved_state;
    restore_interp_state(interp, &saved);

    // Stack overflow — context hit guard page
    if (ctx.status == main::StackCtxStatus.CTX_DEAD) {
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        return eval_error("stack overflow in reset body");
    }

    if (state.shifted) {
        // Shift occurred — evaluate shift body in parent context
        Value* shift_result = jit_eval(state.shift_body, state.shift_env, interp);
        if (shift_result != null && shift_result.tag == ERROR) {
            return eval_error(shift_result.str_chars[:shift_result.str_len]);
        }
        return eval_ok(shift_result);
    }

    // Normal completion (no shift)
    Value* v = (Value*)ctx.result;
    main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
    if (v != null && v.tag == ERROR) {
        return eval_error(v.str_chars[:v.str_len]);
    }
    return eval_ok(v);
}

/**
 * Stack-context-based shift: suspends the current context and creates a continuation.
 *
 * This function is called INSIDE a context (created by jit_reset_impl).
 * It captures the current context as a continuation, stores the shift body info
 * in the parent's ResetState, and suspends. When resumed by (k val),
 * it returns the resume value.
 */
fn Value* jit_shift_impl(Expr* expr, Env* env, Interp* interp) {
    // IMPORTANT: Returns Value* (not EvalResult) so the return fits in RAX.
    // EvalResult is >16 bytes, triggering the System V ABI hidden return pointer.
    // After stack_ctx_clone that pointer is stale, causing writes to the wrong stack.
    main::StackCtx* current = main::g_current_stack_ctx;
    if (current == null) {
        return make_error(interp, "shift outside of reset");
    }

    ResetState* rstate = (ResetState*)interp.current_reset_state;
    if (rstate == null) {
        return make_error(interp, "shift outside of reset (no reset state)");
    }

    // Create continuation wrapping the current context
    Continuation* k = interp.alloc_lisp_continuation();
    k.ctx = current;
    k.is_coroutine_based = true;

    Value* k_val = make_continuation(interp, k);

    // Bind k in the shift body's environment
    Env* shift_env = env.extend(interp, expr.shift.k_name, k_val);

    // Store shift body info for the parent (jit_reset_impl) to evaluate
    rstate.shifted = true;
    rstate.shift_body = expr.shift.body;
    rstate.shift_env = shift_env;

    // Save interpreter state that the parent's evaluation will corrupt.
    SavedInterpState saved;
    save_interp_state(interp, &saved);

    // Suspend the context — control returns to jit_reset_impl
    main::stack_ctx_suspend();

    // === RESUMED HERE by (k val) via jit_apply_continuation_impl ===
    restore_interp_state(interp, &saved);

    return interp.resume_value;
}

/**
 * Apply a continuation: clone the context and resume it.
 *
 * Always clones for multi-shot safety — the original stays suspended
 * as a "template" for potential future invocations.
 */
fn EvalResult jit_apply_continuation_impl(Value* k_val, Value* arg, Interp* interp) {
    main::StackCtx* ctx = k_val.cont_val.ctx;

    if (ctx == null) {
        return eval_error("cannot resume continuation: null stack context");
    }
    if (ctx.status == main::StackCtxStatus.CTX_COMPLETED || ctx.status == main::StackCtxStatus.CTX_DEAD) {
        return eval_error("cannot resume completed/dead continuation");
    }
    if (ctx.status != main::StackCtxStatus.CTX_SUSPENDED) {
        return eval_error("cannot resume running continuation");
    }

    // Clone for multi-shot safety (original stays suspended as template)
    main::StackCtx* target = main::stack_ctx_clone(ctx, &interp.stack_ctx_pool);
    if (target == null) {
        return eval_error("stack engine: failed to clone context for continuation");
    }

    // Pass the resume value to the shift function
    interp.resume_value = arg;

    // Set up a reset-like context to catch any shifts in the resumed body
    ResetState resume_state;
    resume_state.shifted = false;
    resume_state.shift_body = null;
    resume_state.shift_env = null;
    resume_state.body = null;
    resume_state.env = null;
    resume_state.interp = interp;

    void* saved_state = interp.current_reset_state;
    interp.current_reset_state = &resume_state;

    SavedInterpState saved;
    save_interp_state(interp, &saved);

    // Resume the clone
    main::StackContext parent_ctx;
    main::stack_ctx_resume(target, &parent_ctx);

    interp.current_reset_state = saved_state;
    restore_interp_state(interp, &saved);

    // Stack overflow — clone hit guard page
    if (target.status == main::StackCtxStatus.CTX_DEAD) {
        main::stack_ctx_destroy(target, &interp.stack_ctx_pool);
        return eval_error("stack overflow in continuation");
    }

    Value* result;
    if (resume_state.shifted) {
        // The resumed body hit another shift — evaluate that shift's body
        Value* shift_result = jit_eval(resume_state.shift_body, resume_state.shift_env, interp);
        main::stack_ctx_destroy(target, &interp.stack_ctx_pool);
        if (shift_result != null && shift_result.tag == ERROR) {
            return eval_error(shift_result.str_chars[:shift_result.str_len]);
        }
        return eval_ok(shift_result);
    }

    // Clone completed normally
    result = (Value*)target.result;
    main::stack_ctx_destroy(target, &interp.stack_ctx_pool);

    if (result != null && ((Value*)result).tag == ERROR) {
        Value* err = (Value*)result;
        return eval_error(err.str_chars[:err.str_len]);
    }
    return eval_ok(result);
}

// =============================================================================
// END CORO-BASED RESET/SHIFT
// =============================================================================

// =============================================================================
// STACK-CONTEXT-BASED HANDLE/SIGNAL/RESOLVE
// =============================================================================

/**
 * Shared state between jit_handle_impl (parent) and jit_signal_impl (body context).
 * Lives on the parent's stack frame.
 */
struct HandleEffectState {
    Expr*          body;
    Env*           env;
    Interp*        interp;
    bool           signaled;       // Set true by signal before suspending
    SymbolId       signal_tag;     // Which effect tag was signaled
    Value*         signal_arg;     // Argument passed to signal
    EffectHandler  handler_copy;   // Copy of handler for reinstallation by resolve
    usz            handler_idx;    // Handler stack index
    bool           is_aot;
    Value*         aot_body;
}

fn void handle_effect_entry(void* arg) {
    HandleEffectState* state = (HandleEffectState*)arg;
    Value* v;
    if (state.is_aot) {
        v = jit_apply_value(state.aot_body, make_nil(state.interp), state.interp);
    } else {
        v = jit_eval(state.body, state.env, state.interp);
    }
    main::g_current_stack_ctx.result = v;
}

/**
 * Stack-context-based signal: suspends the current context when a matching handler is found.
 *
 * Returns Value* directly (same pattern as jit_shift_impl — avoids the hidden
 * return pointer issue with EvalResult after stack_ctx_clone).
 *
 * Falls through to the I/O fast path or error if no handler matches.
 */
fn Value* jit_signal_impl(Expr* expr, Env* env, Interp* interp) {
    // Evaluate the argument
    Value* arg_v = jit_eval(expr.perform.arg, env, interp);
    if (arg_v != null && arg_v.tag == ERROR) return arg_v;

    SymbolId tag = expr.perform.tag;

    // Type check (same as legacy path)
    TypeId effect_tid = interp.types.lookup(tag, &interp.symbols);
    if (effect_tid != INVALID_TYPE_ID) {
        TypeInfo* effect_info = interp.types.get(effect_tid);
        if (effect_info != null && effect_info.kind == TK_EFFECT && effect_info.field_count > 0) {
            TypeId expected_tid = effect_info.fields[0].field_type;
            if (expected_tid != INVALID_TYPE_ID && expected_tid != interp.tid_Any) {
                TypeId actual_tid = infer_value_type(arg_v, interp);
                if (actual_tid != INVALID_TYPE_ID && !interp.types.is_subtype(actual_tid, expected_tid)) {
                    return make_error(interp, "perform: argument type mismatch");
                }
            }
        }
    }

    // Search handler stack for a matching stack-context handler
    main::StackCtx* current = main::g_current_stack_ctx;
    if (current != null) {
        bool hit_strict = false;
        for (isz i = (isz)interp.handler_count - 1; i >= 0; i--) {
            EffectHandler* h = &interp.handler_stack[(usz)i];

            for (usz j = 0; j < h.clause_count; j++) {
                SymbolId ctag = h.clauses != null ? h.clauses[j].effect_tag : h.tags[j];
                if (ctag == tag) {
                    // Found matching handler
                    if (h.effect_state != null) {
                        // StackCtx path: suspend context, inform handle loop
                        HandleEffectState* hstate = (HandleEffectState*)h.effect_state;
                        hstate.signaled = true;
                        hstate.signal_tag = tag;
                        hstate.signal_arg = arg_v;

                        SavedInterpState* saved_sig = (SavedInterpState*)interp.current_scope.alloc(SavedInterpState.sizeof);
                        save_interp_state(interp, saved_sig);
                        main::stack_ctx_suspend();
                        // === RESUMED by resolve ===
                        restore_interp_state(interp, saved_sig);

                        return interp.resume_value;
                    }
                    // Non-context handler — fall through to legacy
                    break;
                }
            }

            if (h.strict_mode) {
                hit_strict = true;
                break;
            }
        }

        if (hit_strict) {
            return make_error(interp, "strict handler: unhandled effect");
        }
    }

    // I/O fast path: no handler found → look up dispatch table
    for (usz fp_i = 0; fp_i < interp.fast_path_count; fp_i++) {
        if (interp.fast_path_table[fp_i].tag == tag) {
            Value* fp_prim = interp.fast_path_table[fp_i].prim;
            int fp_arity = fp_prim.prim_val.arity;

            if (fp_arity == 0) {
                // Zero-arg effect (e.g., newline): pass nil
                EvalResult r = apply_primitive(fp_prim, make_nil(interp), interp);
                if (r.error.has_error) return make_error(interp, r.error.message[:256]);
                return r.value;
            } else if (fp_arity == 1 || fp_arity == -1) {
                // Single-arg effect: pass signal arg directly
                EvalResult r = apply_primitive(fp_prim, arg_v, interp);
                if (r.error.has_error) return make_error(interp, r.error.message[:256]);
                return r.value;
            } else {
                // Multi-arg effect: signal arg is a cons pair, curry through
                if (arg_v != null && is_cons(arg_v)) {
                    EvalResult r = apply_primitive(fp_prim, car(arg_v), interp);
                    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
                    Value* result = jit_apply_value(r.value, cdr(arg_v), interp);
                    if (result != null && result.tag == ERROR) return result;
                    return result;
                }
            }
            break;
        }
    }

    char[256] ubuf;
    char[] utag = interp.symbols.get_name(tag);
    char[] uarg = interp.symbols.get_name(value_type_name(arg_v, interp));
    char[] umsg = io::bprintf(&ubuf, "unhandled effect '%s' with arg type %s", (ZString)utag, (ZString)uarg)!!;
    return make_error(interp, umsg);
}

/**
 * Dispatch a single signal to a handler clause: find the matching clause, create
 * a continuation, bind k and arg, evaluate the clause body.
 *
 * Returns the handler clause result (Value*). On unmatched clause, returns an
 * ERROR-tagged value. Caller checks ctx.status and hstate.signaled to determine
 * whether to loop, return body result, or abort.
 */
fn Value* jit_handle_dispatch_signal(HandleEffectState* hstate, Expr* expr, Env* env, Interp* interp, main::StackCtx* ctx) {
    hstate.signaled = false;

    // Pop handler for clause evaluation (clause may push its own)
    interp.handler_count = hstate.handler_idx;

    // Find matching clause
    EffectClause* clause = null;
    for (usz j = 0; j < hstate.handler_copy.clause_count; j++) {
        if ((uint)hstate.handler_copy.tags[j] == (uint)hstate.signal_tag) {
            clause = &hstate.handler_copy.clauses[j];
            break;
        }
    }

    if (clause == null) {
        // No matching clause — return error for caller to handle
        char[256] ubuf;
        char[] utag = interp.symbols.get_name(hstate.signal_tag);
        char[] uarg = interp.symbols.get_name(value_type_name(hstate.signal_arg, interp));
        char[] umsg = io::bprintf(&ubuf, "unhandled effect '%s' with arg type %s", (ZString)utag, (ZString)uarg)!!;
        return make_error(interp, umsg);
    }

    // Create continuation wrapping the suspended context
    Continuation* k = interp.alloc_lisp_continuation();
    k.ctx = ctx;
    k.is_coroutine_based = true;
    k.handle_state = hstate;
    Value* k_val = make_continuation(interp, k);

    // Bind k and arg in clause environment
    Env* clause_env = env.extend(interp, clause.k_name, k_val);
    clause_env = clause_env.extend(interp, clause.arg_name, hstate.signal_arg);
    // Also bind __k for resolve
    clause_env = clause_env.extend(interp, interp.sym__k, k_val);

    // Evaluate handler clause
    return jit_eval(clause.handler_body, clause_env, interp);
}

/**
 * Stack-context-based handle: runs body on a context, dispatches signals to handler clauses.
 *
 * Signal suspends the context. The handle loop finds the matching clause, creates
 * a continuation k, and evaluates the clause in the parent context. Resolve
 * resumes the body context (single-shot).
 */
fn EvalResult jit_handle_impl(Expr* expr, Env* env, Interp* interp) {
    // Precompile handle body/clauses on parent stack before switching contexts.
    jit_warm_expr_cache(expr.handle.body, interp);
    for (usz i = 0; i < expr.handle.clause_count; i++) {
        jit_warm_expr_cache(expr.handle.clauses[i].handler_body, interp);
    }

    main::StackCtx* ctx = main::stack_ctx_create(&interp.stack_ctx_pool);
    if (ctx == null) return eval_error("stack engine: failed to create context for handle");

    HandleEffectState state;
    state.body = expr.handle.body;
    state.env = env;
    state.interp = interp;
    state.signaled = false;
    state.signal_tag = (SymbolId)0;
    state.signal_arg = null;

    main::stack_ctx_init(ctx, &handle_effect_entry, &state);

    // Push handler on stack
    if (interp.handler_count >= interp.handler_capacity) interp.grow_handler_stack();
    usz my_idx = interp.handler_count;
    EffectHandler* h = &interp.handler_stack[my_idx];
    h.clause_count = expr.handle.clause_count;
    h.handler_env = env;
    h.body_expr = expr.handle.body;
    h.body_env = env;
    h.strict_mode = expr.handle.strict_mode;
    h.effect_state = &state;

    // Allocate dynamic arrays for handler clauses
    h.tags = (SymbolId*)mem::malloc(SymbolId.sizeof * expr.handle.clause_count);
    h.clauses = (EffectClause*)mem::malloc(EffectClause.sizeof * expr.handle.clause_count);
    if (h.tags == null || h.clauses == null) {
        if (h.tags != null) mem::free(h.tags);
        if (h.clauses != null) mem::free(h.clauses);
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        return eval_error("handle: failed to allocate clauses");
    }
    defer mem::free(h.tags);
    defer mem::free(h.clauses);
    for (usz i = 0; i < expr.handle.clause_count; i++) {
        h.tags[i] = expr.handle.clauses[i].effect_tag;
        h.clauses[i] = expr.handle.clauses[i];
    }

    interp.handler_count = my_idx + 1;

    // Save handler copy for reinstallation after clause evaluation
    state.handler_copy = *h;
    state.handler_idx = my_idx;

    SavedInterpState saved;
    save_interp_state(interp, &saved);

    main::StackContext parent_ctx;
    main::stack_ctx_switch_to(ctx, &parent_ctx);

    // Capture raise state before restoring flags (the context may have set it)
    bool body_raise_pending = interp.flags.raise_pending;
    usz body_raise_len = interp.raise_msg_len;
    if (body_raise_len > 255) body_raise_len = 255;
    char[256] body_raise_msg;
    if (body_raise_pending && body_raise_len > 0) {
        mem::copy(&body_raise_msg[0], &interp.raise_msg[0], body_raise_len);
    }
    restore_interp_state(interp, &saved);
    if (body_raise_pending) {
        interp.flags.raise_pending = true;
        interp.raise_msg_len = body_raise_len;
        if (body_raise_len > 0) {
            mem::copy(&interp.raise_msg[0], &body_raise_msg[0], body_raise_len);
        }
    }

    // Stack overflow — context hit guard page
    if (ctx.status == main::StackCtxStatus.CTX_DEAD) {
        interp.handler_count = my_idx;
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        return eval_error("stack overflow in handle body");
    }

    // Handle loop: process signals
    Value* last_handler_v = null;
    while (state.signaled) {
        last_handler_v = jit_handle_dispatch_signal(&state, expr, env, interp, ctx);

        // resolve was called, body completed
        if (ctx.status == main::StackCtxStatus.CTX_COMPLETED) {
            Value* body_result = (Value*)ctx.result;
            interp.handler_count = my_idx;
            main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
            if (body_result != null && body_result.tag == ERROR) {
                return eval_error(body_result.str_chars[:body_result.str_len]);
            }
            return eval_ok(body_result);
        }

        // Body hit another signal after being resumed — loop to dispatch
        if (state.signaled) {
            continue;
        }

        // Abort or unmatched clause: context still suspended, return handler result
        interp.handler_count = my_idx;
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        if (last_handler_v != null && last_handler_v.tag == ERROR) {
            return eval_error(last_handler_v.str_chars[:last_handler_v.str_len]);
        }
        return eval_ok(last_handler_v);
    }

    // Normal completion (no signal)
    interp.handler_count = my_idx;
    Value* v = (Value*)ctx.result;
    main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);

    // Check for pending raise error
    if (interp.flags.raise_pending) {
        SymbolId raise_sym = interp.sym_raise;
        for (usz ci = 0; ci < expr.handle.clause_count; ci++) {
            if ((uint)expr.handle.clauses[ci].effect_tag == (uint)raise_sym) {
                EffectClause* clause = &expr.handle.clauses[ci];
                interp.flags.raise_pending = false;
                Value* msg_val = make_string(interp, interp.raise_msg[:interp.raise_msg_len]);
                Value* k_val = make_nil(interp);
                Env* clause_env = env.extend(interp, clause.k_name, k_val);
                clause_env = clause_env.extend(interp, clause.arg_name, msg_val);
                Value* handler_v = jit_eval(clause.handler_body, clause_env, interp);
                if (handler_v != null && handler_v.tag != ERROR) {
                    return eval_ok(handler_v);
                }
                if (handler_v != null) {
                    return eval_error(handler_v.str_chars[:handler_v.str_len]);
                }
                return eval_error("raise handler returned nil");
            }
        }
    }

    if (v != null && v.tag == ERROR) {
        return eval_error(v.str_chars[:v.str_len]);
    }
    return eval_ok(v);
}

// =============================================================================
// END CORO-BASED HANDLE/SIGNAL/RESOLVE
// =============================================================================

fn EvalResult jit_apply_continuation(Value* k_val, Value* arg, Interp* interp) {
    if (k_val.cont_val == null) {
        return eval_error("cannot resume continuation: null continuation");
    }
    return jit_apply_continuation_impl(k_val, arg, interp);
}

// Index helper — performs collection indexing without going through eval dispatch.
// Takes already-evaluated collection and index values.
fn Value* jit_do_index(Interp* interp, Value* collection, Value* index) {
    if (collection == null) return raise_error(interp, "index: nil collection");
    // List/cons indexing — supports negative indexing
    if (is_cons(collection) && is_int(index)) {
        long idx = index.int_val;
        if (idx < 0) {
            // Count list length for negative index resolution
            long len = 0;
            Value* c = collection;
            while (is_cons(c)) { len++; c = cdr(c); }
            idx += len;
            if (idx < 0) return raise_error(interp, "list index out of bounds");
        }
        Value* current = collection;
        for (long i = 0; i < idx; i++) {
            if (!is_cons(current)) return raise_error(interp, "list index out of bounds");
            current = cdr(current);
        }
        if (!is_cons(current)) return raise_error(interp, "list index out of bounds");
        return car(current);
    }
    // String indexing — supports negative indexing, UTF-8 codepoint index
    if (is_string(collection) && is_int(index)) {
        char[] bytes = collection.str_chars[:collection.str_len];
        usz cp_count = utf8_strlen(bytes);
        long idx = index.int_val;
        if (idx < 0) idx += (long)cp_count;
        if (idx < 0 || idx >= (long)cp_count) {
            return raise_error(interp, "string index out of bounds");
        }
        usz byte_off = utf8_byte_offset(bytes, (usz)idx);
        usz pos = byte_off;
        uint cp = utf8_decode(bytes, &pos);
        return make_int(interp, (long)cp);
    }
    // Array indexing — supports negative indexing
    if (collection.tag == ARRAY && is_int(index)) {
        long idx = index.int_val;
        long alen = (long)collection.array_val.length;
        if (idx < 0) idx += alen;
        if (idx < 0 || idx >= alen) {
            return raise_error(interp, "array index out of bounds");
        }
        return collection.array_val.items[(usz)idx];
    }
    // Dict indexing
    if (collection.tag == HASHMAP) {
        Value* result = hashmap_get(collection.hashmap_val, index);
        if (result == null) return make_nil(interp);
        return result;
    }
    // Instance indexing — dispatch through `ref` method table if available
    if (collection.tag == INSTANCE) {
        SymbolId ref_sym = interp.symbols.intern("ref");
        Value* ref_fn = interp.global_env.lookup(ref_sym);
        if (ref_fn != null && ref_fn.tag == METHOD_TABLE) {
            Value* args = make_cons(interp, collection, make_cons(interp, index, make_nil(interp)));
            return jit_apply_multi_args(interp, ref_fn, args, 2);
        }
    }
    char[256] ebuf;
    char[] msg = io::bprintf(&ebuf, "cannot index %s (expected array, dict, list, or string)",
        (ZString)interp.symbols.get_name(value_type_name(collection, interp)))!!;
    return raise_error(interp, msg);
}

// Path helper — follows field access chain on Instance values.
// Calls eval_path directly (it doesn't use eval internally).
fn Value* jit_do_path(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_path(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

// Match helper — evaluates scrutinee, runs pattern matching, evals matched result.
// Eliminates one level of eval dispatch compared to jit_eval_match.
fn Value* jit_do_match(Interp* interp, Value* scrutinee, Expr* expr, Env* env) {
    // Set match_env so guard predicates can resolve free variables
    interp.match_env = env;
    for (usz i = 0; i < expr.match.clause_count; i++) {
        MatchClause* clause = &expr.match.clauses[i];
        MatchResult match_r = match_pattern(clause.pattern, scrutinee, interp);
        if (match_r.matched) {
            Env* clause_env = make_env(interp, env);
            for (usz j = 0; j < match_r.binding_count; j++) {
                env_define_with_barrier(clause_env, match_r.bindings[j].name, match_r.bindings[j].value, interp);
            }
            match_r.cleanup();
            // Evaluate clause body directly — TCO bounce doesn't work when
            // match is nested inside define/let/args (outer compiled code
            // would receive the sentinel instead of the actual value).
            return jit_eval(clause.result, clause_env, interp);
        }
    }
    return format_match_error(scrutinee, expr, interp);
}

// Type definition helpers — call eval_* directly, no eval dispatch.
fn Value* jit_do_deftype(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_deftype(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn Value* jit_do_defabstract(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_defabstract(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn Value* jit_do_defunion(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_defunion(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn Value* jit_do_defalias(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_defalias(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn Value* jit_do_defeffect(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_defeffect(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn Value* jit_do_ffi_lib(Interp* interp, Expr* expr, Env* env) {
    return eval_ffi_lib(expr, env, interp);
}

fn Value* jit_do_ffi_fn(Interp* interp, Expr* expr, Env* env) {
    return eval_ffi_fn(expr, env, interp);
}

// JIT-native module/import — no eval() dependency.
fn Value* jit_do_module(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = jit_eval_module_impl(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn Value* jit_do_import(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = jit_eval_import_impl(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn EvalResult jit_eval_module_impl(Expr* expr, Env* env, Interp* interp) {
    SymbolId name = expr.module_expr.name;

    Module* existing = find_module(name, interp);
    if (existing != null) {
        if (existing.loaded) {
            return eval_error("module already defined");
        }
        // Previous load failed — allow re-definition by removing the stale entry
        existing.name = (SymbolId)0;
    }

    if (interp.module_count >= interp.module_capacity) interp.grow_module_table();
    Module* mod = &interp.modules[interp.module_count];
    mod.name = name;
    mod.loaded = false;
    mod.path_len = 0;
    usz exp_cap = expr.module_expr.export_count < 16 ? 16 : expr.module_expr.export_count;
    mod.exports = (SymbolId*)mem::malloc(SymbolId.sizeof * exp_cap);
    mod.export_capacity = exp_cap;
    mod.export_count = expr.module_expr.export_count;
    for (usz i = 0; i < expr.module_expr.export_count; i++) {
        mod.exports[i] = expr.module_expr.exports[i];
    }

    // Module env must live in root_scope (module persists beyond run() scope)
    main::ScopeRegion* saved_mod_scope = interp.current_scope;
    interp.current_scope = interp.root_scope;
    Env* mod_env = make_env(interp, interp.global_env);
    interp.current_scope = saved_mod_scope;
    mod.env = mod_env;
    interp.module_count++;

    usz mod_idx = interp.module_count - 1;
    usz mh_slot = (usz)name % interp.module_hash_capacity;
    while (interp.module_hash_index[mh_slot] != usz.max) {
        mh_slot = (mh_slot + 1) % interp.module_hash_capacity;
    }
    interp.module_hash_index[mh_slot] = mod_idx;

    Env* saved_global = interp.global_env;
    interp.global_env = mod_env;

    EvalResult last_result = eval_ok(make_nil(interp));
    for (usz i = 0; i < expr.module_expr.body_count; i++) {
        last_result = jit_eval_to_result(expr.module_expr.body[i], mod_env, interp);
        if (last_result.error.has_error) {
            interp.global_env = saved_global;
            return last_result;
        }
    }

    interp.global_env = saved_global;
    mod.loaded = true;
    return eval_ok(make_nil(interp));
}

// Extract directory from a file path (e.g. "lib/ffi/torch.omni" → "lib/ffi/")
// Returns length of directory prefix (including trailing /).  0 if no directory.
fn usz path_dir_len(char[] path) {
    usz last_slash = 0;
    bool found = false;
    for (usz i = 0; i < path.len; i++) {
        if (path[i] == '/') { last_slash = i; found = true; }
    }
    return found ? last_slash + 1 : 0;
}

// Resolve a relative path against the current source directory stack.
// Writes resolved path into buf, returns length.  If no source dir on stack, copies path as-is.
fn usz resolve_import_path(char[] rel_path, Interp* interp, char* buf, usz buf_cap) {
    usz pos = 0;
    if (interp.source_dir_count > 0) {
        // Copy current source directory
        char* dir = &interp.source_dirs[interp.source_dir_count - 1];
        usz di = 0;
        while (di < 255 && dir[di] != 0 && pos < buf_cap - 1) {
            buf[pos] = dir[di];
            pos++;
            di++;
        }
    }
    // Copy relative path
    for (usz i = 0; i < rel_path.len && pos < buf_cap - 1; i++) {
        buf[pos] = rel_path[i];
        pos++;
    }
    buf[pos] = 0;
    return pos;
}

// Push a source directory derived from a file path.
fn void push_source_dir(char[] file_path, Interp* interp) {
    if (interp.source_dir_count >= 16) return;
    usz dlen = path_dir_len(file_path);
    usz slot = interp.source_dir_count;
    for (usz i = 0; i < dlen && i < 255; i++) {
        interp.source_dirs[slot][i] = file_path[i];
    }
    interp.source_dirs[slot][dlen] = 0;
    interp.source_dir_count++;
}

fn void pop_source_dir(Interp* interp) {
    if (interp.source_dir_count > 0) interp.source_dir_count--;
}

fn EvalResult jit_load_module_from_file(char[] path, SymbolId name, Interp* interp) {
    if (try content = io::file::load_temp((String)path)) {
        char[] source = content;

        // Push source directory for relative import resolution
        push_source_dir(path, interp);

        Lexer lex;
        lex.init(source);
        Parser p;
        p.init(&lex, interp);

        List{Expr*} expr_list;
        while (!lex.at_end()) {
            expr_list.push(p.parse_expr());
        }
        usz expr_count = expr_list.len();

        if (expr_count > 0 && expr_list[0].tag == E_MODULE) {
            // If this module is already loaded, skip re-evaluation (idempotent file import)
            SymbolId declared_name = expr_list[0].module_expr.name;
            Module* already = find_module(declared_name, interp);
            if (already != null && already.loaded) {
                expr_list.free();
                pop_source_dir(interp);
                return eval_ok(make_nil(interp));
            }

            // Evaluate the module form first
            EvalResult mod_result = jit_eval_to_result(expr_list[0], interp.global_env, interp);
            if (mod_result.error.has_error) { expr_list.free(); pop_source_dir(interp); return mod_result; }

            // Record the file path on the module (for path-based import lookup)
            Module* new_mod = find_module(declared_name, interp);
            if (new_mod != null && new_mod.path_len == 0) {
                for (usz pi = 0; pi < path.len && pi < 255; pi++) {
                    new_mod.path[pi] = path[pi];
                }
                new_mod.path_len = path.len;
            }

            // Evaluate remaining top-level expressions in global scope
            // (e.g. dispatch extensions that need to modify global method tables)
            for (usz i = 1; i < expr_count; i++) {
                EvalResult r = jit_eval_to_result(expr_list[i], interp.global_env, interp);
                if (r.error.has_error) { expr_list.free(); pop_source_dir(interp); return r; }
            }
            expr_list.free();
            pop_source_dir(interp);
            return mod_result;
        }

        if (interp.module_count >= interp.module_capacity) interp.grow_module_table();
        Module* mod = &interp.modules[interp.module_count];
        mod.name = name;
        mod.loaded = false;
        for (usz i = 0; i < path.len && i < 255; i++) {
            mod.path[i] = path[i];
        }
        mod.path_len = path.len;
        mod.export_count = 0;
        mod.export_capacity = 32;
        mod.exports = (SymbolId*)mem::malloc(SymbolId.sizeof * 32);

        // Module env must live in root_scope (module persists beyond run() scope)
        main::ScopeRegion* saved_mod_scope2 = interp.current_scope;
        interp.current_scope = interp.root_scope;
        Env* mod_env = make_env(interp, interp.global_env);
        interp.current_scope = saved_mod_scope2;
        mod.env = mod_env;
        interp.module_count++;

        usz mod_idx2 = interp.module_count - 1;
        usz mh_slot2 = (usz)name % interp.module_hash_capacity;
        while (interp.module_hash_index[mh_slot2] != usz.max) {
            mh_slot2 = (mh_slot2 + 1) % interp.module_hash_capacity;
        }
        interp.module_hash_index[mh_slot2] = mod_idx2;

        Env* saved_global = interp.global_env;
        interp.global_env = mod_env;

        for (usz i = 0; i < expr_count; i++) {
            EvalResult r = jit_eval_to_result(expr_list[i], mod_env, interp);
            if (r.error.has_error) {
                interp.global_env = saved_global;
                expr_list.free();
                pop_source_dir(interp);
                return r;
            }
            if (expr_list[i].tag == E_DEFINE) {
                if (mod.export_count >= mod.export_capacity) {
                    usz new_cap = mod.export_capacity * 2;
                    SymbolId* new_exports = (SymbolId*)mem::malloc(SymbolId.sizeof * new_cap);
                    for (usz j = 0; j < mod.export_count; j++) new_exports[j] = mod.exports[j];
                    mem::free(mod.exports);
                    mod.exports = new_exports;
                    mod.export_capacity = new_cap;
                }
                mod.exports[mod.export_count] = expr_list[i].define.name;
                mod.export_count++;
            }
        }

        interp.global_env = saved_global;
        mod.loaded = true;
        expr_list.free();
        pop_source_dir(interp);
        return eval_ok(make_nil(interp));
    }

    return eval_error("cannot read module file");
}

fn EvalResult jit_eval_import_impl(Expr* expr, Env* env, Interp* interp) {
    SymbolId name = expr.import_expr.name;

    Module* mod = find_module(name, interp);

    if (mod != null) {
        if (!mod.loaded) {
            return eval_error("circular import detected");
        }
    } else if (expr.import_expr.has_path) {
        char[] rel_path = expr.import_expr.path[:expr.import_expr.path_len];
        char[512] resolved;
        usz rlen = resolve_import_path(rel_path, interp, &resolved, 512);
        char[] path = resolved[:rlen];
        usz mod_count_before = interp.module_count;
        EvalResult load_result = jit_load_module_from_file(path, name, interp);
        if (load_result.error.has_error) return load_result;
        // Try import name first, then find the first new module registered by this file
        mod = find_module(name, interp);
        if (mod == null && interp.module_count > mod_count_before) {
            mod = &interp.modules[mod_count_before];
        }
        // Path-based fallback: file declared a module with a different name,
        // or was idempotently skipped. Search by file path.
        if (mod == null) {
            for (usz mi = 0; mi < interp.module_count; mi++) {
                Module* m = &interp.modules[mi];
                if (m.path_len == rlen) {
                    bool match = true;
                    for (usz ci = 0; ci < rlen; ci++) {
                        if (m.path[ci] != path[ci]) { match = false; break; }
                    }
                    if (match && m.loaded) {
                        mod = m;
                        break;
                    }
                }
            }
        }
    } else {
        // Build lib/<name>.omni, then resolve relative to source dir
        char[] sym_name = interp.symbols.get_name(name);
        if (4 + sym_name.len + 5 > 255) {
            return eval_error("module path too long");
        }
        char[256] rel_buf;
        usz rp = 0;
        rel_buf[0] = 'l'; rel_buf[1] = 'i'; rel_buf[2] = 'b'; rel_buf[3] = '/';
        rp = 4;
        for (usz i = 0; i < sym_name.len; i++) {
            rel_buf[rp] = sym_name[i];
            rp++;
        }
        rel_buf[rp] = '.'; rp++;
        rel_buf[rp] = 'o'; rp++;
        rel_buf[rp] = 'm'; rp++;
        rel_buf[rp] = 'n'; rp++;
        rel_buf[rp] = 'i'; rp++;
        rel_buf[rp] = 0;

        char[512] resolved;
        usz rlen = resolve_import_path(rel_buf[:rp], interp, &resolved, 512);
        char[] path = resolved[:rlen];
        EvalResult load_result = jit_load_module_from_file(path, name, interp);
        if (load_result.error.has_error) return load_result;
        mod = find_module(name, interp);
    }

    if (mod == null) {
        return eval_error("module not found");
    }

    // Always bind the module as a first-class value (for qualified access: mod.sym)
    Value* mod_val = make_module(interp, mod);
    env_define_with_barrier(env, name, mod_val, interp);

    if (expr.import_expr.import_all) {
        // (import mod :all) — copy ALL exports into scope (old behavior, explicit opt-in)
        for (usz i = 0; i < mod.export_count; i++) {
            Value* val = mod.env.lookup(mod.exports[i]);
            if (val != null) {
                env_define_with_barrier(env, mod.exports[i], val, interp);
            }
        }
    } else if (expr.import_expr.import_count > 0) {
        // (import mod (sym1 (sym2 :as rename))) — selective import
        for (usz si = 0; si < expr.import_expr.import_count; si++) {
            SymbolId imp_name = expr.import_expr.imports[si];
            SymbolId rename_to = expr.import_expr.aliases[si];
            // Verify it's exported
            bool is_exported = false;
            for (usz ei = 0; ei < mod.export_count; ei++) {
                if ((uint)mod.exports[ei] == (uint)imp_name) {
                    is_exported = true;
                    break;
                }
            }
            if (!is_exported) {
                return eval_error("symbol not exported from module");
            }
            Value* val = mod.env.lookup(imp_name);
            if (val == null) {
                return eval_error("symbol not found in module");
            }
            // Bind using rename if provided, otherwise original name
            SymbolId bind_name = (uint)rename_to != 0 ? rename_to : imp_name;
            env_define_with_barrier(env, bind_name, val, interp);
        }
    }
    // else: qualified-only (import mod) — just the module value is bound

    return eval_ok(make_nil(interp));
}

// Set last_call_name for error messages — called from JIT before apply.
fn void jit_set_call_name(Interp* interp, SymbolId name) {
    interp.last_call_name = name;
}

// Clear last_call_name (for computed function positions)
fn void jit_clear_call_name(Interp* interp) {
    interp.last_call_name = (SymbolId)0;
}

// Cons cell creation helper — called from JIT-compiled code for building arg lists.
fn Value* jit_cons(Interp* interp, Value* car, Value* cdr) {
    return make_cons(interp, car, cdr);
}

// Apply a function to a list of arguments, handling variadic closures natively.
// For variadic closures: binds fixed params from list head, rest from tail, evals body.
// For non-variadic closures: curries through list one arg at a time.
// For primitives: dispatches directly with all args.
// Detects variadic closures appearing mid-curry chain.
fn Value* jit_apply_multi_args(Interp* interp, Value* func, Value* arg_list, usz arg_count) {
    interp.eval_depth++;
    if (interp.eval_depth > interp.max_eval_depth) {
        interp.eval_depth--;
        return make_error(interp, "stack overflow: maximum eval depth exceeded");
    }
    defer interp.eval_depth--;

    if (func == null) {
        if ((uint)interp.last_call_name != 0) {
            char[256] ebuf;
            char[] msg = io::bprintf(&ebuf, "'%s' is not defined\n  hint: did you (load ...) or (import ...) it?",
                (ZString)interp.symbols.get_name(interp.last_call_name))!!;
            return raise_error(interp, msg);
        }
        return raise_error(interp, "called value is nil — not a function");
    }

    // Propagate errors from function evaluation (e.g. unbound variable)
    if (func.tag == ERROR) return func;

    // Zero-arg non-variadic closure: (lambda () body)
    if (arg_count == 0 && func.tag == CLOSURE && !func.closure_val.has_param && !func.closure_val.has_rest) {
        return jit_eval_in_single_scope(func.closure_val.body, func.closure_val.env, interp);
    }

    // Direct variadic closure path
    if (func.tag == CLOSURE && func.closure_val.has_rest) {
        usz pc = func.closure_val.param_count;
        if (arg_count < pc) {
            return raise_error(interp, "variadic lambda arity mismatch");
        }
        Env* new_env = func.closure_val.env;
        Value* curr = arg_list;
        for (usz i = 0; i < pc; i++) {
            if (curr == null || curr.tag != CONS) return raise_error(interp, "arg list too short");
            new_env = new_env.extend(interp, func.closure_val.params[i], curr.cons_val.car);
            curr = curr.cons_val.cdr;
        }
        new_env = new_env.extend(interp, func.closure_val.rest_param, curr);
        return jit_eval_in_single_scope(func.closure_val.body, new_env, interp);
    }

    // Direct primitive path for multi-arg primitives
    if (func.tag == PRIMITIVE) {
        // Set constructor type_id and user_data (needed for type constructor / FFI primitives)
        interp.constructor_type_id = func.prim_val.tag;
        interp.prim_user_data = func.prim_val.user_data;
        usz safe_count = arg_count > 16 ? 16 : arg_count;
        Value** args = safe_count > 0
            ? (Value**)interp.current_scope.alloc(Value*.sizeof * safe_count)
            : null;
        Value* curr = arg_list;
        usz actual_count = 0;
        for (usz i = 0; i < safe_count; i++) {
            if (curr == null || curr.tag != CONS) break;
            args[actual_count++] = curr.cons_val.car;
            curr = curr.cons_val.cdr;
        }
        if (actual_count < safe_count) {
            return raise_error(interp, "arg list too short");
        }
        Value* result = func.prim_val.func(args[:actual_count], null, interp);
        if (is_error(result)) return raise_error(interp, result.str_chars[:result.str_len]);
        return result;
    }

    // Non-variadic closure
    if (func.tag == CLOSURE) {
        usz pc = func.closure_val.param_count;
        if (pc > 1) {
            // Multi-param: strict arity
            if (arg_count < pc) {
                return raise_error(interp, "arity mismatch");
            }
            Env* call_env = make_env(interp, func.closure_val.env);
            Value* curr = arg_list;
            for (usz i = 0; i < pc; i++) {
                if (curr == null || curr.tag != CONS) return raise_error(interp, "arg list too short");
                env_define_with_barrier(call_env, func.closure_val.params[i], curr.cons_val.car, interp);
                curr = curr.cons_val.cdr;
            }
            Value* result = jit_eval_in_single_scope(func.closure_val.body, call_env, interp);
            // If extra args remain and result is callable, chain-apply them
            if (arg_count > pc && result != null && result.tag != ERROR) {
                usz remaining = arg_count - pc;
                return jit_apply_multi_args(interp, result, curr, remaining);
            }
            return result;
        }
        // Single-param: apply one arg, chain-apply rest if any
        // (falls through to one-at-a-time loop below)
    }

    // METHOD_TABLE: dispatch with all args at once
    if (func.tag == METHOD_TABLE) {
        usz safe_count = arg_count > 16 ? 16 : arg_count;
        Value** args = safe_count > 0
            ? (Value**)interp.current_scope.alloc(Value*.sizeof * safe_count)
            : null;
        Value* curr = arg_list;
        usz actual_count = 0;
        for (usz i = 0; i < safe_count; i++) {
            if (curr == null || curr.tag != CONS) break;
            args[actual_count++] = curr.cons_val.car;
            curr = curr.cons_val.cdr;
        }
        MethodTable* mt = func.method_table_val;
        Value* resolved = find_best_method(mt, args[:actual_count], interp);
        if (resolved != null && resolved.tag == ERROR) return resolved;
        if (resolved == null && mt.fallback != null) {
            resolved = mt.fallback;
        }
        if (resolved == null) {
            return format_dispatch_error(mt, args[:actual_count], interp);
        }
        // Apply resolved closure/primitive with all args
        return jit_apply_multi_args(interp, resolved, arg_list, arg_count);
    }

    // Partial primitives, continuations, etc. — apply one arg at a time
    Value* result = func;
    Value* curr = arg_list;
    for (usz i = 0; i < arg_count; i++) {
        if (curr == null || curr.tag != CONS) break;
        Value* arg = curr.cons_val.car;
        curr = curr.cons_val.cdr;
        result = jit_apply_value(result, arg, interp);
        if (result != null && result.tag == ERROR) return result;
    }
    return result;
}

// Tail-call variant: sets TCO bounce fields for CLOSURE instead of recursing.
// ONLY called when the result goes directly to R0 as the function return value.
fn Value* jit_apply_multi_args_tail(Interp* interp, Value* func, Value* arg_list, usz arg_count) {
    interp.eval_depth++;
    if (interp.eval_depth > interp.max_eval_depth) {
        interp.eval_depth--;
        return make_error(interp, "stack overflow: maximum eval depth exceeded");
    }
    defer interp.eval_depth--;

    if (func == null) {
        if ((uint)interp.last_call_name != 0) {
            char[256] ebuf;
            char[] msg = io::bprintf(&ebuf, "'%s' is not defined\n  hint: did you (load ...) or (import ...) it?",
                (ZString)interp.symbols.get_name(interp.last_call_name))!!;
            return raise_error(interp, msg);
        }
        return raise_error(interp, "called value is nil — not a function");
    }

    // Propagate errors from function evaluation (e.g. unbound variable)
    if (func.tag == ERROR) return func;

    // Zero-arg non-variadic closure
    if (arg_count == 0 && func.tag == CLOSURE && !func.closure_val.has_param && !func.closure_val.has_rest) {
        interp.jit_tco_expr = func.closure_val.body;
        interp.jit_tco_env = func.closure_val.env;
        interp.flags.jit_tco_bounce = true;
        return jit_tco_sentinel();
    }

    // Variadic closure
    if (func.tag == CLOSURE && func.closure_val.has_rest) {
        usz pc = func.closure_val.param_count;
        if (arg_count < pc) {
            char[256] ebuf;
            char[] msg = io::bprintf(&ebuf, "variadic lambda requires at least %d argument(s), got %d",
                (int)pc, (int)arg_count)!!;
            return raise_error(interp, msg);
        }
        Env* new_env = func.closure_val.env;
        Value* curr = arg_list;
        for (usz i = 0; i < pc; i++) {
            if (curr == null || curr.tag != CONS) return raise_error(interp, "arg list too short");
            new_env = new_env.extend(interp, func.closure_val.params[i], curr.cons_val.car);
            curr = curr.cons_val.cdr;
        }
        new_env = new_env.extend(interp, func.closure_val.rest_param, curr);
        interp.jit_tco_expr = func.closure_val.body;
        interp.jit_tco_env = new_env;
        interp.flags.jit_tco_bounce = true;
        return jit_tco_sentinel();
    }

    // Non-variadic closure
    if (func.tag == CLOSURE) {
        usz pc = func.closure_val.param_count;
        if (pc > 1) {
            // Multi-param: strict arity
            if (arg_count < pc) {
                char[128] buf;
                usz len = format_arity_error(&buf, pc, arg_count);
                return raise_error(interp, buf[:len]);
            }
            Env* call_env = make_env(interp, func.closure_val.env);
            Value* curr = arg_list;
            for (usz i = 0; i < pc; i++) {
                if (curr == null || curr.tag != CONS) return raise_error(interp, "arg list too short");
                env_define_with_barrier(call_env, func.closure_val.params[i], curr.cons_val.car, interp);
                curr = curr.cons_val.cdr;
            }
            if (arg_count > pc) {
                // Extra args: evaluate body, then chain-apply rest
                Value* result = jit_eval_in_single_scope(func.closure_val.body, call_env, interp);
                if (result != null && result.tag != ERROR) {
                    usz remaining = arg_count - pc;
                    return jit_apply_multi_args_tail(interp, result, curr, remaining);
                }
                return result;
            }
            interp.jit_tco_expr = func.closure_val.body;
            interp.jit_tco_env = call_env;
                interp.flags.jit_tco_bounce = true;
            return jit_tco_sentinel();
        }
        // Single-param: fall through to one-at-a-time loop
    }

    // METHOD_TABLE: resolve, then bounce if closure
    if (func.tag == METHOD_TABLE) {
        usz safe_count = arg_count > 16 ? 16 : arg_count;
        Value** args = safe_count > 0
            ? (Value**)interp.current_scope.alloc(Value*.sizeof * safe_count)
            : null;
        Value* curr = arg_list;
        usz actual_count = 0;
        for (usz i = 0; i < safe_count; i++) {
            if (curr == null || curr.tag != CONS) break;
            args[actual_count++] = curr.cons_val.car;
            curr = curr.cons_val.cdr;
        }
        MethodTable* mt = func.method_table_val;
        Value* resolved = find_best_method(mt, args[:actual_count], interp);
        if (resolved != null && resolved.tag == ERROR) return resolved;
        if (resolved == null && mt.fallback != null) resolved = mt.fallback;
        if (resolved == null) return format_dispatch_error(mt, args[:actual_count], interp);
        // Recurse tail for resolved closure
        return jit_apply_multi_args_tail(interp, resolved, arg_list, arg_count);
    }

    // Everything else: normal apply
    return jit_apply_multi_args(interp, func, arg_list, arg_count);
}

// Environment lookup helper for mutable (boxed) let-locals.
// The env node IS the box — lookup returns the current value.
fn Value* jit_env_lookup_local(Env* env, uint sym_id) {
    SymbolId name = (SymbolId)sym_id;
    Value* v = env.lookup(name);
    if (v != null) return v;
    return null;
}

// Reparent an env node — sets env.parent to new_parent.
// Used by emit_build_locals_env for mutable locals.
fn void jit_env_reparent(Env* env, Env* new_parent) {
    env.parent = new_parent;
}

// Direct primitive helpers — bypass apply() for arithmetic hot path
fn Value* jit_prim_add(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null) {
        if (is_double(a) || is_double(b)) {
            if (is_number(a) && is_number(b)) return make_double(interp, to_double(a) + to_double(b));
        }
        if (a.tag == INT && b.tag == INT) return make_int(interp, a.int_val + b.int_val);
    }
    return raise_error(interp, "+: expected numbers");
}

fn Value* jit_prim_sub(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null) {
        if (is_double(a) || is_double(b)) {
            if (is_number(a) && is_number(b)) return make_double(interp, to_double(a) - to_double(b));
        }
        if (a.tag == INT && b.tag == INT) return make_int(interp, a.int_val - b.int_val);
    }
    return raise_error(interp, "-: expected numbers");
}

fn Value* jit_prim_mul(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null) {
        if (is_double(a) || is_double(b)) {
            if (is_number(a) && is_number(b)) return make_double(interp, to_double(a) * to_double(b));
        }
        if (a.tag == INT && b.tag == INT) return make_int(interp, a.int_val * b.int_val);
    }
    return raise_error(interp, "*: expected numbers");
}

fn Value* jit_prim_lt(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null && is_number(a) && is_number(b)) {
        return to_double(a) < to_double(b) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
    }
    return raise_error(interp, "<: expected numbers");
}

fn Value* jit_prim_gt(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null && is_number(a) && is_number(b)) {
        return to_double(a) > to_double(b) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
    }
    return raise_error(interp, ">: expected numbers");
}

fn Value* jit_prim_eq(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null) {
        bool eq = values_equal(a, b);
        return eq ? make_symbol(interp, interp.sym_true) : make_nil(interp);
    }
    return make_nil(interp);
}
