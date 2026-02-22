module lisp;

import std::io;
import main;

// =============================================================================
// SECTION 1: GNU LIGHTNING FFI DECLARATIONS
// =============================================================================
//
// GNU Lightning v2 uses _jit_* prefixed functions internally.
// The C macros (jit_prolog, jit_movi, etc.) are wrappers around these.
// We declare the underlying functions directly for C3 FFI.
//
// Opaque types: jit_state_t* and jit_node_t* are void* in our declarations.
// jit_word_t = long on x86_64, jit_int32_t = int, jit_gpr_t = int.

extern fn void init_jit(void* progname);
extern fn void finish_jit();
extern fn void* jit_new_state();
extern fn void _jit_clear_state(void* state);
extern fn void _jit_destroy_state(void* state);
extern fn void* _jit_emit(void* state);
extern fn void _jit_prolog(void* state);
extern fn void _jit_ret(void* state);
extern fn void _jit_epilog(void* state);
extern fn void* _jit_arg(void* state, int code);
extern fn void _jit_getarg_l(void* state, int reg, void* arg_node);
extern fn void _jit_prepare(void* state);
extern fn void _jit_pushargr(void* state, int reg, int code);
extern fn void _jit_pushargi(void* state, long val, int code);
extern fn void* _jit_finishi(void* state, void* fn_ptr);
extern fn void _jit_retval_l(void* state, int reg);
extern fn void _jit_retr(void* state, int reg, int code);
extern fn void _jit_reti(void* state, long val, int code);
extern fn void* _jit_label(void* state);
extern fn void* _jit_forward(void* state);
extern fn void _jit_patch(void* state, void* node);
extern fn void _jit_patch_at(void* state, void* node, void* label);
extern fn void* _jit_new_node_w(void* state, int code, long u);
extern fn void* _jit_new_node_ww(void* state, int code, long u, long v);
extern fn void* _jit_new_node_www(void* state, int code, long u, long v, long w);
extern fn void* _jit_new_node_p(void* state, int code, void* u);
extern fn void* _jit_new_node_pww(void* state, int code, void* u, long v, long w);
extern fn int _jit_allocai(void* state, int size);

// =============================================================================
// SECTION 2: LIGHTNING CONSTANTS
// =============================================================================

// Register IDs (x86_64 Linux)
const int JIT_R0 = 0;   // RAX - scratch / return value
const int JIT_R1 = 1;   // R10 - scratch
const int JIT_R2 = 2;   // R11 - scratch
const int JIT_V0 = 3;   // RBX - callee-saved
const int JIT_V1 = 4;   // R13 - callee-saved
const int JIT_V2 = 5;   // R14 - callee-saved

// jit_code_t enum values we need
const int CODE_ARG_L         = 17;
const int CODE_GETARG_L      = 24;
const int CODE_PUSHARGR_L    = 241;
const int CODE_PUSHARGI_L    = 242;
const int CODE_RETR_L        = 258;
const int CODE_RETI_L        = 259;
const int CODE_RETVAL_L      = 266;
const int CODE_MOVR          = 110;
const int CODE_MOVI          = 111;
const int CODE_ADDR          = 43;
const int CODE_SUBR          = 49;
const int CODE_MULR          = 56;
const int CODE_DIVR          = 62;
const int CODE_BEQI          = 193;
const int CODE_BNEI          = 203;
const int CODE_BLEI          = 189;
const int CODE_BGTI          = 199;
const int CODE_BLTR          = 184;
const int CODE_BGER          = 194;
const int CODE_BGTR          = 198;
const int CODE_BLER          = 188;
const int CODE_BEQR          = 192;
const int CODE_BNER          = 202;
const int CODE_JMPI          = 225;
const int CODE_LIVE          = 1;
const int CODE_LDXI_L        = 167;
const int CODE_STXI_L        = 183;
const int JIT_FP  = 15;

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
    return make_error(interp, "unbound variable");
}

fn Value* jit_apply_value_impl(Value* func, Value* arg, Interp* interp) {
    if (func == null) return make_error(interp, "cannot apply null");

    switch (func.tag) {
        case CLOSURE: {
            if (!func.closure_val.has_param && !func.closure_val.has_rest) {
                return jit_eval(func.closure_val.body, func.closure_val.env, interp);
            }
            if (func.closure_val.has_rest) {
                usz pc = func.closure_val.param_count;
                if (pc == 0) {
                    Env* new_env = func.closure_val.env;
                    Value* rest_list = make_cons(interp, arg, make_nil(interp));
                    new_env = new_env.extend(interp, func.closure_val.rest_param, rest_list);
                    return jit_eval(func.closure_val.body, new_env, interp);
                } else if (pc == 1) {
                    Env* new_env = func.closure_val.env;
                    new_env = new_env.extend(interp, func.closure_val.params[0], arg);
                    new_env = new_env.extend(interp, func.closure_val.rest_param, make_nil(interp));
                    return jit_eval(func.closure_val.body, new_env, interp);
                } else {
                    return make_error(interp, "too few arguments for variadic lambda");
                }
            }
            Env* new_env = func.closure_val.env.extend(interp, func.closure_val.param, arg);
            return jit_eval(func.closure_val.body, new_env, interp);
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
            EvalResult r = jit_apply_continuation(func, arg, interp);
            if (r.error.has_error) return make_error(interp, r.error.message[:256]);
            return r.value;
        }

        case METHOD_TABLE: {
            Value*[1] args = { arg };
            MethodTable* mt = func.method_table_val;
            Value* resolved = find_best_method(mt, args[:1], interp);
            if (resolved == null && mt.fallback != null) {
                resolved = mt.fallback;
            }
            if (resolved == null) {
                return make_error(interp, "no matching method");
            }
            return jit_apply_value(resolved, arg, interp);
        }

        default:
            return make_error(interp, "not a function");
    }
}

fn Value* jit_apply_value(Value* func, Value* arg, Interp* interp) {
    interp.eval_depth++;
    if (interp.eval_depth > interp.max_eval_depth) {
        interp.eval_depth--;
        return make_error(interp, "stack overflow: maximum eval depth exceeded");
    }
    Value* result = jit_apply_value_impl(func, arg, interp);
    interp.eval_depth--;
    return result;
}


fn Value* jit_eval_fallback(Expr* expr, Env* env, Interp* interp) {
    return jit_eval(expr, env, interp);
}

// JIT-aware eval replacement with trampoline TCO.
// Compiles sub-expressions with cache, executes with env context.
// When a tail-call helper sets jit_tco_bounce, loops instead of recursing.
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
                return make_error(interp, "JIT compilation failed");
            }
            jit_cache_store(expr, cached);
        }

        // 3. Execute with env context
        interp.jit_env = env;
        interp.jit_tco_bounce = false;
        Value* result = cached(interp);

        // 4. Check for TCO bounce
        if (interp.jit_tco_bounce) {
            // Tail call — loop with new expr/env instead of recursing
            expr = interp.jit_tco_expr;
            env = interp.jit_tco_env;
            interp.jit_tco_bounce = false;
            continue;
        }

        interp.jit_env = saved_env;
        return result;
    }
}

