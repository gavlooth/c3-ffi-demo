/*
 * regex_compile.c - Pika-based Regex Engine Implementation
 *
 * Compiles regex patterns to PikaClause trees and uses the Pika
 * parsing infrastructure for matching.
 *
 * Pipeline:
 *   1. Tokenizer: regex string -> token stream
 *   2. Parser: token stream -> PikaClause tree (via pika_clause_* API)
 *   3. Matcher: Uses Pika's pika_grammar_parse() for matching
 *
 * Supported Features:
 *   - Literals: abc
 *   - Character classes: [abc], [a-z], [^abc]
 *   - Any character: .
 *   - Quantifiers: *, +, ?, {n}, {n,}, {n,m}
 *   - Possessive quantifiers: *+, ++, ?+ (no backtracking - PEG native)
 *   - Alternation: a|b
 *   - Grouping: (ab)+
 *   - Non-capturing groups: (?:...)
 *   - Lookahead: (?=...) positive, (?!...) negative
 *   - Anchors: ^ (start), $ (end), \b (word boundary)
 *   - Escapes: \d, \D, \w, \W, \s, \S, \n, \t, \r, \\
 */

#include "regex_compile.h"
#include "internal_types.h"
#include "../../csrc/parser/pika_c/pika.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ============== Compiled Regex Structure ============== */

struct PikaRegexCompiled {
    PikaClause* pattern;       /* Root clause of the pattern */
    PikaGrammar* grammar;      /* Grammar wrapping the pattern */
    bool anchored_start;       /* Pattern starts with ^ */
    bool anchored_end;         /* Pattern ends with $ */
    bool has_word_boundary;    /* Pattern contains \b */
    char* error;               /* Error message if compilation failed */
};

/* ============== Token Types ============== */

typedef enum {
    TOK_LITERAL,     /* Single character literal */
    TOK_CHAR_CLASS,  /* Character class [abc] or [a-z] */
    TOK_DOT,         /* Any character */
    TOK_STAR,        /* Zero or more */
    TOK_PLUS,        /* One or more */
    TOK_QUESTION,    /* Optional */
    TOK_STAR_POSS,   /* Possessive zero or more *+ */
    TOK_PLUS_POSS,   /* Possessive one or more ++ */
    TOK_QUEST_POSS,  /* Possessive optional ?+ */
    TOK_PIPE,        /* Alternation */
    TOK_LPAREN,      /* Left paren ( */
    TOK_RPAREN,      /* Right paren ) */
    TOK_NONCAP,      /* Non-capturing group (?: */
    TOK_LOOKAHEAD_POS, /* Positive lookahead (?= */
    TOK_LOOKAHEAD_NEG, /* Negative lookahead (?! */
    TOK_LBRACE,      /* Left brace { for quantifiers */
    TOK_ANCHOR_START,/* ^ anchor */
    TOK_ANCHOR_END,  /* $ anchor */
    TOK_WORD_BOUND,  /* \b word boundary */
    TOK_END          /* End of pattern */
} TokenType;

/* Character class representation for tokenizer */
typedef struct {
    char ranges[64][2];  /* Start-end pairs for ranges */
    int num_ranges;
    char chars[64];      /* Individual characters */
    int num_chars;
    bool negated;        /* [^...] negation */
} CharClassData;

typedef struct {
    TokenType type;
    union {
        char ch;              /* For TOK_LITERAL */
        CharClassData* cclass; /* For TOK_CHAR_CLASS */
        struct {              /* For TOK_LBRACE (bounded quantifier) */
            int min;
            int max;          /* -1 for unbounded {n,} */
            bool possessive;
        } bounds;
    } data;
} Token;

/* ============== Tokenizer ============== */

typedef struct {
    const char* pattern;
    int pos;
    int len;
    char* error;
} Tokenizer;

static Tokenizer* tokenizer_new(const char* pattern) {
    Tokenizer* t = malloc(sizeof(Tokenizer));
    t->pattern = pattern;
    t->pos = 0;
    t->len = strlen(pattern);
    t->error = NULL;
    return t;
}

static void tokenizer_free(Tokenizer* t) {
    if (t->error) free(t->error);
    free(t);
}

static void tokenizer_error(Tokenizer* t, const char* msg) {
    if (t->error) free(t->error);
    t->error = malloc(256);
    snprintf(t->error, 256, "Regex error at position %d: %s", t->pos, msg);
}

static char tokenizer_peek(Tokenizer* t) {
    if (t->pos >= t->len) return '\0';
    return t->pattern[t->pos];
}

static char tokenizer_advance(Tokenizer* t) {
    if (t->pos >= t->len) return '\0';
    return t->pattern[t->pos++];
}

