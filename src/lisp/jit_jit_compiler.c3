module lisp;

import std::io;
import std::collections::list;
import main;
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

    // Compile the expression, result goes to R0.
    JitLocals locals = {};
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

        SavedInterpState saved_resolve;
        save_interp_state(interp, &saved_resolve);

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
    SavedInterpState saved;

    save_interp_state(interp, &saved);

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
    if (body_closure != null && body_closure.tag == CLOSURE) {
        jit_warm_expr_cache(body_closure.closure_val.body, interp);
    }
    for (usz i = 0; i < count; i++) {
        if (closures[i] != null && closures[i].tag == CLOSURE) {
            jit_warm_expr_cache(closures[i].closure_val.body, interp);
        }
    }

    main::StackCtx* ctx = main::stack_ctx_create(&interp.stack_ctx_pool);
    if (ctx == null) return raise_error(interp, "stack engine: failed to create context for handle");

    HandleEffectState* state = (HandleEffectState*)mem::malloc(HandleEffectState.sizeof);
    if (state == null) {
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        return raise_error(interp, "handle: failed to allocate state");
    }
    defer mem::free(state);
    state.body = null;
    state.env = null;
    state.interp = interp;
    state.signaled = false;
    state.signal_tag = (SymbolId)0;
    state.signal_arg = null;
    state.is_aot = true;
    state.aot_body = body_closure;

    main::stack_ctx_init(ctx, &handle_effect_entry, state);

    if (interp.handler_count >= interp.handler_capacity) interp.grow_handler_stack();
    usz my_idx = interp.handler_count;
    EffectHandler* h = &interp.handler_stack[my_idx];
    h.clause_count = count;
    h.handler_env = null;
    h.body_expr = null;
    h.body_env = null;
    h.strict_mode = false;
    h.effect_state = state;

    h.tags = (SymbolId*)mem::malloc(SymbolId.sizeof * count);
    h.closures = (Value**)mem::malloc(Value*.sizeof * count);
    if (h.tags == null || h.closures == null) {
        if (h.tags != null) mem::free(h.tags);
        if (h.closures != null) mem::free(h.closures);
        interp.handler_count = my_idx;
        main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);
        return raise_error(interp, "handle: failed to allocate clauses");
    }
    defer mem::free(h.tags);
    defer mem::free(h.closures);
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
        k.handle_state = state;
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

    interp.handler_count = my_idx;
    main::stack_ctx_destroy(ctx, &interp.stack_ctx_pool);

    return result;
}