// Adapter: converts jit_eval's Value* return to EvalResult.
// Used to mechanically replace eval() calls in EvalResult-returning functions.
fn EvalResult jit_eval_to_result(Expr* expr, Env* env, Interp* interp) {
    Value* v = jit_eval(expr, env, interp);
    if (v != null && v.tag == ERROR) {
        return eval_error(v.str_val.chars[:v.str_val.len]);
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
    return make_error(interp, "unbound variable");
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
    while (interp.jit_tco_bounce) {
        interp.jit_tco_bounce = false;
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
fn Value* jit_make_closure_from_expr(Interp* interp, Expr* expr, Env* env) {
    // Allocate closures in root_region so env pointers remain valid
    main::RegionHandle saved = interp.current_frame;
    interp.current_frame = interp.root_region;

    // Check for zero-arg lambda (sentinel param)
    if ((uint)expr.lambda.param == 0xFFFFFFFF && !expr.lambda.has_rest) {
        Value* closure = make_closure_no_param(interp, expr.lambda.body, env);
        if (saved.region_id != interp.root_region.region_id) {
            closure.closure_val.env = deep_copy_env(env, interp);
        }
        interp.current_frame = saved;
        return closure;
    }

    // Create a closure capturing the environment
    Value* closure = make_closure(interp, expr.lambda.param, expr.lambda.body, env);

    // Copy variadic info from ExprLambda to Closure
    if (expr.lambda.param_count > 64) return null;
    closure.closure_val.param_count = expr.lambda.param_count;
    for (usz i = 0; i < expr.lambda.param_count; i++) {
        closure.closure_val.params[i] = expr.lambda.params[i];
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
        MethodSignature* sig = (MethodSignature*)mem::malloc(MethodSignature.sizeof);
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

    // Promote captured env to root_region if in temp frame
    if (saved.region_id != interp.root_region.region_id) {
        closure.closure_val.env = deep_copy_env(env, interp);
    }

    interp.current_frame = saved;
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

    // 4. Patch: if closure, set its env to rec_env for self-reference
    if (is_closure(actual_value)) {
        actual_value.closure_val.env = rec_env;
    }

    // Update placeholder binding in rec_env
    for (usz i = 0; i < rec_env.binding_count; i++) {
        if ((uint)rec_env.bindings[i].name == (uint)expr.let_expr.name) {
            rec_env.bindings[i].value = actual_value;
            break;
        }
    }

    // 5. JIT-compile body in extended env
    return jit_eval(expr.let_expr.body, rec_env, interp);
}

// set! helper — called from JIT-compiled code.
// Evaluates to the set value, mirrors E_SET in eval().
fn Value* jit_eval_set(Interp* interp, SymbolId name, Value* value, Env* env) {
    // Try local env first, then global
    if (!env.set(name, value)) {
        if (interp.global_env == null || !interp.global_env.set(name, value)) {
            return make_error(interp, "set!: unbound variable");
        }
    }
    return value;
}

// set! dot-path helper — handles (set! obj.field value) mutations.
// Mirrors the dot-path E_SET logic from eval.c3 lines 496-561.
fn Value* jit_eval_set_path(Interp* interp, Expr* expr, Value* value, Env* env) {
    // Resolve root object
    Value* current = env.lookup(expr.set_expr.path_segments[0]);
    if (current == null) current = interp.global_env.lookup(expr.set_expr.path_segments[0]);
    if (current == null) return make_error(interp, "set!: unbound path root");

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
            if (!found) return make_error(interp, "set!: field not found in path");
        } else if (current != null && current.tag == CONS) {
            SymbolId seg = expr.set_expr.path_segments[si];
            if ((uint)seg == (uint)interp.sym_car) { current = current.cons_val.car; }
            else if ((uint)seg == (uint)interp.sym_cdr) { current = current.cons_val.cdr; }
            else { return make_error(interp, "set!: cons only supports .car and .cdr"); }
        } else {
            return make_error(interp, "set!: path segment is not an instance or cons");
        }
    }

    // Mutate the last field
    SymbolId last_seg = expr.set_expr.path_segments[expr.set_expr.path_segment_count - 1];

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
        return make_error(interp, "set!: cons only supports .car and .cdr");
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
        return make_error(interp, "set!: field not found");
    }
    return make_error(interp, "set!: target is not an instance or cons");
}

// define helper — called from JIT-compiled code.
// Full define with typed dispatch support, mirrors eval_define from eval.c3.
fn Value* jit_eval_define(Interp* interp, SymbolId name, Value* value) {
    // Copy to root_region for globals (so value survives temp frame release)
    Value* stored_val = value;
    if (interp.current_frame.region_id != interp.root_region.region_id) {
        stored_val = copy_to_parent(value, interp, interp.root_region);
    }

    // Check for typed closure → method table dispatch
    if (stored_val != null && stored_val.tag == CLOSURE && stored_val.closure_val.has_typed_params) {
        Value* existing = interp.global_env.lookup(name);
        if (existing != null && existing.tag == METHOD_TABLE) {
            MethodTable* mt = existing.method_table_val;
            if (mt.entry_count < MAX_METHODS) {
                mt.entries[mt.entry_count].sig = *stored_val.closure_val.type_sig;
                mt.entries[mt.entry_count].implementation = stored_val;
                mt.entry_count++;
            }
            return existing;
        } else if (existing != null && existing.tag == CLOSURE) {
            MethodTable* mt = (MethodTable*)mem::malloc(MethodTable.sizeof);
            mt.name = name;
            mt.entry_count = 0;
            mt.fallback = existing;
            mt.entries[0].sig = *stored_val.closure_val.type_sig;
            mt.entries[0].implementation = stored_val;
            mt.entry_count = 1;
            main::RegionHandle saved_mt = interp.current_frame;
            interp.current_frame = interp.root_region;
            Value* mt_val = interp.alloc_value();
            interp.current_frame = saved_mt;
            mt_val.tag = METHOD_TABLE;
            mt_val.method_table_val = mt;
            interp.global_env.define(name, mt_val);
            return mt_val;
        } else if (existing != null && existing.tag == PRIMITIVE) {
            MethodTable* mt = (MethodTable*)mem::malloc(MethodTable.sizeof);
            mt.name = name;
            mt.entry_count = 0;
            mt.fallback = existing;
            mt.entries[0].sig = *stored_val.closure_val.type_sig;
            mt.entries[0].implementation = stored_val;
            mt.entry_count = 1;
            main::RegionHandle saved_mt = interp.current_frame;
            interp.current_frame = interp.root_region;
            Value* mt_val = interp.alloc_value();
            interp.current_frame = saved_mt;
            mt_val.tag = METHOD_TABLE;
            mt_val.method_table_val = mt;
            interp.global_env.define(name, mt_val);
            return mt_val;
        } else {
            MethodTable* mt = (MethodTable*)mem::malloc(MethodTable.sizeof);
            mt.name = name;
            mt.entries[0].sig = *stored_val.closure_val.type_sig;
            mt.entries[0].implementation = stored_val;
            mt.entry_count = 1;
            mt.fallback = null;
            main::RegionHandle saved_mt = interp.current_frame;
            interp.current_frame = interp.root_region;
            Value* mt_val = interp.alloc_value();
            interp.current_frame = saved_mt;
            mt_val.tag = METHOD_TABLE;
            mt_val.method_table_val = mt;
            interp.global_env.define(name, mt_val);
            return mt_val;
        }
    }

    // Check if we're adding an untyped fallback to an existing method table
    Value* existing_for_fallback = interp.global_env.lookup(name);
    if (existing_for_fallback != null && existing_for_fallback.tag == METHOD_TABLE) {
        existing_for_fallback.method_table_val.fallback = stored_val;
        return existing_for_fallback;
    }

    // Normal (untyped) define
    interp.global_env.define(name, stored_val);
    return stored_val;
}

// Environment extension helper — called from JIT-compiled code.
// Extends an env with a new binding for capturing let-locals in lambdas.
fn Env* jit_env_extend(Interp* interp, Env* env, SymbolId name, Value* value) {
    return env.extend(interp, name, value);
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
    EvalResult r = jit_shift_impl(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn Value* jit_exec_handle(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = jit_handle_impl(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn Value* jit_exec_perform(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = jit_perform_impl(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn EvalResult jit_reset_impl(Expr* expr, Env* env, Interp* interp) {
    usz saved_shift_counter = interp.shift_counter;
    interp.shift_counter = 0;

    if (interp.reset_depth >= 16) return eval_error("reset stack overflow: too many nested reset expressions");
    interp.reset_depth++;

    interp.reset_body_stack[interp.reset_depth - 1] = expr.reset.body;
    interp.reset_env_stack[interp.reset_depth - 1] = env;

    Value* v = jit_eval(expr.reset.body, env, interp);
    EvalResult result;
    if (v != null && v.tag == ERROR) {
        result = eval_error(v.str_val.chars[:v.str_val.len]);
    } else {
        result = eval_ok(v);
    }

    interp.shift_counter = saved_shift_counter;
    interp.reset_depth--;

    if (result.error.has_error) {
        interp.shift_occurred = false;
        return result;
    }

    if (interp.shift_occurred && interp.reset_depth == interp.shift_target_depth) {
        interp.shift_occurred = false;
        return eval_ok(interp.shift_result);
    }

    return result;
}

fn EvalResult jit_shift_impl(Expr* expr, Env* env, Interp* interp) {
    // REPLAY CHECK
    if (interp.cont_substituting && !interp.cont_is_effect) {
        usz current_index = interp.shift_counter;
        interp.shift_counter++;
        if (current_index < interp.cont_target_shift_index) {
            return eval_ok(interp.shift_prior_results[current_index]);
        }
        if (current_index == interp.cont_target_shift_index) {
            interp.cont_substituting = false;
            if (current_index < 16) {
                interp.shift_prior_results[current_index] = interp.cont_substitution;
            }
            return eval_ok(interp.cont_substitution);
        }
    }

    if (interp.reset_depth == 0) {
        return eval_error("shift outside of reset");
    }

    CapturedCont cc_init;
    main::ObjectHandle cc_handle = main::allocate_in(interp.root_region, CapturedCont, cc_init);
    CapturedCont* cc = main::dereference_as(CapturedCont, cc_handle);
    cc.reset_body = interp.reset_body_stack[interp.reset_depth - 1];
    cc.reset_env = interp.reset_env_stack[interp.reset_depth - 1];
    cc.is_effect = false;
    cc.has_handler = false;
    cc.shift_index = interp.shift_counter;
    cc.shift_prior_count = interp.shift_counter;
    for (usz si = 0; si < cc.shift_prior_count && si < 16; si++) {
        cc.shift_prior_results[si] = interp.shift_prior_results[si];
    }
    interp.shift_counter++;

    Continuation* k = interp.alloc_lisp_continuation();
    k.data = cc;

    Value* k_val = make_continuation(interp, k);

    Env* shift_env = env.extend(interp, expr.shift.k_name, k_val);

    Value* body_v = jit_eval(expr.shift.body, shift_env, interp);
    EvalResult body_result;
    if (body_v != null && body_v.tag == ERROR) {
        body_result = eval_error(body_v.str_val.chars[:body_v.str_val.len]);
    } else {
        body_result = eval_ok(body_v);
    }

    if (body_result.error.has_error) {
        return body_result;
    }

    interp.shift_occurred = true;
    interp.shift_target_depth = interp.reset_depth - 1;
    interp.shift_result = body_result.value;

    return body_result;
}

fn EvalResult jit_handle_impl(Expr* expr, Env* env, Interp* interp) {
    if (interp.handler_count >= 16) return eval_error("handler stack overflow: too many nested handlers");

    EffectHandler* h = &interp.handler_stack[interp.handler_count];
    if (expr.handle.clause_count > MAX_EFFECT_CLAUSES) {
        return eval_error("too many effect handler clauses (max 8)");
    }
    h.clause_count = expr.handle.clause_count;
    h.handler_env = env;
    h.body_expr = expr.handle.body;
    h.body_env = env;

    for (usz i = 0; i < expr.handle.clause_count; i++) {
        h.tags[i] = expr.handle.clauses[i].effect_tag;
        h.clauses[i] = expr.handle.clauses[i];
    }

    usz saved_perform_counter = interp.effect_perform_counter;
    interp.effect_perform_counter = 0;

    interp.handler_count++;

    Value* v = jit_eval(expr.handle.body, env, interp);
    EvalResult result;
    if (v != null && v.tag == ERROR) {
        result = eval_error(v.str_val.chars[:v.str_val.len]);
    } else {
        result = eval_ok(v);
    }

    interp.effect_perform_counter = saved_perform_counter;

    if (interp.effect_occurred) {
        interp.effect_occurred = false;
        return eval_ok(interp.effect_result);
    }

    interp.handler_count--;

    return result;
}

fn EvalResult jit_perform_impl(Expr* expr, Env* env, Interp* interp) {
    // Evaluate the argument via JIT
    Value* arg_v = jit_eval(expr.perform.arg, env, interp);
    EvalResult arg_result;
    if (arg_v != null && arg_v.tag == ERROR) {
        arg_result = eval_error(arg_v.str_val.chars[:arg_v.str_val.len]);
    } else {
        arg_result = eval_ok(arg_v);
    }
    if (arg_result.error.has_error) {
        return arg_result;
    }

    SymbolId tag = expr.perform.tag;

    // Check effect declaration for type safety
    TypeId effect_tid = interp.types.lookup(tag, &interp.symbols);
    if (effect_tid != INVALID_TYPE_ID) {
        TypeInfo* effect_info = interp.types.get(effect_tid);
        if (effect_info != null && effect_info.kind == TK_EFFECT && effect_info.field_count > 0) {
            TypeId expected_tid = effect_info.fields[0].field_type;
            if (expected_tid != INVALID_TYPE_ID && expected_tid != interp.tid_Any) {
                TypeId actual_tid = infer_value_type(arg_result.value, interp);
                if (actual_tid != INVALID_TYPE_ID && !interp.types.is_subtype(actual_tid, expected_tid)) {
                    return eval_error("perform: type mismatch for effect");
                }
            }
        }
    }

    // REPLAY CHECK
    if (interp.cont_substituting && interp.cont_is_effect &&
        (uint)interp.cont_effect_tag == (uint)tag) {
        usz current_index = interp.effect_perform_counter;
        interp.effect_perform_counter++;
        if (current_index < interp.cont_target_perform_index) {
            return eval_ok(interp.effect_prior_results[current_index]);
        }
        if (current_index == interp.cont_target_perform_index) {
            interp.cont_substituting = false;
            if (current_index < 16) {
                interp.effect_prior_results[current_index] = interp.cont_substitution;
            }
            return eval_ok(interp.cont_substitution);
        }
    }

    // Search handler stack for a matching handler
    for (isz i = (isz)interp.handler_count - 1; i >= 0; i--) {
        EffectHandler* h = &interp.handler_stack[(usz)i];

        for (usz j = 0; j < h.clause_count; j++) {
            if ((uint)h.clauses[j].effect_tag == (uint)tag) {
                EffectClause* clause = &h.clauses[j];

                CapturedCont cc_init;
                main::ObjectHandle cc_handle = main::allocate_in(interp.root_region, CapturedCont, cc_init);
                CapturedCont* cc = main::dereference_as(CapturedCont, cc_handle);
                cc.reset_body = h.body_expr;
                cc.reset_env = h.body_env;
                cc.is_effect = true;
                cc.effect_tag = tag;
                cc.handler_copy = *h;
                cc.has_handler = true;
                cc.perform_index = interp.effect_perform_counter;
                cc.prior_count = interp.effect_perform_counter;
                for (usz pi = 0; pi < cc.prior_count && pi < 16; pi++) {
                    cc.prior_results[pi] = interp.effect_prior_results[pi];
                }
                interp.effect_perform_counter++;

                Continuation* k = interp.alloc_lisp_continuation();
                k.data = cc;
                Value* k_val = make_continuation(interp, k);

                Env* clause_env = h.handler_env.extend(interp, clause.k_name, k_val);
                clause_env = clause_env.extend(interp, clause.arg_name, arg_result.value);

                interp.handler_count--;

                // Evaluate handler body via JIT
                Value* handler_v = jit_eval(clause.handler_body, clause_env, interp);
                EvalResult result;
                if (handler_v != null && handler_v.tag == ERROR) {
                    result = eval_error(handler_v.str_val.chars[:handler_v.str_val.len]);
                } else {
                    result = eval_ok(handler_v);
                }
                if (!result.error.has_error) {
                    interp.effect_occurred = true;
                    interp.effect_result = result.value;
                }
                return result;
            }
        }
    }

    // I/O fast path: no handler found for io/* effects → use cached primitives
    if ((uint)tag == (uint)interp.sym_io_print) {
        if (interp.raw_print != null) return apply_primitive(interp.raw_print, arg_result.value, interp);
    } else if ((uint)tag == (uint)interp.sym_io_println) {
        if (interp.raw_println != null) return apply_primitive(interp.raw_println, arg_result.value, interp);
    } else if ((uint)tag == (uint)interp.sym_io_display) {
        if (interp.raw_display != null) return apply_primitive(interp.raw_display, arg_result.value, interp);
    } else if ((uint)tag == (uint)interp.sym_io_newline) {
        if (interp.raw_newline != null) return apply_primitive(interp.raw_newline, make_nil(interp), interp);
    } else if ((uint)tag == (uint)interp.sym_io_read_file) {
        if (interp.raw_read_file != null) return apply_primitive(interp.raw_read_file, arg_result.value, interp);
    } else if ((uint)tag == (uint)interp.sym_io_write_file) {
        if (interp.raw_write_file != null && arg_result.value != null && is_cons(arg_result.value)) {
            EvalResult partial_result = apply_primitive(interp.raw_write_file, car(arg_result.value), interp);
            if (partial_result.error.has_error) return partial_result;
            // Use jit_apply_value instead of apply()
            Value* write_result = jit_apply_value(partial_result.value, cdr(arg_result.value), interp);
            if (write_result != null && write_result.tag == ERROR) {
                return eval_error(write_result.str_val.chars[:write_result.str_val.len]);
            }
            return eval_ok(write_result);
        }
    } else if ((uint)tag == (uint)interp.sym_io_file_exists) {
        if (interp.raw_file_exists != null) return apply_primitive(interp.raw_file_exists, arg_result.value, interp);
    } else if ((uint)tag == (uint)interp.sym_io_read_lines) {
        if (interp.raw_read_lines != null) return apply_primitive(interp.raw_read_lines, arg_result.value, interp);
    }

    return eval_error("unhandled effect");
}

fn EvalResult jit_apply_continuation(Value* k_val, Value* arg, Interp* interp) {
    if (k_val.cont_val == null) {
        return eval_error("cannot resume continuation: null continuation");
    }

    CapturedCont* cc = (CapturedCont*)k_val.cont_val.data;
    if (cc == null) {
        return eval_error("cannot resume continuation: no captured state");
    }

    interp.cont_substitution = arg;
    interp.cont_substituting = true;
    interp.cont_is_effect = cc.is_effect;
    if (cc.is_effect) {
        interp.cont_effect_tag = cc.effect_tag;
    }

    EvalResult result;

    if (cc.is_effect && cc.has_handler) {
        if (interp.handler_count >= 16) return eval_error("handler stack overflow: too many nested handlers during continuation resume");
        interp.handler_stack[interp.handler_count] = cc.handler_copy;
        interp.handler_stack[interp.handler_count].body_expr = cc.reset_body;
        interp.handler_stack[interp.handler_count].body_env = cc.reset_env;
        interp.handler_count++;

        interp.cont_target_perform_index = cc.perform_index;
        interp.effect_perform_counter = 0;
        for (usz i = 0; i < cc.prior_count && i < 16; i++) {
            interp.effect_prior_results[i] = cc.prior_results[i];
        }

        Value* v = jit_eval(cc.reset_body, cc.reset_env, interp);
        if (v != null && v.tag == ERROR) {
            result = eval_error(v.str_val.chars[:v.str_val.len]);
        } else {
            result = eval_ok(v);
        }

        if (interp.effect_occurred) {
            interp.effect_occurred = false;
            result = eval_ok(interp.effect_result);
        } else {
            interp.handler_count--;
        }
    } else {
        interp.cont_target_shift_index = cc.shift_index;
        interp.shift_counter = 0;
        for (usz i = 0; i < cc.shift_prior_count && i < 16; i++) {
            interp.shift_prior_results[i] = cc.shift_prior_results[i];
        }

        Value* v = jit_eval(cc.reset_body, cc.reset_env, interp);
        if (v != null && v.tag == ERROR) {
            result = eval_error(v.str_val.chars[:v.str_val.len]);
        } else {
            result = eval_ok(v);
        }
    }

    interp.cont_substituting = false;

    return result;
}

// Index helper — performs collection indexing without going through eval dispatch.
// Takes already-evaluated collection and index values.
fn Value* jit_do_index(Interp* interp, Value* collection, Value* index) {
    if (collection == null) return make_error(interp, "index: nil collection");
    // List/cons indexing
    if (is_cons(collection) && is_int(index)) {
        long idx = index.int_val;
        if (idx < 0) return make_error(interp, "negative list index");
        Value* current = collection;
        for (long i = 0; i < idx; i++) {
            if (!is_cons(current)) return make_error(interp, "list index out of bounds");
            current = cdr(current);
        }
        if (!is_cons(current)) return make_error(interp, "list index out of bounds");
        return car(current);
    }
    // String indexing
    if (is_string(collection) && is_int(index)) {
        long idx = index.int_val;
        if (idx < 0 || (usz)idx >= collection.str_val.len) {
            return make_error(interp, "string index out of bounds");
        }
        return make_int(interp, (long)collection.str_val.chars[(usz)idx]);
    }
    // Array indexing
    if (collection.tag == ARRAY && is_int(index)) {
        long idx = index.int_val;
        if (collection.array_val == null || idx < 0 || (usz)idx >= collection.array_val.length) {
            return make_error(interp, "array index out of bounds");
        }
        return collection.array_val.items[(usz)idx];
    }
    // Dict indexing
    if (collection.tag == HASHMAP) {
        Value* result = hashmap_get(collection.hashmap_val, index);
        if (result == null) return make_nil(interp);
        return result;
    }
    return make_error(interp, "index: unsupported collection type");
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
    for (usz i = 0; i < expr.match.clause_count; i++) {
        MatchClause* clause = &expr.match.clauses[i];
        MatchResult match_r = match_pattern(clause.pattern, scrutinee, interp);
        if (match_r.matched) {
            Env* clause_env = make_env(interp, env);
            for (usz j = 0; j < match_r.binding_count; j++) {
                clause_env.define(match_r.bindings[j].name, match_r.bindings[j].value);
            }
            // TCO bounce — jit_eval() will compile and execute the clause body
            interp.jit_tco_expr = clause.result;
            interp.jit_tco_env = clause_env;
            interp.jit_tco_bounce = true;
            return jit_tco_sentinel();
        }
    }
    return make_error(interp, "no pattern matched in match expression");
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

    if (find_module(name, interp) != null) {
        return eval_error("module already defined");
    }

    if (interp.module_count >= 32) {
        return eval_error("module table exhausted (max 32 modules)");
    }
    Module* mod = &interp.modules[interp.module_count];
    mod.name = name;
    mod.loaded = false;
    mod.path_len = 0;
    mod.export_count = expr.module_expr.export_count;
    for (usz i = 0; i < expr.module_expr.export_count; i++) {
        mod.exports[i] = expr.module_expr.exports[i];
    }

    Env* mod_env = make_env(interp, interp.global_env);
    mod.env = mod_env;
    interp.module_count++;

    usz mod_idx = interp.module_count - 1;
    usz mh_slot = (usz)name % MODULE_HASH_SIZE;
    while (interp.module_hash_index[mh_slot] != usz.max) {
        mh_slot = (mh_slot + 1) % MODULE_HASH_SIZE;
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

fn EvalResult jit_load_module_from_file(char[] path, SymbolId name, Interp* interp) {
    if (try content = io::file::load_temp((String)path)) {
        char[] source = content;

        Lexer lex;
        lex.init(source);
        Parser p;
        p.init(&lex, interp);

        Expr*[64] exprs;
        usz expr_count = 0;
        while (!lex.at_end()) {
            if (expr_count >= 64) return eval_error("module: too many expressions (max 64)");
            exprs[expr_count] = p.parse_expr();
            expr_count++;
        }

        if (expr_count > 0 && exprs[0].tag == E_MODULE) {
            return jit_eval_to_result(exprs[0], interp.global_env, interp);
        }

        if (interp.module_count >= 32) {
            return eval_error("module table exhausted (max 32 modules)");
        }
        Module* mod = &interp.modules[interp.module_count];
        mod.name = name;
        mod.loaded = false;
        for (usz i = 0; i < path.len && i < 255; i++) {
            mod.path[i] = path[i];
        }
        mod.path_len = path.len;
        mod.export_count = 0;

        Env* mod_env = make_env(interp, interp.global_env);
        mod.env = mod_env;
        interp.module_count++;

        usz mod_idx2 = interp.module_count - 1;
        usz mh_slot2 = (usz)name % MODULE_HASH_SIZE;
        while (interp.module_hash_index[mh_slot2] != usz.max) {
            mh_slot2 = (mh_slot2 + 1) % MODULE_HASH_SIZE;
        }
        interp.module_hash_index[mh_slot2] = mod_idx2;

        Env* saved_global = interp.global_env;
        interp.global_env = mod_env;

        for (usz i = 0; i < expr_count; i++) {
            EvalResult r = jit_eval_to_result(exprs[i], mod_env, interp);
            if (r.error.has_error) {
                interp.global_env = saved_global;
                return r;
            }
            if (exprs[i].tag == E_DEFINE && mod.export_count < 128) {
                mod.exports[mod.export_count] = exprs[i].define.name;
                mod.export_count++;
            }
        }

        interp.global_env = saved_global;
        mod.loaded = true;
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
        char[] path = expr.import_expr.path[:expr.import_expr.path_len];
        EvalResult load_result = jit_load_module_from_file(path, name, interp);
        if (load_result.error.has_error) return load_result;
        mod = find_module(name, interp);
    } else {
        char[] sym_name = interp.symbols.get_name(name);
        if (4 + sym_name.len + 5 > 511) {
            return eval_error("module path too long");
        }
        char[512] path_buf;
        usz plen = 0;
        path_buf[0] = 'l'; path_buf[1] = 'i'; path_buf[2] = 'b'; path_buf[3] = '/';
        plen = 4;
        for (usz i = 0; i < sym_name.len; i++) {
            path_buf[plen] = sym_name[i];
            plen++;
        }
        path_buf[plen] = '.'; plen++;
        path_buf[plen] = 'p'; plen++;
        path_buf[plen] = 'i'; plen++;
        path_buf[plen] = 'k'; plen++;
        path_buf[plen] = 'a'; plen++;

        char[] path = path_buf[:plen];
        EvalResult load_result = jit_load_module_from_file(path, name, interp);
        if (load_result.error.has_error) return load_result;
        mod = find_module(name, interp);
    }

    if (mod == null) {
        return eval_error("module not found");
    }

    for (usz i = 0; i < mod.export_count; i++) {
        Value* val = mod.env.lookup(mod.exports[i]);
        if (val != null) {
            env.define(mod.exports[i], val);
        }
    }

    return eval_ok(make_nil(interp));
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
    if (func == null) return make_error(interp, "cannot apply null");

    // Zero-arg non-variadic closure: (lambda () body)
    if (arg_count == 0 && func.tag == CLOSURE && !func.closure_val.has_param && !func.closure_val.has_rest) {
        return jit_eval(func.closure_val.body, func.closure_val.env, interp);
    }

    // Direct variadic closure path
    if (func.tag == CLOSURE && func.closure_val.has_rest) {
        usz pc = func.closure_val.param_count;
        if (arg_count < pc) return make_error(interp, "too few arguments for variadic lambda");
        Env* new_env = func.closure_val.env;
        Value* curr = arg_list;
        for (usz i = 0; i < pc; i++) {
            if (curr == null || curr.tag != CONS) return make_error(interp, "arg list too short");
            new_env = new_env.extend(interp, func.closure_val.params[i], curr.cons_val.car);
            curr = curr.cons_val.cdr;
        }
        new_env = new_env.extend(interp, func.closure_val.rest_param, curr);
        return jit_eval(func.closure_val.body, new_env, interp);
    }

    // Direct primitive path for multi-arg primitives
    if (func.tag == PRIMITIVE) {
        // Set constructor type_id (needed for type constructor primitives)
        interp.constructor_type_id = func.prim_val.tag;
        Value*[16] args;
        Value* curr = arg_list;
        for (usz i = 0; i < arg_count && i < 16; i++) {
            if (curr == null || curr.tag != CONS) break;
            args[i] = curr.cons_val.car;
            curr = curr.cons_val.cdr;
        }
        usz safe_count = arg_count > 16 ? 16 : arg_count;
        Value* result = func.prim_val.func(args[:safe_count], null, interp);
        if (is_error(result)) return make_error(interp, result.str_val.chars[:result.str_val.len]);
        return result;
    }

    // Non-variadic multi-param closure: direct binding
    if (func.tag == CLOSURE) {
        usz pc = func.closure_val.param_count;
        if (arg_count != pc) return make_error(interp, "arity mismatch");
        Env* call_env = make_env(interp, func.closure_val.env);
        Value* curr = arg_list;
        for (usz i = 0; i < pc; i++) {
            if (curr == null || curr.tag != CONS) return make_error(interp, "arg list too short");
            call_env.define(func.closure_val.params[i], curr.cons_val.car);
            curr = curr.cons_val.cdr;
        }
        return jit_eval(func.closure_val.body, call_env, interp);
    }

    // METHOD_TABLE: dispatch with all args at once
    if (func.tag == METHOD_TABLE) {
        Value*[16] args;
        Value* curr = arg_list;
        for (usz i = 0; i < arg_count && i < 16; i++) {
            if (curr == null || curr.tag != CONS) break;
            args[i] = curr.cons_val.car;
            curr = curr.cons_val.cdr;
        }
        usz safe_count = arg_count > 16 ? 16 : arg_count;
        MethodTable* mt = func.method_table_val;
        Value* resolved = find_best_method(mt, args[:safe_count], interp);
        if (resolved == null && mt.fallback != null) {
            resolved = mt.fallback;
        }
        if (resolved == null) {
            return make_error(interp, "no matching method");
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
    return make_error(interp, "+: expected numbers");
}

fn Value* jit_prim_sub(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null) {
        if (is_double(a) || is_double(b)) {
            if (is_number(a) && is_number(b)) return make_double(interp, to_double(a) - to_double(b));
        }
        if (a.tag == INT && b.tag == INT) return make_int(interp, a.int_val - b.int_val);
    }
    return make_error(interp, "-: expected numbers");
}

fn Value* jit_prim_mul(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null) {
        if (is_double(a) || is_double(b)) {
            if (is_number(a) && is_number(b)) return make_double(interp, to_double(a) * to_double(b));
        }
        if (a.tag == INT && b.tag == INT) return make_int(interp, a.int_val * b.int_val);
    }
    return make_error(interp, "*: expected numbers");
}

fn Value* jit_prim_lt(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null && is_number(a) && is_number(b)) {
        return to_double(a) < to_double(b) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
    }
    return make_error(interp, "<: expected numbers");
}

fn Value* jit_prim_gt(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null && is_number(a) && is_number(b)) {
        return to_double(a) > to_double(b) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
    }
    return make_error(interp, ">: expected numbers");
}

fn Value* jit_prim_eq(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null) {
        bool eq = values_equal(a, b);
        return eq ? make_symbol(interp, interp.sym_true) : make_nil(interp);
    }
    return make_nil(interp);
}

// =============================================================================
// SECTION 4: JIT COMPILER
// =============================================================================

struct JitCompiler {
    void*    state;       // jit_state_t*
    Interp*  interp;
    bool     initialized;
}

// Local variable table for JIT-compiled let bindings.
// Maps SymbolId -> stack frame offset (from JIT_FP).
struct JitLocal {
    SymbolId name;
    int stack_offset;  // offset from JIT_FP, returned by _jit_allocai
    bool is_mutable;   // true if closure-captured-and-mutated, uses env-based boxing
}

struct JitLocals {
    JitLocal[32] locals;
    usz count;
}

// Global JIT initialization flag
bool g_jit_initialized = false;

// JIT state cleanup tracking (C9: increased from 64 to 256, then to 4096)
const usz JIT_STATE_POOL_SIZE = 4096;
void*[4096] g_jit_states;
usz g_jit_state_count = 0;
bool g_jit_pool_warned = false;

// JIT compilation cache: Expr* → JitFn (open-addressing hash on pointer value)
struct JitCacheEntry {
    Expr* expr;     // cache key (pointer identity)
    JitFn compiled; // compiled function
}

const usz JIT_CACHE_SIZE = 1024;
JitCacheEntry[JIT_CACHE_SIZE] g_jit_cache;
usz g_jit_cache_count = 0;

fn JitFn jit_cache_lookup(Expr* expr) {
    usz hash = (usz)expr;
    usz idx = hash % JIT_CACHE_SIZE;
    for (usz probe = 0; probe < 16; probe++) {
        usz slot = (idx + probe) % JIT_CACHE_SIZE;
        if (g_jit_cache[slot].expr == expr) return g_jit_cache[slot].compiled;
        if (g_jit_cache[slot].expr == null) return null;
    }
    return null;
}

fn void jit_cache_store(Expr* expr, JitFn compiled) {
    if (g_jit_cache_count >= JIT_CACHE_SIZE - 64) return;  // leave headroom
    usz hash = (usz)expr;
    usz idx = hash % JIT_CACHE_SIZE;
    for (usz probe = 0; probe < 16; probe++) {
        usz slot = (idx + probe) % JIT_CACHE_SIZE;
        if (g_jit_cache[slot].expr == null || g_jit_cache[slot].expr == expr) {
            g_jit_cache[slot].expr = expr;
            g_jit_cache[slot].compiled = compiled;
            g_jit_cache_count++;
            return;
        }
    }
}

fn void jit_global_init() {
    if (!g_jit_initialized) {
        init_jit(null);
        g_jit_initialized = true;
    }
}

fn void jit_global_shutdown() {
    if (g_jit_initialized) {
        // Destroy all tracked JIT states
        for (usz i = 0; i < g_jit_state_count; i++) {
            _jit_destroy_state(g_jit_states[i]);
        }
        g_jit_state_count = 0;
        finish_jit();
        g_jit_initialized = false;
    }
}

/**
 * Compiled function type: takes Interp*, returns Value*
 */
alias JitFn = fn Value*(Interp*);

/**
 * Compile an expression to a native function.
 *
 * Returns null if compilation fails.
 * The compiled function signature is: Value* fn(Interp*)
 */
fn JitFn jit_compile(Expr* expr, Interp* interp) {
    jit_global_init();

    void* s = jit_new_state();
    if (s == null) return null;

    // Function prolog: Value* fn(Interp* interp)
    _jit_prolog(s);
    void* arg = _jit_arg(s, CODE_ARG_L);
    _jit_getarg_l(s, JIT_V0, arg);  // V0 = interp (callee-saved)

    // Compile the expression, result goes to R0
    JitLocals locals;
    locals.count = 0;
    bool ok = jit_compile_expr(s, expr, interp, &locals);
    if (!ok) {
        _jit_destroy_state(s);
        return null;
    }

    // Return R0
    _jit_retr(s, JIT_R0, CODE_RETR_L);

    // Emit machine code
    void* code = _jit_emit(s);

    // C10: If emit failed, destroy the state immediately and bail out
    if (code == null) {
        _jit_destroy_state(s);
        return null;
    }

    _jit_clear_state(s);

    // Track state for cleanup (code buffer lives inside it).
    // C9: Cannot destroy_state here — code buffer lives inside the state.
    // If pool is full, the state leaks but the compiled code remains valid.
    // Increased pool from 64 to 256 to reduce likelihood of overflow.
    if (g_jit_state_count < JIT_STATE_POOL_SIZE) {
        g_jit_states[g_jit_state_count] = s;
        g_jit_state_count++;
    } else {
        if (!g_jit_pool_warned) {
            io::printfn("WARNING: JIT state pool full (%d states), further states leaked", JIT_STATE_POOL_SIZE);
            g_jit_pool_warned = true;
        }
    }

    return (JitFn)code;
}

/**
 * Compile an expression node. Result is left in JIT_R0.
 * Returns false if the expression can't be compiled.
 * locals tracks JIT-compiled let bindings for stack-based variable access.
 */
fn bool jit_compile_expr(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    if (expr == null) {
        // null expr -> nil
        emit_call_1(s, (void*)&jit_make_nil);  // R0 = make_nil(V0=interp)
        return true;
    }

    switch (expr.tag) {
        case E_LIT:
            return jit_compile_lit(s, expr, interp);

        case E_VAR:
            return jit_compile_var(s, expr, interp, locals);

        case E_IF:
            return jit_compile_if(s, expr, interp, locals);

        case E_AND:
            return jit_compile_and(s, expr, interp, locals);

        case E_OR:
            return jit_compile_or(s, expr, interp, locals);

        case E_BEGIN:
            return jit_compile_begin(s, expr, interp, locals);

        case E_APP:
            return jit_compile_app(s, expr, interp, locals);

        case E_CALL:
            return jit_compile_call(s, expr, interp, locals);

        case E_LET:
            return jit_compile_let(s, expr, interp, locals);

        case E_LAMBDA:
            return jit_compile_lambda(s, expr, interp, locals);

        case E_SET:
            return jit_compile_set(s, expr, interp, locals);

        case E_DEFINE:
            return jit_compile_define(s, expr, interp, locals);

        case E_QUASIQUOTE:
            return jit_compile_quasiquote(s, expr, interp, locals);

        case E_MATCH:
            return jit_compile_match(s, expr, interp, locals);

        case E_DEFMACRO:
            return jit_compile_define_macro(s, expr, interp, locals);

        case E_RESET:
            return jit_compile_reset(s, expr, interp, locals);

        case E_SHIFT:
            return jit_compile_shift(s, expr, interp, locals);

        case E_HANDLE:
            return jit_compile_handle(s, expr, interp, locals);

        case E_PERFORM:
            return jit_compile_perform(s, expr, interp, locals);

        case E_QUOTE:
            return jit_compile_quote(s, expr, interp);

        case E_INDEX:
            return jit_compile_index(s, expr, interp, locals);

        case E_PATH:
            return jit_compile_path(s, expr, interp, locals);

        case E_MODULE:
            return jit_compile_module(s, expr, interp, locals);

        case E_IMPORT:
            return jit_compile_import(s, expr, interp, locals);

        case E_DEFTYPE:
            return jit_compile_deftype(s, expr, interp, locals);

        case E_DEFABSTRACT:
            return jit_compile_defabstract(s, expr, interp, locals);

        case E_DEFUNION:
            return jit_compile_defunion(s, expr, interp, locals);

        case E_DEFALIAS:
            return jit_compile_defalias(s, expr, interp, locals);

        case E_DEFEFFECT:
            return jit_compile_defeffect(s, expr, interp, locals);

        case E_UNQUOTE:
        case E_UNQUOTE_SPLICING:
            // These should never appear outside quasiquote
            return false;

        default:
            return jit_compile_fallback(s, expr, interp, locals);
    }
}

// -----------------------------------------------------------------------------
// Expression compilers
// -----------------------------------------------------------------------------

fn bool jit_compile_lit(void* s, Expr* expr, Interp* interp) {
    Value* lit = expr.lit.value;
    if (lit == null || lit.tag == NIL) {
        emit_call_1(s, (void*)&jit_make_nil);
        return true;
    }
    if (lit.tag == INT) {
        // Call jit_make_int(interp, n)
        _jit_prepare(s);
        _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);   // arg0 = interp
        _jit_pushargi(s, lit.int_val, CODE_PUSHARGI_L); // arg1 = int value
        _jit_finishi(s, (void*)&jit_make_int);
        _jit_retval_l(s, JIT_R0);
        return true;
    }
    // For other literal types, use the literal value directly
    // (it's a pointer to a pre-existing Value in the expr pool)
    _jit_new_node_ww(s, CODE_MOVI, JIT_R0, (long)lit);
    return true;
}

fn bool jit_compile_var(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    uint sym_id = (uint)expr.var_expr.name;

    // Check JIT locals first (reverse order for proper scoping of shadowed names)
    for (usz i = locals.count; i > 0; i--) {
        if ((uint)locals.locals[i - 1].name == sym_id) {
            if (locals.locals[i - 1].is_mutable) {
                // Mutable local: stack slot holds Env* (the box). Lookup value in it.
                _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V1, (long)JIT_FP, (long)locals.locals[i - 1].stack_offset);
                _jit_prepare(s);
                _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);                // arg0 = env box
                _jit_pushargi(s, (long)sym_id, CODE_PUSHARGI_L);          // arg1 = symbol id
                _jit_finishi(s, (void*)&jit_env_lookup_local);
                _jit_retval_l(s, JIT_R0);
                return true;
            }
            // Non-mutable: load value directly from stack slot
            _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_R0, (long)JIT_FP, (long)locals.locals[i - 1].stack_offset);
            return true;
        }
    }

    // Fall back to jit_lookup_var: checks jit_env (for sub-expression local bindings), then global
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);     // arg0 = interp
    _jit_pushargi(s, (long)sym_id, CODE_PUSHARGI_L); // arg1 = symbol id
    _jit_finishi(s, (void*)&jit_lookup_var);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_if(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Compile test expression -> R0
    if (!jit_compile_expr(s, expr.if_expr.test, interp, locals)) return false;

    // Save test result to V1, call is_falsy(R0, interp)
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = test result
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);   // arg0 = test_value
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);   // arg1 = interp
    _jit_finishi(s, (void*)&jit_is_falsy);
    _jit_retval_l(s, JIT_R0);

    // If is_falsy returned true (non-zero), jump to else branch
    void* jump_else = _jit_new_node_pww(s, CODE_BNEI, null, (long)JIT_R0, 0);

    // Then branch
    if (!jit_compile_expr(s, expr.if_expr.then_branch, interp, locals)) return false;
    void* jump_end = _jit_new_node_p(s, CODE_JMPI, null);

    // Else branch
    _jit_patch(s, jump_else);
    if (!jit_compile_expr(s, expr.if_expr.else_branch, interp, locals)) return false;

    // End
    _jit_patch(s, jump_end);
    return true;
}

