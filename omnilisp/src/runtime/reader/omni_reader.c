#define _POSIX_C_SOURCE 200809L
#include "omni_reader.h"
#include "../util/dstring.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

// Forward declarations
static Value* read_form(Reader* r);
static Value* read_list(Reader* r, char close);
static Value* read_string(Reader* r);
static Value* read_number(Reader* r);
static Value* read_symbol(Reader* r);
static Value* read_dispatch(Reader* r);
static Value* read_metadata(Reader* r);
static void skip_whitespace(Reader* r);
static void skip_comment(Reader* r);

// Character predicates
static int is_digit(char c) { return c >= '0' && c <= '9'; }
static int is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
// Check if byte is part of a UTF-8 multibyte sequence (high bit set)
// This allows Unicode identifiers like λ, π, α, etc.
static int is_utf8_byte(unsigned char c) { return c >= 0x80; }

static int is_sym_start(char c) {
    return is_alpha(c) || c == '_' || c == '+' || c == '-' || c == '*' ||
           c == '/' || c == '=' || c == '<' || c == '>' || c == '!' ||
           c == '?' || c == '&' || c == '%' || c == '|' || c == '.' ||
           is_utf8_byte((unsigned char)c);
}
static int is_sym_char(char c) {
    return is_sym_start(c) || is_digit(c) || c == ':' || c == '\'' ||
           is_utf8_byte((unsigned char)c);
}
static int is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',';
}

// Reader creation
Reader* reader_new(const char* input) {
    Reader* r = malloc(sizeof(Reader));
    if (!r) return NULL;
    r->input = input;
    r->pos = 0;
    r->len = strlen(input);
    r->line = 1;
    r->col = 1;
    return r;
}

void reader_free(Reader* r) {
    if (r) free(r);
}

// Peek and advance
static char peek(Reader* r) {
    if (r->pos >= r->len) return '\0';
    return r->input[r->pos];
}

static char peek_n(Reader* r, int n) {
    if (r->pos + n >= r->len) return '\0';
    return r->input[r->pos + n];
}

static char advance(Reader* r) {
    if (r->pos >= r->len) return '\0';
    char c = r->input[r->pos++];
    if (c == '\n') {
        r->line++;
        r->col = 1;
    } else {
        r->col++;
    }
    return c;
}

static int at_end(Reader* r) {
    return r->pos >= r->len;
}

// Skip whitespace and comments
static void skip_whitespace(Reader* r) {
    while (!at_end(r)) {
        char c = peek(r);
        if (is_whitespace(c)) {
            advance(r);
        } else if (c == ';') {
            skip_comment(r);
        } else {
            break;
        }
    }
}

static void skip_comment(Reader* r) {
    while (!at_end(r) && peek(r) != '\n') {
        advance(r);
    }
    if (peek(r) == '\n') advance(r);
}

// Main read function
Value* omni_read_expr(Reader* r) {
    skip_whitespace(r);
    return read_form(r);
}

Value* omni_read_all(Reader* r) {
    Value* forms = mk_nil();
    Value** tail = &forms;

    while (1) {
        skip_whitespace(r);
        if (at_end(r)) break;

        Value* form = read_form(r);
        if (is_error(form)) return form;

        Value* cell = mk_cell(form, mk_nil());
        *tail = cell;
        tail = &cell->cell.cdr;
    }

    return forms;
}

