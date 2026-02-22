module lisp::runtime;

import std::io;
import std::collections::list;
import main;

// =============================================================================
// SECTION 1: VALUE REPRESENTATION
// =============================================================================

/**
 * ValueTag — Type tag for runtime values.
 */
enum ValueTag : char {
    V_NIL,
    V_INT,
    V_STRING,
    V_SYMBOL,
    V_CONS,
    V_CLOSURE,
    V_PRIM,
    V_PRIM_UNARY, // Unary primitive (called immediately with one arg)
    V_PARTIAL,    // Partially applied primitive (curried)
    V_TRUE,       // Distinguished true value
    V_FALSE,      // Distinguished false value
    V_CONTINUATION, // Captured delimited continuation
    V_FFI_HANDLE,   // FFI library handle (dlopen result)
    V_THUNK,        // Suspended tail call (for TCO trampoline)
    V_DOUBLE,       // Floating-point number
    V_INTERP_REF,   // Opaque reference to interpreter Value* (for HASHMAP, ARRAY, etc.)
}

/**
 * Closure function signature.
 * Takes closure data pointer and argument, returns result.
 */
alias ClosureFn = fn Value(void* data, Value arg);

/**
 * Primitive function signature.
 * Takes two arguments for binary operations (first may be null for unary).
 */
alias PrimFn = fn Value(Value a, Value b);

/**
 * StringData — Heap-allocated string storage.
 * Allocated in regions.
 */
struct StringData {
    char[4096] chars;
    usz len;
}

/**
 * ConsCell — Pair cell with region-based storage.
 * car and cdr are handles to Value objects in some region.
 */
struct ConsCell {
    main::ObjectHandle car;
    main::ObjectHandle cdr;
}

/**
 * ClosureData — Closure representation.
 * The data pointer points to a closure struct allocated in a region.
 */
struct ClosureData {
    void*               data;         // Captured environment struct (region-allocated)
    ClosureFn           invoke;       // Function pointer
    main::RegionHandle  home_region;  // Region where closure was created
}

/**
 * PartialData — Partially applied primitive.
 * first_arg is a handle to the first argument Value.
 */
struct PartialData {
    PrimFn              func;
    main::ObjectHandle  first_arg;  // Handle to first argument Value
}

/**
 * Value — Tagged union for all runtime values.
 *
 * Design decisions:
 * - Immediates (int, nil, true) are stored inline
 * - Heap data (strings, symbols, cons cells) use ObjectHandles
 * - Closures track their home region for write barrier purposes
 */
struct Value {
    ValueTag tag;

    union {
        long                int_val;      // V_INT - immediate
        main::ObjectHandle  str_handle;   // V_STRING - region-allocated StringData
        main::ObjectHandle  sym_handle;   // V_SYMBOL - region-allocated SymbolData
        ConsCell            cons_val;     // V_CONS - inline with handles to car/cdr
        ClosureData         closure_val;  // V_CLOSURE - inline closure data
        PrimFn              prim_val;     // V_PRIM - function pointer
        PartialData         partial_val;  // V_PARTIAL - inline partial application
        double              double_val;   // V_DOUBLE - floating-point
        void*               ffi_handle;   // V_FFI_HANDLE - dlopen handle
        ThunkData           thunk_val;    // V_THUNK - suspended tail call
        void*               interp_ref;   // V_INTERP_REF - pointer to lisp::Value*
    }
}

/**
 * ThunkData — Suspended tail call for TCO trampoline.
 * Instead of making a recursive call, emit a thunk that rt_invoke will bounce.
 * Arg is stored via ObjectHandle to avoid recursive Value definition.
 */
struct ThunkData {
    ClosureFn           invoke;  // Function to invoke
    void*               data;    // Closure data
    main::ObjectHandle  arg;     // Argument (region-allocated Value)
}

/**
 * SymbolData — Heap-allocated symbol name storage.
 */
struct SymbolData {
    char[256] chars;
    usz len;
}

// =============================================================================
// SECTION 2: RUNTIME INITIALIZATION (Region-Based)
// =============================================================================

/**
 * Thread-local current region for allocations.
 * Objects are allocated in this region by default.
 */
tlocal main::RegionHandle g_current_region;

/**
 * Thread-local root region for global definitions.
 * Global `define`s allocate here so they outlive function calls.
 */
tlocal main::RegionHandle g_root_region;

/**
 * Initialize the runtime with region-based memory management.
 * Must be called once per thread before any runtime operations.
 */
fn void rt_init() {
    main::thread_registry_init();
    g_root_region = main::thread_root_region();
    g_current_region = g_root_region;
}

/**
 * Shutdown the runtime and release all regions.
 * Call before thread exit.
 */
fn void rt_shutdown() {
    main::thread_registry_shutdown();
}

/**
 * Push a new frame region for function calls that create closures.
 * Objects allocated in this frame will be promoted to the parent
 * if they escape (via write barriers).
 *
 * @return Handle to the new frame region
 */
fn main::RegionHandle rt_push_frame() {
    main::RegionHandle child = main::create_region(g_current_region);
    g_current_region = child;
    return child;
}

/**
 * Pop a frame region after a function call completes.
 * Escaped objects are automatically promoted to the parent region.
 *
 * @param frame The frame to pop (for verification)
 */
fn void rt_pop_frame(main::RegionHandle frame) {
    // Restore parent as current region
    main::RegionRegistry* reg = main::thread_registry();
    usz idx = (usz)(uint)frame.region_id;
    main::Region* region = &reg.region_storage[idx];

    // Get parent region handle
    if ((uint)region.parent != (uint)main::NO_PARENT) {
        g_current_region = {
            .region_id = region.parent,
            .generation = reg.region_generations[(usz)(uint)region.parent]
        };
    } else {
        g_current_region = g_root_region;
    }

    // Release the frame (may trigger promotion of escaped objects)
    main::release_region(frame);
}

/**
 * Get the current allocation region.
 */
fn main::RegionHandle rt_current_region() @inline {
    return g_current_region;
}

/**
 * Get the root region for global allocations.
 */
fn main::RegionHandle rt_root_region() @inline {
    return g_root_region;
}

/**
 * Set the current allocation region.
 * Used when allocating in a specific region.
 */
fn void rt_set_current_region(main::RegionHandle region) @inline {
    g_current_region = region;
}

// =============================================================================
// SECTION 3: VALUE CONSTRUCTORS
// =============================================================================

fn Value make_nil() @inline {
    Value v;
    v.tag = V_NIL;
    return v;
}

fn Value make_true() @inline {
    Value v;
    v.tag = V_TRUE;
    return v;
}

fn Value make_false() @inline {
    Value v;
    v.tag = V_FALSE;
    return v;
}

fn Value make_int(long n) @inline {
    Value v;
    v.tag = V_INT;
    v.int_val = n;
    return v;
}

fn Value rt_make_double(double d) @inline {
    Value v;
    v.tag = V_DOUBLE;
    v.double_val = d;
    return v;
}

fn bool rt_is_number(Value v) @inline {
    return v.tag == V_INT || v.tag == V_DOUBLE;
}

fn double rt_to_double(Value v) @inline {
    if (v.tag == V_DOUBLE) return v.double_val;
    return (double)v.int_val;
}

fn Value make_string(char[] s) {
    Value v;
    v.tag = V_STRING;

    // Create StringData in current region
    StringData str;
    usz len = s.len;
    if (len > 4095) len = 4095;
    for (usz i = 0; i < len; i++) {
        str.chars[i] = s[i];
    }
    str.chars[len] = 0;
    str.len = len;

    v.str_handle = main::allocate_in(g_current_region, StringData, str);
    return v;
}

fn Value make_string_in_region(char[] s, main::RegionHandle region) {
    Value v;
    v.tag = V_STRING;

    StringData str;
    usz len = s.len;
    if (len > 4095) len = 4095;
    for (usz i = 0; i < len; i++) {
        str.chars[i] = s[i];
    }
    str.chars[len] = 0;
    str.len = len;

    v.str_handle = main::allocate_in(region, StringData, str);
    return v;
}

fn Value make_symbol(char[] s) {
    Value v;
    v.tag = V_SYMBOL;

    // Create SymbolData in current region
    SymbolData sym;
    usz len = s.len;
    if (len > 4095) len = 4095;
    for (usz i = 0; i < len; i++) {
        sym.chars[i] = s[i];
    }
    sym.chars[len] = 0;
    sym.len = len;

    v.sym_handle = main::allocate_in(g_current_region, SymbolData, sym);
    return v;
}

fn Value make_symbol_in_region(char[] s, main::RegionHandle region) {
    Value v;
    v.tag = V_SYMBOL;

    SymbolData sym;
    usz len = s.len;
    if (len > 4095) len = 4095;
    for (usz i = 0; i < len; i++) {
        sym.chars[i] = s[i];
    }
    sym.chars[len] = 0;
    sym.len = len;

    v.sym_handle = main::allocate_in(region, SymbolData, sym);
    return v;
}

fn Value make_closure(void* data, ClosureFn invoke) @inline {
    Value v;
    v.tag = V_CLOSURE;
    v.closure_val.data = data;
    v.closure_val.invoke = invoke;
    v.closure_val.home_region = g_current_region;
    return v;
}

fn Value make_closure_in_region(void* data, ClosureFn invoke, main::RegionHandle region) @inline {
    Value v;
    v.tag = V_CLOSURE;
    v.closure_val.data = data;
    v.closure_val.invoke = invoke;
    v.closure_val.home_region = region;
    return v;
}

fn Value make_prim(PrimFn func) @inline {
    Value v;
    v.tag = V_PRIM;
    v.prim_val = func;
    return v;
}

fn Value make_prim_unary(PrimFn func) @inline {
    Value v;
    v.tag = V_PRIM_UNARY;
    v.prim_val = func;
    return v;
}

fn Value make_partial(PrimFn func, Value first) @inline {
    Value v;
    v.tag = V_PARTIAL;
    v.partial_val.func = func;

    // Allocate the first argument Value in current region
    v.partial_val.first_arg = main::allocate_in(g_current_region, Value, first);
    return v;
}

fn Value make_thunk(ClosureFn invoke, void* data, Value arg) @inline {
    Value v;
    v.tag = V_THUNK;
    v.thunk_val.invoke = invoke;
    v.thunk_val.data = data;
    v.thunk_val.arg = main::allocate_in(g_current_region, Value, arg);
    return v;
}

fn Value make_partial_in_region(PrimFn func, Value first, main::RegionHandle region) @inline {
    Value v;
    v.tag = V_PARTIAL;
    v.partial_val.func = func;
    v.partial_val.first_arg = main::allocate_in(region, Value, first);
    return v;
}

// =============================================================================
// SECTION 4: CORE RUNTIME FUNCTIONS
// =============================================================================

/**
 * Check if a value is truthy.
 * nil and false are falsy, everything else is truthy.
 */
fn bool rt_is_truthy(Value v) {
    if (v.tag == V_NIL || v.tag == V_FALSE) return false;
    return true;
}

/**
 * Invoke a function with an argument (single call, no trampoline).
 * Returns the result which may be a V_THUNK for tail calls.
 */
