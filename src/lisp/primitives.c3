module lisp;

import std::io;
import main;

// =============================================================================
// SECTION 7: PRIMITIVES
// =============================================================================

/**
 * Create a new empty StringVal with given initial capacity.
 */
fn StringVal* strval_new(usz initial_cap) {
    if (initial_cap < 32) initial_cap = 32;
    StringVal* sv = (StringVal*)mem::malloc(StringVal.sizeof);
    sv.chars = (char*)mem::malloc(initial_cap);
    sv.len = 0;
    sv.capacity = initial_cap;
    sv.chars[0] = 0;
    return sv;
}

/**
 * Ensure StringVal has room for at least `needed` more bytes.
 */
fn void strval_ensure(StringVal* sv, usz needed) {
    if (sv.len + needed + 1 > sv.capacity) {
        usz new_cap = sv.capacity * 2;
        while (new_cap < sv.len + needed + 1) new_cap *= 2;
        char* new_chars = (char*)mem::malloc(new_cap);
        for (usz i = 0; i < sv.len; i++) new_chars[i] = sv.chars[i];
        mem::free(sv.chars);
        sv.chars = new_chars;
        sv.capacity = new_cap;
    }
}

/**
 * Append a single char to a StringVal.
 */
fn void strval_push(StringVal* sv, char c) {
    strval_ensure(sv, 1);
    sv.chars[sv.len++] = c;
}

fn Value* prim_add(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "+: expected at least 1 argument");

    if (args.len == 1) {
        if (!is_number(args[0])) return raise_error(interp, "+: expected number argument");
        return args[0];
    }

    if (!is_number(args[0])) return raise_error(interp, "+: expected number argument");
    if (!is_number(args[1])) return raise_error(interp, "+: expected number argument");
    if (is_double(args[0]) || is_double(args[1])) {
        return make_double(interp, to_double(args[0]) + to_double(args[1]));
    }
    return make_int(interp, args[0].int_val + args[1].int_val);
}

fn Value* prim_sub(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "-: expected at least 1 argument");
    if (args.len == 1) {
        if (!is_number(args[0])) return raise_error(interp, "-: expected number argument");
        if (is_double(args[0])) return make_double(interp, -args[0].double_val);
        return make_int(interp, -args[0].int_val);
    }
    if (!is_number(args[0])) return raise_error(interp, "-: expected number argument");
    if (!is_number(args[1])) return raise_error(interp, "-: expected number argument");
    if (is_double(args[0]) || is_double(args[1])) {
        return make_double(interp, to_double(args[0]) - to_double(args[1]));
    }
    return make_int(interp, args[0].int_val - args[1].int_val);
}

fn Value* prim_mul(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "*: expected 2 arguments");
    if (!is_number(args[0])) return raise_error(interp, "*: expected number argument");
    if (!is_number(args[1])) return raise_error(interp, "*: expected number argument");
    if (is_double(args[0]) || is_double(args[1])) {
        return make_double(interp, to_double(args[0]) * to_double(args[1]));
    }
    return make_int(interp, args[0].int_val * args[1].int_val);
}

fn Value* prim_div(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "/: expected 2 arguments");
    if (!is_number(args[0])) return raise_error(interp, "/: expected number argument");
    if (!is_number(args[1])) return raise_error(interp, "/: expected number argument");
    if (is_double(args[0]) || is_double(args[1])) {
        double b = to_double(args[1]);
        if (b == 0.0) return raise_error(interp, "/: division by zero");
        return make_double(interp, to_double(args[0]) / b);
    }
    long b = args[1].int_val;
    if (b == 0) return raise_error(interp, "/: division by zero");
    return make_int(interp, args[0].int_val / b);
}

fn Value* prim_mod(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "%: expected 2 arguments");
    if (!is_number(args[0])) return raise_error(interp, "%: expected number argument");
    if (!is_number(args[1])) return raise_error(interp, "%: expected number argument");
    if (is_double(args[0]) || is_double(args[1])) {
        return raise_error(interp, "%: expected integer arguments");
    }
    long a = args[0].int_val;
    long b = args[1].int_val;
    if (b == 0) return raise_error(interp, "%: division by zero");
    return make_int(interp, a % b);
}

fn Value* prim_eq(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "=: expected 2 arguments");
    bool eq = values_equal(args[0], args[1]);
    return eq ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_lt(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "<: expected 2 arguments");
    if (!is_number(args[0]) || !is_number(args[1])) return raise_error(interp, "<: expected number arguments");
    bool lt = to_double(args[0]) < to_double(args[1]);
    return lt ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_gt(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, ">: expected 2 arguments");
    if (!is_number(args[0]) || !is_number(args[1])) return raise_error(interp, ">: expected number arguments");
    bool gt = to_double(args[0]) > to_double(args[1]);
    return gt ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_le(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "<=: expected 2 arguments");
    if (!is_number(args[0]) || !is_number(args[1])) return raise_error(interp, "<=: expected number arguments");
    bool le = to_double(args[0]) <= to_double(args[1]);
    return le ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_ge(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, ">=: expected 2 arguments");
    if (!is_number(args[0]) || !is_number(args[1])) return raise_error(interp, ">=: expected number arguments");
    bool ge = to_double(args[0]) >= to_double(args[1]);
    return ge ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_cons(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "cons: expected 2 arguments");
    return make_cons(interp, args[0], args[1]);
}

fn Value* prim_car(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "car: expected 1 argument");
    if (!is_cons(args[0])) return raise_error(interp, "car: argument must be a pair");
    return args[0].cons_val.car;
}

fn Value* prim_cdr(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "cdr: expected 1 argument");
    if (!is_cons(args[0])) return raise_error(interp, "cdr: argument must be a pair");
    return args[0].cons_val.cdr;
}

fn Value* prim_null(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "null?: expected 1 argument");
    bool is_null = is_nil(args[0]);
    return is_null ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_pair(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "pair?: expected 1 argument");
    bool is_pair = is_cons(args[0]);
    return is_pair ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (length lst) -> int
 * Returns the number of elements in a list
 */
fn Value* prim_length(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "length: expected 1 argument");

    // nil has length 0
    if (is_nil(args[0])) {
        return make_int(interp, 0);
    }

    // String length
    if (is_string(args[0])) {
        return make_int(interp, (long)args[0].str_val.len);
    }

    // Array length
    if (is_array(args[0])) {
        return make_int(interp, (long)args[0].array_val.length);
    }

    // Dict count
    if (args[0].tag == HASHMAP) {
        return make_int(interp, (long)args[0].hashmap_val.count);
    }

    // Count cons cells
    if (!is_cons(args[0])) {
        return raise_error(interp, "length: expected list, array, dict, or string");
    }

    long count = 0;
    Value* current = args[0];
    while (is_cons(current)) {
        count++;
        current = current.cons_val.cdr;
    }
    return make_int(interp, count);
}

fn Value* prim_print(Value*[] args, Env* env, Interp* interp) {
    if (args.len >= 1) {
        print_value(args[0], &interp.symbols);
    }
    return make_nil(interp);
}

fn Value* prim_println(Value*[] args, Env* env, Interp* interp) {
    if (args.len >= 1) {
        println_value(args[0], &interp.symbols);
    }
    return make_nil(interp);
}

fn Value* prim_newline(Value*[] args, Env* env, Interp* interp) {
    io::printn("");
    return make_nil(interp);
}

fn Value* prim_not(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "not: expected 1 argument");
    bool falsy = is_falsy(args[0], interp);
    return falsy ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len == 0) return make_nil(interp);

    // Single array arg → convert to list
    if (args.len == 1 && is_array(args[0])) {
        Array* arr = args[0].array_val;
        Value* result = make_nil(interp);
        for (isz i = (isz)arr.length - 1; i >= 0; i--) {
            result = make_cons(interp, arr.items[(usz)i], result);
        }
        return result;
    }

    Value* result = make_nil(interp);
    for (isz i = (isz)args.len - 1; i >= 0; i--) {
        result = make_cons(interp, args[(usz)i], result);
    }
    return result;
}

// =============================================================================

// =============================================================================
// SECTION 7.5: STRING PRIMITIVES
// =============================================================================

/**
 * (string-append s1 s2 ...) -> concatenated string
 * Variadic: concatenates all string arguments
 */
fn Value* prim_string_append(Value*[] args, Env* env, Interp* interp) {
    if (args.len == 0) return make_string(interp, "");

    // Validate all args are strings
    for (usz i = 0; i < args.len; i++) {
        if (!is_string(args[i])) {
            return raise_error(interp, "string-append: expected string arguments");
        }
    }

    DString ds;
    ds.init(mem);
    for (usz i = 0; i < args.len; i++) {
        ds.append_string((String)args[i].str_val.chars[:args[i].str_val.len]);
    }
    Value* result = make_string(interp, ds.str_view());
    ds.free();
    return result;
}

/**
 * (string-join sep list) -> joined string
 * Join list of strings with separator
 */
fn Value* prim_string_join(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "string-join: expected 2 arguments");
    if (!is_string(args[0])) return raise_error(interp, "string-join: expected string separator");

    // Allow nil list to return empty string
    if (is_nil(args[1])) {
        return make_string(interp, "");
    }

    if (!is_cons(args[1])) {
        return raise_error(interp, "string-join: expected list as second argument");
    }

    char[] sep = args[0].str_val.chars[:args[0].str_val.len];

    DString ds;
    ds.init(mem);
    defer ds.free();
    bool first = true;

    Value* list = args[1];
    while (is_cons(list)) {
        Value* elem = list.cons_val.car;
        list = list.cons_val.cdr;

        if (!first) {
            ds.append_string((String)sep);
        }
        first = false;

        if (is_string(elem)) {
            ds.append_string((String)elem.str_val.chars[:elem.str_val.len]);
        }
    }

    return make_string(interp, ds.str_view());
}

/**
 * (substring s start end) -> substring
 */
fn Value* prim_substring(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3) return raise_error(interp, "substring: expected 3 arguments");
    if (!is_string(args[0])) return raise_error(interp, "substring: expected string as first argument");
    if (!is_int(args[1]) || !is_int(args[2])) {
        return raise_error(interp, "substring: expected integer indices");
    }

    char[] bytes = args[0].str_val.chars[:args[0].str_val.len];
    usz cp_len = utf8_strlen(bytes);
    long start = args[1].int_val;
    long end = args[2].int_val;

    // Handle negative indices (Python-style) — based on codepoint count
    if (start < 0) start += (long)cp_len;
    if (end < 0) end += (long)cp_len;

    // Clamp to bounds
    if (start < 0) start = 0;
    if (end > (long)cp_len) end = (long)cp_len;
    if (start >= end) return make_string(interp, "");

    // Convert codepoint indices to byte offsets
    usz byte_start = utf8_byte_offset(bytes, (usz)start);
    usz byte_end = utf8_byte_offset(bytes, (usz)end);
    usz sub_len = byte_end - byte_start;
    return make_string(interp, bytes[byte_start:sub_len]);
}

