module lisp::aot;

import std::io;
import lisp;
import main;

// =============================================================================
// AOT Wrapper Module
// =============================================================================
// This module provides a clean API for AOT-compiled code to call the JIT
// infrastructure. Generated C3 code calls aot::* functions instead of
// runtime::* functions.

tlocal lisp::Interp* g_aot_interp = null;

fn void aot_init() {
    main::thread_registry_init();
    g_aot_interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
    g_aot_interp.init();
    lisp::register_primitives(g_aot_interp);
    lisp::register_stdlib(g_aot_interp);
    g_aot_interp.flags.jit_enabled = true;
}

fn void aot_shutdown() {
    if (g_aot_interp != null) {
        g_aot_interp.symbols.destroy();
        g_aot_interp.types.destroy();
        mem::free(g_aot_interp);
        g_aot_interp = null;
    }
    main::thread_registry_shutdown();
}

fn lisp::Interp* aot_interp() @inline {
    return g_aot_interp;
}

// =============================================================================
// Value Constructors
// =============================================================================

fn lisp::Value* make_nil() @inline { return lisp::make_nil(g_aot_interp); }
fn lisp::Value* make_int(long n) @inline { return lisp::make_int(g_aot_interp, n); }
fn lisp::Value* make_double(double d) @inline { return lisp::make_double(g_aot_interp, d); }
fn lisp::Value* make_string(char[] s) @inline { return lisp::make_string(g_aot_interp, s); }
fn lisp::Value* make_symbol(char[] s) @inline {
    lisp::SymbolId sid = g_aot_interp.symbols.intern(s);
    return lisp::make_symbol(g_aot_interp, sid);
}
fn lisp::Value* make_true() @inline { return lisp::make_symbol(g_aot_interp, g_aot_interp.sym_true); }
fn lisp::Value* make_false() @inline { return lisp::make_symbol(g_aot_interp, g_aot_interp.sym_false); }

// =============================================================================
// List Operations
// =============================================================================

fn lisp::Value* cons(lisp::Value* car, lisp::Value* cdr) @inline { return lisp::make_cons(g_aot_interp, car, cdr); }
fn lisp::Value* car(lisp::Value* v) @inline {
    if (v == null || v.tag != lisp::ValueTag.CONS) return lisp::make_nil(g_aot_interp);
    return v.cons_val.car;
}
fn lisp::Value* cdr(lisp::Value* v) @inline {
    if (v == null || v.tag != lisp::ValueTag.CONS) return lisp::make_nil(g_aot_interp);
    return v.cons_val.cdr;
}

// =============================================================================
// Function Invocation
// =============================================================================

// --- Tail-call trampoline state ---
tlocal lisp::Value* g_tail_func = null;
tlocal lisp::Value* g_tail_arg = null;
tlocal long g_tail_argc = 0;
tlocal bool g_tail_is_multi = false;
tlocal bool g_tail_pending = false;

/**
 * invoke_once — Single call without trampoline loop.
 * Used internally by invoke's trampoline.
 */
fn lisp::Value* invoke_once(lisp::Value* func, lisp::Value* arg) @inline {
    // Fast path: AOT closure → call directly, skipping JIT apply indirection
    if (func != null && func.tag == lisp::ValueTag.PRIMITIVE && func.prim_val != null) {
        lisp::PrimitiveFn pfn = func.prim_val.func;
        if (pfn == &aot_closure_apply) {
            AotClosureData* cd = (AotClosureData*)func.prim_val.user_data;
            if (cd != null) return cd.invoke(cd.data, arg);
        }
        if (pfn == &aot_variadic_apply) {
            AotClosureData* cd = (AotClosureData*)func.prim_val.user_data;
            if (cd != null) {
                // Variadic closures expect a proper list — wrap single arg
                lisp::Value* arg_list = lisp::make_cons(g_aot_interp, arg, lisp::make_nil(g_aot_interp));
                return cd.invoke_var(cd.data, arg_list);
            }
        }
    }
    // Fall through to JIT path for non-AOT values (real primitives, JIT closures, continuations)
    return lisp::jit_apply_value(func, arg, g_aot_interp);
}

/**
 * apply_multi_once — Single multi-arg call without trampoline loop.
 * Used internally by apply_multi's trampoline.
 */
fn lisp::Value* apply_multi_once(lisp::Value* func, lisp::Value* arg_list, long argc) @inline {
    // Fast path: AOT closure → call directly, skipping JIT apply_multi indirection
    if (func != null && func.tag == lisp::ValueTag.PRIMITIVE && func.prim_val != null) {
        lisp::PrimitiveFn pfn = func.prim_val.func;
        if (pfn == &aot_variadic_apply) {
            AotClosureData* cd = (AotClosureData*)func.prim_val.user_data;
            if (cd != null) return cd.invoke_var(cd.data, arg_list);
        }
        if (pfn == &aot_closure_apply) {
            // Non-variadic AOT closure called with multi-arg: take first arg
            AotClosureData* cd = (AotClosureData*)func.prim_val.user_data;
            if (cd != null) {
                lisp::Value* first_arg = (arg_list != null && arg_list.tag == lisp::ValueTag.CONS)
                    ? arg_list.cons_val.car : lisp::make_nil(g_aot_interp);
                return cd.invoke(cd.data, first_arg);
            }
        }
    }
    // Fall through to JIT path for non-AOT values
    return lisp::jit_apply_multi_args(g_aot_interp, func, arg_list, (usz)argc);
}

