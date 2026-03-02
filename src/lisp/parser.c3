module lisp;

import std::io;
import std::collections::list;

// =============================================================================
// SECTION 1: LEXER
// =============================================================================

enum TokenType : char {
    T_EOF,
    T_LPAREN,
    T_RPAREN,
    T_LBRACKET,     // [ for array literals
    T_RBRACKET,     // ] for array literals
    T_DOT_BRACKET,  // .[ for index access
    T_DOTDOT,       // .. for rest patterns in destructuring
    T_QUOTE,
    T_INT,
    T_FLOAT,        // Floating-point literal
    T_STRING,       // "..." string literal
    T_SYMBOL,
    T_PATH,         // dot-separated path: point.x, person.address.city
    T_UNDERSCORE,   // _ wildcard pattern
    T_BACKTICK,     // ` quasiquote
    T_COMMA,        // , unquote
    T_COMMA_AT,     // ,@ unquote-splicing
    T_LBRACE,       // { for metadata dictionaries
    T_RBRACE,       // } for metadata dictionaries
    T_HASH_LBRACE,  // #{ for set literals
    T_HASH_UNDERSCORE, // #_ or #N_ form comment (int_value = N)
    T_REGEX,        // #r"pattern" compiled regex literal
    T_ERROR,
}

struct Token {
    TokenType type;
    char[64]  text;
    usz       text_len;
    long      int_value;    // For T_INT
    double    double_value; // For T_FLOAT
    usz       line;       // 1-indexed line number
    usz       column;     // 1-indexed column number
}

struct Lexer {
    char[]  source;
    usz     pos;
    usz     len;
    usz     line;     // Current line (1-indexed)
    usz     column;   // Current column (1-indexed)
    Token   current;
}

fn void Lexer.init(Lexer* self, char[] source) {
    self.source = source;
    self.pos = 0;
    self.len = source.len;
    self.line = 1;
    self.column = 1;
    self.advance();  // Prime the first token
}

fn char Lexer.peek(Lexer* self) {
    if (self.pos >= self.len) return 0;
    return self.source[self.pos];
}

fn char Lexer.next_char(Lexer* self) {
    if (self.pos >= self.len) return 0;
    char c = self.source[self.pos++];
    if (c == '\n') {
        self.line++;
        self.column = 1;
    } else {
        self.column++;
    }
    return c;
}

fn void Lexer.skip_whitespace(Lexer* self) {
    while (self.pos < self.len) {
        char c = self.source[self.pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            self.next_char();  // Track line/column
        } else if (c == ';') {
            // Line comment: skip to end of line
            while (self.pos < self.len && self.source[self.pos] != '\n') {
                self.next_char();
            }
        } else if (c == '#' && self.pos + 1 < self.len && self.source[self.pos + 1] == '|') {
            // Block comment: #| ... |# (nestable)
            self.next_char();  // consume #
            self.next_char();  // consume |
            int depth = 1;
            while (self.pos < self.len && depth > 0) {
                if (self.source[self.pos] == '#' && self.pos + 1 < self.len && self.source[self.pos + 1] == '|') {
                    depth++;
                    self.next_char();
                    self.next_char();
                } else if (self.source[self.pos] == '|' && self.pos + 1 < self.len && self.source[self.pos + 1] == '#') {
                    depth--;
                    self.next_char();
                    self.next_char();
                } else {
                    self.next_char();
                }
            }
        } else {
            break;
        }
    }
}

fn bool is_symbol_char(char c) {
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    if (c == '_' || c == '-' || c == '+' || c == '*' || c == '/' ||
        c == '=' || c == '<' || c == '>' || c == '!' || c == '?' ||
        c == ':' || c == '.' || c == '@' || c == '#' || c == '$' ||
        c == '%' || c == '&' || c == '|' || c == '^' || c == '~') {
        return true;
    }
    // Accept high bytes (>= 0x80) for UTF-8 multibyte characters (e.g. λ)
    if ((uint)c >= 0x80) return true;
    return false;
}

fn void Lexer.advance(Lexer* self) {
    self.skip_whitespace();

    // Capture token start position
    self.current.line = self.line;
    self.current.column = self.column;

    if (self.pos >= self.len) {
        self.current.type = T_EOF;
        return;
    }

    char c = self.source[self.pos];

    if (c == '(') {
        self.current.type = T_LPAREN;
        self.next_char();
        return;
    }

    if (c == ')') {
        self.current.type = T_RPAREN;
        self.next_char();
        return;
    }

    if (c == '`') {
        self.current.type = T_BACKTICK;
        self.next_char();
        return;
    }

    if (c == ',') {
        if (self.pos + 1 < self.len && self.source[self.pos + 1] == '@') {
            self.current.type = T_COMMA_AT;
            self.next_char();
            self.next_char();
        } else {
            self.current.type = T_COMMA;
            self.next_char();
        }
        return;
    }

    if (c == '\'') {
        self.current.type = T_QUOTE;
        self.next_char();
        return;
    }

    if (c == '[') {
        self.current.type = T_LBRACKET;
        self.next_char();
        return;
    }

    if (c == ']') {
        self.current.type = T_RBRACKET;
        self.next_char();
        return;
    }

    if (c == '{') {
        self.current.type = T_LBRACE;
        self.next_char();
        return;
    }

    if (c == '}') {
        self.current.type = T_RBRACE;
        self.next_char();
        return;
    }

    // Logic variable: ?name (for Deduce queries)
    if (c == '?' && self.pos + 1 < self.len) {
        char next = self.source[self.pos + 1];
        if ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z') || next == '_') {
            // Read ?varname as a symbol with ? prefix
            usz start = self.pos;
            self.next_char();  // consume ?
            usz out_idx = 0;
            self.current.text[out_idx++] = '?';
            while (self.pos < self.len && is_symbol_char(self.source[self.pos]) && out_idx < 63) {
                self.current.text[out_idx++] = self.source[self.pos];
                self.next_char();
            }
            self.current.text[out_idx] = 0;
            self.current.text_len = out_idx;
            self.current.type = T_SYMBOL;  // ?var is a symbol starting with ?
            return;
        }
    }

    // Reader dispatch: # followed by {, r", _, N_
    if (c == '#') {
        if (self.pos + 1 < self.len) {
            char next = self.source[self.pos + 1];
            if (next == '{') {
                // #{} set literal
                self.next_char();  // consume #
                self.next_char();  // consume {
                self.current.type = T_HASH_LBRACE;
                return;
            }
            if (next == '_') {
                // #_ or #N_ form comment — handled as T_HASH_UNDERSCORE
                self.next_char();  // consume #
                self.next_char();  // consume _
                self.current.type = T_HASH_UNDERSCORE;
                self.current.int_value = 1;  // default: skip 1 form
                return;
            }
            if (next >= '1' && next <= '9') {
                // #N_ form comment (e.g., #3_)
                char digit = next;
                if (self.pos + 2 < self.len && self.source[self.pos + 2] == '_') {
                    self.next_char();  // consume #
                    self.next_char();  // consume N
                    self.next_char();  // consume _
                    self.current.type = T_HASH_UNDERSCORE;
                    self.current.int_value = (long)(digit - '0');
                    return;
                }
            }
            if (next == 'r' && self.pos + 2 < self.len && self.source[self.pos + 2] == '"') {
                // #r"pattern" regex literal
                self.next_char();  // consume #
                self.next_char();  // consume r
                // Now parse the string literal (pos is at ")
                // Reuse string parsing but set type to T_REGEX
                self.next_char();  // consume opening "
                usz out_idx = 0;
                while (self.pos < self.len && self.source[self.pos] != '"') {
                    char ch = self.source[self.pos];
                    if (ch == '\\' && self.pos + 1 < self.len) {
                        self.next_char();
                        char escaped = self.source[self.pos];
                        if (out_idx < 63) { self.current.text[out_idx++] = '\\'; }
                        if (out_idx < 63) { self.current.text[out_idx++] = escaped; }
                        self.next_char();
                    } else {
                        if (out_idx < 63) { self.current.text[out_idx++] = ch; }
                        self.next_char();
                    }
                }
                if (self.pos < self.len) self.next_char();  // consume closing "
                self.current.text[out_idx] = 0;
                self.current.text_len = out_idx;
                self.current.type = T_REGEX;
                return;
            }
        }
        // Unrecognized # dispatch — treat as symbol char for backward compat
    }

    // String literal: "..."
    if (c == '"') {
        self.next_char();  // consume opening quote
        usz out_idx = 0;
        bool truncated = false;

        while (self.pos < self.len && self.source[self.pos] != '"') {
            char ch = self.source[self.pos];

            if (ch == '\\' && self.pos + 1 < self.len) {
                // Handle escape sequences
                self.next_char();
                char escaped = self.source[self.pos];
                switch (escaped) {
                    case 'n':
                        if (out_idx < 63) { self.current.text[out_idx++] = '\n'; } else { truncated = true; }
                    case 't':
                        if (out_idx < 63) { self.current.text[out_idx++] = '\t'; } else { truncated = true; }
                    case '\\':
                        if (out_idx < 63) { self.current.text[out_idx++] = '\\'; } else { truncated = true; }
                    case '"':
                        if (out_idx < 63) { self.current.text[out_idx++] = '"'; } else { truncated = true; }
                    default:
                        // Unknown escape, just include the character
                        if (out_idx < 63) { self.current.text[out_idx++] = escaped; } else { truncated = true; }
                }
                self.next_char();
            } else {
                if (out_idx < 63) { self.current.text[out_idx++] = ch; } else { truncated = true; }
                self.next_char();
            }
        }

        if (self.pos < self.len && self.source[self.pos] == '"') {
            self.next_char();  // consume closing quote
        } else {
            // EOF without closing quote
            self.current.type = T_ERROR;
            char[] err = "unterminated string literal";
            for (usz i = 0; i < err.len; i++) self.current.text[i] = err[i];
            self.current.text[err.len] = 0;
            self.current.text_len = err.len;
            return;
        }

        if (truncated) {
            self.current.type = T_ERROR;
            char[] err = "string literal too long (max 63 bytes)";
            for (usz i = 0; i < err.len; i++) self.current.text[i] = err[i];
            self.current.text[err.len] = 0;
            self.current.text_len = err.len;
            return;
        }

        self.current.text[out_idx] = 0;
        self.current.text_len = out_idx;
        self.current.type = T_STRING;
        return;
    }

    // Integer or float (with optional leading minus)
    if ((c >= '0' && c <= '9') ||
        (c == '-' && self.pos + 1 < self.len &&
         self.source[self.pos + 1] >= '0' && self.source[self.pos + 1] <= '9')) {
        usz start = self.pos;
        bool negative = false;
        if (c == '-') {
            negative = true;
            self.next_char();
        }
        long val = 0;
        while (self.pos < self.len &&
               self.source[self.pos] >= '0' && self.source[self.pos] <= '9') {
            long digit = (long)(self.source[self.pos] - '0');
            if (val > (long.max - digit) / 10) {
                self.current.type = T_ERROR;
                char[] err = "integer literal overflow";
                for (usz i = 0; i < err.len; i++) self.current.text[i] = err[i];
                self.current.text[err.len] = 0;
                self.current.text_len = err.len;
                return;
            }
            val = val * 10 + digit;
            self.next_char();
        }

        // Check for decimal point → float literal
        if (self.pos < self.len && self.source[self.pos] == '.' &&
            self.pos + 1 < self.len && self.source[self.pos + 1] >= '0' && self.source[self.pos + 1] <= '9') {
            // Parse as float: collect digits into text buffer, then convert
            double dval = (double)val;
            self.next_char(); // consume '.'
            double frac = 0.1;
            while (self.pos < self.len &&
                   self.source[self.pos] >= '0' && self.source[self.pos] <= '9') {
                dval += (double)(self.source[self.pos] - '0') * frac;
                frac *= 0.1;
                self.next_char();
            }
            // Optional exponent: e/E followed by optional +/- and digits
            if (self.pos < self.len && (self.source[self.pos] == 'e' || self.source[self.pos] == 'E')) {
                self.next_char(); // consume e/E
                bool exp_neg = false;
                if (self.pos < self.len && (self.source[self.pos] == '+' || self.source[self.pos] == '-')) {
                    exp_neg = self.source[self.pos] == '-';
                    self.next_char();
                }
                long exp_val = 0;
                while (self.pos < self.len &&
                       self.source[self.pos] >= '0' && self.source[self.pos] <= '9') {
                    exp_val = exp_val * 10 + (long)(self.source[self.pos] - '0');
                    self.next_char();
                }
                double multiplier = 1.0;
                for (long i = 0; i < exp_val; i++) multiplier *= 10.0;
                if (exp_neg) { dval /= multiplier; } else { dval *= multiplier; }
            }
            if (negative) dval = -dval;
            self.current.type = T_FLOAT;
            self.current.double_value = dval;
            self.current.text_len = self.pos - start;
            return;
        }

        if (negative) val = -val;

        self.current.type = T_INT;
        self.current.int_value = val;
        self.current.text_len = self.pos - start;
        return;
    }

    // Rest pattern: .. (must check before .[ and symbol)
    if (c == '.' && self.pos + 1 < self.len && self.source[self.pos + 1] == '.') {
        // Make sure it's not ... or longer
        if (self.pos + 2 >= self.len || self.source[self.pos + 2] != '.') {
            self.current.type = T_DOTDOT;
            self.current.text[0] = '.';
            self.current.text[1] = '.';
            self.current.text[2] = 0;
            self.current.text_len = 2;
            self.next_char();
            self.next_char();
            return;
        }
    }

    // Index access: .[ (dot-bracket notation)
    // Must check before symbol parsing since . is a symbol char
    if (c == '.' && self.pos + 1 < self.len && self.source[self.pos + 1] == '[') {
        self.current.type = T_DOT_BRACKET;
        self.current.text[0] = '.';
        self.current.text[1] = '[';
        self.current.text[2] = 0;
        self.current.text_len = 2;
        self.next_char();
        self.next_char();
        return;
    }

    // Wildcard: _ (standalone underscore)
    if (c == '_') {
        // Check if it's a standalone _ (not part of a symbol like _foo)
        if (self.pos + 1 >= self.len || !is_symbol_char(self.source[self.pos + 1])) {
            self.current.type = T_UNDERSCORE;
            self.current.text[0] = '_';
            self.current.text[1] = 0;
            self.current.text_len = 1;
            self.pos++;
            return;
        }
    }

    // Symbol or Path (dot-separated like point.x or person.address.city)
    // Stops when seeing .[ (index access) or .. (rest pattern)
    if (is_symbol_char(c)) {
        usz start = self.pos;
        bool has_dot = false;
        while (self.pos < self.len && is_symbol_char(self.source[self.pos])) {
            // Stop before .[ so it can be lexed as T_DOT_BRACKET
            if (self.source[self.pos] == '.' &&
                self.pos + 1 < self.len && self.source[self.pos + 1] == '[') {
                break;
            }
            // Stop before .. so it can be lexed as T_DOTDOT
            if (self.source[self.pos] == '.' &&
                self.pos + 1 < self.len && self.source[self.pos + 1] == '.') {
                break;
            }
            if (self.source[self.pos] == '.') {
                has_dot = true;
            }
            self.next_char();
        }
        usz len = self.pos - start;
        if (len > 63) {
            self.current.type = T_ERROR;
            char[] err = "symbol name too long (max 63 bytes)";
            for (usz i = 0; i < err.len; i++) self.current.text[i] = err[i];
            self.current.text[err.len] = 0;
            self.current.text_len = err.len;
            return;
        }

        for (usz i = 0; i < len; i++) {
            self.current.text[i] = self.source[start + i];
        }
        self.current.text[len] = 0;
        self.current.text_len = len;

        // Classify as T_PATH if contains dot (and not just a dot at start/end)
        // e.g., "point.x" is a path, ".x" or "x." or "." alone are just symbols
        if (has_dot && len > 1 && self.current.text[0] != '.' && self.current.text[len-1] != '.') {
            self.current.type = T_PATH;
        } else {
            self.current.type = T_SYMBOL;
        }
        return;
    }

    // Unknown character
    self.current.type = T_ERROR;
    self.pos++;
}

