/*
 * OmniLisp Parser Implementation
 *
 * Uses the Pika packrat PEG parser for parsing OmniLisp syntax.
 * Grammar follows the OmniLisp design documents.
 */

#include "parser.h"
#include "pika.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

/* ============== Grammar Rule IDs ============== */

enum {
    R_EPSILON, R_ANY,
    R_CHAR_SPACE, R_CHAR_TAB, R_CHAR_NL, R_CHAR_CR, R_SEMICOLON,
    R_NOT_NL, R_OPT_NL, R_COMMENT_CHAR, R_COMMENT_BODY,
    R_SPACE,
    R_COMMENT,
    R_WS_ITEM,
    R_WS,
    /*
     * R_WS1: one-or-more whitespace items.
     *
     * IMPORTANT:
     *   R_WS is intentionally "zero-or-more" to make many grammar productions
     *   tolerant of formatting differences.
     *
     *   Some constructs, however, must force a separator to avoid ambiguity.
     *   Example: we must prevent `^:parent {Any}` from being parsed as the
     *   generic metadata form `^ <expr> <expr>`; `^:parent` is a reserved token
     *   used as a standalone metadata key marker in `define`, `let`, etc.
     */
    R_WS1,

    R_DIGIT, R_DIGIT1, R_INT, R_SIGN, R_SIGNED_INT,
    R_LT, R_GT, R_EQ, R_QUESTION, R_AT,
    R_FLOAT_FRAC, R_FLOAT,

    R_ALPHA, R_ALPHA_UPPER,
    R_SYM_ASTERISK, R_SYM_PLUS, R_SYM_MINUS, R_SYM_UNDERSCORE, R_SYM_PERCENT, R_SYM_SLASH,
    R_SYM_SPECIAL, R_SYM_CHAR, R_SYM_FIRST, R_SYM,
    /*
     * Reserved reader sugar:
     * - :name  => (quote name)
     *
     * IMPORTANT:
     *   This is intentionally NOT a normal symbol token, because ':' is reserved syntax.
     *   We normalize the AST toward the explicit quote form so printing can prefer `'name`.
     */
    R_COLON_QUOTED_SYMBOL,

    /*
     * Reserved metadata key tokens:
     * - ^:parent, ^:where, ^:seq, ...
     *
     * IMPORTANT:
     *   Metadata keys are NOT normal symbols because '^' and ':' are reserved syntax.
     *   We still represent the key in the AST as an OMNI_SYM whose string is "^:<key>"
     *   so later phases can recognize metadata markers by the "^:" prefix.
     */
    R_META_KEY,

    R_CHAR_ESCAPE, R_CHAR_LIT, R_NOT_DQUOTE, R_STRING_CHAR, R_STRING_INNER, R_STRING,

    /* Named character literals: #\newline, #\space, #\tab */
    R_NAMED_CHAR,

    /* Value-to-type conversion: #val 42 (calls value->type) */
    R_HASH_VAL,

    /* String formatting */
    R_FMT_STRING, R_CLF_STRING,

    R_LPAREN, R_RPAREN,
    R_LBRACKET, R_RBRACKET,
    R_LBRACE, R_RBRACE,
    R_DOT, R_CARET, R_COLON, R_EXCLAM, R_HASH, R_BACKSLASH,
    R_HASHBRACE, R_HASHPAREN, R_HASHBRACKET,
    R_HASH_FMT, R_HASH_CLF,

    R_QUOTE_CHAR, R_DQUOTE, R_QUASIQUOTE_CHAR, R_UNQUOTE_SPLICE_CHARS, R_UNQUOTE_CHAR,

    R_EXPR,
    R_ATOM,
    R_PATH_ROOT,

    R_LIST_INNER, R_LIST_SEQ,
    R_LIST,

    R_ARRAY_INNER, R_ARRAY_SEQ,
    R_ARRAY,

    R_TYPE_INNER, R_TYPE,
    R_PATH_SEGMENT, R_PATH_TAIL_ITEM, R_PATH_TAIL, R_PATH,
    R_METADATA,

    R_MATCH, R_MATCH_CLAUSE, R_MATCH_CLAUSES,
    R_WHEN,

    R_QUOTED,

    R_PROGRAM_INNER, R_PROGRAM_SEQ,
    R_PROGRAM,

    NUM_RULES
};

static PikaRule g_rules[NUM_RULES] = {{0}};
static int* g_rule_ids[NUM_RULES] = {NULL};
static bool g_grammar_initialized = false;

/* ============== Helper Functions ============== */

static int* ids(int count, ...) {
    int* arr = malloc(sizeof(int) * count);
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) arr[i] = va_arg(args, int);
    va_end(args);
    return arr;
}

/* ============== Semantic Actions ============== */

static OmniValue* act_int(PikaState* state, size_t pos, PikaMatch match) {
    char buf[64];
    size_t len = match.len > 63 ? 63 : match.len;
    memcpy(buf, state->input + pos, len);
    buf[len] = '\0';
    return omni_new_int(atol(buf));
}

static OmniValue* act_float(PikaState* state, size_t pos, PikaMatch match) {
    char buf[64];
    size_t len = match.len > 63 ? 63 : match.len;
    memcpy(buf, state->input + pos, len);
    buf[len] = '\0';
    return omni_new_float(atof(buf));
}

static OmniValue* act_sym(PikaState* state, size_t pos, PikaMatch match) {
    char* s = malloc(match.len + 1);
    memcpy(s, state->input + pos, match.len);
    s[match.len] = '\0';
    /*
     * Literal symbols with special runtime meaning.
     *
     * NOTE:
     *   These are not "keywords" in the Clojure sense; they are reserved names
     *   that evaluate to dedicated singleton values.
     */
    OmniValue* v =
        (strcmp(s, "nothing") == 0) ? omni_nothing :
        (strcmp(s, "nil") == 0) ? omni_nil :
        omni_new_sym(s);
    free(s);
    return v;
}

/*
 * act_colon_quoted_symbol
 *
 * Canonical syntax decision:
 *   :name is pure reader sugar for 'name, and normalizes to (quote name).
 *
 * Why we do this in the parser:
 *   - We explicitly do NOT want a separate keyword type in the surface language.
 *   - We want printing/normalization to prefer `'name` as the canonical representation.
 */
static OmniValue* act_colon_quoted_symbol(PikaState* state, size_t pos, PikaMatch match) {
    /* Pattern: ':' SYM */
    size_t current = pos;

    /* Skip ':' */
    PikaMatch* colon_m = pika_get_match(state, current, R_COLON);
    if (!colon_m || !colon_m->matched) return omni_nil;
    current += colon_m->len;

    /* Parse the symbol name */
    PikaMatch* sym_m = pika_get_match(state, current, R_SYM);
    if (!sym_m || !sym_m->matched || !sym_m->val) return omni_nil;

    OmniValue* sym = sym_m->val;
    if (!omni_is_sym(sym)) return omni_nil;

    /* Expand to: (quote <sym>) */
    return omni_new_cell(omni_new_sym("quote"), omni_new_cell(sym, omni_nil));
}

