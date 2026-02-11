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
    T_DOT_BRACKET,  // .[ for index access (OmniLisp syntax)
    T_QUOTE,
    T_INT,
    T_STRING,       // "..." string literal
    T_SYMBOL,
    T_PATH,         // dot-separated path: point.x, person.address.city
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
        self.pos++;
        return;
    }

    if (c == ')') {
        self.current.type = T_RPAREN;
        self.pos++;
        return;
    }

    if (c == '\'') {
        self.current.type = T_QUOTE;
        self.pos++;
        return;
    }

    if (c == '[') {
        self.current.type = T_LBRACKET;
        self.pos++;
        return;
    }

    if (c == ']') {
        self.current.type = T_RBRACKET;
        self.pos++;
        return;
    }

    // String literal: "..."
    if (c == '"') {
        self.pos++;  // consume opening quote
        usz out_idx = 0;

        while (self.pos < self.len && self.source[self.pos] != '"') {
            char ch = self.source[self.pos];

            if (ch == '\\' && self.pos + 1 < self.len) {
                // Handle escape sequences
                self.pos++;
                char escaped = self.source[self.pos];
                switch (escaped) {
                    case 'n':
                        if (out_idx < 63) self.current.text[out_idx++] = '\n';
                    case 't':
                        if (out_idx < 63) self.current.text[out_idx++] = '\t';
                    case '\\':
                        if (out_idx < 63) self.current.text[out_idx++] = '\\';
                    case '"':
                        if (out_idx < 63) self.current.text[out_idx++] = '"';
                    default:
                        // Unknown escape, just include the character
                        if (out_idx < 63) self.current.text[out_idx++] = escaped;
                }
                self.pos++;
            } else {
                if (out_idx < 63) self.current.text[out_idx++] = ch;
                self.pos++;
            }
        }

        if (self.pos < self.len && self.source[self.pos] == '"') {
            self.pos++;  // consume closing quote
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
            self.pos++;
        }
        long val = 0;
        while (self.pos < self.len &&
               self.source[self.pos] >= '0' && self.source[self.pos] <= '9') {
            val = val * 10 + (long)(self.source[self.pos] - '0');
            self.pos++;
        }
        if (negative) val = -val;

        self.current.type = T_INT;
        self.current.int_value = val;
        self.current.text_len = self.pos - start;
        return;
    }

    // Index access: .[ (OmniLisp dot-bracket notation)
    // Must check before symbol parsing since . is a symbol char
    if (c == '.' && self.pos + 1 < self.len && self.source[self.pos + 1] == '[') {
        self.current.type = T_DOT_BRACKET;
        self.current.text[0] = '.';
        self.current.text[1] = '[';
        self.current.text[2] = 0;
        self.current.text_len = 2;
        self.pos += 2;
        return;
    }

    // Symbol or Path (dot-separated like point.x or person.address.city)
    // Stops when seeing .[ (index access) to allow T_DOT_BRACKET to be lexed separately
    if (is_symbol_char(c)) {
        usz start = self.pos;
        bool has_dot = false;
        while (self.pos < self.len && is_symbol_char(self.source[self.pos])) {
            // Stop before .[ so it can be lexed as T_DOT_BRACKET
            if (self.source[self.pos] == '.' &&
                self.pos + 1 < self.len && self.source[self.pos + 1] == '[') {
                break;
            }
            if (self.source[self.pos] == '.') {
                has_dot = true;
            }
            self.pos++;
        }
        usz len = self.pos - start;
        if (len > 63) len = 63;

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
        io::printfn("Parse error: expected %s", msg);
        unreachable("parse error");
    }
}

// =============================================================================
// SECTION 2: PARSER
// =============================================================================

struct Parser {
    Lexer*  lexer;
    Interp* interp;
}