fn bool Lexer.at_end(Lexer* self) {
    return self.current.type == T_EOF;
}

fn bool Lexer.match(Lexer* self, TokenType t) {
    if (self.current.type == t) {
        self.advance();
        return true;
    }
    return false;
}

fn void Lexer.expect(Lexer* self, TokenType t, char[] msg) {
    if (!self.match(t)) {
        io::printfn("Parse error at line %d, col %d: expected '%s'", self.line, self.column, msg);
        unreachable("parse error: expected token not found");
    }
}

// =============================================================================
// SECTION 2: PARSER
// =============================================================================

struct Parser {
    Lexer*    lexer;
    Interp*   interp;
    bool      has_error;
    char[256] error_msg;
    usz       error_msg_len;
    usz       error_line;
    usz       error_col;
    int       depth;
}

fn void Parser.init(Parser* self, Lexer* lexer, Interp* interp) {
    self.lexer = lexer;
    self.interp = interp;
    self.has_error = false;
    self.error_msg_len = 0;
    self.error_line = 0;
    self.error_col = 0;
    self.depth = 0;
}

/**
 * Set a parse error (keeps first error only).
 */
fn void Parser.set_error(Parser* self, char[] msg) {
    if (self.has_error) return;  // keep first error
    self.has_error = true;
    self.error_line = self.lexer.current.line;
    self.error_col = self.lexer.current.column;
    usz len = msg.len;
    if (len > 255) len = 255;
    for (usz i = 0; i < len; i++) self.error_msg[i] = msg[i];
    self.error_msg[len] = 0;
    self.error_msg_len = len;
}

/**
 * Expect a token type, or set a parse error.
 */
fn void Parser.expect(Parser* self, TokenType t, char[] msg) {
    if (self.has_error) return;
    if (!self.lexer.match(t)) {
        self.set_error(msg);
    }
}

/**
 * Allocate an expression and capture current source location.
 */
fn Expr* Parser.alloc_expr_here(Parser* self) {
    Expr* e = self.interp.alloc_expr();
    e.loc_line = self.lexer.current.line;
    e.loc_column = self.lexer.current.column;
    return e;
}

/**
 * Get interned symbol from current token text.
 */
fn SymbolId Parser.get_current_symbol(Parser* self) {
    return self.interp.symbols.intern(self.lexer.current.text[:self.lexer.current.text_len]);
}

/**
 * Parse a ^Type annotation on a parameter.
 * Three forms:
 *   ^Int         — simple type (single token ^Int)
 *   ^(List Int)  — compound type (^ then parens)
 *   ^{'T Number} — metadata dictionary (^ then braces)
 */
fn TypeAnnotation Parser.parse_type_annotation(Parser* self) {
    TypeAnnotation ann;
    ann.has_annotation = false;
    ann.is_compound = false;
    ann.is_dict = false;
    ann.param_count = 0;
    ann.meta_count = 0;
    ann.has_val_literal = false;

    if (self.lexer.current.type != T_SYMBOL) return ann;
    if (self.lexer.current.text[0] != '^') return ann;

    if (self.lexer.current.text_len > 1) {
        // Form 1: ^TypeName — strip leading ^, intern rest as base_type
        ann.has_annotation = true;
        ann.base_type = self.interp.symbols.intern(self.lexer.current.text[1..self.lexer.current.text_len - 1]);
        self.lexer.advance();
        return ann;
    }

    // text is exactly "^" (1 char) — peek at next token
    self.lexer.advance();  // consume "^"

    if (self.lexer.current.type == T_LPAREN) {
        // Form 2: ^(Compound T1 T2) or ^(Val 42)
        ann.has_annotation = true;
        ann.is_compound = true;
        self.lexer.advance();  // consume (

        // Parse base type name
        if (self.lexer.current.type != T_SYMBOL) {
            self.set_error("expected type name after ^(");
            return ann;
        }
        ann.base_type = self.get_current_symbol();
        self.lexer.advance();

        // Check for Val literal: ^(Val 42)
        if (ann.base_type == self.interp.sym_Val && self.lexer.current.type == T_INT) {
            ann.has_val_literal = true;
            ann.val_literal = self.lexer.current.int_value;
            self.lexer.advance();
        } else {
            // Collect type params until )
            while (self.lexer.current.type == T_SYMBOL && ann.param_count < MAX_TYPE_PARAMS) {
                ann.params[ann.param_count] = self.get_current_symbol();
                ann.param_count++;
                self.lexer.advance();
            }
        }
        self.expect(T_RPAREN, ")");
        return ann;
    }

    if (self.lexer.current.type == T_LBRACE) {
        // Form 3: ^{'key value 'key2 value2 ...} — Clojure-like map
        ann.has_annotation = true;
        ann.is_dict = true;
        self.lexer.advance();  // consume {

        while (self.lexer.current.type != T_RBRACE && !self.has_error) {
            if (ann.meta_count >= MAX_META_ENTRIES) break;
            // Keys are 'symbol (T_QUOTE + T_SYMBOL)
            if (self.lexer.current.type == T_QUOTE) {
                self.lexer.advance();
                if (self.lexer.current.type != T_SYMBOL) {
                    self.set_error("expected symbol after ' in metadata dict");
                    return ann;
                }
                ann.meta[ann.meta_count].key = self.get_current_symbol();
                self.lexer.advance();
            } else {
                self.set_error("expected 'key in metadata dict");
                return ann;
            }
            // Values: symbol (type bound) or integer (arity)
            if (self.lexer.current.type == T_INT) {
                ann.meta[ann.meta_count].is_int = true;
                ann.meta[ann.meta_count].int_value = self.lexer.current.int_value;
                self.lexer.advance();
            } else if (self.lexer.current.type == T_SYMBOL) {
                ann.meta[ann.meta_count].sym_value = self.get_current_symbol();
                self.lexer.advance();
            } else {
                self.set_error("expected value in metadata dict");
                return ann;
            }
            ann.meta_count++;
        }
        if (self.lexer.current.type == T_RBRACE) {
            self.lexer.advance();  // consume }
        } else if (!self.has_error) {
            self.set_error("expected } in metadata dict");
        }
        return ann;
    }

    self.set_error("expected type name, (, or { after ^");
    return ann;
}

/**
 * Check for and parse postfix index access: expr.[index]
 * Returns the expression wrapped in E_INDEX if followed by .[, otherwise returns unchanged.
 *
 * OmniLisp dot-bracket syntax:
 *   arr.[0]           - array/list access by integer index
 *   dict.['key]       - dictionary access by quoted symbol key
 *   matrix.[i].[j]    - chained indexing
 */
fn Expr* Parser.parse_postfix_index(Parser* self, Expr* base) {
    if (self.has_error) return base;
    // Keep parsing postfix .[index] as long as we see .[
    while (self.lexer.current.type == T_DOT_BRACKET && !self.has_error) {
        self.lexer.advance();  // consume '.['

        // Parse the index expression
        Expr* idx = self.parse_expr();

        self.expect(T_RBRACKET, "]");

        // Wrap in E_INDEX
        Expr* indexed = self.alloc_expr_here();
        indexed.tag = E_INDEX;
        indexed.index.collection = base;
        indexed.index.index = idx;

        base = indexed;
    }
    return base;
}

/**
 * Parse a complete expression.
 */