fn bool jit_compile_and(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Evaluate left -> R0
    if (!jit_compile_expr(s, expr.and_expr.left, interp, locals)) return false;

    // Check if left is falsy
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = left result
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);
    _jit_finishi(s, (void*)&jit_is_falsy);
    _jit_retval_l(s, JIT_R0);

    // If falsy, short-circuit: result = left (V1)
    void* jump_done = _jit_new_node_pww(s, CODE_BNEI, null, (long)JIT_R0, 0);

    // Not falsy: evaluate right
    if (!jit_compile_expr(s, expr.and_expr.right, interp, locals)) return false;
    void* jump_end = _jit_new_node_p(s, CODE_JMPI, null);

    // Short-circuit: restore V1 to R0
    _jit_patch(s, jump_done);
    _jit_new_node_ww(s, CODE_MOVR, JIT_R0, JIT_V1);

    _jit_patch(s, jump_end);
    return true;
}

fn bool jit_compile_or(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Evaluate left -> R0
    if (!jit_compile_expr(s, expr.or_expr.left, interp, locals)) return false;

    // Check if left is falsy
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = left result
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);
    _jit_finishi(s, (void*)&jit_is_falsy);
    _jit_retval_l(s, JIT_R0);

    // If NOT falsy, short-circuit: result = left (V1)
    void* jump_done = _jit_new_node_pww(s, CODE_BEQI, null, (long)JIT_R0, 0);

    // Falsy: evaluate right
    if (!jit_compile_expr(s, expr.or_expr.right, interp, locals)) return false;
    void* jump_end = _jit_new_node_p(s, CODE_JMPI, null);

    // Short-circuit: restore V1 to R0
    _jit_patch(s, jump_done);
    _jit_new_node_ww(s, CODE_MOVR, JIT_R0, JIT_V1);

    _jit_patch(s, jump_end);
    return true;
}

