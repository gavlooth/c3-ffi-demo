/*
 * json.c - JSON parsing and generation for OmniLisp
 *
 * Issue 28 P3: JSON Support
 *
 * Pure C implementation (no external dependencies).
 * Can be replaced with yyjson for better performance if needed.
 *
 * Type mapping:
 *   JSON object  -> OmniLisp Dict (#{ })
 *   JSON array   -> OmniLisp Array ([ ])
 *   JSON string  -> OmniLisp String
 *   JSON number  -> OmniLisp Int (if integer) or Float (if decimal)
 *   JSON boolean -> OmniLisp Bool
 *   JSON null    -> OmniLisp Nothing
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include "../include/omni.h"
#include "internal_types.h"

/* ============== JSON Parser State ============== */

typedef struct {
    const char* json;
    size_t pos;
    size_t len;
    char error[256];
} JsonParser;

/* Forward declarations */
static Obj* parse_value(JsonParser* p);

/* ============== Parser Helpers ============== */

static void set_error(JsonParser* p, const char* msg) {
    snprintf(p->error, sizeof(p->error), "JSON parse error at %zu: %s", p->pos, msg);
}

static char peek(JsonParser* p) {
    if (p->pos >= p->len) return '\0';
    return p->json[p->pos];
}

static char advance(JsonParser* p) {
    if (p->pos >= p->len) return '\0';
    return p->json[p->pos++];
}

static void skip_whitespace(JsonParser* p) {
    while (p->pos < p->len) {
        char c = p->json[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->pos++;
        } else {
            break;
        }
    }
}

static bool expect(JsonParser* p, char c) {
    skip_whitespace(p);
    if (peek(p) == c) {
        advance(p);
        return true;
    }
    return false;
}

/* ============== JSON Value Parsers ============== */