/*
 * act_meta_key
 *
 * Canonical syntax decision:
 *   ^:key is reserved metadata syntax and must be parseable even though '^' and ':'
 *   are not permitted in normal symbols.
 *
 * Representation:
 *   We represent the metadata marker as an OMNI_SYM whose string is "^:<key>".
 *   This allows later phases to recognize metadata markers by prefix without
 *   introducing a new AST node kind.
 */
static OmniValue* act_meta_key(PikaState* state, size_t pos, PikaMatch match) {
    /* Pattern: '^' ':' SYM */
    size_t current = pos;

    /* Skip '^' */
    PikaMatch* caret_m = pika_get_match(state, current, R_CARET);
    if (!caret_m || !caret_m->matched) return omni_nil;
    current += caret_m->len;

    /* Skip ':' */
    PikaMatch* colon_m = pika_get_match(state, current, R_COLON);
    if (!colon_m || !colon_m->matched) return omni_nil;
    current += colon_m->len;

    /* Read the key name */
    PikaMatch* key_m = pika_get_match(state, current, R_SYM);
    if (!key_m || !key_m->matched || !key_m->val) return omni_nil;
    OmniValue* key_sym = key_m->val;
    if (!omni_is_sym(key_sym) || !key_sym->str_val) return omni_nil;

    /* Build "^:<key>" string */
    char buf[256];
    snprintf(buf, sizeof(buf), "^:%s", key_sym->str_val);
    return omni_new_sym(buf);
}

static OmniValue* act_list(PikaState* state, size_t pos, PikaMatch match) {
    /* Get LIST_INNER content */
    size_t current = pos + 1;  /* Skip ( */

    /* Skip whitespace */
    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;

    /* Get inner content */
    PikaMatch* inner_m = pika_get_match(state, current, R_LIST_INNER);
    if (inner_m && inner_m->matched && inner_m->val) return inner_m->val;

    return omni_nil;
}

static OmniValue* act_list_inner(PikaState* state, size_t pos, PikaMatch match) {
    if (match.len == 0) return omni_nil;

    size_t current = pos;

    /* EXPR */
    PikaMatch* expr_m = pika_get_match(state, current, R_EXPR);
    if (!expr_m || !expr_m->matched) return omni_nil;
    OmniValue* head = expr_m->val;
    current += expr_m->len;

    /* WS */
    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;

    /* Recursive tail */
    PikaMatch* rest_m = pika_get_match(state, current, R_LIST_INNER);
    OmniValue* tail = (rest_m && rest_m->matched && rest_m->val) ? rest_m->val : omni_nil;

    return omni_new_cell(head, tail);
}

static OmniValue* act_array(PikaState* state, size_t pos, PikaMatch match) {
    /* Get ARRAY_INNER content, convert to array */
    size_t current = pos + 1;  /* Skip [ */

    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;

    PikaMatch* inner_m = pika_get_match(state, current, R_ARRAY_INNER);
    if (inner_m && inner_m->matched && inner_m->val) {
        /* Convert list to array */
        OmniValue* list = inner_m->val;
        size_t len = 0;
        for (OmniValue* p = list; !omni_is_nil(p) && omni_is_cell(p); p = omni_cdr(p)) len++;

        OmniValue* arr = omni_new_array(len);
        for (OmniValue* p = list; !omni_is_nil(p) && omni_is_cell(p); p = omni_cdr(p)) {
            omni_array_push(arr, omni_car(p));
        }
        return arr;
    }
    return omni_new_array(0);
}

static OmniValue* act_array_inner(PikaState* state, size_t pos, PikaMatch match) {
    /* Same as list_inner, just builds a list */
    return act_list_inner(state, pos, match);
}

static OmniValue* act_quoted(PikaState* state, size_t pos, PikaMatch match) {
    char quote_char = state->input[pos];
    size_t expr_pos = pos + 1;

    /* Handle ,@ (unquote-splicing) */
    if (quote_char == ',' && pos + 1 < state->input_len && state->input[pos + 1] == '@') {
        expr_pos = pos + 2;
        PikaMatch* ws_m = pika_get_match(state, expr_pos, R_WS);
        if (ws_m && ws_m->matched) expr_pos += ws_m->len;

        PikaMatch* expr_m = pika_get_match(state, expr_pos, R_EXPR);
        if (expr_m && expr_m->matched && expr_m->val) {
            return omni_new_cell(omni_new_sym("unquote-splicing"),
                                 omni_new_cell(expr_m->val, omni_nil));
        }
    }

    PikaMatch* ws_m = pika_get_match(state, expr_pos, R_WS);
    if (ws_m && ws_m->matched) expr_pos += ws_m->len;

    PikaMatch* expr_m = pika_get_match(state, expr_pos, R_EXPR);
    if (expr_m && expr_m->matched && expr_m->val) {
        const char* quote_sym;
        switch (quote_char) {
            case '\'': quote_sym = "quote"; break;
            case '`': quote_sym = "quasiquote"; break;
            case ',': quote_sym = "unquote"; break;
            default: quote_sym = "quote"; break;
        }
        return omni_new_cell(omni_new_sym(quote_sym),
                             omni_new_cell(expr_m->val, omni_nil));
    }

    return omni_nil;
}

static OmniValue* act_type(PikaState* state, size_t pos, PikaMatch match) {
    size_t current = pos + 1; /* skip { */
    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;
    
    PikaMatch* inner_m = pika_get_match(state, current, R_TYPE_INNER);
    if (inner_m && inner_m->matched && inner_m->val) {
        /* inner_m->val is a list of expressions inside { } */
        /* First expression is the type name, rest are parameters */
        OmniValue* list = inner_m->val;
        if (omni_is_cell(list)) {
            OmniValue* first = omni_car(list);
            if (omni_is_sym(first)) {
                const char* type_name = first->str_val;
                OmniValue* params_list = omni_cdr(list);
                size_t param_count = omni_list_len(params_list);
                OmniValue** params = NULL;
                if (param_count > 0) {
                    params = omni_list_to_array(params_list, &param_count);
                }
                return omni_new_type_lit(type_name, params, param_count);
            }
        }
        /* Fallback: use the whole list as a type name if not a standard list */
        char* str = omni_value_to_string(list);
        OmniValue* v = omni_new_type_lit(str, NULL, 0);
        free(str);
        return v;
    }
    return omni_new_type_lit("Any", NULL, 0);
}

static OmniValue* act_metadata(PikaState* state, size_t pos, PikaMatch match) {
    size_t current = pos + 1; /* skip ^ */
    PikaMatch* ws1_m = pika_get_match(state, current, R_WS);
    if (ws1_m && ws1_m->matched) current += ws1_m->len;
    
    PikaMatch* meta_m = pika_get_match(state, current, R_EXPR);
    if (!meta_m || !meta_m->matched) return omni_nil;
    OmniValue* meta = meta_m->val;
    current += meta_m->len;
    
    PikaMatch* ws2_m = pika_get_match(state, current, R_WS);
    if (ws2_m && ws2_m->matched) current += ws2_m->len;
    
    PikaMatch* obj_m = pika_get_match(state, current, R_EXPR);
    if (!obj_m || !obj_m->matched) return meta; 
    OmniValue* obj = obj_m->val;
    
    return omni_new_cell(omni_new_sym("with-meta"), 
                         omni_new_cell(meta, omni_new_cell(obj, omni_nil)));
}