fn bool jit_compile_begin(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    usz count = expr.begin.expr_count;
    if (count == 0) {
        emit_call_1(s, (void*)&jit_make_nil);
        return true;
    }
    // Evaluate all expressions; result of last is in R0
    for (usz i = 0; i < count; i++) {
        if (!jit_compile_expr(s, expr.begin.exprs[i], interp, locals)) return false;
    }
    return true;
}

fn bool jit_compile_app(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Compile function -> R0, save to V1
    if (!jit_compile_expr(s, expr.app.func, interp, locals)) return false;
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = func

    // Compile argument -> R0, save to V2
    if (!jit_compile_expr(s, expr.app.arg, interp, locals)) return false;
    _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = arg

    // Call jit_apply_value(func=V1, arg=V2, interp=V0)
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);  // arg0 = func
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);  // arg1 = arg
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);  // arg2 = interp
    _jit_finishi(s, (void*)&jit_apply_value);
    _jit_retval_l(s, JIT_R0);
    return true;
}

/**
 * Check if a symbol names a known arithmetic primitive, return its direct helper.
 */
fn void* jit_get_direct_prim(SymbolId name, Interp* interp) {
    // If the symbol has been overloaded (METHOD_TABLE), don't use direct path
    Value* val = interp.global_env.lookup(name);
    if (val != null && val.tag == METHOD_TABLE) return null;

    // Check against interned primitive symbols
    char[] sym_name = interp.symbols.get_name(name);
    if (sym_name.len == 1) {
        switch (sym_name[0]) {
            case '+': return (void*)&jit_prim_add;
            case '-': return (void*)&jit_prim_sub;
            case '*': return (void*)&jit_prim_mul;
            case '<': return (void*)&jit_prim_lt;
            case '>': return (void*)&jit_prim_gt;
            case '=': return (void*)&jit_prim_eq;
            default: return null;
        }
    }
    return null;
}