fn Expr* Parser.parse_expr(Parser* self) {
    if (self.has_error) return null;
    self.depth++;
    defer self.depth--;
    if (self.depth > 256) { self.set_error("expression nesting too deep (max 256)"); return null; }
    Lexer* lex = self.lexer;

    // Quasiquote shorthand: `x
    if (lex.current.type == T_BACKTICK) {
        Expr* e = self.alloc_expr_here();
        lex.advance();
        e.tag = E_QUASIQUOTE;
        e.quasiquote.body = self.parse_qq_template();
        return e;
    }

    // Unquote shorthand: ,x
    if (lex.current.type == T_COMMA) {
        Expr* e = self.alloc_expr_here();
        lex.advance();
        e.tag = E_UNQUOTE;
        e.unquote.body = self.parse_expr();
        return e;
    }

    // Unquote-splicing shorthand: ,@x
    if (lex.current.type == T_COMMA_AT) {
        Expr* e = self.alloc_expr_here();
        lex.advance();
        e.tag = E_UNQUOTE_SPLICING;
        e.unquote_splicing.body = self.parse_expr();
        return e;
    }

    // Form comment: #_ or #N_ — skip next N forms, then parse the real expr
    if (lex.current.type == T_HASH_UNDERSCORE) {
        long skip_count = lex.current.int_value;
        lex.advance();
        for (long i = 0; i < skip_count; i++) {
            Expr* discarded = self.parse_expr();
            if (discarded == null || self.has_error) return null;
            // Intentionally discarded
        }
        return self.parse_expr();  // Parse the real expression after skipped forms
    }

    // Set literal: #{a b c} => (set a b c)
    if (lex.current.type == T_HASH_LBRACE) {
        Expr* e = self.alloc_expr_here();
        lex.advance();  // consume #{ (already past it)
        // Collect elements until }
        Expr*[64] elems;
        usz elem_count = 0;
        while (lex.current.type != T_RBRACE && lex.current.type != T_EOF && !self.has_error) {
            if (elem_count >= 64) { self.set_error("set literal too large (max 64 elements)"); return null; }
            elems[elem_count++] = self.parse_expr();
        }
        self.expect(T_RBRACE, "}");
        // Desugar to (set elem1 elem2 ...)
        e.tag = E_CALL;
        usz total = elem_count + 1;  // set + elements
        Expr** args = (Expr**)mem::malloc(Expr*.sizeof * total);
        // First arg: the symbol 'set'
        args[0] = self.interp.alloc_expr();
        args[0].tag = E_VAR;
        args[0].var_expr.name = self.interp.symbols.intern("set");
        for (usz i = 0; i < elem_count; i++) args[i + 1] = elems[i];
        e.call.args = args;
        e.call.arg_count = total;
        return e;
    }

    // Regex literal: #r"pattern" => pre-compiled regex value
    if (lex.current.type == T_REGEX) {
        // Treat regex literal same as T_STRING: save text, then advance
        Expr* e = self.alloc_expr_here();
        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value_root();
        e.lit.value.tag = STRING;
        // Save text BEFORE advance (advance overwrites current token)
        usz plen = lex.current.text_len;
        e.lit.value.str_val.chars = (char*)mem::malloc(plen + 1);
        for (usz i = 0; i < plen; i++) e.lit.value.str_val.chars[i] = lex.current.text[i];
        e.lit.value.str_val.chars[plen] = 0;
        e.lit.value.str_val.len = plen;
        e.lit.value.str_val.capacity = plen + 1;
        lex.advance();
        return e;
    }

    // Quote shorthand: 'x => (quote x)
    if (lex.current.type == T_QUOTE) {
        Expr* e = self.alloc_expr_here();  // Capture quote location before consuming
        lex.advance();
        e.tag = E_QUOTE;
        e.quote.datum = self.parse_datum();
        return e;
    }

    // Integer literal - may be followed by postfix .[index]
    if (lex.current.type == T_INT) {
        Expr* e = self.alloc_expr_here();  // Capture location before consuming
        long val = lex.current.int_value;
        lex.advance();

        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value_root();
        e.lit.value.tag = INT;
        e.lit.value.int_val = val;
        return self.parse_postfix_index(e);
    }

    // Float literal
    if (lex.current.type == T_FLOAT) {
        Expr* e = self.alloc_expr_here();
        double dval = lex.current.double_value;
        lex.advance();

        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value_root();
        e.lit.value.tag = DOUBLE;
        e.lit.value.double_val = dval;
        return self.parse_postfix_index(e);
    }

    // String literal - may be followed by postfix .[index]
    if (lex.current.type == T_STRING) {
        Expr* e = self.alloc_expr_here();  // Capture location before consuming
        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value_root();
        e.lit.value.tag = STRING;
        usz len = lex.current.text_len;
        e.lit.value.str_val = strval_new(len);
        for (usz i = 0; i < len; i++) {
            e.lit.value.str_val.chars[i] = lex.current.text[i];
        }
        e.lit.value.str_val.chars[len] = 0;
        e.lit.value.str_val.len = len;
        lex.advance();
        return self.parse_postfix_index(e);
    }

    // Symbol (variable reference) - may be followed by postfix .[index]
    if (lex.current.type == T_SYMBOL) {
        Expr* e = self.alloc_expr_here();  // Capture location before consuming
        SymbolId sym = self.get_current_symbol();
        lex.advance();

        e.tag = E_VAR;
        e.var_expr.name = sym;
        return self.parse_postfix_index(e);
    }

    // Path expression (e.g., point.x, person.address.city) - may be followed by .[index]
    if (lex.current.type == T_PATH) {
        Expr* e = self.alloc_expr_here();  // Capture location before consuming
        e.tag = E_PATH;
        e.path.segment_count = 0;

        // Split the path text on '.' and intern each segment
        usz text_len = lex.current.text_len;
        usz start = 0;
        for (usz i = 0; i <= text_len; i++) {
            if (i == text_len || lex.current.text[i] == '.') {
                if (i > start) {
                    if (e.path.segment_count >= MAX_PATH_SEGMENTS) { self.set_error("too many path segments"); return null; }
                    usz seg_len = i - start;
                    SymbolId seg_sym = self.interp.symbols.intern(lex.current.text[start:seg_len]);

                    e.path.segments[e.path.segment_count] = seg_sym;
                    e.path.segment_count++;
                }
                start = i + 1;
            }
        }
        lex.advance();
        return self.parse_postfix_index(e);
    }

    // Dict literal: {k1 v1 k2 v2 ...} → (dict k1 v1 k2 v2 ...)
    if (lex.current.type == T_LBRACE) {
        Expr* e = self.alloc_expr_here();
        lex.advance();  // consume {

        // Build E_CALL: func = E_VAR("dict"), args = collected expressions
        Expr* func = self.interp.alloc_expr();
        func.tag = E_VAR;
        func.var_expr.name = self.interp.symbols.intern("dict");

        List{Expr*} args;
        while (lex.current.type != T_RBRACE && !self.has_error) {
            Expr* arg = self.parse_expr();
            if (arg == null) { args.free(); return null; }
            args.push(arg);
        }

        e.tag = E_CALL;
        e.call = mem::malloc(ExprCall.sizeof);
        e.call.func = func;
        e.call.arg_count = args.len();
        e.call.args = (Expr**)mem::malloc(Expr*.sizeof * args.len());
        for (usz i = 0; i < args.len(); i++) { e.call.args[i] = args[i]; }
        args.free();

        if (e.call.arg_count % 2 != 0) {
            self.set_error("dict literal requires even number of elements");
            return null;
        }

        self.expect(T_RBRACE, "}");
        return self.parse_postfix_index(e);
    }

    // Array literal: [e1 e2 e3 ...] → (array e1 e2 e3 ...)
    if (lex.current.type == T_LBRACKET) {
        Expr* e = self.alloc_expr_here();
        lex.advance();  // consume [

        // Build E_CALL: func = E_VAR("array"), args = collected expressions
        Expr* func = self.interp.alloc_expr();
        func.tag = E_VAR;
        func.var_expr.name = self.interp.symbols.intern("array");

        List{Expr*} args;
        while (lex.current.type != T_RBRACKET && !self.has_error) {
            Expr* arg = self.parse_expr();
            if (arg == null) { args.free(); return null; }
            args.push(arg);
        }

        e.tag = E_CALL;
        e.call = mem::malloc(ExprCall.sizeof);
        e.call.func = func;
        e.call.arg_count = args.len();
        e.call.args = (Expr**)mem::malloc(Expr*.sizeof * args.len());
        for (usz i = 0; i < args.len(); i++) { e.call.args[i] = args[i]; }
        args.free();

        self.expect(T_RBRACKET, "]");
        return self.parse_postfix_index(e);
    }

    // List form: (...)
    if (lex.current.type == T_LPAREN) {
        lex.advance();

        if (lex.current.type == T_RPAREN) {
            // Empty list () = nil literal
            Expr* e = self.alloc_expr_here();  // Capture location before consuming
            lex.advance();
            e.tag = E_LIT;
            e.lit.value = self.interp.alloc_value_root();
            e.lit.value.tag = NIL;
            return e;
        }

        // Get the first element to check for special forms
        if (lex.current.type == T_SYMBOL) {
            SymbolId head = self.get_current_symbol();

            // Check special forms
            if ((uint)head == (uint)self.interp.sym_lambda) {
                return self.parse_lambda();
            }
            // Check for λ (U+03BB, UTF-8: CE BB)
            char[] head_text = self.interp.symbols.get_name(head);
            if (head_text.len == 2 && head_text[0] == (char)0xCE && head_text[1] == (char)0xBB) {
                return self.parse_lambda();
            }
            if ((uint)head == (uint)self.interp.sym_if) {
                return self.parse_if();
            }
            if ((uint)head == (uint)self.interp.sym_let) {
                return self.parse_let();
            }
            if ((uint)head == (uint)self.interp.sym_define) {
                return self.parse_define();
            }
            if ((uint)head == (uint)self.interp.sym_quote) {
                return self.parse_quote();
            }
            if ((uint)head == (uint)self.interp.sym_reset) {
                return self.parse_reset();
            }
            if ((uint)head == (uint)self.interp.sym_shift) {
                return self.parse_shift();
            }
            if ((uint)head == (uint)self.interp.sym_signal) {
                return self.parse_perform();
            }
            if ((uint)head == (uint)self.interp.sym_handle) {
                return self.parse_handle();
            }
            if ((uint)head == (uint)self.interp.sym_resolve) {
                return self.parse_resolve();
            }
            if ((uint)head == (uint)self.interp.sym_with_continuation) {
                return self.parse_with_continuation();
            }
            if ((uint)head == (uint)self.interp.sym_match) {
                return self.parse_match();
            }
            if ((uint)head == (uint)self.interp.sym_and) {
                return self.parse_and();
            }
            if ((uint)head == (uint)self.interp.sym_or) {
                return self.parse_or();
            }
            if ((uint)head == (uint)self.interp.sym_begin) {
                return self.parse_begin();
            }
            if ((uint)head == (uint)self.interp.sym_set) {
                return self.parse_set();
            }
            if ((uint)head == (uint)self.interp.sym_quasiquote) {
                Expr* e = self.alloc_expr_here();
                self.lexer.advance();  // consume 'quasiquote'
                e.tag = E_QUASIQUOTE;
                e.quasiquote.body = self.parse_qq_template();
                self.expect(T_RPAREN, ")");
                return e;
            }
            if ((uint)head == (uint)self.interp.sym_unquote) {
                Expr* e = self.alloc_expr_here();
                self.lexer.advance();  // consume 'unquote'
                e.tag = E_UNQUOTE;
                e.unquote.body = self.parse_expr();
                self.expect(T_RPAREN, ")");
                return e;
            }
            if ((uint)head == (uint)self.interp.sym_unquote_splicing) {
                Expr* e = self.alloc_expr_here();
                self.lexer.advance();  // consume 'unquote-splicing'
                e.tag = E_UNQUOTE_SPLICING;
                e.unquote_splicing.body = self.parse_expr();
                self.expect(T_RPAREN, ")");
                return e;
            }
            if ((uint)head == (uint)self.interp.sym_module) {
                return self.parse_module();
            }
            if ((uint)head == (uint)self.interp.sym_import) {
                return self.parse_import();
            }
            if ((uint)head == (uint)self.interp.sym_export_from) {
                return self.parse_export_from();
            }
            if ((uint)head == (uint)self.interp.sym_pipe) {
                return self.parse_pipe();
            }
        }

        // Regular application
        return self.parse_application();
    }

    // Placeholder _ in expression context (for partial application desugaring)
    if (lex.current.type == T_UNDERSCORE) {
        Expr* e = self.alloc_expr_here();
        lex.advance();
        e.tag = E_VAR;
        e.var_expr.name = self.interp.sym_placeholder;
        return e;
    }

    if (lex.current.type == T_EOF) {
        self.set_error("unexpected end of input");
    } else {
        self.set_error("unexpected token in expression");
    }
    return null;
}

// Parse one or more body expressions before a closing ')'.
// If multiple expressions, wraps them in an implicit E_BEGIN.
// Does NOT consume the closing ')'.
fn Expr* Parser.parse_implicit_begin(Parser* self) {
    if (self.has_error) return null;
    Expr* first = self.parse_expr();
    if (self.has_error) return null;
    if (self.lexer.current.type == T_RPAREN) {
        // Single body expression — return as-is
        return first;
    }
    // Multiple body expressions — wrap in begin
    List{Expr*} items;
    items.push(first);
    while (self.lexer.current.type != T_RPAREN && !self.has_error) {
        items.push(self.parse_expr());
    }
    Expr* begin_e = self.interp.alloc_expr();
    begin_e.tag = E_BEGIN;
    begin_e.begin = (ExprBegin*)mem::malloc(ExprBegin.sizeof);
    begin_e.begin.expr_count = items.len();
    begin_e.begin.exprs = (Expr**)mem::malloc(Expr*.sizeof * items.len());
    for (usz i = 0; i < items.len(); i++) { begin_e.begin.exprs[i] = items[i]; }
    items.free();
    return begin_e;
}

fn Expr* Parser.parse_lambda(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'lambda' location
    self.lexer.advance();  // consume 'lambda'

    self.expect(T_LPAREN, "(");

    // Check for zero-arg lambda: (lambda () body...)
    if (self.lexer.current.type == T_RPAREN) {
        self.lexer.advance();  // consume ')'

        // Parse body (implicit begin for multiple expressions)
        Expr* body = self.parse_implicit_begin();
        self.expect(T_RPAREN, ")");

        e.tag = E_LAMBDA;
        e.lambda = mem::malloc(ExprLambda.sizeof);
        e.lambda.param = (SymbolId)0xFFFFFFFF;  // sentinel for "no param"
        e.lambda.param_count = 0;
        e.lambda.params = null;
        e.lambda.has_rest = false;
        e.lambda.rest_param = 0;
        e.lambda.has_typed_params = false;
        e.lambda.param_annotations = null;
        e.lambda.body = body;
        return e;
    }

    // Check for (lambda (.. rest) body...) - variadic with no fixed params
    if (self.lexer.current.type == T_DOTDOT) {
        self.lexer.advance();  // consume '..'
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected rest parameter name after .."); return null; }
        SymbolId rest_param = self.get_current_symbol();
        self.lexer.advance();
        self.expect(T_RPAREN, ")");

        Expr* body = self.parse_implicit_begin();
        self.expect(T_RPAREN, ")");
        if (self.has_error) return null;

        e.tag = E_LAMBDA;
        e.lambda = mem::malloc(ExprLambda.sizeof);
        e.lambda.param = (SymbolId)0xFFFFFFFF;  // sentinel: no fixed params
        e.lambda.param_count = 0;
        e.lambda.params = null;
        e.lambda.has_rest = true;
        e.lambda.rest_param = rest_param;
        e.lambda.param_annotations = null;
        e.lambda.body = body;
        return e;
    }

    // Collect all fixed parameters until ')' or '..'
    // Also collect type annotations: (lambda ((^Int x) (^String y)) body)
    List{SymbolId} params;
    List{TypeAnnotation} param_anns;
    bool has_any_typed = false;

    while (self.lexer.current.type == T_SYMBOL || self.lexer.current.type == T_LPAREN) {
        if (self.lexer.current.type == T_LPAREN) {
            // Typed param: (^Type name)
            self.lexer.advance();  // consume (
            TypeAnnotation ann = self.parse_type_annotation();
            if (!ann.has_annotation) {
                params.free(); param_anns.free();
                self.set_error("expected type annotation in typed parameter");
                return null;
            }
            if (self.lexer.current.type != T_SYMBOL) {
                params.free(); param_anns.free();
                self.set_error("expected parameter name after type annotation");
                return null;
            }
            params.push(self.get_current_symbol());
            param_anns.push(ann);
            has_any_typed = true;
            self.lexer.advance();
            self.expect(T_RPAREN, ")");
        } else {
            // Untyped param: name
            params.push(self.get_current_symbol());
            TypeAnnotation empty_ann;
            empty_ann.has_annotation = false;
            param_anns.push(empty_ann);
            self.lexer.advance();
        }

        if (self.lexer.current.type == T_DOTDOT) {
            break;
        }
        if (self.lexer.current.type == T_RPAREN) {
            break;
        }
    }

    usz param_count = params.len();

    // Check for '..' rest parameter: (lambda (x y .. rest) body...)
    if (self.lexer.current.type == T_DOTDOT) {
        self.lexer.advance();  // consume '..'
        if (self.lexer.current.type != T_SYMBOL) { params.free(); param_anns.free(); self.set_error("expected rest parameter name after .."); return null; }
        SymbolId rest_param = self.get_current_symbol();
        self.lexer.advance();
        self.expect(T_RPAREN, ")");

        Expr* body = self.parse_implicit_begin();
        self.expect(T_RPAREN, ")");

        e.tag = E_LAMBDA;
        e.lambda = mem::malloc(ExprLambda.sizeof);
        e.lambda.param = param_count > 0 ? params[0] : (SymbolId)0xFFFFFFFF;
        e.lambda.param_count = param_count;
        e.lambda.params = (SymbolId*)mem::malloc(SymbolId.sizeof * param_count);
        for (usz i = 0; i < param_count; i++) { e.lambda.params[i] = params[i]; }
        e.lambda.has_rest = true;
        e.lambda.rest_param = rest_param;
        e.lambda.has_typed_params = has_any_typed;
        e.lambda.param_annotations = null;
        if (has_any_typed) {
            e.lambda.param_annotations = (TypeAnnotation*)mem::malloc(TypeAnnotation.sizeof * param_count);
            for (usz i = 0; i < param_count; i++) { e.lambda.param_annotations[i] = param_anns[i]; }
        }
        e.lambda.body = body;
        params.free(); param_anns.free();
        return e;
    }

    self.expect(T_RPAREN, ")");

    // Parse body (implicit begin for multiple expressions)
    Expr* body = self.parse_implicit_begin();
    self.expect(T_RPAREN, ")");

    if (self.has_error) { params.free(); param_anns.free(); return null; }
    if (param_count < 1) { params.free(); param_anns.free(); self.set_error("expected at least one parameter"); return null; }

    // Build E_LAMBDA (same path for all non-variadic cases)
    e.tag = E_LAMBDA;
    e.lambda = mem::malloc(ExprLambda.sizeof);
    e.lambda.param = params[0];
    e.lambda.param_count = param_count;
    e.lambda.params = (SymbolId*)mem::malloc(SymbolId.sizeof * param_count);
    for (usz i = 0; i < param_count; i++) { e.lambda.params[i] = params[i]; }
    e.lambda.has_rest = false;
    e.lambda.rest_param = 0;
    e.lambda.body = body;
    e.lambda.has_typed_params = has_any_typed;
    e.lambda.param_annotations = null;
    if (has_any_typed) {
        e.lambda.param_annotations = (TypeAnnotation*)mem::malloc(TypeAnnotation.sizeof * param_count);
        for (usz i = 0; i < param_count; i++) { e.lambda.param_annotations[i] = param_anns[i]; }
    }
    params.free(); param_anns.free();
    return e;
}

fn Expr* Parser.parse_if(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'if' location
    self.lexer.advance();  // consume 'if'

    Expr* test = self.parse_expr();
    Expr* then_branch = self.parse_expr();
    Expr* else_branch = self.parse_expr();

    self.expect(T_RPAREN, ")");

    e.tag = E_IF;
    e.if_expr.test = test;
    e.if_expr.then_branch = then_branch;
    e.if_expr.else_branch = else_branch;
    return e;
}