static OmniValue* act_path(PikaState* state, size_t pos, PikaMatch match) {
    /*
     * PATH roots are restricted to normal atoms (ints or symbols), not metadata markers.
     * Keep this consistent with the grammar's PATH_ROOT rule.
     */
    PikaMatch* root_m = pika_get_match(state, pos, R_PATH_ROOT);
    if (!root_m || !root_m->matched) return omni_nil;
    OmniValue* root = root_m->val;
    
    OmniValue* segments = omni_nil;
    size_t current = pos + root_m->len;
    
    /* We need to build the list of segments in order. 
     * Since we don't have a good way to iterate children of a repeated rule 
     * easily in this Pika wrapper without specific rule actions, 
     * we'll manually scan forward using pika_get_match. */
    
    while (current < pos + match.len) {
        PikaMatch* dot_m = pika_get_match(state, current, R_DOT);
        if (!dot_m || !dot_m->matched) break;
        current += dot_m->len;
        
        PikaMatch* seg_m = pika_get_match(state, current, R_PATH_SEGMENT);
        if (!seg_m || !seg_m->matched) break;
        
        segments = omni_new_cell(seg_m->val, segments);
        current += seg_m->len;
    }
    
    /* Reverse segments because we built them backward */
    OmniValue* rev_segments = omni_nil;
    while (!omni_is_nil(segments)) {
        rev_segments = omni_new_cell(omni_car(segments), rev_segments);
        segments = omni_cdr(segments);
    }
    
    return omni_new_cell(omni_new_sym("path"),
                         omni_new_cell(root, rev_segments));
}

/* Phase 22: Match expression semantic action */
static OmniValue* act_match(PikaState* state, size_t pos, PikaMatch match) {
    /* (match value [pattern result] ...) */
    size_t current = pos;

    /* Skip "match" symbol */
    PikaMatch* sym_m = pika_get_match(state, current, R_SYM);
    if (!sym_m || !sym_m->matched) return omni_nil;
    current += sym_m->len;

    /* Skip whitespace */
    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;

    /* Get the value expression */
    PikaMatch* val_m = pika_get_match(state, current, R_EXPR);
    if (!val_m || !val_m->matched) return omni_nil;
    OmniValue* value_expr = val_m->val;
    current += val_m->len;

    /* Skip whitespace */
    ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;

    /* Get match clauses */
    PikaMatch* clauses_m = pika_get_match(state, current, R_MATCH_CLAUSES);
    OmniValue* clauses = (clauses_m && clauses_m->matched) ? clauses_m->val : omni_nil;

    /* Build match expression: (match value clause1 clause2 ...) */
    return omni_new_cell(omni_new_sym("match"),
                        omni_new_cell(value_expr,
                                     clauses));
}

/* Match clause semantic action */
static OmniValue* act_match_clause(PikaState* state, size_t pos, PikaMatch match) {
    /* [pattern result] or [pattern :when guard result] */
    /* For now, we treat the clause as an array */
    /* The analyzer will destructure it */
    return omni_new_array(0);  /* Placeholder */
}

/* Match clauses list action */
static OmniValue* act_match_clauses(PikaState* state, size_t pos, PikaMatch match) {
    /* Build a list of clauses */
    /* For now, return nil - analyzer will parse from list structure */
    return omni_nil;
}

static OmniValue* act_program(PikaState* state, size_t pos, PikaMatch match) {
    size_t current = pos;

    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;

    PikaMatch* inner_m = pika_get_match(state, current, R_PROGRAM_INNER);
    if (inner_m && inner_m->matched && inner_m->val) return inner_m->val;

    return omni_nil;
}

static OmniValue* act_program_inner(PikaState* state, size_t pos, PikaMatch match) {
    /*
     * PROGRAM_INNER has the same *shape* as LIST_INNER (a sequence of EXPRs
     * separated by whitespace), but it must recurse through PROGRAM_INNER, not
     * LIST_INNER.
     *
     * A historical bug here caused omni_parser_parse_all() to return 0
     * expressions for valid programs, which then surfaced as:
     *   "Error: No expressions to compile"
     *
     * We implement the same cell-building logic as act_list_inner(), but with
     * PROGRAM_INNER as the recursive tail rule.
     */
    if (match.len == 0) return omni_nil;

    size_t current = pos;

    /* EXPR */
    PikaMatch* expr_m = pika_get_match(state, current, R_EXPR);
    if (!expr_m || !expr_m->matched) return omni_nil;
    OmniValue* head = expr_m->val;
    current += expr_m->len;

    /* WS */
    PikaMatch* ws_m = pika_get_match(state, current, R_WS);
    if (ws_m && ws_m->matched) current += ws_m->len;

    /* Recursive tail */
    PikaMatch* rest_m = pika_get_match(state, current, R_PROGRAM_INNER);
    OmniValue* tail = (rest_m && rest_m->matched && rest_m->val) ? rest_m->val : omni_nil;

    return omni_new_cell(head, tail);
}

/* ============== String Parsing Helper ============== */

/**
 * Extract and process string content from a matched string literal.
 * Handles escape sequences like \n, \t, \", \\, \xNN, \uNNNN
 * Returns an AST symbol containing the processed string content.
 */
static OmniValue* extract_string_content(PikaState* state, size_t pos, size_t len) {
    if (len < 2) return omni_new_sym("");  /* Empty or invalid string */

    /* Skip opening and closing quotes */
    const char* start = state->input + pos + 1;
    size_t content_len = len - 2;

    /* Allocate buffer for processed string */
    char* result = malloc(content_len + 1);
    if (!result) return omni_new_sym("");

    size_t result_idx = 0;
    size_t i = 0;

    while (i < content_len) {
        if (start[i] == '\\' && i + 1 < content_len) {
            /* Handle escape sequences */
            switch (start[i + 1]) {
                case 'n': result[result_idx++] = '\n'; i += 2; break;
                case 't': result[result_idx++] = '\t'; i += 2; break;
                case '"': result[result_idx++] = '"';  i += 2; break;
                case '\\': result[result_idx++] = '\\'; i += 2; break;
                case 'x': /* Hex escape \xNN */
                    if (i + 3 < content_len && isxdigit(start[i + 2]) && isxdigit(start[i + 3])) {
                        char hex[3] = { start[i + 2], start[i + 3], '\0' };
                        result[result_idx++] = (char)strtol(hex, NULL, 16);
                        i += 4;
                    } else {
                        result[result_idx++] = start[i + 1]; i += 2; /* Invalid, just copy */
                    }
                    break;
                case 'u': /* Unicode escape \uNNNN - simplified: just copy the bytes */
                    if (i + 5 < content_len) {
                        /* For now, just skip the \u and copy 4 chars */
                        /* A full implementation would decode UTF-8 properly */
                        for (int j = 2; j <= 5 && (i + j) < content_len; j++) {
                            result[result_idx++] = start[i + j];
                        }
                        i += 6;
                    } else {
                        result[result_idx++] = start[i + 1]; i += 2;
                    }
                    break;
                default:
                    result[result_idx++] = start[i + 1]; i += 2; /* Unknown escape, copy char */
                    break;
            }
        } else {
            result[result_idx++] = start[i++];
        }
    }

    result[result_idx] = '\0';

    /* Create a symbol to represent the string content */
    /* Note: In a full implementation, we'd have a dedicated string type */
    OmniValue* sym = omni_new_string(result);
    free(result);
    return sym;
}