static CharClassData* parse_char_class_data(Tokenizer* t) {
    CharClassData* cc = malloc(sizeof(CharClassData));
    cc->num_ranges = 0;
    cc->num_chars = 0;
    cc->negated = false;

    /* Skip opening [ */
    tokenizer_advance(t);

    /* Check for negation */
    if (tokenizer_peek(t) == '^') {
        cc->negated = true;
        tokenizer_advance(t);
    }

    while (tokenizer_peek(t) != ']' && tokenizer_peek(t) != '\0') {
        char c = tokenizer_advance(t);

        /* Handle escape in char class */
        if (c == '\\') {
            c = tokenizer_advance(t);
            switch (c) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'd': /* \d = 0-9 */
                    if (cc->num_ranges < 64) {
                        cc->ranges[cc->num_ranges][0] = '0';
                        cc->ranges[cc->num_ranges][1] = '9';
                        cc->num_ranges++;
                    }
                    continue;
                case 'w': /* \w = a-zA-Z0-9_ */
                    if (cc->num_ranges < 61) {
                        cc->ranges[cc->num_ranges][0] = 'a';
                        cc->ranges[cc->num_ranges][1] = 'z';
                        cc->num_ranges++;
                        cc->ranges[cc->num_ranges][0] = 'A';
                        cc->ranges[cc->num_ranges][1] = 'Z';
                        cc->num_ranges++;
                        cc->ranges[cc->num_ranges][0] = '0';
                        cc->ranges[cc->num_ranges][1] = '9';
                        cc->num_ranges++;
                    }
                    if (cc->num_chars < 64) cc->chars[cc->num_chars++] = '_';
                    continue;
                case 's': /* \s = space, tab, newline, return */
                    if (cc->num_chars < 60) {
                        cc->chars[cc->num_chars++] = ' ';
                        cc->chars[cc->num_chars++] = '\t';
                        cc->chars[cc->num_chars++] = '\n';
                        cc->chars[cc->num_chars++] = '\r';
                    }
                    continue;
                default:
                    /* Literal escaped char */
                    break;
            }
        }

        /* Check for range */
        if (tokenizer_peek(t) == '-' && t->pos + 1 < t->len && t->pattern[t->pos + 1] != ']') {
            tokenizer_advance(t); /* Skip - */
            char end = tokenizer_advance(t);
            if (end == '\\') {
                end = tokenizer_advance(t);
                switch (end) {
                    case 'n': end = '\n'; break;
                    case 't': end = '\t'; break;
                    case 'r': end = '\r'; break;
                    default: break;
                }
            }
            if (cc->num_ranges < 64) {
                cc->ranges[cc->num_ranges][0] = c;
                cc->ranges[cc->num_ranges][1] = end;
                cc->num_ranges++;
            }
        } else {
            if (cc->num_chars < 64) {
                cc->chars[cc->num_chars++] = c;
            }
        }
    }

    if (tokenizer_peek(t) == ']') {
        tokenizer_advance(t);
    } else {
        tokenizer_error(t, "Unclosed character class");
    }

    return cc;
}

/* Parse bounded quantifier {n}, {n,}, {n,m} */
static Token parse_bounded_quantifier(Tokenizer* t) {
    Token tok;
    tok.type = TOK_LBRACE;
    tok.data.bounds.min = 0;
    tok.data.bounds.max = 0;
    tok.data.bounds.possessive = false;

    tokenizer_advance(t); /* Skip { */

    /* Parse min */
    int min = 0;
    while (tokenizer_peek(t) >= '0' && tokenizer_peek(t) <= '9') {
        min = min * 10 + (tokenizer_advance(t) - '0');
    }
    tok.data.bounds.min = min;

    if (tokenizer_peek(t) == '}') {
        /* {n} - exact count */
        tokenizer_advance(t);
        tok.data.bounds.max = min;
    } else if (tokenizer_peek(t) == ',') {
        tokenizer_advance(t); /* Skip , */
        if (tokenizer_peek(t) == '}') {
            /* {n,} - at least n */
            tokenizer_advance(t);
            tok.data.bounds.max = -1; /* Unbounded */
        } else {
            /* {n,m} - between n and m */
            int max = 0;
            while (tokenizer_peek(t) >= '0' && tokenizer_peek(t) <= '9') {
                max = max * 10 + (tokenizer_advance(t) - '0');
            }
            tok.data.bounds.max = max;
            if (tokenizer_peek(t) == '}') {
                tokenizer_advance(t);
            } else {
                tokenizer_error(t, "Expected } in bounded quantifier");
            }
        }
    } else {
        tokenizer_error(t, "Invalid bounded quantifier syntax");
        tok.type = TOK_LITERAL;
        tok.data.ch = '{';
        return tok;
    }

    /* Check for possessive + */
    if (tokenizer_peek(t) == '+') {
        tokenizer_advance(t);
        tok.data.bounds.possessive = true;
    }

    return tok;
}