fn Expr* Parser.parse_let(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'let' location
    self.lexer.advance();  // consume 'let'

    // Check for ^rec metadata
    bool is_recursive = false;
    if (self.lexer.current.type == T_SYMBOL) {
        // Check if symbol starts with ^
        if (self.lexer.current.text_len >= 1 && self.lexer.current.text[0] == '^') {
            // Check for ^rec specifically
            if (self.lexer.current.text_len == 4 &&
                self.lexer.current.text[1] == 'r' &&
                self.lexer.current.text[2] == 'e' &&
                self.lexer.current.text[3] == 'c') {
                is_recursive = true;
                self.lexer.advance();  // consume ^rec
            }
        }
    }

    // Check for named let: (let name (var init ...) body...)
    // After consuming 'let' and optional '^rec', if next is a symbol
    // followed by '(', this is a named let (loop).
    if (self.lexer.current.type == T_SYMBOL) {
        SymbolId loop_name = self.get_current_symbol();
        self.lexer.advance();  // consume loop name

        return self.parse_named_let(e, loop_name);
    }

    self.expect(T_LPAREN, "(");  // consume '(' of bindings list

    // Collect flat-pair bindings: (x 1 y 2 z 3)
    List{SymbolId} names;
    List{Expr*} inits;

    while (self.lexer.current.type == T_SYMBOL && !self.has_error) {
        names.push(self.get_current_symbol());
        self.lexer.advance();
        inits.push(self.parse_expr());
    }
    if (self.has_error) { names.free(); inits.free(); return null; }

    self.expect(T_RPAREN, ")");  // consume ')' of bindings list
    if (self.has_error) { names.free(); inits.free(); return null; }

    usz binding_count = names.len();
    if (binding_count < 1) { names.free(); inits.free(); self.set_error("expected at least one binding in let"); return null; }

    Expr* body = self.parse_implicit_begin();

    self.expect(T_RPAREN, ")");  // consume outer ')' of let form

    if (binding_count == 1) {
        // Single binding - simple case
        e.tag = E_LET;
        e.let_expr.name = names[0];
        e.let_expr.init = inits[0];
        e.let_expr.body = body;
        e.let_expr.is_recursive = is_recursive;
        names.free(); inits.free();
        return e;
    }

    // Multi-binding: desugar (let ((x 1) (y 2) (z 3)) body) to nested lets
    // Build from inside out: innermost let has last binding and body
    Expr* inner = body;
    for (usz i = binding_count; i > 1; i--) {
        Expr* wrapper = self.interp.alloc_expr();
        wrapper.tag = E_LET;
        wrapper.loc_line = e.loc_line;
        wrapper.loc_column = e.loc_column;
        wrapper.let_expr.name = names[i - 1];
        wrapper.let_expr.init = inits[i - 1];
        wrapper.let_expr.body = inner;
        wrapper.let_expr.is_recursive = false;  // only first binding can be ^rec
        inner = wrapper;
    }
    // Outermost let with first binding
    e.tag = E_LET;
    e.let_expr.name = names[0];
    e.let_expr.init = inits[0];
    e.let_expr.body = inner;
    e.let_expr.is_recursive = is_recursive;
    names.free(); inits.free();
    return e;
}

/**
 * Parse a named let (loop).
 *
 * (let name (x init-x y init-y) body...)
 *
 * Desugars to:
 * (let ^rec (name (lambda (x y) body)) (name init-x init-y))
 */
fn Expr* Parser.parse_named_let(Parser* self, Expr* e, SymbolId loop_name) {
    if (self.has_error) return null;
    self.expect(T_LPAREN, "(");  // consume '(' of bindings list

    // Collect flat-pair bindings: name init name init ...
    List{SymbolId} params;
    List{Expr*} inits;

    while (self.lexer.current.type == T_SYMBOL && !self.has_error) {
        params.push(self.get_current_symbol());
        self.lexer.advance();
        inits.push(self.parse_expr());
    }
    if (self.has_error) { params.free(); inits.free(); return null; }

    self.expect(T_RPAREN, ")");  // consume ')' of bindings list
    if (self.has_error) { params.free(); inits.free(); return null; }
    if (params.len() < 1) { params.free(); inits.free(); self.set_error("expected at least one binding in named let"); return null; }

    usz param_count = params.len();

    // Parse body (implicit begin for multiple expressions)
    Expr* body = self.parse_implicit_begin();
    self.expect(T_RPAREN, ")");  // consume outer ')' of let form

    // Build the lambda: (lambda (param1 param2 ...) body)
    Expr* lambda_expr = self.interp.alloc_expr();
    lambda_expr.tag = E_LAMBDA;
    lambda_expr.lambda = mem::malloc(ExprLambda.sizeof);
    lambda_expr.loc_line = e.loc_line;
    lambda_expr.loc_column = e.loc_column;
    lambda_expr.lambda.param = params[0];
    lambda_expr.lambda.param_count = param_count;
    lambda_expr.lambda.params = (SymbolId*)mem::malloc(SymbolId.sizeof * param_count);
    for (usz i = 0; i < param_count; i++) {
        lambda_expr.lambda.params[i] = params[i];
    }
    lambda_expr.lambda.has_rest = false;
    lambda_expr.lambda.rest_param = 0;
    lambda_expr.lambda.has_typed_params = false;
    lambda_expr.lambda.param_annotations = null;
    lambda_expr.lambda.body = body;

    // Build the call: (name init1 init2 ...)
    Expr* call_expr = self.interp.alloc_expr();
    call_expr.tag = E_CALL;
    call_expr.call = mem::malloc(ExprCall.sizeof);
    call_expr.loc_line = e.loc_line;
    call_expr.loc_column = e.loc_column;
    Expr* name_ref = self.interp.alloc_expr();
    name_ref.tag = E_VAR;
    name_ref.loc_line = e.loc_line;
    name_ref.loc_column = e.loc_column;
    name_ref.var_expr.name = loop_name;
    call_expr.call.func = name_ref;
    call_expr.call.arg_count = param_count;
    call_expr.call.args = (Expr**)mem::malloc(Expr*.sizeof * param_count);
    for (usz i = 0; i < param_count; i++) {
        call_expr.call.args[i] = inits[i];
    }
    params.free();
    inits.free();

    // Assemble: (let ^rec ((name lambda)) call)
    e.tag = E_LET;
    e.let_expr.name = loop_name;
    e.let_expr.init = lambda_expr;
    e.let_expr.body = call_expr;
    e.let_expr.is_recursive = true;  // named let is always recursive
    return e;
}

fn Expr* Parser.parse_define(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'define' location
    self.lexer.advance();  // consume 'define'

    // Check for bracket annotation: (define [attr...] ...)
    if (self.lexer.current.type == T_LBRACKET) {
        self.lexer.advance();  // consume '['
        // Read all symbols until ']'
        SymbolId[8] attrs;
        usz attr_count = 0;
        while (self.lexer.current.type == T_SYMBOL && attr_count < 8) {
            attrs[attr_count] = self.get_current_symbol();
            attr_count++;
            self.lexer.advance();
        }
        if (attr_count == 0) { self.set_error("expected attribute after ["); return null; }
        self.expect(T_RBRACKET, "]");

        SymbolId annotation = attrs[0];

        if (annotation == self.interp.sym_macro) {
            // [macro]: Parse macro definition
            if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected macro name"); return null; }
            SymbolId name = self.get_current_symbol();
            self.lexer.advance();

            e.tag = E_DEFMACRO;
            e.define_macro.name = name;
            e.define_macro.clause_count = 0;

            while (self.lexer.current.type == T_LPAREN && !self.has_error) {
                if (e.define_macro.clause_count >= 8) { self.set_error("too many macro clauses"); return null; }
                self.lexer.advance();
                Pattern* pat = self.parse_pattern();
                Value* tmpl = self.parse_template_datum();
                self.expect(T_RPAREN, ")");
                e.define_macro.clauses[e.define_macro.clause_count].pattern = pat;
                e.define_macro.clauses[e.define_macro.clause_count].tmpl = tmpl;
                e.define_macro.clause_count++;
            }
            self.expect(T_RPAREN, ")");
            return e;
        }

        if (annotation == self.interp.sym_type) {
            return self.parse_deftype(e);
        }
        if (annotation == self.interp.sym_abstract) {
            return self.parse_defabstract(e);
        }
        if (annotation == self.interp.sym_union) {
            return self.parse_defunion(e);
        }
        if (annotation == self.interp.sym_alias) {
            return self.parse_defalias(e);
        }
        if (annotation == self.interp.sym_effect) {
            return self.parse_defeffect(e);
        }
        if (annotation == self.interp.sym_ffi) {
            // [ffi ...] — FFI declarations
            if (attr_count < 2) { self.set_error("expected 'lib' or 'λ'/'lambda' after [ffi"); return null; }
            if (attrs[1] == self.interp.sym_lib) {
                return self.parse_ffi_lib(e);
            }
            if (attrs[1] == self.interp.sym_lambda) {
                return self.parse_ffi_fn(e, &attrs, attr_count);
            }
            // Check for λ (U+03BB, UTF-8: CE BB)
            char[] attr1_text = self.interp.symbols.get_name(attrs[1]);
            if (attr1_text.len == 2 && attr1_text[0] == (char)0xCE && attr1_text[1] == (char)0xBB) {
                return self.parse_ffi_fn(e, &attrs, attr_count);
            }
            self.set_error("unknown [ffi ...] form: expected 'lib' or 'λ'");
            return null;
        }
        // [relation db ...]: Relation definition
        SymbolId sym_relation = self.interp.symbols.intern("relation");
        if (annotation == sym_relation) {
            // attrs[1..] contains: db name, optional key/index/history/schema
            if (attr_count < 2) { self.set_error("expected db after [relation"); return null; }
            SymbolId db_name = attrs[1];

            // Collect config attrs (key, index, history, schema) from attrs[2..]
            SymbolId key_col = 0;
            SymbolId index_col = 0;
            SymbolId sym_key = self.interp.symbols.intern("key");
            SymbolId sym_index = self.interp.symbols.intern("index");
            for (usz i = 2; i < attr_count; i++) {
                if ((uint)attrs[i] == (uint)sym_key && i + 1 < attr_count) {
                    key_col = attrs[++i];
                } else if ((uint)attrs[i] == (uint)sym_index && i + 1 < attr_count) {
                    index_col = attrs[++i];
                }
            }

            // Read relation name
            if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected relation name"); return null; }
            SymbolId rel_name = self.get_current_symbol();
            self.lexer.advance();

            // Read column definitions: (^Type name), (key ^Type name), or (name)
            SymbolId[16] col_names;
            usz col_count = 0;
            while (self.lexer.current.type == T_LPAREN && !self.has_error && col_count < 16) {
                self.lexer.advance();  // consume (

                // Check for role prefix: key, index
                if (self.lexer.current.type == T_SYMBOL) {
                    SymbolId first = self.get_current_symbol();
                    if ((uint)first == (uint)sym_key || (uint)first == (uint)sym_index) {
                        if ((uint)first == (uint)sym_key) { /* role = key, skip */ }
                        if ((uint)first == (uint)sym_index) { /* role = index, skip */ }
                        self.lexer.advance();
                    }
                }

                // Skip type annotation if present
                if (self.lexer.current.type == T_SYMBOL && self.lexer.current.text[0] == '^') {
                    self.lexer.advance();  // skip ^Type
                }

                // Column name
                if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected column name"); return null; }
                col_names[col_count] = self.get_current_symbol();
                col_count++;
                self.lexer.advance();

                self.expect(T_RPAREN, ")");
            }

            self.expect(T_RPAREN, ")");  // close define

            // Build runtime call: (__define-relation 'db-name 'rel-name 'col1 'col2 ...)
            usz total_args = 2 + col_count;  // __define-relation + db-name + rel-name + cols
            Expr** call_args = (Expr**)mem::malloc(Expr*.sizeof * (total_args + 1));

            // Function: __define-relation
            call_args[0] = self.interp.alloc_expr();
            call_args[0].tag = E_VAR;
            call_args[0].var_expr.name = self.interp.symbols.intern("__define-relation");

            // Arg 1: db symbol
            call_args[1] = self.interp.alloc_expr();
            call_args[1].tag = E_VAR;
            call_args[1].var_expr.name = db_name;

            // Arg 2: quoted relation name
            call_args[2] = self.interp.alloc_expr();
            call_args[2].tag = E_LIT;
            call_args[2].lit.value = self.interp.alloc_value_root();
            call_args[2].lit.value.tag = SYMBOL;
            call_args[2].lit.value.sym_val = rel_name;

            // Args 3+: quoted column names
            for (usz i = 0; i < col_count; i++) {
                call_args[3 + i] = self.interp.alloc_expr();
                call_args[3 + i].tag = E_LIT;
                call_args[3 + i].lit.value = self.interp.alloc_value_root();
                call_args[3 + i].lit.value.tag = SYMBOL;
                call_args[3 + i].lit.value.sym_val = col_names[i];
            }

            // Build E_CALL + E_DEFINE to bind the result
            Expr* call = self.interp.alloc_expr();
            call.tag = E_CALL;
            call.call.args = call_args;
            call.call.arg_count = total_args + 1;

            e.tag = E_DEFINE;
            e.define.name = rel_name;
            e.define.value = call;
            return e;
        }

        // [schema]: Schema definition — stores body datum under name
        SymbolId sym_schema = self.interp.symbols.intern("schema");
        if (annotation == sym_schema) {
            if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected schema name"); return null; }
            SymbolId name = self.get_current_symbol();
            self.lexer.advance();
            // Parse body as a datum (quoted data) — the schema definition
            Value* body = self.parse_datum();
            self.expect(T_RPAREN, ")");
            // Desugar to: (define name (quote body))
            e.tag = E_DEFINE;
            e.define.name = name;
            Expr* quote_expr = self.interp.alloc_expr();
            quote_expr.tag = E_QUOTE;
            quote_expr.quote.datum = body;
            e.define.value = quote_expr;
            return e;
        }
        self.set_error("unknown define attribute");
        return null;
    }

    // Shorthand define: (define (name params...) body) => (define name (lambda (params...) body))
    // Also supports typed params: (define (name (^Int x) (^String y)) body)
    if (self.lexer.current.type == T_LPAREN) {
        self.lexer.advance();  // consume '('

        // Read function name
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected function name in shorthand define"); return null; }
        SymbolId name = self.get_current_symbol();
        self.lexer.advance();

        // Collect parameter symbols (with optional type annotations) until ')' or '..'
        List{SymbolId} params;
        List{TypeAnnotation} define_param_anns;
        bool define_has_typed = false;
        bool define_has_rest = false;
        SymbolId define_rest_param = 0;
        while (self.lexer.current.type == T_SYMBOL || self.lexer.current.type == T_LPAREN) {
            if (self.lexer.current.type == T_LPAREN) {
                // Typed param: (^Type name)
                self.lexer.advance();
                TypeAnnotation ann = self.parse_type_annotation();
                if (!ann.has_annotation) { params.free(); define_param_anns.free(); self.set_error("expected type annotation in typed parameter"); return null; }
                if (self.lexer.current.type != T_SYMBOL) { params.free(); define_param_anns.free(); self.set_error("expected parameter name after type annotation"); return null; }
                params.push(self.get_current_symbol());
                define_param_anns.push(ann);
                define_has_typed = true;
                self.lexer.advance();
                self.expect(T_RPAREN, ")");
            } else {
                params.push(self.get_current_symbol());
                TypeAnnotation empty_ann;
                empty_ann.has_annotation = false;
                define_param_anns.push(empty_ann);
                self.lexer.advance();
            }
            // Check for variadic '..' rest param
            if (self.lexer.current.type == T_DOTDOT) {
                break;
            }
        }

        usz param_count = params.len();

        // Handle variadic: (define (f x .. rest) body)
        if (self.lexer.current.type == T_DOTDOT) {
            self.lexer.advance();  // consume '..'
            if (self.lexer.current.type != T_SYMBOL) { params.free(); define_param_anns.free(); self.set_error("expected rest parameter name after .."); return null; }
            define_rest_param = self.get_current_symbol();
            define_has_rest = true;
            self.lexer.advance();
        }

        self.expect(T_RPAREN, ")");  // close param list

        // Parse body (implicit begin for multiple expressions)
        Expr* body = self.parse_implicit_begin();

        // Consume closing ')' of the define form
        self.expect(T_RPAREN, ")");

        // Build lambda expression
        Expr* lambda_expr = self.interp.alloc_expr();
        lambda_expr.tag = E_LAMBDA;
        lambda_expr.lambda = mem::malloc(ExprLambda.sizeof);
        lambda_expr.loc_line = e.loc_line;
        lambda_expr.loc_column = e.loc_column;
        lambda_expr.lambda.param = param_count > 0 ? params[0] : (SymbolId)0xFFFFFFFF;
        lambda_expr.lambda.param_count = param_count;
        if (param_count > 0) {
            lambda_expr.lambda.params = (SymbolId*)mem::malloc(SymbolId.sizeof * param_count);
            for (usz i = 0; i < param_count; i++) { lambda_expr.lambda.params[i] = params[i]; }
        } else {
            lambda_expr.lambda.params = null;
        }
        lambda_expr.lambda.has_rest = define_has_rest;
        lambda_expr.lambda.rest_param = define_rest_param;
        lambda_expr.lambda.has_typed_params = define_has_typed;
        if (define_has_typed) {
            lambda_expr.lambda.param_annotations = (TypeAnnotation*)mem::malloc(TypeAnnotation.sizeof * param_count);
            for (usz i = 0; i < param_count; i++) { lambda_expr.lambda.param_annotations[i] = define_param_anns[i]; }
        } else {
            lambda_expr.lambda.param_annotations = null;
        }
        lambda_expr.lambda.body = body;
        params.free(); define_param_anns.free();

        e.tag = E_DEFINE;
        e.define.name = name;
        e.define.value = lambda_expr;
        return e;
    }

    // Normal define: (define name value)
    if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected name after define"); return null; }
    SymbolId name = self.get_current_symbol();
    self.lexer.advance();

    Expr* value = self.parse_expr();

    self.expect(T_RPAREN, ")");

    e.tag = E_DEFINE;
    e.define.name = name;
    e.define.value = value;
    return e;
}