/* ============== New Semantic Actions ============== */

/* String literal action: "..." -> symbol with string content */
static OmniValue* act_string(PikaState* state, size_t pos, PikaMatch match) {
    return extract_string_content(state, pos, match.len);
}

/* Format string action: #fmt"..." -> (fmt-string "content") */
static OmniValue* act_fmt_string(PikaState* state, size_t pos, PikaMatch match) {
    /* Pattern matched: HASH + SYM + DQUOTE + STRING_INNER + DQUOTE */
    /* We need to:
     * 1. Verify the SYM is "fmt"
     * 2. Extract the string content from STRING_INNER
     */

    size_t current = pos;

    /* Skip HASH */
    PikaMatch* hash_m = pika_get_match(state, current, R_HASH);
    if (!hash_m || !hash_m->matched) return omni_nil;
    current += hash_m->len;

    /* Check the symbol is "fmt" */
    PikaMatch* sym_m = pika_get_match(state, current, R_SYM);
    if (!sym_m || !sym_m->matched) return omni_nil;

    /* Get symbol value to verify it's "fmt" */
    OmniValue* sym_val = sym_m->val;
    if (!omni_is_sym(sym_val)) return omni_nil;

    /* Check if symbol is "fmt" */
    const char* sym_name = sym_val->str_val;
    if (strcmp(sym_name, "fmt") != 0) {
        /* Not a format string, return nil to indicate no match */
        return omni_nil;
    }

    current += sym_m->len;

    /* Skip opening DQUOTE */
    PikaMatch* quote_m = pika_get_match(state, current, R_DQUOTE);
    if (!quote_m || !quote_m->matched) return omni_nil;
    current += quote_m->len;

    /* Get STRING_INNER match */
    PikaMatch* inner_m = pika_get_match(state, current, R_STRING_INNER);
    if (!inner_m || !inner_m->matched) {
        /* Empty string content */
        return omni_new_cell(omni_new_sym("fmt-string"),
                             omni_new_cell(omni_new_sym(""), omni_nil));
    }
    current += inner_m->len;

    /* Extract string content (the STRING_INNER match includes the content) */
    OmniValue* content = extract_string_content(state, current - inner_m->len - 1, inner_m->len + 2);

    return omni_new_cell(omni_new_sym("fmt-string"),
                         omni_new_cell(content, omni_nil));
}

/* CLF format string action: #clf"..." -> (clf-string "content") */
static OmniValue* act_clf_string(PikaState* state, size_t pos, PikaMatch match) {
    /* Similar to #fmt but verifies symbol is "clf" */
    size_t current = pos;

    /* Skip HASH */
    PikaMatch* hash_m = pika_get_match(state, current, R_HASH);
    if (!hash_m || !hash_m->matched) return omni_nil;
    current += hash_m->len;

    /* Check the symbol is "clf" */
    PikaMatch* sym_m = pika_get_match(state, current, R_SYM);
    if (!sym_m || !sym_m->matched) return omni_nil;

    /* Get symbol value to verify it's "clf" */
    OmniValue* sym_val = sym_m->val;
    if (!omni_is_sym(sym_val)) return omni_nil;

    /* Check if symbol is "clf" */
    const char* sym_name = sym_val->str_val;
    if (strcmp(sym_name, "clf") != 0) {
        /* Not a clf format string, return nil */
        return omni_nil;
    }

    current += sym_m->len;

    /* Skip opening DQUOTE */
    PikaMatch* quote_m = pika_get_match(state, current, R_DQUOTE);
    if (!quote_m || !quote_m->matched) return omni_nil;
    current += quote_m->len;

    /* Get STRING_INNER match */
    PikaMatch* inner_m = pika_get_match(state, current, R_STRING_INNER);
    if (!inner_m || !inner_m->matched) {
        /* Empty string content */
        return omni_new_cell(omni_new_sym("clf-string"),
                             omni_new_cell(omni_new_sym(""), omni_nil));
    }

    /* Extract string content */
    size_t content_start = current;
    size_t content_len = inner_m->len;
    OmniValue* content = extract_string_content(state, content_start - 1, content_len + 2);

    return omni_new_cell(omni_new_sym("clf-string"),
                         omni_new_cell(content, omni_nil));
}

/* Named character literal: #\newline, #\space, #\tab, etc. */
static OmniValue* act_named_char(PikaState* state, size_t pos, PikaMatch match) {
    /* Pattern matched: HASH + BACKSLASH + SYM (where SYM is the character name) */
    size_t current = pos;

    /* Skip HASH */
    PikaMatch* hash_m = pika_get_match(state, current, R_HASH);
    if (!hash_m || !hash_m->matched) return omni_nil;
    current += hash_m->len;

    /* Skip BACKSLASH */
    PikaMatch* backslash_m = pika_get_match(state, current, R_BACKSLASH);
    if (!backslash_m || !backslash_m->matched) return omni_nil;
    current += backslash_m->len;

    /* Get the character name symbol */
    PikaMatch* sym_m = pika_get_match(state, current, R_SYM);
    if (!sym_m || !sym_m->matched) return omni_nil;

    OmniValue* sym_val = sym_m->val;
    if (!omni_is_sym(sym_val)) return omni_nil;

    const char* name = sym_val->str_val;

    /* Map named characters to their character codes */
    long char_code = -1;

    /* Check for hex syntax: #\xNN (e.g., #\x41 = 65 = 'A') */
    if (strlen(name) >= 2 && name[0] == 'x' && isxdigit(name[1]) && isxdigit(name[2])) {
        /* Parse hex value: xNN -> NN in hex */
        char hex_str[3] = { name[1], name[2], '\0' };
        char_code = strtol(hex_str, NULL, 16);
        /* Ensure it's a valid byte (0-255) */
        if (char_code < 0 || char_code > 255) {
            char_code = -1;  /* Invalid */
        }
    } else if (strcmp(name, "newline") == 0) {
        char_code = 10;  /* '\n' */
    } else if (strcmp(name, "space") == 0) {
        char_code = 32;  /* ' ' */
    } else if (strcmp(name, "tab") == 0) {
        char_code = 9;   /* '\t' */
    } else if (strcmp(name, "return") == 0) {
        char_code = 13;  /* '\r' */
    } else if (strcmp(name, "nul") == 0) {
        char_code = 0;   /* '\0' */
    } else if (strcmp(name, "bell") == 0) {
        char_code = 7;   /* '\a' */
    } else if (strcmp(name, "backspace") == 0) {
        char_code = 8;   /* '\b' */
    } else if (strcmp(name, "escape") == 0) {
        char_code = 27;  /* '\e' (ESC) */
    } else if (strcmp(name, "delete") == 0) {
        char_code = 127; /* DEL */
    }

    /* If we found a valid named character, return as integer */
    if (char_code >= 0) {
        return omni_new_int(char_code);
    }

    /* Otherwise, return an error or nil for unknown character names */
    return omni_nil;
}