static Obj* parse_null(JsonParser* p) {
    if (p->pos + 4 <= p->len &&
        strncmp(p->json + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return mk_nothing();
    }
    set_error(p, "expected 'null'");
    return NULL;
}

static Obj* parse_true(JsonParser* p) {
    if (p->pos + 4 <= p->len &&
        strncmp(p->json + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return mk_bool(1);
    }
    set_error(p, "expected 'true'");
    return NULL;
}

static Obj* parse_false(JsonParser* p) {
    if (p->pos + 5 <= p->len &&
        strncmp(p->json + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return mk_bool(0);
    }
    set_error(p, "expected 'false'");
    return NULL;
}

static Obj* parse_number(JsonParser* p) {
    size_t start = p->pos;
    bool is_float = false;

    /* Optional negative sign */
    if (peek(p) == '-') advance(p);

    /* Integer part */
    if (!isdigit(peek(p))) {
        set_error(p, "expected digit");
        return NULL;
    }

    if (peek(p) == '0') {
        advance(p);
    } else {
        while (isdigit(peek(p))) advance(p);
    }

    /* Fractional part */
    if (peek(p) == '.') {
        is_float = true;
        advance(p);
        if (!isdigit(peek(p))) {
            set_error(p, "expected digit after decimal point");
            return NULL;
        }
        while (isdigit(peek(p))) advance(p);
    }

    /* Exponent */
    if (peek(p) == 'e' || peek(p) == 'E') {
        is_float = true;
        advance(p);
        if (peek(p) == '+' || peek(p) == '-') advance(p);
        if (!isdigit(peek(p))) {
            set_error(p, "expected digit in exponent");
            return NULL;
        }
        while (isdigit(peek(p))) advance(p);
    }

    /* Parse the number */
    size_t len = p->pos - start;
    char* num_str = malloc(len + 1);
    memcpy(num_str, p->json + start, len);
    num_str[len] = '\0';

    Obj* result;
    if (is_float) {
        double d = strtod(num_str, NULL);
        result = mk_float(d);
    } else {
        long l = strtol(num_str, NULL, 10);
        result = mk_int(l);
    }

    free(num_str);
    return result;
}

static Obj* parse_string(JsonParser* p) {
    if (peek(p) != '"') {
        set_error(p, "expected '\"'");
        return NULL;
    }
    advance(p);

    /* Estimate string length */
    size_t capacity = 64;
    char* buf = malloc(capacity);
    size_t len = 0;

    while (p->pos < p->len) {
        char c = advance(p);

        if (c == '"') {
            /* End of string */
            buf[len] = '\0';
            Obj* result = mk_string(buf);
            free(buf);
            return result;
        }

        if (c == '\\') {
            /* Escape sequence */
            c = advance(p);
            switch (c) {
                case '"':  c = '"';  break;
                case '\\': c = '\\'; break;
                case '/':  c = '/';  break;
                case 'b':  c = '\b'; break;
                case 'f':  c = '\f'; break;
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case 'u': {
                    /* Unicode escape \uXXXX */
                    if (p->pos + 4 > p->len) {
                        free(buf);
                        set_error(p, "incomplete unicode escape");
                        return NULL;
                    }
                    char hex[5];
                    memcpy(hex, p->json + p->pos, 4);
                    hex[4] = '\0';
                    p->pos += 4;
                    unsigned int codepoint = (unsigned int)strtol(hex, NULL, 16);

                    /* Convert to UTF-8 */
                    if (codepoint < 0x80) {
                        c = (char)codepoint;
                    } else if (codepoint < 0x800) {
                        if (len + 2 > capacity) {
                            capacity *= 2;
                            buf = realloc(buf, capacity);
                        }
                        buf[len++] = (char)(0xC0 | (codepoint >> 6));
                        c = (char)(0x80 | (codepoint & 0x3F));
                    } else {
                        if (len + 3 > capacity) {
                            capacity *= 2;
                            buf = realloc(buf, capacity);
                        }
                        buf[len++] = (char)(0xE0 | (codepoint >> 12));
                        buf[len++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        c = (char)(0x80 | (codepoint & 0x3F));
                    }
                    break;
                }
                default:
                    free(buf);
                    set_error(p, "invalid escape sequence");
                    return NULL;
            }
        }

        /* Add character to buffer */
        if (len + 1 >= capacity) {
            capacity *= 2;
            buf = realloc(buf, capacity);
        }
        buf[len++] = c;
    }

    free(buf);
    set_error(p, "unterminated string");
    return NULL;
}

static Obj* parse_array(JsonParser* p) {
    if (!expect(p, '[')) {
        set_error(p, "expected '['");
        return NULL;
    }

    Obj* arr = mk_array(8);
    skip_whitespace(p);

    /* Empty array */
    if (peek(p) == ']') {
        advance(p);
        return arr;
    }

    /* Parse elements */
    while (1) {
        Obj* elem = parse_value(p);
        if (!elem && p->error[0]) {
            return NULL;  /* Error already set */
        }
        array_push(arr, elem);

        skip_whitespace(p);
        if (peek(p) == ']') {
            advance(p);
            break;
        }

        if (!expect(p, ',')) {
            set_error(p, "expected ',' or ']'");
            return NULL;
        }
    }

    return arr;
}

static Obj* parse_object(JsonParser* p) {
    if (!expect(p, '{')) {
        set_error(p, "expected '{'");
        return NULL;
    }

    Obj* dict = mk_dict();
    skip_whitespace(p);

    /* Empty object */
    if (peek(p) == '}') {
        advance(p);
        return dict;
    }

    /* Parse key-value pairs */
    while (1) {
        skip_whitespace(p);

        /* Parse key (must be string) */
        Obj* key = parse_string(p);
        if (!key) return NULL;

        skip_whitespace(p);
        if (!expect(p, ':')) {
            set_error(p, "expected ':'");
            return NULL;
        }

        /* Parse value */
        Obj* value = parse_value(p);
        if (!value && p->error[0]) {
            return NULL;
        }

        dict_set(dict, key, value);

        skip_whitespace(p);
        if (peek(p) == '}') {
            advance(p);
            break;
        }

        if (!expect(p, ',')) {
            set_error(p, "expected ',' or '}'");
            return NULL;
        }
    }

    return dict;
}

static Obj* parse_value(JsonParser* p) {
    skip_whitespace(p);

    char c = peek(p);

    if (c == 'n') return parse_null(p);
    if (c == 't') return parse_true(p);
    if (c == 'f') return parse_false(p);
    if (c == '"') return parse_string(p);
    if (c == '[') return parse_array(p);
    if (c == '{') return parse_object(p);
    if (c == '-' || isdigit(c)) return parse_number(p);

    set_error(p, "unexpected character");
    return NULL;
}

/* ============== JSON Stringifier ============== */

/* Forward declaration */
static void stringify_value(Obj* obj, char** buf, size_t* len, size_t* cap, int indent, int depth);

static void buf_append(char** buf, size_t* len, size_t* cap, const char* str) {
    size_t str_len = strlen(str);
    while (*len + str_len + 1 > *cap) {
        *cap *= 2;
        *buf = realloc(*buf, *cap);
    }
    memcpy(*buf + *len, str, str_len);
    *len += str_len;
    (*buf)[*len] = '\0';
}

static void buf_append_char(char** buf, size_t* len, size_t* cap, char c) {
    if (*len + 2 > *cap) {
        *cap *= 2;
        *buf = realloc(*buf, *cap);
    }
    (*buf)[(*len)++] = c;
    (*buf)[*len] = '\0';
}

static void stringify_string(const char* str, char** buf, size_t* len, size_t* cap) {
    buf_append_char(buf, len, cap, '"');

    for (const char* p = str; *p; p++) {
        switch (*p) {
            case '"':  buf_append(buf, len, cap, "\\\""); break;
            case '\\': buf_append(buf, len, cap, "\\\\"); break;
            case '\b': buf_append(buf, len, cap, "\\b"); break;
            case '\f': buf_append(buf, len, cap, "\\f"); break;
            case '\n': buf_append(buf, len, cap, "\\n"); break;
            case '\r': buf_append(buf, len, cap, "\\r"); break;
            case '\t': buf_append(buf, len, cap, "\\t"); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*p);
                    buf_append(buf, len, cap, esc);
                } else {
                    buf_append_char(buf, len, cap, *p);
                }
        }
    }

    buf_append_char(buf, len, cap, '"');
}

static void add_indent(char** buf, size_t* len, size_t* cap, int indent, int depth) {
    if (indent <= 0) return;
    buf_append_char(buf, len, cap, '\n');
    for (int i = 0; i < indent * depth; i++) {
        buf_append_char(buf, len, cap, ' ');
    }
}

static void stringify_value(Obj* obj, char** buf, size_t* len, size_t* cap, int indent, int depth) {
    if (!obj || is_nothing(obj)) {
        buf_append(buf, len, cap, "null");
        return;
    }

    /* Immediate boolean */
    if (IS_IMMEDIATE_BOOL(obj)) {
        buf_append(buf, len, cap, obj == OMNI_TRUE ? "true" : "false");
        return;
    }

    /* Immediate int */
    if (IS_IMMEDIATE_INT(obj)) {
        char num[32];
        snprintf(num, sizeof(num), "%ld", INT_IMM_VALUE(obj));
        buf_append(buf, len, cap, num);
        return;
    }

    /* Immediate char - stringify as string */
    if (IS_IMMEDIATE_CHAR(obj)) {
        char str[8];
        long c = CHAR_IMM_VALUE(obj);
        if (c < 0x80) {
            str[0] = (char)c;
            str[1] = '\0';
        } else {
            snprintf(str, sizeof(str), "\\u%04lx", c);
        }
        stringify_string(str, buf, len, cap);
        return;
    }

    if (!IS_BOXED(obj)) {
        buf_append(buf, len, cap, "null");
        return;
    }

    switch (obj->tag) {
        case TAG_INT: {
            char num[32];
            snprintf(num, sizeof(num), "%ld", obj->i);
            buf_append(buf, len, cap, num);
            break;
        }

        case TAG_FLOAT: {
            char num[64];
            double d = obj->f;
            if (isnan(d)) {
                buf_append(buf, len, cap, "null");  /* JSON doesn't support NaN */
            } else if (isinf(d)) {
                buf_append(buf, len, cap, "null");  /* JSON doesn't support Infinity */
            } else {
                snprintf(num, sizeof(num), "%.17g", d);
                buf_append(buf, len, cap, num);
            }
            break;
        }

        case TAG_STRING:
        case TAG_SYM:
        case TAG_KEYWORD: {
            const char* str = obj->ptr ? (const char*)obj->ptr : "";
            stringify_string(str, buf, len, cap);
            break;
        }

        case TAG_ARRAY: {
            Array* arr = (Array*)obj->ptr;
            buf_append_char(buf, len, cap, '[');
            if (arr && arr->len > 0) {
                for (int i = 0; i < arr->len; i++) {
                    if (i > 0) buf_append_char(buf, len, cap, ',');
                    if (indent > 0) add_indent(buf, len, cap, indent, depth + 1);
                    stringify_value(arr->data[i], buf, len, cap, indent, depth + 1);
                }
                if (indent > 0) add_indent(buf, len, cap, indent, depth);
            }
            buf_append_char(buf, len, cap, ']');
            break;
        }

        case TAG_DICT: {
            Dict* d = (Dict*)obj->ptr;
            buf_append_char(buf, len, cap, '{');
            if (d) {
                /* Get keys and values */
                Obj* keys = dict_keys(obj);
                Array* keys_arr = keys ? (Array*)keys->ptr : NULL;

                bool first = true;
                if (keys_arr) {
                    for (int i = 0; i < keys_arr->len; i++) {
                        Obj* key = keys_arr->data[i];
                        Obj* val = dict_get(obj, key);

                        if (!first) buf_append_char(buf, len, cap, ',');
                        first = false;

                        if (indent > 0) add_indent(buf, len, cap, indent, depth + 1);

                        /* Stringify key (convert to string if needed) */
                        if (IS_BOXED(key) && (key->tag == TAG_STRING || key->tag == TAG_SYM || key->tag == TAG_KEYWORD)) {
                            stringify_string(key->ptr ? (const char*)key->ptr : "", buf, len, cap);
                        } else {
                            /* Convert non-string key to string */
                            char key_str[64];
                            snprintf(key_str, sizeof(key_str), "%ld", obj_to_int(key));
                            stringify_string(key_str, buf, len, cap);
                        }

                        buf_append_char(buf, len, cap, ':');
                        if (indent > 0) buf_append_char(buf, len, cap, ' ');

                        stringify_value(val, buf, len, cap, indent, depth + 1);
                    }
                }

                if (indent > 0 && !first) add_indent(buf, len, cap, indent, depth);
            }
            buf_append_char(buf, len, cap, '}');
            break;
        }

        case TAG_PAIR: {
            /* Convert list to JSON array */
            buf_append_char(buf, len, cap, '[');
            Obj* p = obj;
            bool first = true;
            while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
                if (!first) buf_append_char(buf, len, cap, ',');
                first = false;
                if (indent > 0) add_indent(buf, len, cap, indent, depth + 1);
                stringify_value(p->a, buf, len, cap, indent, depth + 1);
                p = p->b;
            }
            if (indent > 0 && !first) add_indent(buf, len, cap, indent, depth);
            buf_append_char(buf, len, cap, ']');
            break;
        }

        case TAG_NOTHING:
            buf_append(buf, len, cap, "null");
            break;

        default:
            buf_append(buf, len, cap, "null");
            break;
    }
}

/* ============== Public API ============== */

/*
 * TESTED - runtime/tests/test_json.c
 * prim_json_parse - Parse JSON string to OmniLisp data
 *
 * Args: json_str (string)
 * Returns: parsed data structure, or error on parse failure
 */
Obj* prim_json_parse(Obj* json_str_obj) {
    const char* json = NULL;

    if (IS_BOXED(json_str_obj) && (json_str_obj->tag == TAG_STRING || json_str_obj->tag == TAG_SYM)) {
        json = json_str_obj->ptr ? (const char*)json_str_obj->ptr : "";
    }

    if (!json) {
        return mk_error("json-parse: expected string argument");
    }

    JsonParser parser = {
        .json = json,
        .pos = 0,
        .len = strlen(json),
        .error = ""
    };

    Obj* result = parse_value(&parser);

    if (!result && parser.error[0]) {
        return mk_error(parser.error);
    }

    /* Check for trailing content */
    skip_whitespace(&parser);
    if (parser.pos < parser.len) {
        return mk_error("JSON parse error: unexpected trailing content");
    }

    return result;
}

/* TESTED */
/*
 * prim_json_stringify - Convert data to JSON string (compact)
 *
 * Args: data (any OmniLisp value)
 * Returns: JSON string
 */
Obj* prim_json_stringify(Obj* data) {
    size_t cap = 256;
    size_t len = 0;
    char* buf = malloc(cap);
    buf[0] = '\0';

    stringify_value(data, &buf, &len, &cap, 0, 0);

    Obj* result = mk_string(buf);
    free(buf);
    return result;
}

/*
 * prim_json_pretty - Convert data to pretty-printed JSON
 *
 * Args: data (any OmniLisp value)
 * Returns: pretty-printed JSON string (uses 2-space indent)
 */
Obj* prim_json_pretty(Obj* data) {
    int indent = 2;  /* Default indent */

    size_t cap = 256;
    size_t len = 0;
    char* buf = malloc(cap);
    buf[0] = '\0';

    stringify_value(data, &buf, &len, &cap, indent, 0);

    Obj* result = mk_string(buf);
    free(buf);
    return result;
}

/* TESTED - tests/test_json_file_io.omni */
/*
 * prim_json_read - Read and parse JSON from file
 *
 * Args: path (string)
 * Returns: parsed data structure, or error
 */
Obj* prim_json_read(Obj* path_obj) {
    /* Read file first - prim_io_read_file is declared in omni.h */
    Obj* content = prim_io_read_file(path_obj);

    if (IS_BOXED(content) && content->tag == TAG_ERROR) {
        return content;  /* Propagate error */
    }

    return prim_json_parse(content);
}

/* TESTED - tests/test_json_file_io.omni */
/*
 * prim_json_write - Stringify and write JSON to file
 *
 * Args: path (string), data (any value)
 * Returns: true on success, error on failure
 */
Obj* prim_json_write(Obj* path_obj, Obj* data) {
    /* Use compact JSON format */
    Obj* json_str = prim_json_stringify(data);

    /* Write to file - prim_io_write_file is declared in omni.h */
    return prim_io_write_file(path_obj, json_str);
}

/* TESTED */
/*
 * prim_json_get - Get nested value by path
 *
 * Args: data (object), path (string like "foo.bar.0")
 * Returns: value at path, or nothing if not found
 */
Obj* prim_json_get(Obj* data, Obj* path_obj) {
    if (!data) return mk_nothing();

    const char* path = NULL;
    if (IS_BOXED(path_obj) && (path_obj->tag == TAG_STRING || path_obj->tag == TAG_SYM)) {
        path = path_obj->ptr ? (const char*)path_obj->ptr : "";
    }
    if (!path || !*path) return data;

    /* Parse path components */
    char* path_copy = strdup(path);
    char* token = strtok(path_copy, ".");
    Obj* current = data;

    while (token && current) {
        if (IS_BOXED(current) && current->tag == TAG_DICT) {
            /* Dict lookup by string key */
            current = dict_get(current, mk_string(token));
            if (is_nothing(current)) current = NULL;
        }
        else if (IS_BOXED(current) && current->tag == TAG_ARRAY) {
            /* Array index */
            char* endptr;
            long idx = strtol(token, &endptr, 10);
            if (*endptr != '\0') {
                current = NULL;  /* Not a valid index */
            } else {
                current = array_get(current, (int)idx);
            }
        }
        else {
            current = NULL;
        }

        token = strtok(NULL, ".");
    }

    free(path_copy);
    return current ? current : mk_nothing();
}

/* TESTED - tests/test_json_valid_p.omni */
/*
 * prim_json_valid_p - Check if string is valid JSON
 *
 * Args: json_str (string)
 * Returns: true if valid JSON, false otherwise
 */
Obj* prim_json_valid_p(Obj* json_str_obj) {
    const char* json = NULL;

    if (IS_BOXED(json_str_obj) && (json_str_obj->tag == TAG_STRING || json_str_obj->tag == TAG_SYM)) {
        json = json_str_obj->ptr ? (const char*)json_str_obj->ptr : "";
    }

    if (!json) {
        return mk_bool(0);
    }

    JsonParser parser = {
        .json = json,
        .pos = 0,
        .len = strlen(json),
        .error = ""
    };

    Obj* result = parse_value(&parser);

    if (!result && parser.error[0]) {
        return mk_bool(0);
    }

    /* Check for trailing content */
    skip_whitespace(&parser);
    if (parser.pos < parser.len) {
        return mk_bool(0);
    }

    return mk_bool(1);
}