fn void Parser.init(Parser* self, Lexer* lexer, Interp* interp) {
    self.lexer = lexer;
    self.interp = interp;
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
    return self.interp.symbols.intern(self.lexer.current.text[:self.lexer.current.text_len]);
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
    // Keep parsing postfix .[index] as long as we see .[
    while (self.lexer.current.type == T_DOT_BRACKET) {
        self.lexer.advance();  // consume '.['

        // Parse the index expression
        Expr* idx = self.parse_expr();

        self.lexer.expect(T_RBRACKET, "]");

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
    Lexer* lex = self.lexer;

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
        if (len > MAX_SYMBOL_LEN - 1) len = MAX_SYMBOL_LEN - 1;
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
                    assert(e.path.segment_count < MAX_PATH_SEGMENTS, "too many path segments");
                    usz seg_len = i - start;
                    e.path.segments[e.path.segment_count] = self.interp.symbols.intern(lex.current.text[start:seg_len]);
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
            if ((uint)head == (uint)self.interp.sym_def) {
                return self.parse_def();
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
        }

        // Regular application
        return self.parse_application();
    }

    io::printfn("Parse error: unexpected token");
    unreachable("parse error");
}

fn Expr* Parser.parse_lambda(Parser* self) {
    Expr* e = self.alloc_expr_here();  // Capture 'lambda' location
    self.lexer.advance();  // consume 'lambda'

    self.lexer.expect(T_LPAREN, "(");

    // Get parameter
    assert(self.lexer.current.type == T_SYMBOL, "expected parameter name");
    SymbolId param = self.get_current_symbol();
    self.lexer.advance();

    self.lexer.expect(T_RPAREN, ")");

    // Parse body
    Expr* body = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    e.tag = E_LAMBDA;
    e.lambda.param = param;
    e.lambda.body = body;
    return e;
}

fn Expr* Parser.parse_if(Parser* self) {
    Expr* e = self.alloc_expr_here();  // Capture 'if' location
    self.lexer.advance();  // consume 'if'

    Expr* test = self.parse_expr();
    Expr* then_branch = self.parse_expr();
    Expr* else_branch = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    e.tag = E_IF;
    e.if_expr.test = test;
    e.if_expr.then_branch = then_branch;
    e.if_expr.else_branch = else_branch;
    return e;
}

fn Expr* Parser.parse_let(Parser* self) {
    Expr* e = self.alloc_expr_here();  // Capture 'let' location
    self.lexer.advance();  // consume 'let'

    self.lexer.expect(T_LPAREN, "(");
    self.lexer.expect(T_LPAREN, "(");

    assert(self.lexer.current.type == T_SYMBOL, "expected binding name");
    SymbolId name = self.get_current_symbol();
    self.lexer.advance();

    Expr* init = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");
    self.lexer.expect(T_RPAREN, ")");

    Expr* body = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    e.tag = E_LET;
    e.let_expr.name = name;
    e.let_expr.init = init;
    e.let_expr.body = body;
    return e;
}

fn Expr* Parser.parse_def(Parser* self) {
    Expr* e = self.alloc_expr_here();  // Capture 'def' location
    self.lexer.advance();  // consume 'def'

    assert(self.lexer.current.type == T_SYMBOL, "expected name");
    SymbolId name = self.get_current_symbol();
    self.lexer.advance();

    Expr* value = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    e.tag = E_DEF;
    e.def.name = name;
    e.def.value = value;
    return e;
}

fn Expr* Parser.parse_quote(Parser* self) {
    Expr* e = self.alloc_expr_here();  // Capture 'quote' location
    self.lexer.advance();  // consume 'quote'

    Value* datum = self.parse_datum();

    self.lexer.expect(T_RPAREN, ")");

    e.tag = E_QUOTE;
    e.quote.datum = datum;
    return e;
}

fn Expr* Parser.parse_reset(Parser* self) {
    Expr* e = self.alloc_expr_here();  // Capture 'reset' location
    self.lexer.advance();  // consume 'reset'

    Expr* body = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    e.tag = E_RESET;
    e.reset.body = body;
    return e;
}