// Read a single form
static Value* read_form(Reader* r) {
    skip_whitespace(r);
    if (at_end(r)) return mk_error("Unexpected end of input");

    char c = peek(r);

    // Dispatch character #
    if (c == '#') {
        return read_dispatch(r);
    }

    // Metadata ^
    if (c == '^') {
        return read_metadata(r);
    }

    // Quote '
    if (c == '\'') {
        advance(r);
        Value* quoted = read_form(r);
        if (is_error(quoted)) return quoted;
        return mk_cell(mk_sym("quote"), mk_cell(quoted, mk_nil()));
    }

    // Quasiquote `
    if (c == '`') {
        advance(r);
        Value* quoted = read_form(r);
        if (is_error(quoted)) return quoted;
        return mk_cell(mk_sym("quasiquote"), mk_cell(quoted, mk_nil()));
    }

    // Unquote ~ and unquote-splicing ~@
    if (c == '~') {
        advance(r);
        if (peek(r) == '@') {
            advance(r);
            Value* form = read_form(r);
            if (is_error(form)) return form;
            return mk_cell(mk_sym("unquote-splicing"), mk_cell(form, mk_nil()));
        }
        Value* form = read_form(r);
        if (is_error(form)) return form;
        return mk_cell(mk_sym("unquote"), mk_cell(form, mk_nil()));
    }

    // List (...)
    if (c == '(') {
        advance(r);
        return read_list(r, ')');
    }

    // Array [...]
    if (c == '[') {
        advance(r);
        Value* items = read_list(r, ']');
        if (is_error(items)) return items;
        return mk_cell(mk_sym("array"), items);
    }

    // Type annotation {...}
    if (c == '{') {
        advance(r);
        Value* items = read_list(r, '}');
        if (is_error(items)) return items;
        return mk_cell(mk_sym("type"), items);
    }

    // String "..."
    if (c == '"') {
        return read_string(r);
    }

    // :foo is sugar for 'foo (quoted symbol)
    if (c == ':') {
        advance(r);  // skip the colon
        char buf[256];
        int len = 0;
        while (is_sym_char(peek(r)) && len < 255) {
            buf[len++] = peek(r);
            advance(r);
        }
        buf[len] = '\0';
        // (quote foo)
        return mk_cell(mk_sym("quote"), mk_cell(mk_sym(buf), mk_nil()));
    }

    // .field is sugar for (lambda (x) (get x 'field)) - getter function
    if (c == '.' && (is_alpha(peek_n(r, 1)) || peek_n(r, 1) == '_')) {
        advance(r);  // skip the dot
        char buf[256];
        int len = 0;
        while ((is_sym_char(peek(r)) && peek(r) != '.') && len < 255) {
            buf[len++] = peek(r);
            advance(r);
        }
        buf[len] = '\0';
        // (lambda (__x__) (get __x__ 'field))
        Value* param = mk_sym("__x__");
        Value* field_quoted = mk_cell(mk_sym("quote"), mk_cell(mk_sym(buf), mk_nil()));
        Value* get_call = mk_cell(mk_sym("get"),
                         mk_cell(param,
                         mk_cell(field_quoted, mk_nil())));
        Value* body = get_call;
        Value* params = mk_cell(param, mk_nil());
        return mk_cell(mk_sym("lambda"), mk_cell(params, mk_cell(body, mk_nil())));
    }

    // Number
    if (is_digit(c) || (c == '-' && is_digit(peek_n(r, 1)))) {
        return read_number(r);
    }

    // Symbol
    if (is_sym_start(c)) {
        return read_symbol(r);
    }

    advance(r);
    char msg[64];
    snprintf(msg, sizeof(msg), "Unexpected character: '%c' at line %d", c, r->line);
    return mk_error(msg);
}

// Read list until closing delimiter
static Value* read_list(Reader* r, char close) {
    Value* items = mk_nil();
    Value** tail = &items;

    while (1) {
        skip_whitespace(r);
        if (at_end(r)) {
            return mk_error("Unexpected end of input in list");
        }

        char c = peek(r);
        if (c == close) {
            advance(r);
            break;
        }

        // Ellipsis ... (three dots) - used in macro patterns
        if (c == '.' && peek_n(r, 1) == '.' && peek_n(r, 2) == '.') {
            // Check if followed by non-dot
            char after = peek_n(r, 3);
            if (after != '.') {
                advance(r); advance(r); advance(r);
                Value* cell = mk_cell(mk_sym("..."), mk_nil());
                *tail = cell;
                tail = &cell->cell.cdr;
                continue;
            }
        }

        // Rest marker .. (two dots followed by variable)
        if (c == '.' && peek_n(r, 1) == '.' && peek_n(r, 2) != '.') {
            advance(r); advance(r);
            Value* rest = read_form(r);
            if (is_error(rest)) return rest;
            *tail = mk_cell(mk_sym(".."), mk_cell(rest, mk_nil()));
            // After rest, expect closing bracket
            skip_whitespace(r);
            if (peek(r) != close) {
                return mk_error("Expected closing bracket after rest marker");
            }
            advance(r);
            break;
        }

        Value* form = read_form(r);
        if (is_error(form)) return form;

        Value* cell = mk_cell(form, mk_nil());
        *tail = cell;
        tail = &cell->cell.cdr;
    }

    return items;
}