static Token next_token(Tokenizer* t) {
    Token tok;

    if (t->pos >= t->len) {
        tok.type = TOK_END;
        return tok;
    }

    char c = tokenizer_peek(t);

    switch (c) {
        case '[':
            tok.type = TOK_CHAR_CLASS;
            tok.data.cclass = parse_char_class_data(t);
            return tok;

        case '.':
            tokenizer_advance(t);
            tok.type = TOK_DOT;
            return tok;

        case '*':
            tokenizer_advance(t);
            /* Check for possessive *+ */
            if (tokenizer_peek(t) == '+') {
                tokenizer_advance(t);
                tok.type = TOK_STAR_POSS;
            } else {
                tok.type = TOK_STAR;
            }
            return tok;

        case '+':
            tokenizer_advance(t);
            /* Check for possessive ++ */
            if (tokenizer_peek(t) == '+') {
                tokenizer_advance(t);
                tok.type = TOK_PLUS_POSS;
            } else {
                tok.type = TOK_PLUS;
            }
            return tok;

        case '?':
            tokenizer_advance(t);
            /* Check for possessive ?+ */
            if (tokenizer_peek(t) == '+') {
                tokenizer_advance(t);
                tok.type = TOK_QUEST_POSS;
            } else {
                tok.type = TOK_QUESTION;
            }
            return tok;

        case '|':
            tokenizer_advance(t);
            tok.type = TOK_PIPE;
            return tok;

        case '(':
            tokenizer_advance(t);
            /* Check for special groups (?:, (?=, (?! */
            if (tokenizer_peek(t) == '?') {
                tokenizer_advance(t);
                char next = tokenizer_peek(t);
                if (next == ':') {
                    tokenizer_advance(t);
                    tok.type = TOK_NONCAP;
                } else if (next == '=') {
                    tokenizer_advance(t);
                    tok.type = TOK_LOOKAHEAD_POS;
                } else if (next == '!') {
                    tokenizer_advance(t);
                    tok.type = TOK_LOOKAHEAD_NEG;
                } else {
                    /* Just (? - treat as error or literal */
                    tokenizer_error(t, "Unknown group type (?");
                    tok.type = TOK_LPAREN;
                }
            } else {
                tok.type = TOK_LPAREN;
            }
            return tok;

        case ')':
            tokenizer_advance(t);
            tok.type = TOK_RPAREN;
            return tok;

        case '^':
            tokenizer_advance(t);
            tok.type = TOK_ANCHOR_START;
            return tok;

        case '$':
            tokenizer_advance(t);
            tok.type = TOK_ANCHOR_END;
            return tok;

        case '{':
            return parse_bounded_quantifier(t);

        case '\\':
            tokenizer_advance(t);
            c = tokenizer_advance(t);
            switch (c) {
                case 'n': tok.type = TOK_LITERAL; tok.data.ch = '\n'; break;
                case 't': tok.type = TOK_LITERAL; tok.data.ch = '\t'; break;
                case 'r': tok.type = TOK_LITERAL; tok.data.ch = '\r'; break;
                case 'b': tok.type = TOK_WORD_BOUND; break;
                case 'd': {
                    /* \d = [0-9] */
                    tok.type = TOK_CHAR_CLASS;
                    tok.data.cclass = malloc(sizeof(CharClassData));
                    tok.data.cclass->num_ranges = 1;
                    tok.data.cclass->ranges[0][0] = '0';
                    tok.data.cclass->ranges[0][1] = '9';
                    tok.data.cclass->num_chars = 0;
                    tok.data.cclass->negated = false;
                    break;
                }
                case 'D': {
                    /* \D = [^0-9] (negated digits) */
                    tok.type = TOK_CHAR_CLASS;
                    tok.data.cclass = malloc(sizeof(CharClassData));
                    tok.data.cclass->num_ranges = 1;
                    tok.data.cclass->ranges[0][0] = '0';
                    tok.data.cclass->ranges[0][1] = '9';
                    tok.data.cclass->num_chars = 0;
                    tok.data.cclass->negated = true;
                    break;
                }
                case 'w': {
                    /* \w = [a-zA-Z0-9_] */
                    tok.type = TOK_CHAR_CLASS;
                    tok.data.cclass = malloc(sizeof(CharClassData));
                    tok.data.cclass->num_ranges = 3;
                    tok.data.cclass->ranges[0][0] = 'a';
                    tok.data.cclass->ranges[0][1] = 'z';
                    tok.data.cclass->ranges[1][0] = 'A';
                    tok.data.cclass->ranges[1][1] = 'Z';
                    tok.data.cclass->ranges[2][0] = '0';
                    tok.data.cclass->ranges[2][1] = '9';
                    tok.data.cclass->num_chars = 1;
                    tok.data.cclass->chars[0] = '_';
                    tok.data.cclass->negated = false;
                    break;
                }
                case 'W': {
                    /* \W = [^a-zA-Z0-9_] (negated word) */
                    tok.type = TOK_CHAR_CLASS;
                    tok.data.cclass = malloc(sizeof(CharClassData));
                    tok.data.cclass->num_ranges = 3;
                    tok.data.cclass->ranges[0][0] = 'a';
                    tok.data.cclass->ranges[0][1] = 'z';
                    tok.data.cclass->ranges[1][0] = 'A';
                    tok.data.cclass->ranges[1][1] = 'Z';
                    tok.data.cclass->ranges[2][0] = '0';
                    tok.data.cclass->ranges[2][1] = '9';
                    tok.data.cclass->num_chars = 1;
                    tok.data.cclass->chars[0] = '_';
                    tok.data.cclass->negated = true;
                    break;
                }
                case 's': {
                    /* \s = [ \t\n\r] */
                    tok.type = TOK_CHAR_CLASS;
                    tok.data.cclass = malloc(sizeof(CharClassData));
                    tok.data.cclass->num_ranges = 0;
                    tok.data.cclass->num_chars = 4;
                    tok.data.cclass->chars[0] = ' ';
                    tok.data.cclass->chars[1] = '\t';
                    tok.data.cclass->chars[2] = '\n';
                    tok.data.cclass->chars[3] = '\r';
                    tok.data.cclass->negated = false;
                    break;
                }
                case 'S': {
                    /* \S = [^ \t\n\r] (negated space) */
                    tok.type = TOK_CHAR_CLASS;
                    tok.data.cclass = malloc(sizeof(CharClassData));
                    tok.data.cclass->num_ranges = 0;
                    tok.data.cclass->num_chars = 4;
                    tok.data.cclass->chars[0] = ' ';
                    tok.data.cclass->chars[1] = '\t';
                    tok.data.cclass->chars[2] = '\n';
                    tok.data.cclass->chars[3] = '\r';
                    tok.data.cclass->negated = true;
                    break;
                }
                default:
                    /* Literal escaped character */
                    tok.type = TOK_LITERAL;
                    tok.data.ch = c;
                    break;
            }
            return tok;

        default:
            tokenizer_advance(t);
            tok.type = TOK_LITERAL;
            tok.data.ch = c;
            return tok;
    }
}