/**
 * Parse type definition: (define [type] Name (^Type field) ...)
 * or with parent: (define [type] (Name Parent) (^Type field) ...)
 */
fn Expr* Parser.parse_deftype(Parser* self, Expr* e) {
    e.tag = E_DEFTYPE;
    e.deftype = mem::malloc(ExprDefType.sizeof);
    e.deftype.has_parent = false;
    e.deftype.field_count = 0;
    e.deftype.type_param_count = 0;
    e.deftype.parent = 0;

    // Name can be simple: Name or parenthesized: (Name Parent?) or (Name TypeParam...)
    // All extra symbols collected as type_params — disambiguate parent vs type-param at eval time
    if (self.lexer.current.type == T_LPAREN) {
        self.lexer.advance();  // consume (
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected type name"); return null; }
        e.deftype.name = self.get_current_symbol();
        self.lexer.advance();
        // Collect all extra symbols (could be parent OR type params)
        while (self.lexer.current.type == T_SYMBOL && e.deftype.type_param_count < MAX_TYPE_PARAMS) {
            e.deftype.type_params[e.deftype.type_param_count] = self.get_current_symbol();
            e.deftype.type_param_count++;
            self.lexer.advance();
        }
        self.expect(T_RPAREN, ")");
    } else if (self.lexer.current.type == T_SYMBOL) {
        e.deftype.name = self.get_current_symbol();
        self.lexer.advance();
    } else {
        self.set_error("expected type name after [type]");
        return null;
    }

    // Parse fields: (^Type field) or just field
    while (self.lexer.current.type == T_LPAREN && !self.has_error) {
        if (e.deftype.field_count >= MAX_TYPE_FIELDS) { self.set_error("too many type fields"); return null; }
        self.lexer.advance();  // consume (
        TypeAnnotation ann = self.parse_type_annotation();
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected field name"); return null; }
        e.deftype.fields[e.deftype.field_count].name = self.get_current_symbol();
        e.deftype.fields[e.deftype.field_count].type_ann = ann;
        e.deftype.field_count++;
        self.lexer.advance();
        self.expect(T_RPAREN, ")");
    }

    // Also allow bare symbol fields (untyped)
    while (self.lexer.current.type == T_SYMBOL && !self.has_error) {
        if (e.deftype.field_count >= MAX_TYPE_FIELDS) { self.set_error("too many type fields"); return null; }
        e.deftype.fields[e.deftype.field_count].name = self.get_current_symbol();
        e.deftype.fields[e.deftype.field_count].type_ann.has_annotation = false;
        e.deftype.field_count++;
        self.lexer.advance();
    }

    self.expect(T_RPAREN, ")");  // close define
    return e;
}

/**
 * Parse abstract type: (define [abstract] Name) or (define [abstract] (Name Parent))
 */
fn Expr* Parser.parse_defabstract(Parser* self, Expr* e) {
    e.tag = E_DEFABSTRACT;
    e.defabstract.has_parent = false;
    e.defabstract.parent = 0;

    if (self.lexer.current.type == T_LPAREN) {
        self.lexer.advance();
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected type name"); return null; }
        e.defabstract.name = self.get_current_symbol();
        self.lexer.advance();
        if (self.lexer.current.type == T_SYMBOL) {
            e.defabstract.parent = self.get_current_symbol();
            e.defabstract.has_parent = true;
            self.lexer.advance();
        }
        self.expect(T_RPAREN, ")");
    } else if (self.lexer.current.type == T_SYMBOL) {
        e.defabstract.name = self.get_current_symbol();
        self.lexer.advance();
    } else {
        self.set_error("expected type name after [abstract]");
        return null;
    }

    self.expect(T_RPAREN, ")");  // close define
    return e;
}

/**
 * Parse union type: (define [union] (Name T...) Variant1 (Variant2 fields...) ...)
 * or simple: (define [union] Name Variant1 (Variant2 T) ...)
 */
fn Expr* Parser.parse_defunion(Parser* self, Expr* e) {
    e.tag = E_DEFUNION;
    e.defunion = mem::malloc(ExprDefUnion.sizeof);
    e.defunion.type_param_count = 0;
    e.defunion.variant_count = 0;

    // Name can be simple or parameterized: (Option T)
    if (self.lexer.current.type == T_LPAREN) {
        self.lexer.advance();
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected union name"); return null; }
        e.defunion.name = self.get_current_symbol();
        self.lexer.advance();
        // Collect type params
        while (self.lexer.current.type == T_SYMBOL && e.defunion.type_param_count < MAX_TYPE_PARAMS) {
            e.defunion.type_params[e.defunion.type_param_count] = self.get_current_symbol();
            e.defunion.type_param_count++;
            self.lexer.advance();
        }
        self.expect(T_RPAREN, ")");
    } else if (self.lexer.current.type == T_SYMBOL) {
        e.defunion.name = self.get_current_symbol();
        self.lexer.advance();
    } else {
        self.set_error("expected union name after [union]");
        return null;
    }

    // Parse variants: either bare symbols (None) or (VariantName fields...)
    while ((self.lexer.current.type == T_SYMBOL || self.lexer.current.type == T_LPAREN) && !self.has_error) {
        if (e.defunion.variant_count >= 16) { self.set_error("too many union variants"); return null; }
        UnionVariant* v = &e.defunion.variants[e.defunion.variant_count];
        v.field_count = 0;

        if (self.lexer.current.type == T_LPAREN) {
            self.lexer.advance();
            if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected variant name"); return null; }
            v.name = self.get_current_symbol();
            self.lexer.advance();
            // Collect field type params
            while (self.lexer.current.type == T_SYMBOL && v.field_count < MAX_TYPE_PARAMS) {
                v.fields[v.field_count] = self.get_current_symbol();
                v.field_count++;
                self.lexer.advance();
            }
            self.expect(T_RPAREN, ")");
        } else {
            // Bare variant: None, True, etc.
            v.name = self.get_current_symbol();
            self.lexer.advance();
        }
        e.defunion.variant_count++;
    }

    self.expect(T_RPAREN, ")");  // close define
    return e;
}

/**
 * Parse type alias: (define [alias] Name ^Target)
 */
fn Expr* Parser.parse_defalias(Parser* self, Expr* e) {
    e.tag = E_DEFALIAS;

    if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected alias name after [alias]"); return null; }
    e.defalias.name = self.get_current_symbol();
    self.lexer.advance();

    // Parse target type annotation or symbol
    if (self.lexer.current.type == T_SYMBOL && self.lexer.current.text[0] == '^') {
        e.defalias.target = self.parse_type_annotation();
    } else if (self.lexer.current.type == T_SYMBOL) {
        // Plain symbol target
        e.defalias.target.has_annotation = true;
        e.defalias.target.is_compound = false;
        e.defalias.target.is_dict = false;
        e.defalias.target.base_type = self.get_current_symbol();
        e.defalias.target.param_count = 0;
        e.defalias.target.meta_count = 0;
        e.defalias.target.has_val_literal = false;
        self.lexer.advance();
    } else {
        self.set_error("expected target type after alias name");
        return null;
    }

    self.expect(T_RPAREN, ")");
    return e;
}

fn Expr* Parser.parse_defeffect(Parser* self, Expr* e) {
    e.tag = E_DEFEFFECT;
    e.defeffect.has_arg_type = false;

    // Expect ( for parenthesized form: (define [effect] (tag-name (^Type arg)))
    if (self.lexer.current.type != T_LPAREN) {
        self.set_error("expected ( after [effect]");
        return null;
    }
    self.lexer.advance();  // consume '('

    // Read tag name symbol
    if (self.lexer.current.type != T_SYMBOL) {
        self.set_error("expected effect tag name");
        return null;
    }
    e.defeffect.name = self.get_current_symbol();
    self.lexer.advance();

    // Check for typed param: (^Type argname)
    if (self.lexer.current.type == T_LPAREN) {
        self.lexer.advance();  // consume '('
        if (self.lexer.current.type == T_SYMBOL && self.lexer.current.text[0] == '^') {
            e.defeffect.arg_type = self.parse_type_annotation();
            e.defeffect.has_arg_type = true;
            // Consume arg name if present
            if (self.lexer.current.type == T_SYMBOL) {
                self.lexer.advance();
            }
        }
        self.expect(T_RPAREN, ")");  // close param
    }

    self.expect(T_RPAREN, ")");  // close effect form
    self.expect(T_RPAREN, ")");  // close define
    return e;
}

// (define [ffi lib] name "path.so")
fn Expr* Parser.parse_ffi_lib(Parser* self, Expr* e) {
    e.tag = E_FFI_LIB;

    // Read library name symbol
    if (self.lexer.current.type != T_SYMBOL) {
        self.set_error("expected library name after [ffi lib]");
        return null;
    }
    e.ffi_lib.name = self.get_current_symbol();
    self.lexer.advance();

    // Read path expression (string literal)
    e.ffi_lib.path_expr = self.parse_expr();
    if (e.ffi_lib.path_expr == null) {
        self.set_error("expected library path string");
        return null;
    }

    self.expect(T_RPAREN, ")");  // close define
    return e;
}

// (define [ffi λ libname] (fname (^Type param)...) ^RetType)
fn Expr* Parser.parse_ffi_fn(Parser* self, Expr* e, SymbolId[8]* attrs, usz attr_count) {
    // attrs[0] = ffi, attrs[1] = λ/lambda, attrs[2] = library name
    if (attr_count < 3) {
        self.set_error("expected library name: [ffi λ libname]");
        return null;
    }

    // Heap-allocate ExprFfiFn (large struct)
    ExprFfiFn* ffi = (ExprFfiFn*)mem::malloc(ExprFfiFn.sizeof);
    e.tag = E_FFI_FN;
    e.ffi_fn = ffi;

    ffi.lib_name = (*attrs)[2];
    ffi.is_variadic = false;
    ffi.has_return_type = false;
    ffi.param_count = 0;

    // Check for modifier attributes in attrs[3..]
    for (usz i = 3; i < attr_count; i++) {
        char[] attr_name = self.interp.symbols.get_name((*attrs)[i]);
        if (attr_name.len == 8) {
            // "variadic" check
            bool is_var = true;
            char[] vstr = "variadic";
            for (usz j = 0; j < 8; j++) {
                if (attr_name[j] != vstr[j]) { is_var = false; break; }
            }
            if (is_var) ffi.is_variadic = true;
        }
    }

    // Expect ( for function signature
    if (self.lexer.current.type != T_LPAREN) {
        self.set_error("expected ( for FFI function signature");
        return null;
    }
    self.lexer.advance();  // consume '('

    // Read function name
    if (self.lexer.current.type != T_SYMBOL) {
        self.set_error("expected function name in FFI signature");
        return null;
    }
    ffi.fn_name = self.get_current_symbol();

    // Store original C name (the symbol text, before any kebab conversion)
    char[] sym_text = self.interp.symbols.get_name(ffi.fn_name);
    usz clen = sym_text.len;
    if (clen > 127) clen = 127;
    for (usz i = 0; i < clen; i++) {
        // Convert kebab-case to underscore for dlsym
        if (sym_text[i] == '-') {
            ffi.c_name[i] = '_';
        } else {
            ffi.c_name[i] = sym_text[i];
        }
    }
    ffi.c_name[clen] = 0;
    ffi.c_name_len = clen;
    self.lexer.advance();

    // Parse typed parameters: (^Type name) ...
    while (self.lexer.current.type == T_LPAREN && !self.has_error) {
        if (ffi.param_count >= 16) { self.set_error("too many FFI parameters (max 16)"); return null; }
        self.lexer.advance();  // consume '('

        TypeAnnotation ann = self.parse_type_annotation();
        if (!ann.has_annotation) {
            self.set_error("expected type annotation in FFI parameter");
            return null;
        }
        ffi.param_types[ffi.param_count] = ann;

        if (self.lexer.current.type != T_SYMBOL) {
            self.set_error("expected parameter name after type annotation");
            return null;
        }
        ffi.param_names[ffi.param_count] = self.get_current_symbol();
        self.lexer.advance();
        self.expect(T_RPAREN, ")");  // close param
        ffi.param_count++;
    }

    self.expect(T_RPAREN, ")");  // close function signature

    // Parse optional return type annotation: ^RetType
    if (self.lexer.current.type == T_SYMBOL && self.lexer.current.text[0] == '^') {
        ffi.return_type = self.parse_type_annotation();
        ffi.has_return_type = ffi.return_type.has_annotation;
    }

    self.expect(T_RPAREN, ")");  // close define
    return e;
}