/**
 * (string-split s sep) -> list of strings
 */
fn Value* prim_string_split(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "string-split: expected 2 arguments");
    if (!is_string(args[0]) || !is_string(args[1])) {
        return raise_error(interp, "string-split: expected string arguments");
    }
    if (args[1].str_val.len == 0) {
        return raise_error(interp, "string-split: empty separator");
    }

    char* str = args[0].str_val.chars;
    usz str_len = args[0].str_val.len;
    // NOTE: Currently only supports single-character delimiters.
    // The first character of the separator string is used.
    char delim = args[1].str_val.chars[0];

    // Collect parts in reverse order, then reverse
    Value* parts = make_nil(interp);
    usz start = 0;

    for (usz i = 0; i <= str_len; i++) {
        if (i == str_len || str[i] == delim) {
            // Extract substring [start..i) — direct slice, no temp buffer
            usz part_len = i - start;
            Value* part;
            if (part_len == 0) {
                part = make_string(interp, "");
            } else {
                part = make_string(interp, args[0].str_val.chars[start:part_len]);
            }
            parts = make_cons(interp, part, parts);
            start = i + 1;
        }
    }

    // Reverse the list
    Value* result = make_nil(interp);
    while (is_cons(parts)) {
        result = make_cons(interp, parts.cons_val.car, result);
        parts = parts.cons_val.cdr;
    }
    return result;
}

/**
 * (string-length s) -> int
 */
fn Value* prim_string_length(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_int(interp, 0);
    }
    // Count UTF-8 codepoints, not bytes
    usz cp_len = utf8_strlen(args[0].str_val.chars[:args[0].str_val.len]);
    return make_int(interp, (long)cp_len);
}

fn Value* prim_string_byte_length(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_int(interp, 0);
    }
    return make_int(interp, (long)args[0].str_val.len);
}

/**
 * (string->list s) -> list of single-char strings
 */
fn Value* prim_string_to_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "string->list: expected 1 argument");
    if (!is_string(args[0])) {
        return raise_error(interp, "string->list: expected string argument");
    }

    char[] bytes = args[0].str_val.chars[:args[0].str_val.len];

    // First pass: count codepoints
    usz cp_count = 0;
    usz pos = 0;
    while (pos < bytes.len) {
        usz cplen = utf8_codepoint_len(bytes[pos]);
        if (pos + cplen > bytes.len) cplen = bytes.len - pos;
        pos += cplen;
        cp_count++;
    }

    if (cp_count == 0) return make_nil(interp);

    // Allocate exact-size arrays via temp allocator
    usz[] offsets = mem::temp_array(usz, cp_count);
    usz[] cp_lens = mem::temp_array(usz, cp_count);

    // Second pass: fill offsets and lens
    pos = 0;
    for (usz i = 0; i < cp_count; i++) {
        offsets[i] = pos;
        usz cplen = utf8_codepoint_len(bytes[pos]);
        if (pos + cplen > bytes.len) cplen = bytes.len - pos;
        cp_lens[i] = cplen;
        pos += cplen;
    }

    // Build list in reverse order
    Value* result = make_nil(interp);
    for (isz i = (isz)cp_count - 1; i >= 0; i--) {
        usz off = offsets[(usz)i];
        usz clen = cp_lens[(usz)i];
        Value* char_str = make_string(interp, bytes[off:clen]);
        result = make_cons(interp, char_str, result);
    }
    return result;
}

/**
 * (list->string chars) -> string
 * Convert a list of single-character strings to a string
 */
fn Value* prim_list_to_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) {
        return raise_error(interp, "list->string: expected 1 argument");
    }

    // Allow nil to return empty string
    if (is_nil(args[0])) {
        return make_string(interp, "");
    }

    if (!is_cons(args[0])) {
        return raise_error(interp, "list->string: expected a list");
    }

    DString ds;
    ds.init(mem);
    defer ds.free();

    Value* list = args[0];
    while (is_cons(list)) {
        Value* elem = list.cons_val.car;
        list = list.cons_val.cdr;

        if (is_string(elem) && elem.str_val.len > 0) {
            ds.append_char(elem.str_val.chars[0]);
        }
    }

    return make_string(interp, ds.str_view());
}

/**
 * (string-upcase s) -> uppercase string
 */
fn Value* prim_string_upcase(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "string-upcase: expected 1 argument");
    if (!is_string(args[0])) {
        return raise_error(interp, "string-upcase: expected string argument");
    }

    // Allocate result, then transform in place
    Value* result = make_string(interp, args[0].str_val.chars[:args[0].str_val.len]);
    for (usz i = 0; i < result.str_val.len; i++) {
        char c = result.str_val.chars[i];
        if (c >= 'a' && c <= 'z') {
            result.str_val.chars[i] = c - 32;
        }
    }
    return result;
}

/**
 * (string-downcase s) -> lowercase string
 */
fn Value* prim_string_downcase(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "string-downcase: expected 1 argument");
    if (!is_string(args[0])) {
        return raise_error(interp, "string-downcase: expected string argument");
    }

    // Allocate result, then transform in place
    Value* result = make_string(interp, args[0].str_val.chars[:args[0].str_val.len]);
    for (usz i = 0; i < result.str_val.len; i++) {
        char c = result.str_val.chars[i];
        if (c >= 'A' && c <= 'Z') {
            result.str_val.chars[i] = c + 32;
        }
    }
    return result;
}

/**
 * (string-trim s) -> string with leading/trailing whitespace removed
 */
fn Value* prim_string_trim(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "string-trim: expected 1 argument");
    if (!is_string(args[0])) {
        return raise_error(interp, "string-trim: expected string argument");
    }

    char* str = args[0].str_val.chars;
    usz len = args[0].str_val.len;

    // Find start (skip leading whitespace)
    usz start = 0;
    while (start < len && (str[start] == ' ' || str[start] == '\t' ||
                           str[start] == '\n' || str[start] == '\r')) {
        start++;
    }

    // Find end (skip trailing whitespace)
    usz end = len;
    while (end > start && (str[end - 1] == ' ' || str[end - 1] == '\t' ||
                           str[end - 1] == '\n' || str[end - 1] == '\r')) {
        end--;
    }

    // Empty result
    if (start >= end) {
        return make_string(interp, "");
    }

    // Direct slice — no temp buffer needed
    return make_string(interp, args[0].str_val.chars[start:end - start]);
}

/**
 * (continuation? v) -> true if v is a continuation
 */
fn Value* prim_is_continuation(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "continuation?: expected 1 argument");
    bool result = args[0].tag == CONTINUATION;
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

// =============================================================================
// SECTION 7.6: FILE I/O PRIMITIVES
// =============================================================================

/**
 * (read-file path) -> string contents or nil on error
 */
fn Value* prim_read_file(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_nil(interp);
    }

    // Get path as slice (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];

    // Try to load file using C3's file::load_temp
    if (try content = io::file::load_temp((String)path)) {
        return make_string(interp, content);
    }
    return make_nil(interp);
}

/**
 * (write-file path content) -> true on success, nil on error
 */
fn Value* prim_write_file(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_string(args[0]) || !is_string(args[1])) {
        return make_nil(interp);
    }

    // Get path and content as slices (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];
    char[] content = args[1].str_val.chars[:args[1].str_val.len];

    // Open file for writing
    if (try file = io::file::open((String)path, "w")) {
        defer (void)file.close();
        if (try bytes_written = file.write(content)) {
            return make_symbol(interp, interp.sym_true);
        }
    }
    return make_nil(interp);
}

/**
 * (file-exists? path) -> true if file exists, nil otherwise
 */
fn Value* prim_file_exists(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_nil(interp);
    }

    // Get path as slice (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];

    // Use file::is_file to check existence
    if (io::file::is_file((String)path)) {
        return make_symbol(interp, interp.sym_true);
    }
    return make_nil(interp);
}

/**
 * (read-lines path) -> list of strings (one per line)
 */
fn Value* prim_read_lines(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_nil(interp);
    }

    // Get path as slice (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];

    // Try to load file using C3's file::load_temp
    if (try content = io::file::load_temp((String)path)) {
        // Split content into lines
        Value* lines = make_nil(interp);
        usz start = 0;
        usz content_len = content.len;

        for (usz i = 0; i <= content_len; i++) {
            if (i == content_len || content[i] == '\n') {
                // Extract line [start..i)
                usz line_len = i - start;
                // Skip trailing \r if present (Windows line endings)
                if (line_len > 0 && content[start + line_len - 1] == '\r') {
                    line_len--;
                }
                Value* line = make_string(interp, content[start:line_len]);
                lines = make_cons(interp, line, lines);
                start = i + 1;
            }
        }

        // Reverse the list to get correct order
        Value* result = make_nil(interp);
        while (is_cons(lines)) {
            result = make_cons(interp, lines.cons_val.car, result);
            lines = lines.cons_val.cdr;
        }
        return result;
    }
    return make_nil(interp);
}

/**
 * (load path) -> evaluates all expressions in the file, returns last result
 * Reads a script file and evaluates it in the current environment.
 */
fn Value* prim_load(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return raise_error(interp, "load: expected a string path");
    }

    // Resolve path relative to current source directory
    char[] rel_path = args[0].str_val.chars[:args[0].str_val.len];
    char[512] resolved;
    usz rlen = resolve_import_path(rel_path, interp, &resolved, 512);
    char[] path = resolved[:rlen];

    // Try to load file
    if (try content = io::file::load_temp((String)path)) {
        char[] source = content;

        // Push source directory for nested loads/imports
        push_source_dir(path, interp);

        // Run all expressions in the file
        EvalResult r = run_program(source, interp);

        pop_source_dir(interp);

        if (r.error.has_error) {
            char[512] ebuf;
            char[] msg = io::bprintf(&ebuf, "load: error in '%s': %s",
                (ZString)path.ptr, (ZString)&r.error.message)!!;
            return raise_error(interp, msg);
        }
        return r.value;
    }
    char[256] ebuf;
    char[] msg = io::bprintf(&ebuf, "load: file not found '%s'", (ZString)path.ptr)!!;
    return raise_error(interp, msg);
}

// === System primitives (libc-backed) ===

