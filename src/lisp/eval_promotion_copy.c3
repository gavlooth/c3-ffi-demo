module lisp;

import std::io;
import main;

// =============================================================================
// SECTION 2.4: FRAME PUSH/POP (copy-to-parent core)
// =============================================================================
fn bool in_target_scope_chain(void* ptr, Interp* interp) {
    main::ScopeRegion* s = interp.current_scope;
    while (s != null) {
        if (main::is_in_scope(ptr, s)) return true;
        s = s.parent;
    }
    return false;
}

fn Value* copy_cons_to_parent(Value* v, Interp* interp) {
    // Iterative copy to prevent C stack overflow on long lists.
    Value* head = interp.alloc_value();
    head.tag = CONS;
    Value* current_new = head;
    Value* current_old = v;

    while (true) {
        current_new.cons_val.car = copy_to_parent(current_old.cons_val.car, interp);
        Value* old_cdr = current_old.cons_val.cdr;
        if (old_cdr != null && old_cdr.tag == CONS) {
            if (boundary_can_reuse_value(old_cdr, interp)) {
                // Safe fast-path: tail already lives in a surviving scope chain.
                current_new.cons_val.cdr = old_cdr;
                break;
            }
            // Deep-copy tail iteratively to avoid recursive overflow on long lists.
            Value* next_new = interp.alloc_value();
            next_new.tag = CONS;
            current_new.cons_val.cdr = next_new;
            current_new = next_new;
            current_old = old_cdr;
        } else {
            current_new.cons_val.cdr = copy_to_parent(old_cdr, interp);
            break;
        }
    }
    return head;
}

fn Value* copy_closure_to_parent(Value* v, Interp* interp) {
    bool closure_in_releasing_scope =
        interp.releasing_scope != null &&
        (v.closure_val.scope_gen == interp.releasing_scope.generation ||
         main::scope_contains(interp.releasing_scope, (void*)v.closure_val));
    if (!closure_in_releasing_scope && in_target_scope_chain((void*)v, interp)) {
        return v;
    }

    Value* result = interp.alloc_value();
    result.tag = CLOSURE;
    result.closure_val = (Closure*)interp.current_scope.alloc(Closure.sizeof);
    *result.closure_val = *v.closure_val;
    result.closure_val.scope_gen = interp.current_scope.generation;

    if (v.closure_val.param_count > 0) {
        result.closure_val.params = (SymbolId*)interp.current_scope.alloc(SymbolId.sizeof * v.closure_val.param_count);
        for (usz i = 0; i < v.closure_val.param_count; i++) {
            result.closure_val.params[i] = v.closure_val.params[i];
        }
    } else {
        result.closure_val.params = null;
    }

    if (v.closure_val.type_sig != null) {
        result.closure_val.type_sig = (MethodSignature*)interp.current_scope.alloc(MethodSignature.sizeof);
        *result.closure_val.type_sig = *v.closure_val.type_sig;
    }

    if (result.closure_val.env_scope != null) {
        result.closure_val.env_scope.refcount++;
    }
    main::scope_register_dtor(interp.current_scope, (void*)result, &scope_dtor_closure);
    return result;
}

fn bool needs_wrapper_copy(Value* v, Interp* interp) @inline {
    return !boundary_can_reuse_value(v, interp);
}

fn Value* copy_instance_to_parent(Value* v, Interp* interp) {
    if (!needs_wrapper_copy(v, interp)) return v;
    Value* result = interp.alloc_value();
    result.tag = INSTANCE;
    result.instance_val = v.instance_val;
    instance_retain(v.instance_val);
    main::scope_register_dtor(interp.current_scope, (void*)result, &scope_dtor_value);
    return result;
}

fn Value* copy_ffi_handle_to_parent(Value* v, Interp* interp) {
    if (!needs_wrapper_copy(v, interp)) return v;
    Value* result = interp.alloc_value();
    result.tag = FFI_HANDLE;
    result.ffi_val = v.ffi_val;
    ffi_handle_retain(result.ffi_val);
    main::scope_register_dtor(interp.current_scope, (void*)result, &scope_dtor_value);
    return result;
}