/*
 * act_hash_val: Process #val <value> reader tag
 *
 * Pattern: HASH + SYM("val") + <atom>
 * Expands to: (value->type <atom>)
 *
 * Example:
 *   #val 42  ->  (value->type 42)
 *   #val "hello"  ->  (value->type "hello")
 */
static OmniValue* act_hash_val(PikaState* state, size_t pos, PikaMatch match) {
    /* Pattern matched: HASH + SYM + ATOM */
    size_t current = pos;

    /* Skip HASH */
    PikaMatch* hash_m = pika_get_match(state, current, R_HASH);
    if (!hash_m || !hash_m->matched) return omni_nil;
    current += hash_m->len;

    /* Get the symbol after HASH - should be "val" */
    PikaMatch* sym_m = pika_get_match(state, current, R_SYM);
    if (!sym_m || !sym_m->matched) return omni_nil;
    current += sym_m->len;

    /* Get the atom value */
    PikaMatch* atom_m = pika_get_match(state, current, R_ATOM);
    if (!atom_m || !atom_m->matched) return omni_nil;

    OmniValue* value = atom_m->val;
    if (!value) return omni_nil;

    /* Build the list: (value->type <value>) */
    OmniValue* value_to_type_sym = omni_new_sym("value->type");

    /* Create cons cells: (value->type value . nil) */
    OmniValue* result = omni_new_cell(value_to_type_sym, omni_new_cell(value, NULL));

    return result;
}

/* ============== Grammar Initialization ============== */