/* ============== Parser (builds PikaClause trees) ============== */

typedef struct {
    Tokenizer* tokenizer;
    Token current;
    char* error;
    bool saw_anchor_start;
    bool saw_anchor_end;
    bool saw_word_boundary;
} Parser;

static Parser* parser_new(const char* pattern) {
    Parser* p = malloc(sizeof(Parser));
    p->tokenizer = tokenizer_new(pattern);
    p->current = next_token(p->tokenizer);
    p->error = NULL;
    p->saw_anchor_start = false;
    p->saw_anchor_end = false;
    p->saw_word_boundary = false;
    return p;
}

static void parser_free(Parser* p) {
    tokenizer_free(p->tokenizer);
    if (p->error) free(p->error);
    free(p);
}

static void parser_advance(Parser* p) {
    p->current = next_token(p->tokenizer);
}

/* Convert CharClassData to PikaClause */
static PikaClause* charclass_to_clause(CharClassData* cc) {
    if (!cc) return NULL;

    /* Build charset clauses for ranges and chars, then union them */
    PikaClause* charsets[128];
    int num_charsets = 0;

    /* Add ranges */
    for (int i = 0; i < cc->num_ranges && num_charsets < 128; i++) {
        PikaClause* range_clause = pika_clause_charset_from_range(
            cc->ranges[i][0], cc->ranges[i][1]);
        if (range_clause) {
            charsets[num_charsets++] = range_clause;
        }
    }

    /* Add individual chars */
    if (cc->num_chars > 0 && num_charsets < 128) {
        PikaClause* chars_clause = pika_clause_charset_from_chars(
            cc->chars, cc->num_chars);
        if (chars_clause) {
            charsets[num_charsets++] = chars_clause;
        }
    }

    if (num_charsets == 0) {
        /* Empty character class - create a charset that matches nothing */
        free(cc);
        return pika_clause_charset_from_chars("", 0);
    }

    PikaClause* result;
    if (num_charsets == 1) {
        result = charsets[0];
    } else {
        /* Union all charsets (consumes inputs) */
        result = pika_clause_charset_union_take(charsets, num_charsets);
    }

    /* Apply negation if needed */
    if (cc->negated && result) {
        result = pika_clause_charset_invert(result);
    }

    free(cc);
    return result;
}

