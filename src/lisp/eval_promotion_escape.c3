module lisp;

import std::io;
import main;

// =============================================================================
// SECTION 2.4: FRAME PUSH/POP (escape/root promotion)
// =============================================================================
fn Value* promote_to_escape_disjoint(Value* v, Interp* interp) {
    PromotionContext local_ctx = {};
    PromotionContext* local_ptr = interp.active_promotion_ctx;
    bool owns_ctx = false;
    if (local_ptr == null) {
        promotion_context_begin(interp, &local_ctx);
        local_ptr = &local_ctx;
        owns_ctx = true;
    }
    Value* copied = copy_to_parent_site_ctx(v, interp, COPY_SITE_GENERIC, local_ptr);
    if (owns_ctx) promotion_context_end(interp, local_ptr);
    if (copied == null || copied == v) return copied;
    return promote_to_escape(copied, interp);
}

fn Value* promote_escape_string_or_error(Value* v, Interp* interp, PromotionContext* ctx) {
    if (!promotion_context_consume(ctx, 1)) return v;
    Value* result = interp.alloc_value_escape();
    result.tag = v.tag;
    main::scope_register_dtor_escape(interp.current_scope, (void*)result, &scope_dtor_value);
    usz len = v.str_len;
    usz cap = len + 1;
    if (cap < 32) cap = 32;
    result.str_chars = (char*)mem::malloc(cap);
    for (usz i = 0; i < len; i++) {
        result.str_chars[i] = v.str_chars[i];
    }
    result.str_chars[len] = 0;
    result.str_len = len;
    return result;
}

fn Value* promote_escape_cons(Value* v, Interp* interp, PromotionContext* ctx) {
    if (!promotion_context_consume(ctx, 1)) return v;
    // Iterative spine copy to avoid recursion blowups on long lists.
    Value* head = interp.alloc_value_escape();
    head.tag = CONS;
    Value* current_new = head;
    Value* current_old = v;

    while (true) {
        current_new.cons_val.car = promote_to_escape(current_old.cons_val.car, interp);
        if (ctx != null && ctx.aborted) return v;
        Value* old_cdr = current_old.cons_val.cdr;
        if (old_cdr != null && old_cdr.tag == CONS &&
            old_cdr.scope_gen == interp.current_scope.generation) {
            if (!promotion_context_consume(ctx, 1)) return v;
            Value* next_new = interp.alloc_value_escape();
            next_new.tag = CONS;
            current_new.cons_val.cdr = next_new;
            current_new = next_new;
            current_old = old_cdr;
        } else {
            current_new.cons_val.cdr = promote_to_escape(old_cdr, interp);
            if (ctx != null && ctx.aborted) return v;
            break;
        }
    }
    return head;
}

fn Value* promote_escape_closure(Value* v, Interp* interp, PromotionContext* ctx) {
    if (!promotion_context_consume(ctx, 1)) return v;
    Value* result = interp.alloc_value_escape();
    result.tag = CLOSURE;
    main::scope_register_dtor_escape(interp.current_scope, (void*)result, &scope_dtor_closure);
    result.closure_val = (Closure*)interp.current_scope.alloc_escape(Closure.sizeof);
    *result.closure_val = *v.closure_val;
    result.closure_val.scope_gen = interp.current_scope.escape_generation;

    if (v.closure_val.param_count > 0) {
        result.closure_val.params = (SymbolId*)interp.current_scope.alloc_escape(SymbolId.sizeof * v.closure_val.param_count);
        for (usz i = 0; i < v.closure_val.param_count; i++) {
            result.closure_val.params[i] = v.closure_val.params[i];
        }
    } else {
        result.closure_val.params = null;
    }

    if (v.closure_val.type_sig != null) {
        result.closure_val.type_sig = (MethodSignature*)interp.current_scope.alloc_escape(MethodSignature.sizeof);
        *result.closure_val.type_sig = *v.closure_val.type_sig;
    }

    if (result.closure_val.env_scope != null) {
        main::scope_retain(result.closure_val.env_scope);
    }
    return result;
}

fn Value* promote_escape_partial_prim(Value* v, Interp* interp, PromotionContext* ctx) {
    if (!promotion_context_consume(ctx, 1)) return v;
    Value* result = interp.alloc_value_escape();
    result.tag = PARTIAL_PRIM;
    result.partial_val.func = v.partial_val.func;
    result.partial_val.remaining = v.partial_val.remaining;
    result.partial_val.first_arg = promote_to_escape(v.partial_val.first_arg, interp);
    result.partial_val.second_arg = promote_to_escape(v.partial_val.second_arg, interp);
    return result;
}

