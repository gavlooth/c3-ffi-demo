module lisp;

import std::io;
import main;

const usz ITER_BUFFER_MAX = 4096;

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
    Value* iterator = null;
    Value* parse_err = iterator_require_arg(args, "next: expected iterator", &iterator, interp);
    if (parse_err != null) return parse_err;
    // Call the thunk closure — iterator_val is a Value* (CLOSURE)
    return jit_apply_value(iterator.iterator_val, make_nil(interp), interp);
}

fn Value* iterator_require_arg(Value*[] args, char[] expected_msg, Value** iterator_out, Interp* interp) {
    if (args.len < 1 || args[0].tag != ITERATOR) {
        return raise_error(interp, expected_msg);
    }
    *iterator_out = args[0];
    return null;
}

fn Value* iterator_consume_items(Value* iterator, Value** items, usz* count_out, Interp* interp) {
    usz count = 0;
    Value* it = iterator;

    while (count < ITER_BUFFER_MAX) {
        Value* pair = jit_apply_value(it.iterator_val, make_nil(interp), interp);
        if (pair == null || pair.tag == NIL) break;
        if (pair.tag == ERROR) return pair;
        if (!is_cons(pair)) break;

        items[count++] = car(pair);
        Value* rest = cdr(pair);
        if (rest == null || rest.tag != ITERATOR) break;
        it = rest;
    }

    *count_out = count;
    return null;
}

fn Value* prim_collect(Value*[] args, Env* env, Interp* interp) {
    Value* iterator = null;
    Value* parse_err = iterator_require_arg(args, "collect: expected iterator", &iterator, interp);
    if (parse_err != null) return parse_err;
    // Consume iterator into a list
    // Collect into stack buffer first, then build list in reverse
    Value*[ITER_BUFFER_MAX] items;
    usz count = 0;
    Value* consume_err = iterator_consume_items(iterator, &items[0], &count, interp);
    if (consume_err != null) return consume_err;

    // Build list in reverse
    Value* result = make_nil(interp);
    for (isz i = (isz)count - 1; i >= 0; i--) {
        result = make_cons(interp, items[(usz)i], result);
    }
    return result;
}

fn Value* prim_to_array(Value*[] args, Env* env, Interp* interp) {
    Value* iterator = null;
    Value* parse_err = iterator_require_arg(args, "to-array: expected iterator", &iterator, interp);
    if (parse_err != null) return parse_err;
    // Consume iterator into an array
    Value*[ITER_BUFFER_MAX] items;
    usz count = 0;
    Value* consume_err = iterator_consume_items(iterator, &items[0], &count, interp);
    if (consume_err != null) return consume_err;

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

fn CoroutineThunkState* coroutine_alloc_thunk_state(Value* thunk, Interp* interp) {
    CoroutineThunkState* state = (CoroutineThunkState*)mem::malloc(CoroutineThunkState.sizeof);
    if (state == null) return null;
    state.thunk = thunk;
    state.interp = interp;
    return state;
}

fn Value* prim_coroutine_p(Value*[] args, Env* env, Interp* interp) {
    if (args[0].tag == COROUTINE) return make_symbol(interp, interp.sym_true);
    return make_nil(interp);
}

fn Value* prim_coroutine_require_thunk(Value*[] args, Value** thunk_out, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "coroutine: expected a thunk argument");
    Value* thunk = args[0];
    if (thunk == null || thunk.tag != CLOSURE) {
        return raise_error(interp, "coroutine: argument must be a closure");
    }
    *thunk_out = thunk;
    return null;
}

fn Value* prim_coroutine_prepare_thunk(Value* thunk, Interp* interp) {
    // Phase 4b: Promote thunk to root_scope so it survives call_scope release.
    // The coroutine may be resumed after the creating scope is freed.
    thunk = boundary_promote_to_root(thunk, interp);
    if (thunk != null && thunk.tag == CLOSURE) {
        // Precompile coroutine body before first execution on switched stack.
        jit_warm_expr_cache(thunk.closure_val.body, interp);
    }
    return thunk;
}

fn Value* prim_coroutine_create_ctx(Value* thunk, main::StackCtx** ctx_out, Interp* interp) {
    main::StackCtx* ctx = main::stack_ctx_create(&interp.stack_ctx_pool);
    if (ctx == null) return raise_error(interp, "coroutine: failed to create context");

    // Store thunk state on the context's user_data (lives as long as the context)
    CoroutineThunkState* state = coroutine_alloc_thunk_state(thunk, interp);
    if (state == null) {
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        return raise_error(interp, "coroutine: out of memory");
    }
    ctx.user_data = state;

    main::stack_ctx_init(ctx, &coroutine_thunk_entry, state);
    *ctx_out = ctx;
    return null;
}

