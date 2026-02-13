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
    V_CONTINUATION, // Captured delimited continuation
    V_FFI_HANDLE,   // FFI library handle (dlopen result)
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
    char[256] chars;
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
        void*               ffi_handle;   // V_FFI_HANDLE - dlopen handle
    }
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

fn Value make_int(long n) @inline {
    Value v;
    v.tag = V_INT;
    v.int_val = n;
    return v;
}

fn Value make_string(char[] s) {
    Value v;
    v.tag = V_STRING;

    // Create StringData in current region
    StringData str;
    usz len = s.len;
    if (len > 255) len = 255;
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
    if (len > 255) len = 255;
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
    if (len > 255) len = 255;
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
    if (len > 255) len = 255;
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
    if (v.tag == V_NIL) return false;
    return true;
}

/**
 * Invoke a function with an argument.
 */
fn Value rt_invoke(Value func, Value arg) {
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
 * Check if two values are equal.
 */
fn bool rt_values_equal(Value a, Value b) {
    if (a.tag != b.tag) return false;

    switch (a.tag) {
        case V_NIL:
            return true;
        case V_TRUE:
            return true;
        case V_INT:
            return a.int_val == b.int_val;
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
            return rt_values_equal(*car_a, *car_b) && rt_values_equal(*cdr_a, *cdr_b);
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
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    return make_int(a.int_val + b.int_val);
}

fn Value rt_sub(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    return make_int(a.int_val - b.int_val);
}

fn Value rt_mul(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    return make_int(a.int_val * b.int_val);
}

fn Value rt_div(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
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
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    return a.int_val < b.int_val ? make_true() : make_nil();
}

fn Value rt_gt(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    return a.int_val > b.int_val ? make_true() : make_nil();
}

fn Value rt_le(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    return a.int_val <= b.int_val ? make_true() : make_nil();
}

fn Value rt_ge(Value a, Value b) {
    if (a.tag != V_INT || b.tag != V_INT) return make_nil();
    return a.int_val >= b.int_val ? make_true() : make_nil();
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
        case V_INT:
            io::printf("%d", v.int_val);
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
    if (total > 255) total = 255;
    usz i = 0;
    for (usz j = 0; j < sa.len && i < 255; j++) {
        result.chars[i++] = sa.chars[j];
    }
    for (usz j = 0; j < sb.len && i < 255; j++) {
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
            for (usz i = 0; i < sep_str.len && pos < 255; i++) {
                result.chars[pos++] = sep_str.chars[i];
            }
        }
        first = false;

        Value* elem = main::dereference_as(Value, current.cons_val.car);
        if (elem.tag == V_STRING) {
            StringData* s = main::dereference_as(StringData, elem.str_handle);
            for (usz i = 0; i < s.len && pos < 255; i++) {
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

fn Value rt_substring(Value str, Value start_v, Value end_v) {
    // Note: this is a ternary primitive but called via currying in compiled code
    // The compiler chains: rt_invoke(rt_invoke(rt_invoke(make_prim(&rt_substring), str), start), end)
    // Since rt_substring has PrimFn signature (binary), we need it as binary for the runtime
    // But this function is called as rt_substring(str, start_v) with end_v unused
    // For proper ternary support, we'd need a different approach
    // For now, just handle the binary case (substring str start) returning from start to end
    if (str.tag != V_STRING || start_v.tag != V_INT) return make_nil();
    StringData* s = main::dereference_as(StringData, str.str_handle);
    long start = start_v.int_val;
    long end = (long)s.len;

    // Handle negative indices
    if (start < 0) start = (long)s.len + start;
    if (start < 0) start = 0;
    if (start > (long)s.len) start = (long)s.len;
    if (end > (long)s.len) end = (long)s.len;
    if (end < start) end = start;

    StringData result;
    usz len = (usz)(end - start);
    if (len > 255) len = 255;
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

fn Value rt_string_split(Value str, Value sep) {
    if (str.tag != V_STRING || sep.tag != V_STRING) return make_nil();
    StringData* s = main::dereference_as(StringData, str.str_handle);
    StringData* d = main::dereference_as(StringData, sep.str_handle);
    if (d.len == 0) return make_nil();

    char delim = d.chars[0];
    Value result = make_nil();

    // Build list in reverse, then reverse
    isz last = (isz)s.len;
    for (isz i = (isz)s.len - 1; i >= -1; i--) {
        if (i == -1 || s.chars[(usz)i] == delim) {
            usz start = (usz)(i + 1);
            usz len = (usz)last - start;

            StringData part;
            if (len > 255) len = 255;
            for (usz j = 0; j < len; j++) {
                part.chars[j] = s.chars[start + j];
            }
            part.chars[len] = 0;
            part.len = len;

            Value part_val;
            part_val.tag = V_STRING;
            part_val.str_handle = main::allocate_in(g_current_region, StringData, part);
            result = rt_cons(part_val, result);
            last = (isz)i;
        }
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
            for (usz i = 0; i < s.len && pos < 255; i++) {
                result.chars[pos++] = s.chars[i];
            }
        } else if (elem.tag == V_INT) {
            if (pos < 255) {
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
// SECTION 15: CONTINUATION RUNTIME (LEGACY STUBS)
// =============================================================================

// NOTE: The old stubs have been replaced by the interpreter bridge in Section 17.
// The compiler now serializes continuation forms (reset/shift/handle/perform) to
// Pika source text and delegates to the interpreter via rt_eval_source().
//
// These legacy stubs are kept for backward compatibility with any generated code
// that may still reference them. They are not used by the current compiler.

struct ContinuationHandle {
    void* data;
}

fn void rt_cont_reset_begin() {
    // Legacy stub — no longer used by compiler
}

fn Value rt_cont_reset_end(Value result) {
    // Legacy stub — no longer used by compiler
    return result;
}

fn ContinuationHandle rt_cont_shift() {
    // Legacy stub — no longer used by compiler
    ContinuationHandle h;
    h.data = null;
    return h;
}

fn void rt_push_handler() {
    // Legacy stub — no longer used by compiler
}

fn void rt_pop_handler() {
    // Legacy stub — no longer used by compiler
}

fn void rt_register_effect(char[] tag_name) {
    // Legacy stub — no longer used by compiler
}

fn Value rt_perform_stub(char[] tag_name, Value arg) {
    // Legacy stub — renamed to avoid collision with rt_perform that was never
    // actually generated by the old compiler (it used inline stubs).
    io::printfn("Error: unhandled effect '%s' in compiled code", tag_name);
    return make_nil();
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
        if (len > 255) len = 255;

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
                return make_nil();
            }
        }
        char[] sym_name = g_interp.symbols.get_name(v.sym_val);
        return make_symbol(sym_name);
    } else if (itag == lisp::ValueTag.CONS) {
        Value car = interp_to_runtime(v.cons_val.car);
        Value cdr = interp_to_runtime(v.cons_val.cdr);
        return rt_cons(car, cdr);
    } else if (itag == lisp::ValueTag.CLOSURE) {
        // Closures from the interpreter can't be directly used in compiled code.
        // Wrap as a runtime closure that delegates to the interpreter's apply.
        return make_interp_closure_wrapper(v);
    } else if (itag == lisp::ValueTag.CONTINUATION) {
        // Continuations from the interpreter can't be directly used.
        // Wrap as a runtime closure that invokes the continuation.
        return make_interp_continuation_wrapper(v);
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

        case V_CLOSURE:
            // Wrap compiled closure as interpreter closure
            // For now, return a placeholder
            return lisp::make_nil(g_interp);

        default:
            return lisp::make_nil(g_interp);
    }
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
    lisp::EvalResult result = lisp::apply(wrapper.interp_value, interp_arg, g_interp);

    if (result.error.has_error) {
        io::printfn("Error in interpreter closure: %s", (ZString)&result.error.message);
        return make_nil();
    }

    return interp_to_runtime(result.value);
}

/**
 * Create a runtime closure that wraps an interpreter closure.
 */
fn Value make_interp_closure_wrapper(lisp::Value* interp_val) {
    InterpClosureWrapper wrapper_data;
    wrapper_data.interp_value = interp_val;

    // Allocate wrapper in root region so it persists
    main::ObjectHandle wrapper_handle = main::allocate_in(g_root_region, InterpClosureWrapper, wrapper_data);
    void* wrapper_ptr = main::dereference(wrapper_handle);

    return make_closure(wrapper_ptr, &invoke_interp_closure);
}

/**
 * Create a runtime closure that wraps an interpreter continuation.
 * Continuations are invoked with apply_continuation in the interpreter.
 */
fn Value make_interp_continuation_wrapper(lisp::Value* interp_val) {
    // Continuations behave like closures: (k value) applies the continuation
    // We use the same wrapper mechanism
    InterpClosureWrapper wrapper_data;
    wrapper_data.interp_value = interp_val;

    main::ObjectHandle wrapper_handle = main::allocate_in(g_root_region, InterpClosureWrapper, wrapper_data);
    void* wrapper_ptr = main::dereference(wrapper_handle);

    return make_closure(wrapper_ptr, &invoke_interp_closure);
}

/**
 * Define a variable in the interpreter's global environment.
 * Used to inject compiled-scope variables before evaluating continuation expressions.
 *
 * @param name Variable name
 * @param val Runtime value to inject
 */
fn void rt_define_var(char[] name, Value val) {
    rt_ensure_interp();

    lisp::SymbolId sym = g_interp.symbols.intern(name);
    lisp::Value* interp_val = runtime_to_interp(val);
    g_interp.global_env.define(sym, interp_val);
}

/**
 * Evaluate Pika source code using the interpreter and return the result
 * as a runtime Value.
 *
 * This is the main entry point for compiled code to delegate continuation
 * forms (reset/shift/handle/perform) to the interpreter.
 *
 * @param source Pika source code to evaluate
 * @return The result as a runtime Value
 */
fn Value rt_eval_source(char[] source) {
    rt_ensure_interp();

    lisp::EvalResult result = lisp::run(source, g_interp);

    if (result.error.has_error) {
        io::printfn("Error in rt_eval_source: %s", (ZString)&result.error.message);
        return make_nil();
    }

    return interp_to_runtime(result.value);
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