// Read string with interpolation
static Value* read_string(Reader* r) {
    advance(r); // Skip opening "

    DString* ds = ds_new();
    if (!ds) return mk_error("OOM");

    Value* parts = mk_nil();
    Value** tail = &parts;
    int has_interpolation = 0;

    while (!at_end(r) && peek(r) != '"') {
        char c = peek(r);

        // String interpolation $
        if (c == '$') {
            advance(r);
            // Flush current string part
            if (ds_len(ds) > 0) {
                char* s = ds_take(ds);
                Value* str_part = mk_string_cstr(s);
                free(s);
                Value* cell = mk_cell(str_part, mk_nil());
                *tail = cell;
                tail = &cell->cell.cdr;
                ds = ds_new();
            }
            has_interpolation = 1;

            if (peek(r) == '(') {
                // $(expr)
                advance(r);
                Value* expr = read_list(r, ')');
                if (is_error(expr)) {
                    ds_free(ds);
                    return expr;
                }
                Value* cell = mk_cell(expr, mk_nil());
                *tail = cell;
                tail = &cell->cell.cdr;
            } else if (is_alpha(peek(r)) || peek(r) == '_') {
                // $name
                Value* var = read_symbol(r);
                if (is_error(var)) {
                    ds_free(ds);
                    return var;
                }
                Value* cell = mk_cell(var, mk_nil());
                *tail = cell;
                tail = &cell->cell.cdr;
            } else {
                ds_append_char(ds, '$');
            }
            continue;
        }

        // Escape sequences
        if (c == '\\') {
            advance(r);
            if (at_end(r)) {
                ds_free(ds);
                return mk_error("Unexpected end in string escape");
            }
            c = advance(r);
            switch (c) {
                case 'n': ds_append_char(ds, '\n'); break;
                case 't': ds_append_char(ds, '\t'); break;
                case 'r': ds_append_char(ds, '\r'); break;
                case '\\': ds_append_char(ds, '\\'); break;
                case '"': ds_append_char(ds, '"'); break;
                case '$': ds_append_char(ds, '$'); break;
                case '0': ds_append_char(ds, '\0'); break;
                default: ds_append_char(ds, c); break;
            }
            continue;
        }

        ds_append_char(ds, advance(r));
    }

    if (at_end(r)) {
        ds_free(ds);
        return mk_error("Unterminated string");
    }
    advance(r); // Skip closing "

    // Flush remaining
    if (ds_len(ds) > 0 || !has_interpolation) {
        char* s = ds_take(ds);
        if (has_interpolation) {
            Value* cell = mk_cell(mk_string_cstr(s), mk_nil());
            free(s);
            *tail = cell;
        } else {
            Value* result = mk_string_cstr(s);
            free(s);
            return result;
        }
    } else {
        ds_free(ds);
    }

    if (has_interpolation) {
        return mk_cell(mk_sym("str"), parts);
    }

    return mk_code("");
}

// Read number (int or float)
static Value* read_number(Reader* r) {
    DString* ds = ds_new();
    if (!ds) return mk_error("OOM");

    int is_float = 0;
    int is_hex = 0;
    int is_bin = 0;

    // Sign
    if (peek(r) == '-') {
        ds_append_char(ds, advance(r));
    }

    // Hex/binary prefix
    if (peek(r) == '0') {
        ds_append_char(ds, advance(r));
        if (peek(r) == 'x' || peek(r) == 'X') {
            ds_append_char(ds, advance(r));
            is_hex = 1;
        } else if (peek(r) == 'b' || peek(r) == 'B') {
            ds_append_char(ds, advance(r));
            is_bin = 1;
        }
    }

    // Digits
    while (!at_end(r)) {
        char c = peek(r);
        if (c == '_') {
            advance(r); // Skip underscores in numbers
            continue;
        }
        if (is_hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            ds_append_char(ds, advance(r));
        } else if (is_bin && (c == '0' || c == '1')) {
            ds_append_char(ds, advance(r));
        } else if (!is_hex && !is_bin && is_digit(c)) {
            ds_append_char(ds, advance(r));
        } else if (!is_hex && !is_bin && c == '.' && is_digit(peek_n(r, 1))) {
            is_float = 1;
            ds_append_char(ds, advance(r));
        } else if (!is_hex && !is_bin && (c == 'e' || c == 'E')) {
            is_float = 1;
            ds_append_char(ds, advance(r));
            if (peek(r) == '+' || peek(r) == '-') {
                ds_append_char(ds, advance(r));
            }
        } else {
            break;
        }
    }

    char* s = ds_take(ds);
    Value* result;

    if (is_float) {
        double val = strtod(s, NULL);
        result = mk_float(val);
    } else {
        long val;
        if (is_hex) {
            val = strtol(s + 2, NULL, 16);
        } else if (is_bin) {
            val = strtol(s + 2, NULL, 2);
        } else {
            val = strtol(s, NULL, 10);
        }
        result = mk_int(val);
    }

    free(s);
    return result;
}