fn Value* prim_coroutine(Value*[] args, Env* env, Interp* interp) {
    Value* thunk = null;
    Value* parse_err = prim_coroutine_require_thunk(args, &thunk, interp);
    if (parse_err != null) return parse_err;

    thunk = prim_coroutine_prepare_thunk(thunk, interp);

    main::StackCtx* ctx = null;
    Value* ctx_err = prim_coroutine_create_ctx(thunk, &ctx, interp);
    if (ctx_err != null) return ctx_err;

    return make_coroutine(interp, ctx);
}

fn void coroutine_ctx_cleanup(Value* coroutine_val, main::StackCtx* ctx, Interp* interp) {
    if (ctx.user_data != null) {
        mem::free(ctx.user_data);
        ctx.user_data = null;
    }
    coroutine_val.coroutine_val = null;
    main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
}

fn Value* prim_resume_error_and_cleanup(
    Value* coroutine_val,
    main::StackCtx* ctx,
    Interp* interp,
    char[] msg
) {
    coroutine_ctx_cleanup(coroutine_val, ctx, interp);
    return raise_error(interp, msg);
}

fn Value* prim_resume_complete(Value* coroutine_val, main::StackCtx* ctx, Interp* interp) {
    Value* result = (Value*)ctx.result;
    coroutine_ctx_cleanup(coroutine_val, ctx, interp);
    return result != null ? result : make_nil(interp);
}

fn Value* prim_resume_yield_result(Interp* interp) {
    if (interp.yield_value != null) {
        return boundary_copy_to_parent_site(interp.yield_value, interp, COPY_SITE_PRIM_RESUME);
    }
    return make_nil(interp);
}

fn Value* prim_resume_require_ctx(
    Value*[] args,
    Value** coroutine_out,
    main::StackCtx** ctx_out,
    Interp* interp
) {
    if (args.len < 1) return raise_error(interp, "resume: expected a coroutine argument");
    Value* coroutine_val = args[0];
    if (coroutine_val == null || coroutine_val.tag != COROUTINE) {
        return raise_error(interp, "resume: argument must be a coroutine");
    }

    main::StackCtx* ctx = coroutine_val.coroutine_val;
    if (ctx == null) return raise_error(interp, "resume: null coroutine");

    *coroutine_out = coroutine_val;
    *ctx_out = ctx;
    return null;
}

fn void prim_resume_switch_context(main::StackCtx* ctx, Interp* interp) {
    SavedInterpState saved;
    save_interp_state(interp, &saved);

    main::StackContext parent_ctx;
    if (ctx.status == main::StackCtxStatus.CTX_READY) {
        main::stack_ctx_switch_to(ctx, &parent_ctx);
    } else {
        main::stack_ctx_resume(ctx, &parent_ctx);
    }

    restore_interp_state(interp, &saved);
}

fn Value* prim_resume_post_switch(Value* coroutine_val, main::StackCtx* ctx, Interp* interp) {
    if (ctx.status == main::StackCtxStatus.CTX_DEAD) {
        return prim_resume_error_and_cleanup(coroutine_val, ctx, interp, "stack overflow in coroutine");
    }
    if (ctx.status == main::StackCtxStatus.CTX_COMPLETED) {
        return prim_resume_complete(coroutine_val, ctx, interp);
    }
    return prim_resume_yield_result(interp);
}

fn Value* prim_resume(Value*[] args, Env* env, Interp* interp) {
    Value* coroutine_val = null;
    main::StackCtx* ctx = null;
    Value* parse_err = prim_resume_require_ctx(args, &coroutine_val, &ctx, interp);
    if (parse_err != null) return parse_err;

    if (ctx.status == main::StackCtxStatus.CTX_COMPLETED || ctx.status == main::StackCtxStatus.CTX_DEAD) {
        return prim_resume_error_and_cleanup(coroutine_val, ctx, interp, "resume: coroutine already completed");
    }

    Value* resume_val = args.len > 1 ? args[1] : make_nil(interp);
    interp.resume_value = resume_val;

    prim_resume_switch_context(ctx, interp);
    return prim_resume_post_switch(coroutine_val, ctx, interp);
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