fn Expr* Parser.parse_shift(Parser* self) {
    Expr* e = self.alloc_expr_here();  // Capture 'shift' location
    self.lexer.advance();  // consume 'shift'

    assert(self.lexer.current.type == T_SYMBOL, "expected continuation name");
    SymbolId k_name = self.get_current_symbol();
    self.lexer.advance();

    Expr* body = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    e.tag = E_SHIFT;
    e.shift.k_name = k_name;
    e.shift.body = body;
    return e;
}

fn Expr* Parser.parse_perform(Parser* self) {
    Expr* e = self.alloc_expr_here();  // Capture 'perform' location
    self.lexer.advance();  // consume 'perform'

    assert(self.lexer.current.type == T_SYMBOL, "expected effect tag");
    SymbolId etag = self.get_current_symbol();
    self.lexer.advance();

    Expr* arg = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    e.tag = E_PERFORM;
    e.perform.tag = etag;
    e.perform.arg = arg;
    return e;
}

fn Expr* Parser.parse_handle(Parser* self) {
    Expr* e = self.alloc_expr_here();  // Capture 'handle' location
    self.lexer.advance();  // consume 'handle'

    Expr* body = self.parse_expr();

    e.tag = E_HANDLE;
    e.handle.body = body;
    e.handle.clause_count = 0;

    // Parse handler clauses
    while (self.lexer.current.type == T_LPAREN) {
        self.lexer.advance();
        self.lexer.expect(T_LPAREN, "(");

        // (tag k arg)
        assert(self.lexer.current.type == T_SYMBOL, "expected effect tag");
        SymbolId etag = self.get_current_symbol();
        self.lexer.advance();

        assert(self.lexer.current.type == T_SYMBOL, "expected continuation name");
        SymbolId k_name = self.get_current_symbol();
        self.lexer.advance();

        assert(self.lexer.current.type == T_SYMBOL, "expected arg name");
        SymbolId arg_name = self.get_current_symbol();
        self.lexer.advance();

        self.lexer.expect(T_RPAREN, ")");

        Expr* handler_body = self.parse_expr();

        self.lexer.expect(T_RPAREN, ")");

        assert(e.handle.clause_count < MAX_EFFECT_CLAUSES, "too many effect clauses");
        e.handle.clauses[e.handle.clause_count] = {
            .effect_tag = etag,
            .k_name = k_name,
            .arg_name = arg_name,
            .handler_body = handler_body
        };
        e.handle.clause_count++;
    }

    self.lexer.expect(T_RPAREN, ")");

    return e;
}

fn Expr* Parser.parse_application(Parser* self) {
    // Already consumed '(' - capture func location for application
    usz app_line = self.lexer.current.line;
    usz app_col = self.lexer.current.column;
    Expr* func = self.parse_expr();

    // Collect arguments
    List{Expr*} args;
    while (self.lexer.current.type != T_RPAREN) {
        args.push(self.parse_expr());
    }
    self.lexer.advance();  // consume ')'

    // Curry if multiple arguments: (f a b c) => (((f a) b) c)
    Expr* result = func;
    foreach (arg : args) {
        Expr* app = self.interp.alloc_expr();
        app.tag = E_APP;
        app.app.func = result;
        app.app.arg = arg;
        // Use original application location for all curried apps
        app.loc_line = app_line;
        app.loc_column = app_col;
        result = app;
    }
    args.free();

    // Check for postfix indexing: (make-array 10).[0]
    return self.parse_postfix_index(result);
}

/**
 * Parse a datum (for quote).
 */
fn Value* Parser.parse_datum(Parser* self) {
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
        if (len > MAX_SYMBOL_LEN - 1) len = MAX_SYMBOL_LEN - 1;
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

        while (lex.current.type != T_RPAREN) {
            Value* next = self.parse_datum();
            Value* cell = self.interp.alloc_value();
            cell.tag = CONS;
            cell.cons_val.car = next;
            cell.cons_val.cdr = null;
            tail.cons_val.cdr = cell;
            tail = cell;
        }

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
    while (!lex.at_end()) {
        exprs.push(p.parse_expr());
    }
    return exprs;
}
