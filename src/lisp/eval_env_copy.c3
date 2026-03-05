module lisp;

import std::io;
import main;

fn bool copy_env_should_reuse_value(Value* v, Interp* interp) @inline {
    return boundary_can_reuse_value(v, interp);
}

fn Value* copy_env_copy_by_boundary_policy(Value* v, Interp* interp, PromotionContext* ctx) @inline {
    if (copy_env_should_reuse_value(v, interp)) return v;
    return boundary_copy_to_parent_site_ctx(v, interp, COPY_SITE_COPY_ENV_TO_SCOPE, ctx);
}

fn Value* copy_env_value_fast(Value* v, Interp* interp, PromotionContext* ctx = null) {
    if (v == null) return null;
    switch (v.tag) {
        case NIL:
            return make_nil(interp);
        case INT:
            return make_int(interp, v.int_val);
        case DOUBLE:
            return make_double(interp, v.double_val);
        case SYMBOL:
            return make_symbol(interp, v.sym_val);
        case STRING:
            return make_string(interp, v.str_chars[:v.str_len]);
        case ERROR:
            return make_error(interp, v.str_chars[:v.str_len]);
        case PRIMITIVE:
        case HASHMAP:
        case ARRAY:
        case METHOD_TABLE:
        case TYPE_INFO:
        case MODULE:
            // Root-lifetime or external-lifetime objects can be shared.
            return v;
        case INSTANCE:
        case FFI_HANDLE:
            return copy_env_copy_by_boundary_policy(v, interp, ctx);
        default:
            return copy_env_copy_by_boundary_policy(v, interp, ctx);
    }
}

fn bool copy_env_should_clone_closure(Value* val, Interp* interp) @inline {
    if (val == null || val.tag != CLOSURE || val.closure_val.env_scope == null) return false;
    if (copy_env_should_reuse_value(val, interp)) return false;
    return val.closure_val.scope_gen != interp.current_scope.generation;
}

fn void copy_env_clone_closure_payload(Value* result, Value* src, Interp* interp) {
    result.tag = CLOSURE;
    result.closure_val = (Closure*)interp.current_scope.alloc(Closure.sizeof);
    *result.closure_val = *src.closure_val;
    result.closure_val.scope_gen = interp.current_scope.generation;

    if (src.closure_val.param_count > 0) {
        result.closure_val.params = (SymbolId*)interp.current_scope.alloc(SymbolId.sizeof * src.closure_val.param_count);
        for (usz p = 0; p < src.closure_val.param_count; p++) {
            result.closure_val.params[p] = src.closure_val.params[p];
        }
    } else {
        result.closure_val.params = null;
    }

    if (src.closure_val.type_sig != null) {
        result.closure_val.type_sig = (MethodSignature*)interp.current_scope.alloc(MethodSignature.sizeof);
        *result.closure_val.type_sig = *src.closure_val.type_sig;
    }

    if (result.closure_val.env_scope != null) {
        result.closure_val.env_scope.refcount++;
    }
}

fn Value* copy_env_clone_closure_if_needed(Value* val, Interp* interp) {
    if (!copy_env_should_clone_closure(val, interp)) return val;
    Value* result = interp.alloc_value();
    copy_env_clone_closure_payload(result, val, interp);
    main::scope_register_dtor(interp.current_scope, (void*)result, &scope_dtor_closure);
    return result;
}

fn Value* copy_env_clone_iterator_if_needed(Value* val, Interp* interp) {
    if (val == null || val.tag != ITERATOR) return val;
    if (in_target_scope_chain((void*)val, interp)) return val;

    Value* inner = val.iterator_val;
    if (inner != null && inner.tag == CLOSURE && inner.closure_val.env_scope != null) {
        inner = copy_env_clone_closure_if_needed(inner, interp);
    }

    Value* iter_result = interp.alloc_value();
    iter_result.tag = ITERATOR;
    iter_result.iterator_val = inner;
    return iter_result;
}