fn Expr* Parser.parse_quote(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'quote' location
    self.lexer.advance();  // consume 'quote'

    Value* datum = self.parse_datum();

    self.expect(T_RPAREN, ")");

    e.tag = E_QUOTE;
    e.quote.datum = datum;
    return e;
}

fn Expr* Parser.parse_reset(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'reset' location
    self.lexer.advance();  // consume 'reset'

    Expr* body = self.parse_expr();

    self.expect(T_RPAREN, ")");

    e.tag = E_RESET;
    e.reset.body = body;
    return e;
}

fn Expr* Parser.parse_shift(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'shift' location
    self.lexer.advance();  // consume 'shift'

    if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected continuation name"); return null; }
    SymbolId k_name = self.get_current_symbol();
    self.lexer.advance();

    Expr* body = self.parse_expr();

    self.expect(T_RPAREN, ")");

    e.tag = E_SHIFT;
    e.shift.k_name = k_name;
    e.shift.body = body;
    return e;
}

fn Expr* Parser.parse_perform(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'perform' location
    self.lexer.advance();  // consume 'perform'

    if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected effect tag"); return null; }
    SymbolId etag = self.get_current_symbol();
    self.lexer.advance();

    Expr* arg = self.parse_expr();

    self.expect(T_RPAREN, ")");

    e.tag = E_PERFORM;
    e.perform.tag = etag;
    e.perform.arg = arg;
    return e;
}

fn Expr* Parser.parse_resolve(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();
    self.lexer.advance();  // consume 'resolve'

    Expr* val = self.parse_expr();

    self.expect(T_RPAREN, ")");

    e.tag = E_RESOLVE;
    e.resolve.value = val;
    return e;
}

// (with-continuation k body...) → (let ((k __k)) body...)
fn Expr* Parser.parse_with_continuation(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();
    self.lexer.advance();  // consume 'with-continuation'

    if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected continuation name"); return null; }
    SymbolId k_name = self.get_current_symbol();
    self.lexer.advance();

    // Parse body expressions (implicit begin)
    Expr* body = self.parse_implicit_begin();
    self.expect(T_RPAREN, ")");

    // Desugar to (let ((k __k)) body)
    e.tag = E_LET;
    e.let_expr.name = k_name;
    Expr* var_ref = self.interp.alloc_expr();
    var_ref.tag = E_VAR;
    var_ref.var_expr.name = self.interp.sym__k;
    e.let_expr.init = var_ref;
    e.let_expr.body = body;
    e.let_expr.is_recursive = false;
    return e;
}

fn Expr* Parser.parse_handle(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'handle' location
    self.lexer.advance();  // consume 'handle'

    // Check for ^strict annotation
    bool strict = false;
    if (self.lexer.current.type == T_SYMBOL) {
        if (self.lexer.current.text_len == 7 &&
            self.lexer.current.text[0] == '^' && self.lexer.current.text[1] == 's' &&
            self.lexer.current.text[2] == 't' && self.lexer.current.text[3] == 'r' &&
            self.lexer.current.text[4] == 'i' && self.lexer.current.text[5] == 'c' &&
            self.lexer.current.text[6] == 't') {
            strict = true;
            self.lexer.advance();  // consume ^strict
        }
    }

    Expr* body = self.parse_expr();

    e.tag = E_HANDLE;
    e.handle = (ExprHandle*)mem::malloc(ExprHandle.sizeof);
    e.handle.body = body;
    e.handle.strict_mode = strict;

    // Parse handler clauses — two syntaxes:
    //   Old: ((tag k arg) body)     — explicit continuation binding
    //   New: (tag arg body...)      — match-style, use `resolve` to resume
    List{EffectClause} clause_list;
    while (self.lexer.current.type == T_LPAREN && !self.has_error) {
        self.lexer.advance();  // consume outer (

        SymbolId etag;
        SymbolId k_name;
        SymbolId arg_name;
        Expr* handler_body;

        if (self.lexer.current.type == T_LPAREN) {
            // Old syntax: ((tag k arg) body)
            self.lexer.advance();  // consume inner (

            if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected effect tag"); return null; }
            etag = self.get_current_symbol();
            self.lexer.advance();

            if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected continuation name"); return null; }
            k_name = self.get_current_symbol();
            self.lexer.advance();

            if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected arg name"); return null; }
            arg_name = self.get_current_symbol();
            self.lexer.advance();

            self.expect(T_RPAREN, ")");

            handler_body = self.parse_expr();
        } else if (self.lexer.current.type == T_SYMBOL) {
            // New match-style: (tag arg body...)
            etag = self.get_current_symbol();
            self.lexer.advance();

            if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected arg name"); return null; }
            arg_name = self.get_current_symbol();
            self.lexer.advance();

            // Hidden continuation — `resolve` will look this up
            k_name = self.interp.sym__k;

            // Parse body (implicit begin for multiple expressions)
            handler_body = self.parse_implicit_begin();
        } else {
            self.set_error("expected effect tag or '('");
            return null;
        }

        self.expect(T_RPAREN, ")");

        clause_list.push({
            .effect_tag = etag,
            .k_name = k_name,
            .arg_name = arg_name,
            .handler_body = handler_body
        });
    }

    // Finalize clauses
    e.handle.clause_count = clause_list.len();
    e.handle.clauses = (EffectClause*)mem::malloc(EffectClause.sizeof * clause_list.len());
    for (usz i = 0; i < clause_list.len(); i++) { e.handle.clauses[i] = clause_list[i]; }
    clause_list.free();

    self.expect(T_RPAREN, ")");

    return e;
}

/**
 * Parse a pattern for match expressions.
 * Patterns:
 *   _             - wildcard (matches anything, binds nothing)
 *   x             - variable (matches anything, binds to x)
 *   42, "hello"   - literal
 *   'sym          - quoted symbol
 *   [a b c]       - exact sequence
 *   [head .. tail] - first element + rest
 *   [x y ..]      - first N, ignore rest
 *   [.. last]     - ignore all but last
 */
fn Pattern* Parser.parse_pattern(Parser* self) {
    if (self.has_error) return null;
    Lexer* lex = self.lexer;

    // Wildcard: _
    if (lex.current.type == T_UNDERSCORE) {
        Pattern* p = self.interp.alloc_pattern();
        p.tag = PAT_WILDCARD;
        lex.advance();
        return p;
    }

    // Quote: 'datum
    if (lex.current.type == T_QUOTE) {
        lex.advance();
        Pattern* p = self.interp.alloc_pattern();
        p.tag = PAT_QUOTE;
        p.quote_datum = self.parse_datum();
        return p;
    }

    // Integer literal
    if (lex.current.type == T_INT) {
        Pattern* p = self.interp.alloc_pattern();
        p.tag = PAT_LIT;
        p.lit_value = self.interp.alloc_value_root();
        p.lit_value.tag = INT;
        p.lit_value.int_val = lex.current.int_value;
        lex.advance();
        return p;
    }

    // String literal
    if (lex.current.type == T_STRING) {
        Pattern* p = self.interp.alloc_pattern();
        p.tag = PAT_LIT;
        p.lit_value = self.interp.alloc_value_root();
        p.lit_value.tag = STRING;
        usz len = lex.current.text_len;
        p.lit_value.str_val = strval_new(len);
        for (usz i = 0; i < len; i++) {
            p.lit_value.str_val.chars[i] = lex.current.text[i];
        }
        p.lit_value.str_val.chars[len] = 0;
        p.lit_value.str_val.len = len;
        lex.advance();
        return p;
    }

    // Sequence pattern: [a b c], [head .. tail], etc.
    if (lex.current.type == T_LBRACKET) {
        lex.advance();  // consume '['

        Pattern* p = self.interp.alloc_pattern();
        p.tag = PAT_SEQ;
        p.rest_pos = REST_NONE;
        p.rest_binding = 0;

        List{Pattern*} elems;

        // Check for [.. rest] pattern (REST_START)
        if (lex.current.type == T_DOTDOT) {
            lex.advance();  // consume '..'
            p.rest_pos = REST_START;

            // Parse elements after ..
            while (lex.current.type != T_RBRACKET && !self.has_error) {
                elems.push(self.parse_pattern());
            }
            if (self.has_error) { elems.free(); return null; }

            p.elem_count = elems.len();
            p.elements = (Pattern**)mem::malloc(Pattern*.sizeof * elems.len());
            for (usz i = 0; i < elems.len(); i++) { p.elements[i] = elems[i]; }
            elems.free();

            lex.advance();  // consume ']'
            return p;
        }

        // Parse elements until ] or ..
        while (lex.current.type != T_RBRACKET && lex.current.type != T_DOTDOT && !self.has_error) {
            elems.push(self.parse_pattern());
        }

        p.elem_count = elems.len();
        p.elements = (Pattern**)mem::malloc(Pattern*.sizeof * elems.len());
        for (usz i = 0; i < elems.len(); i++) { p.elements[i] = elems[i]; }
        elems.free();

        // Check for .. (rest pattern)
        if (lex.current.type == T_DOTDOT) {
            lex.advance();  // consume '..'

            if (lex.current.type == T_RBRACKET) {
                // [x y ..] - ignore rest
                p.rest_pos = REST_END;
            } else {
                // [head .. tail] - bind rest to variable
                p.rest_pos = REST_MIDDLE;
                if (lex.current.type != T_SYMBOL) { self.set_error("expected variable after .."); return null; }
                p.rest_binding = self.get_current_symbol();
                lex.advance();
            }
        }

        self.expect(T_RBRACKET, "]");
        return p;
    }

    // Constructor or guard pattern: (ConstructorName sub-patterns...) or (? pred [sub-pattern])
    if (lex.current.type == T_LPAREN) {
        lex.advance();  // consume '('
        if (lex.current.type == T_SYMBOL) {
            SymbolId sym = self.get_current_symbol();

            // Guard pattern: (? pred) or (? pred sub-pattern)
            if ((uint)sym == (uint)self.interp.sym_question) {
                lex.advance();  // consume '?'
                Pattern* p = self.interp.alloc_pattern();
                p.tag = PAT_GUARD;
                p.guard_pred = self.parse_expr();  // Parse predicate expression
                if (lex.current.type != T_RPAREN) {
                    p.guard_sub = self.parse_pattern();  // Optional sub-pattern
                } else {
                    p.guard_sub = null;  // No sub-pattern, just guard
                }
                self.expect(T_RPAREN, ")");
                return p;
            }

            // Constructor pattern
            Pattern* p = self.interp.alloc_pattern();
            p.tag = PAT_CONSTRUCTOR;
            p.constructor_name = sym;
            lex.advance();  // consume constructor name

            // Parse sub-patterns until )
            List{Pattern*} subs;
            while (lex.current.type != T_RPAREN && !self.has_error) {
                subs.push(self.parse_pattern());
            }
            p.ctor_sub_count = subs.len();
            p.ctor_sub_patterns = (Pattern**)mem::malloc(Pattern*.sizeof * subs.len());
            for (usz i = 0; i < subs.len(); i++) { p.ctor_sub_patterns[i] = subs[i]; }
            subs.free();
            self.expect(T_RPAREN, ")");
            return p;
        }
        self.set_error("expected constructor name or ? in pattern");
        return null;
    }

    // Variable pattern (symbol)
    if (lex.current.type == T_SYMBOL) {
        Pattern* p = self.interp.alloc_pattern();
        p.tag = PAT_VAR;
        p.var_name = self.get_current_symbol();
        lex.advance();
        return p;
    }

    self.set_error("invalid pattern syntax");
    return null;
}

/**
 * Parse a match expression.
 * (match expr (pattern1 result1) (pattern2 result2) ...)
 */
fn Expr* Parser.parse_match(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'match' location
    self.lexer.advance();  // consume 'match'

    // Parse the scrutinee (expression being matched)
    Expr* scrutinee = self.parse_expr();

    e.tag = E_MATCH;
    e.match = (ExprMatch*)mem::malloc(ExprMatch.sizeof);
    e.match.scrutinee = scrutinee;

    // Parse clauses: (pattern result)
    List{MatchClause} match_clauses;
    while (self.lexer.current.type == T_LPAREN && !self.has_error) {
        self.lexer.advance();  // consume '('

        Pattern* pattern = self.parse_pattern();
        Expr* result = self.parse_expr();
        self.expect(T_RPAREN, ")");

        match_clauses.push({ .pattern = pattern, .result = result });
    }

    // Finalize clauses
    e.match.clause_count = match_clauses.len();
    e.match.clauses = (MatchClause*)mem::malloc(MatchClause.sizeof * match_clauses.len());
    for (usz i = 0; i < match_clauses.len(); i++) { e.match.clauses[i] = match_clauses[i]; }
    match_clauses.free();

    self.expect(T_RPAREN, ")");

    return e;
}

/**
 * Parse an 'and' expression (short-circuit boolean and).
 * (and left right) - returns left if falsy, otherwise right
 */
fn Expr* Parser.parse_and(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'and' location
    self.lexer.advance();  // consume 'and'

    Expr* left = self.parse_expr();
    Expr* right = self.parse_expr();

    self.expect(T_RPAREN, ")");

    e.tag = E_AND;
    e.and_expr.left = left;
    e.and_expr.right = right;
    return e;
}

/**
 * Parse an 'or' expression (short-circuit boolean or).
 * (or left right) - returns left if truthy, otherwise right
 */
fn Expr* Parser.parse_or(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'or' location
    self.lexer.advance();  // consume 'or'

    Expr* left = self.parse_expr();
    Expr* right = self.parse_expr();

    self.expect(T_RPAREN, ")");

    e.tag = E_OR;
    e.or_expr.left = left;
    e.or_expr.right = right;
    return e;
}

/**
 * Parse a begin expression (sequencing).
 * (begin e1 e2 ... en) - evaluates all, returns last
 */
