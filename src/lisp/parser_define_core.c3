module lisp;

import std::collections::list;

/**
 * Parse a named let (loop).
 *
 * (let name (x init-x y init-y) body...)
 *
 * Desugars to:
 * (let ^rec (name (lambda (x y) body)) (name init-x init-y))
 */
fn bool Parser.parse_named_let_bindings(Parser* self, List{SymbolId}* params, List{Expr*}* inits) {
    self.expect(T_LPAREN, "(");  // consume '(' of bindings list
    while (self.lexer.current.type == T_SYMBOL && !self.has_error) {
        (*params).push(self.get_current_symbol());
        self.lexer.advance();
        (*inits).push(self.parse_expr());
    }
    if (self.has_error) return false;

    self.expect(T_RPAREN, ")");  // consume ')' of bindings list
    if (self.has_error) return false;
    if ((*params).len() < 1) {
        self.set_error("expected at least one binding in named let");
        return false;
    }
    return true;
}

fn Expr* Parser.build_named_let_lambda(Parser* self, Expr* e, List{SymbolId}* params, Expr* body) {
    usz param_count = (*params).len();
    Expr* lambda_expr = self.interp.alloc_expr();
    lambda_expr.tag = E_LAMBDA;
    lambda_expr.lambda = mem::malloc(ExprLambda.sizeof);
    lambda_expr.loc_line = e.loc_line;
    lambda_expr.loc_column = e.loc_column;
    lambda_expr.lambda.param = (*params)[0];
    lambda_expr.lambda.param_count = param_count;
    lambda_expr.lambda.params = (SymbolId*)mem::malloc(SymbolId.sizeof * param_count);
    for (usz i = 0; i < param_count; i++) {
        lambda_expr.lambda.params[i] = (*params)[i];
    }
    lambda_expr.lambda.has_rest = false;
    lambda_expr.lambda.rest_param = 0;
    lambda_expr.lambda.has_typed_params = false;
    lambda_expr.lambda.param_annotations = null;
    lambda_expr.lambda.body = body;
    return lambda_expr;
}

fn Expr* Parser.build_named_let_call(Parser* self, Expr* e, SymbolId loop_name, List{Expr*}* inits) {
    usz arg_count = (*inits).len();
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
    call_expr.call.arg_count = arg_count;
    call_expr.call.args = (Expr**)mem::malloc(Expr*.sizeof * arg_count);
    for (usz i = 0; i < arg_count; i++) {
        call_expr.call.args[i] = (*inits)[i];
    }
    return call_expr;
}