/* Create "any char" clause (matches any single character) */
static PikaClause* create_any_char_clause(void) {
    /* Create charset for all printable ASCII and common control chars */
    /* Use range 0x01-0xFF (all non-null bytes) */
    return pika_clause_charset_from_range(0x01, 0x7F);
}

/* Forward declarations for recursive descent parser */
static PikaClause* parse_alternation(Parser* p);
static PikaClause* parse_sequence(Parser* p);
static PikaClause* parse_quantified(Parser* p);
static PikaClause* parse_atom(Parser* p);

static PikaClause* parse_atom(Parser* p) {
    Token tok = p->current;

    switch (tok.type) {
        case TOK_LITERAL: {
            char s[2] = { tok.data.ch, '\0' };
            PikaClause* clause = pika_clause_str(s);
            parser_advance(p);
            return clause;
        }

        case TOK_CHAR_CLASS: {
            PikaClause* clause = charclass_to_clause(tok.data.cclass);
            parser_advance(p);
            return clause;
        }

        case TOK_DOT: {
            PikaClause* clause = create_any_char_clause();
            parser_advance(p);
            return clause;
        }

        case TOK_ANCHOR_START: {
            /* ^ anchor - record it and return pika_clause_start() */
            p->saw_anchor_start = true;
            PikaClause* clause = pika_clause_start();
            parser_advance(p);
            return clause;
        }

        case TOK_ANCHOR_END: {
            /* $ anchor - record it, match end via negative lookahead of any char */
            p->saw_anchor_end = true;
            parser_advance(p);
            /* $ = not followed by any char */
            return pika_clause_not_followed_by(create_any_char_clause());
        }

        case TOK_WORD_BOUND: {
            /* \b word boundary - complex to express in PEG */
            /* For now, just mark it and return nothing (zero-width) */
            p->saw_word_boundary = true;
            parser_advance(p);
            /* Word boundary is hard in PEG - return nothing for now */
            return pika_clause_nothing();
        }

        case TOK_LPAREN: {
            parser_advance(p); /* Skip ( */
            PikaClause* inner = parse_alternation(p);
            if (p->current.type != TOK_RPAREN) {
                p->error = strdup("Expected closing parenthesis");
                return NULL;
            }
            parser_advance(p); /* Skip ) */
            return inner;
        }

        case TOK_NONCAP: {
            /* Non-capturing group (?:...) - same as regular group */
            parser_advance(p); /* Skip (?: */
            PikaClause* inner = parse_alternation(p);
            if (p->current.type != TOK_RPAREN) {
                p->error = strdup("Expected closing parenthesis for non-capturing group");
                return NULL;
            }
            parser_advance(p); /* Skip ) */
            return inner;
        }

        case TOK_LOOKAHEAD_POS: {
            parser_advance(p); /* Skip (?= */
            PikaClause* inner = parse_alternation(p);
            if (p->current.type != TOK_RPAREN) {
                p->error = strdup("Expected closing parenthesis for lookahead");
                return NULL;
            }
            parser_advance(p); /* Skip ) */
            return pika_clause_followed_by(inner);
        }

        case TOK_LOOKAHEAD_NEG: {
            parser_advance(p); /* Skip (?! */
            PikaClause* inner = parse_alternation(p);
            if (p->current.type != TOK_RPAREN) {
                p->error = strdup("Expected closing parenthesis for negative lookahead");
                return NULL;
            }
            parser_advance(p); /* Skip ) */
            return pika_clause_not_followed_by(inner);
        }

        default:
            return NULL;
    }
}

/* Create bounded repetition: a{min,max} */
static PikaClause* create_bounded_repeat(PikaClause* clause, int min, int max) {
    if (!clause) return NULL;
    if (max < 0) {
        /* Unbounded: a{n,} = a...a (n times) followed by a* */
        if (min == 0) {
            return pika_clause_zero_or_more(clause);
        } else if (min == 1) {
            return pika_clause_one_or_more(clause);
        } else {
            /* a{n,} = a a ... a a* (n required, then zero or more) */
            PikaClause** parts = malloc((min + 1) * sizeof(PikaClause*));
            for (int i = 0; i < min; i++) {
                parts[i] = clause;
            }
            parts[min] = pika_clause_zero_or_more(clause);
            PikaClause* result = pika_clause_seq(parts, min + 1);
            free(parts);
            return result;
        }
    } else if (max == 0 && min == 0) {
        /* a{0,0} = nothing */
        return pika_clause_nothing();
    } else {
        /* Bounded: a{min,max} = a...a (min times) a?...a? (max-min times) */
        int total = max;
        if (total == 0) return pika_clause_nothing();

        PikaClause** parts = malloc(total * sizeof(PikaClause*));
        for (int i = 0; i < min; i++) {
            parts[i] = clause;
        }
        for (int i = min; i < max; i++) {
            parts[i] = pika_clause_optional(clause);
        }

        PikaClause* result;
        if (total == 1) {
            result = parts[0];
        } else {
            result = pika_clause_seq(parts, total);
        }
        free(parts);
        return result;
    }
}