fn Expr* Parser.parse_begin(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'begin' location
    self.lexer.advance();  // consume 'begin'

    List{Expr*} items;
    while (self.lexer.current.type != T_RPAREN && !self.has_error) {
        items.push(self.parse_expr());
    }
    if (self.has_error) { items.free(); return null; }
    self.lexer.advance();  // consume ')'

    // (begin) with no expressions evaluates to nil
    if (items.len() == 0) {
        items.free();
        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value_root();
        e.lit.value.tag = NIL;
    } else {
        e.tag = E_BEGIN;
        e.begin = mem::malloc(ExprBegin.sizeof);
        e.begin.expr_count = items.len();
        e.begin.exprs = (Expr**)mem::malloc(Expr*.sizeof * items.len());
        for (usz i = 0; i < items.len(); i++) { e.begin.exprs[i] = items[i]; }
        items.free();
    }
    return e;
}

/**
 * Parse a set! expression (variable mutation).
 * (set! name value)
 */
fn Expr* Parser.parse_set(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();
    self.lexer.advance();  // consume 'set!'

    if (self.lexer.current.type == T_PATH) {
        // (set! obj.field value) — path mutation
        e.set_expr.is_path = true;
        e.set_expr.path_segment_count = 0;
        usz text_len = self.lexer.current.text_len;
        usz start = 0;
        for (usz i = 0; i <= text_len; i++) {
            if (i == text_len || self.lexer.current.text[i] == '.') {
                if (i > start) {
                    if (e.set_expr.path_segment_count >= MAX_PATH_SEGMENTS) { self.set_error("too many path segments in set!"); return null; }
                    usz seg_len = i - start;
                    SymbolId seg_sym = self.interp.symbols.intern(self.lexer.current.text[start:seg_len]);

                    e.set_expr.path_segments[e.set_expr.path_segment_count] = seg_sym;
                    e.set_expr.path_segment_count++;
                }
                start = i + 1;
            }
        }
        e.set_expr.name = e.set_expr.path_segments[0];
        self.lexer.advance();
    } else if (self.lexer.current.type == T_SYMBOL) {
        e.set_expr.is_path = false;
        e.set_expr.name = self.get_current_symbol();
        self.lexer.advance();
    } else {
        self.set_error("expected variable name or path after set!");
        return null;
    }

    Expr* value = self.parse_expr();

    self.expect(T_RPAREN, ")");

    e.tag = E_SET;
    e.set_expr.value = value;
    return e;
}

/**
 * Parse a template datum (for macro templates).
 * Like parse_datum() but also handles T_DOTDOT as a ".." symbol,
 * and T_UNDERSCORE as a "_" symbol.
 */
fn Value* Parser.parse_template_datum(Parser* self) {
    if (self.has_error) return null;
    Lexer* lex = self.lexer;

    // Handle ".." as a symbol in templates (splice marker)
    if (lex.current.type == T_DOTDOT) {
        lex.advance();
        Value* v = self.interp.alloc_value_root();
        v.tag = SYMBOL;
        v.sym_val = self.interp.sym_dotdot;
        return v;
    }

    // Handle "_" as a symbol in templates
    if (lex.current.type == T_UNDERSCORE) {
        lex.advance();
        SymbolId usym = self.interp.symbols.intern("_");
        Value* v = self.interp.alloc_value_root();
        v.tag = SYMBOL;
        v.sym_val = usym;
        return v;
    }

    if (lex.current.type == T_INT) {
        long val = lex.current.int_value;
        lex.advance();
        Value* v = self.interp.alloc_value_root();
        v.tag = INT;
        v.int_val = val;
        return v;
    }

    if (lex.current.type == T_SYMBOL) {
        SymbolId sym = self.get_current_symbol();
        lex.advance();
        Value* v = self.interp.alloc_value_root();
        v.tag = SYMBOL;
        v.sym_val = sym;
        return v;
    }

    if (lex.current.type == T_STRING) {
        Value* v = self.interp.alloc_value_root();
        v.tag = STRING;
        usz len = lex.current.text_len;
        v.str_val = strval_new(len);
        for (usz i = 0; i < len; i++) {
            v.str_val.chars[i] = lex.current.text[i];
        }
        v.str_val.chars[len] = 0;
        v.str_val.len = len;
        lex.advance();
        return v;
    }

    if (lex.current.type == T_QUOTE) {
        lex.advance();
        Value* inner = self.parse_template_datum();
        Value* quote_sym = self.interp.alloc_value_root();
        quote_sym.tag = SYMBOL;
        quote_sym.sym_val = self.interp.sym_quote;
        Value* rest = self.interp.alloc_value_root();
        rest.tag = CONS;
        rest.cons_val.car = inner;
        rest.cons_val.cdr = self.interp.alloc_value_root();
        rest.cons_val.cdr.tag = NIL;
        Value* v = self.interp.alloc_value_root();
        v.tag = CONS;
        v.cons_val.car = quote_sym;
        v.cons_val.cdr = rest;
        return v;
    }

    if (lex.current.type == T_LPAREN) {
        lex.advance();

        if (lex.current.type == T_RPAREN) {
            lex.advance();
            Value* v = self.interp.alloc_value_root();
            v.tag = NIL;
            return v;
        }

        // Parse list of template datums
        Value* first = self.parse_template_datum();
        Value* head = self.interp.alloc_value_root();
        head.tag = CONS;
        head.cons_val.car = first;
        head.cons_val.cdr = null;

        Value* tail = head;

        while (lex.current.type != T_RPAREN && !self.has_error) {
            Value* next = self.parse_template_datum();
            if (self.has_error) return null;
            Value* cell = self.interp.alloc_value_root();
            cell.tag = CONS;
            cell.cons_val.car = next;
            cell.cons_val.cdr = null;
            tail.cons_val.cdr = cell;
            tail = cell;
        }
        if (self.has_error) return null;

        Value* nil = self.interp.alloc_value_root();
        nil.tag = NIL;
        tail.cons_val.cdr = nil;

        lex.advance();  // consume ')'
        return head;
    }

    // Fallback: nil
    Value* v = self.interp.alloc_value_root();
    v.tag = NIL;
    return v;
}

/**
 * Parse module definition: (module name (export sym1 sym2 ...) body...)
 */
fn Expr* Parser.parse_module(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();
    self.lexer.advance();  // consume 'module'

    // Parse module name
    if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected module name after module"); return null; }
    SymbolId name = self.get_current_symbol();
    self.lexer.advance();

    e.tag = E_MODULE;
    e.module_expr = mem::malloc(ExprModule.sizeof);
    e.module_expr.name = name;
    e.module_expr.export_count = 0;
    e.module_expr.export_capacity = 32;
    e.module_expr.exports = (SymbolId*)mem::malloc(SymbolId.sizeof * 32);
    e.module_expr.body_count = 0;
    e.module_expr.body_capacity = 32;
    e.module_expr.body = (Expr**)mem::malloc(Expr*.sizeof * 32);

    // Parse (export sym1 sym2 ...) — required
    self.expect(T_LPAREN, "(");
    if (self.has_error) return null;
    if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected 'export' in module"); return null; }
    SymbolId maybe_export = self.get_current_symbol();
    if ((uint)maybe_export != (uint)self.interp.sym_export) { self.set_error("expected 'export' in module"); return null; }
    self.lexer.advance();  // consume 'export'
    while (self.lexer.current.type == T_SYMBOL) {
        if (e.module_expr.export_count >= e.module_expr.export_capacity) {
            usz new_cap = e.module_expr.export_capacity * 2;
            SymbolId* new_exports = (SymbolId*)mem::malloc(SymbolId.sizeof * new_cap);
            for (usz i = 0; i < e.module_expr.export_count; i++) new_exports[i] = e.module_expr.exports[i];
            mem::free(e.module_expr.exports);
            e.module_expr.exports = new_exports;
            e.module_expr.export_capacity = new_cap;
        }
        e.module_expr.exports[e.module_expr.export_count] = self.get_current_symbol();
        e.module_expr.export_count++;
        self.lexer.advance();
    }
    self.expect(T_RPAREN, ")");  // close export list

    // Parse remaining body expressions
    while (self.lexer.current.type != T_RPAREN && !self.lexer.at_end() && !self.has_error) {
        if (e.module_expr.body_count >= e.module_expr.body_capacity) {
            usz new_cap = e.module_expr.body_capacity * 2;
            Expr** new_body = (Expr**)mem::malloc(Expr*.sizeof * new_cap);
            for (usz i = 0; i < e.module_expr.body_count; i++) new_body[i] = e.module_expr.body[i];
            mem::free(e.module_expr.body);
            e.module_expr.body = new_body;
            e.module_expr.body_capacity = new_cap;
        }
        e.module_expr.body[e.module_expr.body_count] = self.parse_expr();
        e.module_expr.body_count++;
    }

    self.expect(T_RPAREN, ")");  // close module
    return e;
}

/**
 * Parse import: (import name), (import name (sym1 (sym2 :as alias))), (import name :all), (import "path")
 */
fn Expr* Parser.parse_import(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();
    self.lexer.advance();  // consume 'import'

    e.tag = E_IMPORT;
    e.import_expr.has_path = false;
    e.import_expr.path_len = 0;
    e.import_expr.import_count = 0;
    e.import_expr.import_capacity = 0;
    e.import_expr.imports = null;
    e.import_expr.aliases = null;
    e.import_expr.import_all = false;

    if (self.lexer.current.type == T_SYMBOL) {
        e.import_expr.name = self.get_current_symbol();
        self.lexer.advance();
    } else if (self.lexer.current.type == T_STRING) {
        // Import by file path
        e.import_expr.has_path = true;
        char[] str = self.lexer.current.text[:self.lexer.current.text_len];
        usz len = str.len;
        if (len > 255) len = 255;
        for (usz i = 0; i < len; i++) {
            e.import_expr.path[i] = str[i];
        }
        e.import_expr.path[len] = 0;
        e.import_expr.path_len = len;
        SymbolId import_sym = self.interp.symbols.intern(str);
        e.import_expr.name = import_sym;
        self.lexer.advance();
    } else {
        self.set_error("expected module name or path after import");
        return null;
    }

    // Check for optional specifiers before closing )
    // (import name :all) or (import name (sym1 (sym2 :as alias)))
    if (self.lexer.current.type == T_SYMBOL) {
        SymbolId maybe_all = self.get_current_symbol();
        if ((uint)maybe_all == (uint)self.interp.sym_all) {
            // (import name :all)
            e.import_expr.import_all = true;
            self.lexer.advance();
        }
    } else if (self.lexer.current.type == T_LPAREN) {
        // (import name (sym1 sym2 (sym3 :as alias3)))
        // Allocate initial arrays
        e.import_expr.import_capacity = 16;
        e.import_expr.imports = (SymbolId*)mem::malloc(SymbolId.sizeof * 16);
        e.import_expr.aliases = (SymbolId*)mem::malloc(SymbolId.sizeof * 16);
        self.lexer.advance();  // consume '('
        while (self.lexer.current.type != T_RPAREN && !self.lexer.at_end() && !self.has_error) {
            if (e.import_expr.import_count >= e.import_expr.import_capacity) {
                usz new_cap = e.import_expr.import_capacity * 2;
                SymbolId* new_imports = (SymbolId*)mem::malloc(SymbolId.sizeof * new_cap);
                SymbolId* new_aliases = (SymbolId*)mem::malloc(SymbolId.sizeof * new_cap);
                for (usz i = 0; i < e.import_expr.import_count; i++) {
                    new_imports[i] = e.import_expr.imports[i];
                    new_aliases[i] = e.import_expr.aliases[i];
                }
                mem::free(e.import_expr.imports);
                mem::free(e.import_expr.aliases);
                e.import_expr.imports = new_imports;
                e.import_expr.aliases = new_aliases;
                e.import_expr.import_capacity = new_cap;
            }
            usz idx = e.import_expr.import_count;

            if (self.lexer.current.type == T_LPAREN) {
                // (sym :as alias) form
                self.lexer.advance();  // consume '('
                if (self.lexer.current.type != T_SYMBOL) {
                    self.set_error("expected symbol in import specifier");
                    return null;
                }
                e.import_expr.imports[idx] = self.get_current_symbol();
                self.lexer.advance();
                // Expect :as
                if (self.lexer.current.type != T_SYMBOL) {
                    self.set_error("expected :as in import specifier");
                    return null;
                }
                SymbolId maybe_as = self.get_current_symbol();
                if ((uint)maybe_as != (uint)self.interp.sym_as) {
                    self.set_error("expected :as in import specifier");
                    return null;
                }
                self.lexer.advance();  // consume ':as'
                if (self.lexer.current.type != T_SYMBOL) {
                    self.set_error("expected alias after :as");
                    return null;
                }
                e.import_expr.aliases[idx] = self.get_current_symbol();
                self.lexer.advance();
                self.expect(T_RPAREN, ")");  // close (sym :as alias)
            } else if (self.lexer.current.type == T_SYMBOL) {
                // Plain symbol — check for inline :as
                e.import_expr.imports[idx] = self.get_current_symbol();
                e.import_expr.aliases[idx] = (SymbolId)0;
                self.lexer.advance();
                // Check for :as after symbol (without parens)
                if (self.lexer.current.type == T_SYMBOL) {
                    SymbolId maybe_as = self.get_current_symbol();
                    if ((uint)maybe_as == (uint)self.interp.sym_as) {
                        self.lexer.advance();  // consume ':as'
                        if (self.lexer.current.type != T_SYMBOL) {
                            self.set_error("expected alias after :as");
                            return null;
                        }
                        e.import_expr.aliases[idx] = self.get_current_symbol();
                        self.lexer.advance();
                    }
                }
            } else {
                self.set_error("expected symbol in import list");
                return null;
            }

            e.import_expr.import_count++;
        }
        self.expect(T_RPAREN, ")");  // close selective list
    }

    self.expect(T_RPAREN, ")");  // close import
    return e;
}

/**
 * Parse export-from: (export-from mod (sym1 sym2)) or (export-from mod :all)
 */
