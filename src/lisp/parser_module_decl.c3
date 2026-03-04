module lisp;

fn void parser_module_ensure_export_capacity(ExprModule* module_expr) @inline {
    if (module_expr.export_count < module_expr.export_capacity) return;
    usz new_cap = module_expr.export_capacity * 2;
    SymbolId* new_exports = (SymbolId*)mem::malloc(SymbolId.sizeof * new_cap);
    for (usz i = 0; i < module_expr.export_count; i++) {
        new_exports[i] = module_expr.exports[i];
    }
    mem::free(module_expr.exports);
    module_expr.exports = new_exports;
    module_expr.export_capacity = new_cap;
}

fn void parser_module_ensure_body_capacity(ExprModule* module_expr) @inline {
    if (module_expr.body_count < module_expr.body_capacity) return;
    usz new_cap = module_expr.body_capacity * 2;
    Expr** new_body = (Expr**)mem::malloc(Expr*.sizeof * new_cap);
    for (usz i = 0; i < module_expr.body_count; i++) {
        new_body[i] = module_expr.body[i];
    }
    mem::free(module_expr.body);
    module_expr.body = new_body;
    module_expr.body_capacity = new_cap;
}

fn Expr* Parser.alloc_module_expr(Parser* self, SymbolId name) {
    Expr* e = self.alloc_expr_here();
    e.tag = E_MODULE;
    e.module_expr = mem::malloc(ExprModule.sizeof);
    e.module_expr.name = name;
    e.module_expr.export_count = 0;
    e.module_expr.export_capacity = 32;
    e.module_expr.exports = (SymbolId*)mem::malloc(SymbolId.sizeof * 32);
    e.module_expr.body_count = 0;
    e.module_expr.body_capacity = 32;
    e.module_expr.body = (Expr**)mem::malloc(Expr*.sizeof * 32);
    return e;
}

fn bool Parser.parse_module_export_keyword(Parser* self) {
    self.expect(T_LPAREN, "(");
    if (self.has_error) return false;
    if (self.lexer.current.type != T_SYMBOL) {
        self.set_error("expected 'export' in module");
        return false;
    }
    SymbolId maybe_export = self.get_current_symbol();
    if ((uint)maybe_export != (uint)self.interp.sym_export) {
        self.set_error("expected 'export' in module");
        return false;
    }
    self.lexer.advance();  // consume 'export'
    return true;
}

fn void Parser.module_append_export(Parser* self, ExprModule* module_expr) {
    parser_module_ensure_export_capacity(module_expr);
    module_expr.exports[module_expr.export_count] = self.get_current_symbol();
    module_expr.export_count++;
    self.lexer.advance();
}

fn void Parser.module_append_body_expr(Parser* self, ExprModule* module_expr) {
    parser_module_ensure_body_capacity(module_expr);
    module_expr.body[module_expr.body_count] = self.parse_expr();
    module_expr.body_count++;
}

fn Expr* Parser.parse_module(Parser* self) {
    if (self.has_error) return null;
    self.lexer.advance();  // consume 'module'

    // Parse module name
    if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected module name after module"); return null; }
    SymbolId name = self.get_current_symbol();
    self.lexer.advance();

    Expr* e = self.alloc_module_expr(name);
    ExprModule* module_expr = e.module_expr;

    // Parse (export sym1 sym2 ...) — required
    if (!self.parse_module_export_keyword()) return null;
    while (self.lexer.current.type == T_SYMBOL) {
        self.module_append_export(module_expr);
    }
    self.expect(T_RPAREN, ")");  // close export list

    // Parse remaining body expressions
    while (self.lexer.current.type != T_RPAREN && !self.lexer.at_end() && !self.has_error) {
        self.module_append_body_expr(module_expr);
    }

    self.expect(T_RPAREN, ")");  // close module
    return e;
}