fn Value rt_invoke_once(Value func, Value arg) {
    switch (func.tag) {
        case V_CLOSURE:
            return func.closure_val.invoke(func.closure_val.data, arg);

        case V_PRIM:
            // Create partial application for first argument
            return make_partial(func.prim_val, arg);

        case V_PRIM_UNARY:
            // Call unary primitive immediately with dummy second arg
            return func.prim_val(arg, make_nil());

        case V_PARTIAL:
            // Apply second argument to complete the call
            Value* first = main::dereference_as(Value, func.partial_val.first_arg);
            return func.partial_val.func(*first, arg);

        default:
            io::printfn("Error: cannot invoke non-function");
            return make_nil();
    }
}

/**
 * Invoke a function with an argument, with trampoline for TCO.
 * Bounces thunks until a non-thunk result is obtained.
 */
fn Value rt_invoke(Value func, Value arg) {
    Value result = rt_invoke_once(func, arg);
    while (result.tag == V_THUNK) {
        Value* thunk_arg = main::dereference_as(Value, result.thunk_val.arg);
        result = result.thunk_val.invoke(result.thunk_val.data, *thunk_arg);
    }
    return result;
}

/**
 * Apply a function to a cons-list of arguments.
 * Handles variadic closures, primitives, and curried application.
 * This is the multi-arg equivalent of rt_invoke.
 * Includes trampoline for TCO on the final result.
 */
fn Value rt_apply_multi(Value func, Value arg_list, long argc) {
    if (argc == 0) {
        // Zero-arg call
        return rt_invoke(func, make_nil());
    }

    if (argc == 1) {
        // Single arg: extract car and invoke
        if (arg_list.tag == V_CONS) {
            Value* car = main::dereference_as(Value, arg_list.cons_val.car);
            return rt_invoke(func, *car);
        }
        return rt_invoke(func, make_nil());
    }

    // Multi-arg: apply one at a time (currying)
    // Use rt_invoke_once for intermediate applications (no trampoline bounce),
    // use rt_invoke for the last arg (with trampoline for TCO).
    Value result = func;
    Value current = arg_list;
    for (long i = 0; i < argc; i++) {
        if (current.tag != V_CONS) break;
        Value* car = main::dereference_as(Value, current.cons_val.car);
        if (i == argc - 1) {
            // Last arg: use full trampoline
            result = rt_invoke(result, *car);
        } else {
            // Intermediate: no trampoline (partial app shouldn't return thunks)
            result = rt_invoke_once(result, *car);
        }
        Value* cdr = main::dereference_as(Value, current.cons_val.cdr);
        current = *cdr;
    }
    return result;
}

/**
 * Check if two values are equal.
 */
fn bool rt_values_equal(Value a, Value b, usz depth = 0) {
    if (depth >= 256) return false;  // Guard against circular structures
    // Mixed int/double comparison
    if (rt_is_number(a) && rt_is_number(b)) {
        return rt_to_double(a) == rt_to_double(b);
    }
    if (a.tag != b.tag) return false;

    switch (a.tag) {
        case V_NIL:
            return true;
        case V_TRUE:
            return true;
        case V_FALSE:
            return true;
        case V_INT:
            return a.int_val == b.int_val;
        case V_DOUBLE:
            return a.double_val == b.double_val;
        case V_STRING:
            StringData* sa = main::dereference_as(StringData, a.str_handle);
            StringData* sb = main::dereference_as(StringData, b.str_handle);
            if (sa.len != sb.len) return false;
            for (usz i = 0; i < sa.len; i++) {
                if (sa.chars[i] != sb.chars[i]) return false;
            }
            return true;
        case V_SYMBOL:
            SymbolData* sya = main::dereference_as(SymbolData, a.sym_handle);
            SymbolData* syb = main::dereference_as(SymbolData, b.sym_handle);
            if (sya.len != syb.len) return false;
            for (usz i = 0; i < sya.len; i++) {
                if (sya.chars[i] != syb.chars[i]) return false;
            }
            return true;
        case V_CONS:
            Value* car_a = main::dereference_as(Value, a.cons_val.car);
            Value* cdr_a = main::dereference_as(Value, a.cons_val.cdr);
            Value* car_b = main::dereference_as(Value, b.cons_val.car);
            Value* cdr_b = main::dereference_as(Value, b.cons_val.cdr);
            return rt_values_equal(*car_a, *car_b, depth + 1) && rt_values_equal(*cdr_a, *cdr_b, depth + 1);
        case V_INTERP_REF:
            lisp::Value* ia = (lisp::Value*)a.interp_ref;
            lisp::Value* ib = (lisp::Value*)b.interp_ref;
            if (ia == null || ib == null) return ia == ib;
            if (ia.tag != ib.tag) return false;
            if (ia.tag == lisp::ValueTag.HASHMAP) {
                return ia == ib;  // Pointer equality for mutable containers
            }
            if (ia.tag == lisp::ValueTag.ARRAY) {
                if (ia.array_val.length != ib.array_val.length) return false;
                for (usz i = 0; i < ia.array_val.length; i++) {
                    Value ra = interp_to_runtime(ia.array_val.items[i]);
                    Value rb = interp_to_runtime(ib.array_val.items[i]);
                    if (!rt_values_equal(ra, rb, depth + 1)) return false;
                }
                return true;
            }
            return ia == ib;  // Pointer equality for other interp types
        default:
            return false;
    }
}

// =============================================================================
// SECTION 5: LIST OPERATIONS
// =============================================================================

/**
 * Construct a cons cell with region-based storage.
 * The car and cdr Values are allocated in the current region.
 */
fn Value rt_cons(Value car, Value cdr) {
    Value v;
    v.tag = V_CONS;

    // Allocate car and cdr Values in current region
    v.cons_val.car = main::allocate_in(g_current_region, Value, car);
    v.cons_val.cdr = main::allocate_in(g_current_region, Value, cdr);

    return v;
}

/**
 * Construct a cons cell in a specific region.
 */
fn Value rt_cons_in_region(Value car, Value cdr, main::RegionHandle region) {
    Value v;
    v.tag = V_CONS;

    v.cons_val.car = main::allocate_in(region, Value, car);
    v.cons_val.cdr = main::allocate_in(region, Value, cdr);

    return v;
}

/**
 * Append two lists: (append '(a b) '(c d)) => '(a b c d).
 * Copies spine of first list, shares tail with second.
 */
fn Value rt_list_append(Value a, Value b) {
    if (a.tag == V_NIL) return b;
    if (a.tag != V_CONS) return b;

    // Collect elements of first list
    Value[64] items;
    usz count = 0;
    Value cur = a;
    while (cur.tag == V_CONS && count < 64) {
        Value* car = main::dereference_as(Value, cur.cons_val.car);
        items[count] = *car;
        count++;
        Value* cdr = main::dereference_as(Value, cur.cons_val.cdr);
        cur = *cdr;
    }

    // Build result from right: b, then prepend items in reverse
    Value result = b;
    for (usz i = count; i > 0; i--) {
        result = rt_cons(items[i - 1], result);
    }
    return result;
}

/**
 * Check if value is a list (nil or cons chain ending in nil).
 */
fn bool rt_is_list(Value v) {
    while (v.tag == V_CONS) {
        Value* cdr = main::dereference_as(Value, v.cons_val.cdr);
        v = *cdr;
    }
    return v.tag == V_NIL;
}

/**
 * Get list length.
 */
fn usz rt_list_length(Value v) {
    usz len = 0;
    while (v.tag == V_CONS) {
        len++;
        Value* cdr = main::dereference_as(Value, v.cons_val.cdr);
        v = *cdr;
    }
    return len;
}

/**
 * Get nth element of a list.
 */
fn Value rt_list_nth(Value v, usz n) {
    for (usz i = 0; i < n; i++) {
        if (v.tag != V_CONS) return make_nil();
        Value* cdr = main::dereference_as(Value, v.cons_val.cdr);
        v = *cdr;
    }
    if (v.tag != V_CONS) return make_nil();
    Value* car = main::dereference_as(Value, v.cons_val.car);
    return *car;
}

/**
 * Get rest of list starting at index n.
 */
fn Value rt_list_rest(Value v, usz n) {
    for (usz i = 0; i < n; i++) {
        if (v.tag != V_CONS) return make_nil();
        Value* cdr = main::dereference_as(Value, v.cons_val.cdr);
        v = *cdr;
    }
    return v;
}

// =============================================================================
// SECTION 6: ARITHMETIC PRIMITIVES
// =============================================================================

fn Value rt_add(Value a, Value b) {
    if (!rt_is_number(a) || !rt_is_number(b)) return make_nil();
    if (a.tag == V_DOUBLE || b.tag == V_DOUBLE) return rt_make_double(rt_to_double(a) + rt_to_double(b));
    return make_int(a.int_val + b.int_val);
}

fn Value rt_sub(Value a, Value b) {
    if (!rt_is_number(a) || !rt_is_number(b)) return make_nil();
    if (a.tag == V_DOUBLE || b.tag == V_DOUBLE) return rt_make_double(rt_to_double(a) - rt_to_double(b));
    return make_int(a.int_val - b.int_val);
}

fn Value rt_mul(Value a, Value b) {
    if (!rt_is_number(a) || !rt_is_number(b)) return make_nil();
    if (a.tag == V_DOUBLE || b.tag == V_DOUBLE) return rt_make_double(rt_to_double(a) * rt_to_double(b));
    return make_int(a.int_val * b.int_val);
}

fn Value rt_div(Value a, Value b) {
    if (!rt_is_number(a) || !rt_is_number(b)) return make_nil();
    if (a.tag == V_DOUBLE || b.tag == V_DOUBLE) {
        double bd = rt_to_double(b);
        if (bd == 0.0) return make_nil();
        return rt_make_double(rt_to_double(a) / bd);
    }
    if (b.int_val == 0) return make_nil();
    return make_int(a.int_val / b.int_val);
}

fn Value rt_mod(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    if (b.int_val == 0) return make_nil();
    return make_int(a.int_val % b.int_val);
}

// =============================================================================
// SECTION 7: COMPARISON PRIMITIVES
// =============================================================================

fn Value rt_eq(Value a, Value b) {
    return rt_values_equal(a, b) ? make_true() : make_nil();
}

fn Value rt_lt(Value a, Value b) {
    if (!rt_is_number(a) || !rt_is_number(b)) return make_nil();
    return rt_to_double(a) < rt_to_double(b) ? make_true() : make_nil();
}

fn Value rt_gt(Value a, Value b) {
    if (!rt_is_number(a) || !rt_is_number(b)) return make_nil();
    return rt_to_double(a) > rt_to_double(b) ? make_true() : make_nil();
}

fn Value rt_le(Value a, Value b) {
    if (!rt_is_number(a) || !rt_is_number(b)) return make_nil();
    return rt_to_double(a) <= rt_to_double(b) ? make_true() : make_nil();
}

fn Value rt_ge(Value a, Value b) {
    if (!rt_is_number(a) || !rt_is_number(b)) return make_nil();
    return rt_to_double(a) >= rt_to_double(b) ? make_true() : make_nil();
}

// =============================================================================
// SECTION 8: LIST PRIMITIVES
// =============================================================================

fn Value rt_cons_prim(Value a, Value b) {
    return rt_cons(a, b);
}

fn Value rt_car(Value a, Value _unused) {
    if (a.tag != V_CONS) return make_nil();
    Value* car = main::dereference_as(Value, a.cons_val.car);
    return *car;
}