fn Value* promote_escape_shared_wrapper(Value* v, Interp* interp, PromotionContext* ctx) {
    if (!promotion_context_consume(ctx, 1)) return v;
    Value* result = interp.alloc_value_escape();
    *result = *v;
    result.scope_gen = interp.current_scope.escape_generation;
    return result;
}

fn Value* promote_escape_instance(Value* v, Interp* interp, PromotionContext* ctx) {
    if (!promotion_context_consume(ctx, 1)) return v;
    Value* result = interp.alloc_value_escape();
    result.tag = INSTANCE;
    result.scope_gen = interp.current_scope.escape_generation;
    result.instance_val = v.instance_val;
    instance_retain(v.instance_val);
    main::scope_register_dtor_escape(interp.current_scope, (void*)result, &scope_dtor_value);
    return result;
}

fn Value* promote_escape_ffi_handle(Value* v, Interp* interp, PromotionContext* ctx) {
    if (!promotion_context_consume(ctx, 1)) return v;
    Value* result = interp.alloc_value_escape();
    result.tag = FFI_HANDLE;
    result.ffi_val = v.ffi_val;
    ffi_handle_retain(result.ffi_val);
    main::scope_register_dtor_escape(interp.current_scope, (void*)result, &scope_dtor_value);
    return result;
}

fn bool promote_to_escape_fast_path(Value* v, Interp* interp, Value** out) {
    if (v.scope_gen == interp.current_scope.escape_generation) {
        *out = v;
        return true;
    }
    if (v.scope_gen != interp.current_scope.generation) {
        if (boundary_ptr_in_target_scope_chain((void*)v, interp)) {
            *out = v;
            return true;
        }
        // Defensive fallback: disjoint lifetime object (e.g. standalone env_scope).
        // Copy into current TEMP lane first, then continue promotion into ESCAPE.
        *out = promote_to_escape_disjoint(v, interp);
        return true;
    }
    return false;
}

fn Value* promote_escape_scalar(Value* v, Interp* interp, PromotionContext* ctx) {
    if (!promotion_context_consume(ctx, 1)) return v;
    Value* result = interp.alloc_value_escape();
    switch (v.tag) {
        case NIL:
            result.tag = NIL;
        case INT:
            result.tag = INT;
            result.int_val = v.int_val;
        case DOUBLE:
            result.tag = DOUBLE;
            result.double_val = v.double_val;
        case SYMBOL:
            result.tag = SYMBOL;
            result.sym_val = v.sym_val;
        default:
            return v;
    }
    return result;
}

fn Value* promote_escape_continuation(Value* v, Interp* interp, PromotionContext* ctx) {
    if (!promotion_context_consume(ctx, 1)) return v;
    Value* result = interp.alloc_value_escape();
    result.tag = CONTINUATION;
    result.cont_val = v.cont_val;
    return result;
}

fn Value* promote_escape_iterator(Value* v, Interp* interp, PromotionContext* ctx) {
    if (!promotion_context_consume(ctx, 1)) return v;
    Value* result = interp.alloc_value_escape();
    result.tag = ITERATOR;
    result.iterator_val = promote_to_escape(v.iterator_val, interp);
    return result;
}

fn Value* promote_escape_coroutine(Value* v, Interp* interp, PromotionContext* ctx) {
    if (!promotion_context_consume(ctx, 1)) return v;
    Value* result = interp.alloc_value_escape();
    result.tag = COROUTINE;
    result.coroutine_val = v.coroutine_val;
    return result;
}

enum PromoteEscapeRoute : char {
    PE_ROUTE_SCALAR,
    PE_ROUTE_STRING_OR_ERROR,
    PE_ROUTE_CONS,
    PE_ROUTE_CLOSURE,
    PE_ROUTE_PARTIAL_PRIM,
    PE_ROUTE_CONTINUATION,
    PE_ROUTE_ITERATOR,
    PE_ROUTE_COROUTINE,
    PE_ROUTE_SHARED_WRAPPER,
    PE_ROUTE_INSTANCE,
    PE_ROUTE_FFI_HANDLE,
    PE_ROUTE_PASSTHROUGH,
}