/**
 * Check if an expression is "simple" (doesn't clobber V1/V2 during compilation).
 */
fn bool jit_is_simple_expr(Expr* expr) {
    if (expr == null) return true;
    return expr.tag == E_LIT || expr.tag == E_VAR;
}

fn bool jit_compile_call(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Check for macro expansion at compile time
    if (expr.call.func.tag == E_VAR) {
        MacroDef* macro_def = lookup_macro(expr.call.func.var_expr.name, interp);
        if (macro_def != null) {
            EvalResult macro_result = expand_pattern_macro(macro_def, expr, interp);
            if (macro_result.error.has_error) return false;
            Expr* expanded = value_to_expr(macro_result.value, interp);
            return jit_compile_expr(s, expanded, interp, locals);
        }
    }

    // Check if this is a known binary primitive: (+ a b), (- a b), etc.
    // Supports both simple (E_LIT/E_VAR) and complex (nested calls) arguments
    // by spilling intermediate results to the stack frame.
    if (expr.call.arg_count == 2 && expr.call.func.tag == E_VAR) {
        void* direct_fn = jit_get_direct_prim(expr.call.func.var_expr.name, interp);
        if (direct_fn != null) {
            bool arg0_simple = jit_is_simple_expr(expr.call.args[0]);
            bool arg1_simple = jit_is_simple_expr(expr.call.args[1]);

            if (arg0_simple && arg1_simple) {
                // Fast path: both args are simple, no spilling needed
                // Compile arg0 -> R0, save to V1
                if (!jit_compile_expr(s, expr.call.args[0], interp, locals)) return false;
                _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = arg0

                // Compile arg1 -> R0, save to V2
                if (!jit_compile_expr(s, expr.call.args[1], interp, locals)) return false;
                _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = arg1
            } else {
                // Nested path: at least one arg is complex (e.g. a function call).
                // Compile arg0, spill to stack, compile arg1, restore arg0.
                // This avoids V1/V2 clobbering when nested jit_compile_expr
                // uses those registers internally.

                // Allocate a stack slot for spilling arg0 result
                int spill_offset = _jit_allocai(s, 8);  // 8 bytes for a pointer

                // Compile first arg -> result in R0
                if (!jit_compile_expr(s, expr.call.args[0], interp, locals)) return false;
                // Spill arg0 result to stack: *(FP + spill_offset) = R0
                _jit_new_node_www(s, CODE_STXI_L, (long)spill_offset, (long)JIT_FP, (long)JIT_R0);

                // Compile second arg -> result in R0, move to V2
                if (!jit_compile_expr(s, expr.call.args[1], interp, locals)) return false;
                _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = arg1

                // Restore arg0 from stack into V1: V1 = *(FP + spill_offset)
                _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V1, (long)JIT_FP, (long)spill_offset);
            }

            // Call direct_fn(interp=V0, a=V1, b=V2) -> R0
            _jit_prepare(s);
            _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);  // arg0 = interp
            _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);  // arg1 = a
            _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);  // arg2 = b
            _jit_finishi(s, direct_fn);
            _jit_retval_l(s, JIT_R0);
            return true;
        }
    }

    // General case: compile function and args
    usz argc = expr.call.arg_count;

    if (argc == 0) {
        // Zero-arg call: compile function, pass empty list via jit_apply_multi_args.
        // This correctly handles both zero-arg closures (lambda () body)
        // and zero-arg variadic closures (lambda (.. args) body).
        if (!jit_compile_expr(s, expr.call.func, interp, locals)) return false;
        _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = func

        // Make nil (empty arg list)
        emit_call_1(s, (void*)&jit_make_nil);
        _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = nil

        // Call jit_apply_multi_args(interp=V0, func=V1, arg_list=V2, argc=0)
        _jit_prepare(s);
        _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);
        _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);
        _jit_pushargi(s, 0, CODE_PUSHARGI_L);
        _jit_finishi(s, (void*)&jit_apply_multi_args);
        _jit_retval_l(s, JIT_R0);
        return true;
    }

    if (argc == 1) {
        // Single-arg call: like E_APP
        // Compile func, spill to stack, compile arg, restore func, apply
        int spill_fn = _jit_allocai(s, 8);

        if (!jit_compile_expr(s, expr.call.func, interp, locals)) return false;
        _jit_new_node_www(s, CODE_STXI_L, (long)spill_fn, (long)JIT_FP, (long)JIT_R0);

        if (!jit_compile_expr(s, expr.call.args[0], interp, locals)) return false;
        _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = arg

        _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V1, (long)JIT_FP, (long)spill_fn);  // V1 = func

        _jit_prepare(s);
        _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);
        _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);
        _jit_finishi(s, (void*)&jit_apply_value);
        _jit_retval_l(s, JIT_R0);
        return true;
    }

    // Multi-arg call: build cons list and use jit_apply_multi_args.
    // Handles variadic closures, multi-arity primitives, and curried application.
    // Compiles all args natively, spills to stack, builds list right-to-left,
    // then dispatches through the smart apply helper.

    // C8: Refuse to JIT-compile calls with >16 args; fall back to interpreter
    if (argc > 16) return false;

    // Allocate stack slots for all args
    int[16] arg_slots;
    for (usz i = 0; i < argc; i++) {
        arg_slots[i] = _jit_allocai(s, 8);
    }

    // Compile each arg and spill to its stack slot
    for (usz i = 0; i < argc; i++) {
        if (!jit_compile_expr(s, expr.call.args[i], interp, locals)) return false;
        _jit_new_node_www(s, CODE_STXI_L, (long)arg_slots[i], (long)JIT_FP, (long)JIT_R0);
    }

    // Compile function and spill to stack
    int func_slot = _jit_allocai(s, 8);
    if (!jit_compile_expr(s, expr.call.func, interp, locals)) return false;
    _jit_new_node_www(s, CODE_STXI_L, (long)func_slot, (long)JIT_FP, (long)JIT_R0);

    // Build cons list right-to-left: start with nil
    emit_call_1(s, (void*)&jit_make_nil);
    _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = nil (accumulator)

    for (usz i = argc; i > 0; i--) {
        // V1 = arg[i-1] loaded from stack slot
        _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V1, (long)JIT_FP, (long)arg_slots[i - 1]);
        // Call jit_cons(interp=V0, car=V1, cdr=V2)
        _jit_prepare(s);
        _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);   // arg0 = interp
        _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);   // arg1 = car (current arg)
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);   // arg2 = cdr (accumulated list)
        _jit_finishi(s, (void*)&jit_cons);
        _jit_retval_l(s, JIT_R0);
        _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = updated list
    }

    // V2 = complete arg list, load func from stack
    _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V1, (long)JIT_FP, (long)func_slot);

    // Call jit_apply_multi_args(interp=V0, func=V1, arg_list=V2, argc)
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);        // arg0 = interp
    _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);        // arg1 = func
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);        // arg2 = arg list
    _jit_pushargi(s, (long)argc, CODE_PUSHARGI_L);    // arg3 = arg count
    _jit_finishi(s, (void*)&jit_apply_multi_args);
    _jit_retval_l(s, JIT_R0);
    return true;
}