// Read symbol (possibly with dot accessors)
static Value* read_symbol(Reader* r) {
    DString* ds = ds_new();
    if (!ds) return mk_error("OOM");

    // Read the base symbol (stop at . if followed by accessor pattern)
    while (!at_end(r) && is_sym_char(peek(r))) {
        char c = peek(r);
        // Check for dot accessor patterns: .( .[ or .field
        if (c == '.') {
            char next = peek_n(r, 1);
            if (next == '(' || next == '[' || is_alpha(next) || next == '_') {
                break;  // Stop here, handle accessor separately
            }
        }
        ds_append_char(ds, advance(r));
    }

    char* s = ds_take(ds);

    // Special symbols
    if (strcmp(s, "true") == 0) {
        free(s);
        return mk_sym("true");
    }
    if (strcmp(s, "false") == 0) {
        free(s);
        return mk_sym("false");
    }
    if (strcmp(s, "nothing") == 0) {
        free(s);
        return mk_nothing();
    }

    Value* result = mk_sym(s);
    free(s);

    // Check for dot accessors: obj.field, obj.(expr), obj.:key, obj.[slice]
    // Supports chaining: a.b.c -> (get (get a :b) :c)
    while (!at_end(r) && peek(r) == '.') {
        char next = peek_n(r, 1);

        // Computed key: obj.(expr) -> (get obj expr)
        if (next == '(') {
            advance(r);  // skip .
            advance(r);  // skip (
            Value* index_list = read_list(r, ')');
            if (is_error(index_list)) return index_list;
            // (get obj expr) - take first element of index_list
            Value* index = is_nil(index_list) ? mk_int(0) : car(index_list);
            result = mk_cell(mk_sym("get"),
                           mk_cell(result,
                           mk_cell(index, mk_nil())));
        }
        // Slice: obj.[start end] -> (array-slice obj start end)
        else if (next == '[') {
            advance(r);  // skip .
            advance(r);  // skip [
            Value* indices = read_list(r, ']');
            if (is_error(indices)) return indices;
            // (array-slice obj start end) if two elements
            int len = 0;
            Value* tmp = indices;
            while (!is_nil(tmp)) { len++; tmp = cdr(tmp); }
            if (len == 2) {
                result = mk_cell(mk_sym("array-slice"),
                               mk_cell(result, indices));
            } else if (len == 1) {
                // Single index in brackets - treat as get
                result = mk_cell(mk_sym("get"),
                               mk_cell(result,
                               mk_cell(car(indices), mk_nil())));
            } else {
                return mk_error("Invalid slice syntax: expected [start end] or [index]");
            }
        }
        // Bare field: obj.field -> (get obj 'field)
        else if (is_alpha(next) || next == '_') {
            advance(r);  // skip .
            // Read the field name as a symbol
            char buf[256];
            int len = 0;
            while ((is_sym_char(peek(r)) && peek(r) != '.') && len < 255) {
                buf[len++] = peek(r);
                advance(r);
            }
            buf[len] = '\0';
            // (quote field)
            Value* key = mk_cell(mk_sym("quote"), mk_cell(mk_sym(buf), mk_nil()));
            // (get obj 'field)
            result = mk_cell(mk_sym("get"),
                           mk_cell(result,
                           mk_cell(key, mk_nil())));
        }
        else {
            break;  // Not a valid accessor, stop
        }
    }

    return result;
}

