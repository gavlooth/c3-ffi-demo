module lisp;

import std::io;
import std::core::mem;
import main;

// Collection primitives: sorting + array operations.
fn Value* prim_sort(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "sort: expected list argument");
    if (is_nil(args[0])) return make_nil(interp);
    // Convert list to array
    Value*[256] arr;
    usz len = 0;
    Value* cur = args[0];
    while (is_cons(cur) && len < 256) {
        arr[len++] = cur.cons_val.car;
        cur = cur.cons_val.cdr;
    }
    // Insertion sort (stable)
    for (usz i = 1; i < len; i++) {
        Value* key = arr[i];
        isz j = (isz)i - 1;
        // Compare: numbers by value, strings lexicographic
        while (j >= 0) {
            bool gt = false;
            if (is_number(arr[(usz)j]) && is_number(key)) {
                gt = to_double(arr[(usz)j]) > to_double(key);
            } else if (arr[(usz)j].tag == STRING && key.tag == STRING) {
                // Lexicographic string comparison
                usz al = arr[(usz)j].str_len; usz bl = key.str_len;
                usz ml = al < bl ? al : bl;
                int cmp = 0;
                for (usz k = 0; k < ml; k++) {
                    if (arr[(usz)j].str_chars[k] != key.str_chars[k]) {
                        cmp = arr[(usz)j].str_chars[k] > key.str_chars[k] ? 1 : -1;
                        break;
                    }
                }
                if (cmp == 0) { cmp = al > bl ? 1 : (al < bl ? -1 : 0); }
                gt = cmp > 0;
            }
            if (!gt) break;
            arr[(usz)(j + 1)] = arr[(usz)j];
            j--;
        }
        arr[(usz)(j + 1)] = key;
    }
    // Rebuild list
    Value* result = make_nil(interp);
    for (isz i = (isz)len - 1; i >= 0; i--) {
        result = make_cons(interp, arr[(usz)i], result);
    }
    return result;
}

fn Value* prim_sort_by(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "sort-by: expected comparator and list");
    Value* cmp_fn = args[0];
    if (is_nil(args[1])) return make_nil(interp);
    // Convert list to array
    Value*[256] arr;
    usz len = 0;
    Value* cur = args[1];
    while (is_cons(cur) && len < 256) {
        arr[len++] = cur.cons_val.car;
        cur = cur.cons_val.cdr;
    }
    // Insertion sort with custom comparator
    for (usz i = 1; i < len; i++) {
        Value* key = arr[i];
        isz j = (isz)i - 1;
        while (j >= 0) {
            // Call comparator: (cmp a b) should return negative/0/positive or truthy for a>b
            Value* r1 = jit_apply_value(cmp_fn, arr[(usz)j], interp);
            if (r1 == null || r1.tag == ERROR) break;
            Value* r2 = jit_apply_value(r1, key, interp);
            if (r2 == null || r2.tag == ERROR) break;
            // If result > 0 (or truthy for >), swap
            bool gt = false;
            if (is_int(r2)) { gt = r2.int_val > 0; }
            else if (is_double(r2)) { gt = r2.double_val > 0.0; }
            else { gt = !is_nil(r2); }
            if (!gt) break;
            arr[(usz)(j + 1)] = arr[(usz)j];
            j--;
        }
        arr[(usz)(j + 1)] = key;
    }
    // Rebuild list
    Value* result = make_nil(interp);
    for (isz i = (isz)len - 1; i >= 0; i--) {
        result = make_cons(interp, arr[(usz)i], result);
    }
    return result;
}


// --- Phase 8: Array primitives ---

fn Value* prim_array(Value*[] args, Env* env, Interp* interp) {
    // Single list/nil arg → convert to array
    if (args.len == 1 && (is_cons(args[0]) || is_nil(args[0]))) {
        usz count = 0;
        Value* cur = args[0];
        while (is_cons(cur) && count < 65536) { count++; cur = cur.cons_val.cdr; }
        Value* v = make_array(interp, count);
        cur = args[0];
        for (usz i = 0; i < count; i++) {
            v.array_val.items[i] = boundary_promote_to_root(cur.cons_val.car, interp);
            cur = cur.cons_val.cdr;
        }
        v.array_val.length = count;
        return v;
    }

    Value* v = make_array(interp, args.len < 4 ? 4 : args.len);
    for (usz i = 0; i < args.len; i++) {
        v.array_val.items[i] = boundary_promote_to_root(args[i], interp);
    }
    v.array_val.length = args.len;
    return v;
}

fn Value* prim_array_set(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3 || !is_array(args[0]) || !is_int(args[1])) return raise_error(interp, "array-set!: expected array, int, value");
    long idx = args[1].int_val;
    long alen = (long)args[0].array_val.length;
    if (idx < 0) idx += alen;
    if (idx < 0 || idx >= alen) return raise_error(interp, "array-set!: index out of range");
    args[0].array_val.items[(usz)idx] = boundary_promote_to_root(args[2], interp);
    return make_nil(interp);
}

fn Value* prim_array_length(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_array(args[0])) return raise_error(interp, "array-length: expected array");
    return make_int(interp, (long)args[0].array_val.length);
}

fn Value* prim_array_to_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_array(args[0])) return raise_error(interp, "array->list: expected array");
    Array* vec = args[0].array_val;
    Value* result = make_nil(interp);
    for (isz i = (isz)vec.length - 1; i >= 0; i--) {
        result = make_cons(interp, vec.items[(usz)i], result);
    }
    return result;
}

fn Value* prim_list_to_array(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "list->array: expected list");
    // Count elements
    usz count = 0;
    Value* cur = args[0];
    while (is_cons(cur) && count < 65536) { count++; cur = cur.cons_val.cdr; }
    Value* v = make_array(interp, count);
    cur = args[0];
    for (usz i = 0; i < count; i++) {
        v.array_val.items[i] = cur.cons_val.car;
        cur = cur.cons_val.cdr;
    }
    v.array_val.length = count;
    return v;
}

fn Value* prim_is_array(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "array?: expected 1 argument");
    return is_array(args[0]) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_array_push(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_array(args[0])) return raise_error(interp, "push!: expected array and value");
    Array* vec = args[0].array_val;
    if (vec.length >= vec.capacity) {
        usz new_cap = vec.capacity * 2;
        Value** new_items = (Value**)mem::malloc(Value*.sizeof * new_cap);
        for (usz i = 0; i < vec.length; i++) { new_items[i] = vec.items[i]; }
        mem::free(vec.items);
        vec.items = new_items;
        vec.capacity = new_cap;
    }
    vec.items[vec.length++] = boundary_promote_to_root(args[1], interp);
    return make_nil(interp);
}