// Check if expr tree contains (set! name ...) at any depth
fn bool has_set_on_name(Expr* expr, SymbolId name) {
    if (expr == null) return false;
    switch (expr.tag) {
        case E_SET:
            if (expr.set_expr.name == name) return true;
            return has_set_on_name(expr.set_expr.value, name);
        case E_LET:
            if (expr.let_expr.name == name) return false;  // shadowed
            return has_set_on_name(expr.let_expr.init, name) ||
                   has_set_on_name(expr.let_expr.body, name);
        case E_IF:
            return has_set_on_name(expr.if_expr.test, name) ||
                   has_set_on_name(expr.if_expr.then_branch, name) ||
                   has_set_on_name(expr.if_expr.else_branch, name);
        case E_LAMBDA:
            return has_set_on_name(expr.lambda.body, name);
        case E_BEGIN:
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                if (has_set_on_name(expr.begin.exprs[i], name)) return true;
            }
            return false;
        case E_APP:
            return has_set_on_name(expr.app.func, name) ||
                   has_set_on_name(expr.app.arg, name);
        case E_CALL:
            if (has_set_on_name(expr.call.func, name)) return true;
            for (usz i = 0; i < expr.call.arg_count; i++) {
                if (has_set_on_name(expr.call.args[i], name)) return true;
            }
            return false;
        case E_AND:
            return has_set_on_name(expr.and_expr.left, name) ||
                   has_set_on_name(expr.and_expr.right, name);
        case E_OR:
            return has_set_on_name(expr.or_expr.left, name) ||
                   has_set_on_name(expr.or_expr.right, name);
        default: return false;
    }
}

