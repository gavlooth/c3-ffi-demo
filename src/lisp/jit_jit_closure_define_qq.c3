module lisp;

import std::io;
import std::collections::list;
import main;
// =============================================================================
// SECTION 3: JIT HELPER FUNCTIONS (closure/define/quasiquote)
// =============================================================================
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

fn void jit_copy_closure_env_if_needed(Interp* interp, Value* closure, Env* env, main::ScopeRegion* creation_scope, bool need_env_copy) {
    if (!need_env_copy) return;
    main::ScopeRegion* env_scope = main::scope_create(null);
    interp.current_scope = env_scope;
    closure.closure_val.env = boundary_copy_env_to_scope(env, interp);
    closure.closure_val.env_scope = env_scope;
    interp.current_scope = creation_scope;
}

fn void jit_copy_lambda_variadic_info(Interp* interp, Value* closure, Expr* expr) {
    closure.closure_val.param_count = expr.lambda.param_count;
    if (expr.lambda.param_count > 0) {
        closure.closure_val.params = (SymbolId*)interp.current_scope.alloc(SymbolId.sizeof * expr.lambda.param_count);
        for (usz i = 0; i < expr.lambda.param_count; i++) {
            closure.closure_val.params[i] = expr.lambda.params[i];
        }
    }
    closure.closure_val.has_rest = expr.lambda.has_rest;
    closure.closure_val.rest_param = expr.lambda.rest_param;

    if (expr.lambda.has_rest && expr.lambda.param_count == 0) {
        closure.closure_val.has_param = false;
        closure.closure_val.param = 0;
    }
}

fn MethodSignature* jit_build_lambda_type_sig(Interp* interp, Expr* expr) {
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
    return sig;
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
        jit_copy_closure_env_if_needed(interp, closure, env, creation_scope, need_env_copy);
        return closure;
    }

    // Create closure in current_scope
    Value* closure = make_closure(interp, expr.lambda.param, expr.lambda.body, env);

    jit_copy_lambda_variadic_info(interp, closure, expr);

    closure.closure_val.has_typed_params = expr.lambda.has_typed_params;
    closure.closure_val.type_sig = null;
    if (expr.lambda.has_typed_params) {
        closure.closure_val.type_sig = jit_build_lambda_type_sig(interp, expr);
    }

    jit_copy_closure_env_if_needed(interp, closure, env, creation_scope, need_env_copy);

    return closure;
}

// Recursive let helper — called from JIT-compiled code.
// Mirrors the is_recursive branch of eval_let from eval.c3.
// Creates placeholder, extends env, evaluates init, patches closure, evaluates body.
// Destructuring let: evaluate init, match against pattern, bind results in env.
// Called from JIT-compiled code when let_expr.pattern != null.
fn Value* jit_eval_let_destruct(Interp* interp, Expr* expr, Env* env) {
    Value* init_val = jit_eval(expr.let_expr.init, env, interp);
    if (init_val != null && init_val.tag == ERROR) return init_val;

    MatchResult mr = match_pattern(expr.let_expr.pattern, init_val, interp);
    if (!mr.matched) {
        mr.cleanup();
        return raise_error(interp, "let destructuring: pattern did not match value");
    }

    Env* dest_env = env;
    for (usz i = 0; i < mr.binding_count; i++) {
        dest_env = make_env(interp, dest_env);
        env_define_with_barrier(dest_env, mr.bindings[i].name, mr.bindings[i].value, interp);
    }
    mr.cleanup();

    return jit_eval(expr.let_expr.body, dest_env, interp);
}

fn void jit_patch_rec_closure_in_env_scope(Interp* interp, Value* actual_value, SymbolId name) {
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
    interp.current_scope = saved_scope;

    Env* closure_env = actual_value.closure_val.env;
    for (usz i = 0; i < closure_env.binding_count; i++) {
        if ((uint)closure_env.bindings[i].name == (uint)name) {
            closure_env.bindings[i].value = self_ref;
            break;
        }
    }
}

fn void jit_patch_rec_closure(Value* actual_value, Env* rec_env, SymbolId name, Interp* interp) {
    if (!is_closure(actual_value)) return;
    if (actual_value.closure_val.env_scope != null) {
        jit_patch_rec_closure_in_env_scope(interp, actual_value, name);
    } else {
        actual_value.closure_val.env = rec_env;
    }
}

fn void jit_patch_rec_binding(Env* rec_env, SymbolId name, Value* actual_value) {
    for (usz i = 0; i < rec_env.binding_count; i++) {
        if ((uint)rec_env.bindings[i].name == (uint)name) {
            rec_env.bindings[i].value = actual_value;
            break;
        }
    }
}

fn bool jit_letrec_body_has_shift(Value* actual_value, Expr* init_expr) {
    if (!is_closure(actual_value)) return false;
    Expr* lambda_body = init_expr;
    if (lambda_body == null || lambda_body.tag != E_LAMBDA) return false;
    return expr_contains_shift(lambda_body.lambda.body);
}

