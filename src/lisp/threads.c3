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
    // fault: lisp::EXPECTED_INT
    if (args.len < 1 || !is_int(args[0])) return raise_error(interp, "atomic: expected integer");

    AtomicRef* ref = (AtomicRef*)mem::malloc(AtomicRef.sizeof);
    // fault: lisp::TYPE_MISMATCH
    if (ref == null) return raise_error(interp, "atomic: out of memory");
    ref.value.store(args[0].int_val);

    Value* v = make_ffi_handle_ex(interp, (void*)ref, "atomic-ref", null, true, false);
    if (v == null) {
        mem::free(ref);
        return raise_error(interp, "atomic: out of memory");
    }
    return v;
}

fn AtomicRef* get_atomic_ref(Value* v) {
    if (v == null || v.tag != FFI_HANDLE) return null;
    if (v.ffi_val == null) return null;
    return (AtomicRef*)v.ffi_val.lib_handle;
}

fn Value* prim_atomic_add(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 2) return raise_error(interp, "atomic-add!: expected (atomic-add! ref n)");
    AtomicRef* ref = get_atomic_ref(args[0]);
    // fault: lisp::TYPE_MISMATCH
    if (ref == null) return raise_error(interp, "atomic-add!: first arg must be atomic ref");
    // fault: lisp::EXPECTED_INT
    if (!is_int(args[1])) return raise_error(interp, "atomic-add!: second arg must be integer");

    long old = ref.value.add(args[1].int_val);
    return make_int(interp, old);
}

fn Value* prim_atomic_read(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 1) return raise_error(interp, "atomic-read: expected (atomic-read ref)");
    AtomicRef* ref = get_atomic_ref(args[0]);
    // fault: lisp::TYPE_MISMATCH
    if (ref == null) return raise_error(interp, "atomic-read: arg must be atomic ref");

    return make_int(interp, ref.value.load());
}

fn Value* prim_atomic_cas(Value*[] args, Env* env, Interp* interp) {
    // fault: lisp::ARITY_MISMATCH
    if (args.len < 3) return raise_error(interp, "atomic-cas!: expected (atomic-cas! ref old new)");
    AtomicRef* ref = get_atomic_ref(args[0]);
    // fault: lisp::TYPE_MISMATCH
    if (ref == null) return raise_error(interp, "atomic-cas!: first arg must be atomic ref");
    // fault: lisp::EXPECTED_INT
    if (!is_int(args[1]) || !is_int(args[2])) return raise_error(interp, "atomic-cas!: expected integers");

    long expected = args[1].int_val;
    long desired = args[2].int_val;
    // True atomic CAS: single compare-exchange operation.
    long previous = mem::compare_exchange(&ref.value.data, expected, desired);
    if (previous == expected) {
        return interp.global_env.lookup(interp.symbols.intern("true"));
    }
    return make_nil(interp);
}
