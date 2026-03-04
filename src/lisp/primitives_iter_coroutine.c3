module lisp;

import std::io;
import main;

// Iterator and coroutine primitives.
fn Value* prim_iterator_p(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    if (args[0].tag == ITERATOR) return make_symbol(interp, interp.sym_true);
    return make_nil(interp);
}

fn Value* prim_make_iterator(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != CLOSURE) {
        return raise_error(interp, "make-iterator: expected closure thunk");
    }
    return make_iterator(interp, args[0]);
}

fn Value* prim_next(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != ITERATOR) {
        return raise_error(interp, "next: expected iterator");
    }
    // Call the thunk closure — iterator_val is a Value* (CLOSURE)
    return jit_apply_value(args[0].iterator_val, make_nil(interp), interp);
}

fn Value* prim_collect(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != ITERATOR) {
        return raise_error(interp, "collect: expected iterator");
    }
    // Consume iterator into a list
    // Collect into stack buffer first, then build list in reverse
    Value*[4096] items;
    usz count = 0;
    Value* it = args[0];

    while (count < 4096) {
        Value* pair = jit_apply_value(it.iterator_val, make_nil(interp), interp);
        if (pair == null || pair.tag == NIL) break;
        if (pair.tag == ERROR) return pair;
        if (!is_cons(pair)) break;
        items[count++] = car(pair);
        Value* rest = cdr(pair);
        if (rest == null || rest.tag != ITERATOR) break;
        it = rest;
    }

    // Build list in reverse
    Value* result = make_nil(interp);
    for (isz i = (isz)count - 1; i >= 0; i--) {
        result = make_cons(interp, items[(usz)i], result);
    }
    return result;
}

fn Value* prim_to_array(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != ITERATOR) {
        return raise_error(interp, "to-array: expected iterator");
    }
    // Consume iterator into an array
    Value*[4096] items;
    usz count = 0;
    Value* it = args[0];

    while (count < 4096) {
        Value* pair = jit_apply_value(it.iterator_val, make_nil(interp), interp);
        if (pair == null || pair.tag == NIL) break;
        if (pair.tag == ERROR) return pair;
        if (!is_cons(pair)) break;
        items[count++] = car(pair);
        Value* rest = cdr(pair);
        if (rest == null || rest.tag != ITERATOR) break;
        it = rest;
    }

    Value* arr = make_array(interp, count);
    for (usz i = 0; i < count; i++) {
        arr.array_val.items[i] = items[i];
    }
    arr.array_val.length = count;
    return arr;
}

// ============================================================================
// COROUTINE PRIMITIVES
// ============================================================================

struct CoroutineThunkState {
    Value*  thunk;
    Interp* interp;
}

fn void coroutine_thunk_entry(void* arg) {
    CoroutineThunkState* state = (CoroutineThunkState*)arg;
    Value* result = jit_apply_value(state.thunk, null, state.interp);
    main::g_current_stack_ctx.result = result;
}

fn Value* prim_coroutine_p(Value*[] args, Env* env, Interp* interp) {
    if (args[0].tag == COROUTINE) return make_symbol(interp, interp.sym_true);
    return make_nil(interp);
}

fn Value* prim_coroutine(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "coroutine: expected a thunk argument");
    Value* thunk = args[0];
    if (thunk == null || thunk.tag != CLOSURE) {
        return raise_error(interp, "coroutine: argument must be a closure");
    }

    // Phase 4b: Promote thunk to root_scope so it survives call_scope release.
    // The coroutine may be resumed after the creating scope is freed.
    thunk = boundary_promote_to_root(thunk, interp);
    if (thunk != null && thunk.tag == CLOSURE) {
        // Precompile coroutine body before first execution on switched stack.
        jit_warm_expr_cache(thunk.closure_val.body, interp);
    }

    main::StackCtx* ctx = main::stack_ctx_create(&interp.stack_ctx_pool);
    if (ctx == null) return raise_error(interp, "coroutine: failed to create context");

    // Store thunk state on the context's user_data (lives as long as the context)
    CoroutineThunkState* state = (CoroutineThunkState*)mem::malloc(CoroutineThunkState.sizeof);
    state.thunk = thunk;
    state.interp = interp;
    ctx.user_data = state;

    main::stack_ctx_init(ctx, &coroutine_thunk_entry, state);

    return make_coroutine(interp, ctx);
}