static PikaClause* parse_quantified(Parser* p) {
    PikaClause* atom = parse_atom(p);
    if (!atom) return NULL;

    /* Check for quantifier */
    Token tok = p->current;

    /* Standard quantifiers (PEG is naturally possessive/greedy) */
    if (tok.type == TOK_STAR || tok.type == TOK_STAR_POSS) {
        parser_advance(p);
        return pika_clause_zero_or_more(atom);
    }

    if (tok.type == TOK_PLUS || tok.type == TOK_PLUS_POSS) {
        parser_advance(p);
        return pika_clause_one_or_more(atom);
    }

    if (tok.type == TOK_QUESTION || tok.type == TOK_QUEST_POSS) {
        parser_advance(p);
        return pika_clause_optional(atom);
    }

    /* Bounded quantifier {n}, {n,}, {n,m} */
    if (tok.type == TOK_LBRACE) {
        int min = tok.data.bounds.min;
        int max = tok.data.bounds.max;
        parser_advance(p);
        return create_bounded_repeat(atom, min, max);
    }

    return atom;
}

static PikaClause* parse_sequence(Parser* p) {
    PikaClause* children[128];
    int num_children = 0;

    while (p->current.type != TOK_END &&
           p->current.type != TOK_PIPE &&
           p->current.type != TOK_RPAREN) {
        PikaClause* child = parse_quantified(p);
        if (!child) break;
        if (num_children < 128) {
            children[num_children++] = child;
        }
    }

    if (num_children == 0) {
        return pika_clause_nothing();
    }
    if (num_children == 1) {
        return children[0];
    }

    return pika_clause_seq(children, num_children);
}

static PikaClause* parse_alternation(Parser* p) {
    PikaClause* left = parse_sequence(p);

    if (p->current.type != TOK_PIPE) {
        return left;
    }

    /* Collect all alternatives */
    PikaClause* alternatives[64];
    int num_alts = 0;
    alternatives[num_alts++] = left;

    while (p->current.type == TOK_PIPE) {
        parser_advance(p); /* Skip | */
        PikaClause* alt = parse_sequence(p);
        if (alt && num_alts < 64) {
            alternatives[num_alts++] = alt;
        }
    }

    if (num_alts == 1) {
        return alternatives[0];
    }

    return pika_clause_first(alternatives, num_alts);
}

/* ============== Public API ============== */

PikaRegexCompiled* pika_regex_compile(const char* pattern) {
    PikaRegexCompiled* compiled = malloc(sizeof(PikaRegexCompiled));
    compiled->pattern = NULL;
    compiled->grammar = NULL;
    compiled->anchored_start = false;
    compiled->anchored_end = false;
    compiled->has_word_boundary = false;
    compiled->error = NULL;

    if (!pattern) {
        compiled->error = strdup("Pattern is NULL");
        return compiled;
    }

    /* Parse pattern to PikaClause tree */
    Parser* p = parser_new(pattern);
    PikaClause* clause = parse_alternation(p);

    if (p->error) {
        compiled->error = strdup(p->error);
        parser_free(p);
        return compiled;
    }

    if (p->tokenizer->error) {
        compiled->error = strdup(p->tokenizer->error);
        parser_free(p);
        return compiled;
    }

    if (!clause) {
        compiled->error = strdup("Failed to parse pattern");
        parser_free(p);
        return compiled;
    }

    compiled->pattern = clause;
    compiled->anchored_start = p->saw_anchor_start;
    compiled->anchored_end = p->saw_anchor_end;
    compiled->has_word_boundary = p->saw_word_boundary;

    /* Create a grammar with a single rule */
    PikaRule* rule = pika_rule("regex", clause);
    if (!rule) {
        compiled->error = strdup("Failed to create rule");
        parser_free(p);
        return compiled;
    }

    PikaRule* rules[1] = { rule };
    compiled->grammar = pika_grammar_new(rules, 1);

    if (!compiled->grammar) {
        compiled->error = strdup("Failed to create grammar");
    }

    parser_free(p);
    return compiled;
}

const char* pika_regex_error(PikaRegexCompiled* compiled) {
    if (!compiled) return "NULL compiled regex";
    return compiled->error;
}