/**
 * invoke — Call a function with trampoline for tail-call optimization.
 * After the initial call, if g_tail_pending is set (by invoke_tail/apply_multi_tail),
 * re-dispatch the stored tail call instead of recursing.
 */
fn lisp::Value* invoke(lisp::Value* func, lisp::Value* arg) {
    lisp::Value* result = invoke_once(func, arg);
    while (g_tail_pending) {
        g_tail_pending = false;
        if (g_tail_is_multi) {
            result = apply_multi_once(g_tail_func, g_tail_arg, g_tail_argc);
        } else {
            result = invoke_once(g_tail_func, g_tail_arg);
        }
    }
    return result;
}

/**
 * apply_multi — Call a function with multiple args, with trampoline for TCO.
 */
fn lisp::Value* apply_multi(lisp::Value* func, lisp::Value* arg_list, long argc) {
    lisp::Value* result = apply_multi_once(func, arg_list, argc);
    while (g_tail_pending) {
        g_tail_pending = false;
        if (g_tail_is_multi) {
            result = apply_multi_once(g_tail_func, g_tail_arg, g_tail_argc);
        } else {
            result = invoke_once(g_tail_func, g_tail_arg);
        }
    }
    return result;
}

/**
 * invoke_tail — Register a tail call (single-arg) and return immediately.
 * The caller's invoke/apply_multi trampoline will dispatch this.
 */
fn lisp::Value* invoke_tail(lisp::Value* func, lisp::Value* arg) @inline {
    g_tail_func = func;
    g_tail_arg = arg;
    g_tail_is_multi = false;
    g_tail_pending = true;
    return lisp::make_nil(g_aot_interp);
}

/**
 * apply_multi_tail — Register a tail call (multi-arg) and return immediately.
 * The caller's invoke/apply_multi trampoline will dispatch this.
 */
fn lisp::Value* apply_multi_tail(lisp::Value* func, lisp::Value* arg_list, long argc) @inline {
    g_tail_func = func;
    g_tail_arg = arg_list;
    g_tail_argc = argc;
    g_tail_is_multi = true;
    g_tail_pending = true;
    return lisp::make_nil(g_aot_interp);
}

fn bool is_truthy(lisp::Value* v) @inline {
    if (v == null) return false;
    if (v.tag == lisp::ValueTag.NIL) return false;
    if (v.tag == lisp::ValueTag.SYMBOL && v.sym_val == g_aot_interp.sym_false) return false;
    return true;
}

fn bool values_equal(lisp::Value* a, lisp::Value* b) {
    return lisp::values_equal(a, b);
}

fn bool is_list(lisp::Value* v) {
    return lisp::is_list(v);
}

fn usz list_length(lisp::Value* v) {
    return lisp::list_length(v);
}

fn lisp::Value* list_nth(lisp::Value* v, long n) {
    return lisp::get_list_nth(v, (usz)n);
}

fn lisp::Value* list_rest(lisp::Value* v, long n) {
    return lisp::get_list_rest(v, (usz)n, g_aot_interp);
}

fn lisp::Value* lookup_prim(char[] name) {
    lisp::SymbolId sid = g_aot_interp.symbols.intern(name);
    lisp::Value* v = g_aot_interp.global_env.lookup(sid);
    if (v != null) return v;
    return lisp::make_nil(g_aot_interp);
}

// =============================================================================
// AOT Closure Creation
// =============================================================================

alias AotClosureFn = fn lisp::Value*(void* data, lisp::Value* arg);
alias AotVariadicFn = fn lisp::Value*(void* data, lisp::Value* arg_list);

struct AotClosureData {
    void*         data;
    AotClosureFn  invoke;
    AotVariadicFn invoke_var;
    bool          is_variadic;
}

fn lisp::Value* aot_closure_apply(lisp::Value*[] args, lisp::Env* env, lisp::Interp* interp) {
    AotClosureData* cd = (AotClosureData*)interp.prim_user_data;
    lisp::Value* arg = args.len > 0 ? args[0] : lisp::make_nil(interp);
    return cd.invoke(cd.data, arg);
}

fn lisp::Value* aot_variadic_apply(lisp::Value*[] args, lisp::Env* env, lisp::Interp* interp) {
    AotClosureData* cd = (AotClosureData*)interp.prim_user_data;
    lisp::Value* list = lisp::make_nil(interp);
    for (isz i = (isz)args.len - 1; i >= 0; i--) list = lisp::make_cons(interp, args[(usz)i], list);
    return cd.invoke_var(cd.data, list);
}