fn PromoteEscapeRoute promote_escape_route_for_tag(ValueTag tag) @inline {
    switch (tag) {
        case NIL:
        case INT:
        case DOUBLE:
        case SYMBOL:
            return PE_ROUTE_SCALAR;
        case STRING:
        case ERROR:
            return PE_ROUTE_STRING_OR_ERROR;
        case CONS:
            return PE_ROUTE_CONS;
        case CLOSURE:
            return PE_ROUTE_CLOSURE;
        case PARTIAL_PRIM:
            return PE_ROUTE_PARTIAL_PRIM;
        case CONTINUATION:
            return PE_ROUTE_CONTINUATION;
        case ITERATOR:
            return PE_ROUTE_ITERATOR;
        case COROUTINE:
            return PE_ROUTE_COROUTINE;
        case PRIMITIVE:
        case HASHMAP:
        case ARRAY:
        case METHOD_TABLE:
        case TYPE_INFO:
        case MODULE:
            return PE_ROUTE_SHARED_WRAPPER;
        case INSTANCE:
            return PE_ROUTE_INSTANCE;
        case FFI_HANDLE:
            return PE_ROUTE_FFI_HANDLE;
        default:
            return PE_ROUTE_PASSTHROUGH;
    }
}

fn Value* promote_to_escape_by_route(
    Value* v,
    Interp* interp,
    PromotionContext* ctx,
    PromoteEscapeRoute route
) {
    switch (route) {
        case PE_ROUTE_SCALAR:
            return promote_escape_scalar(v, interp, ctx);
        case PE_ROUTE_STRING_OR_ERROR:
            return promote_escape_string_or_error(v, interp, ctx);
        case PE_ROUTE_CONS:
            return promote_escape_cons(v, interp, ctx);
        case PE_ROUTE_CLOSURE:
            return promote_escape_closure(v, interp, ctx);
        case PE_ROUTE_PARTIAL_PRIM:
            return promote_escape_partial_prim(v, interp, ctx);
        case PE_ROUTE_CONTINUATION:
            return promote_escape_continuation(v, interp, ctx);
        case PE_ROUTE_ITERATOR:
            return promote_escape_iterator(v, interp, ctx);
        case PE_ROUTE_COROUTINE:
            return promote_escape_coroutine(v, interp, ctx);
        case PE_ROUTE_SHARED_WRAPPER:
            // Wrapper promotion into ESCAPE lane; backing data/ownership remains unchanged.
            return promote_escape_shared_wrapper(v, interp, ctx);
        case PE_ROUTE_INSTANCE:
            return promote_escape_instance(v, interp, ctx);
        case PE_ROUTE_FFI_HANDLE:
            return promote_escape_ffi_handle(v, interp, ctx);
        default:
            return v;
    }
}

fn Value* promote_to_escape_by_tag(Value* v, Interp* interp, PromotionContext* ctx) {
    PromoteEscapeRoute route = promote_escape_route_for_tag(v.tag);
    return promote_to_escape_by_route(v, interp, ctx, route);
}

fn void promote_to_escape_memo(Value* old_v, Value* new_v, Interp* interp, PromotionContext* ctx) {
    if (ctx == null || new_v == null || new_v == old_v) return;
    promotion_context_remember(ctx, old_v, new_v, interp);
}

/**
 * Promote a value from current scope TEMP lane to current scope ESCAPE lane.
 * This is an intra-scope promotion primitive for dual-lane rollout.
 * Values not owned by current TEMP lane are returned unchanged.
 */
fn Value* promote_to_escape(Value* v, Interp* interp) {
    if (v == null || interp == null || interp.current_scope == null) return v;
    PromotionContext* ctx = interp.active_promotion_ctx;
    if (ctx != null && ctx.aborted) return v;

    // Fast paths:
    // - already in current ESCAPE lane
    // - in current TEMP lane (normal case for lane promotion)
    // - in a surviving scope chain (root/parent/shared lifetime)
    Value* fast_path = null;
    if (promote_to_escape_fast_path(v, interp, &fast_path)) return fast_path;

    Value* result = promote_to_escape_by_tag(v, interp, ctx);
    promote_to_escape_memo(v, result, interp, ctx);
    return result;
}

// Boundary helper: promote a child-scope value into root while preserving
// releasing_scope context so copy_to_parent keeps its defensive-copy behavior
// for disjoint transient lifetimes.
fn Value* promote_to_root_site(Value* v, Interp* interp, uint site) {
    if (v == null || interp == null || interp.current_scope == interp.root_scope) return v;
    main::ScopeRegion* releasing_scope = interp.current_scope;
    return boundary_copy_to_scope_site(v, interp, interp.root_scope, releasing_scope, site);
}

/**
 * Promote a value to root_scope so it survives child scope release.
 * No-op when already in root_scope (fast path for stdlib loading, etc.).
 * Used when storing values into root-scope data structures (hashmap, array).
 */
fn Value* promote_to_root(Value* v, Interp* interp) {
    return promote_to_root_site(v, interp, COPY_SITE_PROMOTE_TO_ROOT);
}

// Copy a value into an arbitrary target scope, preserving releasing_scope context.