// Read dispatch #...
static Value* read_dispatch(Reader* r) {
    advance(r); // Skip #
    if (at_end(r)) return mk_error("Unexpected end after #");

    char c = peek(r);

    // Dict literal #{...}
    if (c == '{') {
        advance(r);
        Value* items = read_list(r, '}');
        if (is_error(items)) return items;
        return mk_cell(mk_sym("dict"), items);
    }

    // Character literal #\x
    if (c == '\\') {
        advance(r);
        if (at_end(r)) return mk_error("Unexpected end in character literal");

        // Named characters
        if (is_alpha(peek(r))) {
            DString* ds = ds_new();
            while (!at_end(r) && is_alpha(peek(r))) {
                ds_append_char(ds, advance(r));
            }
            char* name = ds_take(ds);
            long charval;
            if (strcmp(name, "newline") == 0) charval = '\n';
            else if (strcmp(name, "space") == 0) charval = ' ';
            else if (strcmp(name, "tab") == 0) charval = '\t';
            else if (strcmp(name, "return") == 0) charval = '\r';
            else if (strlen(name) == 1) charval = name[0];
            else {
                free(name);
                return mk_error("Unknown character name");
            }
            free(name);
            return mk_char(charval);
        }

        long charval = advance(r);
        return mk_char(charval);
    }

    // Lambda shorthand #(...)
    if (c == '(') {
        advance(r);
        Value* body = read_list(r, ')');
        if (is_error(body)) return body;
        return mk_cell(mk_sym("fn*"), mk_cell(body, mk_nil()));
    }

    // Discard #_
    if (c == '_') {
        advance(r);
        Value* discarded = read_form(r);
        if (is_error(discarded)) return discarded;
        // Return nothing-placeholder, will be filtered
        return mk_sym("__discard__");
    }

    // Block comment #| ... |#
    if (c == '|') {
        advance(r);
        int depth = 1;
        while (!at_end(r) && depth > 0) {
            if (peek(r) == '#' && peek_n(r, 1) == '|') {
                advance(r); advance(r);
                depth++;
            } else if (peek(r) == '|' && peek_n(r, 1) == '#') {
                advance(r); advance(r);
                depth--;
            } else {
                advance(r);
            }
        }
        return read_form(r); // Read next form after comment
    }

    // Reader conditional #?
    if (c == '?') {
        advance(r);
        skip_whitespace(r);
        if (peek(r) != '(') return mk_error("Expected ( after #?");
        advance(r);
        Value* clauses = read_list(r, ')');
        if (is_error(clauses)) return clauses;
        return mk_cell(mk_sym("reader-cond"), clauses);
    }

    // Shebang #!
    if (c == '!') {
        skip_comment(r);
        return read_form(r);
    }

    // Raw string #raw"..."
    if (c == 'r' && peek_n(r, 1) == 'a' && peek_n(r, 2) == 'w' && peek_n(r, 3) == '"') {
        advance(r); advance(r); advance(r); // skip "raw"
        advance(r); // skip "
        DString* ds = ds_new();
        while (!at_end(r) && peek(r) != '"') {
            ds_append_char(ds, advance(r));
        }
        if (at_end(r)) {
            ds_free(ds);
            return mk_error("Unterminated raw string");
        }
        advance(r);
        char* s = ds_take(ds);
        Value* result = mk_code(s);
        free(s);
        return mk_cell(mk_sym("raw-string"), mk_cell(result, mk_nil()));
    }

    // Regex #r"..."
    if (c == 'r' && peek_n(r, 1) == '"') {
        advance(r); advance(r);
        DString* ds = ds_new();
        while (!at_end(r) && peek(r) != '"') {
            if (peek(r) == '\\') {
                ds_append_char(ds, advance(r));
                if (!at_end(r)) ds_append_char(ds, advance(r));
            } else {
                ds_append_char(ds, advance(r));
            }
        }
        if (at_end(r)) {
            ds_free(ds);
            return mk_error("Unterminated regex");
        }
        advance(r);
        char* s = ds_take(ds);
        Value* result = mk_code(s);
        free(s);
        return mk_cell(mk_sym("regex"), mk_cell(result, mk_nil()));
    }

    // Bytes #b"..."
    if (c == 'b' && peek_n(r, 1) == '"') {
        advance(r); advance(r);
        DString* ds = ds_new();
        while (!at_end(r) && peek(r) != '"') {
            if (peek(r) == '\\') {
                advance(r);
                if (peek(r) == 'x') {
                    advance(r);
                    char hex[3] = {0};
                    if (!at_end(r)) hex[0] = advance(r);
                    if (!at_end(r)) hex[1] = advance(r);
                    ds_append_char(ds, (char)strtol(hex, NULL, 16));
                } else {
                    ds_append_char(ds, advance(r));
                }
            } else {
                ds_append_char(ds, advance(r));
            }
        }
        if (at_end(r)) {
            ds_free(ds);
            return mk_error("Unterminated bytes");
        }
        advance(r);
        char* s = ds_take(ds);
        Value* result = mk_code(s);
        free(s);
        return mk_cell(mk_sym("bytes"), mk_cell(result, mk_nil()));
    }

    // UUID #uuid"..."
    if (c == 'u' && peek_n(r, 1) == 'u' && peek_n(r, 2) == 'i' && peek_n(r, 3) == 'd') {
        advance(r); advance(r); advance(r); advance(r);
        skip_whitespace(r);
        if (peek(r) != '"') return mk_error("Expected \" after #uuid");
        Value* str = read_string(r);
        if (is_error(str)) return str;
        return mk_cell(mk_sym("uuid"), mk_cell(str, mk_nil()));
    }

    // Path #path"..."
    if (c == 'p' && peek_n(r, 1) == 'a' && peek_n(r, 2) == 't' && peek_n(r, 3) == 'h') {
        advance(r); advance(r); advance(r); advance(r);
        skip_whitespace(r);
        if (peek(r) != '"') return mk_error("Expected \" after #path");
        Value* str = read_string(r);
        if (is_error(str)) return str;
        return mk_cell(mk_sym("path"), mk_cell(str, mk_nil()));
    }

    // Syntax quote #'
    if (c == '\'') {
        advance(r);
        Value* form = read_form(r);
        if (is_error(form)) return form;
        return mk_cell(mk_sym("syntax-quote"), mk_cell(form, mk_nil()));
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "Unknown dispatch character: #%c", c);
    return mk_error(msg);
}