void omni_grammar_init(void) {
    if (g_grammar_initialized) return;

    /* Epsilon */
    g_rules[R_EPSILON] = (PikaRule){ PIKA_TERMINAL, .data.str = "" };
    g_rules[R_ANY] = (PikaRule){ PIKA_ANY };

    /* Individual characters */
    g_rules[R_CHAR_SPACE] = (PikaRule){ PIKA_TERMINAL, .data.str = " " };
    g_rules[R_CHAR_TAB] = (PikaRule){ PIKA_TERMINAL, .data.str = "\t" };
    g_rules[R_CHAR_NL] = (PikaRule){ PIKA_TERMINAL, .data.str = "\n" };
    g_rules[R_CHAR_CR] = (PikaRule){ PIKA_TERMINAL, .data.str = "\r" };
    g_rules[R_SEMICOLON] = (PikaRule){ PIKA_TERMINAL, .data.str = ";" };

    /* Negative lookahead for newline */
    g_rule_ids[R_NOT_NL] = ids(1, R_CHAR_NL);
    g_rules[R_NOT_NL] = (PikaRule){ PIKA_NOT, .data.children = { g_rule_ids[R_NOT_NL], 1 } };

    /* Optional newline */
    g_rule_ids[R_OPT_NL] = ids(1, R_CHAR_NL);
    g_rules[R_OPT_NL] = (PikaRule){ PIKA_OPT, .data.children = { g_rule_ids[R_OPT_NL], 1 } };

    /* Comment char: any char that is NOT a newline */
    g_rule_ids[R_COMMENT_CHAR] = ids(2, R_NOT_NL, R_ANY);
    g_rules[R_COMMENT_CHAR] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_COMMENT_CHAR], 2 } };

    /* Comment body: sequence of comment chars */
    g_rule_ids[R_COMMENT_BODY] = ids(1, R_COMMENT_CHAR);
    g_rules[R_COMMENT_BODY] = (PikaRule){ PIKA_REP, .data.children = { g_rule_ids[R_COMMENT_BODY], 1 } };

    /* Single whitespace */
    g_rule_ids[R_SPACE] = ids(4, R_CHAR_SPACE, R_CHAR_TAB, R_CHAR_NL, R_CHAR_CR);
    g_rules[R_SPACE] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_SPACE], 4 } };

    /* Comments: ; to end of line or EOF */
    g_rule_ids[R_COMMENT] = ids(3, R_SEMICOLON, R_COMMENT_BODY, R_OPT_NL);
    g_rules[R_COMMENT] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_COMMENT], 3 } };

    /* Whitespace item: space or comment */
    g_rule_ids[R_WS_ITEM] = ids(2, R_SPACE, R_COMMENT);
    g_rules[R_WS_ITEM] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_WS_ITEM], 2 } };

    /* Whitespace sequence */
    g_rule_ids[R_WS] = ids(1, R_WS_ITEM);
    g_rules[R_WS] = (PikaRule){ PIKA_REP, .data.children = { g_rule_ids[R_WS], 1 } };

    /* Whitespace sequence (one-or-more) */
    g_rule_ids[R_WS1] = ids(1, R_WS_ITEM);
    g_rules[R_WS1] = (PikaRule){ PIKA_POS, .data.children = { g_rule_ids[R_WS1], 1 } };

    /* Digits */
    g_rules[R_DIGIT] = (PikaRule){ PIKA_RANGE, .data.range = { '0', '9' } };
    g_rules[R_DIGIT1] = (PikaRule){ PIKA_RANGE, .data.range = { '1', '9' } };

    /* Integer */
    g_rule_ids[R_INT] = ids(1, R_DIGIT);
    g_rules[R_INT] = (PikaRule){ PIKA_POS, .data.children = { g_rule_ids[R_INT], 1 }, .action = act_int };

    /* Signed integer */
    g_rules[R_SIGNED_INT] = g_rules[R_INT];

    /* Float literal: <INT> "." <INT> (e.g., 1.5, 3.14, 0.5)
     * IMPORTANT: This must come BEFORE R_PATH in EXPR alternatives
     * so that "1.5" is parsed as a float, not as a path expression "1 . 5"
     */
    g_rule_ids[R_FLOAT] = ids(3, R_INT, R_DOT, R_INT);
    g_rules[R_FLOAT] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_FLOAT], 3 }, .action = act_float };

    /* Alphabetic */
    g_rules[R_ALPHA] = (PikaRule){ PIKA_RANGE, .data.range = { 'a', 'z' } };
    g_rules[R_ALPHA_UPPER] = (PikaRule){ PIKA_RANGE, .data.range = { 'A', 'Z' } };

    /* Single-character operators for symbols
     * Based on T-syntax-symbol-rules specification:
     * START WITH: a-z A-Z * + ! - _ ? % / = < >
     * MIDDLE: All of above + digits (0-9)
     * EXCLUDE: . @ # & : ; (reserved for syntax)
     */
    g_rules[R_EXCLAM] = (PikaRule){ PIKA_TERMINAL, .data.str = "!" };       /* Bang suffix */
    g_rules[R_LT] = (PikaRule){ PIKA_TERMINAL, .data.str = "<" };           /* Less than */
    g_rules[R_GT] = (PikaRule){ PIKA_TERMINAL, .data.str = ">" };           /* Greater than */
    g_rules[R_EQ] = (PikaRule){ PIKA_TERMINAL, .data.str = "=" };           /* Equals */
    g_rules[R_QUESTION] = (PikaRule){ PIKA_TERMINAL, .data.str = "?" };     /* Question mark */
    g_rules[R_AT] = (PikaRule){ PIKA_TERMINAL, .data.str = "@" };           /* At sign (metadata) - NOT in symbols */

    /* Symbol operators: * + ! - _ ? % / = < >
     * Each defined separately for clarity
     */
    g_rules[R_SYM_ASTERISK] = (PikaRule){ PIKA_TERMINAL, .data.str = "*" };    /* Multiplication */
    g_rules[R_SYM_PLUS] = (PikaRule){ PIKA_TERMINAL, .data.str = "+" };        /* Addition */
    g_rules[R_SYM_MINUS] = (PikaRule){ PIKA_TERMINAL, .data.str = "-" };       /* Minus/separator */
    g_rules[R_SYM_UNDERSCORE] = (PikaRule){ PIKA_TERMINAL, .data.str = "_" };  /* Underscore */
    g_rules[R_SYM_PERCENT] = (PikaRule){ PIKA_TERMINAL, .data.str = "%" };     /* Percent */
    g_rules[R_SYM_SLASH] = (PikaRule){ PIKA_TERMINAL, .data.str = "/" };       /* Slash */

    /* R_SYM_FIRST: first char of symbol (CANNOT start with digit, ., @, #, &, :, ;) */
    /* Includes: a-z A-Z * ! - _ ? % / = < > */
    g_rule_ids[R_SYM_FIRST] = ids(13,
        R_ALPHA, R_ALPHA_UPPER,
        R_SYM_ASTERISK, R_SYM_PLUS, R_EXCLAM, R_SYM_MINUS, R_SYM_UNDERSCORE,
        R_QUESTION, R_SYM_PERCENT, R_SYM_SLASH,
        R_LT, R_GT, R_EQ);
    g_rules[R_SYM_FIRST] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_SYM_FIRST], 13 } };

    /* R_SYM_CHAR: all symbol characters (for middle positions) */
    /* Includes: R_SYM_FIRST + digits (0-9) */
    /* EXCLUDES: . @ # & : ; (not in symbol definitions above) */
    g_rule_ids[R_SYM_CHAR] = ids(14,
        R_ALPHA, R_ALPHA_UPPER,
        R_SYM_ASTERISK, R_SYM_PLUS, R_EXCLAM, R_SYM_MINUS, R_SYM_UNDERSCORE,
        R_QUESTION, R_SYM_PERCENT, R_SYM_SLASH,
        R_LT, R_GT, R_EQ, R_DIGIT);
    g_rules[R_SYM_CHAR] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_SYM_CHAR], 14 } };

    /* Symbol: first char then rest */
    g_rule_ids[R_SYM] = ids(1, R_SYM_CHAR);
    g_rules[R_SYM] = (PikaRule){ PIKA_POS, .data.children = { g_rule_ids[R_SYM], 1 }, .action = act_sym };

    /* Brackets */
    g_rules[R_LPAREN] = (PikaRule){ PIKA_TERMINAL, .data.str = "(" };
    g_rules[R_RPAREN] = (PikaRule){ PIKA_TERMINAL, .data.str = ")" };
    g_rules[R_LBRACKET] = (PikaRule){ PIKA_TERMINAL, .data.str = "[" };
    g_rules[R_RBRACKET] = (PikaRule){ PIKA_TERMINAL, .data.str = "]" };
    g_rules[R_LBRACE] = (PikaRule){ PIKA_TERMINAL, .data.str = "{" };
    g_rules[R_RBRACE] = (PikaRule){ PIKA_TERMINAL, .data.str = "}" };

    /* Quote characters */
    g_rules[R_QUOTE_CHAR] = (PikaRule){ PIKA_TERMINAL, .data.str = "'" };
    g_rules[R_DQUOTE] = (PikaRule){ PIKA_TERMINAL, .data.str = "\"" };
    g_rules[R_QUASIQUOTE_CHAR] = (PikaRule){ PIKA_TERMINAL, .data.str = "`" };
    g_rules[R_UNQUOTE_CHAR] = (PikaRule){ PIKA_TERMINAL, .data.str = "," };
    g_rules[R_DOT] = (PikaRule){ PIKA_TERMINAL, .data.str = "." };
    g_rules[R_CARET] = (PikaRule){ PIKA_TERMINAL, .data.str = "^" };

    /* New syntax characters */
    g_rules[R_COLON] = (PikaRule){ PIKA_TERMINAL, .data.str = ":" };
    g_rules[R_EXCLAM] = (PikaRule){ PIKA_TERMINAL, .data.str = "!" };
    g_rules[R_HASH] = (PikaRule){ PIKA_TERMINAL, .data.str = "#" };
    g_rules[R_BACKSLASH] = (PikaRule){ PIKA_TERMINAL, .data.str = "\\" };

    /* Format strings: #fmt"..." and #clf"..." */
    g_rule_ids[R_HASH_FMT] = ids(2, R_HASH, R_SYM);
    g_rules[R_HASH_FMT] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_HASH_FMT], 2 } };

    g_rule_ids[R_HASH_CLF] = ids(2, R_HASH, R_SYM);
    g_rules[R_HASH_CLF] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_HASH_CLF], 2 } };

    /* Reserved metadata keys: ^:parent, ^:where, ... */
    int* meta_key_ids = ids(3, R_CARET, R_COLON, R_SYM);
    g_rule_ids[R_META_KEY] = meta_key_ids;
    g_rules[R_META_KEY] = (PikaRule){ PIKA_SEQ, .data.children = { meta_key_ids, 3 }, .action = act_meta_key };

    /*
     * :name sugar:
     *   :foo  =>  (quote foo)
     */
    int* colon_quote_ids = ids(2, R_COLON, R_SYM);
    g_rule_ids[R_COLON_QUOTED_SYMBOL] = colon_quote_ids;
    g_rules[R_COLON_QUOTED_SYMBOL] = (PikaRule){ PIKA_SEQ, .data.children = { colon_quote_ids, 2 }, .action = act_colon_quoted_symbol };

    /*
     * ATOM:
     *   We include R_META_KEY so metadata markers can appear as standalone items
     *   in special forms (define/let), without weakening normal symbol rules.
     */
    g_rule_ids[R_ATOM] = ids(3, R_INT, R_META_KEY, R_SYM);
    g_rules[R_ATOM] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_ATOM], 3 } };

    /*
     * PATH_ROOT:
     *   Paths should be rooted in normal atoms (ints or symbols), not metadata keys.
     *   This prevents nonsense parses like ^:parent.foo becoming a path expression.
     */
    g_rule_ids[R_PATH_ROOT] = ids(2, R_INT, R_SYM);
    g_rules[R_PATH_ROOT] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_PATH_ROOT], 2 } };

    /* LIST_SEQ = EXPR WS LIST_INNER */
    g_rule_ids[R_LIST_SEQ] = ids(3, R_EXPR, R_WS, R_LIST_INNER);
    g_rules[R_LIST_SEQ] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_LIST_SEQ], 3 } };

    /* LIST_INNER = LIST_SEQ / EPSILON */
    g_rule_ids[R_LIST_INNER] = ids(2, R_LIST_SEQ, R_EPSILON);
    g_rules[R_LIST_INNER] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_LIST_INNER], 2 }, .action = act_list_inner };

    /* LIST = ( WS LIST_INNER ) */
    g_rule_ids[R_LIST] = ids(4, R_LPAREN, R_WS, R_LIST_INNER, R_RPAREN);
    g_rules[R_LIST] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_LIST], 4 }, .action = act_list };

    /* ARRAY_SEQ = EXPR WS ARRAY_INNER */
    g_rule_ids[R_ARRAY_SEQ] = ids(3, R_EXPR, R_WS, R_ARRAY_INNER);
    g_rules[R_ARRAY_SEQ] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_ARRAY_SEQ], 3 } };

    /* ARRAY_INNER = ARRAY_SEQ / EPSILON */
    g_rule_ids[R_ARRAY_INNER] = ids(2, R_ARRAY_SEQ, R_EPSILON);
    g_rules[R_ARRAY_INNER] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_ARRAY_INNER], 2 }, .action = act_array_inner };

    /* ARRAY = [ WS ARRAY_INNER ] */
    g_rule_ids[R_ARRAY] = ids(4, R_LBRACKET, R_WS, R_ARRAY_INNER, R_RBRACKET);
    g_rules[R_ARRAY] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_ARRAY], 4 }, .action = act_array };

    /* TYPE_INNER = LIST_INNER */
    g_rules[R_TYPE_INNER] = g_rules[R_LIST_INNER];

    /* TYPE = { WS TYPE_INNER } */
    g_rule_ids[R_TYPE] = ids(4, R_LBRACE, R_WS, R_TYPE_INNER, R_RBRACE);
    g_rules[R_TYPE] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_TYPE], 4 }, .action = act_type };

    /* PATH_SEGMENT = SYM / ARRAY */
    g_rule_ids[R_PATH_SEGMENT] = ids(2, R_SYM, R_ARRAY);
    g_rules[R_PATH_SEGMENT] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_PATH_SEGMENT], 2 } };

    /* PATH_TAIL_ITEM = DOT PATH_SEGMENT */
    g_rule_ids[R_PATH_TAIL_ITEM] = ids(2, R_DOT, R_PATH_SEGMENT);
    g_rules[R_PATH_TAIL_ITEM] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_PATH_TAIL_ITEM], 2 } };

    /* PATH_TAIL = PATH_TAIL_ITEM+ */
    g_rule_ids[R_PATH_TAIL] = ids(1, R_PATH_TAIL_ITEM);
    g_rules[R_PATH_TAIL] = (PikaRule){ PIKA_POS, .data.children = { g_rule_ids[R_PATH_TAIL], 1 } };

    /* PATH = PATH_ROOT PATH_TAIL */
    g_rule_ids[R_PATH] = ids(2, R_PATH_ROOT, R_PATH_TAIL);
    g_rules[R_PATH] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_PATH], 2 }, .action = act_path };

    /*
     * METADATA = ^ WS1 EXPR WS1 EXPR
     *
     * This is the *general* metadata attachment form.
     *
     * We REQUIRE whitespace separators so that reserved metadata key tokens
     * like `^:parent` can exist without being swallowed as `^ <expr> <expr>`.
     */
    g_rule_ids[R_METADATA] = ids(5, R_CARET, R_WS1, R_EXPR, R_WS1, R_EXPR);
    g_rules[R_METADATA] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_METADATA], 5 }, .action = act_metadata };

    /* QUOTED = 'EXPR | `EXPR | ,EXPR */
    g_rule_ids[R_QUOTED] = ids(2, R_QUOTE_CHAR, R_EXPR);
    g_rules[R_QUOTED] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_QUOTED], 2 }, .action = act_quoted };

    /* ============== String Literals ============== */

    /* Terminal for double quote character (for string literals) */
    /* R_QUOTE_CHAR is for single quote (quoted expressions like 'x) */
    /* R_DQUOTE is for double quote (string literals like "text") */

    /* Negative lookahead for double quote */
    g_rule_ids[R_NOT_DQUOTE] = ids(1, R_DQUOTE);
    g_rules[R_NOT_DQUOTE] = (PikaRule){ PIKA_NOT, .data.children = { g_rule_ids[R_NOT_DQUOTE], 1 } };

    /* STRING_CHAR: matches any character except DQUOTE */
    g_rule_ids[R_STRING_CHAR] = ids(2, R_NOT_DQUOTE, R_ANY);
    g_rules[R_STRING_CHAR] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_STRING_CHAR], 2 } };

    /* STRING_INNER = STRING_CHAR* (zero or more) */
    int* string_inner_ids = ids(1, R_STRING_CHAR);
    g_rule_ids[R_STRING_INNER] = string_inner_ids;
    g_rules[R_STRING_INNER] = (PikaRule){ PIKA_REP, .data.children = { string_inner_ids, 1 } };

    /* STRING = DQUOTE STRING_INNER DQUOTE with semantic action for escape processing */
    int* string_ids = ids(3, R_DQUOTE, R_STRING_INNER, R_DQUOTE);
    g_rule_ids[R_STRING] = string_ids;
    g_rules[R_STRING] = (PikaRule){ PIKA_SEQ, .data.children = { string_ids, 3 }, .action = act_string };

    /* ============== Format Strings ============== */

    /* Format strings: #fmt"..." and #clf"..." */
    /* Pattern: HASH + SYM + DQUOTE + STRING_INNER + DQUOTE */
    /* The semantic action will verify the SYM is "fmt" or "clf" */

    int* fmt_string_ids = ids(5, R_HASH, R_SYM, R_DQUOTE, R_STRING_INNER, R_DQUOTE);
    g_rule_ids[R_FMT_STRING] = fmt_string_ids;
    g_rules[R_FMT_STRING] = (PikaRule){ PIKA_SEQ, .data.children = { fmt_string_ids, 5 }, .action = act_fmt_string };

    int* clf_string_ids = ids(5, R_HASH, R_SYM, R_DQUOTE, R_STRING_INNER, R_DQUOTE);
    g_rule_ids[R_CLF_STRING] = clf_string_ids;
    g_rules[R_CLF_STRING] = (PikaRule){ PIKA_SEQ, .data.children = { clf_string_ids, 5 }, .action = act_clf_string };

    /* ============== Named Character Literals ============== */

    /* Named characters: #\newline, #\space, #\tab, etc. */
    /* Pattern: HASH + BACKSLASH + SYM (where SYM is the character name) */
    int* named_char_ids = ids(3, R_HASH, R_BACKSLASH, R_SYM);
    g_rule_ids[R_NAMED_CHAR] = named_char_ids;
    g_rules[R_NAMED_CHAR] = (PikaRule){ PIKA_SEQ, .data.children = { named_char_ids, 3 }, .action = act_named_char };

    /* ============== Value-to-Type Reader Tag ============== */

    /* Value-to-type conversion: #val <value> */
    /* Pattern: HASH + SYM("val") + ATOM */
    /* Expands to: (value->type <value>) */
    int* hash_val_ids = ids(3, R_HASH, R_SYM, R_ATOM);
    g_rule_ids[R_HASH_VAL] = hash_val_ids;
    g_rules[R_HASH_VAL] = (PikaRule){ PIKA_SEQ, .data.children = { hash_val_ids, 3 }, .action = act_hash_val };

    /* ============== Expression ============== */

    /* EXPR = FLOAT / PATH / LIST / ARRAY / TYPE / METADATA / QUOTED / :SYMBOL / STRING / FMT_STRING / CLF_STRING / NAMED_CHAR / HASH_VAL / ATOM
     * IMPORTANT: FLOAT must come BEFORE PATH so that "1.5" is parsed as a float literal,
     * not as a path expression "1 . 5" (integer 1, dot, integer 5)
     */
    g_rule_ids[R_EXPR] = ids(14, R_FLOAT, R_PATH, R_LIST, R_ARRAY, R_TYPE, R_METADATA, R_QUOTED, R_COLON_QUOTED_SYMBOL, R_STRING, R_FMT_STRING, R_CLF_STRING, R_NAMED_CHAR, R_HASH_VAL, R_ATOM);
    g_rules[R_EXPR] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_EXPR], 14 } };

    /* PROGRAM_SEQ = EXPR WS PROGRAM_INNER */
    g_rule_ids[R_PROGRAM_SEQ] = ids(3, R_EXPR, R_WS, R_PROGRAM_INNER);
    g_rules[R_PROGRAM_SEQ] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_PROGRAM_SEQ], 3 } };

    /* PROGRAM_INNER = PROGRAM_SEQ / EPSILON */
    g_rule_ids[R_PROGRAM_INNER] = ids(2, R_PROGRAM_SEQ, R_EPSILON);
    g_rules[R_PROGRAM_INNER] = (PikaRule){ PIKA_ALT, .data.children = { g_rule_ids[R_PROGRAM_INNER], 2 }, .action = act_program_inner };

    /* PROGRAM = WS PROGRAM_INNER */
    g_rule_ids[R_PROGRAM] = ids(2, R_WS, R_PROGRAM_INNER);
    g_rules[R_PROGRAM] = (PikaRule){ PIKA_SEQ, .data.children = { g_rule_ids[R_PROGRAM], 2 }, .action = act_program };

    g_grammar_initialized = true;
}

