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
    T_QUOTE,
    T_INT,
    T_SYMBOL,
    T_ERROR,
}

struct Token {
    TokenType type;
    char[64]  text;
    usz       text_len;
    long      int_value;  // For T_INT
}

struct Lexer {
    char[]  source;
    usz     pos;
    usz     len;
    Token   current;
}

fn void Lexer.init(Lexer* self, char[] source) {
    self.source = source;
    self.pos = 0;
    self.len = source.len;
    self.advance();  // Prime the first token
}

fn char Lexer.peek(Lexer* self) {
    if (self.pos >= self.len) return 0;
    return self.source[self.pos];
}

fn char Lexer.next_char(Lexer* self) {
    if (self.pos >= self.len) return 0;
    return self.source[self.pos++];
}

fn void Lexer.skip_whitespace(Lexer* self) {
    while (self.pos < self.len) {
        char c = self.source[self.pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            self.pos++;
        } else if (c == ';') {
            // Comment: skip to end of line
            while (self.pos < self.len && self.source[self.pos] != '\n') {
                self.pos++;
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

    // Symbol
    if (is_symbol_char(c)) {
        usz start = self.pos;
        while (self.pos < self.len && is_symbol_char(self.source[self.pos])) {
            self.pos++;
        }
        usz len = self.pos - start;
        if (len > 63) len = 63;

        for (usz i = 0; i < len; i++) {
            self.current.text[i] = self.source[start + i];
        }
        self.current.text[len] = 0;
        self.current.text_len = len;
        self.current.type = T_SYMBOL;
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
 * Get interned symbol from current token text.
 */
fn SymbolId Parser.get_current_symbol(Parser* self) {
    // Pass the char slice directly as String (String = char[])
    return self.interp.symbols.intern(self.lexer.current.text[:self.lexer.current.text_len]);
}

/**
 * Parse a complete expression.
 */
fn Expr* Parser.parse_expr(Parser* self) {
    Lexer* lex = self.lexer;

    // Quote shorthand: 'x => (quote x)
    if (lex.current.type == T_QUOTE) {
        lex.advance();
        Expr* e = self.interp.alloc_expr();
        e.tag = E_QUOTE;
        e.quote.datum = self.parse_datum();
        return e;
    }

    // Integer literal
    if (lex.current.type == T_INT) {
        long val = lex.current.int_value;
        lex.advance();

        Expr* e = self.interp.alloc_expr();
        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value();
        e.lit.value.tag = INT;
        e.lit.value.int_val = val;
        return e;
    }

    // Symbol (variable reference)
    if (lex.current.type == T_SYMBOL) {
        SymbolId sym = self.get_current_symbol();
        lex.advance();

        Expr* e = self.interp.alloc_expr();
        e.tag = E_VAR;
        e.var_expr.name = sym;
        return e;
    }

    // List form: (...)
    if (lex.current.type == T_LPAREN) {
        lex.advance();

        if (lex.current.type == T_RPAREN) {
            // Empty list () = nil literal
            lex.advance();
            Expr* e = self.interp.alloc_expr();
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

    Expr* e = self.interp.alloc_expr();
    e.tag = E_LAMBDA;
    e.lambda.param = param;
    e.lambda.body = body;
    return e;
}

fn Expr* Parser.parse_if(Parser* self) {
    self.lexer.advance();  // consume 'if'

    Expr* test = self.parse_expr();
    Expr* then_branch = self.parse_expr();
    Expr* else_branch = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    Expr* e = self.interp.alloc_expr();
    e.tag = E_IF;
    e.if_expr.test = test;
    e.if_expr.then_branch = then_branch;
    e.if_expr.else_branch = else_branch;
    return e;
}

fn Expr* Parser.parse_let(Parser* self) {
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

    Expr* e = self.interp.alloc_expr();
    e.tag = E_LET;
    e.let_expr.name = name;
    e.let_expr.init = init;
    e.let_expr.body = body;
    return e;
}

fn Expr* Parser.parse_def(Parser* self) {
    self.lexer.advance();  // consume 'def'

    assert(self.lexer.current.type == T_SYMBOL, "expected name");
    SymbolId name = self.get_current_symbol();
    self.lexer.advance();

    Expr* value = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    Expr* e = self.interp.alloc_expr();
    e.tag = E_DEF;
    e.def.name = name;
    e.def.value = value;
    return e;
}

fn Expr* Parser.parse_quote(Parser* self) {
    self.lexer.advance();  // consume 'quote'

    Value* datum = self.parse_datum();

    self.lexer.expect(T_RPAREN, ")");

    Expr* e = self.interp.alloc_expr();
    e.tag = E_QUOTE;
    e.quote.datum = datum;
    return e;
}

fn Expr* Parser.parse_reset(Parser* self) {
    self.lexer.advance();  // consume 'reset'

    Expr* body = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    Expr* e = self.interp.alloc_expr();
    e.tag = E_RESET;
    e.reset.body = body;
    return e;
}

fn Expr* Parser.parse_shift(Parser* self) {
    self.lexer.advance();  // consume 'shift'

    assert(self.lexer.current.type == T_SYMBOL, "expected continuation name");
    SymbolId k_name = self.get_current_symbol();
    self.lexer.advance();

    Expr* body = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    Expr* e = self.interp.alloc_expr();
    e.tag = E_SHIFT;
    e.shift.k_name = k_name;
    e.shift.body = body;
    return e;
}

fn Expr* Parser.parse_perform(Parser* self) {
    self.lexer.advance();  // consume 'perform'

    assert(self.lexer.current.type == T_SYMBOL, "expected effect tag");
    SymbolId tag = self.get_current_symbol();
    self.lexer.advance();

    Expr* arg = self.parse_expr();

    self.lexer.expect(T_RPAREN, ")");

    Expr* e = self.interp.alloc_expr();
    e.tag = E_PERFORM;
    e.perform.tag = tag;
    e.perform.arg = arg;
    return e;
}

fn Expr* Parser.parse_handle(Parser* self) {
    self.lexer.advance();  // consume 'handle'

    Expr* body = self.parse_expr();

    Expr* e = self.interp.alloc_expr();
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
    // Already consumed '('
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
        result = app;
    }
    args.free();

    return result;
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
