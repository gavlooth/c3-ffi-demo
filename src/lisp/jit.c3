module lisp;

import std::io;
import std::collections::list;
import main;

faultdef JIT_COMPILE_FAILED;

// =============================================================================
// Context boundary state — saved/restored at every StackCtx switch point.
// Replaces 12-field manual save/restore (24 lines per site → 2 lines).
// =============================================================================

struct SavedInterpState {
    Env*        jit_env;
    Env*        match_env;
    usz         eval_depth;
    InterpFlags flags;
    Expr*       tco_expr;
    Env*        tco_env;
    main::ScopeRegion* tco_recycle;
    usz         reset_depth;
    usz         handler_count;
    main::ScopeRegion* scope;
    main::ScopeRegion* escape_scope;
    bool        escape_env_mode;
}

fn SavedInterpState save_interp_state(Interp* interp) {
    SavedInterpState s;
    s.jit_env        = interp.jit_env;
    s.match_env      = interp.match_env;
    s.eval_depth     = interp.eval_depth;
    s.flags          = interp.flags;
    s.tco_expr       = interp.jit_tco_expr;
    s.tco_env        = interp.jit_tco_env;
    s.tco_recycle    = interp.tco_recycle_scope;
    s.reset_depth    = interp.reset_depth;
    s.handler_count  = interp.handler_count;
    s.scope          = interp.current_scope;
    s.escape_scope   = interp.escape_scope;
    s.escape_env_mode = interp.escape_env_mode;
    return s;
}

fn void restore_interp_state(Interp* interp, SavedInterpState* s) {
    interp.jit_env           = s.jit_env;
    interp.match_env         = s.match_env;
    interp.eval_depth        = s.eval_depth;
    interp.flags             = s.flags;
    interp.jit_tco_expr      = s.tco_expr;
    interp.jit_tco_env       = s.tco_env;
    interp.tco_recycle_scope = s.tco_recycle;
    interp.reset_depth       = s.reset_depth;
    interp.handler_count     = s.handler_count;
    interp.current_scope     = s.scope;
    interp.escape_scope      = s.escape_scope;
    interp.escape_env_mode   = s.escape_env_mode;
}

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
                // Create fresh call_scope as child of escape_scope (result_scope)
                // so accumulator values in escape_scope survive the recycle.
                main::ScopeRegion* parent = interp.escape_scope != null ? interp.escape_scope : cs.parent;
                main::ScopeRegion* fresh = main::scope_create(parent);
                interp.current_scope = fresh;
                interp.tco_recycle_scope = fresh;  // Update: fresh is now the recyclable scope
                interp.releasing_scope = cs;       // Tell copy_to_parent what's being released
                env = copy_tco_env_chain(env, interp);
                interp.releasing_scope = null;
                main::scope_release(cs);
            }

            continue;
        }

        interp.jit_env = saved_env;
        return result;
    }
}

// Single-scope wrapper: evaluates closure body in ONE child scope.
// Used for non-loop function calls. No escape_scope/tco_recycle_scope set.
// Avoids the overhead of two scopes (result_scope + call_scope).
fn Value* jit_eval_in_single_scope(Expr* body, Env* call_env, Interp* interp) {
    main::ScopeRegion* saved_scope = interp.current_scope;
    main::ScopeRegion* call_scope = main::scope_create(saved_scope);
    interp.current_scope = call_scope;

    Value* result = jit_eval(body, call_env, interp);

    main::ScopeRegion* active_scope = interp.current_scope;

    interp.current_scope = saved_scope;
    if (active_scope.refcount == 1 && result != null && result.tag == CONS) {
        // CONS return: adopt O(1) to avoid O(n) spine copy. Temps leak as garbage.
        main::scope_adopt(saved_scope, active_scope);
    } else {
        // Scalar return: copy O(1) + release frees all temporaries.
        interp.releasing_scope = active_scope;
        if (result != null) {
            result = copy_to_parent(result, interp);
        }
        interp.releasing_scope = null;
        main::scope_release(active_scope);
    }

    return result;
}

