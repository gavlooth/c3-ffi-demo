module lisp;

import std::io;
import pika;

// ============================================================
// Schema Validation — data-driven contracts via multiple dispatch
//
// Schemas are plain Omni data (lists + symbols). validate/explain
// walk the schema and check values. Dispatch on the schema tag symbol.
//
// Schema types:
//   'string, 'int, 'double, 'bool, 'any, 'nil
//   '(and s1 s2), '(or s1 s2), '(not s), '(maybe s)
//   '(> n), '(< n), '(>= n), '(<= n), '(= v)
//   '(re pattern)
//   '(map (name schema) ...), '(vector-of schema), '(tuple s1 s2 ...)
//   '(enum v1 v2 ...)
// ============================================================

// Internal: check if a value satisfies a schema. Returns true/false.
fn bool schema_check(Value* schema, Value* val, Interp* interp) {
    if (schema == null) return true;

    // Bare symbol: type check
    if (schema.tag == SYMBOL) {
        char[] name = interp.symbols.get_name(schema.sym_val);
        if (name.len == 3 && name[0] == 'a' && name[1] == 'n' && name[2] == 'y') return true;
        if (name.len == 3 && name[0] == 'n' && name[1] == 'i' && name[2] == 'l') return val == null || val.tag == NIL;
        if (name.len == 3 && name[0] == 'i' && name[1] == 'n' && name[2] == 't') return val != null && val.tag == INT;
        if (name.len == 6 && name[0] == 's' && name[1] == 't' && name[2] == 'r') return val != null && val.tag == STRING;  // "string"
        if (name.len == 6 && name[0] == 'd' && name[1] == 'o' && name[2] == 'u') return val != null && val.tag == DOUBLE;  // "double"
        if (name.len == 4 && name[0] == 'b' && name[1] == 'o' && name[2] == 'o' && name[3] == 'l') {
            return val != null && val.tag == SYMBOL;  // true is a symbol
        }
        // Check by interned name
        SymbolId s_string = interp.symbols.intern("string");
        SymbolId s_int = interp.symbols.intern("int");
        SymbolId s_double = interp.symbols.intern("double");
        SymbolId s_bool = interp.symbols.intern("bool");
        SymbolId s_any = interp.symbols.intern("any");
        SymbolId s_nil = interp.symbols.intern("nil");
        if (schema.sym_val == s_string) return val != null && val.tag == STRING;
        if (schema.sym_val == s_int) return val != null && val.tag == INT;
        if (schema.sym_val == s_double) return val != null && val.tag == DOUBLE;
        if (schema.sym_val == s_any) return true;
        if (schema.sym_val == s_nil) return val == null || val.tag == NIL;
        if (schema.sym_val == s_bool) return val != null && val.tag == SYMBOL;
        return false;
    }

    // List: dispatch on car
    if (is_cons(schema)) {
        Value* tag = car(schema);
        if (tag == null || tag.tag != SYMBOL) return false;

        SymbolId tag_sym = tag.sym_val;
        SymbolId s_and = interp.symbols.intern("and");
        SymbolId s_or = interp.symbols.intern("or");
        SymbolId s_not = interp.symbols.intern("not");
        SymbolId s_maybe = interp.symbols.intern("maybe");
        SymbolId s_gt = interp.symbols.intern(">");
        SymbolId s_lt = interp.symbols.intern("<");
        SymbolId s_ge = interp.symbols.intern(">=");
        SymbolId s_le = interp.symbols.intern("<=");
        SymbolId s_eq = interp.symbols.intern("=");
        SymbolId s_re = interp.symbols.intern("re");
        SymbolId s_map = interp.symbols.intern("map");
        SymbolId s_vector_of = interp.symbols.intern("vector-of");
        SymbolId s_tuple = interp.symbols.intern("tuple");
        SymbolId s_enum = interp.symbols.intern("enum");

        // (and s1 s2 ...)
        if (tag_sym == s_and) {
            Value* rest = cdr(schema);
            while (is_cons(rest)) {
                if (!schema_check(car(rest), val, interp)) return false;
                rest = cdr(rest);
            }
            return true;
        }

        // (or s1 s2 ...)
        if (tag_sym == s_or) {
            Value* rest = cdr(schema);
            while (is_cons(rest)) {
                if (schema_check(car(rest), val, interp)) return true;
                rest = cdr(rest);
            }
            return false;
        }

        // (not s)
        if (tag_sym == s_not) {
            Value* inner = car(cdr(schema));
            return !schema_check(inner, val, interp);
        }

        // (maybe s) — nil or matches s
        if (tag_sym == s_maybe) {
            if (val == null || val.tag == NIL) return true;
            return schema_check(car(cdr(schema)), val, interp);
        }

        // (> n), (< n), (>= n), (<= n)
        if (tag_sym == s_gt || tag_sym == s_lt ||
            tag_sym == s_ge || tag_sym == s_le) {
            if (val == null || (val.tag != INT && val.tag != DOUBLE)) return false;
            Value* bound = car(cdr(schema));
            if (bound == null || (bound.tag != INT && bound.tag != DOUBLE)) return false;
            double v = val.tag == INT ? (double)val.int_val : val.double_val;
            double b = bound.tag == INT ? (double)bound.int_val : bound.double_val;
            if (tag_sym == s_gt) return v > b;
            if (tag_sym == s_lt) return v < b;
            if (tag_sym == s_ge) return v >= b;
            return v <= b;
        }

        // (= v) — exact value match
        if (tag_sym == s_eq) {
            Value* expected = car(cdr(schema));
            if (expected == null || val == null) return expected == val;
            if (expected.tag == INT && val.tag == INT) return expected.int_val == val.int_val;
            if (expected.tag == STRING && val.tag == STRING) {
                if (expected.str_val.len != val.str_val.len) return false;
                for (usz i = 0; i < expected.str_val.len; i++) {
                    if (expected.str_val.chars[i] != val.str_val.chars[i]) return false;
                }
                return true;
            }
            return false;
        }

        // (re pattern) — Pika regex match
        if (tag_sym == s_re) {
            if (val == null || val.tag != STRING) return false;
            Value* pat = car(cdr(schema));
            if (pat == null || pat.tag != STRING) return false;
            // Call prim_re_fullmatch
            Value*[2] re_args;
            re_args[0] = pat;
            re_args[1] = val;
            Value* result = pika::prim_re_fullmatch(re_args[..], null, interp);
            return result != null && result.tag != NIL;
        }

        // (map (name schema) ...) — dict with required keys
        if (tag_sym == s_map) {
            if (val == null || val.tag != HASHMAP) return false;
            Value* entries = cdr(schema);
            while (is_cons(entries)) {
                Value* entry = car(entries);
                if (is_cons(entry)) {
                    Value* key_sym = car(entry);
                    Value* val_schema = car(cdr(entry));
                    // Look up key in dict
                    Value* dict_val = hashmap_get(val.hashmap_val, key_sym);
                    if (dict_val == null) return false;  // required key missing
                    if (val_schema != null && !schema_check(val_schema, dict_val, interp)) return false;
                }
                entries = cdr(entries);
            }
            return true;
        }

        // (vector-of schema) — array where all elements match
        if (tag_sym == s_vector_of) {
            if (val == null || val.tag != ARRAY) return false;
            Value* elem_schema = car(cdr(schema));
            for (usz i = 0; i < val.array_val.length; i++) {
                if (!schema_check(elem_schema, val.array_val.items[i], interp)) return false;
            }
            return true;
        }

        // (tuple s1 s2 ...) — fixed-length array with per-position schemas
        if (tag_sym == s_tuple) {
            if (val == null || val.tag != ARRAY) return false;
            Value* schemas = cdr(schema);
            usz idx = 0;
            while (is_cons(schemas)) {
                if (idx >= val.array_val.length) return false;
                if (!schema_check(car(schemas), val.array_val.items[idx], interp)) return false;
                schemas = cdr(schemas);
                idx++;
            }
            return idx == val.array_val.length;
        }

        // (enum v1 v2 ...) — value must be one of the listed values
        if (tag_sym == s_enum) {
            Value* options = cdr(schema);
            while (is_cons(options)) {
                Value* opt = car(options);
                // Simple equality check
                if (opt.tag == INT && val != null && val.tag == INT && opt.int_val == val.int_val) return true;
                if (opt.tag == STRING && val != null && val.tag == STRING) {
                    if (opt.str_val.len == val.str_val.len) {
                        bool eq = true;
                        for (usz i = 0; i < opt.str_val.len; i++) {
                            if (opt.str_val.chars[i] != val.str_val.chars[i]) { eq = false; break; }
                        }
                        if (eq) return true;
                    }
                }
                if (opt.tag == SYMBOL && val != null && val.tag == SYMBOL && opt.sym_val == val.sym_val) return true;
                options = cdr(options);
            }
            return false;
        }
    }

    return false;
}