void omni_grammar_cleanup(void) {
    if (!g_grammar_initialized) return;

    /* Free rule ID arrays */
    for (int i = 0; i < NUM_RULES; i++) {
        if (g_rule_ids[i]) {
            free(g_rule_ids[i]);
            g_rule_ids[i] = NULL;
        }
    }

    g_grammar_initialized = false;
}

/* ============== Parser API ============== */

OmniParser* omni_parser_new(const char* input) {
    return omni_parser_new_n(input, strlen(input));
}

OmniParser* omni_parser_new_n(const char* input, size_t len) {
    OmniParser* p = malloc(sizeof(OmniParser));
    if (!p) return NULL;
    p->input = input;
    p->input_len = len;
    p->pos = 0;
    p->errors = NULL;
    p->error_count = 0;
    return p;
}

void omni_parser_free(OmniParser* parser) {
    if (!parser) return;

    /* Free error chain */
    OmniParseError* err = parser->errors;
    while (err) {
        OmniParseError* next = err->next;
        free(err);
        err = next;
    }

    free(parser);
}

OmniValue* omni_parse_string(const char* source) {
    omni_grammar_init();

    PikaState* state = pika_new(source, g_rules, NUM_RULES);
    if (!state) return omni_new_error("Failed to create parser state");

    OmniValue* result = pika_run(state, R_EXPR);

#ifdef DEBUG
    fprintf(stderr, "[DEBUG] parse_string input='%s'\n", source);
    PikaMatch* m = pika_get_match(state, 0, R_EXPR);
    if (m) {
        fprintf(stderr, "[DEBUG] R_EXPR at 0: matched=%d len=%zu\n", m->matched, m->len);
    }
    m = pika_get_match(state, 0, R_LIST);
    if (m) {
        fprintf(stderr, "[DEBUG] R_LIST at 0: matched=%d len=%zu\n", m->matched, m->len);
    }
    m = pika_get_match(state, 0, R_ATOM);
    if (m) {
        fprintf(stderr, "[DEBUG] R_ATOM at 0: matched=%d len=%zu\n", m->matched, m->len);
    }
    m = pika_get_match(state, 0, R_INT);
    if (m) {
        fprintf(stderr, "[DEBUG] R_INT at 0: matched=%d len=%zu\n", m->matched, m->len);
    }
    m = pika_get_match(state, 0, R_LPAREN);
    if (m) {
        fprintf(stderr, "[DEBUG] R_LPAREN at 0: matched=%d len=%zu\n", m->matched, m->len);
    }
#endif

    pika_free(state);

    return result;
}