// Scope-per-call wrapper: evaluates closure body in a child scope.
// Three-tier hierarchy: result_scope (persists across TCO bounces) → call_scope (recycled).
// Accumulator cons cells go in result_scope; temporaries go in call_scope.
fn Value* jit_eval_in_call_scope(Expr* body, Env* call_env, Interp* interp) {
    main::ScopeRegion* saved_scope = interp.current_scope;

    // Two-scope hierarchy: result_scope persists, call_scope recycles
    main::ScopeRegion* result_scope = main::scope_create(saved_scope);
    main::ScopeRegion* call_scope = main::scope_create(result_scope);
    interp.current_scope = call_scope;

    // Set escape_scope so make_cons can redirect accumulator cells
    main::ScopeRegion* saved_escape = interp.escape_scope;
    interp.escape_scope = result_scope;

    // Mark call_scope (not result_scope) as eligible for TCO recycling
    main::ScopeRegion* saved_recycle = interp.tco_recycle_scope;
    interp.tco_recycle_scope = call_scope;

    Value* result = jit_eval(body, call_env, interp);

    // After jit_eval, current_scope may differ from call_scope (TCO recycling)
    main::ScopeRegion* active_scope = interp.current_scope;

    // Restore saved state
    interp.tco_recycle_scope = saved_recycle;
    interp.escape_scope = saved_escape;

    // Step 1: Merge active_scope (call_scope) into result_scope.
    // If nothing escaped (RC=1), adopt is safe and O(1) — chunks move to result_scope.
    // Otherwise, copy the result value from active_scope to result_scope.
    if (active_scope != result_scope) {
        if (active_scope.refcount == 1) {
            // O(1) adoption: move chunks/dtors to result_scope, recycle active_scope
            main::scope_adopt(result_scope, active_scope);
        } else {
            // Something escaped from call_scope — copy result, then release
            interp.current_scope = result_scope;
            interp.releasing_scope = active_scope;
            if (result != null) {
                result = copy_to_parent(result, interp);
            }
            interp.releasing_scope = null;
            main::scope_release(active_scope);
        }
    }

    // Step 2: Promote result from result_scope to caller's scope.
    // Only adopt for CONS returns (the O(n) case). Scalar returns use O(1) copy + release
    // to free temporaries and avoid garbage accumulation.
    interp.current_scope = saved_scope;
    if (result_scope.refcount == 1 && result != null && result.tag == CONS) {
        main::scope_adopt(saved_scope, result_scope);
    } else {
        interp.releasing_scope = result_scope;
        if (result != null) {
            result = copy_to_parent(result, interp);
        }
        interp.releasing_scope = null;
        main::scope_release(result_scope);
    }

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

    // Escape-env optimization: Env in escape_scope survives the bounce.
    // Skip frame copy — just promote binding values to escape_scope.
    // Eliminates make_env + malloc(Binding[]) + scope_register_dtor + build_hash_table per bounce.
    if (interp.escape_scope != null &&
        src.scope_gen == interp.escape_scope.generation) {
        // Env is NOT in the dying scope. Update binding values in-place.
        main::ScopeRegion* saved = interp.current_scope;
        interp.current_scope = interp.escape_scope;
        for (usz i = 0; i < src.binding_count; i++) {
            src.bindings[i].value = copy_to_parent(src.bindings[i].value, interp);
        }
        interp.current_scope = saved;
        src.parent = copy_tco_env_chain(src.parent, interp);
        return src;  // same Env, updated values, no alloc
    }

    // Recursive: copy parent chain first (bottom-up)
    Env* parent_copy = copy_tco_env_chain(src.parent, interp);

    // Create new env frame in current_scope (already switched to fresh scope)
    Env* dst = make_env(interp, parent_copy);
    for (usz i = 0; i < src.binding_count; i++) {
        Value* v = copy_to_parent(src.bindings[i].value, interp);
        dst.define(src.bindings[i].name, v);
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
    // enable escape_env_mode so the loop's param env is allocated in
    // escape_scope (survives TCO bounces without frame copy).
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
        stored = copy_to_parent(value, interp);
        interp.releasing_scope = null;
        interp.current_scope = saved_scope_ctp;
    }
    // Try local env first, then global
    if (catch err = env.set(name, stored)) {
        if (interp.global_env == null) return raise_error(interp, "set!: unbound variable");
        if (catch err2 = interp.global_env.set(name, stored)) {
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
        stored_val = copy_to_parent(value, interp);
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

            interp.global_env.define(name, mt_val);
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

            interp.global_env.define(name, mt_val);
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

    // Tag closure with its bound name for display
    if (stored_val != null && stored_val.tag == CLOSURE) {
        stored_val.closure_val.name = name;
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
    Value* promoted = copy_to_parent(value, interp);
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
    SavedInterpState saved = save_interp_state(interp);

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
    SavedInterpState saved = save_interp_state(interp);

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

    SavedInterpState saved = save_interp_state(interp);

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
                    char[256] ebuf;
                    char[] tag_name = interp.symbols.get_name(tag);
                    TypeInfo* exp_ti = interp.types.get(expected_tid);
                    TypeInfo* act_ti = interp.types.get(actual_tid);
                    char[] exp_name = exp_ti != null ? interp.symbols.get_name(exp_ti.name) : "?";
                    char[] act_name = act_ti != null ? interp.symbols.get_name(act_ti.name) : "?";
                    char[] msg = io::bprintf(&ebuf, "perform '%s': expected %s argument, got %s",
                        (ZString)tag_name, (ZString)exp_name, (ZString)act_name)!!;
                    return make_error(interp, msg);
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

                        SavedInterpState saved_sig = save_interp_state(interp);
                        main::stack_ctx_suspend();
                        // === RESUMED by resolve ===
                        restore_interp_state(interp, &saved_sig);

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
            char[256] sbuf;
            char[] tag_name = interp.symbols.get_name(tag);
            char[] arg_type = interp.symbols.get_name(value_type_name(arg_v, interp));
            char[] smsg = io::bprintf(&sbuf, "strict handler: unhandled effect '%s' with arg type %s",
                (ZString)tag_name, (ZString)arg_type)!!;
            return make_error(interp, smsg);
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
    for (usz i = 0; i < expr.handle.clause_count; i++) {
        h.tags[i] = expr.handle.clauses[i].effect_tag;
        h.clauses[i] = expr.handle.clauses[i];
    }

    interp.handler_count = my_idx + 1;

    // Save handler copy for reinstallation after clause evaluation
    state.handler_copy = *h;
    state.handler_idx = my_idx;

    SavedInterpState saved = save_interp_state(interp);

    main::StackContext parent_ctx;
    main::stack_ctx_switch_to(ctx, &parent_ctx);

    // Capture raise_pending before restoring flags (the context may have set it)
    bool body_raise_pending = interp.flags.raise_pending;
    restore_interp_state(interp, &saved);
    if (body_raise_pending) interp.flags.raise_pending = true;

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
                clause_env.define(match_r.bindings[j].name, match_r.bindings[j].value);
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
    env.define(name, mod_val);

    if (expr.import_expr.import_all) {
        // (import mod :all) — copy ALL exports into scope (old behavior, explicit opt-in)
        for (usz i = 0; i < mod.export_count; i++) {
            Value* val = mod.env.lookup(mod.exports[i]);
            if (val != null) {
                env.define(mod.exports[i], val);
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
            env.define(bind_name, val);
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
        return jit_eval_in_single_scope(func.closure_val.body, new_env, interp);
    }

    // Direct primitive path for multi-arg primitives
    if (func.tag == PRIMITIVE) {
        // Set constructor type_id and user_data (needed for type constructor / FFI primitives)
        interp.constructor_type_id = func.prim_val.tag;
        interp.prim_user_data = func.prim_val.user_data;
        Value*[16] args;
        Value* curr = arg_list;
        for (usz i = 0; i < arg_count && i < 16; i++) {
            if (curr == null || curr.tag != CONS) break;
            args[i] = curr.cons_val.car;
            curr = curr.cons_val.cdr;
        }
        usz safe_count = arg_count > 16 ? 16 : arg_count;
        Value* result = func.prim_val.func(args[:safe_count], null, interp);
        if (is_error(result)) return raise_error(interp, result.str_chars[:result.str_len]);
        return result;
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
                call_env.define(func.closure_val.params[i], curr.cons_val.car);
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
        if (resolved != null && resolved.tag == ERROR) return resolved;
        if (resolved == null && mt.fallback != null) {
            resolved = mt.fallback;
        }
        if (resolved == null) {
            return format_dispatch_error(mt, args[:safe_count], interp);
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
                call_env.define(func.closure_val.params[i], curr.cons_val.car);
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
        if (resolved != null && resolved.tag == ERROR) return resolved;
        if (resolved == null && mt.fallback != null) resolved = mt.fallback;
        if (resolved == null) return format_dispatch_error(mt, args[:safe_count], interp);
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

alias JitLocalList = List{JitLocal};

struct JitLocals {
    JitLocalList locals;
}

// Global JIT initialization flag
bool g_jit_initialized = false;

// JIT state cleanup tracking (C9: increased from 64 to 256, then to 4096)
const usz JIT_STATE_POOL_SIZE = 4096;
const usz JIT_STATE_GC_THRESHOLD = 3072;  // GC when pool reaches 75%
void*[4096] g_jit_states;
usz g_jit_state_count = 0;
bool g_jit_pool_warned = false;
bool g_jit_gc_needed = false;  // signals that GC should run between evals

// JIT compilation cache: Expr* → JitFn (open-addressing hash on pointer value)
struct JitCacheEntry {
    Expr* expr;     // cache key (pointer identity)
    JitFn compiled; // compiled function
}

const usz JIT_CACHE_SIZE = 1024;
const usz JIT_CACHE_GC_THRESHOLD = 768;  // clear cache at 75% capacity
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
    // If cache is too full, clear it entirely (codes remain valid in state pool)
    if (g_jit_cache_count >= JIT_CACHE_GC_THRESHOLD) {
        $if DEBUG_BUILD:
            io::eprintfn("[debug] JIT cache full (%d/%d), clearing", g_jit_cache_count, JIT_CACHE_SIZE);
        $endif
        jit_cache_clear();
    }
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

/**
 * Clear the JIT cache. Safe to call anytime — just causes recompilation on next access.
 * Does NOT free state memory (codes remain valid but unreachable from cache).
 */
fn void jit_cache_clear() {
    for (usz i = 0; i < JIT_CACHE_SIZE; i++) {
        g_jit_cache[i].expr = null;
        g_jit_cache[i].compiled = null;
    }
    g_jit_cache_count = 0;
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
 * Garbage-collect JIT states and cache.
 *
 * SAFETY: Must only be called between top-level evaluations, when NO JIT
 * code is on the call stack. Destroys all state objects (freeing their code
 * buffers) and clears the compilation cache. Subsequent jit_eval() calls
 * will recompile on demand.
 */
fn void jit_gc() {
    if (!g_jit_gc_needed) return;

    // Instead of destroying states (which is slow/hangs in GNU Lightning),
    // just reset the pool counter. The states are already cleared via
    // _jit_clear_state() — only the code buffer remains. We leak the code
    // buffers but they're small (~100 bytes each after clear_state).
    // The cache is cleared so expressions get recompiled into new states.
    g_jit_state_count = 0;
    jit_cache_clear();
    g_jit_gc_needed = false;
    g_jit_pool_warned = false;
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

    // GC old states when pool is near capacity
    if (g_jit_gc_needed) {
        jit_gc();
    }

    void* s = jit_new_state();
    if (s == null) return null;

    // Function prolog: Value* fn(Interp* interp)
    _jit_prolog(s);
    void* arg = _jit_arg(s, CODE_ARG_L);
    _jit_getarg_l(s, JIT_V0, arg);  // V0 = interp (callee-saved)

    // Compile the expression, result goes to R0
    JitLocals locals;
    if (catch err = jit_compile_expr(s, expr, interp, &locals, true)) {
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
    // States are destroyed by jit_gc() between top-level evaluations.
    if (g_jit_state_count < JIT_STATE_POOL_SIZE) {
        g_jit_states[g_jit_state_count] = s;
        g_jit_state_count++;
        // Signal GC needed when approaching capacity
        if (g_jit_state_count >= JIT_STATE_GC_THRESHOLD) {
            g_jit_gc_needed = true;
            $if DEBUG_BUILD:
                io::eprintfn("[debug] JIT pool at %d/%d (%.0f%%), GC scheduled",
                    g_jit_state_count, JIT_STATE_POOL_SIZE,
                    (double)g_jit_state_count / (double)JIT_STATE_POOL_SIZE * 100.0);
            $endif
        }
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
 * Returns a fault if the expression can't be compiled.
 * locals tracks JIT-compiled let bindings for stack-based variable access.
 */
fn void? jit_compile_expr(void* s, Expr* expr, Interp* interp, JitLocals* locals, bool is_tail = false) {
    if (expr == null) {
        // null expr -> nil
        emit_call_1(s, (void*)&jit_make_nil);  // R0 = make_nil(V0=interp)
        return;
    }

    switch (expr.tag) {
        case E_LIT:
            jit_compile_lit(s, expr, interp)!;

        case E_VAR:
            jit_compile_var(s, expr, interp, locals)!;

        case E_IF:
            jit_compile_if(s, expr, interp, locals, is_tail)!;

        case E_AND:
            jit_compile_and(s, expr, interp, locals, is_tail)!;

        case E_OR:
            jit_compile_or(s, expr, interp, locals, is_tail)!;

        case E_BEGIN:
            jit_compile_begin(s, expr, interp, locals, is_tail)!;

        case E_APP:
            jit_compile_app(s, expr, interp, locals, is_tail)!;

        case E_CALL:
            jit_compile_call(s, expr, interp, locals, is_tail)!;

        case E_LET:
            jit_compile_let(s, expr, interp, locals, is_tail)!;

        case E_LAMBDA:
            jit_compile_lambda(s, expr, interp, locals)!;

        case E_SET:
            jit_compile_set(s, expr, interp, locals)!;

        case E_DEFINE:
            jit_compile_define(s, expr, interp, locals)!;

        case E_QUASIQUOTE:
            jit_compile_quasiquote(s, expr, interp, locals)!;

        case E_MATCH:
            jit_compile_match(s, expr, interp, locals)!;

        case E_DEFMACRO:
            jit_compile_define_macro(s, expr, interp, locals)!;

        case E_RESET:
            jit_compile_reset(s, expr, interp, locals)!;

        case E_SHIFT:
            jit_compile_shift(s, expr, interp, locals)!;

        case E_HANDLE:
            jit_compile_handle(s, expr, interp, locals)!;

        case E_PERFORM:
            jit_compile_perform(s, expr, interp, locals)!;

        case E_RESOLVE:
            jit_compile_resolve(s, expr, interp, locals)!;

        case E_QUOTE:
            jit_compile_quote(s, expr, interp)!;

        case E_INDEX:
            jit_compile_index(s, expr, interp, locals)!;

        case E_PATH:
            jit_compile_path(s, expr, interp, locals)!;

        case E_MODULE:
            jit_compile_module(s, expr, interp, locals)!;

        case E_IMPORT:
            jit_compile_import(s, expr, interp, locals)!;

        case E_DEFTYPE:
            jit_compile_deftype(s, expr, interp, locals)!;

        case E_DEFABSTRACT:
            jit_compile_defabstract(s, expr, interp, locals)!;

        case E_DEFUNION:
            jit_compile_defunion(s, expr, interp, locals)!;

        case E_DEFALIAS:
            jit_compile_defalias(s, expr, interp, locals)!;

        case E_DEFEFFECT:
            jit_compile_defeffect(s, expr, interp, locals)!;

        case E_EXPORT_FROM:
            jit_compile_export_from(s, expr, interp, locals)!;

        case E_FFI_LIB:
            jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_ffi_lib)!;

        case E_FFI_FN:
            jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_ffi_fn)!;

        case E_UNQUOTE:
        case E_UNQUOTE_SPLICING:
            // These should never appear outside quasiquote
            return JIT_COMPILE_FAILED~;

        default:
            jit_compile_fallback(s, expr, interp, locals)!;
    }
}

// -----------------------------------------------------------------------------
// Expression compilers
// -----------------------------------------------------------------------------

fn void? jit_compile_lit(void* s, Expr* expr, Interp* interp) {
    Value* lit = expr.lit.value;
    if (lit == null || lit.tag == NIL) {
        emit_call_1(s, (void*)&jit_make_nil);
        return;
    }
    if (lit.tag == INT) {
        emit_call_2i(s, (void*)&jit_make_int, JIT_V0, lit.int_val);
        return;
    }
    // For other literal types, use the literal value directly
    // (it's a pointer to a pre-existing Value in the expr pool)
    _jit_new_node_ww(s, CODE_MOVI, JIT_R0, (long)lit);
}

fn void? jit_compile_var(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    uint sym_id = (uint)expr.var_expr.name;

    // Check JIT locals first (reverse order for proper scoping of shadowed names)
    for (usz i = locals.locals.len(); i > 0; i--) {
        if ((uint)locals.locals[i - 1].name == sym_id) {
            if (locals.locals[i - 1].is_mutable) {
                // Mutable local: stack slot holds Env* (the box). Lookup value in it.
                _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V1, (long)JIT_FP, (long)locals.locals[i - 1].stack_offset);
                emit_call_2i(s, (void*)&jit_env_lookup_local, JIT_V1, (long)sym_id);
                return;
            }
            // Non-mutable: load value directly from stack slot
            _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_R0, (long)JIT_FP, (long)locals.locals[i - 1].stack_offset);
            return;
        }
    }

    // Fall back to jit_lookup_var: checks jit_env (for sub-expression local bindings), then global
    emit_call_2i(s, (void*)&jit_lookup_var, JIT_V0, (long)sym_id);
}

fn void? jit_compile_if(void* s, Expr* expr, Interp* interp, JitLocals* locals, bool is_tail) {
    // Compile test expression -> R0 (test is never in tail position)
    jit_compile_expr(s, expr.if_expr.test, interp, locals)!;

    // Save test result to V1, call is_falsy(V1, interp)
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = test result
    emit_call_2(s, (void*)&jit_is_falsy, JIT_V1, JIT_V0);

    // If is_falsy returned true (non-zero), jump to else branch
    void* jump_else = _jit_new_node_pww(s, CODE_BNEI, null, (long)JIT_R0, 0);

    // Then branch — inherits tail position
    jit_compile_expr(s, expr.if_expr.then_branch, interp, locals, is_tail)!;
    void* jump_end = _jit_new_node_p(s, CODE_JMPI, null);

    // Else branch — inherits tail position
    _jit_patch(s, jump_else);
    jit_compile_expr(s, expr.if_expr.else_branch, interp, locals, is_tail)!;

    // End
    _jit_patch(s, jump_end);
}

fn void? jit_compile_and(void* s, Expr* expr, Interp* interp, JitLocals* locals, bool is_tail) {
    // Evaluate left -> R0 (left is never in tail position)
    jit_compile_expr(s, expr.and_expr.left, interp, locals)!;

    // Check if left is falsy
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = left result
    emit_call_2(s, (void*)&jit_is_falsy, JIT_V1, JIT_V0);

    // If falsy, short-circuit: result = left (V1)
    void* jump_done = _jit_new_node_pww(s, CODE_BNEI, null, (long)JIT_R0, 0);

    // Not falsy: evaluate right — inherits tail position
    jit_compile_expr(s, expr.and_expr.right, interp, locals, is_tail)!;
    void* jump_end = _jit_new_node_p(s, CODE_JMPI, null);

    // Short-circuit: restore V1 to R0
    _jit_patch(s, jump_done);
    _jit_new_node_ww(s, CODE_MOVR, JIT_R0, JIT_V1);

    _jit_patch(s, jump_end);
}

fn void? jit_compile_or(void* s, Expr* expr, Interp* interp, JitLocals* locals, bool is_tail) {
    // Evaluate left -> R0 (left is never in tail position)
    jit_compile_expr(s, expr.or_expr.left, interp, locals)!;

    // Check if left is falsy
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = left result
    emit_call_2(s, (void*)&jit_is_falsy, JIT_V1, JIT_V0);

    // If NOT falsy, short-circuit: result = left (V1)
    void* jump_done = _jit_new_node_pww(s, CODE_BEQI, null, (long)JIT_R0, 0);

    // Falsy: evaluate right — inherits tail position
    jit_compile_expr(s, expr.or_expr.right, interp, locals, is_tail)!;
    void* jump_end = _jit_new_node_p(s, CODE_JMPI, null);

    // Short-circuit: restore V1 to R0
    _jit_patch(s, jump_done);
    _jit_new_node_ww(s, CODE_MOVR, JIT_R0, JIT_V1);

    _jit_patch(s, jump_end);
}

fn void? jit_compile_begin(void* s, Expr* expr, Interp* interp, JitLocals* locals, bool is_tail) {
    usz count = expr.begin.expr_count;
    if (count == 0) {
        emit_call_1(s, (void*)&jit_make_nil);
        return;
    }
    // Evaluate all expressions; last inherits tail position
    for (usz i = 0; i < count; i++) {
        bool tail = is_tail && (i == count - 1);
        jit_compile_expr(s, expr.begin.exprs[i], interp, locals, tail)!;
    }
}

fn void? jit_compile_app(void* s, Expr* expr, Interp* interp, JitLocals* locals, bool is_tail) {
    // Compile function -> R0, save to V1 (not in tail position)
    jit_compile_expr(s, expr.app.func, interp, locals)!;
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = func

    // Compile argument -> R0, save to V2 (not in tail position)
    jit_compile_expr(s, expr.app.arg, interp, locals)!;
    _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = arg

    // Call apply — use _tail variant when in tail position
    void* apply_fn = is_tail ? (void*)&jit_apply_value_tail : (void*)&jit_apply_value;
    emit_call_3(s, apply_fn, JIT_V1, JIT_V2, JIT_V0);
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

fn void? jit_compile_call(void* s, Expr* expr, Interp* interp, JitLocals* locals, bool is_tail) {
    // Check for macro expansion at compile time
    if (expr.call.func.tag == E_VAR) {
        MacroDef* macro_def = lookup_macro(expr.call.func.var_expr.name, interp);
        if (macro_def != null) {
            EvalResult macro_result = expand_pattern_macro(macro_def, expr, interp);
            if (macro_result.error.has_error) return JIT_COMPILE_FAILED~;
            Expr* expanded = value_to_expr(macro_result.value, interp);
            jit_compile_expr(s, expanded, interp, locals, is_tail)!;
            return;
        }
    }

    // Check if this is a known binary primitive: (+ a b), (- a b), etc.
    if (expr.call.arg_count == 2 && expr.call.func.tag == E_VAR) {
        void* direct_fn = jit_get_direct_prim(expr.call.func.var_expr.name, interp);
        if (direct_fn != null) {
            jit_compile_inline_binop(s, expr, interp, locals, direct_fn)!;
            return;
        }
    }

    // General case: compile function and args, dispatch through apply
    jit_compile_multi_arg(s, expr, interp, locals, is_tail)!;
}

// Inline 2-arg call for known binary primitives (+, -, *, /, =, <, >).
// Supports both simple (E_LIT/E_VAR) and complex (nested calls) arguments
// by spilling intermediate results to the stack frame.
fn void? jit_compile_inline_binop(void* s, Expr* expr, Interp* interp, JitLocals* locals, void* direct_fn) {
    bool arg0_simple = jit_is_simple_expr(expr.call.args[0]);
    bool arg1_simple = jit_is_simple_expr(expr.call.args[1]);

    if (arg0_simple && arg1_simple) {
        // Fast path: both args are simple, no spilling needed
        // Compile arg0 -> R0, save to V1
        jit_compile_expr(s, expr.call.args[0], interp, locals)!;
        _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = arg0

        // Compile arg1 -> R0, save to V2
        jit_compile_expr(s, expr.call.args[1], interp, locals)!;
        _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = arg1
    } else {
        // Nested path: at least one arg is complex (e.g. a function call).
        // Compile arg0, spill to stack, compile arg1, restore arg0.
        // This avoids V1/V2 clobbering when nested jit_compile_expr
        // uses those registers internally.

        // Allocate a stack slot for spilling arg0 result
        int spill_offset = _jit_allocai(s, 8);  // 8 bytes for a pointer

        // Compile first arg -> result in R0
        jit_compile_expr(s, expr.call.args[0], interp, locals)!;
        // Spill arg0 result to stack: *(FP + spill_offset) = R0
        _jit_new_node_www(s, CODE_STXI_L, (long)spill_offset, (long)JIT_FP, (long)JIT_R0);

        // Compile second arg -> result in R0, move to V2
        jit_compile_expr(s, expr.call.args[1], interp, locals)!;
        _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = arg1

        // Restore arg0 from stack into V1: V1 = *(FP + spill_offset)
        _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V1, (long)JIT_FP, (long)spill_offset);
    }

    // Call direct_fn(interp=V0, a=V1, b=V2) -> R0
    emit_call_3(s, direct_fn, JIT_V0, JIT_V1, JIT_V2);
}

// General multi-arg call: handles 0, 1, 2+ arguments.
// Compiles function and args, spills to stack slots, builds cons arg list
// right-to-left, dispatches through jit_apply_multi_args / jit_apply_value.
fn void? jit_compile_multi_arg(void* s, Expr* expr, Interp* interp, JitLocals* locals, bool is_tail) {
    usz argc = expr.call.arg_count;

    // Set call name for error messages (before any apply call)
    if (expr.call.func.tag == E_VAR) {
        emit_call_2i(s, (void*)&jit_set_call_name, JIT_V0, (long)expr.call.func.var_expr.name);
    } else {
        emit_call_1(s, (void*)&jit_clear_call_name);
    }

    if (argc == 0) {
        // Zero-arg call: compile function, pass empty list via jit_apply_multi_args.
        // This correctly handles both zero-arg closures (lambda () body)
        // and zero-arg variadic closures (lambda (.. args) body).
        jit_compile_expr(s, expr.call.func, interp, locals)!;
        _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = func

        // Make nil (empty arg list)
        emit_call_1(s, (void*)&jit_make_nil);
        _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = nil

        // Call apply — use _tail variant when in tail position
        void* apply_fn = is_tail ? (void*)&jit_apply_multi_args_tail : (void*)&jit_apply_multi_args;
        emit_call_4_rrri(s, apply_fn, JIT_V0, JIT_V1, JIT_V2, 0);
        return;
    }

    if (argc == 1) {
        // Single-arg call: like E_APP
        // Compile func, spill to stack, compile arg, restore func, apply
        int spill_fn = _jit_allocai(s, 8);

        jit_compile_expr(s, expr.call.func, interp, locals)!;
        _jit_new_node_www(s, CODE_STXI_L, (long)spill_fn, (long)JIT_FP, (long)JIT_R0);

        jit_compile_expr(s, expr.call.args[0], interp, locals)!;
        _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = arg

        _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V1, (long)JIT_FP, (long)spill_fn);  // V1 = func

        // Use _tail variant when in tail position
        void* apply_fn = is_tail ? (void*)&jit_apply_value_tail : (void*)&jit_apply_value;
        emit_call_3(s, apply_fn, JIT_V1, JIT_V2, JIT_V0);
        return;
    }

    // Multi-arg call: build cons list and use jit_apply_multi_args.
    // Handles variadic closures, multi-arity primitives, and curried application.
    // Compiles all args natively, spills to stack, builds list right-to-left,
    // then dispatches through the smart apply helper.

    // C8: Refuse to JIT-compile calls with >16 args; fall back to interpreter
    if (argc > 16) return JIT_COMPILE_FAILED~;

    // Allocate stack slots for all args
    int[16] arg_slots;
    for (usz i = 0; i < argc; i++) {
        arg_slots[i] = _jit_allocai(s, 8);
    }

    // Compile each arg and spill to its stack slot (args are not in tail position)
    for (usz i = 0; i < argc; i++) {
        jit_compile_expr(s, expr.call.args[i], interp, locals)!;
        _jit_new_node_www(s, CODE_STXI_L, (long)arg_slots[i], (long)JIT_FP, (long)JIT_R0);
    }

    // Compile function and spill to stack (not in tail position)
    int func_slot = _jit_allocai(s, 8);
    jit_compile_expr(s, expr.call.func, interp, locals)!;
    _jit_new_node_www(s, CODE_STXI_L, (long)func_slot, (long)JIT_FP, (long)JIT_R0);

    // Build cons list right-to-left: start with nil
    emit_call_1(s, (void*)&jit_make_nil);
    _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = nil (accumulator)

    for (usz i = argc; i > 0; i--) {
        // V1 = arg[i-1] loaded from stack slot
        _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V1, (long)JIT_FP, (long)arg_slots[i - 1]);
        // Call jit_cons(interp=V0, car=V1, cdr=V2)
        emit_call_3(s, (void*)&jit_cons, JIT_V0, JIT_V1, JIT_V2);
        _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);  // V2 = updated list
    }

    // V2 = complete arg list, load func from stack
    _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V1, (long)JIT_FP, (long)func_slot);

    // Call apply — use _tail variant when in tail position
    void* apply_fn = is_tail ? (void*)&jit_apply_multi_args_tail : (void*)&jit_apply_multi_args;
    emit_call_4_rrri(s, apply_fn, JIT_V0, JIT_V1, JIT_V2, (long)argc);
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

fn void? jit_compile_let(void* s, Expr* expr, Interp* interp, JitLocals* locals, bool is_tail) {
    // Recursive let (let ^rec): use dedicated helper that does full env setup + patching
    if (expr.let_expr.is_recursive) {
        jit_compile_let_rec(s, expr, interp, locals)!;
        return;
    }

    bool is_mutable = has_closure_set_on_local(expr.let_expr.body, expr.let_expr.name);

    // Compile init expression -> result in R0 (init is not in tail position)
    jit_compile_expr(s, expr.let_expr.init, interp, locals)!;

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

        // Call jit_env_extend_root(interp, parent_env, name, init_value) -> Env* (the box)
        // Uses root_region so the box is persistent and shared with closures.
        emit_call_4_rrir(s, (void*)&jit_env_extend_root, JIT_V0, JIT_V2, (long)(uint)expr.let_expr.name, JIT_V1);

        // Store Env* to stack slot
        _jit_new_node_www(s, CODE_STXI_L, (long)offset, (long)JIT_FP, (long)JIT_R0);
    } else {
        // Non-mutable local: store value directly in stack slot (fast path)
        _jit_new_node_www(s, CODE_STXI_L, (long)offset, (long)JIT_FP, (long)JIT_R0);
    }

    // Push local binding into the locals table
    JitLocal local = { .name = expr.let_expr.name, .stack_offset = offset, .is_mutable = is_mutable };
    locals.locals.push(local);

    // Compile body -> result in R0 (body inherits tail position)
    defer locals.locals.pop()!!;
    jit_compile_expr(s, expr.let_expr.body, interp, locals, is_tail)!;
}

fn void? jit_compile_lambda(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Build env from JIT locals into V2 (callee-saved), or load runtime env
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);

    // Call jit_make_closure_from_expr(interp, expr, env)
    emit_call_3i(s, (void*)&jit_make_closure_from_expr, JIT_V0, (long)expr, JIT_V2);
}

fn void? jit_compile_let_rec(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Build env from JIT locals into V2 (callee-saved), or load runtime env
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);

    // Call jit_eval_let_rec(interp, expr, env)
    emit_call_3i(s, (void*)&jit_eval_let_rec, JIT_V0, (long)expr, JIT_V2);
}

fn void? jit_compile_set(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Dot-path set!: (set! obj.field value) — use runtime helper
    if (expr.set_expr.is_path && expr.set_expr.path_segment_count >= 2) {
        // Compile the value expression -> R0
        jit_compile_expr(s, expr.set_expr.value, interp, locals)!;
        _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = value

        // Build env into V2
        bool has_env = emit_build_locals_env(s, interp, locals);
        if (!has_env) emit_load_env(s, JIT_V2);

        // Call jit_eval_set_path(interp, expr, value, env)
        emit_call_4_rirr(s, (void*)&jit_eval_set_path, JIT_V0, (long)expr, JIT_V1, JIT_V2);
        return;
    }

    // Check if target is a JIT local variable
    for (usz i = locals.locals.len(); i > 0; i--) {
        if (locals.locals[i - 1].name == expr.set_expr.name) {
            if (locals.locals[i - 1].is_mutable) {
                // Mutable local: stack slot holds Env* (the box). Set value through it.
                jit_compile_expr(s, expr.set_expr.value, interp, locals)!;
                _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = new value

                // Load Env* box from stack slot
                _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_V2, (long)JIT_FP, (long)locals.locals[i - 1].stack_offset);

                // Call jit_eval_set(interp, name, value, env_box)
                emit_call_4_rirr(s, (void*)&jit_eval_set, JIT_V0, (long)(uint)expr.set_expr.name, JIT_V1, JIT_V2);
                return;
            }
            // Non-mutable local: update stack slot directly
            jit_compile_expr(s, expr.set_expr.value, interp, locals)!;
            _jit_new_node_www(s, CODE_STXI_L, (long)locals.locals[i - 1].stack_offset,
                             (long)JIT_FP, (long)JIT_R0);
            return;
        }
    }

    // Not a local: use env-based set!
    // Compile the value expression -> R0
    jit_compile_expr(s, expr.set_expr.value, interp, locals)!;

    // Save compiled value to V1 before env building (which clobbers R0/R1)
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = value

    // Build env from JIT locals into V2 (callee-saved), or load runtime env
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);

    // Call jit_eval_set(interp, name, value, env)
    emit_call_4_rirr(s, (void*)&jit_eval_set, JIT_V0, (long)(uint)expr.set_expr.name, JIT_V1, JIT_V2);
}