extern fn void* c_popen(char* command, char* mode) @extern("popen");
extern fn int c_pclose(void* stream) @extern("pclose");
extern fn usz c_fread(void* ptr, usz size, usz count, void* stream) @extern("fread");
extern fn int c_feof(void* stream) @extern("feof");
extern fn char* c_getenv(char* name) @extern("getenv");
extern fn long c_time(long* t) @extern("time");
extern fn int c_usleep(uint usec) @extern("usleep");
extern fn void c_exit(int status) @extern("_exit");
extern fn int c_getrandom(void* buf, usz buflen, uint flags) @extern("getrandom");

/**
 * (shell cmd) -> string (stdout of command)
 * (shell cmd true) -> list (stdout-string exit-code)
 * Runs cmd via popen, captures stdout, returns output as string.
 */
fn Value* prim_shell(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return raise_error(interp, "shell: expected string command");
    }

    void* fp = c_popen((ZString)args[0].str_val.chars, "r");
    if (fp == null) {
        return raise_error(interp, "shell: popen failed");
    }

    // Read output into buffer
    char[8192] buf;
    usz total = 0;
    while (c_feof(fp) == 0) {
        usz n = c_fread(&buf[total], 1, 8192 - total, fp);
        if (n == 0) break;
        total += n;
        if (total >= 8192) break;
    }
    int status = c_pclose(fp);
    int exit_code = status >> 8;  // WEXITSTATUS

    // Strip trailing newline
    if (total > 0 && buf[total - 1] == '\n') total--;

    Value* output = make_string(interp, buf[:total]);

    // If second arg is truthy, return (output exit-code)
    if (args.len >= 2 && !is_falsy(args[1], interp)) {
        Value* code = make_int(interp, exit_code);
        return make_cons(interp, output, make_cons(interp, code, make_nil(interp)));
    }
    return output;
}

/**
 * (random) -> double in [0.0, 1.0)
 * (random-int n) -> integer in [0, n)
 * Uses getrandom() for quality randomness.
 */
fn Value* prim_random(Value*[] args, Env* env, Interp* interp) {
    ulong buf;
    c_getrandom(&buf, 8, 0);
    // Convert to double in [0, 1): mask to 52 bits, divide by 2^52
    ulong mantissa = buf & 0x000F_FFFF_FFFF_FFFF;
    double val = (double)mantissa / 4503599627370496.0;  // 2^52
    return make_double(interp, val);
}

fn Value* prim_random_int(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "random-int: expected max value");
    long n;
    if (args[0].tag == INT) { n = args[0].int_val; }
    else if (args[0].tag == DOUBLE) { n = (long)args[0].double_val; }
    else { return raise_error(interp, "random-int: expected number"); }
    if (n <= 0) return make_int(interp, 0);

    ulong buf;
    c_getrandom(&buf, 8, 0);
    long result = (long)(buf % (ulong)n);
    return make_int(interp, result);
}

/**
 * (getenv name) -> string or nil
 */
fn Value* prim_getenv(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return raise_error(interp, "getenv: expected string");
    }
    char* val = c_getenv((ZString)args[0].str_val.chars);
    if (val == null) return make_nil(interp);
    // Measure length
    usz len = 0;
    while (val[len] != 0) len++;
    return make_string(interp, val[:len]);
}

/**
 * (time) -> integer epoch seconds
 * (time-ms) -> integer epoch milliseconds (via clock_gettime)
 */
fn Value* prim_time(Value*[] args, Env* env, Interp* interp) {
    long t = c_time(null);
    return make_int(interp, t);
}

// clock_gettime for millisecond precision
extern fn int c_clock_gettime(int clk_id, void* tp) @extern("clock_gettime");
const int CLOCK_REALTIME = 0;

fn Value* prim_time_ms(Value*[] args, Env* env, Interp* interp) {
    long[2] ts;  // tv_sec, tv_nsec
    c_clock_gettime(CLOCK_REALTIME, &ts);
    long ms = ts[0] * 1000 + ts[1] / 1000000;
    return make_int(interp, ms);
}

/**
 * (exit) or (exit code) -> exits process
 */
fn Value* prim_exit(Value*[] args, Env* env, Interp* interp) {
    int code = 0;
    if (args.len >= 1 && args[0].tag == INT) code = (int)args[0].int_val;
    c_exit(code);
    return make_nil(interp);  // unreachable
}

/**
 * (sleep secs) -> nil
 * secs can be int or double (fractional seconds supported)
 */
fn Value* prim_sleep(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "sleep: expected number of seconds");
    double secs;
    if (args[0].tag == INT) { secs = (double)args[0].int_val; }
    else if (args[0].tag == DOUBLE) { secs = args[0].double_val; }
    else { return raise_error(interp, "sleep: expected number"); }
    uint usec = (uint)(secs * 1000000.0);
    c_usleep(usec);
    return make_nil(interp);
}

fn Value* prim_string_to_number(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) return make_nil(interp);
    char[] s = args[0].str_val.chars[:args[0].str_val.len];
    if (s.len == 0) return make_nil(interp);
    // Check if it contains a dot or exponent → parse as float
    bool is_float = false;
    for (usz j = 0; j < s.len; j++) {
        if (s[j] == '.' || s[j] == 'e' || s[j] == 'E') { is_float = true; break; }
    }
    if (is_float) {
        // Parse as double: [+-]digits[.digits][e[+-]digits]
        usz i = 0;
        bool negative = false;
        if (i < s.len && (s[i] == '-' || s[i] == '+')) { negative = s[i] == '-'; i++; }
        if (i >= s.len) return make_nil(interp);
        double val = 0.0;
        bool has_digits = false;
        while (i < s.len && s[i] >= '0' && s[i] <= '9') { val = val * 10.0 + (double)(s[i] - '0'); i++; has_digits = true; }
        if (i < s.len && s[i] == '.') {
            i++;
            double frac = 0.1;
            while (i < s.len && s[i] >= '0' && s[i] <= '9') { val = val + (double)(s[i] - '0') * frac; frac = frac * 0.1; i++; has_digits = true; }
        }
        if (!has_digits) return make_nil(interp);
        if (i < s.len && (s[i] == 'e' || s[i] == 'E')) {
            i++;
            bool exp_neg = false;
            if (i < s.len && (s[i] == '-' || s[i] == '+')) { exp_neg = s[i] == '-'; i++; }
            long exp_val = 0;
            while (i < s.len && s[i] >= '0' && s[i] <= '9') { exp_val = exp_val * 10 + (long)(s[i] - '0'); i++; }
            double mult = 1.0;
            for (long e = 0; e < exp_val; e++) mult = mult * 10.0;
            if (exp_neg) { val = val / mult; } else { val = val * mult; }
        }
        if (i != s.len) return make_nil(interp);
        if (negative) val = -val;
        return make_double(interp, val);
    }
    // Integer parse
    usz i = 0;
    bool negative = false;
    if (s[0] == '-') { negative = true; i = 1; }
    if (i >= s.len) return make_nil(interp);
    long val = 0;
    while (i < s.len) {
        if (s[i] < '0' || s[i] > '9') return make_nil(interp);
        val = val * 10 + (long)(s[i] - '0');
        i++;
    }
    if (negative) val = -val;
    return make_int(interp, val);
}

fn Value* prim_number_to_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "number->string: expected number");
    char[64] buf;
    if (is_double(args[0])) {
        return make_string(interp, double_to_string(args[0].double_val, &buf));
    }
    return make_string(interp, int_to_string(args[0].int_val, &buf));
}

fn Value* prim_gensym(Value*[] args, Env* env, Interp* interp) {
    char[64] buf;
    buf[0] = 'g'; buf[1] = '#';
    usz len = 2;
    interp.gensym_counter++;
    long n = (long)interp.gensym_counter;
    // Convert number to string
    char[20] digits;
    usz dlen = 0;
    if (n == 0) { digits[0] = '0'; dlen = 1; }
    else {
        while (n > 0) { digits[dlen] = (char)('0' + (char)(n % 10)); dlen++; n = n / 10; }
    }
    for (usz j = 0; j < dlen; j++) {
        buf[len + j] = digits[dlen - 1 - j];
    }
    len += dlen;
    SymbolId id = interp.symbols.intern(buf[:len]);
    return make_symbol(interp, id);
}

// =============================================================================
// HASH MAP OPERATIONS
// =============================================================================

fn uint fnv1a(char[] data) {
    uint h = 2166136261;
    for (usz i = 0; i < data.len; i++) { h ^= (uint)data[i]; h *= 16777619; }
    return h;
}

fn uint murmur_finalizer(uint h) {
    h ^= h >> 16; h *= 0x85ebca6b; h ^= h >> 13; h *= 0xc2b2ae35; h ^= h >> 16;
    return h;
}

// ValueKey — wrapper for using Value* as C3 map key
// Provides .hash() and == required by std::collections::map
struct ValueKey {
    Value* val;
}

fn uint ValueKey.hash(self) {
    return hash_value(self.val);
}

fn bool ValueKey.is_eq(self, ValueKey other) {
    return values_equal(self.val, other.val);
}

// C3 native map — deferred: Value* not equatable for map{Key, Value}
// Need to either add .is_eq to Value* or use wrapper type for value too.
// See M1.4b in c3-migration.md for approach.

fn uint hash_value(Value* key) {
    if (key == null) return 0;
    switch (key.tag) {
        case INT: return murmur_finalizer((uint)key.int_val);
        case STRING: return fnv1a(key.str_val.chars[:key.str_val.len]);
        case SYMBOL: return murmur_finalizer((uint)key.sym_val);
        default: return 0;
    }
}

fn HashMap* hashmap_new(uint capacity, Interp* interp) {
    // Allocate HashMap struct via malloc (freed by scope_dtor_value)
    HashMap* map = (HashMap*)mem::malloc(HashMap.sizeof);
    map.capacity = capacity;
    map.count = 0;
    map.mask = capacity - 1;

    // Allocate entries array via malloc (contiguous needed for indexing)
    map.entries = (HashEntry*)mem::malloc(HashEntry.sizeof * capacity);
    for (uint i = 0; i < capacity; i++) {
        map.entries[i].key = null;
        map.entries[i].value = null;
    }

    return map;
}

fn Value* hashmap_get(HashMap* map, Value* key) {
    uint h = hash_value(key) & map.mask;
    for (uint i = 0; i < map.capacity; i++) {
        uint idx = (h + i) & map.mask;
        HashEntry* entry = &map.entries[idx];
        if (entry.key == null) return null;  // empty slot
        if (values_equal(entry.key, key)) return entry.value;
    }
    return null;
}

