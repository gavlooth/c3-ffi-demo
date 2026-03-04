module lisp;

import std::io;
import std::collections::list;
import main;

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

fn Value* jit_unhandled_effect_error(SymbolId tag, Value* arg_v, Interp* interp) {
    char[256] ubuf;
    char[] utag = interp.symbols.get_name(tag);
    char[] uarg = interp.symbols.get_name(value_type_name(arg_v, interp));
    char[] umsg = io::bprintf(&ubuf, "unhandled effect '%s' with arg type %s", (ZString)utag, (ZString)uarg)!!;
    return make_error(interp, umsg);
}

fn bool effect_handler_has_tag(EffectHandler* h, SymbolId tag) {
    for (usz j = 0; j < h.clause_count; j++) {
        SymbolId ctag = h.clauses != null ? h.clauses[j].effect_tag : h.tags[j];
        if (ctag == tag) return true;
    }
    return false;
}

fn Value* jit_signal_type_check(SymbolId tag, Value* arg_v, Interp* interp) {
    TypeId effect_tid = interp.types.lookup(tag, &interp.symbols);
    if (effect_tid == INVALID_TYPE_ID) return null;

    TypeInfo* effect_info = interp.types.get(effect_tid);
    if (effect_info == null || effect_info.kind != TK_EFFECT || effect_info.field_count == 0) {
        return null;
    }

    TypeId expected_tid = effect_info.fields[0].field_type;
    if (expected_tid == INVALID_TYPE_ID || expected_tid == interp.tid_Any) return null;

    TypeId actual_tid = infer_value_type(arg_v, interp);
    if (actual_tid != INVALID_TYPE_ID && !interp.types.is_subtype(actual_tid, expected_tid)) {
        return make_error(interp, "perform: argument type mismatch");
    }
    return null;
}

fn Value* jit_signal_suspend(EffectHandler* h, SymbolId tag, Value* arg_v, Interp* interp, main::StackCtx* current) {
    if (h.effect_state == null) return null;

    HandleEffectState* hstate = (HandleEffectState*)h.effect_state;
    hstate.signaled = true;
    hstate.signal_tag = tag;
    hstate.signal_arg = arg_v;

    SavedInterpState* saved_sig = (SavedInterpState*)interp.current_scope.alloc(SavedInterpState.sizeof);
    save_interp_state(interp, saved_sig);
    main::stack_ctx_pin_scope(current, interp.current_scope);
    main::stack_ctx_suspend();
    restore_interp_state(interp, saved_sig);
    main::stack_ctx_unpin_scope(current);
    return interp.resume_value;
}

