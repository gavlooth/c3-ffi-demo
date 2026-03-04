module lisp;

import std::collections::list;

/**
 * Datum construction helpers used by parse_datum/parse_template_datum.
 */
fn Value* parser_make_nil_datum(Interp* interp) @inline {
    Value* v = interp.alloc_value_root();
    v.tag = NIL;
    return v;
}

fn Value* parser_make_int_datum(Interp* interp, long val) @inline {
    Value* v = interp.alloc_value_root();
    v.tag = INT;
    v.int_val = val;
    return v;
}

fn Value* parser_make_symbol_datum(Interp* interp, SymbolId sym) @inline {
    Value* v = interp.alloc_value_root();
    v.tag = SYMBOL;
    v.sym_val = sym;
    return v;
}

fn Value* parser_make_string_datum(Interp* interp, Lexer* lex) {
    Value* v = interp.alloc_value_root();
    v.tag = STRING;
    usz len = lex.current.text_len;
    StringVal* builder = strval_new(len);
    for (usz i = 0; i < len; i++) {
        builder.chars[i] = lex.current.text[i];
    }
    builder.chars[len] = 0;
    builder.len = len;
    strval_into_value(builder, v);
    return v;
}

fn Value* parser_make_quote_datum(Interp* interp, Value* inner) {
    Value* quote_sym = parser_make_symbol_datum(interp, interp.sym_quote);
    Value* rest = interp.alloc_value_root();
    rest.tag = CONS;
    rest.cons_val.car = inner;
    rest.cons_val.cdr = parser_make_nil_datum(interp);

    Value* v = interp.alloc_value_root();
    v.tag = CONS;
    v.cons_val.car = quote_sym;
    v.cons_val.cdr = rest;
    return v;
}

fn Value* Parser.parse_datum_template_only(
    Parser* self,
    bool template_mode,
    SymbolId sym
) {
    if (!template_mode) return parser_make_nil_datum(self.interp);
    self.lexer.advance();
    return parser_make_symbol_datum(self.interp, sym);
}

fn Value* Parser.parse_datum_string(Parser* self) {
    Value* v = parser_make_string_datum(self.interp, self.lexer);
    self.lexer.advance();
    return v;
}

fn Value* Parser.parse_datum_quote(Parser* self, bool template_mode) {
    self.lexer.advance();
    Value* inner = self.parse_datum_impl(template_mode);
    return parser_make_quote_datum(self.interp, inner);
}

fn Value* Parser.parse_datum_list(Parser* self, bool template_mode) {
    Lexer* lex = self.lexer;
    lex.advance();
    if (lex.current.type == T_RPAREN) {
        lex.advance();
        return parser_make_nil_datum(self.interp);
    }

    Value* first = self.parse_datum_impl(template_mode);
    Value* head = self.interp.alloc_value_root();
    head.tag = CONS;
    head.cons_val.car = first;
    head.cons_val.cdr = null;

    Value* tail = head;
    while (lex.current.type != T_RPAREN && !self.has_error) {
        Value* next = self.parse_datum_impl(template_mode);
        if (self.has_error) return null;
        Value* cell = self.interp.alloc_value_root();
        cell.tag = CONS;
        cell.cons_val.car = next;
        cell.cons_val.cdr = null;
        tail.cons_val.cdr = cell;
        tail = cell;
    }
    if (self.has_error) return null;

    tail.cons_val.cdr = parser_make_nil_datum(self.interp);
    lex.advance();  // consume ')'
    return head;
}

fn Value* Parser.parse_datum_impl(Parser* self, bool template_mode) {
    if (self.has_error) return null;
    Lexer* lex = self.lexer;

    switch (lex.current.type) {
        case T_DOTDOT:
            return self.parse_datum_template_only(template_mode, self.interp.sym_dotdot);
        case T_UNDERSCORE:
            return self.parse_datum_template_only(template_mode, self.interp.symbols.intern("_"));
        case T_INT: {
            long val = lex.current.int_value;
            lex.advance();
            return parser_make_int_datum(self.interp, val);
        }
        case T_SYMBOL: {
            SymbolId sym = self.get_current_symbol();
            lex.advance();
            return parser_make_symbol_datum(self.interp, sym);
        }
        case T_STRING:
            return self.parse_datum_string();
        case T_QUOTE:
            return self.parse_datum_quote(template_mode);
        case T_LPAREN:
            return self.parse_datum_list(template_mode);
        default:
            return parser_make_nil_datum(self.interp);
    }
}