fn void hashmap_grow(HashMap* map, Interp* interp) {
    uint old_cap = map.capacity;
    HashEntry* old_entries = map.entries;
    uint new_cap = old_cap * 2;
    uint new_mask = new_cap - 1;

    // Allocate new entries array via malloc
    HashEntry* new_entries = (HashEntry*)mem::malloc(HashEntry.sizeof * new_cap);
    for (uint i = 0; i < new_cap; i++) {
        new_entries[i].key = null;
        new_entries[i].value = null;
    }

    map.entries = new_entries;
    map.capacity = new_cap;
    map.mask = new_mask;
    map.count = 0;

    // Rehash
    for (uint i = 0; i < old_cap; i++) {
        if (old_entries[i].key != null) {
            uint h = hash_value(old_entries[i].key) & new_mask;
            for (uint j = 0; j < new_cap; j++) {
                uint idx = (h + j) & new_mask;
                if (new_entries[idx].key == null) {
                    new_entries[idx].key = old_entries[i].key;
                    new_entries[idx].value = old_entries[i].value;
                    map.count++;
                    break;
                }
            }
        }
    }
    mem::free(old_entries);
}

fn void hashmap_set(HashMap* map, Value* key, Value* value, Interp* interp) {
    // Grow at 70% load
    if (map.count * 10 >= map.capacity * 7) {
        hashmap_grow(map, interp);
    }

    // Promote key/value to root_scope — hashmap lives in root_scope,
    // so its entries must survive child scope release
    key = promote_to_root(key, interp);
    value = promote_to_root(value, interp);

    uint h = hash_value(key) & map.mask;
    for (uint i = 0; i < map.capacity; i++) {
        uint idx = (h + i) & map.mask;
        HashEntry* entry = &map.entries[idx];
        if (entry.key == null) {
            entry.key = key;
            entry.value = value;
            map.count++;
            return;
        }
        if (values_equal(entry.key, key)) {
            entry.value = value;
            return;
        }
    }
}

fn bool hashmap_remove(HashMap* map, Value* key) {
    uint h = hash_value(key) & map.mask;
    for (uint i = 0; i < map.capacity; i++) {
        uint idx = (h + i) & map.mask;
        HashEntry* entry = &map.entries[idx];
        if (entry.key == null) return false;
        if (values_equal(entry.key, key)) {
            // Backward shift delete
            uint j = idx;
            for (;;) {
                uint next = (j + 1) & map.mask;
                if (map.entries[next].key == null) break;
                uint natural = hash_value(map.entries[next].key) & map.mask;
                // Check if next entry would be displaced by removing j
                // If natural position is between (j+1) and next, it's fine where it is
                bool should_move;
                if (j < next) {
                    should_move = natural <= j || natural > next;
                } else {
                    should_move = natural <= j && natural > next;
                }
                if (!should_move) break;
                map.entries[j] = map.entries[next];
                j = next;
            }
            map.entries[j].key = null;
            map.entries[j].value = null;
            map.count--;
            return true;
        }
    }
    return false;
}

fn Value* make_hashmap(Interp* interp, uint capacity) {
    // Allocate value in root_scope so it persists
    main::ScopeRegion* saved_scope = interp.current_scope;
    interp.current_scope = interp.root_scope;
    Value* v = interp.alloc_value();
    v.tag = HASHMAP;
    main::scope_register_dtor(interp.root_scope, (void*)v, &scope_dtor_value);
    v.hashmap_val = hashmap_new(capacity, interp);
    interp.current_scope = saved_scope;
    return v;
}

// Dict primitives
fn Value* prim_dict(Value*[] args, Env* env, Interp* interp) {
    if (args.len % 2 != 0) return raise_error(interp, "dict: expected even number of args");
    uint cap = 16;
    while (cap < (uint)args.len * 2) cap *= 2;
    Value* v = make_hashmap(interp, cap);
    for (usz i = 0; i < args.len; i += 2) {
        hashmap_set(v.hashmap_val, args[i], args[i + 1], interp);
    }
    return v;
}

fn Value* prim_dict_set(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3) return raise_error(interp, "dict-set!: expected 3 arguments");
    if (args[0].tag != HASHMAP) return raise_error(interp, "dict-set!: expected dict");
    hashmap_set(args[0].hashmap_val, args[1], args[2], interp);
    return args[0];
}

// prim_dict_has, prim_dict_remove, prim_dict_keys, prim_dict_values
// inlined into their dispatch wrappers (prim_has, prim_remove, prim_keys, prim_values)


// =============================================================================
// PHASE 1-7 NEW PRIMITIVES
// =============================================================================

// --- Phase 1: Float conversions ---

fn Value* prim_exact_to_inexact(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "exact->inexact: expected number");
    return make_double(interp, to_double(args[0]));
}

fn Value* prim_inexact_to_exact(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "inexact->exact: expected number");
    return make_int(interp, (long)to_double(args[0]));
}

// --- Phase 2: Math library ---

import std::math;

// C math functions (linked via -lm)
extern fn double c_sin(double x) @extern("sin");
extern fn double c_cos(double x) @extern("cos");
extern fn double c_tan(double x) @extern("tan");
extern fn double c_asin(double x) @extern("asin");
extern fn double c_acos(double x) @extern("acos");
extern fn double c_atan(double x) @extern("atan");
extern fn double c_atan2(double y, double x) @extern("atan2");
extern fn double c_exp(double x) @extern("exp");
extern fn double c_log(double x) @extern("log");
extern fn double c_log10(double x) @extern("log10");
extern fn double c_pow(double x, double y) @extern("pow");
extern fn double c_sqrt(double x) @extern("sqrt");
extern fn double c_floor(double x) @extern("floor");
extern fn double c_ceil(double x) @extern("ceil");
extern fn double c_round(double x) @extern("round");
extern fn double c_fabs(double x) @extern("fabs");

fn Value* prim_sin(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "sin: expected number");
    return make_double(interp, c_sin(to_double(args[0])));
}

fn Value* prim_cos(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "cos: expected number");
    return make_double(interp, c_cos(to_double(args[0])));
}

fn Value* prim_tan(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "tan: expected number");
    return make_double(interp, c_tan(to_double(args[0])));
}

fn Value* prim_asin(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "asin: expected number");
    return make_double(interp, c_asin(to_double(args[0])));
}

fn Value* prim_acos(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "acos: expected number");
    return make_double(interp, c_acos(to_double(args[0])));
}

fn Value* prim_atan(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "atan: expected number");
    return make_double(interp, c_atan(to_double(args[0])));
}

fn Value* prim_atan2(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_number(args[0]) || !is_number(args[1])) return raise_error(interp, "atan2: expected 2 numbers");
    return make_double(interp, c_atan2(to_double(args[0]), to_double(args[1])));
}

fn Value* prim_exp(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "exp: expected number");
    return make_double(interp, c_exp(to_double(args[0])));
}

fn Value* prim_log(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "log: expected number");
    return make_double(interp, c_log(to_double(args[0])));
}

fn Value* prim_log10(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "log10: expected number");
    return make_double(interp, c_log10(to_double(args[0])));
}

fn Value* prim_pow(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_number(args[0]) || !is_number(args[1])) return raise_error(interp, "pow: expected 2 numbers");
    return make_double(interp, c_pow(to_double(args[0]), to_double(args[1])));
}

fn Value* prim_sqrt(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "sqrt: expected number");
    return make_double(interp, c_sqrt(to_double(args[0])));
}

fn Value* prim_floor(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "floor: expected number");
    return make_int(interp, (long)c_floor(to_double(args[0])));
}

fn Value* prim_ceiling(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "ceiling: expected number");
    return make_int(interp, (long)c_ceil(to_double(args[0])));
}

fn Value* prim_round(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "round: expected number");
    return make_int(interp, (long)c_round(to_double(args[0])));
}

fn Value* prim_truncate(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "truncate: expected number");
    return make_int(interp, (long)to_double(args[0]));
}

fn Value* prim_abs(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "abs: expected number");
    if (is_double(args[0])) return make_double(interp, c_fabs(args[0].double_val));
    long n = args[0].int_val;
    return make_int(interp, n < 0 ? -n : n);
}

fn Value* prim_min(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_number(args[0]) || !is_number(args[1])) return raise_error(interp, "min: expected 2 numbers");
    if (is_double(args[0]) || is_double(args[1])) {
        double a = to_double(args[0]); double b = to_double(args[1]);
        return make_double(interp, a < b ? a : b);
    }
    return make_int(interp, args[0].int_val < args[1].int_val ? args[0].int_val : args[1].int_val);
}

fn Value* prim_max(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_number(args[0]) || !is_number(args[1])) return raise_error(interp, "max: expected 2 numbers");
    if (is_double(args[0]) || is_double(args[1])) {
        double a = to_double(args[0]); double b = to_double(args[1]);
        return make_double(interp, a > b ? a : b);
    }
    return make_int(interp, args[0].int_val > args[1].int_val ? args[0].int_val : args[1].int_val);
}

fn Value* prim_gcd(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return raise_error(interp, "gcd: expected 2 integers");
    long a = args[0].int_val; long b = args[1].int_val;
    if (a < 0) { a = -a; }
    if (b < 0) { b = -b; }
    while (b != 0) { long t = b; b = a % b; a = t; }
    return make_int(interp, a);
}

fn Value* prim_lcm(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return raise_error(interp, "lcm: expected 2 integers");
    long a = args[0].int_val; long b = args[1].int_val;
    if (a == 0 || b == 0) return make_int(interp, 0);
    if (a < 0) { a = -a; }
    if (b < 0) { b = -b; }
    // gcd
    long ga = a; long gb = b;
    while (gb != 0) { long t = gb; gb = ga % gb; ga = t; }
    return make_int(interp, (a / ga) * b);
}

// --- Phase 3: Sorting & Bitwise ---

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
                usz al = arr[(usz)j].str_val.len; usz bl = key.str_val.len;
                usz ml = al < bl ? al : bl;
                int cmp = 0;
                for (usz k = 0; k < ml; k++) {
                    if (arr[(usz)j].str_val.chars[k] != key.str_val.chars[k]) {
                        cmp = arr[(usz)j].str_val.chars[k] > key.str_val.chars[k] ? 1 : -1;
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

fn Value* prim_bitwise_and(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return raise_error(interp, "bitwise-and: expected 2 integers");
    return make_int(interp, args[0].int_val & args[1].int_val);
}

fn Value* prim_bitwise_or(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return raise_error(interp, "bitwise-or: expected 2 integers");
    return make_int(interp, args[0].int_val | args[1].int_val);
}

fn Value* prim_bitwise_xor(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return raise_error(interp, "bitwise-xor: expected 2 integers");
    return make_int(interp, args[0].int_val ^ args[1].int_val);
}

fn Value* prim_bitwise_not(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_int(args[0])) return raise_error(interp, "bitwise-not: expected integer");
    return make_int(interp, ~args[0].int_val);
}

fn Value* prim_lshift(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return raise_error(interp, "lshift: expected 2 integers");
    long shift = args[1].int_val;
    if (shift < 0 || shift >= 64) return make_int(interp, 0);
    return make_int(interp, args[0].int_val << shift);
}

fn Value* prim_rshift(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_int(args[0]) || !is_int(args[1])) return raise_error(interp, "rshift: expected 2 integers");
    long shift = args[1].int_val;
    if (shift < 0 || shift >= 64) return make_int(interp, 0);
    return make_int(interp, args[0].int_val >> shift);
}

