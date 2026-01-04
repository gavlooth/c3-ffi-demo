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
static int is_sym_start(char c) {
    return is_alpha(c) || c == '_' || c == '+' || c == '-' || c == '*' ||
           c == '/' || c == '=' || c == '<' || c == '>' || c == '!' ||
           c == '?' || c == '&' || c == '%' || c == '|' || c == '.';
}
static int is_sym_char(char c) {
    return is_sym_start(c) || is_digit(c) || c == ':' || c == '\'';
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

    // Keyword :foo
    if (c == ':') {
        advance(r);
        Value* sym = read_symbol(r);
        if (is_error(sym)) return sym;
        return mk_cell(mk_sym("quote"), mk_cell(sym, mk_nil()));
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

        // Rest marker ..
        if (c == '.' && peek_n(r, 1) == '.') {
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
                Value* str_part = mk_code(s);
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
            Value* cell = mk_cell(mk_code(s), mk_nil());
            free(s);
            *tail = cell;
        } else {
            Value* result = mk_code(s);
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
        // Create float value - for now store as code
        result = mk_code(s);
        // TODO: Add T_FLOAT to types.h
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

// Read symbol
static Value* read_symbol(Reader* r) {
    DString* ds = ds_new();
    if (!ds) return mk_error("OOM");

    while (!at_end(r) && is_sym_char(peek(r))) {
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
        return mk_sym("nothing");
    }

    Value* result = mk_sym(s);
    free(s);
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
            return mk_cell(mk_sym("char"), mk_cell(mk_int(charval), mk_nil()));
        }

        long charval = advance(r);
        return mk_cell(mk_sym("char"), mk_cell(mk_int(charval), mk_nil()));
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
        // ^:keyword
        advance(r);
        Value* sym = read_symbol(r);
        if (is_error(sym)) return sym;
        meta = mk_cell(sym, mk_cell(mk_sym("true"), mk_nil()));
    } else if (peek(r) == '"') {
        // ^"docstring"
        Value* doc = read_string(r);
        if (is_error(doc)) return doc;
        meta = mk_cell(mk_sym("doc"), mk_cell(doc, mk_nil()));
    } else if (peek(r) == '{') {
        // ^{:key val ...}
        advance(r);
        meta = read_list(r, '}');
        if (is_error(meta)) return meta;
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
