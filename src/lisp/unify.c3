module lisp;

import std::core::mem;
import std::io;

// ============================================================
// Deduce Unification Engine
//
// Matches patterns with ?variables against dicts from relation scans.
// A binding environment maps ?var SymbolIds to Values.
// ============================================================

const usz MAX_BINDINGS = 32;

struct Bindings {
    SymbolId[MAX_BINDINGS] vars;
    Value*[MAX_BINDINGS]   vals;
    usz count;
}

fn void bindings_init(Bindings* b) {
    b.count = 0;
}

fn Value* bindings_lookup(Bindings* b, SymbolId vname) {
    for (usz i = 0; i < b.count; i++) {
        if ((uint)b.vars[i] == (uint)vname) return b.vals[i];
    }
    return null;
}

fn void? bindings_bind(Bindings* b, SymbolId vname, Value* val) {
    // Check if already bound
    Value* existing = bindings_lookup(b, vname);
    if (existing != null) {
        // Already bound — check consistency
        if (!deduce_val_eq(existing, val)) return TYPE_MISMATCH~;
        return;
    }
    if (b.count >= MAX_BINDINGS) return TYPE_MISMATCH~;
    b.vars[b.count] = vname;
    b.vals[b.count] = val;
    b.count++;
}

fn bool deduce_val_eq(Value* a, Value* b) {
    if (a == null && b == null) return true;
    if (a == null || b == null) return false;
    if (a.tag != b.tag) return false;
    switch (a.tag) {
        case INT: return a.int_val == b.int_val;
        case DOUBLE: return a.double_val == b.double_val;
        case STRING: {
            if (a.str_len != b.str_len) return false;
            for (usz i = 0; i < a.str_len; i++) {
                if (a.str_chars[i] != b.str_chars[i]) return false;
            }
            return true;
        }
        case SYMBOL: return a.sym_val == b.sym_val;
        case NIL: return true;
        default: return a == b;  // pointer equality for other types
    }
}

fn bool is_logic_var(Value* v, Interp* interp) {
    if (v == null || v.tag != SYMBOL) return false;
    char[] name = interp.symbols.get_name(v.sym_val);
    return name.len > 0 && name[0] == '?';
}

fn bool is_wildcard(Value* v, Interp* interp) {
    if (v == null || v.tag != SYMBOL) return false;
    char[] name = interp.symbols.get_name(v.sym_val);
    return name.len == 1 && name[0] == '_';
}

// ============================================================
// Match a pattern clause against a dict row
//
// Pattern: (relation-name ?var1 ?var2 literal _ ...)
// Row: dict with column keys
// Relation: provides column name ordering
//
// Returns void on match success, TYPE_MISMATCH~ on failure.
// ============================================================

fn void? match_clause_against_row(
    Value* clause,       // list: (rel-name arg1 arg2 ...)
    Value* row,          // dict from deduce-scan
    Relation* rel,       // relation for column names
    Bindings* bindings,
    Interp* interp
) {
    // Skip relation name (first element)
    Value* args = cdr(clause);

    for (usz col = 0; col < rel.col_count && is_cons(args); col++) {
        Value* pattern_elem = car(args);
        // Get column value from row dict
        Value* key = interp.alloc_value();
        key.tag = SYMBOL;
        key.sym_val = rel.columns[col];
        Value* col_val = hashmap_get(row.hashmap_val, key);

        if (is_wildcard(pattern_elem, interp)) {
            // _ matches anything
        } else if (is_logic_var(pattern_elem, interp)) {
            // ?var — bind or check consistency
            if (catch err = bindings_bind(bindings, pattern_elem.sym_val, col_val)) {
                return TYPE_MISMATCH~;  // inconsistent binding
            }
        } else {
            // Literal — must match exactly
            if (!deduce_val_eq(pattern_elem, col_val)) {
                return TYPE_MISMATCH~;
            }
        }
        args = cdr(args);
    }
}

// ============================================================
// Build result dict from bindings + projection
//
// If projection is nil, return all bindings.
// If projection is a list of ?vars, return only those.
// Strip ? from variable names in result keys.
// ============================================================

fn Value* build_result_dict(Bindings* bindings, Value* projection, Interp* interp) {
    Value* dict = make_hashmap(interp, 8);

    if (projection == null || projection.tag == NIL) {
        // Return all bindings
        for (usz i = 0; i < bindings.count; i++) {
            // Strip ? from variable name
            char[] name = interp.symbols.get_name(bindings.vars[i]);
            SymbolId stripped;
            if (name.len > 1 && name[0] == '?') {
                stripped = interp.symbols.intern(name[1..]);
            } else {
                stripped = bindings.vars[i];
            }
            Value* key = interp.alloc_value();
            key.tag = SYMBOL;
            key.sym_val = stripped;
            hashmap_set(dict.hashmap_val, key, bindings.vals[i], interp);
        }
    } else {
        // Return only projected variables
        Value* proj = projection;
        while (is_cons(proj)) {
            Value* pvar = car(proj);
            if (pvar.tag == SYMBOL) {
                Value* val = bindings_lookup(bindings, pvar.sym_val);
                if (val != null) {
                    char[] name = interp.symbols.get_name(pvar.sym_val);
                    SymbolId stripped;
                    if (name.len > 1 && name[0] == '?') {
                        stripped = interp.symbols.intern(name[1..]);
                    } else {
                        stripped = pvar.sym_val;
                    }
                    Value* key = interp.alloc_value();
                    key.tag = SYMBOL;
                    key.sym_val = stripped;
                    hashmap_set(dict.hashmap_val, key, val, interp);
                }
            }
            proj = cdr(proj);
        }
    }

    return dict;
}

// ============================================================
// (deduce-match relation pattern) → list of dicts
//
// Pattern is a quoted list: '(person ?name ?age _)
// Returns dicts with ?var bindings (? stripped from keys).
// ============================================================

fn Value* prim_deduce_match(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "deduce-match: expected (deduce-match relation '(pattern))");

    Value* rel_val = args[0];
    if (rel_val == null || rel_val.tag != FFI_HANDLE) {
        return raise_error(interp, "deduce-match: first argument must be a relation");
    }
    if (rel_val.ffi_val == null) return raise_error(interp, "deduce-match: relation handle is closed");
    Relation* rel = (Relation*)rel_val.ffi_val.lib_handle;

    Value* pattern = args[1];
    if (!is_cons(pattern)) return raise_error(interp, "deduce-match: pattern must be a list");

    // Scan all rows and match against pattern
    Value* all_rows = relation_scan_all(rel, interp);

    Value* result = make_nil(interp);
    Value*[512] matches;
    usz match_count = 0;

    Value* curr = all_rows;
    while (is_cons(curr) && match_count < 512) {
        Value* row = car(curr);
        Bindings bindings;
        bindings_init(&bindings);

        if (catch err = match_clause_against_row(pattern, row, rel, &bindings, interp)) {
            // Pattern didn't match this row — skip
        } else {
            Value* result_dict = build_result_dict(&bindings, null, interp);
            matches[match_count++] = result_dict;
        }
        curr = cdr(curr);
    }

    // Build list in order
    for (isz i = (isz)match_count - 1; i >= 0; i--) {
        result = make_cons(interp, matches[(usz)i], result);
    }
    return result;
}