// --- Phase 5: String operations & type predicates ---

fn Value* prim_string_contains(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != STRING || args[1].tag != STRING) return raise_error(interp, "string-contains?: expected 2 strings");
    char[] haystack = args[0].str_val.chars[:args[0].str_val.len];
    char[] needle = args[1].str_val.chars[:args[1].str_val.len];
    if (needle.len == 0) return make_symbol(interp, interp.sym_true);
    if (needle.len > haystack.len) return make_nil(interp);
    for (usz i = 0; i <= haystack.len - needle.len; i++) {
        bool match = true;
        for (usz j = 0; j < needle.len; j++) {
            if (haystack[i + j] != needle[j]) { match = false; break; }
        }
        if (match) return make_symbol(interp, interp.sym_true);
    }
    return make_nil(interp);
}

fn Value* prim_string_index_of(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != STRING || args[1].tag != STRING) return raise_error(interp, "string-index-of: expected 2 strings");
    char[] haystack = args[0].str_val.chars[:args[0].str_val.len];
    char[] needle = args[1].str_val.chars[:args[1].str_val.len];
    if (needle.len == 0) return make_int(interp, 0);
    if (needle.len > haystack.len) return make_int(interp, -1);
    for (usz i = 0; i <= haystack.len - needle.len; i++) {
        bool match = true;
        for (usz j = 0; j < needle.len; j++) {
            if (haystack[i + j] != needle[j]) { match = false; break; }
        }
        if (match) return make_int(interp, (long)i);
    }
    return make_int(interp, -1);
}

fn Value* prim_string_replace(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3 || args[0].tag != STRING || args[1].tag != STRING || args[2].tag != STRING) {
        return raise_error(interp, "string-replace: expected 3 strings");
    }
    char[] str = args[0].str_val.chars[:args[0].str_val.len];
    char[] old = args[1].str_val.chars[:args[1].str_val.len];
    char[] new_str = args[2].str_val.chars[:args[2].str_val.len];
    if (old.len == 0) return args[0]; // nothing to replace
    Value* result = interp.alloc_value();
    result.tag = STRING;
    result.str_val = strval_new(str.len);
    usz i = 0;
    while (i < str.len) {
        if (i + old.len <= str.len) {
            bool match = true;
            for (usz j = 0; j < old.len; j++) {
                if (str[i + j] != old[j]) { match = false; break; }
            }
            if (match) {
                strval_ensure(result.str_val, new_str.len);
                for (usz j = 0; j < new_str.len; j++) {
                    result.str_val.chars[result.str_val.len++] = new_str[j];
                }
                i += old.len;
                continue;
            }
        }
        strval_push(result.str_val, str[i]);
        i++;
    }
    result.str_val.chars[result.str_val.len] = 0;
    return result;
}

fn Value* prim_char_at(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != STRING || !is_int(args[1])) return raise_error(interp, "char-at: expected string and int");
    char[] bytes = args[0].str_val.chars[:args[0].str_val.len];
    usz cp_count = utf8_strlen(bytes);
    long idx = args[1].int_val;
    if (idx < 0) idx += (long)cp_count;
    if (idx < 0 || idx >= (long)cp_count) return raise_error(interp, "char-at: index out of range");
    // Find byte offset of the idx-th codepoint
    usz byte_off = utf8_byte_offset(bytes, (usz)idx);
    usz cplen = utf8_codepoint_len(bytes[byte_off]);
    if (byte_off + cplen > bytes.len) cplen = bytes.len - byte_off;
    return make_string(interp, bytes[byte_off:cplen]);
}

fn Value* prim_string_repeat(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != STRING || !is_int(args[1])) return raise_error(interp, "string-repeat: expected string and int");
    long n = args[1].int_val;
    if (n <= 0) return make_string(interp, "");
    Value* result = interp.alloc_value();
    result.tag = STRING;
    result.str_val = strval_new(args[0].str_val.len * (usz)n);
    for (long i = 0; i < n; i++) {
        strval_ensure(result.str_val, args[0].str_val.len);
        for (usz j = 0; j < args[0].str_val.len; j++) {
            result.str_val.chars[result.str_val.len++] = args[0].str_val.chars[j];
        }
    }
    result.str_val.chars[result.str_val.len] = 0;
    return result;
}

fn Value* prim_is_boolean(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "boolean?: expected 1 argument");
    if (is_nil(args[0])) return make_symbol(interp, interp.sym_true); // false is nil
    if (is_symbol(args[0]) && args[0].sym_val == interp.sym_true) return make_symbol(interp, interp.sym_true);
    return make_nil(interp);
}

fn Value* prim_is_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "list?: expected 1 argument");
    Value* v = args[0];
    if (is_nil(v)) return make_symbol(interp, interp.sym_true); // empty list is a list
    // Check if proper list (nil-terminated cons chain)
    while (is_cons(v)) { v = v.cons_val.cdr; }
    return is_nil(v) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_procedure(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "procedure?: expected 1 argument");
    ValueTag t = args[0].tag;
    return (t == CLOSURE || t == PRIMITIVE || t == PARTIAL_PRIM || t == METHOD_TABLE) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_zero(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "zero?: expected number");
    return to_double(args[0]) == 0.0 ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_positive(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "positive?: expected number");
    return to_double(args[0]) > 0.0 ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_negative(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_number(args[0])) return raise_error(interp, "negative?: expected number");
    return to_double(args[0]) < 0.0 ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_even(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_int(args[0])) return raise_error(interp, "even?: expected integer");
    return (args[0].int_val % 2 == 0) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_is_odd(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_int(args[0])) return raise_error(interp, "odd?: expected integer");
    return (args[0].int_val % 2 != 0) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

// Helper: append a char[] slice to a StringVal
fn void strval_append(StringVal* sv, char[] s) {
    strval_ensure(sv, s.len);
    for (usz k = 0; k < s.len; k++) {
        sv.chars[sv.len++] = s[k];
    }
}

// Helper: append with width/precision padding
fn void strval_append_padded(StringVal* sv, char[] s, int width, int precision, bool left_align, bool has_precision) {
    // Apply precision (truncate) — only for string-like content
    char[] content = s;
    if (has_precision && precision >= 0 && (usz)precision < content.len) {
        content = content[:(usz)precision];
    }
    int content_len = (int)content.len;
    int pad = 0;
    if (width > content_len) pad = width - content_len;

    if (!left_align && pad > 0) {
        strval_ensure(sv, (usz)pad);
        for (int k = 0; k < pad; k++) sv.chars[sv.len++] = ' ';
    }
    strval_append(sv, content);
    if (left_align && pad > 0) {
        strval_ensure(sv, (usz)pad);
        for (int k = 0; k < pad; k++) sv.chars[sv.len++] = ' ';
    }
}

// Helper: append numeric string with width padding (no precision truncation)
fn void strval_append_num_padded(StringVal* sv, char[] s, int width, bool left_align) {
    int content_len = (int)s.len;
    int pad = 0;
    if (width > content_len) pad = width - content_len;

    if (!left_align && pad > 0) {
        strval_ensure(sv, (usz)pad);
        for (int k = 0; k < pad; k++) sv.chars[sv.len++] = ' ';
    }
    strval_append(sv, s);
    if (left_align && pad > 0) {
        strval_ensure(sv, (usz)pad);
        for (int k = 0; k < pad; k++) sv.chars[sv.len++] = ' ';
    }
}

// Helper: append a value in display form to a StringVal (like %s: no quotes on strings)
fn void format_append_display(StringVal* sv, Value* a, Interp* interp) {
    if (a == null || is_nil(a)) {
        strval_append(sv, "nil");
    } else if (is_int(a)) {
        char[32] nbuf;
        char[] nslice = io::bprintf(&nbuf, "%d", a.int_val)!!;
        strval_append(sv, nslice);
    } else if (is_double(a)) {
        char[64] nbuf;
        char[] nslice = io::bprintf(&nbuf, "%.15g", a.double_val)!!;
        strval_append(sv, nslice);
    } else if (a.tag == STRING) {
        strval_append(sv, a.str_val.chars[:a.str_val.len]);
    } else if (is_symbol(a)) {
        strval_append(sv, interp.symbols.get_name(a.sym_val));
    } else {
        strval_append(sv, "#<obj>");
    }
}

