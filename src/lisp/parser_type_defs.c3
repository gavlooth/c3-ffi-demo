module lisp;

/**
 * Parse type definition: (define [type] Name (^Type field) ...)
 * or with parent: (define [type] (Name Parent) (^Type field) ...)
 */
fn void Parser.init_deftype_expr(Parser* self, Expr* e) {
    e.tag = E_DEFTYPE;
    e.deftype = mem::malloc(ExprDefType.sizeof);
    e.deftype.has_parent = false;
    e.deftype.field_count = 0;
    e.deftype.type_param_count = 0;
    e.deftype.parent = 0;
}

fn bool Parser.parse_deftype_name_compound(Parser* self, Expr* e) {
    self.lexer.advance();  // consume (
    if (self.lexer.current.type != T_SYMBOL) {
        self.set_error("expected type name");
        return false;
    }
    e.deftype.name = self.get_current_symbol();
    self.lexer.advance();
    while (self.lexer.current.type == T_SYMBOL && e.deftype.type_param_count < MAX_TYPE_PARAMS) {
        e.deftype.type_params[e.deftype.type_param_count] = self.get_current_symbol();
        e.deftype.type_param_count++;
        self.lexer.advance();
    }
    self.expect(T_RPAREN, ")");
    return !self.has_error;
}

fn bool Parser.parse_deftype_name(Parser* self, Expr* e) {
    // Name can be simple: Name or parenthesized: (Name Parent?) or (Name TypeParam...)
    // All extra symbols collected as type_params — disambiguate parent vs type-param at eval time.
    if (self.lexer.current.type == T_LPAREN) {
        return self.parse_deftype_name_compound(e);
    }
    if (self.lexer.current.type == T_SYMBOL) {
        e.deftype.name = self.get_current_symbol();
        self.lexer.advance();
        return true;
    }
    self.set_error("expected type name after [type]");
    return false;
}

fn bool Parser.parse_deftype_typed_field(Parser* self, Expr* e) {
    if (e.deftype.field_count >= MAX_TYPE_FIELDS) {
        self.set_error("too many type fields");
        return false;
    }
    self.lexer.advance();  // consume (
    TypeAnnotation ann = self.parse_type_annotation();
    if (self.lexer.current.type != T_SYMBOL) {
        self.set_error("expected field name");
        return false;
    }
    e.deftype.fields[e.deftype.field_count].name = self.get_current_symbol();
    e.deftype.fields[e.deftype.field_count].type_ann = ann;
    e.deftype.field_count++;
    self.lexer.advance();
    self.expect(T_RPAREN, ")");
    return !self.has_error;
}

fn bool Parser.parse_deftype_bare_field(Parser* self, Expr* e) {
    if (e.deftype.field_count >= MAX_TYPE_FIELDS) {
        self.set_error("too many type fields");
        return false;
    }
    e.deftype.fields[e.deftype.field_count].name = self.get_current_symbol();
    e.deftype.fields[e.deftype.field_count].type_ann.has_annotation = false;
    e.deftype.field_count++;
    self.lexer.advance();
    return true;
}

fn Expr* Parser.parse_deftype(Parser* self, Expr* e) {
    self.init_deftype_expr(e);

    if (!self.parse_deftype_name(e)) return null;

    // Parse fields: (^Type field) or just field
    while (self.lexer.current.type == T_LPAREN && !self.has_error) {
        if (!self.parse_deftype_typed_field(e)) return null;
    }

    // Also allow bare symbol fields (untyped)
    while (self.lexer.current.type == T_SYMBOL && !self.has_error) {
        if (!self.parse_deftype_bare_field(e)) return null;
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

    if (self.lexer.current.type == T_LPAREN) {
        self.lexer.advance();
        if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected union name"); return null; }
        e.defunion.name = self.get_current_symbol();
        self.lexer.advance();
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

    while ((self.lexer.current.type == T_SYMBOL || self.lexer.current.type == T_LPAREN) && !self.has_error) {
        if (e.defunion.variant_count >= 16) { self.set_error("too many union variants"); return null; }
        UnionVariant* v = &e.defunion.variants[e.defunion.variant_count];
        v.field_count = 0;

        if (self.lexer.current.type == T_LPAREN) {
            self.lexer.advance();
            if (self.lexer.current.type != T_SYMBOL) { self.set_error("expected variant name"); return null; }
            v.name = self.get_current_symbol();
            self.lexer.advance();
            while (self.lexer.current.type == T_SYMBOL && v.field_count < MAX_TYPE_PARAMS) {
                v.fields[v.field_count] = self.get_current_symbol();
                v.field_count++;
                self.lexer.advance();
            }
            self.expect(T_RPAREN, ")");
        } else {
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

    if (self.lexer.current.type == T_SYMBOL && self.lexer.current.text[0] == '^') {
        e.defalias.target = self.parse_type_annotation();
    } else if (self.lexer.current.type == T_SYMBOL) {
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

    if (self.lexer.current.type != T_LPAREN) {
        self.set_error("expected ( after [effect]");
        return null;
    }
    self.lexer.advance();  // consume '('

    if (self.lexer.current.type != T_SYMBOL) {
        self.set_error("expected effect tag name");
        return null;
    }
    e.defeffect.name = self.get_current_symbol();
    self.lexer.advance();

    if (self.lexer.current.type == T_LPAREN) {
        self.lexer.advance();  // consume '('
        if (self.lexer.current.type == T_SYMBOL && self.lexer.current.text[0] == '^') {
            e.defeffect.arg_type = self.parse_type_annotation();
            e.defeffect.has_arg_type = true;
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
