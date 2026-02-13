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
    T_STRING,       // "..." string literal
    T_SYMBOL,
    T_PATH,         // dot-separated path: point.x, person.address.city
    T_UNDERSCORE,   // _ wildcard pattern
    T_BACKTICK,     // ` quasiquote
    T_COMMA,        // , unquote
    T_COMMA_AT,     // ,@ unquote-splicing
    T_ERROR,
}

struct Token {
    TokenType type;
    char[64]  text;
    usz       text_len;
    long      int_value;  // For T_INT
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
            // Comment: skip to end of line
            while (self.pos < self.len && self.source[self.pos] != '\n') {
                self.next_char();
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

    // Integer (with optional leading minus)
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
                // Store error info in token text for diagnostics
                char[] err = "integer literal overflow";
                for (usz i = 0; i < err.len; i++) self.current.text[i] = err[i];
                self.current.text[err.len] = 0;
                self.current.text_len = err.len;
                return;
            }
            val = val * 10 + digit;
            self.next_char();
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
    // Pass the char slice directly as String (String = char[])
    SymbolId sym = self.interp.symbols.intern(self.lexer.current.text[:self.lexer.current.text_len]);
    if (sym == INVALID_SYMBOL_ID) { self.set_error("symbol table exhausted"); }
    return sym;
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
        e.lit.value = self.interp.alloc_value();
        e.lit.value.tag = INT;
        e.lit.value.int_val = val;
        return self.parse_postfix_index(e);
    }

    // String literal - may be followed by postfix .[index]
    if (lex.current.type == T_STRING) {
        Expr* e = self.alloc_expr_here();  // Capture location before consuming
        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value();
        e.lit.value.tag = STRING;
        // Copy string content from token
        usz len = lex.current.text_len;
        if (len > MAX_STRING_LEN - 1) len = MAX_STRING_LEN - 1;
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
                    if (seg_sym == INVALID_SYMBOL_ID) { self.set_error("symbol table exhausted"); return null; }
                    e.path.segments[e.path.segment_count] = seg_sym;
                    e.path.segment_count++;
                }
                start = i + 1;
            }
        }
        lex.advance();
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
            e.lit.value = self.interp.alloc_value();
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
            if ((uint)head == (uint)self.interp.sym_perform) {
                return self.parse_perform();
            }
            if ((uint)head == (uint)self.interp.sym_handle) {
                return self.parse_handle();
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
        }

        // Regular application
        return self.parse_application();
    }

    if (lex.current.type == T_EOF) {
        self.set_error("unexpected end of input");
    } else {
        self.set_error("unexpected token in expression");
    }
    return null;
}