fn Value* prim_format(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != STRING) return raise_error(interp, "format: expected format string");
    char[] fmt = args[0].str_val.chars[:args[0].str_val.len];
    Value* result = interp.alloc_value();
    result.tag = STRING;
    result.str_val = strval_new(fmt.len);
    usz arg_idx = 1;

    for (usz i = 0; i < fmt.len; i++) {
        if (fmt[i] == '%' && i + 1 < fmt.len) {
            i++; // skip '%'

            // Parse flags
            bool left_align = false;
            if (i < fmt.len && fmt[i] == '-') {
                left_align = true;
                i++;
            }

            // Parse width
            int width = 0;
            bool has_width = false;
            while (i < fmt.len && fmt[i] >= '0' && fmt[i] <= '9') {
                width = width * 10 + (int)(fmt[i] - '0');
                has_width = true;
                i++;
            }

            // Parse precision
            int precision = -1;
            bool has_precision = false;
            if (i < fmt.len && fmt[i] == '.') {
                i++;
                precision = 0;
                has_precision = true;
                while (i < fmt.len && fmt[i] >= '0' && fmt[i] <= '9') {
                    precision = precision * 10 + (int)(fmt[i] - '0');
                    i++;
                }
            }

            if (i >= fmt.len) {
                // Trailing '%' with no directive — just emit '%'
                strval_push(result.str_val, '%');
                break;
            }

            char directive = fmt[i];

            if (directive == '%') {
                // %% → literal %
                strval_push(result.str_val, '%');
                continue;
            }

            // All other directives consume an argument
            if (arg_idx >= args.len) {
                return raise_error(interp, "format: not enough arguments");
            }
            Value* a = args[arg_idx++];

            switch (directive) {
                case 's':
                    // Display value (strings without quotes)
                    // Use a temp StringVal to capture the display output
                    StringVal* stmp = strval_new(32);
                    format_append_display(stmp, a, interp);
                    char[] sval = stmp.chars[:stmp.len];
                    strval_append_padded(result.str_val, sval, width, precision, left_align, has_precision);
                    mem::free(stmp.chars);
                    mem::free(stmp);

                case 'd':
                    // Decimal integer
                    if (!is_int(a)) return raise_error(interp, "format: %d expects integer");
                    char[32] dbuf;
                    char[] dval = io::bprintf(&dbuf, "%d", a.int_val)!!;
                    strval_append_num_padded(result.str_val, dval, width, left_align);

                case 'f':
                    // Fixed-point float
                    double fnum;
                    if (is_int(a)) {
                        fnum = (double)a.int_val;
                    } else if (is_double(a)) {
                        fnum = a.double_val;
                    } else {
                        return raise_error(interp, "format: %f expects number");
                    }
                    char[128] fbuf;
                    char[] fval;
                    if (has_precision) {
                        // Build format string dynamically for precision
                        char[16] ffmt;
                        char[] ffmt_s = io::bprintf(&ffmt, "%%.%df", precision)!!;
                        // We need to use the precision value; use bprintf with a fixed set
                        // Since C3 bprintf doesn't support dynamic format strings easily,
                        // we format with max precision and truncate
                        fval = io::bprintf(&fbuf, "%.20f", fnum)!!;
                        // Find decimal point and truncate to precision digits after it
                        usz dot_pos = 0;
                        bool found_dot = false;
                        for (usz k = 0; k < fval.len; k++) {
                            if (fval[k] == '.') { dot_pos = k; found_dot = true; break; }
                        }
                        if (found_dot && precision == 0) {
                            fval = fval[:dot_pos];
                        } else if (found_dot) {
                            usz end = dot_pos + 1 + (usz)precision;
                            if (end > fval.len) end = fval.len;
                            fval = fval[:end];
                        }
                    } else {
                        // Default: 6 decimal places
                        fval = io::bprintf(&fbuf, "%.6f", fnum)!!;
                    }
                    strval_append_num_padded(result.str_val, fval, width, left_align);

                case 'e':
                    // Scientific notation
                    double eval_num;
                    if (is_int(a)) {
                        eval_num = (double)a.int_val;
                    } else if (is_double(a)) {
                        eval_num = a.double_val;
                    } else {
                        return raise_error(interp, "format: %e expects number");
                    }
                    char[128] ebuf;
                    char[] eval_s;
                    if (has_precision) {
                        eval_s = io::bprintf(&ebuf, "%.20e", eval_num)!!;
                        // Find decimal point and truncate to precision digits after it,
                        // preserving the exponent part
                        usz edot = 0;
                        bool efound_dot = false;
                        for (usz k = 0; k < eval_s.len; k++) {
                            if (eval_s[k] == '.') { edot = k; efound_dot = true; break; }
                        }
                        // Find 'e' or 'E'
                        usz epos = eval_s.len;
                        for (usz k = 0; k < eval_s.len; k++) {
                            if (eval_s[k] == 'e' || eval_s[k] == 'E') { epos = k; break; }
                        }
                        if (efound_dot) {
                            usz trunc_end = edot + 1 + (usz)precision;
                            if (trunc_end > epos) trunc_end = epos;
                            // Build: digits before trunc_end + exponent part
                            char[128] etmp;
                            usz eidx = 0;
                            for (usz k = 0; k < trunc_end; k++) { etmp[eidx++] = eval_s[k]; }
                            for (usz k = epos; k < eval_s.len; k++) { etmp[eidx++] = eval_s[k]; }
                            eval_s = etmp[:eidx];
                        }
                    } else {
                        eval_s = io::bprintf(&ebuf, "%e", eval_num)!!;
                    }
                    strval_append_num_padded(result.str_val, eval_s, width, left_align);

                case 'x':
                    // Hexadecimal lowercase
                    if (!is_int(a)) return raise_error(interp, "format: %x expects integer");
                    char[32] xbuf;
                    char[] xval = io::bprintf(&xbuf, "%x", a.int_val)!!;
                    strval_append_num_padded(result.str_val, xval, width, left_align);

                case 'X':
                    // Hexadecimal uppercase
                    if (!is_int(a)) return raise_error(interp, "format: %X expects integer");
                    char[32] xubuf;
                    // C3 bprintf %X should give uppercase hex
                    char[] xuval = io::bprintf(&xubuf, "%X", a.int_val)!!;
                    strval_append_num_padded(result.str_val, xuval, width, left_align);

                case 'o':
                    // Octal
                    if (!is_int(a)) return raise_error(interp, "format: %o expects integer");
                    char[32] obuf;
                    char[] oval = io::bprintf(&obuf, "%o", a.int_val)!!;
                    strval_append_num_padded(result.str_val, oval, width, left_align);

                case 'b':
                    // Binary — manual conversion
                    if (!is_int(a)) return raise_error(interp, "format: %b expects integer");
                    char[68] bbuf;
                    usz bidx = 0;
                    long bval = a.int_val;
                    if (bval == 0) {
                        bbuf[0] = '0';
                        bidx = 1;
                    } else {
                        bool bneg = bval < 0;
                        // For negative, show minus sign then binary of absolute value
                        if (bneg) {
                            bbuf[bidx++] = '-';
                            bval = -bval;
                        }
                        // Write binary digits in reverse
                        char[64] brev;
                        usz brev_len = 0;
                        while (bval > 0) {
                            brev[brev_len++] = (char)('0' + (char)(bval % 2));
                            bval = bval / 2;
                        }
                        // Reverse into bbuf
                        for (usz k = 0; k < brev_len; k++) {
                            bbuf[bidx++] = brev[brev_len - 1 - k];
                        }
                    }
                    strval_append_num_padded(result.str_val, bbuf[:bidx], width, left_align);

                default:
                    // Unknown directive — emit as literal
                    strval_push(result.str_val, '%');
                    strval_push(result.str_val, directive);
            }
        } else {
            strval_push(result.str_val, fmt[i]);
        }
    }
    result.str_val.chars[result.str_val.len] = 0;
    return result;
}

fn Value* prim_cl_format(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != STRING) return raise_error(interp, "cl-format: expected format string");
    char[] fmt = args[0].str_val.chars[:args[0].str_val.len];
    Value* result = interp.alloc_value();
    result.tag = STRING;
    result.str_val = strval_new(fmt.len);
    usz arg_idx = 1;
    for (usz i = 0; i < fmt.len; i++) {
        if (fmt[i] == '~' && i + 1 < fmt.len) {
            char directive = fmt[i + 1];
            i++;
            if (directive == 'a' || directive == 's') {
                if (arg_idx < args.len) {
                    Value* a = args[arg_idx++];
                    if (a == null || is_nil(a)) {
                        char[] s = "nil";
                        strval_ensure(result.str_val, 3);
                        for (usz k = 0; k < 3; k++) {
                            result.str_val.chars[result.str_val.len++] = s[k];
                        }
                    } else if (is_int(a)) {
                        char[32] nbuf;
                        char[] nslice = io::bprintf(&nbuf, "%d", a.int_val)!!;
                        strval_ensure(result.str_val, nslice.len);
                        for (usz k = 0; k < nslice.len; k++) {
                            result.str_val.chars[result.str_val.len++] = nslice[k];
                        }
                    } else if (is_double(a)) {
                        char[64] nbuf;
                        char[] nslice = io::bprintf(&nbuf, "%.15g", a.double_val)!!;
                        strval_ensure(result.str_val, nslice.len);
                        for (usz k = 0; k < nslice.len; k++) {
                            result.str_val.chars[result.str_val.len++] = nslice[k];
                        }
                    } else if (a.tag == STRING) {
                        if (directive == 's') {
                            strval_push(result.str_val, '"');
                        }
                        strval_ensure(result.str_val, a.str_val.len);
                        for (usz k = 0; k < a.str_val.len; k++) {
                            result.str_val.chars[result.str_val.len++] = a.str_val.chars[k];
                        }
                        if (directive == 's') {
                            strval_push(result.str_val, '"');
                        }
                    } else if (is_symbol(a)) {
                        char[] sname = interp.symbols.get_name(a.sym_val);
                        strval_ensure(result.str_val, sname.len);
                        for (usz k = 0; k < sname.len; k++) {
                            result.str_val.chars[result.str_val.len++] = sname[k];
                        }
                    } else {
                        char[] s = "#<obj>";
                        strval_ensure(result.str_val, s.len);
                        for (usz k = 0; k < s.len; k++) {
                            result.str_val.chars[result.str_val.len++] = s[k];
                        }
                    }
                }
            } else if (directive == '~') {
                strval_push(result.str_val, '~');
            } else {
                strval_push(result.str_val, '~');
                strval_push(result.str_val, directive);
            }
        } else {
            strval_push(result.str_val, fmt[i]);
        }
    }
    result.str_val.chars[result.str_val.len] = 0;
    return result;
}

fn Value* prim_display(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "display: expected argument");
    Value* v = args[0];
    if (v == null || is_nil(v)) {
        io::print("nil");
    } else if (v.tag == STRING) {
        io::printf("%s", (ZString)v.str_val.chars); // No quotes for display
    } else {
        print_value(v, &interp.symbols);
    }
    return make_nil(interp);
}

// --- Phase 7: Macroexpand, eval, apply, bound? ---

fn Value* prim_macroexpand(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "macroexpand: expected quoted expression");
    Value* form = args[0];
    // Form should be a list (macro-name arg...)
    if (!is_cons(form)) return form; // not a macro call, return as-is
    Value* head = form.cons_val.car;
    if (head == null || !is_symbol(head)) return form;
    SymbolId sym = head.sym_val;
    // Check macro table using hash index
    usz mh = (usz)((uint)sym * 2654435761);
    usz mmask = interp.macro_hash_capacity - 1;
    for (usz probe = 0; probe < interp.macro_hash_capacity; probe++) {
        usz slot = (mh + probe) & mmask;
        usz midx = interp.macro_hash_index[slot];
        if (midx == usz.max) break; // empty slot
        if ((uint)interp.macro_table[midx].name == (uint)sym) {
            Expr* call_expr = value_to_expr(form, interp);
            if (call_expr == null) return form;
            EvalResult r = expand_pattern_macro(&interp.macro_table[midx], call_expr, interp);
            if (!r.error.has_error && r.value != null) return r.value;
            return form;
        }
    }
    return form;
}