fn Value rt_cdr(Value a, Value _unused) {
    if (a.tag != V_CONS) return make_nil();
    Value* cdr = main::dereference_as(Value, a.cons_val.cdr);
    return *cdr;
}

fn Value rt_null_p(Value a, Value _unused) {
    return a.tag == V_NIL ? make_true() : make_nil();
}

fn Value rt_pair_p(Value a, Value _unused) {
    return a.tag == V_CONS ? make_true() : make_nil();
}

fn Value rt_list(Value a, Value _unused) {
    // Binary runtime primitive: wraps a single element in a list.
    // The interpreter's prim_list handles the variadic case by building
    // proper cons chains from all arguments. This function is only used
    // for the 2-arg runtime calling convention (PrimFn signature).
    return rt_cons(a, make_nil());
}

fn Value rt_length(Value a, Value _unused) {
    return make_int((long)rt_list_length(a));
}

// =============================================================================
// SECTION 9: BOOLEAN PRIMITIVES
// =============================================================================

fn Value rt_not(Value a, Value _unused) {
    return rt_is_truthy(a) ? make_nil() : make_true();
}

// =============================================================================
// SECTION 10: I/O PRIMITIVES
// =============================================================================

fn void rt_print_value(Value v) {
    switch (v.tag) {
        case V_NIL:
            io::print("nil");
        case V_TRUE:
            io::print("true");
        case V_FALSE:
            io::print("false");
        case V_INT:
            io::printf("%d", v.int_val);
        case V_DOUBLE:
            double dv = v.double_val;
            if (dv != dv) {
                io::print("+nan.0");
            } else if (dv == 1.0/0.0) {
                io::print("+inf.0");
            } else if (dv == -1.0/0.0) {
                io::print("-inf.0");
            } else {
                io::printf("%.15g", dv);
                // If printf didn't include a decimal point, add .0
                char[64] dbuf;
                char[] dslice = io::bprintf(&dbuf, "%.15g", dv)!!;
                bool has_dot = false;
                for (usz di = 0; di < dslice.len; di++) {
                    if (dslice[di] == '.' || dslice[di] == 'e' || dslice[di] == 'E') { has_dot = true; break; }
                }
                if (!has_dot) io::print(".0");
            }
        case V_STRING:
            StringData* str = main::dereference_as(StringData, v.str_handle);
            io::printf("\"%s\"", (ZString)&str.chars);
        case V_SYMBOL:
            SymbolData* sym = main::dereference_as(SymbolData, v.sym_handle);
            io::printf("%s", (ZString)&sym.chars);
        case V_CONS:
            io::print("(");
            Value* car = main::dereference_as(Value, v.cons_val.car);
            rt_print_value(*car);
            Value* cdr = main::dereference_as(Value, v.cons_val.cdr);
            Value rest = *cdr;
            while (rest.tag == V_CONS) {
                io::print(" ");
                Value* rest_car = main::dereference_as(Value, rest.cons_val.car);
                rt_print_value(*rest_car);
                Value* rest_cdr = main::dereference_as(Value, rest.cons_val.cdr);
                rest = *rest_cdr;
            }
            if (rest.tag != V_NIL) {
                io::print(" . ");
                rt_print_value(rest);
            }
            io::print(")");
        case V_CLOSURE:
            io::print("#<closure>");
        case V_PRIM:
            io::print("#<primitive>");
        case V_PRIM_UNARY:
            io::print("#<primitive-unary>");
        case V_PARTIAL:
            io::print("#<partial>");
        case V_INTERP_REF:
            // Delegate printing to interpreter's print_value
            rt_ensure_interp();
            lisp::Value* iv = (lisp::Value*)v.interp_ref;
            lisp::print_value(iv, &g_interp.symbols);
        default:
            io::print("#<unknown>");
    }
}

fn Value rt_print(Value a, Value _unused) {
    rt_print_value(a);
    return make_nil();
}

fn Value rt_println(Value a, Value _unused) {
    rt_print_value(a);
    io::printn("");
    return make_nil();
}

// =============================================================================
// SECTION 11: WRITE BARRIER HELPERS
// =============================================================================

/**
 * Mark a Value's handles as escaped if they cross region boundaries.
 * Call this when storing a Value into a closure or other cross-region structure.
 *
 * @param storing_region The region where the Value is being stored
 * @param v The Value being stored
 */
fn void rt_write_barrier_value(main::RegionId storing_region, Value v) {
    switch (v.tag) {
        case V_STRING:
            main::write_barrier(storing_region, v.str_handle);
        case V_SYMBOL:
            main::write_barrier(storing_region, v.sym_handle);
        case V_CONS:
            main::write_barrier(storing_region, v.cons_val.car);
            main::write_barrier(storing_region, v.cons_val.cdr);
        case V_PARTIAL:
            main::write_barrier(storing_region, v.partial_val.first_arg);
        default:
            // Immediates don't need write barriers
            return;
    }
}

/**
 * Convenience function to apply write barrier when capturing a value in a closure.
 *
 * @param closure_region The region where the closure lives
 * @param captured The Value being captured
 */
fn void rt_capture_value(main::RegionHandle closure_region, Value captured) {
    rt_write_barrier_value(closure_region.region_id, captured);
}

// =============================================================================
// SECTION 12: STRING PRIMITIVES
// =============================================================================

fn Value rt_string_append(Value a, Value b) {
    if (a.tag != V_STRING || b.tag != V_STRING) return make_nil();
    StringData* sa = main::dereference_as(StringData, a.str_handle);
    StringData* sb = main::dereference_as(StringData, b.str_handle);

    StringData result;
    usz total = sa.len + sb.len;
    if (total > 4095) total = 4095;
    usz i = 0;
    for (usz j = 0; j < sa.len && i < 4095; j++) {
        result.chars[i++] = sa.chars[j];
    }
    for (usz j = 0; j < sb.len && i < 4095; j++) {
        result.chars[i++] = sb.chars[j];
    }
    result.chars[i] = 0;
    result.len = i;

    Value v;
    v.tag = V_STRING;
    v.str_handle = main::allocate_in(g_current_region, StringData, result);
    return v;
}

fn Value rt_string_join(Value sep, Value lst) {
    if (sep.tag != V_STRING) return make_nil();
    StringData* sep_str = main::dereference_as(StringData, sep.str_handle);

    StringData result;
    usz pos = 0;
    bool first = true;
    Value current = lst;

    while (current.tag == V_CONS) {
        if (!first && pos + sep_str.len < 255) {
            for (usz i = 0; i < sep_str.len && pos < 4095; i++) {
                result.chars[pos++] = sep_str.chars[i];
            }
        }
        first = false;

        Value* elem = main::dereference_as(Value, current.cons_val.car);
        if (elem.tag == V_STRING) {
            StringData* s = main::dereference_as(StringData, elem.str_handle);
            for (usz i = 0; i < s.len && pos < 4095; i++) {
                result.chars[pos++] = s.chars[i];
            }
        }

        Value* next = main::dereference_as(Value, current.cons_val.cdr);
        current = *next;
    }

    result.chars[pos] = 0;
    result.len = pos;

    Value v;
    v.tag = V_STRING;
    v.str_handle = main::allocate_in(g_current_region, StringData, result);
    return v;
}

/**
 * Substring — 3-arg curried primitive: (substring str start end)
 * The compiler curries as (((substring str) start) end).
 * Step 1: (substring str) → partial(rt_substring_step1, str)
 * Step 2: (partial start) → rt_substring_step1(str, start) → closure expecting end
 * Step 3: (closure end) → performs the actual substring extraction
 */
struct SubstringCapture {
    Value str;
    Value start;
}

fn Value rt_substring_end_fn(void* data, Value end_val) {
    SubstringCapture* cap = (SubstringCapture*)data;
    if (cap.str.tag != V_STRING || cap.start.tag != V_INT) return make_nil();
    if (end_val.tag != V_INT) return make_nil();

    StringData* s = main::dereference_as(StringData, cap.str.str_handle);
    long start = cap.start.int_val;
    long end = end_val.int_val;

    // Bounds checking
    if (start < 0) start = (long)s.len + start;
    if (start < 0) start = 0;
    if (start > (long)s.len) start = (long)s.len;
    if (end < 0) end = (long)s.len + end;
    if (end < 0) end = 0;
    if (end > (long)s.len) end = (long)s.len;
    if (end < start) end = start;

    StringData result;
    usz len = (usz)(end - start);
    if (len > 4095) len = 4095;
    for (usz i = 0; i < len; i++) {
        result.chars[i] = s.chars[(usz)start + i];
    }
    result.chars[len] = 0;
    result.len = len;

    Value v;
    v.tag = V_STRING;
    v.str_handle = main::allocate_in(g_current_region, StringData, result);
    return v;
}

fn Value rt_substring_step1(Value str, Value start_v) {
    // Create a closure capturing str and start, expecting end
    SubstringCapture* cap = (SubstringCapture*)mem::malloc(SubstringCapture.sizeof);
    cap.str = str;
    cap.start = start_v;
    return make_closure(cap, &rt_substring_end_fn);
}

fn Value rt_string_split(Value str, Value sep) {
    if (str.tag != V_STRING || sep.tag != V_STRING) return make_nil();
    StringData* s = main::dereference_as(StringData, str.str_handle);
    StringData* d = main::dereference_as(StringData, sep.str_handle);
    if (d.len == 0) return make_nil();

    usz dlen = d.len;
    Value result = make_nil();

    // Scan forward, split at delimiter occurrences
    usz[256] starts;  // start indices of each segment
    usz[256] lens;    // lengths of each segment
    usz seg_count = 0;
    usz seg_start = 0;  // start of current segment
    usz pos = 0;

    while (pos + dlen <= s.len) {
        // Check if delimiter matches at pos
        bool match = true;
        for (usz k = 0; k < dlen; k++) {
            if (s.chars[pos + k] != d.chars[k]) {
                match = false;
                break;
            }
        }

        if (match) {
            if (seg_count < 256) {
                starts[seg_count] = seg_start;
                lens[seg_count] = pos - seg_start;
                seg_count++;
            }
            pos += dlen;
            seg_start = pos;
        } else {
            pos++;
        }
    }

    // Final segment (from last delimiter to end of string)
    if (seg_count < 256) {
        starts[seg_count] = seg_start;
        lens[seg_count] = s.len - seg_start;
        seg_count++;
    }

    // Build list in reverse order so cons produces correct order
    for (usz i = seg_count; i > 0; i--) {
        usz idx = i - 1;
        usz start = starts[idx];
        usz len = lens[idx];
        if (len > 4095) { len = 4095; }

        StringData part;
        for (usz j = 0; j < len; j++) {
            part.chars[j] = s.chars[start + j];
        }
        part.chars[len] = 0;
        part.len = len;

        Value part_val;
        part_val.tag = V_STRING;
        part_val.str_handle = main::allocate_in(g_current_region, StringData, part);
        result = rt_cons(part_val, result);
    }

    return result;
}

fn Value rt_string_length(Value a, Value _unused) {
    if (a.tag != V_STRING) return make_nil();
    StringData* s = main::dereference_as(StringData, a.str_handle);
    return make_int((long)s.len);
}