fn Expr* Parser.parse_lambda(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'lambda' location
    self.lexer.advance();  // consume 'lambda'

    self.expect(T_LPAREN, "(");

    // Check for zero-arg lambda: (lambda () body)
    if (self.lexer.current.type == T_RPAREN) {
        self.lexer.advance();  // consume ')'

        // Parse body
        Expr* body = self.parse_expr();
        self.expect(T_RPAREN, ")");

        e.tag = E_LAMBDA;
        e.lambda.param = (SymbolId)0xFFFFFFFF;  // sentinel for "no param"
        e.lambda.param_count = 0;
        e.lambda.has_rest = false;
        e.lambda.rest_param = 0;
        e.lambda.body = body;
        return e;
    }

    // Check for (lambda (.. rest) body) - variadic with no fixed params
    if (self.lexer.current.type == T_DOTDOT) {
        self.lexer.advance();  // consume '..'
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected rest parameter name after .."); return null; }
        SymbolId rest_param = self.get_current_symbol();
        self.lexer.advance();
        self.expect(T_RPAREN, ")");

        Expr* body = self.parse_expr();
        self.expect(T_RPAREN, ")");
        if (self.has_error) return null;

        e.tag = E_LAMBDA;
        e.lambda.param = (SymbolId)0xFFFFFFFF;  // sentinel: no fixed params
        e.lambda.param_count = 0;
        e.lambda.has_rest = true;
        e.lambda.rest_param = rest_param;
        e.lambda.body = body;
        return e;
    }

    // Collect all fixed parameters until ')' or '..'
    SymbolId[64] params;
    usz param_count = 0;

    while (self.lexer.current.type == T_SYMBOL) {
        if (param_count >= 64) { self.set_error("too many lambda parameters"); return null; }
        params[param_count] = self.get_current_symbol();
        param_count++;
        self.lexer.advance();

        if (self.lexer.current.type == T_DOTDOT) {
            break;
        }
        if (self.lexer.current.type == T_RPAREN) {
            break;
        }
    }

    // Check for '..' rest parameter: (lambda (x y .. rest) body)
    if (self.lexer.current.type == T_DOTDOT) {
        self.lexer.advance();  // consume '..'
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected rest parameter name after .."); return null; }
        SymbolId rest_param = self.get_current_symbol();
        self.lexer.advance();
        self.expect(T_RPAREN, ")");

        Expr* body = self.parse_expr();
        self.expect(T_RPAREN, ")");

        e.tag = E_LAMBDA;
        e.lambda.param = params[0];
        e.lambda.param_count = param_count;
        for (usz i = 0; i < param_count; i++) {
            e.lambda.params[i] = params[i];
        }
        e.lambda.has_rest = true;
        e.lambda.rest_param = rest_param;
        e.lambda.body = body;
        return e;
    }

    self.expect(T_RPAREN, ")");

    // Parse body
    Expr* body = self.parse_expr();
    self.expect(T_RPAREN, ")");

    if (self.has_error) return null;
    if (param_count < 1) { self.set_error("expected at least one parameter"); return null; }

    if (param_count == 1) {
        // Single param - simple case
        e.tag = E_LAMBDA;
        e.lambda.param = params[0];
        e.lambda.param_count = 1;
        e.lambda.params[0] = params[0];
        e.lambda.has_rest = false;
        e.lambda.rest_param = 0;
        e.lambda.body = body;
        return e;
    }

    // Multi-param (no rest): desugar (lambda (x y z) body) => (lambda (x) (lambda (y) (lambda (z) body)))
    // Build from inside out
    Expr* inner = body;
    for (usz i = param_count; i > 1; i--) {
        Expr* wrapper = self.interp.alloc_expr();
        wrapper.tag = E_LAMBDA;
        wrapper.loc_line = e.loc_line;
        wrapper.loc_column = e.loc_column;
        wrapper.lambda.param = params[i - 1];
        wrapper.lambda.param_count = 1;
        wrapper.lambda.params[0] = params[i - 1];
        wrapper.lambda.has_rest = false;
        wrapper.lambda.rest_param = 0;
        wrapper.lambda.body = inner;
        inner = wrapper;
    }
    // Outermost lambda with first param
    e.tag = E_LAMBDA;
    e.lambda.param = params[0];
    e.lambda.param_count = 1;
    e.lambda.params[0] = params[0];
    e.lambda.has_rest = false;
    e.lambda.rest_param = 0;
    e.lambda.body = inner;
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

    // Check for named let: (let name ((var init) ...) body)
    // After consuming 'let' and optional '^rec', if next is a symbol
    // followed by '(', this is a named let (loop).
    if (self.lexer.current.type == T_SYMBOL) {
        // This is a named let — desugar to:
        // (let ^rec ((name (lambda (var ...) body))) (name init ...))
        SymbolId loop_name = self.get_current_symbol();
        self.lexer.advance();  // consume loop name

        return self.parse_named_let(e, loop_name);
    }

    self.expect(T_LPAREN, "(");  // consume outer '(' of bindings list

    // Collect all bindings: ((x 1) (y 2) (z 3))
    SymbolId[64] names;
    Expr*[64] inits;
    usz binding_count = 0;

    while (self.lexer.current.type == T_LPAREN && !self.has_error) {
        if (binding_count >= 64) { self.set_error("too many let bindings"); return null; }
        self.lexer.advance();  // consume '(' of binding pair

        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected binding name"); return null; }
        names[binding_count] = self.get_current_symbol();
        self.lexer.advance();

        inits[binding_count] = self.parse_expr();

        self.expect(T_RPAREN, ")");  // consume ')' of binding pair
        binding_count++;
    }
    if (self.has_error) return null;

    self.expect(T_RPAREN, ")");  // consume outer ')' of bindings list
    if (self.has_error) return null;

    if (binding_count < 1) { self.set_error("expected at least one binding in let"); return null; }

    Expr* body = self.parse_expr();

    self.expect(T_RPAREN, ")");  // consume outer ')' of let form

    if (binding_count == 1) {
        // Single binding - simple case
        e.tag = E_LET;
        e.let_expr.name = names[0];
        e.let_expr.init = inits[0];
        e.let_expr.body = body;
        e.let_expr.is_recursive = is_recursive;
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
    return e;
}

/**
 * Parse a named let (loop).
 *
 * (let name ((x init-x) (y init-y)) body)
 *
 * Desugars to:
 * (let ^rec ((name (lambda (x y) body))) (name init-x init-y))
 */
fn Expr* Parser.parse_named_let(Parser* self, Expr* e, SymbolId loop_name) {
    if (self.has_error) return null;
    self.expect(T_LPAREN, "(");  // consume outer '(' of bindings list

    // Collect parameter names and init expressions
    SymbolId[64] params;
    Expr*[64] inits;
    usz param_count = 0;

    while (self.lexer.current.type == T_LPAREN && !self.has_error) {
        if (param_count >= 64) { self.set_error("too many named let bindings"); return null; }
        self.lexer.advance();  // consume '(' of binding pair

        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected binding name"); return null; }
        params[param_count] = self.get_current_symbol();
        self.lexer.advance();

        inits[param_count] = self.parse_expr();

        self.expect(T_RPAREN, ")");  // consume ')' of binding pair
        param_count++;
    }
    if (self.has_error) return null;

    self.expect(T_RPAREN, ")");  // consume outer ')' of bindings list
    if (self.has_error) return null;
    if (param_count < 1) { self.set_error("expected at least one binding in named let"); return null; }

    // Parse body
    Expr* body = self.parse_expr();
    self.expect(T_RPAREN, ")");  // consume outer ')' of let form

    // Build the lambda: (lambda (param1 param2 ...) body)
    // Multi-param desugars to nested single-param lambdas (same as parse_lambda)
    Expr* lambda_expr;
    if (param_count == 1) {
        lambda_expr = self.interp.alloc_expr();
        lambda_expr.tag = E_LAMBDA;
        lambda_expr.loc_line = e.loc_line;
        lambda_expr.loc_column = e.loc_column;
        lambda_expr.lambda.param = params[0];
        lambda_expr.lambda.param_count = 1;
        lambda_expr.lambda.params[0] = params[0];
        lambda_expr.lambda.has_rest = false;
        lambda_expr.lambda.rest_param = 0;
        lambda_expr.lambda.body = body;
    } else {
        // Build nested lambdas from inside out
        Expr* inner = body;
        for (usz i = param_count; i > 1; i--) {
            Expr* wrapper = self.interp.alloc_expr();
            wrapper.tag = E_LAMBDA;
            wrapper.loc_line = e.loc_line;
            wrapper.loc_column = e.loc_column;
            wrapper.lambda.param = params[i - 1];
            wrapper.lambda.param_count = 1;
            wrapper.lambda.params[0] = params[i - 1];
            wrapper.lambda.has_rest = false;
            wrapper.lambda.rest_param = 0;
            wrapper.lambda.body = inner;
            inner = wrapper;
        }
        lambda_expr = self.interp.alloc_expr();
        lambda_expr.tag = E_LAMBDA;
        lambda_expr.loc_line = e.loc_line;
        lambda_expr.loc_column = e.loc_column;
        lambda_expr.lambda.param = params[0];
        lambda_expr.lambda.param_count = 1;
        lambda_expr.lambda.params[0] = params[0];
        lambda_expr.lambda.has_rest = false;
        lambda_expr.lambda.rest_param = 0;
        lambda_expr.lambda.body = inner;
    }

    // Build the call: (name init1 init2 ...)
    Expr* call_expr = self.interp.alloc_expr();
    call_expr.tag = E_CALL;
    call_expr.loc_line = e.loc_line;
    call_expr.loc_column = e.loc_column;
    Expr* name_ref = self.interp.alloc_expr();
    name_ref.tag = E_VAR;
    name_ref.loc_line = e.loc_line;
    name_ref.loc_column = e.loc_column;
    name_ref.var_expr.name = loop_name;
    call_expr.call.func = name_ref;
    call_expr.call.arg_count = param_count;
    for (usz i = 0; i < param_count; i++) {
        call_expr.call.args[i] = inits[i];
    }

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

    // Check for [macro] annotation: (define [macro] name ...)
    if (self.lexer.current.type == T_LBRACKET) {
        self.lexer.advance();  // consume '['
        // Expect 'macro' symbol
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected 'macro' after ["); return null; }
        SymbolId annotation = self.get_current_symbol();
        if ((uint)annotation != (uint)self.interp.sym_macro) { self.set_error("expected 'macro' annotation"); return null; }
        self.lexer.advance();  // consume 'macro'
        self.expect(T_RBRACKET, "]");

        // Parse macro name
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected macro name"); return null; }
        SymbolId name = self.get_current_symbol();
        self.lexer.advance();

        // Parse macro clauses
        e.tag = E_DEFMACRO;
        e.define_macro.name = name;
        e.define_macro.clause_count = 0;

        // Parse clauses: (pattern template) ...
        while (self.lexer.current.type == T_LPAREN && !self.has_error) {
            if (e.define_macro.clause_count >= 8) { self.set_error("too many macro clauses"); return null; }
            self.lexer.advance();  // consume '('

            // Parse pattern
            Pattern* pat = self.parse_pattern();

            // Parse template datum
            Value* tmpl = self.parse_template_datum();

            self.expect(T_RPAREN, ")");  // close clause

            e.define_macro.clauses[e.define_macro.clause_count].pattern = pat;
            e.define_macro.clauses[e.define_macro.clause_count].tmpl = tmpl;
            e.define_macro.clause_count++;
        }

        self.expect(T_RPAREN, ")");  // close define
        return e;
    }

    // Shorthand define: (define (name params...) body) => (define name (lambda (params...) body))
    if (self.lexer.current.type == T_LPAREN) {
        self.lexer.advance();  // consume '('

        // Read function name
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected function name in shorthand define"); return null; }
        SymbolId name = self.get_current_symbol();
        self.lexer.advance();

        // Collect parameter symbols until ')'
        SymbolId[64] params;
        usz param_count = 0;
        while (self.lexer.current.type == T_SYMBOL) {
            if (param_count >= 64) { self.set_error("too many parameters in shorthand define"); return null; }
            params[param_count] = self.get_current_symbol();
            param_count++;
            self.lexer.advance();
        }
        self.expect(T_RPAREN, ")");  // close param list

        // Parse body expression
        Expr* body = self.parse_expr();

        // Consume closing ')' of the define form
        self.expect(T_RPAREN, ")");

        // Build lambda expression
        Expr* lambda_expr;
        if (param_count == 0) {
            // Zero-arg lambda
            lambda_expr = self.interp.alloc_expr();
            lambda_expr.tag = E_LAMBDA;
            lambda_expr.loc_line = e.loc_line;
            lambda_expr.loc_column = e.loc_column;
            lambda_expr.lambda.param = (SymbolId)0xFFFFFFFF;
            lambda_expr.lambda.param_count = 0;
            lambda_expr.lambda.has_rest = false;
            lambda_expr.lambda.rest_param = 0;
            lambda_expr.lambda.body = body;
        } else if (param_count == 1) {
            // Single-param lambda
            lambda_expr = self.interp.alloc_expr();
            lambda_expr.tag = E_LAMBDA;
            lambda_expr.loc_line = e.loc_line;
            lambda_expr.loc_column = e.loc_column;
            lambda_expr.lambda.param = params[0];
            lambda_expr.lambda.param_count = 1;
            lambda_expr.lambda.params[0] = params[0];
            lambda_expr.lambda.has_rest = false;
            lambda_expr.lambda.rest_param = 0;
            lambda_expr.lambda.body = body;
        } else {
            // Multi-param: desugar to nested single-param lambdas (inside out)
            Expr* inner = body;
            for (usz i = param_count; i > 1; i--) {
                Expr* wrapper = self.interp.alloc_expr();
                wrapper.tag = E_LAMBDA;
                wrapper.loc_line = e.loc_line;
                wrapper.loc_column = e.loc_column;
                wrapper.lambda.param = params[i - 1];
                wrapper.lambda.param_count = 1;
                wrapper.lambda.params[0] = params[i - 1];
                wrapper.lambda.has_rest = false;
                wrapper.lambda.rest_param = 0;
                wrapper.lambda.body = inner;
                inner = wrapper;
            }
            // Outermost lambda with first param
            lambda_expr = self.interp.alloc_expr();
            lambda_expr.tag = E_LAMBDA;
            lambda_expr.loc_line = e.loc_line;
            lambda_expr.loc_column = e.loc_column;
            lambda_expr.lambda.param = params[0];
            lambda_expr.lambda.param_count = 1;
            lambda_expr.lambda.params[0] = params[0];
            lambda_expr.lambda.has_rest = false;
            lambda_expr.lambda.rest_param = 0;
            lambda_expr.lambda.body = inner;
        }

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

fn Expr* Parser.parse_handle(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'handle' location
    self.lexer.advance();  // consume 'handle'

    Expr* body = self.parse_expr();

    e.tag = E_HANDLE;
    e.handle.body = body;
    e.handle.clause_count = 0;

    // Parse handler clauses
    while (self.lexer.current.type == T_LPAREN && !self.has_error) {
        self.lexer.advance();
        self.expect(T_LPAREN, "(");

        // (tag k arg)
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected effect tag"); return null; }
        SymbolId etag = self.get_current_symbol();
        self.lexer.advance();

        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected continuation name"); return null; }
        SymbolId k_name = self.get_current_symbol();
        self.lexer.advance();

        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected arg name"); return null; }
        SymbolId arg_name = self.get_current_symbol();
        self.lexer.advance();

        self.expect(T_RPAREN, ")");

        Expr* handler_body = self.parse_expr();

        self.expect(T_RPAREN, ")");

        if (e.handle.clause_count >= MAX_EFFECT_CLAUSES) { self.set_error("too many effect clauses"); return null; }
        e.handle.clauses[e.handle.clause_count] = {
            .effect_tag = etag,
            .k_name = k_name,
            .arg_name = arg_name,
            .handler_body = handler_body
        };
        e.handle.clause_count++;
    }

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
        p.lit_value = self.interp.alloc_value();
        p.lit_value.tag = INT;
        p.lit_value.int_val = lex.current.int_value;
        lex.advance();
        return p;
    }

    // String literal
    if (lex.current.type == T_STRING) {
        Pattern* p = self.interp.alloc_pattern();
        p.tag = PAT_LIT;
        p.lit_value = self.interp.alloc_value();
        p.lit_value.tag = STRING;
        usz len = lex.current.text_len;
        if (len > MAX_STRING_LEN - 1) len = MAX_STRING_LEN - 1;
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
        p.elem_count = 0;
        p.rest_pos = REST_NONE;
        p.rest_binding = 0;

        // Check for [.. rest] pattern (REST_START)
        if (lex.current.type == T_DOTDOT) {
            lex.advance();  // consume '..'
            p.rest_pos = REST_START;

            // Parse elements after ..
            while (lex.current.type != T_RBRACKET && !self.has_error) {
                if (p.elem_count >= MAX_PATTERN_ELEMS) { self.set_error("too many pattern elements"); return null; }
                p.elements[p.elem_count] = self.parse_pattern();
                p.elem_count++;
            }
            if (self.has_error) return null;
            lex.advance();  // consume ']'
            return p;
        }

        // Parse elements until ] or ..
        while (lex.current.type != T_RBRACKET && lex.current.type != T_DOTDOT && !self.has_error) {
            if (p.elem_count >= MAX_PATTERN_ELEMS) { self.set_error("too many pattern elements"); return null; }
            p.elements[p.elem_count] = self.parse_pattern();
            p.elem_count++;
        }

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
    e.match.scrutinee = scrutinee;
    e.match.clause_count = 0;

    // Parse clauses: (pattern result)
    while (self.lexer.current.type == T_LPAREN && !self.has_error) {
        self.lexer.advance();  // consume '('

        if (e.match.clause_count >= MAX_MATCH_CLAUSES) { self.set_error("too many match clauses"); return null; }

        // Parse pattern
        Pattern* pattern = self.parse_pattern();

        // Parse result expression
        Expr* result = self.parse_expr();

        self.expect(T_RPAREN, ")");

        e.match.clauses[e.match.clause_count] = {
            .pattern = pattern,
            .result = result
        };
        e.match.clause_count++;
    }

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

    e.tag = E_BEGIN;
    e.begin.expr_count = 0;

    while (self.lexer.current.type != T_RPAREN && !self.has_error) {
        if (e.begin.expr_count >= 64) { self.set_error("too many expressions in begin"); return null; }
        e.begin.exprs[e.begin.expr_count] = self.parse_expr();
        e.begin.expr_count++;
    }
    if (self.has_error) return null;
    self.lexer.advance();  // consume ')'

    // (begin) with no expressions evaluates to nil
    if (e.begin.expr_count == 0) {
        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value();
        e.lit.value.tag = NIL;
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

    if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected variable name after set!"); return null; }
    SymbolId name = self.get_current_symbol();
    self.lexer.advance();

    Expr* value = self.parse_expr();

    self.expect(T_RPAREN, ")");

    e.tag = E_SET;
    e.set_expr.name = name;
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
        Value* v = self.interp.alloc_value();
        v.tag = SYMBOL;
        v.sym_val = self.interp.sym_dotdot;
        return v;
    }

    // Handle "_" as a symbol in templates
    if (lex.current.type == T_UNDERSCORE) {
        lex.advance();
        SymbolId usym = self.interp.symbols.intern("_");
        if (usym == INVALID_SYMBOL_ID) { self.set_error("symbol table exhausted"); return null; }
        Value* v = self.interp.alloc_value();
        v.tag = SYMBOL;
        v.sym_val = usym;
        return v;
    }

    if (lex.current.type == T_INT) {
        long val = lex.current.int_value;
        lex.advance();
        Value* v = self.interp.alloc_value();
        v.tag = INT;
        v.int_val = val;
        return v;
    }

    if (lex.current.type == T_SYMBOL) {
        SymbolId sym = self.get_current_symbol();
        lex.advance();
        Value* v = self.interp.alloc_value();
        v.tag = SYMBOL;
        v.sym_val = sym;
        return v;
    }

    if (lex.current.type == T_STRING) {
        Value* v = self.interp.alloc_value();
        v.tag = STRING;
        usz len = lex.current.text_len;
        if (len > MAX_STRING_LEN - 1) len = MAX_STRING_LEN - 1;
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
        Value* quote_sym = self.interp.alloc_value();
        quote_sym.tag = SYMBOL;
        quote_sym.sym_val = self.interp.sym_quote;
        Value* rest = self.interp.alloc_value();
        rest.tag = CONS;
        rest.cons_val.car = inner;
        rest.cons_val.cdr = self.interp.alloc_value();
        rest.cons_val.cdr.tag = NIL;
        Value* v = self.interp.alloc_value();
        v.tag = CONS;
        v.cons_val.car = quote_sym;
        v.cons_val.cdr = rest;
        return v;
    }

    if (lex.current.type == T_LPAREN) {
        lex.advance();

        if (lex.current.type == T_RPAREN) {
            lex.advance();
            Value* v = self.interp.alloc_value();
            v.tag = NIL;
            return v;
        }

        // Parse list of template datums
        Value* first = self.parse_template_datum();
        Value* head = self.interp.alloc_value();
        head.tag = CONS;
        head.cons_val.car = first;
        head.cons_val.cdr = null;

        Value* tail = head;

        while (lex.current.type != T_RPAREN && !self.has_error) {
            Value* next = self.parse_template_datum();
            if (self.has_error) return null;
            Value* cell = self.interp.alloc_value();
            cell.tag = CONS;
            cell.cons_val.car = next;
            cell.cons_val.cdr = null;
            tail.cons_val.cdr = cell;
            tail = cell;
        }
        if (self.has_error) return null;

        Value* nil = self.interp.alloc_value();
        nil.tag = NIL;
        tail.cons_val.cdr = nil;

        lex.advance();  // consume ')'
        return head;
    }

    // Fallback: nil
    Value* v = self.interp.alloc_value();
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
    e.module_expr.name = name;
    e.module_expr.export_count = 0;
    e.module_expr.body_count = 0;

    // Parse (export sym1 sym2 ...) — required
    self.expect(T_LPAREN, "(");
    if (self.has_error) return null;
    if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected 'export' in module"); return null; }
    SymbolId maybe_export = self.get_current_symbol();
    if ((uint)maybe_export != (uint)self.interp.sym_export) { self.set_error("expected 'export' in module"); return null; }
    self.lexer.advance();  // consume 'export'
    while (self.lexer.current.type == T_SYMBOL) {
        if (e.module_expr.export_count >= 64) { self.set_error("too many exports"); return null; }
        e.module_expr.exports[e.module_expr.export_count] = self.get_current_symbol();
        e.module_expr.export_count++;
        self.lexer.advance();
    }
    self.expect(T_RPAREN, ")");  // close export list

    // Parse remaining body expressions
    while (self.lexer.current.type != T_RPAREN && !self.lexer.at_end() && !self.has_error) {
        if (e.module_expr.body_count >= 64) { self.set_error("too many body expressions in module"); return null; }
        e.module_expr.body[e.module_expr.body_count] = self.parse_expr();
        e.module_expr.body_count++;
    }

    self.expect(T_RPAREN, ")");  // close module
    return e;
}

/**
 * Parse import: (import name) or (import "path/to/file.pika")
 */
fn Expr* Parser.parse_import(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();
    self.lexer.advance();  // consume 'import'

    e.tag = E_IMPORT;
    e.import_expr.has_path = false;
    e.import_expr.path_len = 0;

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
        e.import_expr.path_len = len;
        SymbolId import_sym = self.interp.symbols.intern(str);
        if (import_sym == INVALID_SYMBOL_ID) { self.set_error("symbol table exhausted"); return null; }
        e.import_expr.name = import_sym;
        self.lexer.advance();
    } else {
        self.set_error("expected module name or path after import");
        return null;
    }

    self.expect(T_RPAREN, ")");
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
        e.lit.value = self.interp.alloc_value();
        e.lit.value.tag = INT;
        e.lit.value.int_val = val;
        return e;
    }

    // String literal
    if (lex.current.type == T_STRING) {
        Expr* e = self.alloc_expr_here();
        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value();
        e.lit.value.tag = STRING;
        usz len = lex.current.text_len;
        if (len > MAX_STRING_LEN - 1) len = MAX_STRING_LEN - 1;
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
            e.lit.value = self.interp.alloc_value();
            e.lit.value.tag = NIL;
            return e;
        }

        // Parse as generic call: (head elem1 elem2 ...)
        Expr* func = self.parse_qq_template();
        Expr* e = self.interp.alloc_expr();
        e.tag = E_CALL;
        e.call.func = func;
        e.call.arg_count = 0;

        while (lex.current.type != T_RPAREN && !self.has_error) {
            if (e.call.arg_count >= 64) { self.set_error("too many elements in quasiquote list"); return null; }
            e.call.args[e.call.arg_count] = self.parse_qq_template();
            e.call.arg_count++;
        }
        if (self.has_error) return null;
        lex.advance();  // consume ')'
        return e;
    }

    // Underscore as symbol in templates
    if (lex.current.type == T_UNDERSCORE) {
        Expr* e = self.alloc_expr_here();
        SymbolId sym = self.interp.symbols.intern("_");
        if (sym == INVALID_SYMBOL_ID) { self.set_error("symbol table exhausted"); return null; }
        lex.advance();
        e.tag = E_VAR;
        e.var_expr.name = sym;
        return e;
    }

    self.set_error("unexpected token in quasiquote template");
    return null;
}

fn Expr* Parser.parse_application(Parser* self) {
    if (self.has_error) return null;
    // Already consumed '(' - capture func location for application
    usz app_line = self.lexer.current.line;
    usz app_col = self.lexer.current.column;
    Expr* func = self.parse_expr();

    // Collect arguments
    Expr* e = self.interp.alloc_expr();
    e.tag = E_CALL;
    e.loc_line = app_line;
    e.loc_column = app_col;
    e.call.func = func;
    e.call.arg_count = 0;

    while (self.lexer.current.type != T_RPAREN && !self.has_error) {
        if (e.call.arg_count >= 64) { self.set_error("too many arguments in call"); return null; }
        e.call.args[e.call.arg_count] = self.parse_expr();
        e.call.arg_count++;
    }
    if (self.has_error) return null;
    self.lexer.advance();  // consume ')'

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
        Value* v = self.interp.alloc_value();
        v.tag = INT;
        v.int_val = val;
        return v;
    }

    if (lex.current.type == T_SYMBOL) {
        SymbolId sym = self.get_current_symbol();
        lex.advance();
        Value* v = self.interp.alloc_value();
        v.tag = SYMBOL;
        v.sym_val = sym;
        return v;
    }

    if (lex.current.type == T_STRING) {
        Value* v = self.interp.alloc_value();
        v.tag = STRING;
        usz len = lex.current.text_len;
        if (len > MAX_STRING_LEN - 1) len = MAX_STRING_LEN - 1;
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
        Value* quote_sym = self.interp.alloc_value();
        quote_sym.tag = SYMBOL;
        quote_sym.sym_val = self.interp.sym_quote;

        Value* rest = self.interp.alloc_value();
        rest.tag = CONS;
        rest.cons_val.car = inner;
        rest.cons_val.cdr = self.interp.alloc_value();
        rest.cons_val.cdr.tag = NIL;

        Value* v = self.interp.alloc_value();
        v.tag = CONS;
        v.cons_val.car = quote_sym;
        v.cons_val.cdr = rest;
        return v;
    }

    if (lex.current.type == T_LPAREN) {
        lex.advance();

        if (lex.current.type == T_RPAREN) {
            lex.advance();
            Value* v = self.interp.alloc_value();
            v.tag = NIL;
            return v;
        }

        // Parse list of datums
        Value* first = self.parse_datum();
        Value* head = self.interp.alloc_value();
        head.tag = CONS;
        head.cons_val.car = first;
        head.cons_val.cdr = null;

        Value* tail = head;

        while (lex.current.type != T_RPAREN && !self.has_error) {
            Value* next = self.parse_datum();
            if (self.has_error) return null;
            Value* cell = self.interp.alloc_value();
            cell.tag = CONS;
            cell.cons_val.car = next;
            cell.cons_val.cdr = null;
            tail.cons_val.cdr = cell;
            tail = cell;
        }
        if (self.has_error) return null;

        Value* nil = self.interp.alloc_value();
        nil.tag = NIL;
        tail.cons_val.cdr = nil;

        lex.advance();  // consume ')'
        return head;
    }

    Value* v = self.interp.alloc_value();
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
        exprs.push(p.parse_expr());
    }
    return exprs;
}