fn Value* prim_eval(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "eval: expected expression");
    Expr* expr = value_to_expr(args[0], interp);
    if (expr == null) return raise_error(interp, "eval: could not convert to expression");
    Value* result = jit_eval(expr, interp.global_env, interp);
    if (result != null && result.tag == ERROR) return raise_error(interp, "eval: error during evaluation");
    return result;
}

fn Value* prim_apply(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "apply: expected function and argument list");
    Value* func = args[0];
    Value* arg_list = args[1];
    // Count args
    usz arg_count = 0;
    Value* cur = arg_list;
    while (is_cons(cur)) {
        arg_count++;
        cur = cur.cons_val.cdr;
    }
    // Use multi-arg apply (handles multi-param closures, variadics, etc.)
    return jit_apply_multi_args(interp, func, arg_list, arg_count);
}

fn Value* prim_bound(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_symbol(args[0])) return raise_error(interp, "bound?: expected symbol");
    SymbolId sym = args[0].sym_val;
    Env* lookup_env = env != null ? env : interp.global_env;
    Value* v = lookup_env.lookup(sym);
    return (v != null) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_error(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != STRING) return raise_error(interp, "error: expected string message");
    return raise_error(interp, args[0].str_val.chars[:args[0].str_val.len]);
}

fn Value* prim_error_message(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != ERROR) return raise_error(interp, "error-message: expected error value");
    return make_string(interp, args[0].str_val.chars[:args[0].str_val.len]);
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
            v.array_val.items[i] = promote_to_root(cur.cons_val.car, interp);
            cur = cur.cons_val.cdr;
        }
        v.array_val.length = count;
        return v;
    }

    Value* v = make_array(interp, args.len < 4 ? 4 : args.len);
    for (usz i = 0; i < args.len; i++) {
        v.array_val.items[i] = promote_to_root(args[i], interp);
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
    args[0].array_val.items[(usz)idx] = promote_to_root(args[2], interp);
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
    vec.items[vec.length++] = promote_to_root(args[1], interp);
    return make_nil(interp);
}

// --- Generic collection primitives ---

fn Value* prim_ref(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "ref: expected collection and key");
    Value* coll = args[0];

    // Array: (ref arr idx) — supports negative indexing
    if (coll.tag == ARRAY) {
        if (!is_int(args[1])) return raise_error(interp, "ref: array requires int index");
        long idx = args[1].int_val;
        long alen = (long)coll.array_val.length;
        if (idx < 0) idx += alen;
        if (idx < 0 || idx >= alen) { return raise_error(interp, "ref: array index out of bounds"); }
        return coll.array_val.items[(usz)idx];
    }

    // Dict: (ref dict key)
    if (coll.tag == HASHMAP) {
        Value* result = hashmap_get(coll.hashmap_val, args[1]);
        if (result == null) return make_nil(interp);
        return result;
    }

    // Cons/list: walk cons chain — supports negative indexing
    if (coll.tag == CONS) {
        if (!is_int(args[1])) return raise_error(interp, "ref: list requires int index");
        long idx = args[1].int_val;
        if (idx < 0) {
            long len = 0;
            Value* c = coll;
            while (is_cons(c)) { len++; c = c.cons_val.cdr; }
            idx += len;
            if (idx < 0) return raise_error(interp, "ref: list index out of bounds");
        }
        Value* current = coll;
        for (long i = 0; i < idx; i++) {
            if (!is_cons(current)) return raise_error(interp, "ref: list index out of bounds");
            current = cdr(current);
        }
        if (is_cons(current)) return car(current);
        // Dotted pair terminal: (ref (cons 10 20) 1) => 20
        if (idx > 0) return current;
        return raise_error(interp, "ref: list index out of bounds");
    }

    // String: (ref str idx) = char as int — supports negative indexing
    if (coll.tag == STRING) {
        if (!is_int(args[1])) return raise_error(interp, "ref: string requires int index");
        long idx = args[1].int_val;
        long slen = (long)coll.str_val.len;
        if (idx < 0) idx += slen;
        if (idx < 0 || idx >= slen) { return raise_error(interp, "ref: string index out of bounds"); }
        return make_int(interp, (long)coll.str_val.chars[(usz)idx]);
    }

    return raise_error(interp, "ref: expected array, dict, cons, or string");
}

fn Value* prim_push(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "push!: expected 2 arguments");
    if (!is_array(args[0])) return raise_error(interp, "push!: expected array");
    return prim_array_push(args, env, interp);
}

fn Value* prim_keys(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "keys: expected 1 argument");
    if (args[0].tag != HASHMAP) return raise_error(interp, "keys: expected dict");
    HashMap* map = args[0].hashmap_val;
    Value* result = make_nil(interp);
    for (uint i = 0; i < map.capacity; i++) {
        if (map.entries[i].key != null) {
            result = make_cons(interp, map.entries[i].key, result);
        }
    }
    return result;
}

fn Value* prim_values(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "values: expected 1 argument");
    if (args[0].tag != HASHMAP) return raise_error(interp, "values: expected dict");
    HashMap* map = args[0].hashmap_val;
    Value* result = make_nil(interp);
    for (uint i = 0; i < map.capacity; i++) {
        if (map.entries[i].key != null) {
            result = make_cons(interp, map.entries[i].value, result);
        }
    }
    return result;
}

fn Value* prim_has(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "has?: expected 2 arguments");
    if (args[0].tag != HASHMAP) return raise_error(interp, "has?: expected dict");
    Value* found = hashmap_get(args[0].hashmap_val, args[1]);
    return found != null ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_remove(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "remove!: expected 2 arguments");
    if (args[0].tag != HASHMAP) return raise_error(interp, "remove!: expected dict");
    hashmap_remove(args[0].hashmap_val, args[1]);
    return args[0];
}

// --- Phase 9: Set primitives (built on dict) ---

fn Value* prim_set(Value*[] args, Env* env, Interp* interp) {
    // Create a new hash map in root_scope, using args as keys, value = true
    uint cap = 16;
    Value* map_val = make_hashmap(interp, cap);
    HashMap* map = map_val.hashmap_val;
    Value* true_val = make_symbol(interp, interp.sym_true);
    for (usz i = 0; i < args.len; i++) {
        hashmap_set(map, args[i], true_val, interp);
    }
    return map_val;
}

fn Value* prim_set_add(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != HASHMAP) return raise_error(interp, "set-add: expected set and value");
    Value* true_val = make_symbol(interp, interp.sym_true);
    hashmap_set(args[0].hashmap_val, args[1], true_val, interp);
    return args[0];
}

fn Value* prim_set_remove(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != HASHMAP) return raise_error(interp, "set-remove: expected set and value");
    hashmap_remove(args[0].hashmap_val, args[1]);
    return args[0];
}

fn Value* prim_set_contains(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || args[0].tag != HASHMAP) return raise_error(interp, "set-contains?: expected set and value");
    Value* v = hashmap_get(args[0].hashmap_val, args[1]);
    return (v != null) ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_set_size(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != HASHMAP) return raise_error(interp, "set-size: expected set");
    return make_int(interp, (long)args[0].hashmap_val.count);
}

fn Value* prim_set_to_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != HASHMAP) return raise_error(interp, "set->list: expected set");
    HashMap* map = args[0].hashmap_val;
    Value* result = make_nil(interp);
    for (uint i = 0; i < map.capacity; i++) {
        if (map.entries[i].key != null) {
            result = make_cons(interp, map.entries[i].key, result);
        }
    }
    return result;
}

// --- Phase 12: Additional I/O and convenience ---

fn Value* prim_read_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != STRING) return raise_error(interp, "read-string: expected string");
    char[] src = args[0].str_val.chars[:args[0].str_val.len];
    EvalResult r = run(src, interp);
    if (r.error.has_error) return raise_error(interp, "read-string: parse/eval error");
    return r.value;
}

fn Value* prim_string_to_symbol(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != STRING) return raise_error(interp, "string->symbol: expected string");
    SymbolId sym = interp.symbols.intern(args[0].str_val.chars[:args[0].str_val.len]);
    return make_symbol(interp, sym);
}

fn Value* prim_symbol_to_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_symbol(args[0])) return raise_error(interp, "symbol->string: expected symbol");
    char[] name = interp.symbols.get_name(args[0].sym_val);
    return make_string(interp, name);
}

/// =============================================================================
// SECTION 7.5: TYPE SYSTEM PRIMITIVES
// =============================================================================

/**
 * (type-of v) -> symbol representing the type name
 */
fn Value* prim_type_of(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "type-of: expected 1 argument");
    SymbolId type_name = value_type_name(args[0], interp);
    return make_symbol(interp, type_name);
}

/**
 * (is? v 'TypeName) -> true if v's type is or is subtype of TypeName
 */
fn Value* prim_is_type(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "is?: expected 2 arguments");
    if (args[1] == null || args[1].tag != SYMBOL) {
        return raise_error(interp, "is?: second argument must be a symbol");
    }

    SymbolId target_name = args[1].sym_val;
    TypeId target_type = interp.types.lookup(target_name, &interp.symbols);
    if (target_type == INVALID_TYPE_ID) {
        return make_nil(interp);  // Unknown type
    }

    TypeId value_type = infer_value_type(args[0], interp);
    if (value_type == INVALID_TYPE_ID) {
        return make_nil(interp);
    }

    // Check for Any type (everything matches)
    TypeId any_type = interp.types.lookup(interp.sym_Any, &interp.symbols);
    if (target_type == any_type) {
        return make_symbol(interp, interp.sym_true);
    }

    // Check exact match or subtype
    if (interp.types.is_subtype(value_type, target_type)) {
        return make_symbol(interp, interp.sym_true);
    }

    return make_nil(interp);
}

/**
 * (instance? v) -> true if v is a user-defined type instance
 */
fn Value* prim_is_instance(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "instance?: expected 1 argument");
    bool result = args[0].tag == INSTANCE;
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (type-args v) -> list of inferred type argument symbols, or nil if not parametric.
 * Example: (type-args (Box 42)) => '(Int)
 */
fn Value* prim_type_args(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "type-args: expected 1 argument");
    if (args[0] == null || args[0].tag != INSTANCE) return make_nil(interp);
    Instance* inst = args[0].instance_val;
    if (inst == null || inst.type_arg_count == 0) return make_nil(interp);

    // Build a list of type name symbols from the inferred type args
    Value* result = make_nil(interp);
    // Build in reverse so first type arg is at head
    for (usz i = inst.type_arg_count; i > 0; i--) {
        TypeId tid = inst.type_args[i - 1];
        if (tid != INVALID_TYPE_ID) {
            TypeInfo* ti = interp.types.get(tid);
            if (ti != null) {
                result = make_cons(interp, make_symbol(interp, ti.name), result);
            } else {
                result = make_cons(interp, make_nil(interp), result);
            }
        } else {
            result = make_cons(interp, make_nil(interp), result);
        }
    }
    return result;
}