fn Value* jit_signal_try_fast_path(SymbolId tag, Value* arg_v, Interp* interp) {
    for (usz i = 0; i < interp.fast_path_count; i++) {
        if (interp.fast_path_table[i].tag != tag) continue;

        Value* fp_prim = interp.fast_path_table[i].prim;
        int fp_arity = fp_prim.prim_val.arity;
        if (fp_arity == 0) {
            EvalResult r = apply_primitive(fp_prim, make_nil(interp), interp);
            return r.error.has_error ? make_error(interp, r.error.message[:256]) : r.value;
        }
        if (fp_arity == 1 || fp_arity == -1) {
            EvalResult r = apply_primitive(fp_prim, arg_v, interp);
            return r.error.has_error ? make_error(interp, r.error.message[:256]) : r.value;
        }
        if (arg_v != null && is_cons(arg_v)) {
            EvalResult r = apply_primitive(fp_prim, car(arg_v), interp);
            if (r.error.has_error) return make_error(interp, r.error.message[:256]);
            Value* result = jit_apply_value(r.value, cdr(arg_v), interp);
            if (result != null && result.tag == ERROR) return result;
            return result;
        }
        return null;
    }
    return null;
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
    Value* arg_v = jit_eval(expr.perform.arg, env, interp);
    if (arg_v != null && arg_v.tag == ERROR) return arg_v;

    SymbolId tag = expr.perform.tag;
    Value* type_err = jit_signal_type_check(tag, arg_v, interp);
    if (type_err != null) return type_err;

    main::StackCtx* current = main::g_current_stack_ctx;
    if (current != null) {
        bool hit_strict = false;
        for (isz i = (isz)interp.handler_count - 1; i >= 0; i--) {
            EffectHandler* h = &interp.handler_stack[(usz)i];
            if (effect_handler_has_tag(h, tag)) {
                Value* resumed = jit_signal_suspend(h, tag, arg_v, interp, current);
                if (resumed != null) return resumed;
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

    Value* fast = jit_signal_try_fast_path(tag, arg_v, interp);
    if (fast != null) return fast;
    return jit_unhandled_effect_error(tag, arg_v, interp);
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

fn EvalResult value_to_eval_result(Value* v) @inline {
    if (v != null && v.tag == ERROR) {
        return eval_error(v.str_chars[:v.str_len]);
    }
    return eval_ok(v);
}

fn void preserve_raise_across_restore(Interp* interp, SavedInterpState* saved) {
    bool raise_pending = interp.flags.raise_pending;
    usz raise_len = interp.raise_msg_len;
    if (raise_len > 255) raise_len = 255;
    char[256] raise_buf;
    if (raise_pending && raise_len > 0) {
        mem::copy(&raise_buf[0], &interp.raise_msg[0], raise_len);
    }
    restore_interp_state(interp, saved);
    if (raise_pending) {
        interp.flags.raise_pending = true;
        interp.raise_msg_len = raise_len;
        if (raise_len > 0) {
            mem::copy(&interp.raise_msg[0], &raise_buf[0], raise_len);
        }
    }
}

fn Value* dispatch_pending_raise(Expr* expr, Env* env, Interp* interp) {
    if (!interp.flags.raise_pending) return null;
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
            if (handler_v == null) {
                return make_error(interp, "raise handler returned nil");
            }
            return handler_v;
        }
    }
    return null;
}

/**
 * Stack-context-based handle: runs body on a context, dispatches signals to handler clauses.
 *
 * Signal suspends the context. The handle loop finds the matching clause, creates
 * a continuation k, and evaluates the clause in the parent context. Resolve
 * resumes the body context (single-shot).
 */
fn void jit_handle_warm_clauses(Expr* expr, Interp* interp) {
    jit_warm_expr_cache(expr.handle.body, interp);
    for (usz i = 0; i < expr.handle.clause_count; i++) {
        jit_warm_expr_cache(expr.handle.clauses[i].handler_body, interp);
    }
}

fn void jit_handle_init_state(HandleEffectState* state, Expr* expr, Env* env, Interp* interp) {
    state.body = expr.handle.body;
    state.env = env;
    state.interp = interp;
    state.signaled = false;
    state.signal_tag = (SymbolId)0;
    state.signal_arg = null;
}

fn void jit_handle_init_handler(EffectHandler* h, Expr* expr, Env* env, HandleEffectState* state) {
    h.clause_count = expr.handle.clause_count;
    h.handler_env = env;
    h.body_expr = expr.handle.body;
    h.body_env = env;
    h.strict_mode = expr.handle.strict_mode;
    h.effect_state = state;
}

fn bool jit_handle_copy_clauses(EffectHandler* h, Expr* expr) {
    h.tags = (SymbolId*)mem::malloc(SymbolId.sizeof * expr.handle.clause_count);
    h.clauses = (EffectClause*)mem::malloc(EffectClause.sizeof * expr.handle.clause_count);
    if (h.tags == null || h.clauses == null) {
        if (h.tags != null) mem::free(h.tags);
        if (h.clauses != null) mem::free(h.clauses);
        return false;
    }
    for (usz i = 0; i < expr.handle.clause_count; i++) {
        h.tags[i] = expr.handle.clauses[i].effect_tag;
        h.clauses[i] = expr.handle.clauses[i];
    }
    return true;
}

fn EvalResult jit_handle_switch_to_body(main::StackCtx* ctx, Interp* interp) {
    SavedInterpState saved;
    save_interp_state(interp, &saved);

    main::StackContext parent_ctx;
    main::stack_ctx_switch_to(ctx, &parent_ctx);
    preserve_raise_across_restore(interp, &saved);

    if (ctx.status == main::StackCtxStatus.CTX_DEAD) {
        return eval_error("stack overflow in handle body");
    }
    return eval_ok(null);
}

fn bool jit_handle_dispatch_signals(
    HandleEffectState* state,
    Expr* expr,
    Env* env,
    Interp* interp,
    main::StackCtx* ctx,
    EvalResult* out_result
) {
    while (state.signaled) {
        Value* handler_v = jit_handle_dispatch_signal(state, expr, env, interp, ctx);

        // resolve was called, body completed
        if (ctx.status == main::StackCtxStatus.CTX_COMPLETED) {
            *out_result = value_to_eval_result((Value*)ctx.result);
            return true;
        }

        // Body hit another signal after being resumed — loop to dispatch.
        if (state.signaled) {
            continue;
        }

        // Abort or unmatched clause: context still suspended, return handler result.
        *out_result = value_to_eval_result(handler_v);
        return true;
    }
    return false;
}

fn EvalResult jit_handle_finish_no_signal(Expr* expr, Env* env, Interp* interp, main::StackCtx* ctx) {
    Value* raise_v = dispatch_pending_raise(expr, env, interp);
    if (raise_v != null) return value_to_eval_result(raise_v);
    return value_to_eval_result((Value*)ctx.result);
}

fn EvalResult jit_handle_impl(Expr* expr, Env* env, Interp* interp) {
    jit_handle_warm_clauses(expr, interp);

    main::StackCtx* ctx = main::stack_ctx_create(&interp.stack_ctx_pool);
    if (ctx == null) return eval_error("stack engine: failed to create context for handle");
    defer main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);

    HandleEffectState state;
    jit_handle_init_state(&state, expr, env, interp);

    main::stack_ctx_init(ctx, &handle_effect_entry, &state);

    if (interp.handler_count >= interp.handler_capacity) interp.grow_handler_stack();
    usz my_idx = interp.handler_count;
    EffectHandler* h = &interp.handler_stack[my_idx];
    jit_handle_init_handler(h, expr, env, &state);

    if (!jit_handle_copy_clauses(h, expr)) {
        return eval_error("handle: failed to allocate clauses");
    }
    defer mem::free(h.tags);
    defer mem::free(h.clauses);

    interp.handler_count = my_idx + 1;
    defer interp.handler_count = my_idx;

    state.handler_copy = *h;
    state.handler_idx = my_idx;

    EvalResult run_result = jit_handle_switch_to_body(ctx, interp);
    if (run_result.error.has_error) return run_result;

    EvalResult dispatch_result;
    if (jit_handle_dispatch_signals(&state, expr, env, interp, ctx, &dispatch_result)) {
        return dispatch_result;
    }
    return jit_handle_finish_no_signal(expr, env, interp, ctx);
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