fn Expr* Parser.parse_export_from(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();
    self.lexer.advance();  // consume 'export-from'

    e.tag = E_EXPORT_FROM;
    e.export_from.name_count = 0;
    e.export_from.name_capacity = 0;
    e.export_from.names = null;
    e.export_from.all = false;

    // Parse source module name
    if (self.lexer.current.type != T_SYMBOL) {
        self.set_error("expected module name after export-from");
        return null;
    }
    e.export_from.source_module = self.get_current_symbol();
    self.lexer.advance();

    // Parse specifiers: :all or (sym1 sym2 ...)
    if (self.lexer.current.type == T_SYMBOL) {
        SymbolId maybe_all = self.get_current_symbol();
        if ((uint)maybe_all == (uint)self.interp.sym_all) {
            e.export_from.all = true;
            self.lexer.advance();
        } else {
            self.set_error("expected :all or (names...) after export-from module");
            return null;
        }
    } else if (self.lexer.current.type == T_LPAREN) {
        e.export_from.name_capacity = 16;
        e.export_from.names = (SymbolId*)mem::malloc(SymbolId.sizeof * 16);
        self.lexer.advance();  // consume '('
        while (self.lexer.current.type != T_RPAREN && !self.lexer.at_end() && !self.has_error) {
            if (e.export_from.name_count >= e.export_from.name_capacity) {
                usz new_cap = e.export_from.name_capacity * 2;
                SymbolId* new_names = (SymbolId*)mem::malloc(SymbolId.sizeof * new_cap);
                for (usz i = 0; i < e.export_from.name_count; i++) new_names[i] = e.export_from.names[i];
                mem::free(e.export_from.names);
                e.export_from.names = new_names;
                e.export_from.name_capacity = new_cap;
            }
            if (self.lexer.current.type != T_SYMBOL) {
                self.set_error("expected symbol in export-from list");
                return null;
            }
            e.export_from.names[e.export_from.name_count] = self.get_current_symbol();
            e.export_from.name_count++;
            self.lexer.advance();
        }
        self.expect(T_RPAREN, ")");  // close name list
    } else {
        self.set_error("expected :all or (names...) after export-from module");
        return null;
    }

    self.expect(T_RPAREN, ")");  // close export-from
    return e;
}

/**
 * Parse a quasiquote template.
 * Like parse_expr but treats list forms as generic E_CALL (no special form detection).
 * Handles ,expr and ,@expr as E_UNQUOTE/E_UNQUOTE_SPLICING.
 */
fn Expr* Parser.parse_qq_template(Parser* self) {
    if (self.has_error) return null;
    Lexer* lex = self.lexer;

    // Nested quasiquote
    if (lex.current.type == T_BACKTICK) {
        Expr* e = self.alloc_expr_here();
        lex.advance();
        e.tag = E_QUASIQUOTE;
        e.quasiquote.body = self.parse_qq_template();
        return e;
    }

    // Unquote: ,expr — parse the inner expr as regular code
    if (lex.current.type == T_COMMA) {
        Expr* e = self.alloc_expr_here();
        lex.advance();
        e.tag = E_UNQUOTE;
        e.unquote.body = self.parse_expr();
        return e;
    }

    // Unquote-splicing: ,@expr
    if (lex.current.type == T_COMMA_AT) {
        Expr* e = self.alloc_expr_here();
        lex.advance();
        e.tag = E_UNQUOTE_SPLICING;
        e.unquote_splicing.body = self.parse_expr();
        return e;
    }

    // Quote shorthand: 'x
    if (lex.current.type == T_QUOTE) {
        Expr* e = self.alloc_expr_here();
        lex.advance();
        e.tag = E_QUOTE;
        e.quote.datum = self.parse_datum();
        return e;
    }

    // Integer literal
    if (lex.current.type == T_INT) {
        Expr* e = self.alloc_expr_here();
        long val = lex.current.int_value;
        lex.advance();
        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value_root();
        e.lit.value.tag = INT;
        e.lit.value.int_val = val;
        return e;
    }

    // String literal
    if (lex.current.type == T_STRING) {
        Expr* e = self.alloc_expr_here();
        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value_root();
        e.lit.value.tag = STRING;
        usz len = lex.current.text_len;
        e.lit.value.str_val = strval_new(len);
        for (usz i = 0; i < len; i++) {
            e.lit.value.str_val.chars[i] = lex.current.text[i];
        }
        e.lit.value.str_val.chars[len] = 0;
        e.lit.value.str_val.len = len;
        lex.advance();
        return e;
    }

    // Symbol — always E_VAR (no special form detection)
    if (lex.current.type == T_SYMBOL) {
        Expr* e = self.alloc_expr_here();
        SymbolId sym = self.get_current_symbol();
        lex.advance();
        e.tag = E_VAR;
        e.var_expr.name = sym;
        return e;
    }

    // List form: (...) — always E_CALL (no special form detection)
    if (lex.current.type == T_LPAREN) {
        lex.advance();

        if (lex.current.type == T_RPAREN) {
            // Empty list () = nil literal
            Expr* e = self.alloc_expr_here();
            lex.advance();
            e.tag = E_LIT;
            e.lit.value = self.interp.alloc_value_root();
            e.lit.value.tag = NIL;
            return e;
        }

        // Parse as generic call: (head elem1 elem2 ...)
        Expr* func = self.parse_qq_template();
        List{Expr*} qq_args;
        while (lex.current.type != T_RPAREN && !self.has_error) {
            qq_args.push(self.parse_qq_template());
        }
        if (self.has_error) { qq_args.free(); return null; }
        lex.advance();  // consume ')'

        Expr* e = self.interp.alloc_expr();
        e.tag = E_CALL;
        e.call = mem::malloc(ExprCall.sizeof);
        e.call.func = func;
        e.call.arg_count = qq_args.len();
        e.call.args = (Expr**)mem::malloc(Expr*.sizeof * qq_args.len());
        for (usz i = 0; i < qq_args.len(); i++) { e.call.args[i] = qq_args[i]; }
        qq_args.free();
        return e;
    }

    // Underscore as symbol in templates
    if (lex.current.type == T_UNDERSCORE) {
        Expr* e = self.alloc_expr_here();
        SymbolId sym = self.interp.symbols.intern("_");
        lex.advance();
        e.tag = E_VAR;
        e.var_expr.name = sym;
        return e;
    }

    self.set_error("unexpected token in quasiquote template");
    return null;
}

/**
 * Parse pipe expression: (|> val step1 step2 ...)
 * Desugars to nested application: (step2 (step1 val))
 * Each step is applied as a function to the accumulated value.
 * Steps containing _ placeholders are already desugared to lambdas by parse_application.
 */
fn Expr* Parser.parse_pipe(Parser* self) {
    if (self.has_error) return null;
    Expr* pipe_loc = self.alloc_expr_here();
    self.lexer.advance();  // consume '|>'

    // Parse initial value
    Expr* acc = self.parse_expr();
    if (acc == null || self.has_error) return null;

    // Parse steps
    Expr*[64] steps;
    usz step_count = 0;
    while (self.lexer.current.type != T_RPAREN && !self.has_error) {
        if (step_count >= 64) { self.set_error("too many pipe steps"); return null; }
        steps[step_count] = self.parse_expr();
        step_count++;
    }
    if (self.has_error) return null;
    self.expect(T_RPAREN, ")");

    if (step_count == 0) return acc;

    // Build nested calls left-to-right: (step1 acc), then (step2 result), etc.
    for (usz i = 0; i < step_count; i++) {
        Expr* step = steps[i];
        // If step is an E_CALL, append acc as last arg
        if (step.tag == E_CALL) {
            // Grow args array by one
            usz new_count = step.call.arg_count + 1;
            Expr** new_args = (Expr**)mem::malloc(Expr*.sizeof * new_count);
            for (usz j = 0; j < step.call.arg_count; j++) { new_args[j] = step.call.args[j]; }
            new_args[step.call.arg_count] = acc;
            mem::free(step.call.args);
            step.call.args = new_args;
            step.call.arg_count = new_count;
            acc = step;
        } else {
            // Bare function: wrap as (step acc)
            Expr* call = self.interp.alloc_expr();
            call.tag = E_CALL;
            call.call = mem::malloc(ExprCall.sizeof);
            call.loc_line = pipe_loc.loc_line;
            call.loc_column = pipe_loc.loc_column;
            call.call.func = step;
            call.call.arg_count = 1;
            call.call.args = (Expr**)mem::malloc(Expr*.sizeof * 1);
            call.call.args[0] = acc;
            acc = call;
        }
    }
    return acc;
}

fn Expr* Parser.parse_application(Parser* self) {
    if (self.has_error) return null;
    // Already consumed '(' - capture func location for application
    usz app_line = self.lexer.current.line;
    usz app_col = self.lexer.current.column;
    Expr* func = self.parse_expr();

    // Collect arguments
    List{Expr*} arg_list;
    while (self.lexer.current.type != T_RPAREN && !self.has_error) {
        arg_list.push(self.parse_expr());
    }
    if (self.has_error) { arg_list.free(); return null; }
    self.lexer.advance();  // consume ')'

    Expr* e = self.interp.alloc_expr();
    e.tag = E_CALL;
    e.call = mem::malloc(ExprCall.sizeof);
    e.loc_line = app_line;
    e.loc_column = app_col;
    e.call.func = func;
    e.call.arg_count = arg_list.len();
    e.call.args = (Expr**)mem::malloc(Expr*.sizeof * arg_list.len());
    for (usz i = 0; i < arg_list.len(); i++) { e.call.args[i] = arg_list[i]; }
    arg_list.free();

    // Placeholder desugaring: scan args for __placeholder sentinels
    usz placeholder_count = 0;
    for (usz i = 0; i < e.call.arg_count; i++) {
        if (e.call.args[i] != null && e.call.args[i].tag == E_VAR &&
            (uint)e.call.args[i].var_expr.name == (uint)self.interp.sym_placeholder) {
            placeholder_count++;
        }
    }
    if (placeholder_count > 0) {
        // Generate unique param names for each placeholder, replace in-place
        SymbolId[16] param_syms;
        usz param_idx = 0;
        char[32] name_buf;
        for (usz i = 0; i < e.call.arg_count; i++) {
            if (e.call.args[i] != null && e.call.args[i].tag == E_VAR &&
                (uint)e.call.args[i].var_expr.name == (uint)self.interp.sym_placeholder) {
                // Generate __pN name using gensym counter
                self.interp.gensym_counter++;
                usz pos = 0;
                name_buf[pos++] = '_'; name_buf[pos++] = '_'; name_buf[pos++] = 'p';
                char[20] num_buf;
                char[] num_str = int_to_string((long)self.interp.gensym_counter, &num_buf);
                for (usz j = 0; j < num_str.len && pos < 31; j++) name_buf[pos++] = num_str[j];
                SymbolId psym = self.interp.symbols.intern(name_buf[:pos]);
                if (param_idx < 16) param_syms[param_idx] = psym;
                param_idx++;
                // Replace placeholder with E_VAR referencing the generated param
                e.call.args[i].var_expr.name = psym;
            }
        }
        // Wrap the E_CALL in nested single-param lambdas (outermost = first _)
        // Build from innermost to outermost
        Expr* body = e;
        usz wrap_count = param_idx < 16 ? param_idx : 16;
        for (usz i = wrap_count; i > 0; i--) {
            Expr* lam = self.interp.alloc_expr();
            lam.tag = E_LAMBDA;
            lam.lambda = mem::malloc(ExprLambda.sizeof);
            lam.loc_line = app_line;
            lam.loc_column = app_col;
            lam.lambda.param = param_syms[i - 1];
            lam.lambda.param_count = 1;
            lam.lambda.params = (SymbolId*)mem::malloc(SymbolId.sizeof * 1);
            lam.lambda.params[0] = param_syms[i - 1];
            lam.lambda.param_annotations = null;
            lam.lambda.has_rest = false;
            lam.lambda.rest_param = 0;
            lam.lambda.has_typed_params = false;
            lam.lambda.body = body;
            body = lam;
        }
        return self.parse_postfix_index(body);
    }

    // Check for postfix indexing: (make-array 10).[0]
    return self.parse_postfix_index(e);
}

/**
 * Parse a datum (for quote).
 */
fn Value* Parser.parse_datum(Parser* self) {
    if (self.has_error) return null;
    Lexer* lex = self.lexer;

    if (lex.current.type == T_INT) {
        long val = lex.current.int_value;
        lex.advance();
        Value* v = self.interp.alloc_value_root();
        v.tag = INT;
        v.int_val = val;
        return v;
    }

    if (lex.current.type == T_SYMBOL) {
        SymbolId sym = self.get_current_symbol();
        lex.advance();
        Value* v = self.interp.alloc_value_root();
        v.tag = SYMBOL;
        v.sym_val = sym;
        return v;
    }

    if (lex.current.type == T_STRING) {
        Value* v = self.interp.alloc_value_root();
        v.tag = STRING;
        usz len = lex.current.text_len;
        v.str_val = strval_new(len);
        for (usz i = 0; i < len; i++) {
            v.str_val.chars[i] = lex.current.text[i];
        }
        v.str_val.chars[len] = 0;
        v.str_val.len = len;
        lex.advance();
        return v;
    }

    if (lex.current.type == T_QUOTE) {
        lex.advance();
        Value* inner = self.parse_datum();
        // 'x => (quote . x) as a cons
        Value* quote_sym = self.interp.alloc_value_root();
        quote_sym.tag = SYMBOL;
        quote_sym.sym_val = self.interp.sym_quote;

        Value* rest = self.interp.alloc_value_root();
        rest.tag = CONS;
        rest.cons_val.car = inner;
        rest.cons_val.cdr = self.interp.alloc_value_root();
        rest.cons_val.cdr.tag = NIL;

        Value* v = self.interp.alloc_value_root();
        v.tag = CONS;
        v.cons_val.car = quote_sym;
        v.cons_val.cdr = rest;
        return v;
    }

    if (lex.current.type == T_LPAREN) {
        lex.advance();

        if (lex.current.type == T_RPAREN) {
            lex.advance();
            Value* v = self.interp.alloc_value_root();
            v.tag = NIL;
            return v;
        }

        // Parse list of datums
        Value* first = self.parse_datum();
        Value* head = self.interp.alloc_value_root();
        head.tag = CONS;
        head.cons_val.car = first;
        head.cons_val.cdr = null;

        Value* tail = head;

        while (lex.current.type != T_RPAREN && !self.has_error) {
            Value* next = self.parse_datum();
            if (self.has_error) return null;
            Value* cell = self.interp.alloc_value_root();
            cell.tag = CONS;
            cell.cons_val.car = next;
            cell.cons_val.cdr = null;
            tail.cons_val.cdr = cell;
            tail = cell;
        }
        if (self.has_error) return null;

        Value* nil = self.interp.alloc_value_root();
        nil.tag = NIL;
        tail.cons_val.cdr = nil;

        lex.advance();  // consume ')'
        return head;
    }

    Value* v = self.interp.alloc_value_root();
    v.tag = NIL;
    return v;
}

// =============================================================================
// SECTION 3: TOP-LEVEL PARSE
// =============================================================================

/**
 * Parse a string into an expression.
 */
fn Expr* parse(char[] source, Interp* interp) {
    Lexer lex;
    lex.init(source);

    Parser p;
    p.init(&lex, interp);

    return p.parse_expr();
}

/**
 * Parse multiple expressions (a program).
 */
fn List{Expr*} parse_program(char[] source, Interp* interp) {
    Lexer lex;
    lex.init(source);

    Parser p;
    p.init(&lex, interp);

    List{Expr*} exprs;
    while (!lex.at_end() && !p.has_error) {
        Expr* e = p.parse_expr();
        if (e != null) {
            exprs.push(e);
        }
    }

    return exprs;
}