// --- Memory reclamation ---

fn Value* prim_free_bang(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "unsafe-free!: expected 1 argument");
    Value* v = args[0];
    if (v == null) return make_nil(interp);

    // Free heap-allocated backing storage and mark as error.
    // Note: Array struct is malloc'd, but HashMap/Instance structs are region-allocated.
    // Only free what was actually malloc'd.
    switch (v.tag) {
        case ARRAY:
            if (v.array_val != null) {
                if (v.array_val.items != null) mem::free(v.array_val.items);
                mem::free(v.array_val);  // Array struct is malloc'd in make_array
                v.array_val = null;
            }
        case HASHMAP:
            // HashMap struct is region-allocated, only entries buffer is malloc'd
            if (v.hashmap_val != null) {
                if (v.hashmap_val.entries != null) {
                    mem::free(v.hashmap_val.entries);
                    v.hashmap_val.entries = null;
                }
                v.hashmap_val.count = 0;
                v.hashmap_val.capacity = 0;
            }
        case INSTANCE:
            // Instance struct is malloc'd in make_instance
            if (v.instance_val != null) {
                mem::free(v.instance_val);
                v.instance_val = null;
            }
        case STRING:
        case ERROR:
            if (v.str_val != null) {
                if (v.str_val.chars != null) mem::free(v.str_val.chars);
                mem::free(v.str_val);
                v.str_val = null;
            }
        default: return make_nil(interp);  // No backing to free
    }
    // Mark as error so use-after-free is caught instead of silently becoming nil
    v.tag = ERROR;
    char[] msg = "use after unsafe-free!";
    v.str_val = strval_new(msg.len);
    for (usz i = 0; i < msg.len; i++) {
        v.str_val.chars[i] = msg[i];
    }
    v.str_val.chars[msg.len] = 0;
    v.str_val.len = msg.len;
    return make_nil(interp);
}

// =============================================================================
// ITERATOR PRIMITIVES
// =============================================================================

fn Value* prim_iterator_p(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    if (args[0].tag == ITERATOR) return make_symbol(interp, interp.sym_true);
    return make_nil(interp);
}

fn Value* prim_make_iterator(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != CLOSURE) {
        return raise_error(interp, "make-iterator: expected closure thunk");
    }
    return make_iterator(interp, args[0]);
}

fn Value* prim_next(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != ITERATOR) {
        return raise_error(interp, "next: expected iterator");
    }
    // Call the thunk closure — iterator_val is a Value* (CLOSURE)
    return jit_apply_value(args[0].iterator_val, make_nil(interp), interp);
}

fn Value* prim_collect(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != ITERATOR) {
        return raise_error(interp, "collect: expected iterator");
    }
    // Consume iterator into a list
    // Collect into stack buffer first, then build list in reverse
    Value*[4096] items;
    usz count = 0;
    Value* it = args[0];

    while (count < 4096) {
        Value* pair = jit_apply_value(it.iterator_val, make_nil(interp), interp);
        if (pair == null || pair.tag == NIL) break;
        if (pair.tag == ERROR) return pair;
        if (!is_cons(pair)) break;
        items[count++] = car(pair);
        Value* rest = cdr(pair);
        if (rest == null || rest.tag != ITERATOR) break;
        it = rest;
    }

    // Build list in reverse
    Value* result = make_nil(interp);
    for (isz i = (isz)count - 1; i >= 0; i--) {
        result = make_cons(interp, items[(usz)i], result);
    }
    return result;
}

fn Value* prim_to_array(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0].tag != ITERATOR) {
        return raise_error(interp, "to-array: expected iterator");
    }
    // Consume iterator into an array
    Value*[4096] items;
    usz count = 0;
    Value* it = args[0];

    while (count < 4096) {
        Value* pair = jit_apply_value(it.iterator_val, make_nil(interp), interp);
        if (pair == null || pair.tag == NIL) break;
        if (pair.tag == ERROR) return pair;
        if (!is_cons(pair)) break;
        items[count++] = car(pair);
        Value* rest = cdr(pair);
        if (rest == null || rest.tag != ITERATOR) break;
        it = rest;
    }

    Value* arr = make_array(interp, count);
    for (usz i = 0; i < count; i++) {
        arr.array_val.items[i] = items[i];
    }
    arr.array_val.length = count;
    return arr;
}

// ============================================================================
// COROUTINE PRIMITIVES
// ============================================================================

struct CoroutineThunkState {
    Value*  thunk;
    Interp* interp;
}

fn void coroutine_thunk_entry(void* arg) {
    CoroutineThunkState* state = (CoroutineThunkState*)arg;
    Value* result = jit_apply_value(state.thunk, null, state.interp);
    main::g_current_stack_ctx.result = result;
}

fn Value* prim_coroutine_p(Value*[] args, Env* env, Interp* interp) {
    if (args[0].tag == COROUTINE) return make_symbol(interp, interp.sym_true);
    return make_nil(interp);
}

fn Value* prim_coroutine(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "coroutine: expected a thunk argument");
    Value* thunk = args[0];
    if (thunk == null || thunk.tag != CLOSURE) {
        return raise_error(interp, "coroutine: argument must be a closure");
    }

    // Phase 4b: Promote thunk to root_scope so it survives call_scope release.
    // The coroutine may be resumed after the creating scope is freed.
    thunk = promote_to_root(thunk, interp);

    main::StackCtx* ctx = main::stack_ctx_create(&interp.stack_ctx_pool);
    if (ctx == null) return raise_error(interp, "coroutine: failed to create context");

    // Store thunk state on the context's user_data (lives as long as the context)
    CoroutineThunkState* state = (CoroutineThunkState*)mem::malloc(CoroutineThunkState.sizeof);
    state.thunk = thunk;
    state.interp = interp;
    ctx.user_data = state;

    main::stack_ctx_init(ctx, &coroutine_thunk_entry, state);

    return make_coroutine(interp, ctx);
}

fn Value* prim_resume(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return raise_error(interp, "resume: expected a coroutine argument");
    Value* coroutine_val = args[0];
    if (coroutine_val == null || coroutine_val.tag != COROUTINE) {
        return raise_error(interp, "resume: argument must be a coroutine");
    }

    main::StackCtx* ctx = coroutine_val.coroutine_val;
    if (ctx == null) return raise_error(interp, "resume: null coroutine");

    if (ctx.status == main::StackCtxStatus.CTX_COMPLETED || ctx.status == main::StackCtxStatus.CTX_DEAD) {
        return raise_error(interp, "resume: coroutine already completed");
    }

    Value* resume_val = args.len > 1 ? args[1] : make_nil(interp);
    interp.resume_value = resume_val;

    // Save interp state before entering coroutine
    Env*        saved_jit_env       = interp.jit_env;
    Env*        saved_match_env     = interp.match_env;
    usz         saved_eval_depth    = interp.eval_depth;
    InterpFlags saved_flags         = interp.flags;
    Expr*       saved_tco_expr      = interp.jit_tco_expr;
    Env*        saved_tco_env       = interp.jit_tco_env;
    usz         saved_reset_depth   = interp.reset_depth;
    usz         saved_handler_count = interp.handler_count;
    main::ScopeRegion* saved_scope = interp.current_scope;

    main::StackContext parent_ctx;
    if (ctx.status == main::StackCtxStatus.CTX_READY) {
        main::stack_ctx_switch_to(ctx, &parent_ctx);
    } else {
        main::stack_ctx_resume(ctx, &parent_ctx);
    }

    // Restore interp state
    interp.jit_env        = saved_jit_env;
    interp.match_env      = saved_match_env;
    interp.eval_depth     = saved_eval_depth;
    interp.flags          = saved_flags;
    interp.jit_tco_expr   = saved_tco_expr;
    interp.jit_tco_env    = saved_tco_env;
    interp.reset_depth    = saved_reset_depth;
    interp.handler_count  = saved_handler_count;
    interp.current_scope  = saved_scope;

    // Stack overflow — coroutine hit guard page
    if (ctx.status == main::StackCtxStatus.CTX_DEAD) {
        if (ctx.user_data != null) {
            mem::free(ctx.user_data);
            ctx.user_data = null;
        }
        return raise_error(interp, "stack overflow in coroutine");
    }

    if (ctx.status == main::StackCtxStatus.CTX_COMPLETED) {
        Value* result = (Value*)ctx.result;
        // Free thunk state
        if (ctx.user_data != null) {
            mem::free(ctx.user_data);
            ctx.user_data = null;
        }
        return result != null ? result : make_nil(interp);
    }

    // Coroutine yielded — copy value to caller's scope.
    // Yield values live in the coroutine's call_scope (created by jit_eval_in_call_scope).
    // When the coroutine completes, that scope is released, invalidating all yield values.
    // The caller may still hold pointers to previously yielded values, so we must
    // copy each yield value into the caller's scope (current_scope, restored above).
    if (interp.yield_value != null) {
        return copy_to_parent(interp.yield_value, interp);
    }
    return make_nil(interp);
}

fn Value* prim_yield(Value*[] args, Env* env, Interp* interp) {
    main::StackCtx* current = main::g_current_stack_ctx;
    if (current == null) {
        return raise_error(interp, "yield: not inside a coroutine");
    }

    Value* yield_val = args.len > 0 ? args[0] : make_nil(interp);
    interp.yield_value = yield_val;

    // Save interp state (same pattern as jit_shift_impl)
    Env*        saved_jit_env       = interp.jit_env;
    Env*        saved_match_env     = interp.match_env;
    usz         saved_eval_depth    = interp.eval_depth;
    InterpFlags saved_flags         = interp.flags;
    Expr*       saved_tco_expr      = interp.jit_tco_expr;
    Env*        saved_tco_env       = interp.jit_tco_env;
    usz         saved_reset_depth   = interp.reset_depth;
    usz         saved_handler_count = interp.handler_count;
    main::ScopeRegion* saved_scope = interp.current_scope;

    main::stack_ctx_suspend();

    // Resumed — restore interp state
    interp.jit_env        = saved_jit_env;
    interp.match_env      = saved_match_env;
    interp.eval_depth     = saved_eval_depth;
    interp.flags          = saved_flags;
    interp.jit_tco_expr   = saved_tco_expr;
    interp.jit_tco_env    = saved_tco_env;
    interp.reset_depth    = saved_reset_depth;
    interp.handler_count  = saved_handler_count;
    interp.current_scope  = saved_scope;

    return interp.resume_value != null ? interp.resume_value : make_nil(interp);
}