fn Value* copy_env_binding_value(Value* val, Interp* interp, PromotionContext* ctx) {
    if (val != null && val.tag == CLOSURE) {
        return copy_env_clone_closure_if_needed(val, interp);
    }
    if (val != null && val.tag == ITERATOR) {
        return copy_env_clone_iterator_if_needed(val, interp);
    }
    return copy_env_value_fast(val, interp, ctx);
}

fn Env* copy_env_alloc_frame(Env* src, Interp* interp) {
    Env* dst = interp.alloc_env();
    dst.binding_count = src.binding_count;

    if (src.binding_count <= 4) {
        dst.is_inline = true;
        dst.bindings = &dst.inline_bindings[0];
        dst.capacity = 4;
        return dst;
    }

    dst.is_inline = false;
    dst.bindings = (Binding*)mem::malloc(Binding.sizeof * src.binding_count);
    dst.capacity = src.binding_count;
    return dst;
}

fn void copy_env_copy_bindings(
    Env* src,
    Env* dst,
    Interp* interp,
    PromotionContext* ctx
) {
    for (usz i = 0; i < src.binding_count; i++) {
        dst.bindings[i].name = src.bindings[i].name;
        dst.bindings[i].value = copy_env_binding_value(src.bindings[i].value, interp, ctx);
    }
}

fn bool copy_env_is_terminal_frame(Env* env, Interp* interp, usz depth) @inline {
    if (env == null || env == interp.global_env) return true;
    return depth >= 256;
}

fn Env* copy_env_rewrite_persistent_parent(Env* env, Interp* interp, usz depth, PromotionContext* active_ctx) {
    env.parent = copy_env_to_scope_inner(env.parent, interp, depth + 1, active_ctx);
    return env;
}

fn Env* copy_env_materialize_frame(Env* env, Interp* interp, usz depth, PromotionContext* active_ctx) {
    Env* new_env = copy_env_alloc_frame(env, interp);
    copy_env_copy_bindings(env, new_env, interp, active_ctx);
    new_env.parent = copy_env_to_scope_inner(env.parent, interp, depth + 1, active_ctx);
    if (env.hash_table != null) {
        new_env.build_hash_table();
    }
    main::scope_register_dtor(interp.current_scope, (void*)new_env, &scope_dtor_env);
    return new_env;
}

fn Env* copy_env_to_scope_inner(Env* env, Interp* interp, usz depth, PromotionContext* active_ctx) {
    if (copy_env_is_terminal_frame(env, interp, depth)) {
        if (depth >= 256) return null;
        return env;
    }

    // Persistent env nodes (mutable boxes allocated in root_region) are kept
    // as-is to preserve sharing between JIT stack slots and closure captures.
    // Their parent chain still needs fixing up since it may point to child-scope envs.
    if (env.persistent) {
        return copy_env_rewrite_persistent_parent(env, interp, depth, active_ctx);
    }
    return copy_env_materialize_frame(env, interp, depth, active_ctx);
}

/**
 * Copy an env chain into interp.current_scope (caller must set this to the target scope,
 * typically a closure's env_scope). Uses copy_to_parent for each binding value.
 * Stops at global_env (already in root_region). Skips persistent env nodes (mutable boxes).
 * Replaces deep_copy_env — allocates into any scope, not just root_scope.
 */
fn Env* copy_env_to_scope(Env* env, Interp* interp, usz depth = 0, PromotionContext* ctx = null) {
    PromotionContext* active_ctx = ctx;
    bool owns_ctx = false;
    if (active_ctx == null) active_ctx = interp.active_promotion_ctx;
    if (depth == 0 && active_ctx == null) {
        // Keep boundary context in scope memory (not stack local) so JIT-driven
        // env-copy paths use the same deterministic region lifetime model.
        PromotionContext* local_ctx = (PromotionContext*)interp.current_scope.alloc(PromotionContext.sizeof);
        if (local_ctx == null) return null;
        promotion_context_begin(interp, local_ctx);
        active_ctx = local_ctx;
        owns_ctx = true;
    }
    Env* copied = copy_env_to_scope_inner(env, interp, depth, active_ctx);
    if (owns_ctx) promotion_context_end(interp, active_ctx);
    return copied;
}

// Continuations, effects, index — all handled by JIT (jit.c3)

// =============================================================================