// Check if a lambda in the expression captures and mutates the named variable
fn bool has_closure_set_on_local(Expr* body, SymbolId name) {
    if (body == null) return false;
    switch (body.tag) {
        case E_LAMBDA:
            return has_set_on_name(body.lambda.body, name);
        case E_LET:
            if (body.let_expr.name == name) return false;  // shadowed
            return has_closure_set_on_local(body.let_expr.init, name) ||
                   has_closure_set_on_local(body.let_expr.body, name);
        case E_IF:
            return has_closure_set_on_local(body.if_expr.test, name) ||
                   has_closure_set_on_local(body.if_expr.then_branch, name) ||
                   has_closure_set_on_local(body.if_expr.else_branch, name);
        case E_BEGIN:
            for (usz i = 0; i < body.begin.expr_count; i++) {
                if (has_closure_set_on_local(body.begin.exprs[i], name)) return true;
            }
            return false;
        case E_APP:
            return has_closure_set_on_local(body.app.func, name) ||
                   has_closure_set_on_local(body.app.arg, name);
        case E_CALL:
            if (has_closure_set_on_local(body.call.func, name)) return true;
            for (usz i = 0; i < body.call.arg_count; i++) {
                if (has_closure_set_on_local(body.call.args[i], name)) return true;
            }
            return false;
        case E_AND:
            return has_closure_set_on_local(body.and_expr.left, name) ||
                   has_closure_set_on_local(body.and_expr.right, name);
        case E_OR:
            return has_closure_set_on_local(body.or_expr.left, name) ||
                   has_closure_set_on_local(body.or_expr.right, name);
        default: return false;
    }
}

fn bool jit_compile_let(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Recursive let (let ^rec): use dedicated helper that does full env setup + patching
    if (expr.let_expr.is_recursive) {
        return jit_compile_let_rec(s, expr, interp, locals);
    }

    bool is_mutable = has_closure_set_on_local(expr.let_expr.body, expr.let_expr.name);

    // Compile init expression -> result in R0
    if (!jit_compile_expr(s, expr.let_expr.init, interp, locals)) return false;

    // Allocate a stack slot for this local variable
    int offset = _jit_allocai(s, 8);

    if (is_mutable) {
        // Mutable local: create an Env node as a "box" for shared mutable state.
        // The Env node is shared between JIT stack and closures that capture this var.
        // All reads/writes go through this Env node.

        // Save init value to V1 (callee-saved, survives env building)
        _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);

        // Build parent env from preceding JIT locals, or load runtime env
        bool has_env = emit_build_locals_env(s, interp, locals);
        if (!has_env) emit_load_env(s, JIT_V2);

        // Call jit_env_extend(interp, parent_env, name, init_value) -> Env* (the box)
        _jit_prepare(s);
        _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                                    // interp
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                                    // env (V2)
        _jit_pushargi(s, (long)(uint)expr.let_expr.name, CODE_PUSHARGI_L);            // name
        _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);                                    // value
        _jit_finishi(s, (void*)&jit_env_extend);
        _jit_retval_l(s, JIT_R0);  // R0 = Env* (boxed variable)

        // Store Env* to stack slot
        _jit_new_node_www(s, CODE_STXI_L, (long)offset, (long)JIT_FP, (long)JIT_R0);
    } else {
        // Non-mutable local: store value directly in stack slot (fast path)
        _jit_new_node_www(s, CODE_STXI_L, (long)offset, (long)JIT_FP, (long)JIT_R0);
    }

    // Push local binding into the locals table
    if (locals.count >= 32) return false;  // Too many locals — fall back to interpreter
    locals.locals[locals.count].name = expr.let_expr.name;
    locals.locals[locals.count].stack_offset = offset;
    locals.locals[locals.count].is_mutable = is_mutable;
    locals.count++;

    // Compile body -> result in R0
    bool ok = jit_compile_expr(s, expr.let_expr.body, interp, locals);

    // Pop local binding (restore count for proper scoping)
    locals.count--;

    return ok;
}