/**
 * Parse a datum (for quote).
 */
fn Value* Parser.parse_datum(Parser* self) {
    return self.parse_datum_impl(false);
}

/**
 * Parse a template datum (for macro templates).
 * Like parse_datum() but also handles T_DOTDOT as a ".." symbol,
 * and T_UNDERSCORE as a "_" symbol.
 */
fn Value* Parser.parse_template_datum(Parser* self) {
    return self.parse_datum_impl(true);
}

fn Expr* Parser.parse_qq_string_literal(Parser* self) {
    Expr* e = self.alloc_expr_here();
    e.tag = E_LIT;
    e.lit.value = self.interp.alloc_value_root();
    e.lit.value.tag = STRING;
    usz len = self.lexer.current.text_len;
    StringVal* builder = strval_new(len);
    for (usz i = 0; i < len; i++) {
        builder.chars[i] = self.lexer.current.text[i];
    }
    builder.chars[len] = 0;
    builder.len = len;
    strval_into_value(builder, e.lit.value);
    self.lexer.advance();
    return e;
}

fn Expr* Parser.parse_qq_list_form(Parser* self) {
    self.lexer.advance();  // consume '('

    if (self.lexer.current.type == T_RPAREN) {
        Expr* e = self.alloc_expr_here();
        self.lexer.advance();
        e.tag = E_LIT;
        e.lit.value = self.interp.alloc_value_root();
        e.lit.value.tag = NIL;
        return e;
    }

    Expr* func = self.parse_qq_template();
    List{Expr*} qq_args;
    while (self.lexer.current.type != T_RPAREN && !self.has_error) {
        qq_args.push(self.parse_qq_template());
    }
    if (self.has_error) { qq_args.free(); return null; }
    self.lexer.advance();  // consume ')'

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

fn Expr* Parser.parse_qq_template(Parser* self) {
    if (self.has_error) return null;
    switch (self.lexer.current.type) {
        case T_BACKTICK: {
            Expr* e = self.alloc_expr_here();
            self.lexer.advance();
            e.tag = E_QUASIQUOTE;
            e.quasiquote.body = self.parse_qq_template();
            return e;
        }
        case T_COMMA: {
            Expr* e = self.alloc_expr_here();
            self.lexer.advance();
            e.tag = E_UNQUOTE;
            e.unquote.body = self.parse_expr();
            return e;
        }
        case T_COMMA_AT: {
            Expr* e = self.alloc_expr_here();
            self.lexer.advance();
            e.tag = E_UNQUOTE_SPLICING;
            e.unquote_splicing.body = self.parse_expr();
            return e;
        }
        case T_QUOTE: {
            Expr* e = self.alloc_expr_here();
            self.lexer.advance();
            e.tag = E_QUOTE;
            e.quote.datum = self.parse_datum();
            return e;
        }
        case T_INT: {
            Expr* e = self.alloc_expr_here();
            long val = self.lexer.current.int_value;
            self.lexer.advance();
            e.tag = E_LIT;
            e.lit.value = self.interp.alloc_value_root();
            e.lit.value.tag = INT;
            e.lit.value.int_val = val;
            return e;
        }
        case T_STRING:
            return self.parse_qq_string_literal();
        case T_SYMBOL: {
            Expr* e = self.alloc_expr_here();
            SymbolId sym = self.get_current_symbol();
            self.lexer.advance();
            e.tag = E_VAR;
            e.var_expr.name = sym;
            return e;
        }
        case T_LPAREN:
            return self.parse_qq_list_form();
        case T_UNDERSCORE: {
            Expr* e = self.alloc_expr_here();
            SymbolId sym = self.interp.symbols.intern("_");
            self.lexer.advance();
            e.tag = E_VAR;
            e.var_expr.name = sym;
            return e;
        }
        default:
            self.set_error("unexpected token in quasiquote template");
            return null;
    }
}