void pika_regex_free(PikaRegexCompiled* compiled) {
    if (!compiled) return;
    if (compiled->grammar) {
        pika_grammar_free(compiled->grammar);
    }
    if (compiled->error) free(compiled->error);
    free(compiled);
}

/* Helper: Create string Obj from C string */
static Obj* cstr_to_obj(const char* s) {
    if (!s) return NULL;
    return mk_string(s);
}

/* Helper: Check if position is at word boundary */
static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_';
}

static bool is_word_boundary(const char* input, int pos, int len) {
    bool prev_is_word = (pos > 0) && is_word_char(input[pos - 1]);
    bool curr_is_word = (pos < len) && is_word_char(input[pos]);
    return prev_is_word != curr_is_word;
}

Obj* pika_regex_search(PikaRegexCompiled* compiled, const char* input) {
    if (!compiled || compiled->error || !compiled->pattern || !input) {
        return NULL;
    }

    int len = strlen(input);

    /* Parse the entire input to build memoization table */
    PikaMemoTable* memo = pika_grammar_parse(compiled->grammar, input);
    if (!memo) return NULL;

    /* Get all matches for the "regex" rule */
    size_t match_count = 0;
    PikaMatch** matches = pika_memo_get_all_matches_for_rule(memo, "regex", &match_count);

    if (!matches || match_count == 0) {
        free(matches);
        pika_memo_free(memo);
        return NULL;
    }

    /* Find first valid match */
    PikaMatch* best_match = NULL;
    int best_start = len + 1;

    for (size_t i = 0; i < match_count; i++) {
        PikaMatch* m = matches[i];
        int start = pika_match_start(m);
        int mlen = pika_match_len(m);

        /* Skip zero-length matches for search */
        if (mlen == 0) continue;

        /* Check anchored_start constraint */
        if (compiled->anchored_start && start != 0) continue;

        /* Check word boundary if needed */
        if (compiled->has_word_boundary && !is_word_boundary(input, start, len)) continue;

        /* Find earliest match */
        if (start < best_start) {
            best_start = start;
            best_match = m;
        }
    }

    Obj* result = NULL;
    if (best_match) {
        int start = pika_match_start(best_match);
        int mlen = pika_match_len(best_match);
        char* matched_str = malloc(mlen + 1);
        strncpy(matched_str, input + start, mlen);
        matched_str[mlen] = '\0';
        result = cstr_to_obj(matched_str);
        free(matched_str);
    }

    free(matches);
    pika_memo_free(memo);
    return result;
}

Obj* pika_regex_find_all(PikaRegexCompiled* compiled, const char* input) {
    if (!compiled || compiled->error || !compiled->pattern || !input) {
        return NULL;
    }

    /* Parse to build memoization table */
    PikaMemoTable* memo = pika_grammar_parse(compiled->grammar, input);
    if (!memo) return NULL;

    /* Get non-overlapping matches for the "regex" rule */
    size_t match_count = 0;
    PikaMatch** matches = pika_memo_get_non_overlapping_matches_for_rule(memo, "regex", &match_count);

    if (!matches || match_count == 0) {
        free(matches);
        pika_memo_free(memo);
        return NULL;
    }

    /* Build result list */
    Obj* result_list = NULL;
    Obj* list_tail = NULL;

    for (size_t i = 0; i < match_count; i++) {
        PikaMatch* m = matches[i];
        int start = pika_match_start(m);
        int mlen = pika_match_len(m);

        /* Skip zero-length matches */
        if (mlen == 0) continue;

        char* matched_str = malloc(mlen + 1);
        strncpy(matched_str, input + start, mlen);
        matched_str[mlen] = '\0';

        Obj* matched_obj = cstr_to_obj(matched_str);
        Obj* new_pair = mk_pair(matched_obj, NULL);

        if (!result_list) {
            result_list = new_pair;
        } else {
            list_tail->b = new_pair;
        }
        list_tail = new_pair;

        free(matched_str);
    }

    free(matches);
    pika_memo_free(memo);
    return result_list;
}