fn Expr* Parser.parse_named_let(Parser* self, Expr* e, SymbolId loop_name) {
    if (self.has_error) return null;
    List{SymbolId} params;
    List{Expr*} inits;
    if (!self.parse_named_let_bindings(&params, &inits)) {
        params.free();
        inits.free();
        return null;
    }

    // Parse body (implicit begin for multiple expressions)
    Expr* body = self.parse_implicit_begin();
    self.expect(T_RPAREN, ")");  // consume outer ')' of let form

    Expr* lambda_expr = self.build_named_let_lambda(e, &params, body);
    Expr* call_expr = self.build_named_let_call(e, loop_name, &inits);
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

fn bool Parser.parse_shorthand_rest_param(Parser* self, bool* has_rest_out, SymbolId* rest_param_out) {
    *has_rest_out = false;
    *rest_param_out = 0;
    if (self.lexer.current.type != T_DOTDOT) return true;

    self.lexer.advance();  // consume '..'
    if (self.lexer.current.type != T_SYMBOL) {
        self.set_error("expected rest parameter name after ..");
        return false;
    }

    *rest_param_out = self.get_current_symbol();
    *has_rest_out = true;
    self.lexer.advance();
    return true;
}

fn Expr* Parser.parse_shorthand_define_body(
    Parser* self,
    Expr* e,
    Pattern** destr_patterns,
    SymbolId* destr_param_names,
    usz destr_count
) {
    self.expect(T_RPAREN, ")");  // close param list
    Expr* body = self.parse_implicit_begin();
    self.expect(T_RPAREN, ")");
    if (self.has_error) return null;
    return self.wrap_body_with_destructuring(e, body, destr_patterns, destr_param_names, destr_count);
}

fn Expr* Parser.build_shorthand_define_lambda(
    Parser* self,
    Expr* e,
    List{SymbolId} params,
    List{TypeAnnotation} define_param_anns,
    bool define_has_typed,
    bool define_has_rest,
    SymbolId define_rest_param,
    Expr* body
) {
    Expr* lambda_expr = self.interp.alloc_expr();
    lambda_expr.loc_line = e.loc_line;
    lambda_expr.loc_column = e.loc_column;
    self.finalize_lambda_expr(
        lambda_expr,
        params,
        define_param_anns,
        define_has_typed,
        define_has_rest,
        define_rest_param,
        body
    );
    return lambda_expr;
}

fn Expr* Parser.parse_shorthand_define(Parser* self, Expr* e) {
    if (self.lexer.current.type != T_SYMBOL) {
        self.set_error("expected function name in shorthand define");
        return null;
    }
    SymbolId name = self.get_current_symbol();
    self.lexer.advance();

    List{SymbolId} params;
    List{TypeAnnotation} define_param_anns;
    bool define_has_typed = false;
    bool define_has_rest = false;
    SymbolId define_rest_param = 0;
    Pattern*[8] destr_patterns;
    SymbolId[8] destr_param_names;
    usz destr_count = 0;

    if (!self.collect_callable_params(&params, &define_param_anns, &define_has_typed, &destr_patterns[0], &destr_param_names[0], &destr_count)) {
        params.free();
        define_param_anns.free();
        return null;
    }

    if (!self.parse_shorthand_rest_param(&define_has_rest, &define_rest_param)) {
        params.free();
        define_param_anns.free();
        return null;
    }

    Expr* body = self.parse_shorthand_define_body(e, &destr_patterns[0], &destr_param_names[0], destr_count);
    if (body == null) {
        params.free();
        define_param_anns.free();
        return null;
    }

    Expr* lambda_expr = self.build_shorthand_define_lambda(
        e,
        params,
        define_param_anns,
        define_has_typed,
        define_has_rest,
        define_rest_param,
        body
    );
    params.free();
    define_param_anns.free();

    e.tag = E_DEFINE;
    e.define.name = name;
    e.define.value = lambda_expr;
    return e;
}

fn bool Parser.parse_define_attrs(Parser* self, SymbolId[8]* attrs, usz* attr_count_out) {
    usz attr_count = 0;
    while (self.lexer.current.type == T_SYMBOL && attr_count < 8) {
        (*attrs)[attr_count] = self.get_current_symbol();
        attr_count++;
        self.lexer.advance();
    }
    if (attr_count == 0) {
        self.set_error("expected attribute after [");
        return false;
    }

    self.expect(T_RBRACKET, "]");
    if (self.has_error) return false;

    *attr_count_out = attr_count;
    return true;
}

fn Expr* Parser.parse_define_with_annotation(Parser* self, Expr* e, SymbolId[8]* attrs, usz attr_count) {
    SymbolId annotation = (*attrs)[0];

    // Type-related defines: [type], [abstract], [union], [alias]
    if (annotation == self.interp.sym_type ||
        annotation == self.interp.sym_abstract ||
        annotation == self.interp.sym_union ||
        annotation == self.interp.sym_alias) {
        return self.parse_define_type(e, annotation);
    }

    // FFI defines: [ffi lib], [ffi λ]
    if (annotation == self.interp.sym_ffi) {
        return self.parse_define_ffi(e, attrs, attr_count);
    }

    // Special defines: [macro], [effect], [schema], [relation]
    return self.parse_define_special(e, annotation, attrs, attr_count);
}

fn Expr* Parser.parse_normal_define(Parser* self, Expr* e) {
    if (self.lexer.current.type != T_SYMBOL) {
        self.set_error("expected name after define");
        return null;
    }

    SymbolId name = self.get_current_symbol();
    self.lexer.advance();
    Expr* value = self.parse_expr();
    self.expect(T_RPAREN, ")");

    e.tag = E_DEFINE;
    e.define.name = name;
    e.define.value = value;
    return e;
}

fn Expr* Parser.parse_define(Parser* self) {
    if (self.has_error) return null;
    Expr* e = self.alloc_expr_here();  // Capture 'define' location
    self.lexer.advance();  // consume 'define'

    // Check for bracket annotation: (define [attr...] ...)
    if (self.lexer.current.type == T_LBRACKET) {
        self.lexer.advance();  // consume '['
        SymbolId[8] attrs;
        usz attr_count = 0;
        if (!self.parse_define_attrs(&attrs, &attr_count)) return null;
        return self.parse_define_with_annotation(e, &attrs, attr_count);
    }

    // Shorthand define: (define (name params...) body) => (define name (lambda (params...) body))
    // Also supports typed params: (define (name (^Int x) (^String y)) body)
    if (self.lexer.current.type == T_LPAREN) {
        self.lexer.advance();  // consume '('
        return self.parse_shorthand_define(e);
    }

    return self.parse_normal_define(e);
}

/**
 * Handle type-related bracket defines: [type], [abstract], [union], [alias]
 * Dispatches to the appropriate existing sub-parser.
 */
fn Expr* Parser.parse_define_type(Parser* self, Expr* e, SymbolId annotation) {
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
    self.set_error("unknown type define attribute");
    return null;
}

/**
 * Handle FFI bracket defines: [ffi lib], [ffi λ]
 * Dispatches to parse_ffi_lib or parse_ffi_fn.
 */
fn Expr* Parser.parse_define_ffi(Parser* self, Expr* e, SymbolId[8]* attrs, usz attr_count) {
    if (attr_count < 2) { self.set_error("expected 'lib' or 'λ'/'lambda' after [ffi"); return null; }
    if ((*attrs)[1] == self.interp.sym_lib) {
        return self.parse_ffi_lib(e);
    }
    if ((*attrs)[1] == self.interp.sym_lambda) {
        return self.parse_ffi_fn(e, attrs, attr_count);
    }
    // Check for λ (U+03BB, UTF-8: CE BB)
    char[] attr1_text = self.interp.symbols.get_name((*attrs)[1]);
    if (attr1_text.len == 2 && attr1_text[0] == (char)0xCE && attr1_text[1] == (char)0xBB) {
        return self.parse_ffi_fn(e, attrs, attr_count);
    }
    self.set_error("unknown [ffi ...] form: expected 'lib' or 'λ'");
    return null;
}
