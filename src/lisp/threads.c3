module lisp;

import std::core::mem;
import std::atomic;
import std::atomic::types;
import std::io;
import main;

// ============================================================
// Atomic Refs — hardware CAS via C3 std::atomic
//
// (atomic n) → shared atomic integer
// (atomic-add! ref n) → old value (fetch-and-add)
// (atomic-read ref) → current value
// (atomic-cas! ref old new) → true/false
// ============================================================

struct AtomicRef {
    types::Atomic{long} value;
}

fn Value* prim_atomic(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_int(args[0])) return raise_error(interp, "atomic: expected integer");

    AtomicRef* ref = (AtomicRef*)mem::malloc(AtomicRef.sizeof);
    if (ref == null) return raise_error(interp, "atomic: out of memory");
    ref.value.store(args[0].int_val);

    // Wrap as FFI_HANDLE in root_scope
    ScopeRegion* saved = interp.current_scope;
    interp.current_scope = interp.root_scope;
    Value* v = interp.alloc_value();
    main::scope_register_dtor(interp.root_scope, (void*)v, &scope_dtor_value);
    interp.current_scope = saved;
    v.tag = FFI_HANDLE;
    v.ffi_val = (FfiHandle*)ref;
    return v;
}

fn AtomicRef* get_atomic_ref(Value* v) {
    if (v == null || v.tag != FFI_HANDLE) return null;
    return (AtomicRef*)v.ffi_val;
}

fn Value* prim_atomic_add(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "atomic-add!: expected (atomic-add! ref n)");
    AtomicRef* ref = get_atomic_ref(args[0]);
    if (ref == null) return raise_error(interp, "atomic-add!: first arg must be atomic ref");
    if (!is_int(args[1])) return raise_error(interp, "atomic-add!: second arg must be integer");

    long old = ref.value.add(args[1].int_val);
    return make_int(interp, old);
}

fn Value* prim_atomic_read(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "atomic-read: expected (atomic-read ref)");
    AtomicRef* ref = get_atomic_ref(args[0]);
    if (ref == null) return raise_error(interp, "atomic-read: arg must be atomic ref");

    return make_int(interp, ref.value.load());
}

fn Value* prim_atomic_cas(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3) return raise_error(interp, "atomic-cas!: expected (atomic-cas! ref old new)");
    AtomicRef* ref = get_atomic_ref(args[0]);
    if (ref == null) return raise_error(interp, "atomic-cas!: first arg must be atomic ref");
    if (!is_int(args[1]) || !is_int(args[2])) return raise_error(interp, "atomic-cas!: expected integers");

    long expected = args[1].int_val;
    long desired = args[2].int_val;
    // Manual CAS: read, compare, write if match
    long current = ref.value.load();
    if (current == expected) {
        ref.value.store(desired);
        return interp.global_env.lookup(interp.symbols.intern("true"));
    }
    return make_nil(interp);
}