fn Value* prim_resume(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "resume: expected a coroutine argument");
    Value* coroutine_val = args[0];
    if (coroutine_val == null || coroutine_val.tag != COROUTINE) {
        return raise_error(interp, "resume: argument must be a coroutine");
    }

    main::StackCtx* ctx = coroutine_val.coroutine_val;
    if (ctx == null) return raise_error(interp, "resume: null coroutine");

    if (ctx.status == main::StackCtxStatus.CTX_COMPLETED || ctx.status == main::StackCtxStatus.CTX_DEAD) {
        if (ctx.user_data != null) {
            mem::free(ctx.user_data);
            ctx.user_data = null;
        }
        coroutine_val.coroutine_val = null;
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        return raise_error(interp, "resume: coroutine already completed");
    }

    Value* resume_val = args.len > 1 ? args[1] : make_nil(interp);
    interp.resume_value = resume_val;

    // Save full interpreter state across coroutine switch.
    SavedInterpState saved;
    save_interp_state(interp, &saved);

    main::StackContext parent_ctx;
    if (ctx.status == main::StackCtxStatus.CTX_READY) {
        main::stack_ctx_switch_to(ctx, &parent_ctx);
    } else {
        main::stack_ctx_resume(ctx, &parent_ctx);
    }

    // Restore full interpreter state.
    restore_interp_state(interp, &saved);

    // Stack overflow — coroutine hit guard page
    if (ctx.status == main::StackCtxStatus.CTX_DEAD) {
        if (ctx.user_data != null) {
            mem::free(ctx.user_data);
            ctx.user_data = null;
        }
        coroutine_val.coroutine_val = null;
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        return raise_error(interp, "stack overflow in coroutine");
    }

    if (ctx.status == main::StackCtxStatus.CTX_COMPLETED) {
        Value* result = (Value*)ctx.result;
        // Free thunk state
        if (ctx.user_data != null) {
            mem::free(ctx.user_data);
            ctx.user_data = null;
        }
        coroutine_val.coroutine_val = null;
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        return result != null ? result : make_nil(interp);
    }

    // Coroutine yielded — copy value to caller's scope.
    // Yield values live in the coroutine's call_scope (created by jit_eval_in_call_scope).
    // When the coroutine completes, that scope is released, invalidating all yield values.
    // The caller may still hold pointers to previously yielded values, so we must
    // copy each yield value into the caller's scope (current_scope, restored above).
    if (interp.yield_value != null) {
        return boundary_copy_to_parent_site(interp.yield_value, interp, COPY_SITE_PRIM_RESUME);
    }
    return make_nil(interp);
}

fn Value* prim_yield(Value*[] args, Env* env, Interp* interp) {
    main::StackCtx* current = main::g_current_stack_ctx;
    if (current == null) {
        return raise_error(interp, "yield: not inside a coroutine");
    }

    Value* yield_val = args.len > 0 ? args[0] : make_nil(interp);
    interp.yield_value = yield_val;

    // Save full interpreter state across suspend/resume.
    SavedInterpState saved;
    save_interp_state(interp, &saved);

    // Keep the active scope alive while this context is suspended.
    main::stack_ctx_pin_scope(current, interp.current_scope);
    main::stack_ctx_suspend();

    // Resumed — restore full interpreter state.
    restore_interp_state(interp, &saved);
    main::stack_ctx_unpin_scope(current);

    return interp.resume_value != null ? interp.resume_value : make_nil(interp);
}