// Read metadata ^:keyword or ^"docstring"
static Value* read_metadata(Reader* r) {
    advance(r); // Skip ^
    if (at_end(r)) return mk_error("Unexpected end after ^");

    Value* meta;
    if (peek(r) == ':') {
        // ^:keyword -> produces quoted symbol 'keyword
        // This way ^:private -> (with-meta 'private form)
        advance(r);
        Value* sym = read_symbol(r);
        if (is_error(sym)) return sym;
        // Quote it so it evaluates to itself
        meta = mk_cell(mk_sym("quote"), mk_cell(sym, mk_nil()));
    } else if (peek(r) == '"') {
        // ^"docstring" - strings are self-evaluating
        meta = read_string(r);
        if (is_error(meta)) return meta;
    } else if (peek(r) == '{') {
        // ^{:key val ...} - quote the whole map
        advance(r);
        Value* map_content = read_list(r, '}');
        if (is_error(map_content)) return map_content;
        meta = mk_cell(mk_sym("quote"), mk_cell(map_content, mk_nil()));
    } else {
        return mk_error("Expected :keyword, \"docstring\", or {map} after ^");
    }

    // Read the form this metadata applies to
    skip_whitespace(r);
    Value* form = read_form(r);
    if (is_error(form)) return form;

    return mk_cell(mk_sym("with-meta"), mk_cell(meta, mk_cell(form, mk_nil())));
}

// Convenience functions
Value* omni_parse(const char* input) {
    Reader* r = reader_new(input);
    if (!r) return mk_error("OOM");
    Value* result = omni_read_expr(r);
    reader_free(r);
    return result;
}

Value* omni_parse_all(const char* input) {
    Reader* r = reader_new(input);
    if (!r) return mk_error("OOM");
    Value* result = omni_read_all(r);
    reader_free(r);
    return result;
}