fn void? jit_compile_define(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Compile the value expression -> R0
    jit_compile_expr(s, expr.define.value, interp, locals)!;

    // Call jit_eval_define(interp, name, value)
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);  // V1 = value
    emit_call_3i(s, (void*)&jit_eval_define, JIT_V0, (long)(uint)expr.define.name, JIT_V1);
}

fn void? jit_compile_quasiquote(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_eval_quasiquote)!;
}

fn void? jit_compile_match(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Compile scrutinee natively → R0
    jit_compile_expr(s, expr.match.scrutinee, interp, locals)!;
    // Save scrutinee to V1
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);

    // Build env from locals for pattern bindings, or load runtime env
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);

    // Call jit_do_match(interp, scrutinee, expr, env)
    emit_call_4_rrir(s, (void*)&jit_do_match, JIT_V0, JIT_V1, (long)expr, JIT_V2);
}

fn void? jit_compile_define_macro(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_eval_define_macro)!;
}

fn void? jit_compile_reset(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_exec_reset)!;
}

fn void? jit_compile_shift(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_exec_shift)!;
}

fn void? jit_compile_handle(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_exec_handle)!;
}

fn void? jit_compile_perform(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);
    emit_call_3i(s, (void*)&jit_exec_perform, JIT_V0, (long)expr, JIT_V2);
}

