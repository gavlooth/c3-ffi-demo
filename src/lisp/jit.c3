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
    return make_nil(interp);
}

fn Value* jit_apply_value(Value* func, Value* arg, Interp* interp) {
    if (func == null) return make_error(interp, "cannot apply null");
    EvalResult r = apply(func, arg, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

fn Value* jit_eval_fallback(Expr* expr, Env* env, Interp* interp) {
    EvalResult r = eval(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
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

    // 3. Evaluate init in the extended env (so it can reference itself)
    EvalResult init_result = eval(expr.let_expr.init, rec_env, interp);
    if (init_result.error.has_error) {
        return make_error(interp, init_result.error.message[:256]);
    }

    // 4. Patch: if closure, set its env to rec_env for self-reference
    Value* actual_value = init_result.value;
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

    // 5. Evaluate body in extended env
    EvalResult body_result = eval(expr.let_expr.body, rec_env, interp);
    if (body_result.error.has_error) {
        return make_error(interp, body_result.error.message[:256]);
    }
    return body_result.value;
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

// define helper — called from JIT-compiled code.
// Stores value in global_env, mirrors eval_define from eval.c3.
fn Value* jit_eval_define(Interp* interp, SymbolId name, Value* value) {
    // Copy to root_region for globals (so value survives temp frame release)
    Value* stored_val = value;
    if (interp.current_frame.region_id != interp.root_region.region_id) {
        stored_val = copy_to_parent(value, interp, interp.root_region);
    }
    interp.global_env.define(name, stored_val);
    return stored_val;
}

// Environment extension helper — called from JIT-compiled code.
// Extends an env with a new binding for capturing let-locals in lambdas.
fn Env* jit_env_extend(Interp* interp, Env* env, SymbolId name, Value* value) {
    return env.extend(interp, name, value);
}

// quasiquote helper — called from JIT-compiled code.
// Delegates to the interpreter's eval_quasiquote with depth=0.
fn Value* jit_eval_quasiquote(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_quasiquote(expr.quasiquote.body, env, interp, 0);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

// match helper — called from JIT-compiled code.
// Delegates to the interpreter's eval() for E_MATCH expressions.
fn Value* jit_eval_match(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

// define-macro helper — called from JIT-compiled code.
// Delegates to the interpreter's eval_define_macro.
fn Value* jit_eval_define_macro(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_define_macro(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

// reset helper — called from JIT-compiled code.
// Delegates to the interpreter's eval_reset.
fn Value* jit_exec_reset(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_reset(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

// shift helper — called from JIT-compiled code.
// Delegates to the interpreter's eval_shift.
fn Value* jit_exec_shift(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_shift(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

// handle helper — called from JIT-compiled code.
// Delegates to the interpreter's eval_handle.
fn Value* jit_exec_handle(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_handle(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
}

// perform helper — called from JIT-compiled code.
// Delegates to the interpreter's eval_perform.
fn Value* jit_exec_perform(Interp* interp, Expr* expr, Env* env) {
    EvalResult r = eval_perform(expr, env, interp);
    if (r.error.has_error) return make_error(interp, r.error.message[:256]);
    return r.value;
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
        EvalResult r = eval(func.closure_val.body, func.closure_val.env, interp);
        if (r.error.has_error) return make_error(interp, r.error.message[:256]);
        return r.value;
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
        // curr is now the rest list — use it directly
        new_env = new_env.extend(interp, func.closure_val.rest_param, curr);
        EvalResult r = eval(func.closure_val.body, new_env, interp);
        if (r.error.has_error) return make_error(interp, r.error.message[:256]);
        return r.value;
    }

    // Direct primitive path for multi-arg primitives
    if (func.tag == PRIMITIVE) {
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

    // Non-variadic: curry through list, detecting variadic mid-chain
    Value* result = func;
    Value* curr = arg_list;
    for (usz i = 0; i < arg_count; i++) {
        if (curr == null || curr.tag != CONS) break;
        Value* arg = curr.cons_val.car;
        curr = curr.cons_val.cdr;

        // Check if result became variadic mid-curry
        if (result.tag == CLOSURE && result.closure_val.has_rest) {
            // Rebuild remaining arg list (curr still has the rest)
            // Include current arg at the front
            Value* remaining = make_cons(interp, arg, curr);
            usz remaining_count = arg_count - i;
            // Recurse with direct variadic path
            return jit_apply_multi_args(interp, result, remaining, remaining_count);
        }

        EvalResult r = apply(result, arg, interp);
        if (r.error.has_error) return make_error(interp, r.error.message[:256]);
        result = r.value;
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
    if (a != null && b != null && a.tag == INT && b.tag == INT) {
        return make_int(interp, a.int_val + b.int_val);
    }
    return make_error(interp, "+: expected integers");
}

fn Value* jit_prim_sub(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null && a.tag == INT && b.tag == INT) {
        return make_int(interp, a.int_val - b.int_val);
    }
    return make_error(interp, "-: expected integers");
}

fn Value* jit_prim_mul(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null && a.tag == INT && b.tag == INT) {
        return make_int(interp, a.int_val * b.int_val);
    }
    return make_error(interp, "*: expected integers");
}

fn Value* jit_prim_lt(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null && a.tag == INT && b.tag == INT) {
        return a.int_val < b.int_val ? make_symbol(interp, interp.sym_true) : make_nil(interp);
    }
    return make_error(interp, "<: expected integers");
}

fn Value* jit_prim_gt(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null && a.tag == INT && b.tag == INT) {
        return a.int_val > b.int_val ? make_symbol(interp, interp.sym_true) : make_nil(interp);
    }
    return make_error(interp, ">: expected integers");
}

fn Value* jit_prim_eq(Interp* interp, Value* a, Value* b) {
    if (a != null && b != null && a.tag == INT && b.tag == INT) {
        return a.int_val == b.int_val ? make_symbol(interp, interp.sym_true) : make_nil(interp);
    }
    return make_error(interp, "=: expected integers");
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

// JIT state cleanup tracking (C9: increased from 64 to 256)
const usz JIT_STATE_POOL_SIZE = 256;
void*[256] g_jit_states;
usz g_jit_state_count = 0;
bool g_jit_pool_warned = false;

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

        default:
            // Unsupported expression types: fall back to interpreter
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

    // Fall back to interpreter global lookup
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);     // arg0 = interp
    _jit_pushargi(s, (long)sym_id, CODE_PUSHARGI_L); // arg1 = symbol id
    _jit_finishi(s, (void*)&jit_lookup_global);
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

        // Build parent env from preceding JIT locals
        bool has_env = emit_build_locals_env(s, interp, locals);

        // Call jit_env_extend(interp, parent_env, name, init_value) -> Env* (the box)
        _jit_prepare(s);
        _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                                    // interp
        if (has_env) {
            _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                                // parent env
        } else {
            _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);                // global env
        }
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
    // Build env from JIT locals into V2 (callee-saved)
    bool has_env = emit_build_locals_env(s, interp, locals);

    // Call jit_make_closure_from_expr(interp, expr, env)
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    if (has_env) {
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                   // arg2 = captured env
    } else {
        _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);  // arg2 = global env
    }
    _jit_finishi(s, (void*)&jit_make_closure_from_expr);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_let_rec(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Build env from JIT locals into V2 (callee-saved)
    bool has_env = emit_build_locals_env(s, interp, locals);

    // Call jit_eval_let_rec(interp, expr, env)
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    if (has_env) {
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                   // arg2 = captured env
    } else {
        _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);  // arg2 = global env
    }
    _jit_finishi(s, (void*)&jit_eval_let_rec);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_set(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
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

    // Build env from JIT locals into V2 (callee-saved)
    bool has_env = emit_build_locals_env(s, interp, locals);

    // Call jit_eval_set(interp, name, value, env)
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                          // arg0 = interp
    _jit_pushargi(s, (long)(uint)expr.set_expr.name, CODE_PUSHARGI_L);  // arg1 = symbol id
    _jit_pushargr(s, JIT_V1, CODE_PUSHARGR_L);                          // arg2 = value
    if (has_env) {
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                      // arg3 = captured env
    } else {
        _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);     // arg3 = global env
    }
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
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    if (has_env) {
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                   // arg2 = captured env
    } else {
        _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);  // arg2 = global env
    }
    _jit_finishi(s, (void*)&jit_eval_quasiquote);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_match(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    if (has_env) {
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                   // arg2 = captured env
    } else {
        _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);  // arg2 = global env
    }
    _jit_finishi(s, (void*)&jit_eval_match);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_define_macro(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    if (has_env) {
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                   // arg2 = captured env
    } else {
        _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);  // arg2 = global env
    }
    _jit_finishi(s, (void*)&jit_eval_define_macro);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_reset(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    if (has_env) {
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                   // arg2 = captured env
    } else {
        _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);  // arg2 = global env
    }
    _jit_finishi(s, (void*)&jit_exec_reset);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_shift(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    if (has_env) {
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                   // arg2 = captured env
    } else {
        _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);  // arg2 = global env
    }
    _jit_finishi(s, (void*)&jit_exec_shift);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_handle(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    if (has_env) {
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                   // arg2 = captured env
    } else {
        _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);  // arg2 = global env
    }
    _jit_finishi(s, (void*)&jit_exec_handle);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_perform(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    bool has_env = emit_build_locals_env(s, interp, locals);
    _jit_prepare(s);
    _jit_pushargr(s, JIT_V0, CODE_PUSHARGR_L);                       // arg0 = interp
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                    // arg1 = expr ptr
    if (has_env) {
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                   // arg2 = captured env
    } else {
        _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);  // arg2 = global env
    }
    _jit_finishi(s, (void*)&jit_exec_perform);
    _jit_retval_l(s, JIT_R0);
    return true;
}

fn bool jit_compile_fallback(void* s, Expr* expr, Interp* interp, JitLocals* locals) {
    // Build env from JIT locals into V2 (callee-saved)
    bool has_env = emit_build_locals_env(s, interp, locals);

    // Fall back to interpreter: jit_eval_fallback(expr, env, interp)
    _jit_prepare(s);
    _jit_pushargi(s, (long)expr, CODE_PUSHARGI_L);                   // arg0 = expr ptr
    if (has_env) {
        _jit_pushargr(s, JIT_V2, CODE_PUSHARGR_L);                   // arg1 = captured env
    } else {
        _jit_pushargi(s, (long)interp.global_env, CODE_PUSHARGI_L);  // arg1 = global env
    }
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
fn bool emit_build_locals_env(void* s, Interp* interp, JitLocals* locals) {
    if (locals.count == 0) return false;

    // Start with global_env in V2
    _jit_new_node_ww(s, CODE_MOVI, JIT_V2, (long)interp.global_env);

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