OmniValue** omni_parser_parse_all(OmniParser* parser, size_t* count) {
    omni_grammar_init();

    PikaState* state = pika_new(parser->input, g_rules, NUM_RULES);
    if (!state) {
        *count = 0;
        return NULL;
    }

    OmniValue* program = pika_run(state, R_PROGRAM);

#ifdef DEBUG
    fprintf(stderr, "[DEBUG] parse_all input='%.50s'\n", parser->input);
    PikaMatch* m = pika_get_match(state, 0, R_PROGRAM);
    if (m) fprintf(stderr, "[DEBUG] R_PROGRAM at 0: matched=%d len=%zu\n", m->matched, m->len);
    m = pika_get_match(state, 0, R_EXPR);
    if (m) fprintf(stderr, "[DEBUG] R_EXPR at 0: matched=%d len=%zu\n", m->matched, m->len);
    m = pika_get_match(state, 0, R_LIST);
    if (m) fprintf(stderr, "[DEBUG] R_LIST at 0: matched=%d len=%zu\n", m->matched, m->len);
    m = pika_get_match(state, 0, R_LPAREN);
    if (m) fprintf(stderr, "[DEBUG] R_LPAREN at 0: matched=%d len=%zu\n", m->matched, m->len);
    m = pika_get_match(state, 0, R_LIST_INNER);
    if (m) fprintf(stderr, "[DEBUG] R_LIST_INNER at 0: matched=%d len=%zu\n", m->matched, m->len);
    m = pika_get_match(state, 1, R_SYM);
    if (m) fprintf(stderr, "[DEBUG] R_SYM at 1: matched=%d len=%zu\n", m->matched, m->len);
#endif

    pika_free(state);

    if (omni_is_error(program) || omni_is_nil(program)) {
        *count = 0;
        return NULL;
    }

    /* Count expressions */
    size_t n = 0;
    for (OmniValue* p = program; !omni_is_nil(p) && omni_is_cell(p); p = omni_cdr(p)) n++;

    /* Build array */
    OmniValue** exprs = malloc(sizeof(OmniValue*) * (n + 1));
    size_t i = 0;
    for (OmniValue* p = program; !omni_is_nil(p) && omni_is_cell(p); p = omni_cdr(p)) {
        exprs[i++] = omni_car(p);
    }
    exprs[n] = NULL;

    *count = n;
    return exprs;
}

OmniParseError* omni_parser_get_errors(OmniParser* parser) {
    return parser ? parser->errors : NULL;
}