fn Value* jit_exec_resolve(Interp* interp, Expr* expr, Env* env) {
    // Evaluate the value to resolve with
    Value* val = jit_eval(expr.resolve.value, env, interp);
    if (val != null && val.tag == ERROR) return val;

    // Look up __k in the current environment (set by handle clause)
    Value* k_val = env.lookup(interp.sym__k);
    if (k_val == null) {
        k_val = interp.global_env.lookup(interp.sym__k);
    }
    if (k_val == null || k_val.tag != CONTINUATION) {
        return make_error(interp, "resolve: not inside a handler clause");
    }

    // Effect handler path: single-shot resume (no clone)
    if (k_val.cont_val.is_coroutine_based && k_val.cont_val.handle_state != null) {
        main::StackCtx* ctx = k_val.cont_val.ctx;
        if (ctx == null || ctx.status != main::StackCtxStatus.CTX_SUSPENDED) {
            return make_error(interp, "resolve: continuation not suspended");
        }

        HandleEffectState* hstate = (HandleEffectState*)k_val.cont_val.handle_state;

        // Reinstall handler so the body can signal again
        if (interp.handler_count >= interp.handler_capacity) interp.grow_handler_stack();
        interp.handler_stack[hstate.handler_idx] = hstate.handler_copy;
        interp.handler_count = hstate.handler_idx + 1;

        // Set resume value
        interp.resume_value = val;

        SavedInterpState saved_resolve = save_interp_state(interp);

        main::StackContext parent_ctx;
        main::stack_ctx_resume(ctx, &parent_ctx);

        restore_interp_state(interp, &saved_resolve);

        // Pop the reinstalled handler
        interp.handler_count = hstate.handler_idx;

        // Stack overflow — context hit guard page after resolve
        if (ctx.status == main::StackCtxStatus.CTX_DEAD) {
            return make_error(interp, "stack overflow after resolve");
        }

        // Return value: body result if completed, or val as placeholder
        if (ctx.status == main::StackCtxStatus.CTX_COMPLETED) {
            Value* body_result = (Value*)ctx.result;
            return body_result != null ? body_result : val;
        }
        // Body signaled again (hstate.signaled set by signal) — return val
        // The handle loop will dispatch the next signal
        return val;
    }

    // All continuations are context-based — apply directly
    EvalResult r = jit_apply_continuation(k_val, val, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn void? jit_compile_resolve(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);
    emit_call_3i(s, (void*)&jit_exec_resolve, JIT_V0, (long)expr, JIT_V2);
}

// --- Native compilation for previously-fallback expression types ---

fn void? jit_compile_quote(void* s, Expr* expr, Interp* interp) {
    // Quote returns the literal datum directly — no eval needed
    Value* datum = expr.quote.datum;
    if (datum == null) {
        emit_call_1(s, (void*)&jit_make_nil);
    } else {
        _jit_new_node_ww(s, CODE_MOVI, JIT_R0, (long)datum);
    }
}

fn void? jit_compile_index(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Compile collection sub-expression → R0, save to V1
    jit_compile_expr(s, expr.index.collection, interp, locals)!;
    _jit_new_node_ww(s, CODE_MOVR, JIT_V1, JIT_R0);

    // Compile index sub-expression → R0, save to V2
    jit_compile_expr(s, expr.index.index, interp, locals)!;
    _jit_new_node_ww(s, CODE_MOVR, JIT_V2, JIT_R0);

    // Call jit_do_index(interp=V0, collection=V1, index=V2)
    emit_call_3(s, (void*)&jit_do_index, JIT_V0, JIT_V1, JIT_V2);
}

fn void? jit_compile_path(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_path)!;
}