fn Value* jit_eval_let_rec(Interp* interp, Expr* expr, Env* env) {
    Value* placeholder = make_nil(interp);
    Env* rec_env = env.extend(interp, expr.let_expr.name, placeholder);
    Value* actual_value = jit_eval(expr.let_expr.init, rec_env, interp);
    if (actual_value != null && actual_value.tag == ERROR) return actual_value;

    jit_patch_rec_closure(actual_value, rec_env, expr.let_expr.name, interp);
    jit_patch_rec_binding(rec_env, expr.let_expr.name, actual_value);

    bool body_has_shift = jit_letrec_body_has_shift(actual_value, expr.let_expr.init);
    Value* result = jit_eval_in_call_scope(expr.let_expr.body, rec_env, interp, !body_has_shift);
    return result;
}

// set! helper — called from JIT-compiled code.
// Evaluates to the set value, mirrors E_SET in eval().
fn Value* jit_eval_set(Interp* interp, SymbolId name, Value* value, Env* env) {
    // Env bindings (closures, globals) live in root scope; promote child-lifetime
    // values before storing to avoid dangling pointers.
    Value* stored = boundary_promote_to_root_site(value, interp, COPY_SITE_JIT_EVAL_SET);
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
                            current = &current.instance_val.fields[fi];
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

    // Cons cell mutation
    if (current != null && current.tag == CONS) {
        // Cons payloads are modeled as root/shared values.
        Value* stored = boundary_promote_to_root(value, interp);
        if ((uint)last_seg == (uint)interp.sym_car) {
            current.cons_val.car = stored;
            return stored;
        }
        if ((uint)last_seg == (uint)interp.sym_cdr) {
            current.cons_val.cdr = stored;
            return stored;
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
                        main::ScopeRegion* target = current.instance_val.owner_scope;
                        if (target == null) target = interp.root_scope;
                        Value* stored = boundary_copy_to_scope_site(
                            value,
                            interp,
                            target,
                            interp.current_scope,
                            COPY_SITE_ENV_PROMOTE_FOR_TARGET
                        );
                        if (stored != null) current.instance_val.fields[fi] = *stored;
                        return stored;
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
fn Value* jit_make_method_table_value(Interp* interp, MethodTable* mt) {
    Value* mt_val = boundary_alloc_value_in_root(interp, true);
    mt_val.tag = METHOD_TABLE;
    mt_val.method_table_val = mt;
    return mt_val;
}

fn void jit_method_table_append_entry(MethodTable* mt, Value* impl) {
    if (mt.entry_count >= mt.capacity) {
        usz new_cap = mt.capacity * 2;
        MethodEntry* new_entries = (MethodEntry*)mem::malloc(MethodEntry.sizeof * new_cap);
        for (usz ei = 0; ei < mt.entry_count; ei++) new_entries[ei] = mt.entries[ei];
        mem::free(mt.entries);
        mt.entries = new_entries;
        mt.capacity = new_cap;
    }
    mt.entries[mt.entry_count].sig = *impl.closure_val.type_sig;
    mt.entries[mt.entry_count].implementation = impl;
    mt.entry_count++;
}

fn MethodTable* jit_new_method_table(SymbolId name, Value* impl, Value* fallback) {
    MethodTable* mt = (MethodTable*)mem::malloc(MethodTable.sizeof);
    mt.name = name;
    mt.entries = (MethodEntry*)mem::malloc(MethodEntry.sizeof * METHOD_INITIAL_CAPACITY);
    mt.capacity = METHOD_INITIAL_CAPACITY;
    mt.entry_count = 1;
    mt.fallback = fallback;
    mt.entries[0].sig = *impl.closure_val.type_sig;
    mt.entries[0].implementation = impl;
    return mt;
}

fn Value* jit_eval_define_typed_closure(Interp* interp, SymbolId name, Value* stored_val) {
    Value* existing = interp.global_env.lookup(name);
    if (existing != null && existing.tag == METHOD_TABLE) {
        jit_method_table_append_entry(existing.method_table_val, stored_val);
        return existing;
    }
    Value* fallback = null;
    if (existing != null && (existing.tag == CLOSURE || existing.tag == PRIMITIVE)) {
        fallback = existing;
    }
    MethodTable* mt = jit_new_method_table(name, stored_val, fallback);
    Value* mt_val = jit_make_method_table_value(interp, mt);
    env_define_with_barrier(interp.global_env, name, mt_val, interp);
    return mt_val;
}

<* @require interp != null : "jit_eval_define: null interp"
   @require value != null : "jit_eval_define: null value" *>
fn Value* jit_eval_define(Interp* interp, SymbolId name, Value* value) {
    // Copy to root scope/region for globals (so value survives scope release)
    Value* stored_val = boundary_promote_to_root_site(value, interp, COPY_SITE_JIT_EVAL_DEFINE);

    // Check for typed closure → method table dispatch
    if (stored_val != null && stored_val.tag == CLOSURE && stored_val.closure_val.has_typed_params) {
        return jit_eval_define_typed_closure(interp, name, stored_val);
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
    // Promote initial payload before publishing into persistent root env box.
    Value* promoted = boundary_promote_to_root_site(value, interp, COPY_SITE_JIT_ENV_EXTEND_ROOT);
    Env* box = boundary_env_extend_in_root(interp, env, name, promoted);
    box.persistent = true;
    return box;
}

// JIT-native quasiquote — no eval() dependency.