fn bool copy_to_parent_try_fast_reuse(Value* v, Interp* interp, Value** out) {
    if (boundary_can_reuse_value(v, interp)) {
        g_copy_to_parent_stats.fast_reuse_hits++;
        *out = v;
        return true;
    }

    // Not in releasing_scope, but also not in a known surviving scope.
    // Copy defensively to avoid dangling pointers from transient env scopes.
    g_copy_to_parent_stats.defensive_copies++;
    return false;
}

fn Value* copy_partial_prim_to_parent(Value* v, Interp* interp) {
    Value* result = interp.alloc_value();
    result.tag = PARTIAL_PRIM;
    result.partial_val.func = v.partial_val.func;
    result.partial_val.remaining = v.partial_val.remaining;
    result.partial_val.first_arg = copy_to_parent(v.partial_val.first_arg, interp);
    result.partial_val.second_arg = copy_to_parent(v.partial_val.second_arg, interp);
    return result;
}

fn Value* copy_continuation_to_parent(Value* v, Interp* interp) {
    Value* result = interp.alloc_value();
    result.tag = CONTINUATION;
    result.cont_val = v.cont_val;
    return result;
}

fn Value* copy_iterator_to_parent(Value* v, Interp* interp) {
    Value* promoted_thunk = copy_to_parent(v.iterator_val, interp);
    Value* result = interp.alloc_value();
    result.tag = ITERATOR;
    result.iterator_val = promoted_thunk;
    return result;
}

fn Value* copy_coroutine_to_parent(Value* v, Interp* interp) {
    Value* result = interp.alloc_value();
    result.tag = COROUTINE;
    result.coroutine_val = v.coroutine_val;
    return result;
}

fn Value* copy_to_parent(Value* v, Interp* interp) {
    if (v == null) return null;
    g_copy_to_parent_stats.total_calls++;
    copy_to_parent_note_tag(v.tag);

    // Scope-aware optimization: if value is NOT in the scope being released,
    // reuse only when it is already in the target scope chain.
    // Note: uses is_in_scope (chunk walk) rather than scope_gen stamp because
    // chunk-transfer paths (scope_splice_escapes) move allocations without
    // rewriting per-value scope_gen stamps.
    Value* fast_reuse = null;
    if (copy_to_parent_try_fast_reuse(v, interp, &fast_reuse)) return fast_reuse;

    Value* result;
    switch (v.tag) {
        case NIL:
            result = make_nil(interp);
        case INT:
            result = make_int(interp, v.int_val);
        case DOUBLE:
            result = make_double(interp, v.double_val);
        case STRING:
            result = make_string(interp, v.str_chars[:v.str_len]);
        case SYMBOL:
            result = make_symbol(interp, v.sym_val);
        case CONS:
            result = copy_cons_to_parent(v, interp);
        case CLOSURE:
            result = copy_closure_to_parent(v, interp);
        case PRIMITIVE:
            result = v;
        case PARTIAL_PRIM:
            // Deep copy: Value wrapper may be in child scope, and first_arg/second_arg may too.
            result = copy_partial_prim_to_parent(v, interp);
        case CONTINUATION:
            // Shallow copy: Value wrapper may be in child scope, cont_val itself is in root_region.
            result = copy_continuation_to_parent(v, interp);
        case HASHMAP:
        case ARRAY:
        case METHOD_TABLE:
            result = v;  // Value allocated in root_scope; backing data is malloc'd with registered destructors
        case INSTANCE:
            result = copy_instance_to_parent(v, interp);
        case FFI_HANDLE:
            result = copy_ffi_handle_to_parent(v, interp);
        case TYPE_INFO:
            result = v;  // type info lives in registry
        case ITERATOR:
            // Phase 4b: promote underlying closure thunk so iterator outlives child scope.
            result = copy_iterator_to_parent(v, interp);
        case COROUTINE:
            // Shallow copy: wrapper may be in child scope, Coro* is stack-pool managed.
            result = copy_coroutine_to_parent(v, interp);
        case MODULE:
            result = v;  // Module lives in interp.modules array
        case ERROR:
            result = make_error(interp, v.str_chars[:v.str_len]);
        default:
            result = v;
    }
    return result;
}

fn Value* copy_to_parent_site(Value* v, Interp* interp, uint site) {
    if (site < COPY_SITE_COUNT) g_copy_to_parent_stats.site_calls[(usz)site]++;
    return copy_to_parent(v, interp);
}