// Emit a standard 3-arg call pattern: helper(interp, expr, env)
fn void? jit_compile_3arg_helper(void* s, Expr* expr, Interp* interp, JitLocals* locals, void* helper_fn) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    if (!has_env) emit_load_env(s, JIT_V2);
    emit_call_3i(s, helper_fn, JIT_V0, (long)expr, JIT_V2);
}

fn void? jit_compile_module(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_module)!;
}

fn void? jit_compile_import(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_import)!;
}

fn Value* jit_do_export_from(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = jit_eval_export_from_impl(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn EvalResult jit_eval_export_from_impl(Expr* expr, Env* env, Interp* interp) {
    SymbolId source_name = expr.export_from.source_module;
    Module* source_mod = find_module(source_name, interp);
    if (source_mod == null) {
        return eval_error("export-from: source module not found");
    }

    // Find the current module being defined (the one whose env we're in)
    // Walk module table to find which module owns env's parent chain
    Module* current_mod = null;
    for (usz i = 0; i < interp.module_count; i++) {
        if (interp.modules[i].env == env || interp.modules[i].env == env.parent) {
            current_mod = &interp.modules[i];
            break;
        }
    }
    // Also try global_env's match (module body swaps global_env)
    if (current_mod == null) {
        for (usz i = 0; i < interp.module_count; i++) {
            if (interp.modules[i].env == interp.global_env) {
                current_mod = &interp.modules[i];
                break;
            }
        }
    }
    if (current_mod == null) {
        return eval_error("export-from: must be used inside a module");
    }

    if (expr.export_from.all) {
        // Re-export all exports from source
        for (usz i = 0; i < source_mod.export_count; i++) {
            Value* val = source_mod.env.lookup(source_mod.exports[i]);
            if (val != null) {
                env.define(source_mod.exports[i], val);
                // Add to current module's export list (grow if needed)
                if (current_mod.export_count >= current_mod.export_capacity) {
                    usz new_cap = current_mod.export_capacity * 2;
                    SymbolId* new_exports = (SymbolId*)mem::malloc(SymbolId.sizeof * new_cap);
                    for (usz j = 0; j < current_mod.export_count; j++) new_exports[j] = current_mod.exports[j];
                    mem::free(current_mod.exports);
                    current_mod.exports = new_exports;
                    current_mod.export_capacity = new_cap;
                }
                current_mod.exports[current_mod.export_count] = source_mod.exports[i];
                current_mod.export_count++;
            }
        }
    } else {
        // Re-export specific names
        for (usz ni = 0; ni < expr.export_from.name_count; ni++) {
            SymbolId sym = expr.export_from.names[ni];
            // Verify exported from source
            bool is_exported = false;
            for (usz ei = 0; ei < source_mod.export_count; ei++) {
                if ((uint)source_mod.exports[ei] == (uint)sym) {
                    is_exported = true;
                    break;
                }
            }
            if (!is_exported) {
                return eval_error("export-from: symbol not exported from source module");
            }
            Value* val = source_mod.env.lookup(sym);
            if (val != null) {
                env.define(sym, val);
                if (current_mod.export_count >= current_mod.export_capacity) {
                    usz new_cap = current_mod.export_capacity * 2;
                    SymbolId* new_exports = (SymbolId*)mem::malloc(SymbolId.sizeof * new_cap);
                    for (usz j = 0; j < current_mod.export_count; j++) new_exports[j] = current_mod.exports[j];
                    mem::free(current_mod.exports);
                    current_mod.exports = new_exports;
                    current_mod.export_capacity = new_cap;
                }
                current_mod.exports[current_mod.export_count] = sym;
                current_mod.export_count++;
            }
        }
    }

    return eval_ok(make_nil(interp));
}

fn void? jit_compile_export_from(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_export_from)!;
}

fn void? jit_compile_deftype(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_deftype)!;
}