// ============================================================
// (validate schema-name value) → true or nil
// ============================================================

fn Value* prim_validate(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "validate: expected (validate schema value)");

    Value* schema = args[0];
    // If schema is a symbol, try looking up as a named schema first
    if (schema.tag == SYMBOL) {
        Value* named = interp.global_env.lookup(schema.sym_val);
        if (named != null && named.tag != NIL) {
            schema = named;  // Found named schema
        }
        // Otherwise use the symbol itself as the schema (bare type: 'int, 'string, etc.)
    }

    Value* val = args[1];

    if (schema_check(schema, val, interp)) {
        return interp.global_env.lookup(interp.symbols.intern("true"));
    }
    return make_nil(interp);
}

// ============================================================
// (explain schema-name value) → list of (path message) or nil if valid
// ============================================================

fn Value* prim_explain(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return raise_error(interp, "explain: expected (explain schema value)");

    Value* schema = args[0];
    if (schema.tag == SYMBOL) {
        Value* named = interp.global_env.lookup(schema.sym_val);
        if (named != null && named.tag != NIL) {
            schema = named;
        }
    }

    Value* val = args[1];

    if (schema_check(schema, val, interp)) {
        return make_nil(interp);  // valid — no errors
    }

    // Simple explanation: return schema that failed
    Value* msg = make_string(interp, "validation failed");
    return make_cons(interp, msg, make_nil(interp));
}