fn bool jit_compile_lambda(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Build env from JIT locals into V2 (callee-saved), or load runtime env
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);

    // Call jit_make_closure_from_expr(interp, expr, env)
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg2 = env (V2)
    _jit_finishi(s, (void*)&jit_make_closure_from_expr);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_let_rec(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Build env from JIT locals into V2 (callee-saved), or load runtime env
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);

    // Call jit_eval_let_rec(interp, expr, env)
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg2 = env (V2)
    _jit_finishi(s, (void*)&jit_eval_let_rec);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_set(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Dot-path set!: (set! obj.field value) — use runtime helper
    if (expr.set_expr.is_path && expr.set_expr.path_segment_count >= 2) {
        // Compile the value expression -> R0
        if (!jit_compile_expr(s, expr.set_expr.value, interp, locals)) return false;
        _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = value

        // Build env into V2
        bool has_env = emit_build_locals_env(s, interp, locals);
        if (!has_env) emit_load_env(s, JIT_V2);

        // Call jit_eval_set_path(interp, expr, value, env)
        _jit_prepare(s);
        _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                  // arg0 = interp
        _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);               // arg1 = expr ptr
        _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);                  // arg2 = value
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                  // arg3 = env
        _jit_finishi(s, (void*)&jit_eval_set_path);
        _jit_retval_l(s, JIT_R0);
        return true;
    }

    // Check if target is a JIT local variable
    for (usz i = locals.count; i > 0; i--) {
        if (locals.locals[i - 1].name == expr.set_expr.name) {
            if (locals.locals[i - 1].is_mutable) {
                // Mutable local: stack slot holds Env* (the box). Set value through it.
                if (!jit_compile_expr(s, expr.set_expr.value, interp, locals)) return false;
                _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = new value

                // Load Env* box from stack slot
                _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V2, (long)JIT_FP, (long)locals.locals[i - 1].stack_offset);

                // Call jit_eval_set(interp, name, value, env_box)
                _jit_prepare(s);
                _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                                    // interp
                _jit_pushargi(s, (long)(uint)expr.set_expr.name, CODE_PUSHARGI_L);            // name
                _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);                                    // value
                _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                                    // env box
                _jit_finishi(s, (void*)&jit_eval_set);
                _jit_retval_l(s, JIT_R0);
                return true;
            }
            // Non-mutable local: update stack slot directly
            if (!jit_compile_expr(s, expr.set_expr.value, interp, locals)) return false;
            _jit_new_node_www(s, CODE_STXI_L, (long)locals.locals[i - 1].stack_offset,
                             (long)JIT_FP, (long)JIT_R0);
            return true;
        }
    }

    // Not a local: use env-based set!
    // Compile the value expression -> R0
    if (!jit_compile_expr(s, expr.set_expr.value, interp, locals)) return false;

    // Save compiled value to V1 before env building (which clobbers R0/R1)
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = value

    // Build env from JIT locals into V2 (callee-saved), or load runtime env
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);

    // Call jit_eval_set(interp, name, value, env)
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                          // arg0 = interp
    _jit_pushargi(s, (long)(uint)expr.set_expr.name, CODE_PUSHARGI_L);  // arg1 = symbol id
    _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);                          // arg2 = value
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                          // arg3 = env (V2)
    _jit_finishi(s, (void*)&jit_eval_set);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_define(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Compile the value expression -> R0
    if (!jit_compile_expr(s, expr.define.value, interp, locals)) return false;

    // Call jit_eval_define(interp, name, value)
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = value
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                        // arg0 = interp
    _jit_pushargi(s, (long)(uint)expr.define.name, CODE_PUSHARGI_L);   // arg1 = symbol id
    _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);                        // arg2 = value
    _jit_finishi(s, (void*)&jit_eval_define);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_quasiquote(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg2 = env (V2)
    _jit_finishi(s, (void*)&jit_eval_quasiquote);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_match(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Compile scrutinee natively → R0
    if (!jit_compile_expr(s, expr.match.scrutinee, interp, locals)) return false;
    // Save scrutinee to V1
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);

    // Build env from locals for pattern bindings, or load runtime env
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);

    // Call jit_do_match(interp, scrutinee, expr, env)
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);                       // arg1 = scrutinee value
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg2 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg3 = env (V2)
    _jit_finishi(s, (void*)&jit_do_match);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_define_macro(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg2 = env (V2)
    _jit_finishi(s, (void*)&jit_eval_define_macro);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_reset(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg2 = env (V2)
    _jit_finishi(s, (void*)&jit_exec_reset);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_shift(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg2 = env (V2)
    _jit_finishi(s, (void*)&jit_exec_shift);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_handle(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg2 = env (V2)
    _jit_finishi(s, (void*)&jit_exec_handle);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_perform(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg2 = env (V2)
    _jit_finishi(s, (void*)&jit_exec_perform);
    _jit_retval_l(s, JIT_R0);
    return true;
}

// --- Native compilation for previously-fallback expression types ---

fn bool jit_compile_quote(void* s, Expr* expr, Interp* interp) {
    // Quote returns the literal datum directly — no eval needed
    Value* datum = expr.quote.datum;
    if (datum == null) {
        emit_call_1(s, (void*)&jit_make_nil);
    } else {
        _jit_new_node_ww(s, CODE_MOVI, JIT_R0, (long)datum);
    }
    return true;
}

fn bool jit_compile_index(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Compile collection sub-expression → R0, save to V1
    if (!jit_compile_expr(s, expr.index.collection, interp, locals)) return false;
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);

    // Compile index sub-expression → R0, save to V2
    if (!jit_compile_expr(s, expr.index.index, interp, locals)) return false;
    _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);

    // Call jit_do_index(interp=V0, collection=V1, index=V2)
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);  // arg0 = interp
    _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);  // arg1 = collection
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);  // arg2 = index
    _jit_finishi(s, (void*)&jit_do_index);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_path(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Path access: eval_path does variable lookup + field chain — no eval() internally
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg2 = env (V2)
    _jit_finishi(s, (void*)&jit_do_path);
    _jit_retval_l(s, JIT_R0);
    return true;
}

// Emit a standard 3-arg call pattern: helper(interp, expr, env)
fn bool jit_compile_3arg_helper(void* s, Expr* expr, Interp* interp, JitLocals* locals, void* helper_fn) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg2 = env (V2)
    _jit_finishi(s, helper_fn);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_module(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    return jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_module);
}

fn bool jit_compile_import(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    return jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_import);
}

fn bool jit_compile_deftype(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    return jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_deftype);
}

fn bool jit_compile_defabstract(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    return jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_defabstract);
}

fn bool jit_compile_defunion(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    return jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_defunion);
}

fn bool jit_compile_defalias(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    return jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_defalias);
}

fn bool jit_compile_defeffect(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    return jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_defeffect);
}

fn bool jit_compile_fallback(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Build env from JIT locals into V2 (callee-saved), or load runtime env
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);

    // Fall back to interpreter: jit_eval_fallback(expr, env, interp)
    _jit_prepare(s);
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                   // arg0 = expr ptr
    _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                       // arg1 = env (V2)
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg2 = interp
    _jit_finishi(s, (void*)&jit_eval_fallback);
    _jit_retval_l(s, JIT_R0);
    return true;
}

// -----------------------------------------------------------------------------
// Emit helpers
// -----------------------------------------------------------------------------

/**
 * Emit a call to fn(interp), result in R0.
 * The function takes one Interp* argument.
 */
fn void emit_call_1(void* s, void* fn_ptr) {
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);  // arg0 = interp
    _jit_finishi(s, fn_ptr);
    _jit_retval_l(s, JIT_R0);
}

/**
 * Emit code that builds an Env* from JIT locals into V2 (callee-saved register).
 * Returns true if locals were captured (V2 holds the env), false if no locals.
 * Used by jit_compile_lambda/let_rec/fallback/set to capture let-locals.
 * Clobbers R0/R1 (scratch) and V2; V0/V1 are preserved.
 */
// Emit code to load current env into a register at runtime.
// Uses jit_get_env(interp) which returns jit_env if set, else global_env.
// Result goes to the specified register. Clobbers R0.
fn void emit_load_env(void* s, int target_reg) {
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);  // arg0 = interp
    _jit_finishi(s, (void*)&jit_get_env);
    _jit_retval_l(s, target_reg);
}

fn bool emit_build_locals_env(void* s, Interp* interp, JitLocals* locals) {
    if (locals.count == 0) return false;

    // Start with current env in V2 (jit_env if in sub-expression context, else global_env)
    emit_load_env(s, JIT_V2);

    for (usz i = 0; i < locals.count; i++) {
        if (locals.locals[i].is_mutable) {
            // Mutable local: stack slot holds a shared Env* (the box).
            // Reparent it to the current chain (V2) so it links in properly,
            // then use it as the new V2.
            _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_R1, (long)JIT_FP, (long)locals.locals[i].stack_offset);
            // Call jit_env_reparent(env=R1, new_parent=V2)
            _jit_prepare(s);
            _jit_pushargr(s, JIT_R1, CODE_PUSHARGR_L);   // arg0 = mutable env box
            _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);    // arg1 = new parent chain
            _jit_finishi(s, (void*)&jit_env_reparent);
            // After reparenting, reload the Env* from stack into V2
            // (R1 may be clobbered by finishi, so reload from stack)
            _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V2, (long)JIT_FP, (long)locals.locals[i].stack_offset);
        } else {
            // Non-mutable local: extend env with current stack value
            _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_R1, (long)JIT_FP, (long)locals.locals[i].stack_offset);
            // Call jit_env_extend(interp=V0, env=V2, name, value=R1)
            _jit_prepare(s);
            _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);   // arg0 = interp
            _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);    // arg1 = env
            _jit_pushargi(s, (long)(uint)locals.locals[i].name, CODE_PUSHARGI_L);  // arg2 = name
            _jit_pushargr(s, JIT_R1, CODE_PUSHARGR_L);    // arg3 = value
            _jit_finishi(s, (void*)&jit_env_extend);
            _jit_retval_l(s, JIT_V2);  // V2 = updated env
        }
    }

    return true;
}


/**
 * Parse a string into an Expr for JIT compilation.
 * Uses the existing parser infrastructure.
 */
fn Expr* parse_for_jit(char[] source, Interp* interp) {
    return parse(source, interp);
}