fn lisp::Value* make_closure(void* data, AotClosureFn invoke) {
    AotClosureData* cd = (AotClosureData*)mem::malloc(AotClosureData.sizeof);
    cd.data = data; cd.invoke = invoke; cd.invoke_var = null; cd.is_variadic = false;
    lisp::Value* v = lisp::make_primitive(g_aot_interp, "aot-closure", &aot_closure_apply, 1);
    v.prim_val.user_data = (void*)cd;
    return v;
}

fn lisp::Value* make_variadic_closure(void* data, AotVariadicFn invoke) {
    AotClosureData* cd = (AotClosureData*)mem::malloc(AotClosureData.sizeof);
    cd.data = data; cd.invoke = null; cd.invoke_var = invoke; cd.is_variadic = true;
    lisp::Value* v = lisp::make_primitive(g_aot_interp, "aot-closure-var", &aot_variadic_apply, -1);
    v.prim_val.user_data = (void*)cd;
    return v;
}

// =============================================================================
// Variable Table Operations
// =============================================================================

fn void define_var(char[] name, lisp::Value* val) {
    lisp::SymbolId sid = g_aot_interp.symbols.intern(name);
    g_aot_interp.global_env.define(sid, val);
}

fn lisp::Value* lookup_var(char[] name) {
    lisp::SymbolId sid = g_aot_interp.symbols.intern(name);
    lisp::Value* v = g_aot_interp.global_env.lookup(sid);
    return v != null ? v : lisp::make_nil(g_aot_interp);
}

fn void set_var(char[] name, lisp::Value* val) {
    lisp::SymbolId sid = g_aot_interp.symbols.intern(name);
    // AOT set_var on global env — silently ignore if unbound (define_var should be called first)
    g_aot_interp.global_env.set(sid, val)!!;
}

// =============================================================================
// Output
// =============================================================================

fn void print_value(lisp::Value* v) {
    lisp::print_value(v, &g_aot_interp.symbols);
    io::print("\n");
}

// =============================================================================
// Dict/Array Operations
// =============================================================================

fn lisp::Value* dict_from_args(lisp::Value* arg_list) {
    lisp::Value* dict = lisp::make_hashmap(g_aot_interp, 16);
    lisp::HashMap* map = dict.hashmap_val;
    lisp::Value* curr = arg_list;
    while (curr != null && curr.tag == lisp::ValueTag.CONS) {
        lisp::Value* pair = curr.cons_val.car;
        if (pair != null && pair.tag == lisp::ValueTag.CONS) {
            lisp::Value* key = pair.cons_val.car;
            lisp::Value* val_pair = pair.cons_val.cdr;
            lisp::Value* val = (val_pair != null && val_pair.tag == lisp::ValueTag.CONS) ? val_pair.cons_val.car : lisp::make_nil(g_aot_interp);
            lisp::hashmap_set(map, key, val, g_aot_interp);
        }
        curr = curr.cons_val.cdr;
    }
    return dict;
}

fn lisp::Value* index(lisp::Value* collection, lisp::Value* idx) {
    lisp::Value* ref_fn = g_aot_interp.global_env.lookup(g_aot_interp.symbols.intern("ref"));
    if (ref_fn == null) return lisp::raise_error(g_aot_interp, "ref: primitive not found");
    lisp::Value* partial = lisp::jit_apply_value(ref_fn, collection, g_aot_interp);
    if (partial != null && partial.tag == lisp::ValueTag.ERROR) return partial;
    return lisp::jit_apply_value(partial, idx, g_aot_interp);
}

// =============================================================================
// Effects (Bridge to JIT)
// =============================================================================

/**
 * AOT handle wrapper.
 */
fn lisp::Value* compiled_handle(lisp::Value** tags, lisp::Value** handlers, usz count, lisp::Value* body) {
    return lisp::jit_handle_value(g_aot_interp, tags, handlers, count, body);
}
/**
 * AOT signal wrapper.
 */
fn lisp::Value* compiled_signal(lisp::Value* tag, lisp::Value* arg) {
    return lisp::jit_signal_value(g_aot_interp, tag, arg);
}

/**
 * AOT reset wrapper.
 */
fn lisp::Value* compiled_reset(lisp::Value* body) {
    return lisp::jit_reset_value(g_aot_interp, body);
}

/**
 * AOT shift wrapper.
 */
fn lisp::Value* compiled_shift(lisp::Value* body) {
    return lisp::jit_shift_value(g_aot_interp, body);
}

/**
 * AOT resolve wrapper.
 */
fn lisp::Value* compiled_resolve(lisp::Value* k_val, lisp::Value* val) {
    return lisp::jit_resolve_value(g_aot_interp, k_val, val);
}