fn Value rt_string_to_list(Value a, Value _unused) {
    if (a.tag != V_STRING) return make_nil();
    StringData* s = main::dereference_as(StringData, a.str_handle);

    Value result = make_nil();
    for (isz i = (isz)s.len - 1; i >= 0; i--) {
        StringData ch;
        ch.chars[0] = s.chars[(usz)i];
        ch.chars[1] = 0;
        ch.len = 1;

        Value char_val;
        char_val.tag = V_STRING;
        char_val.str_handle = main::allocate_in(g_current_region, StringData, ch);
        result = rt_cons(char_val, result);
    }
    return result;
}

fn Value rt_list_to_string(Value a, Value _unused) {
    StringData result;
    usz pos = 0;

    Value current = a;
    while (current.tag == V_CONS) {
        Value* elem = main::dereference_as(Value, current.cons_val.car);
        if (elem.tag == V_STRING) {
            StringData* s = main::dereference_as(StringData, elem.str_handle);
            for (usz i = 0; i < s.len && pos < 4095; i++) {
                result.chars[pos++] = s.chars[i];
            }
        } else if (elem.tag == V_INT) {
            if (pos < 4095) {
                result.chars[pos++] = (char)elem.int_val;
            }
        }
        Value* next = main::dereference_as(Value, current.cons_val.cdr);
        current = *next;
    }

    result.chars[pos] = 0;
    result.len = pos;

    Value v;
    v.tag = V_STRING;
    v.str_handle = main::allocate_in(g_current_region, StringData, result);
    return v;
}