fn void? jit_compile_defabstract(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_defabstract)!;
}

fn void? jit_compile_defunion(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_defunion)!;
}

fn void? jit_compile_defalias(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_defalias)!;
}

fn void? jit_compile_defeffect(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    jit_compile_3arg_helper(s, expr, interp, locals, (void*)&jit_do_defeffect)!;
}

fn void? jit_compile_fallback(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
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

/// Emit fn(reg0, reg1) -> R0
fn void emit_call_2(void* s, void* fn_ptr, int reg0, int reg1) {
    _jit_prepare(s);
    _jit_pushargr(s, reg0, CODE_PUSHARGR_L);
    _jit_pushargr(s, reg1, CODE_PUSHARGR_L);
    _jit_finishi(s, fn_ptr);
    _jit_retval_l(s, JIT_R0);
}

/// Emit fn(reg0, imm1) -> R0
fn void emit_call_2i(void* s, void* fn_ptr, int reg0, long imm1) {
    _jit_prepare(s);
    _jit_pushargr(s, reg0, CODE_PUSHARGR_L);
    _jit_pushargi(s, imm1, CODE_PUSHARGI_L);
    _jit_finishi(s, fn_ptr);
    _jit_retval_l(s, JIT_R0);
}

/// Emit fn(reg0, reg1, reg2) -> R0
fn void emit_call_3(void* s, void* fn_ptr, int reg0, int reg1, int reg2) {
    _jit_prepare(s);
    _jit_pushargr(s, reg0, CODE_PUSHARGR_L);
    _jit_pushargr(s, reg1, CODE_PUSHARGR_L);
    _jit_pushargr(s, reg2, CODE_PUSHARGR_L);
    _jit_finishi(s, fn_ptr);
    _jit_retval_l(s, JIT_R0);
}

/// Emit fn(reg0, imm1, reg2) -> R0
fn void emit_call_3i(void* s, void* fn_ptr, int reg0, long imm1, int reg2) {
    _jit_prepare(s);
    _jit_pushargr(s, reg0, CODE_PUSHARGR_L);
    _jit_pushargi(s, imm1, CODE_PUSHARGI_L);
    _jit_pushargr(s, reg2, CODE_PUSHARGR_L);
    _jit_finishi(s, fn_ptr);
    _jit_retval_l(s, JIT_R0);
}

/// Emit fn(reg0, reg1, imm2, reg3) -> R0
fn void emit_call_4_rrir(void* s, void* fn_ptr, int reg0, int reg1, long imm2, int reg3) {
    _jit_prepare(s);
    _jit_pushargr(s, reg0, CODE_PUSHARGR_L);
    _jit_pushargr(s, reg1, CODE_PUSHARGR_L);
    _jit_pushargi(s, imm2, CODE_PUSHARGI_L);
    _jit_pushargr(s, reg3, CODE_PUSHARGR_L);
    _jit_finishi(s, fn_ptr);
    _jit_retval_l(s, JIT_R0);
}

/// Emit fn(reg0, imm1, reg2, reg3) -> R0
fn void emit_call_4_rirr(void* s, void* fn_ptr, int reg0, long imm1, int reg2, int reg3) {
    _jit_prepare(s);
    _jit_pushargr(s, reg0, CODE_PUSHARGR_L);
    _jit_pushargi(s, imm1, CODE_PUSHARGI_L);
    _jit_pushargr(s, reg2, CODE_PUSHARGR_L);
    _jit_pushargr(s, reg3, CODE_PUSHARGR_L);
    _jit_finishi(s, fn_ptr);
    _jit_retval_l(s, JIT_R0);
}

/// Emit fn(reg0, reg1, reg2, imm3) -> R0
fn void emit_call_4_rrri(void* s, void* fn_ptr, int reg0, int reg1, int reg2, long imm3) {
    _jit_prepare(s);
    _jit_pushargr(s, reg0, CODE_PUSHARGR_L);
    _jit_pushargr(s, reg1, CODE_PUSHARGR_L);
    _jit_pushargr(s, reg2, CODE_PUSHARGR_L);
    _jit_pushargi(s, imm3, CODE_PUSHARGI_L);
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
    if (locals.locals.len() == 0) return false;

    // Start with current env in V2 (jit_env if in sub-expression context, else global_env)
    emit_load_env(s, JIT_V2);

    for (usz i = 0; i < locals.locals.len(); i++) {
        if (locals.locals[i].is_mutable) {
            // Mutable local: stack slot holds a shared Env* (the box).
            // Reparent it to the current chain (V2) so it links in properly,
            // then use it as the new V2.
            _jit_new_node_www(s, CODE_LDXI_L, (long)JIT_R1, (long)JIT_FP, (long)locals.locals[i].stack_offset);
            // Call jit_env_reparent(env=R1, new_parent=V2)
            emit_call_2(s, (void*)&jit_env_reparent, JIT_R1, JIT_V2);
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
// Parallel state for Value-based (AOT) effects
struct ResetValueState {
    Value*  body;         // Reset body closure
    Interp* interp;
    bool    shifted;
    Value*  shift_body;   // Shift body closure
    Value*  shift_k;      // Captured k for shift body
}

fn void reset_value_entry(void* arg) {
    ResetValueState* state = (ResetValueState*)arg;
    Value* v = jit_apply_value(state.body, make_nil(state.interp), state.interp);
    main::g_current_stack_ctx.result = v;
}

fn Value* jit_reset_value(Interp* interp, Value* body) {
    main::StackCtx* ctx = main::stack_ctx_create(&interp.stack_ctx_pool);
    if (ctx == null) return raise_error(interp, "stack engine: failed to create context");

    ResetValueState state;
    state.body = body;
    state.interp = interp;
    state.shifted = false;
    state.shift_body = null;
    state.shift_k = null;

    main::stack_ctx_init(ctx, &reset_value_entry, &state);

    void* saved_state = interp.current_reset_state;
    interp.current_reset_state = &state;

    // Save ALL interp state (was incomplete — only saved 3 of 12 fields)
    SavedInterpState saved = save_interp_state(interp);

    main::StackContext parent_ctx;
    main::stack_ctx_switch_to(ctx, &parent_ctx);

    interp.current_reset_state = saved_state;
    restore_interp_state(interp, &saved);

    if (ctx.status == main::StackCtxStatus.CTX_DEAD) {
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        return raise_error(interp, "stack overflow in reset");
    }

    if (state.shifted) {
        return jit_apply_value(state.shift_body, state.shift_k, interp);
    }

    Value* v = (Value*)ctx.result;
    main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
    return v;
}
fn Value* jit_shift_value(Interp* interp, Value* body) {
    main::StackCtx* current = main::g_current_stack_ctx;
    if (current == null) return raise_error(interp, "shift outside of reset");

    ResetValueState* rstate = (ResetValueState*)interp.current_reset_state;
    if (rstate == null) return raise_error(interp, "shift outside of reset");

    Continuation* k = interp.alloc_lisp_continuation();
    k.ctx = current;
    k.is_coroutine_based = true;
    Value* k_val = make_continuation(interp, k);

    rstate.shifted = true;
    rstate.shift_body = body;
    rstate.shift_k = k_val;

    main::stack_ctx_suspend();
    return interp.resume_value;
}

fn Value* jit_signal_value(Interp* interp, Value* tag, Value* arg) {
    // Search handler stack
    main::StackCtx* current = main::g_current_stack_ctx;
    if (current != null) {
        for (isz i = (isz)interp.handler_count - 1; i >= 0; i--) {
            EffectHandler* h = &interp.handler_stack[(usz)i];
            for (usz j = 0; j < h.clause_count; j++) {
                if (h.tags[j] == tag.sym_val) {
                    if (h.effect_state != null) {
                        HandleEffectState* hstate = (HandleEffectState*)h.effect_state;
                        hstate.signaled = true;
                        hstate.signal_tag = (SymbolId)tag.sym_val;
                        hstate.signal_arg = arg;
                        main::stack_ctx_suspend();
                        return interp.resume_value;
                    }
                }
            }
        }
    }
    // I/O fast path: look up dispatch table
    for (usz fp_i = 0; fp_i < interp.fast_path_count; fp_i++) {
        if ((uint)interp.fast_path_table[fp_i].tag == (uint)tag.sym_val) {
            Value* fp_prim = interp.fast_path_table[fp_i].prim;
            int fp_arity = fp_prim.prim_val.arity;

            if (fp_arity == 0) {
                EvalResult r = apply_primitive(fp_prim, make_nil(interp), interp);
                if (r.error.has_error) return make_error(interp, r.error.message[:256]);
                return r.value;
            } else if (fp_arity == 1 || fp_arity == -1) {
                EvalResult r = apply_primitive(fp_prim, arg, interp);
                if (r.error.has_error) return make_error(interp, r.error.message[:256]);
                return r.value;
            } else if (arg != null && is_cons(arg)) {
                EvalResult r = apply_primitive(fp_prim, car(arg), interp);
                if (r.error.has_error) return make_error(interp, r.error.message[:256]);
                Value* result = jit_apply_value(r.value, cdr(arg), interp);
                if (result != null && result.tag == ERROR) return result;
                return result;
            }
            break;
        }
    }
    return raise_error(interp, "unhandled effect");
}

fn Value* jit_resolve_value(Interp* interp, Value* k_val, Value* val) {
    if (k_val == null || k_val.tag != CONTINUATION) {
        return raise_error(interp, "resolve: __k is not a valid continuation");
    }

    Continuation* k = k_val.cont_val;

    // Single-shot check
    if (k.ctx == null) {
        return raise_error(interp, "resolve: continuation already resumed (one-shot only)");
    }

    if (!k.is_coroutine_based) {
        return raise_error(interp, "resolve: expected coroutine-based continuation");
    }

    if (k.ctx.status == main::StackCtxStatus.CTX_COMPLETED || k.ctx.status == main::StackCtxStatus.CTX_DEAD) {
        return raise_error(interp, "resolve: continuation context is dead");
    }

    // Reinstall effect handler copy from when the signal occurred
    HandleEffectState* hstate = (HandleEffectState*)k.handle_state;
    if (hstate != null) {
        if (interp.handler_count >= interp.handler_capacity) interp.grow_handler_stack();
        interp.handler_stack[interp.handler_count] = hstate.handler_copy;
        interp.handler_count++;
    }

    // Pass resolve value to suspended context
    interp.resume_value = val;

    // Resume the context
    main::StackContext parent_ctx;
    main::stack_ctx_switch_to(k.ctx, &parent_ctx);

    // Context returned (either completed, signaled again, or died)
    if (hstate != null) {
        interp.handler_count--;
    }

    Value* result;
    if (k.ctx.status == main::StackCtxStatus.CTX_COMPLETED) {
        result = (Value*)k.ctx.result;
    } else {
        result = interp.resume_value;
    }

    // Single-shot: invalidate continuation
    k.ctx = null;
    k.handle_state = null;

    return result;
}

fn Value* jit_handle_value(Interp* interp, Value** tags, Value** closures, usz count, Value* body_closure) {
    main::StackCtx* ctx = main::stack_ctx_create(&interp.stack_ctx_pool);
    if (ctx == null) return raise_error(interp, "stack engine: failed to create context for handle");

    HandleEffectState state;
    state.body = null;
    state.env = null;
    state.interp = interp;
    state.signaled = false;
    state.signal_tag = (SymbolId)0;
    state.signal_arg = null;
    state.is_aot = true;
    state.aot_body = body_closure;

    main::stack_ctx_init(ctx, &handle_effect_entry, &state);

    if (interp.handler_count >= interp.handler_capacity) interp.grow_handler_stack();
    usz my_idx = interp.handler_count;
    EffectHandler* h = &interp.handler_stack[my_idx];
    h.clause_count = count;
    h.handler_env = null;
    h.body_expr = null;
    h.body_env = null;
    h.strict_mode = false;
    h.effect_state = &state;

    h.tags = (SymbolId*)mem::malloc(SymbolId.sizeof * count);
    h.closures = (Value**)mem::malloc(Value*.sizeof * count);
    for (usz i = 0; i < count; i++) {
        h.tags[i] = tags[i].sym_val;
        h.closures[i] = closures[i];
    }
    h.clauses = null;

    interp.handler_count = my_idx + 1;

    state.handler_copy = *h;
    state.handler_idx = my_idx;

    main::StackContext parent_ctx;
    main::stack_ctx_switch_to(ctx, &parent_ctx);

    if (ctx.status == main::StackCtxStatus.CTX_DEAD) {
        interp.handler_count = my_idx;
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        return raise_error(interp, "stack overflow in handle body");
    }

    Value* last_handler_v = null;
    while (state.signaled) {
        state.signaled = false;
        interp.handler_count = my_idx;

        Value* handler_closure = null;
        for (usz j = 0; j < state.handler_copy.clause_count; j++) {
            if ((uint)state.handler_copy.tags[j] == (uint)state.signal_tag) {
                handler_closure = state.handler_copy.closures[j];
                break;
            }
        }

        if (handler_closure == null) {
            main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
            return raise_error(interp, "unhandled effect");
        }

        Continuation* k = interp.alloc_lisp_continuation();
        k.ctx = ctx;
        k.is_coroutine_based = true;
        k.handle_state = &state;
        Value* k_val = make_continuation(interp, k);

        Value* pair = make_cons(interp, k_val, state.signal_arg);

        last_handler_v = jit_apply_value(handler_closure, pair, interp);

        if (ctx.status == main::StackCtxStatus.CTX_COMPLETED) {
            break;
        }
    }

    Value* result;
    if (ctx.status == main::StackCtxStatus.CTX_COMPLETED) {
        result = (Value*)ctx.result;
    } else {
        result = last_handler_v;
    }

    mem::free(state.handler_copy.tags);
    mem::free(state.handler_copy.closures);

    interp.handler_count = my_idx;
    main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);

    return result;
}