Obj* pika_regex_split(PikaRegexCompiled* compiled, const char* input) {
    if (!compiled || compiled->error || !compiled->pattern || !input) {
        return NULL;
    }

    int len = strlen(input);

    /* Parse to build memoization table */
    PikaMemoTable* memo = pika_grammar_parse(compiled->grammar, input);
    if (!memo) return NULL;

    /* Get non-overlapping matches for the "regex" rule */
    size_t match_count = 0;
    PikaMatch** matches = pika_memo_get_non_overlapping_matches_for_rule(memo, "regex", &match_count);

    Obj* result_list = NULL;
    Obj* list_tail = NULL;
    int last_end = 0;

    if (matches) {
        for (size_t i = 0; i < match_count; i++) {
            PikaMatch* m = matches[i];
            int start = pika_match_start(m);
            int mlen = pika_match_len(m);

            /* Skip zero-length matches */
            if (mlen == 0) continue;

            /* Add segment before match */
            if (start > last_end) {
                int seg_len = start - last_end;
                char* segment = malloc(seg_len + 1);
                strncpy(segment, input + last_end, seg_len);
                segment[seg_len] = '\0';

                Obj* segment_obj = cstr_to_obj(segment);
                Obj* new_pair = mk_pair(segment_obj, NULL);

                if (!result_list) {
                    result_list = new_pair;
                } else {
                    list_tail->b = new_pair;
                }
                list_tail = new_pair;
                free(segment);
            }

            last_end = start + mlen;
        }
        free(matches);
    }

    /* Add remaining segment */
    if (last_end < len) {
        char* segment = strdup(input + last_end);
        Obj* segment_obj = cstr_to_obj(segment);
        Obj* new_pair = mk_pair(segment_obj, NULL);

        if (!result_list) {
            result_list = new_pair;
        } else {
            list_tail->b = new_pair;
        }
        free(segment);
    }

    pika_memo_free(memo);
    return result_list;
}

// REVIEWED:NAIVE
Obj* pika_regex_replace(PikaRegexCompiled* compiled, const char* replacement,
                        const char* input, bool global) {
    if (!compiled || compiled->error || !compiled->pattern || !input) {
        return cstr_to_obj(input);
    }
    if (!replacement) replacement = "";

    int len = strlen(input);
    size_t repl_len = strlen(replacement);

    /* Parse to build memoization table */
    PikaMemoTable* memo = pika_grammar_parse(compiled->grammar, input);
    if (!memo) return cstr_to_obj(input);

    /* Get non-overlapping matches */
    size_t match_count = 0;
    PikaMatch** matches = pika_memo_get_non_overlapping_matches_for_rule(memo, "regex", &match_count);

    if (!matches || match_count == 0) {
        free(matches);
        pika_memo_free(memo);
        return cstr_to_obj(input);
    }

    /* Build result string */
    size_t result_size = len * 2 + 1;
    char* result = malloc(result_size);
    result[0] = '\0';
    size_t result_len = 0;
    int last_end = 0;
    bool first_done = false;

    for (size_t i = 0; i < match_count; i++) {
        if (!global && first_done) break;

        PikaMatch* m = matches[i];
        int start = pika_match_start(m);
        int mlen = pika_match_len(m);

        /* Skip zero-length matches */
        if (mlen == 0) continue;

        /* Add segment before match */
        int seg_len = start - last_end;
        if (seg_len > 0) {
            if (result_len + seg_len >= result_size) {
                result_size = result_size * 2 + seg_len;
                result = realloc(result, result_size);
            }
            strncat(result, input + last_end, seg_len);
            result_len += seg_len;
        }

        /* Add replacement */
        if (result_len + repl_len >= result_size) {
            result_size = result_size * 2 + repl_len;
            result = realloc(result, result_size);
        }
        strcat(result, replacement);
        result_len += repl_len;

        last_end = start + mlen;
        first_done = true;
    }

    /* Add remaining part */
    if (last_end < len) {
        int seg_len = len - last_end;
        if (result_len + seg_len >= result_size) {
            result_size = result_size * 2 + seg_len;
            result = realloc(result, result_size);
        }
        strcat(result, input + last_end);
    }

    free(matches);
    pika_memo_free(memo);

    Obj* ret = cstr_to_obj(result);
    free(result);
    return ret;
}

Obj* pika_regex_fullmatch(PikaRegexCompiled* compiled, const char* input) {
    if (!compiled || compiled->error || !compiled->pattern || !input) {
        return mk_bool(0);
    }

    int len = strlen(input);

    /* Parse to build memoization table */
    PikaMemoTable* memo = pika_grammar_parse(compiled->grammar, input);
    if (!memo) return mk_bool(0);

    /* Check if there's a match at position 0 with length == input length */
    size_t match_count = 0;
    PikaMatch** matches = pika_memo_get_all_matches_for_rule(memo, "regex", &match_count);

    bool found_fullmatch = false;
    if (matches) {
        for (size_t i = 0; i < match_count; i++) {
            PikaMatch* m = matches[i];
            int start = pika_match_start(m);
            int mlen = pika_match_len(m);

            if (start == 0 && mlen == len) {
                found_fullmatch = true;
                break;
            }
        }
        free(matches);
    }

    pika_memo_free(memo);
    return mk_bool(found_fullmatch);
}