fn Value rt_string_upcase(Value a, Value _unused) {
    if (a.tag != V_STRING) return make_nil();
    StringData* s = main::dereference_as(StringData, a.str_handle);

    StringData result;
    for (usz i = 0; i < s.len; i++) {
        char c = s.chars[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        result.chars[i] = c;
    }
    result.chars[s.len] = 0;
    result.len = s.len;

    Value v;
    v.tag = V_STRING;
    v.str_handle = main::allocate_in(g_current_region, StringData, result);
    return v;
}

fn Value rt_string_downcase(Value a, Value _unused) {
    if (a.tag != V_STRING) return make_nil();
    StringData* s = main::dereference_as(StringData, a.str_handle);

    StringData result;
    for (usz i = 0; i < s.len; i++) {
        char c = s.chars[i];
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        result.chars[i] = c;
    }
    result.chars[s.len] = 0;
    result.len = s.len;

    Value v;
    v.tag = V_STRING;
    v.str_handle = main::allocate_in(g_current_region, StringData, result);
    return v;
}

fn Value rt_string_trim(Value a, Value _unused) {
    if (a.tag != V_STRING) return make_nil();
    StringData* s = main::dereference_as(StringData, a.str_handle);

    usz start = 0;
    while (start < s.len && (s.chars[start] == ' ' || s.chars[start] == '\t' ||
           s.chars[start] == '\n' || s.chars[start] == '\r')) {
        start++;
    }

    usz end = s.len;
    while (end > start && (s.chars[end-1] == ' ' || s.chars[end-1] == '\t' ||
           s.chars[end-1] == '\n' || s.chars[end-1] == '\r')) {
        end--;
    }

    StringData result;
    usz len = end - start;
    for (usz i = 0; i < len; i++) {
        result.chars[i] = s.chars[start + i];
    }
    result.chars[len] = 0;
    result.len = len;

    Value v;
    v.tag = V_STRING;
    v.str_handle = main::allocate_in(g_current_region, StringData, result);
    return v;
}

fn Value rt_string_p(Value a, Value _unused) {
    return a.tag == V_STRING ? make_true() : make_nil();
}

// =============================================================================
// SECTION 13: TYPE PREDICATES
// =============================================================================

fn Value rt_int_p(Value a, Value _unused) {
    return a.tag == V_INT ? make_true() : make_nil();
}

fn Value rt_symbol_p(Value a, Value _unused) {
    return a.tag == V_SYMBOL ? make_true() : make_nil();
}

fn Value rt_closure_p(Value a, Value _unused) {
    return a.tag == V_CLOSURE ? make_true() : make_nil();
}

fn Value rt_continuation_p(Value a, Value _unused) {
    return a.tag == V_CONTINUATION ? make_true() : make_nil();
}

fn Value rt_double_p(Value a, Value _unused) {
    return a.tag == V_DOUBLE ? make_true() : make_nil();
}

fn Value rt_list_p(Value a, Value _unused) {
    return (a.tag == V_CONS || a.tag == V_NIL) ? make_true() : make_nil();
}

fn Value rt_boolean_p(Value a, Value _unused) {
    return (a.tag == V_TRUE || a.tag == V_FALSE) ? make_true() : make_nil();
}

fn Value rt_number_p(Value a, Value _unused) {
    return (a.tag == V_INT || a.tag == V_DOUBLE) ? make_true() : make_nil();
}

// =============================================================================
// SECTION 13b: TYPE-OF
// =============================================================================

fn Value rt_type_of(Value a, Value _unused) {
    switch (a.tag) {
        case V_INT:          return make_symbol("Int");
        case V_DOUBLE:       return make_symbol("Double");
        case V_STRING:       return make_symbol("String");
        case V_SYMBOL:       return make_symbol("Symbol");
        case V_CONS:         return make_symbol("List");
        case V_CLOSURE:      return make_symbol("Closure");
        case V_PRIM:         return make_symbol("Closure");
        case V_PRIM_UNARY:   return make_symbol("Closure");
        case V_PARTIAL:      return make_symbol("Closure");
        case V_TRUE:         return make_symbol("Bool");
        case V_FALSE:        return make_symbol("Bool");
        case V_NIL:          return make_symbol("Nil");
        case V_CONTINUATION: return make_symbol("Continuation");
        case V_FFI_HANDLE:   return make_symbol("FFIHandle");
        case V_INTERP_REF:
            // Delegate to interpreter for HASHMAP/ARRAY/INSTANCE
            lisp::Value* iv = (lisp::Value*)a.interp_ref;
            if (iv != null) {
                if (iv.tag == lisp::ValueTag.HASHMAP) return make_symbol("Dict");
                if (iv.tag == lisp::ValueTag.ARRAY) return make_symbol("Array");
                if (iv.tag == lisp::ValueTag.INSTANCE) return make_symbol("Instance");
            }
            return make_symbol("Any");
        default:             return make_symbol("Any");
    }
}

// =============================================================================
// SECTION 13c: MATH PRIMITIVES
// =============================================================================

extern fn double c_sqrt(double x) @extern("sqrt");
extern fn double c_floor(double x) @extern("floor");
extern fn double c_ceil(double x) @extern("ceil");
extern fn double c_round(double x) @extern("round");
extern fn double c_fabs(double x) @extern("fabs");

fn Value rt_abs(Value a, Value _unused) {
    if (a.tag == V_DOUBLE) return rt_make_double(c_fabs(a.double_val));
    if (a.tag == V_INT) {
        long n = a.int_val;
        if (n == long.min) return make_int(long.max);  // Avoid -long.min overflow
        return make_int(n < 0 ? -n : n);
    }
    return make_nil();
}

fn Value rt_min(Value a, Value b) {
    if (a.tag == V_DOUBLE || b.tag == V_DOUBLE) {
        double da = rt_to_double(a); double db = rt_to_double(b);
        return rt_make_double(da < db ? da : db);
    }
    return make_int(a.int_val < b.int_val ? a.int_val : b.int_val);
}

fn Value rt_max(Value a, Value b) {
    if (a.tag == V_DOUBLE || b.tag == V_DOUBLE) {
        double da = rt_to_double(a); double db = rt_to_double(b);
        return rt_make_double(da > db ? da : db);
    }
    return make_int(a.int_val > b.int_val ? a.int_val : b.int_val);
}

fn Value rt_floor(Value a, Value _unused) {
    return make_int((long)c_floor(rt_to_double(a)));
}

fn Value rt_ceiling(Value a, Value _unused) {
    return make_int((long)c_ceil(rt_to_double(a)));
}

fn Value rt_round(Value a, Value _unused) {
    return make_int((long)c_round(rt_to_double(a)));
}

fn Value rt_truncate(Value a, Value _unused) {
    return make_int((long)rt_to_double(a));
}

fn Value rt_sqrt(Value a, Value _unused) {
    return rt_make_double(c_sqrt(rt_to_double(a)));
}

fn Value rt_even_p(Value a, Value _unused) {
    if (a.tag != V_INT) return make_nil();
    return (a.int_val % 2 == 0) ? make_true() : make_nil();
}

fn Value rt_odd_p(Value a, Value _unused) {
    if (a.tag != V_INT) return make_nil();
    return (a.int_val % 2 != 0) ? make_true() : make_nil();
}

fn Value rt_zero_p(Value a, Value _unused) {
    if (a.tag == V_INT) return a.int_val == 0 ? make_true() : make_nil();
    if (a.tag == V_DOUBLE) return a.double_val == 0.0 ? make_true() : make_nil();
    return make_nil();
}

fn Value rt_positive_p(Value a, Value _unused) {
    if (a.tag == V_INT) return a.int_val > 0 ? make_true() : make_nil();
    if (a.tag == V_DOUBLE) return a.double_val > 0.0 ? make_true() : make_nil();
    return make_nil();
}

fn Value rt_negative_p(Value a, Value _unused) {
    if (a.tag == V_INT) return a.int_val < 0 ? make_true() : make_nil();
    if (a.tag == V_DOUBLE) return a.double_val < 0.0 ? make_true() : make_nil();
    return make_nil();
}

fn Value rt_gcd(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    long x = a.int_val; long y = b.int_val;
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    while (y != 0) {
        long t = y;
        y = x % y;
        x = t;
    }
    return make_int(x);
}

// =============================================================================
// SECTION 13d: BITWISE OPS
// =============================================================================

fn Value rt_bitwise_and(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    return make_int(a.int_val & b.int_val);
}

fn Value rt_bitwise_or(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    return make_int(a.int_val | b.int_val);
}

fn Value rt_bitwise_xor(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    return make_int(a.int_val ^ b.int_val);
}

fn Value rt_bitwise_not(Value a, Value _unused) {
    if (a.tag != V_INT) return make_nil();
    return make_int(~a.int_val);
}

fn Value rt_lshift(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    long shift = b.int_val;
    if (shift < 0 || shift >= 64) return make_int(0);
    return make_int(a.int_val << shift);
}

fn Value rt_rshift(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    long shift = b.int_val;
    if (shift < 0 || shift >= 64) return make_int(0);
    return make_int(a.int_val >> shift);
}

fn Value rt_newline(Value _a, Value _unused) {
    io::printn("");
    return make_nil();
}

// =============================================================================
// SECTION 14: INDEX AND FIELD ACCESS
// =============================================================================

fn Value rt_index(Value collection, Value index) {
    if (collection.tag == V_CONS && index.tag == V_INT) {
        return rt_list_nth(collection, (usz)index.int_val);
    }
    if (collection.tag == V_STRING && index.tag == V_INT) {
        StringData* s = main::dereference_as(StringData, collection.str_handle);
        usz idx = (usz)index.int_val;
        if (idx < s.len) {
            return make_int((long)s.chars[idx]);
        }
        return make_nil();
    }
    return make_nil();
}

fn Value rt_field_access(Value obj, Value field_name) {
    // Field access for path notation: obj.field
    // Objects are represented as association lists: ((name . value) (name . value) ...)
    if (obj.tag != V_CONS || field_name.tag != V_STRING) return make_nil();

    StringData* field = main::dereference_as(StringData, field_name.str_handle);

    Value current = obj;
    while (current.tag == V_CONS) {
        Value* pair = main::dereference_as(Value, current.cons_val.car);
        if (pair.tag == V_CONS) {
            Value* key = main::dereference_as(Value, pair.cons_val.car);
            if (key.tag == V_SYMBOL) {
                SymbolData* sym = main::dereference_as(SymbolData, key.sym_handle);
                if (sym.len == field.len) {
                    bool match = true;
                    for (usz i = 0; i < sym.len; i++) {
                        if (sym.chars[i] != field.chars[i]) { match = false; break; }
                    }
                    if (match) {
                        Value* val = main::dereference_as(Value, pair.cons_val.cdr);
                        return *val;
                    }
                }
            }
        }
        Value* next = main::dereference_as(Value, current.cons_val.cdr);
        current = *next;
    }
    return make_nil();
}

// =============================================================================
// SECTION 15: NATIVE COMPILED EFFECTS (reset/shift/handle/perform)
// =============================================================================
// Self-contained effect mechanism for AOT-compiled code.
// Uses replay-based continuations: body closures are re-invoked with substitution.

const usz RT_MAX_HANDLERS = 16;
const usz RT_MAX_CLAUSES = 8;
const usz RT_MAX_PRIOR = 16;

struct RtEffectClause {
    Value tag;                    // V_SYMBOL tag
    Value handler_closure;        // V_CLOSURE receiving (cons k arg)
}

struct RtEffectHandler {
    RtEffectClause[RT_MAX_CLAUSES] clauses;
    usz clause_count;
    Value body_closure;           // Handle body closure (for continuation capture)
}

struct RtCapturedCont {
    Value body_closure;           // Body closure to replay
    bool is_effect;
    Value effect_tag;             // V_SYMBOL for effect replay
    RtEffectHandler handler_copy; // Handler to reinstall during effect replay
    bool has_handler;
    usz perform_index;
    Value[RT_MAX_PRIOR] prior_results;
    usz prior_count;
    usz shift_index;
    Value[RT_MAX_PRIOR] shift_prior_results;
    usz shift_prior_count;
}

// Thread-local effect state
tlocal usz rt_reset_depth = 0;
tlocal bool rt_shift_occurred = false;
tlocal usz rt_shift_target_depth = 0;
tlocal Value rt_shift_result_val;

tlocal bool rt_cont_sub = false;           // In replay mode?
tlocal Value rt_cont_sub_val;              // Substitution value
tlocal bool rt_cont_is_eff = false;        // Effect replay?
tlocal Value rt_cont_eff_tag;              // Effect tag for replay

tlocal RtEffectHandler[RT_MAX_HANDLERS] rt_handler_stack;
tlocal usz rt_handler_count = 0;
tlocal bool rt_effect_occurred = false;
tlocal Value rt_effect_result_val;

tlocal Value[RT_MAX_HANDLERS] rt_reset_body_stack;  // Body closures

// Multi-perform/shift replay tracking
tlocal usz rt_eff_perform_ctr = 0;
tlocal Value[RT_MAX_PRIOR] rt_eff_prior;
tlocal usz rt_cont_target_perform = 0;
tlocal usz rt_eff_shift_ctr = 0;
tlocal Value[RT_MAX_PRIOR] rt_shift_prior;
tlocal usz rt_cont_target_shift = 0;

/**
 * rt_compiled_reset — Evaluate body closure within a reset delimiter.
 * Body is a V_CLOSURE called with make_nil() arg.
 */
fn Value rt_compiled_reset(Value body_closure) {
    usz saved_shift_ctr = rt_eff_shift_ctr;
    rt_eff_shift_ctr = 0;

    if (rt_reset_depth >= RT_MAX_HANDLERS) {
        io::printfn("Error: reset stack overflow");
        return make_nil();
    }
    rt_reset_depth++;
    rt_reset_body_stack[rt_reset_depth - 1] = body_closure;

    Value result = body_closure.closure_val.invoke(body_closure.closure_val.data, make_nil());
    // Trampoline
    while (result.tag == V_THUNK) {
        Value* ta = main::dereference_as(Value, result.thunk_val.arg);
        result = result.thunk_val.invoke(result.thunk_val.data, *ta);
    }

    rt_eff_shift_ctr = saved_shift_ctr;
    rt_reset_depth--;

    if (rt_shift_occurred && rt_reset_depth == rt_shift_target_depth) {
        rt_shift_occurred = false;
        return rt_shift_result_val;
    }

    return result;
}

/**
 * rt_compiled_shift — Capture continuation and evaluate k-body.
 * k_body_closure is a V_CLOSURE called with continuation as arg.
 */
fn Value rt_compiled_shift(Value k_body_closure) {
    // REPLAY CHECK
    if (rt_cont_sub && !rt_cont_is_eff) {
        usz current = rt_eff_shift_ctr;
        rt_eff_shift_ctr++;
        if (current < rt_cont_target_shift) {
            return rt_shift_prior[current];
        }
        if (current == rt_cont_target_shift) {
            rt_cont_sub = false;
            if (current < RT_MAX_PRIOR) {
                rt_shift_prior[current] = rt_cont_sub_val;
            }
            return rt_cont_sub_val;
        }
    }

    if (rt_reset_depth == 0) {
        io::printfn("Error: shift outside of reset");
        return make_nil();
    }

    // Capture continuation
    RtCapturedCont* cc = (RtCapturedCont*)mem::malloc(RtCapturedCont.sizeof);
    cc.body_closure = rt_reset_body_stack[rt_reset_depth - 1];
    cc.is_effect = false;
    cc.has_handler = false;
    cc.shift_index = rt_eff_shift_ctr;
    cc.shift_prior_count = rt_eff_shift_ctr;
    for (usz i = 0; i < cc.shift_prior_count && i < RT_MAX_PRIOR; i++) {
        cc.shift_prior_results[i] = rt_shift_prior[i];
    }
    rt_eff_shift_ctr++;

    // Create continuation closure
    Value k_val = make_closure((void*)cc, &invoke_compiled_continuation);

    // Call k-body with continuation
    Value body_result = k_body_closure.closure_val.invoke(k_body_closure.closure_val.data, k_val);
    while (body_result.tag == V_THUNK) {
        Value* ta = main::dereference_as(Value, body_result.thunk_val.arg);
        body_result = body_result.thunk_val.invoke(body_result.thunk_val.data, *ta);
    }

    rt_shift_occurred = true;
    rt_shift_target_depth = rt_reset_depth - 1;
    rt_shift_result_val = body_result;

    return body_result;
}

/**
 * rt_compiled_handle — Evaluate body with effect handlers installed.
 * tags[i] is V_SYMBOL, handlers[i] is V_CLOSURE receiving (cons k arg).
 */
fn Value rt_compiled_handle(Value* tags, Value* handlers, usz clause_count, Value body_closure) {
    if (rt_handler_count >= RT_MAX_HANDLERS) {
        io::printfn("Error: handler stack overflow");
        return make_nil();
    }

    RtEffectHandler* h = &rt_handler_stack[rt_handler_count];
    h.clause_count = clause_count;
    h.body_closure = body_closure;
    for (usz i = 0; i < clause_count && i < RT_MAX_CLAUSES; i++) {
        h.clauses[i].tag = tags[i];
        h.clauses[i].handler_closure = handlers[i];
    }

    usz saved_perform_ctr = rt_eff_perform_ctr;
    rt_eff_perform_ctr = 0;
    rt_handler_count++;

    Value result = body_closure.closure_val.invoke(body_closure.closure_val.data, make_nil());
    while (result.tag == V_THUNK) {
        Value* ta = main::dereference_as(Value, result.thunk_val.arg);
        result = result.thunk_val.invoke(result.thunk_val.data, *ta);
    }

    rt_eff_perform_ctr = saved_perform_ctr;

    if (rt_effect_occurred) {
        rt_effect_occurred = false;
        return rt_effect_result_val;
    }

    rt_handler_count--;
    return result;
}

/**
 * rt_compiled_perform — Dispatch an effect.
 * Searches handler stack, captures continuation, calls handler.
 * I/O fast path when no handler installed.
 */
fn Value rt_compiled_perform(Value tag, Value arg) {
    // REPLAY CHECK
    if (rt_cont_sub && rt_cont_is_eff) {
        // Check if tags match
        if (tag.tag == V_SYMBOL && rt_cont_eff_tag.tag == V_SYMBOL) {
            SymbolData* t1 = main::dereference_as(SymbolData, tag.sym_handle);
            SymbolData* t2 = main::dereference_as(SymbolData, rt_cont_eff_tag.sym_handle);
            bool match = (t1.len == t2.len);
            if (match) {
                for (usz c = 0; c < t1.len; c++) {
                    if (t1.chars[c] != t2.chars[c]) { match = false; break; }
                }
            }
            if (match) {
                usz current = rt_eff_perform_ctr;
                rt_eff_perform_ctr++;
                if (current < rt_cont_target_perform) {
                    return rt_eff_prior[current];
                }
                if (current == rt_cont_target_perform) {
                    rt_cont_sub = false;
                    if (current < RT_MAX_PRIOR) {
                        rt_eff_prior[current] = rt_cont_sub_val;
                    }
                    return rt_cont_sub_val;
                }
            }
        }
    }

    // Search handler stack for matching handler
    for (isz i = (isz)rt_handler_count - 1; i >= 0; i--) {
        RtEffectHandler* h = &rt_handler_stack[(usz)i];
        for (usz j = 0; j < h.clause_count; j++) {
            // Compare tag symbols
            if (tag.tag != V_SYMBOL || h.clauses[j].tag.tag != V_SYMBOL) continue;
            SymbolData* t1 = main::dereference_as(SymbolData, tag.sym_handle);
            SymbolData* t2 = main::dereference_as(SymbolData, h.clauses[j].tag.sym_handle);
            bool match = (t1.len == t2.len);
            if (match) {
                for (usz c = 0; c < t1.len; c++) {
                    if (t1.chars[c] != t2.chars[c]) { match = false; break; }
                }
            }
            if (!match) continue;

            // Found matching handler — capture continuation
            RtCapturedCont* cc = (RtCapturedCont*)mem::malloc(RtCapturedCont.sizeof);
            cc.body_closure = h.body_closure;
            cc.is_effect = true;
            cc.effect_tag = tag;
            cc.handler_copy = *h;
            cc.has_handler = true;
            cc.perform_index = rt_eff_perform_ctr;
            cc.prior_count = rt_eff_perform_ctr;
            for (usz pi = 0; pi < cc.prior_count && pi < RT_MAX_PRIOR; pi++) {
                cc.prior_results[pi] = rt_eff_prior[pi];
            }
            rt_eff_perform_ctr++;

            Value k_val = make_closure((void*)cc, &invoke_compiled_continuation);

            rt_handler_count--;

            // Call handler with (cons k arg)
            Value pair = rt_cons(k_val, arg);
            Value handler_result = h.clauses[j].handler_closure.closure_val.invoke(
                h.clauses[j].handler_closure.closure_val.data, pair);
            while (handler_result.tag == V_THUNK) {
                Value* ta = main::dereference_as(Value, handler_result.thunk_val.arg);
                handler_result = handler_result.thunk_val.invoke(handler_result.thunk_val.data, *ta);
            }

            rt_effect_occurred = true;
            rt_effect_result_val = handler_result;
            return handler_result;
        }
    }

    // I/O fast path: no handler found → direct I/O
    if (tag.tag == V_SYMBOL) {
        SymbolData* ts = main::dereference_as(SymbolData, tag.sym_handle);
        char[] tn = ts.chars[:ts.len];
        if (rt_sym_eq(tn, "io/print")) {
            rt_print_value(arg);
            return make_nil();
        } else if (rt_sym_eq(tn, "io/println")) {
            rt_print_value(arg);
            io::printn("");
            return make_nil();
        } else if (rt_sym_eq(tn, "io/display")) {
            rt_print_value(arg);
            return make_nil();
        } else if (rt_sym_eq(tn, "io/newline")) {
            io::printn("");
            return make_nil();
        } else if (rt_sym_eq(tn, "io/read-file")) {
            return rt_read_file(arg, make_nil());
        } else if (rt_sym_eq(tn, "io/write-file")) {
            // arg should be (cons filename content)
            if (arg.tag == V_CONS) {
                Value* fname = main::dereference_as(Value, arg.cons_val.car);
                Value* content = main::dereference_as(Value, arg.cons_val.cdr);
                return rt_write_file(*fname, *content);
            }
            return make_nil();
        } else if (rt_sym_eq(tn, "io/file-exists?")) {
            return rt_file_exists(arg, make_nil());
        } else if (rt_sym_eq(tn, "io/read-lines")) {
            return rt_read_lines(arg, make_nil());
        }
    }

    io::printfn("Error: unhandled effect in compiled code");
    return make_nil();
}

fn bool rt_sym_eq(char[] a, char[] b) @inline {
    if (a.len != b.len) return false;
    for (usz i = 0; i < a.len; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

/**
 * invoke_compiled_continuation — Resume a captured continuation with a value.
 * Wraps the replay mechanism as a ClosureFn for use as V_CLOSURE.
 */
fn Value invoke_compiled_continuation(void* data, Value arg) {
    RtCapturedCont* cc = (RtCapturedCont*)data;
    if (cc == null) {
        io::printfn("Error: null continuation");
        return make_nil();
    }

    rt_cont_sub_val = arg;
    rt_cont_sub = true;
    rt_cont_is_eff = cc.is_effect;
    if (cc.is_effect) {
        rt_cont_eff_tag = cc.effect_tag;
    }

    Value result;

    if (cc.is_effect && cc.has_handler) {
        // Reinstall handler for replay
        if (rt_handler_count >= RT_MAX_HANDLERS) {
            io::printfn("Error: handler stack overflow during continuation resume");
            return make_nil();
        }
        rt_handler_stack[rt_handler_count] = cc.handler_copy;
        rt_handler_stack[rt_handler_count].body_closure = cc.body_closure;
        rt_handler_count++;

        rt_cont_target_perform = cc.perform_index;
        rt_eff_perform_ctr = 0;
        for (usz i = 0; i < cc.prior_count && i < RT_MAX_PRIOR; i++) {
            rt_eff_prior[i] = cc.prior_results[i];
        }

        // Replay body
        result = cc.body_closure.closure_val.invoke(cc.body_closure.closure_val.data, make_nil());
        while (result.tag == V_THUNK) {
            Value* ta = main::dereference_as(Value, result.thunk_val.arg);
            result = result.thunk_val.invoke(result.thunk_val.data, *ta);
        }

        if (rt_effect_occurred) {
            rt_effect_occurred = false;
            result = rt_effect_result_val;
        } else {
            rt_handler_count--;
        }
    } else {
        // Shift continuation replay
        rt_cont_target_shift = cc.shift_index;
        rt_eff_shift_ctr = 0;
        for (usz i = 0; i < cc.shift_prior_count && i < RT_MAX_PRIOR; i++) {
            rt_shift_prior[i] = cc.shift_prior_results[i];
        }

        result = cc.body_closure.closure_val.invoke(cc.body_closure.closure_val.data, make_nil());
        while (result.tag == V_THUNK) {
            Value* ta = main::dereference_as(Value, result.thunk_val.arg);
            result = result.thunk_val.invoke(result.thunk_val.data, *ta);
        }
    }

    rt_cont_sub = false;
    return result;
}

// =============================================================================
// SECTION 16: FILE I/O PRIMITIVES
// =============================================================================

fn Value rt_read_file(Value path, Value _unused) {
    if (path.tag != V_STRING) return make_nil();
    StringData* p = main::dereference_as(StringData, path.str_handle);

    // Get path as slice
    char[] path_slice = p.chars[:p.len];

    // Use C3's file::load_temp to read entire file
    if (try content = io::file::load_temp((String)path_slice)) {
        usz len = content.len;
        if (len > 4095) len = 4095;

        StringData result;
        for (usz i = 0; i < len; i++) {
            result.chars[i] = content[i];
        }
        result.chars[len] = 0;
        result.len = len;

        Value v;
        v.tag = V_STRING;
        v.str_handle = main::allocate_in(g_current_region, StringData, result);
        return v;
    }
    return make_nil();
}

fn Value rt_write_file(Value path, Value content) {
    if (path.tag != V_STRING || content.tag != V_STRING) return make_nil();
    StringData* p = main::dereference_as(StringData, path.str_handle);
    StringData* c = main::dereference_as(StringData, content.str_handle);

    // Get path and content as slices
    char[] path_slice = p.chars[:p.len];
    char[] content_slice = c.chars[:c.len];

    if (try file = io::file::open((String)path_slice, "w")) {
        file.write(content_slice)!!;
        file.close()!!;
        return make_true();
    }
    return make_nil();
}

fn Value rt_file_exists(Value path, Value _unused) {
    if (path.tag != V_STRING) return make_nil();
    StringData* p = main::dereference_as(StringData, path.str_handle);

    // Get path as slice
    char[] path_slice = p.chars[:p.len];

    if (io::file::is_file((String)path_slice)) {
        return make_true();
    }
    return make_nil();
}

fn Value rt_read_lines(Value path, Value _unused) {
    if (path.tag != V_STRING) return make_nil();

    // Read the whole file first
    Value content = rt_read_file(path, make_nil());
    if (content.tag != V_STRING) return make_nil();

    // Split by newline
    Value newline_sep = make_string("\n");
    return rt_string_split(content, newline_sep);
}

// =============================================================================
// SECTION 17: INTERPRETER BRIDGE
// =============================================================================

/**
 * The interpreter bridge allows compiled code to delegate reset/shift/handle/perform
 * to the interpreter. The compiler serializes continuation expressions to Pika source
 * text, and these runtime functions parse and evaluate them using a real Interp instance.
 *
 * This is the simplest correct approach: the interpreter already fully implements
 * the replay-based continuation system, so we reuse it for compiled code.
 *
 * Variable injection: The compiler emits rt_define_var() calls to inject compiled-scope
 * variables into the interpreter before evaluating the continuation expression.
 */

/**
 * Thread-local interpreter instance for delegation.
 * Lazily initialized on first use.
 */
tlocal lisp::Interp* g_interp = null;

/**
 * Ensure the interpreter is initialized.
 * Creates a fresh Interp with all primitives and stdlib registered.
 */
fn void rt_ensure_interp() {
    if (g_interp != null) return;

    g_interp = (lisp::Interp*)mem::malloc(lisp::Interp.sizeof);
    g_interp.init();
    lisp::register_primitives(g_interp);
    lisp::register_stdlib(g_interp);
    g_interp.jit_enabled = true;
}

/**
 * Convert an interpreter Value* to a runtime Value.
 * Recursively converts cons cells, strings, symbols, etc.
 */
fn Value interp_to_runtime(lisp::Value* v) {
    if (v == null) {
        return make_nil();
    }

    // Use the underlying char value of the tag for cross-module comparison
    lisp::ValueTag itag = v.tag;

    if (itag == lisp::ValueTag.NIL) {
        return make_nil();
    } else if (itag == lisp::ValueTag.INT) {
        return make_int(v.int_val);
    } else if (itag == lisp::ValueTag.STRING) {
        return make_string(v.str_val.chars[:v.str_val.len]);
    } else if (itag == lisp::ValueTag.SYMBOL) {
        // Check for 'true' and 'false' symbols
        if (g_interp != null) {
            if ((uint)v.sym_val == (uint)g_interp.sym_true) {
                return make_true();
            }
            if ((uint)v.sym_val == (uint)g_interp.sym_false) {
                return make_false();
            }
        }
        char[] sym_name = g_interp.symbols.get_name(v.sym_val);
        return make_symbol(sym_name);
    } else if (itag == lisp::ValueTag.CONS) {
        Value car = interp_to_runtime(v.cons_val.car);
        Value cdr = interp_to_runtime(v.cons_val.cdr);
        return rt_cons(car, cdr);
    } else if (itag == lisp::ValueTag.CLOSURE || itag == lisp::ValueTag.PRIMITIVE || itag == lisp::ValueTag.PARTIAL_PRIM) {
        // Callable interpreter values: wrap as runtime closure that delegates via apply.
        return make_interp_closure_wrapper(v);
    } else if (itag == lisp::ValueTag.CONTINUATION) {
        // Continuations from the interpreter can't be directly used.
        // Wrap as a runtime closure that invokes the continuation.
        return make_interp_continuation_wrapper(v);
    } else if (itag == lisp::ValueTag.DOUBLE) {
        Value r;
        r.tag = V_DOUBLE;
        r.double_val = v.double_val;
        return r;
    } else if (itag == lisp::ValueTag.HASHMAP || itag == lisp::ValueTag.ARRAY || itag == lisp::ValueTag.INSTANCE) {
        // Complex interpreter types: wrap as opaque reference
        Value r;
        r.tag = V_INTERP_REF;
        r.interp_ref = v;
        return r;
    }
    return make_nil();
}

/**
 * Convert a runtime Value to an interpreter Value*.
 * Allocates the interpreter value in the interpreter's current frame.
 */
fn lisp::Value* runtime_to_interp(Value v) {
    rt_ensure_interp();

    switch (v.tag) {
        case V_NIL:
            return lisp::make_nil(g_interp);

        case V_TRUE:
            return lisp::make_symbol(g_interp, g_interp.sym_true);

        case V_FALSE:
            return lisp::make_symbol(g_interp, g_interp.sym_false);

        case V_INT:
            return lisp::make_int(g_interp, v.int_val);

        case V_STRING:
            StringData* s = main::dereference_as(StringData, v.str_handle);
            return lisp::make_string(g_interp, s.chars[:s.len]);

        case V_SYMBOL:
            SymbolData* sym = main::dereference_as(SymbolData, v.sym_handle);
            lisp::SymbolId sid = g_interp.symbols.intern(sym.chars[:sym.len]);
            return lisp::make_symbol(g_interp, sid);

        case V_CONS:
            Value* car_rt = main::dereference_as(Value, v.cons_val.car);
            Value* cdr_rt = main::dereference_as(Value, v.cons_val.cdr);
            lisp::Value* car_interp = runtime_to_interp(*car_rt);
            lisp::Value* cdr_interp = runtime_to_interp(*cdr_rt);
            return lisp::make_cons(g_interp, car_interp, cdr_interp);

        case V_DOUBLE:
            return lisp::make_double(g_interp, v.double_val);

        case V_INTERP_REF:
            // Opaque interpreter reference — just return the stored pointer
            return (lisp::Value*)v.interp_ref;

        case V_CLOSURE:
            // Wrap compiled closure as interpreter-callable primitive
            CompiledClosureWrapper* ccw = (CompiledClosureWrapper*)mem::malloc(CompiledClosureWrapper.sizeof);
            ccw.data = v.closure_val.data;
            ccw.func = v.closure_val.invoke;
            lisp::Value* prim = lisp::make_primitive(g_interp, "compiled-closure", &invoke_compiled_closure, 1);
            prim.prim_val.user_data = (void*)ccw;
            return prim;

        default:
            return lisp::make_nil(g_interp);
    }
}

/**
 * Wrapper state for compiled closures passed back to interpreter.
 */
struct CompiledClosureWrapper {
    void* data;
    ClosureFn func;
}

/**
 * PrimitiveFn adapter: invoke a compiled closure from interpreter context.
 * interp.prim_user_data is set to Primitive.user_data by apply_primitive().
 */
fn lisp::Value* invoke_compiled_closure(lisp::Value*[] args, lisp::Env* env, lisp::Interp* interp) {
    if (args.len < 1) return lisp::make_nil(interp);

    CompiledClosureWrapper* ccw = (CompiledClosureWrapper*)interp.prim_user_data;
    if (ccw == null) return lisp::make_nil(interp);

    Value rt_arg = interp_to_runtime(args[0]);
    Value result = ccw.func(ccw.data, rt_arg);
    return runtime_to_interp(result);
}

/**
 * Wrapper state for interpreter closures used in compiled code.
 */
struct InterpClosureWrapper {
    lisp::Value* interp_value;  // The interpreter closure/continuation value
}

/**
 * Invoke function for interpreter closure wrappers.
 * Converts the runtime arg to interpreter form, applies, converts back.
 */
fn Value invoke_interp_closure(void* data, Value arg) {
    InterpClosureWrapper* wrapper = (InterpClosureWrapper*)data;
    rt_ensure_interp();

    lisp::Value* interp_arg = runtime_to_interp(arg);
    lisp::Value* result = lisp::jit_apply_value(wrapper.interp_value, interp_arg, g_interp);

    if (result != null && lisp::is_error(result)) {
        io::printfn("Error in interpreter closure: %s", (ZString)result.str_val.chars[:result.str_val.len]);
        return make_nil();
    }

    return interp_to_runtime(result);
}

/**
 * Create a runtime closure that wraps an interpreter closure.
 */
fn Value make_interp_closure_wrapper(lisp::Value* interp_val) {
    // Use malloc instead of root region to avoid unbounded region growth
    InterpClosureWrapper* wrapper = (InterpClosureWrapper*)mem::malloc(InterpClosureWrapper.sizeof);
    wrapper.interp_value = interp_val;
    return make_closure((void*)wrapper, &invoke_interp_closure);
}

/**
 * Create a runtime closure that wraps an interpreter continuation.
 * Continuations are invoked with apply_continuation in the interpreter.
 */
fn Value make_interp_continuation_wrapper(lisp::Value* interp_val) {
    InterpClosureWrapper* wrapper = (InterpClosureWrapper*)mem::malloc(InterpClosureWrapper.sizeof);
    wrapper.interp_value = interp_val;
    return make_closure((void*)wrapper, &invoke_interp_closure);
}

// =============================================================================
// SECTION 17b: STANDALONE VARIABLE TABLE (mutable-captured variables)
// =============================================================================

const usz RT_VAR_TABLE_SIZE = 256;

struct RtVarEntry {
    char[64] name;
    usz      name_len;
    Value    val;
    bool     used;
}

RtVarEntry[RT_VAR_TABLE_SIZE] g_rt_vars;

fn usz rt_var_hash(char[] name) {
    usz h = 5381;
    for (usz i = 0; i < name.len; i++) {
        h = ((h << 5) + h) + (usz)name[i];
    }
    return h & (RT_VAR_TABLE_SIZE - 1);
}

/**
 * Define/update a variable in the standalone var table.
 * Used for mutable-captured variables.
 */
fn void rt_define_var(char[] name, Value val) {
    usz idx = rt_var_hash(name);
    for (usz i = 0; i < RT_VAR_TABLE_SIZE; i++) {
        usz slot = (idx + i) & (RT_VAR_TABLE_SIZE - 1);
        if (g_rt_vars[slot].used && g_rt_vars[slot].name_len == name.len) {
            bool match = true;
            for (usz j = 0; j < name.len; j++) {
                if (g_rt_vars[slot].name[j] != name[j]) { match = false; break; }
            }
            if (match) {
                g_rt_vars[slot].val = val;
                return;
            }
        }
        if (!g_rt_vars[slot].used) {
            usz len = name.len;
            if (len > 63) len = 63;
            for (usz j = 0; j < len; j++) {
                g_rt_vars[slot].name[j] = name[j];
            }
            g_rt_vars[slot].name[len] = 0;
            g_rt_vars[slot].name_len = len;
            g_rt_vars[slot].val = val;
            g_rt_vars[slot].used = true;
            return;
        }
    }
}

/**
 * Look up a variable in the standalone var table.
 * Used for mutable-captured variables.
 */
fn Value rt_lookup_var(char[] name) {
    usz idx = rt_var_hash(name);
    for (usz i = 0; i < RT_VAR_TABLE_SIZE; i++) {
        usz slot = (idx + i) & (RT_VAR_TABLE_SIZE - 1);
        if (!g_rt_vars[slot].used) return make_nil();
        if (g_rt_vars[slot].name_len == name.len) {
            bool match = true;
            for (usz j = 0; j < name.len; j++) {
                if (g_rt_vars[slot].name[j] != name[j]) { match = false; break; }
            }
            if (match) return g_rt_vars[slot].val;
        }
    }
    return make_nil();
}

/**
 * Set a variable in the standalone var table.
 * Returns the new value. Used for mutable-captured variable mutation.
 */
fn Value rt_set_var(char[] name, Value val) {
    rt_define_var(name, val);
    return val;
}

/**
 * rt_eval_source — DEPRECATED: No longer needed.
 * All expression types are now compiled natively.
 * Kept as a stub for backwards compatibility during transition.
 */
fn Value rt_eval_source(char[] source) {
    rt_ensure_interp();

    lisp::EvalResult result = lisp::run(source, &*g_interp);

    if (result.error.has_error) {
        io::printfn("Error in rt_eval_source: %s", (ZString)&result.error.message);
        return make_nil();
    }

    return interp_to_runtime(result.value);
}

// =============================================================================
// SECTION 18: MISSING PRIMITIVES
// =============================================================================

/**
 * (string->number str) => int or nil
 */
fn Value rt_string_to_number(Value a, Value _unused) {
    if (a.tag != V_STRING) return make_nil();
    StringData* s = main::dereference_as(StringData, a.str_handle);
    if (s.len == 0) return make_nil();

    // Check if it contains a decimal point or exponent → parse as double
    bool is_float = false;
    for (usz i = 0; i < s.len; i++) {
        if (s.chars[i] == '.' || s.chars[i] == 'e' || s.chars[i] == 'E') {
            is_float = true;
            break;
        }
    }

    if (is_float) {
        // Parse as double manually
        double result = 0.0;
        bool negative = false;
        usz pos = 0;
        if (s.chars[0] == '-') { negative = true; pos = 1; }
        else if (s.chars[0] == '+') { pos = 1; }
        // Integer part
        while (pos < s.len && s.chars[pos] >= '0' && s.chars[pos] <= '9') {
            result = result * 10.0 + (double)(s.chars[pos] - '0');
            pos++;
        }
        // Fractional part
        if (pos < s.len && s.chars[pos] == '.') {
            pos++;
            double frac = 0.1;
            while (pos < s.len && s.chars[pos] >= '0' && s.chars[pos] <= '9') {
                result += (double)(s.chars[pos] - '0') * frac;
                frac *= 0.1;
                pos++;
            }
        }
        // Exponent part
        if (pos < s.len && (s.chars[pos] == 'e' || s.chars[pos] == 'E')) {
            pos++;
            bool exp_neg = false;
            if (pos < s.len && s.chars[pos] == '-') { exp_neg = true; pos++; }
            else if (pos < s.len && s.chars[pos] == '+') { pos++; }
            long exp_val = 0;
            while (pos < s.len && s.chars[pos] >= '0' && s.chars[pos] <= '9') {
                exp_val = exp_val * 10 + (long)(s.chars[pos] - '0');
                pos++;
            }
            double multiplier = 1.0;
            for (long e = 0; e < exp_val; e++) multiplier *= 10.0;
            if (exp_neg) { result /= multiplier; }
            else { result *= multiplier; }
        }
        if (pos != s.len) return make_nil();  // Unparsed characters
        return rt_make_double(negative ? -result : result);
    }

    // Parse as integer
    long result = 0;
    bool negative = false;
    usz start = 0;

    if (s.chars[0] == '-') {
        negative = true;
        start = 1;
        if (s.len == 1) return make_nil();
    }

    for (usz i = start; i < s.len; i++) {
        char c = s.chars[i];
        if (c < '0' || c > '9') return make_nil();
        result = result * 10 + (long)(c - '0');
    }

    return make_int(negative ? -result : result);
}

/**
 * (number->string n) => string
 */
fn Value rt_number_to_string(Value a, Value _unused) {
    if (a.tag == V_DOUBLE) {
        char[64] dbuf;
        char[] dslice = io::bprintf(&dbuf, "%.15g", a.double_val)!!;
        return make_string(dslice);
    }
    if (a.tag != V_INT) return make_nil();
    long n = a.int_val;

    // Handle long.min specially to avoid -long.min overflow
    if (n == long.min) {
        char[] min_str = "-9223372036854775808";
        return make_string(min_str);
    }

    char[32] buf;
    usz pos = 0;
    bool negative = n < 0;
    if (negative) { n = -n; }

    if (n == 0) {
        buf[0] = '0';
        pos = 1;
    } else {
        while (n > 0 && pos < 31) {
            buf[pos++] = (char)('0' + (n % 10));
            n /= 10;
        }
        // Reverse
        for (usz i = 0; i < pos / 2; i++) {
            char tmp = buf[i];
            buf[i] = buf[pos - 1 - i];
            buf[pos - 1 - i] = tmp;
        }
    }

    if (negative) {
        // Shift right and prepend '-'
        for (isz i = (isz)pos; i > 0; i--) {
            buf[(usz)i] = buf[(usz)i - 1];
        }
        buf[0] = '-';
        pos++;
    }

    return make_string(buf[:pos]);
}

/**
 * (gensym) => unique symbol via interpreter delegation
 */
tlocal long g_gensym_counter = 0;

fn Value rt_gensym(Value _a, Value _unused) {
    long n = g_gensym_counter++;
    char[32] buf;
    // Format as "g#N"
    buf[0] = 'g';
    buf[1] = '#';
    usz pos = 2;
    if (n == 0) {
        buf[pos++] = '0';
    } else {
        // Write digits reversed, then reverse
        usz start = pos;
        long tmp = n;
        while (tmp > 0 && pos < 31) {
            buf[pos++] = (char)('0' + (tmp % 10));
            tmp /= 10;
        }
        for (usz i = 0; i < (pos - start) / 2; i++) {
            char c = buf[start + i];
            buf[start + i] = buf[pos - 1 - i];
            buf[pos - 1 - i] = c;
        }
    }
    return make_symbol(buf[:pos]);
}

/**
 * (apply f args) => apply function to list of args
 */
fn Value rt_apply_prim(Value f, Value args) {
    // Count the args list
    long argc = 0;
    Value cur = args;
    while (cur.tag == V_CONS) {
        argc++;
        Value* cdr = main::dereference_as(Value, cur.cons_val.cdr);
        cur = *cdr;
    }
    return rt_apply_multi(f, args, argc);
}

/**
 * (equal? a b) => true or nil — deep structural equality
 */
fn Value rt_equal_p(Value a, Value b) {
    return rt_values_equal(a, b) ? make_true() : make_nil();
}

/**
 * (display val) => nil — print without quotes around strings
 */
fn Value rt_display(Value a, Value _unused) {
    if (a.tag == V_STRING) {
        StringData* s = main::dereference_as(StringData, a.str_handle);
        io::printf("%s", (ZString)&s.chars);
    } else {
        rt_print_value(a);
    }
    return make_nil();
}

/**
 * Hash-map operations — all delegated to interpreter.
 */
fn Value rt_hash_map_create(Value a, Value _unused) {
    rt_ensure_interp();
    lisp::Value* v = lisp::make_hashmap(g_interp, 16);
    Value r;
    r.tag = V_INTERP_REF;
    r.interp_ref = v;
    return r;
}

/**
 * Create a dict from a cons list of interleaved key-value pairs.
 * (dict k1 v1 k2 v2 ...) compiles to rt_dict_from_args(args_cons_list)
 */
fn Value rt_dict_from_args(Value arg_list) {
    rt_ensure_interp();

    // Count args first
    Value current = arg_list;
    long count = 0;
    while (current.tag == V_CONS) {
        count++;
        Value* cdr = main::dereference_as(Value, current.cons_val.cdr);
        current = *cdr;
    }

    // Create hashmap with appropriate capacity (min 16, 2x pairs)
    uint cap = 16;
    if (count > 8) cap = (uint)count * 2;
    lisp::Value* map_val = lisp::make_hashmap(g_interp, cap);
    lisp::HashMap* map = map_val.hashmap_val;

    // Insert key-value pairs
    current = arg_list;
    while (current.tag == V_CONS) {
        Value* car = main::dereference_as(Value, current.cons_val.car);
        lisp::Value* key = runtime_to_interp(*car);
        Value* cdr = main::dereference_as(Value, current.cons_val.cdr);
        current = *cdr;
        if (current.tag != V_CONS) break;
        Value* val_car = main::dereference_as(Value, current.cons_val.car);
        lisp::Value* val = runtime_to_interp(*val_car);
        lisp::hashmap_set(map, key, val, g_interp);
        Value* val_cdr = main::dereference_as(Value, current.cons_val.cdr);
        current = *val_cdr;
    }

    Value r;
    r.tag = V_INTERP_REF;
    r.interp_ref = map_val;
    return r;
}

fn Value rt_hash_ref(Value m, Value k) {
    if (m.tag != V_INTERP_REF) return make_nil();
    lisp::Value* im = (lisp::Value*)m.interp_ref;
    if (im.tag != lisp::ValueTag.HASHMAP) return make_nil();
    rt_ensure_interp();
    lisp::Value* ik = runtime_to_interp(k);
    lisp::Value* result = lisp::hashmap_get(im.hashmap_val, ik);
    if (result == null) return make_nil();
    return interp_to_runtime(result);
}

// Capture struct for curried dict-set! (3-arg: dict, key, value)
struct HashSetCapture {
    Value dict;
    Value key;
}

fn Value hash_set_apply(void* data, Value val) {
    HashSetCapture* cap = (HashSetCapture*)data;
    Value m = cap.dict;
    Value k = cap.key;
    if (m.tag != V_INTERP_REF) return m;
    lisp::Value* im = (lisp::Value*)m.interp_ref;
    if (im.tag != lisp::ValueTag.HASHMAP) return m;
    rt_ensure_interp();
    lisp::Value* ik = runtime_to_interp(k);
    lisp::Value* iv = runtime_to_interp(val);
    lisp::hashmap_set(im.hashmap_val, ik, iv, g_interp);
    return m;
}

fn Value rt_hash_set(Value m, Value k) {
    // Returns a closure that captures (dict, key) and accepts value
    HashSetCapture* cap = (HashSetCapture*)mem::malloc(HashSetCapture.sizeof);
    cap.dict = m;
    cap.key = k;
    return make_closure((void*)cap, &hash_set_apply);
}

fn Value rt_hash_has(Value m, Value k) {
    if (m.tag != V_INTERP_REF) return make_nil();
    lisp::Value* im = (lisp::Value*)m.interp_ref;
    if (im.tag != lisp::ValueTag.HASHMAP) return make_nil();
    rt_ensure_interp();
    lisp::Value* ik = runtime_to_interp(k);
    lisp::Value* result = lisp::hashmap_get(im.hashmap_val, ik);
    if (result != null) return make_true();
    return make_nil();
}

fn Value rt_hash_remove(Value m, Value k) {
    if (m.tag != V_INTERP_REF) return m;
    lisp::Value* im = (lisp::Value*)m.interp_ref;
    if (im.tag != lisp::ValueTag.HASHMAP) return m;
    rt_ensure_interp();
    lisp::Value* ik = runtime_to_interp(k);
    lisp::hashmap_remove(im.hashmap_val, ik);
    return m;
}

fn Value rt_hash_keys(Value a, Value _unused) {
    if (a.tag != V_INTERP_REF) return make_nil();
    lisp::Value* im = (lisp::Value*)a.interp_ref;
    if (im.tag != lisp::ValueTag.HASHMAP) return make_nil();
    rt_ensure_interp();
    lisp::HashMap* map = im.hashmap_val;
    // Build cons list of keys
    lisp::Value* result = lisp::make_nil(g_interp);
    for (uint i = 0; i < map.capacity; i++) {
        if (map.entries[i].key != null) {
            result = lisp::make_cons(g_interp, map.entries[i].key, result);
        }
    }
    return interp_to_runtime(result);
}

fn Value rt_hash_values(Value a, Value _unused) {
    if (a.tag != V_INTERP_REF) return make_nil();
    lisp::Value* im = (lisp::Value*)a.interp_ref;
    if (im.tag != lisp::ValueTag.HASHMAP) return make_nil();
    rt_ensure_interp();
    lisp::HashMap* map = im.hashmap_val;
    // Build cons list of values
    lisp::Value* result = lisp::make_nil(g_interp);
    for (uint i = 0; i < map.capacity; i++) {
        if (map.entries[i].key != null) {
            result = lisp::make_cons(g_interp, map.entries[i].value, result);
        }
    }
    return interp_to_runtime(result);
}

fn Value rt_hash_count(Value a, Value _unused) {
    if (a.tag != V_INTERP_REF) return make_int(0);
    lisp::Value* im = (lisp::Value*)a.interp_ref;
    if (im.tag != lisp::ValueTag.HASHMAP) return make_int(0);
    return make_int((long)im.hashmap_val.count);
}

fn Value rt_hash_map_p(Value a, Value _unused) {
    if (a.tag == V_INTERP_REF) {
        lisp::Value* im = (lisp::Value*)a.interp_ref;
        if (im.tag == lisp::ValueTag.HASHMAP) return make_true();
    }
    return make_nil();
}

/**
 * (load path) => result of evaluating file
 */
fn Value rt_load(Value path, Value _unused) {
    if (path.tag != V_STRING) return make_nil();
    StringData* p = main::dereference_as(StringData, path.str_handle);
    // Read the file contents, then evaluate with interpreter
    Value content = rt_read_file(path, make_nil());
    if (content.tag != V_STRING) return make_nil();
    StringData* c = main::dereference_as(StringData, content.str_handle);
    return rt_eval_source(c.chars[:c.len]);
}

// =============================================================================
// SECTION: FFI RUNTIME SUPPORT
// =============================================================================

extern fn void* dlopen(char* filename, int flags) @extern("dlopen");
extern fn void* dlsym(void* handle, char* symbol) @extern("dlsym");
extern fn int dlclose(void* handle) @extern("dlclose");
extern fn char* dlerror() @extern("dlerror");
const int RTLD_LAZY = 1;

// Function pointer aliases for FFI calls
alias RtFfiFn0 = fn long();
alias RtFfiFn1 = fn long(long);
alias RtFfiFn2 = fn long(long, long);
alias RtFfiFn3 = fn long(long, long, long);
alias RtFfiFn4 = fn long(long, long, long, long);
alias RtFfiFn5 = fn long(long, long, long, long, long);
alias RtFfiFn6 = fn long(long, long, long, long, long, long);

fn Value make_ffi_handle(void* handle) @inline {
    Value v;
    v.tag = V_FFI_HANDLE;
    v.ffi_handle = handle;
    return v;
}

/**
 * (ffi-open name) => V_FFI_HANDLE
 */
fn Value rt_ffi_open(Value name, Value _unused) {
    if (name.tag != V_STRING) return make_nil();
    StringData* s = main::dereference_as(StringData, name.str_handle);
    void* handle = dlopen((ZString)&s.chars, RTLD_LAZY);
    if (handle == null) return make_nil();
    return make_ffi_handle(handle);
}

/**
 * (ffi-close handle) => nil
 */
fn Value rt_ffi_close(Value handle, Value _unused) {
    if (handle.tag == V_FFI_HANDLE && handle.ffi_handle != null) {
        dlclose(handle.ffi_handle);
    }
    return make_nil();
}

/**
 * (ffi-sym handle name) => int (pointer as integer)
 */
fn Value rt_ffi_sym(Value handle, Value name) {
    if (handle.tag != V_FFI_HANDLE || handle.ffi_handle == null) return make_nil();
    if (name.tag != V_STRING) return make_nil();
    StringData* s = main::dereference_as(StringData, name.str_handle);
    void* sym = dlsym(handle.ffi_handle, (ZString)&s.chars);
    if (sym == null) return make_int(0);
    return make_int((long)(uptr)sym);
}

/**
 * Helper to convert a runtime Value to a long for FFI.
 */
fn long rt_ffi_val_to_long(Value val) {
    switch (val.tag) {
        case V_INT:
            return val.int_val;
        case V_STRING: {
            StringData* s = main::dereference_as(StringData, val.str_handle);
            return (long)&s.chars;
        }
        case V_FFI_HANDLE:
            return (long)(uptr)val.ffi_handle;
        default:
            return 0;
    }
}

/**
 * (ffi-call handle "name" 'ret-type arg1 'type1 ...)
 * Compiled as rt_ffi_call(handle, name, ret_type, arg1, type1, ...)
 * through rt_eval_source delegation.
 */
fn Value rt_ffi_call(Value handle, Value name) {
    // Returns a partial that will be completed by rt_eval_source delegation
    // For compiled code, FFI calls go through the interpreter via rt_eval_source
    return make_nil();
}
