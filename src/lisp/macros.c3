module lisp;

import std::io;
import std::collections::list;
import main;

// Quasiquote eval — handled by JIT (jit.c3)

// =============================================================================
// SECTION 2.6: DEFMACRO
// =============================================================================

/**
 * Convert an Expr AST to a Value (quoted list representation).
 * Used for macro expansion: transforms the call AST into data the macro can manipulate.
 */
fn Value* expr_to_value(Expr* expr, Interp* interp) {
    if (expr == null) return make_nil(interp);

    switch (expr.tag) {
        case E_LIT:
            return expr.lit.value;
        case E_VAR:
            return make_symbol(interp, expr.var_expr.name);
        case E_QUOTE: {
            Value* sym = make_symbol(interp, interp.sym_quote);
            Value* rest = make_cons(interp, expr.quote.datum, make_nil(interp));
            return make_cons(interp, sym, rest);
        }
        case E_CALL: {
            // Build (func arg1 arg2 ...)
            Value* result = make_nil(interp);
            // Add args right-to-left
            for (usz i = expr.call.arg_count; i > 0; i--) {
                result = make_cons(interp, expr_to_value(expr.call.args[i - 1], interp), result);
            }
            // Add func
            result = make_cons(interp, expr_to_value(expr.call.func, interp), result);
            return result;
        }
        case E_APP: {
            Value* f = expr_to_value(expr.app.func, interp);
            Value* a = expr_to_value(expr.app.arg, interp);
            Value* rest = make_cons(interp, a, make_nil(interp));
            return make_cons(interp, f, rest);
        }
        case E_IF: {
            Value* sym = make_symbol(interp, interp.sym_if);
            Value* t = expr_to_value(expr.if_expr.test, interp);
            Value* th = expr_to_value(expr.if_expr.then_branch, interp);
            Value* el = expr_to_value(expr.if_expr.else_branch, interp);
            return make_cons(interp, sym, make_cons(interp, t, make_cons(interp, th, make_cons(interp, el, make_nil(interp)))));
        }
        case E_LET: {
            Value* sym = make_symbol(interp, interp.sym_let);
            Value* name_sym = make_symbol(interp, expr.let_expr.name);
            Value* init_val = expr_to_value(expr.let_expr.init, interp);
            Value* binding = make_cons(interp, name_sym, make_cons(interp, init_val, make_nil(interp)));
            Value* bindings = make_cons(interp, binding, make_nil(interp));
            Value* body = expr_to_value(expr.let_expr.body, interp);
            return make_cons(interp, sym, make_cons(interp, bindings, make_cons(interp, body, make_nil(interp))));
        }
        case E_LAMBDA: {
            Value* sym = make_symbol(interp, interp.sym_lambda);
            // Build param list
            Value* params = make_nil(interp);
            if (expr.lambda.param_count > 0) {
                for (usz i = expr.lambda.param_count; i > 0; i--) {
                    params = make_cons(interp, make_symbol(interp, expr.lambda.params[i - 1]), params);
                }
            }
            Value* body = expr_to_value(expr.lambda.body, interp);
            return make_cons(interp, sym, make_cons(interp, params, make_cons(interp, body, make_nil(interp))));
        }
        case E_BEGIN: {
            Value* sym = make_symbol(interp, interp.sym_begin);
            Value* result = make_nil(interp);
            for (usz i = expr.begin.expr_count; i > 0; i--) {
                result = make_cons(interp, expr_to_value(expr.begin.exprs[i - 1], interp), result);
            }
            return make_cons(interp, sym, result);
        }
        case E_SET: {
            Value* sym = make_symbol(interp, interp.sym_set);
            Value* name_sym = make_symbol(interp, expr.set_expr.name);
            Value* val = expr_to_value(expr.set_expr.value, interp);
            return make_cons(interp, sym, make_cons(interp, name_sym, make_cons(interp, val, make_nil(interp))));
        }
        case E_QUASIQUOTE: {
            Value* sym = make_symbol(interp, interp.sym_quasiquote);
            Value* body = expr_to_value(expr.quasiquote.body, interp);
            return make_cons(interp, sym, make_cons(interp, body, make_nil(interp)));
        }
        case E_UNQUOTE: {
            Value* sym = make_symbol(interp, interp.sym_unquote);
            Value* body = expr_to_value(expr.unquote.body, interp);
            return make_cons(interp, sym, make_cons(interp, body, make_nil(interp)));
        }
        case E_UNQUOTE_SPLICING: {
            Value* sym = make_symbol(interp, interp.sym_unquote_splicing);
            Value* body = expr_to_value(expr.unquote_splicing.body, interp);
            return make_cons(interp, sym, make_cons(interp, body, make_nil(interp)));
        }
        case E_DEFINE: {
            Value* sym = make_symbol(interp, interp.sym_define);
            Value* name_sym = make_symbol(interp, expr.define.name);
            Value* val = expr_to_value(expr.define.value, interp);
            return make_cons(interp, sym, make_cons(interp, name_sym, make_cons(interp, val, make_nil(interp))));
        }
        default:
            return make_nil(interp);
    }
}

/**
 * Convert a Value (list representation) back to an Expr AST.
 * Used for macro expansion: transforms the macro output back to executable AST.
 */
fn Expr* value_to_expr(Value* val, Interp* interp) {
    if (val == null || val.tag == NIL) {
        Expr* e = interp.alloc_expr();
        e.tag = E_LIT;
        e.lit.value = make_nil(interp);
        return e;
    }

    if (val.tag == INT || val.tag == STRING || val.tag == DOUBLE) {
        Expr* e = interp.alloc_expr();
        e.tag = E_LIT;
        e.lit.value = val;
        return e;
    }

    if (val.tag == SYMBOL) {
        // Check for special form names
        SymbolId sym = val.sym_val;
        if ((uint)sym == (uint)interp.sym_true || (uint)sym == (uint)interp.sym_false) {
            Expr* e = interp.alloc_expr();
            e.tag = E_VAR;
            e.var_expr.name = sym;
            return e;
        }
        Expr* e = interp.alloc_expr();
        e.tag = E_VAR;
        e.var_expr.name = sym;
        return e;
    }

    if (val.tag == CONS) {
        // It's a list — check if head is a special form symbol
        Value* head = val.cons_val.car;
        if (head != null && head.tag == SYMBOL) {
            SymbolId sym = head.sym_val;
            // Check for special forms and delegate to parser-style construction
            if ((uint)sym == (uint)interp.sym_if) {
                // (if test then else)
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_IF;
                e.if_expr.test = value_to_expr(rest.cons_val.car, interp);
                rest = rest.cons_val.cdr;
                e.if_expr.then_branch = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                rest = is_cons(rest) ? rest.cons_val.cdr : rest;
                e.if_expr.else_branch = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_quote) {
                Value* rest = val.cons_val.cdr;
                Expr* e = interp.alloc_expr();
                e.tag = E_QUOTE;
                e.quote.datum = is_cons(rest) ? rest.cons_val.car : make_nil(interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_begin) {
                Value* rest = val.cons_val.cdr;
                List{Expr*} items;
                while (is_cons(rest)) {
                    items.push(value_to_expr(rest.cons_val.car, interp));
                    rest = rest.cons_val.cdr;
                }
                Expr* e = interp.alloc_expr();
                e.tag = E_BEGIN;
                e.begin = mem::malloc(ExprBegin.sizeof);
                e.begin.expr_count = items.len();
                e.begin.exprs = (Expr**)mem::malloc(Expr*.sizeof * items.len());
                for (usz i = 0; i < items.len(); i++) { e.begin.exprs[i] = items[i]; }
                items.free();
                return e;
            }
            if ((uint)sym == (uint)interp.sym_define) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_DEFINE;
                Value* name_val = rest.cons_val.car;
                e.define.name = (name_val != null && name_val.tag == SYMBOL) ? name_val.sym_val : 0;
                rest = rest.cons_val.cdr;
                e.define.value = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_lambda) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Value* params_list = rest.cons_val.car;
                rest = rest.cons_val.cdr;
                Expr* body = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);

                // Collect params
                List{SymbolId} params;
                Value* p = params_list;
                while (is_cons(p)) {
                    if (p.cons_val.car != null && p.cons_val.car.tag == SYMBOL) {
                        params.push(p.cons_val.car.sym_val);
                    }
                    p = p.cons_val.cdr;
                }
                usz param_count = params.len();

                if (param_count == 0) {
                    // Zero-arg lambda
                    params.free();
                    Expr* e = interp.alloc_expr();
                    e.tag = E_LAMBDA;
                    e.lambda = mem::malloc(ExprLambda.sizeof);
                    e.lambda.param = (SymbolId)0xFFFFFFFF;
                    e.lambda.param_count = 0;
                    e.lambda.params = null;
                    e.lambda.has_rest = false;
                    e.lambda.rest_param = 0;
                    e.lambda.has_typed_params = false;
                    e.lambda.param_annotations = null;
                    e.lambda.body = body;
                    return e;
                }
                if (param_count == 1) {
                    Expr* e = interp.alloc_expr();
                    e.tag = E_LAMBDA;
                    e.lambda = mem::malloc(ExprLambda.sizeof);
                    e.lambda.param = params[0];
                    e.lambda.param_count = 1;
                    e.lambda.params = (SymbolId*)mem::malloc(SymbolId.sizeof * 1);
                    e.lambda.params[0] = params[0];
                    e.lambda.has_rest = false;
                    e.lambda.rest_param = 0;
                    e.lambda.has_typed_params = false;
                    e.lambda.param_annotations = null;
                    e.lambda.body = body;
                    params.free();
                    return e;
                }
                // Multi-param: desugar to nested lambdas
                Expr* inner = body;
                for (usz i = param_count; i > 1; i--) {
                    Expr* wrapper = interp.alloc_expr();
                    wrapper.tag = E_LAMBDA;
                    wrapper.lambda = mem::malloc(ExprLambda.sizeof);
                    wrapper.lambda.param = params[i - 1];
                    wrapper.lambda.param_count = 1;
                    wrapper.lambda.params = (SymbolId*)mem::malloc(SymbolId.sizeof * 1);
                    wrapper.lambda.params[0] = params[i - 1];
                    wrapper.lambda.has_rest = false;
                    wrapper.lambda.rest_param = 0;
                    wrapper.lambda.has_typed_params = false;
                    wrapper.lambda.param_annotations = null;
                    wrapper.lambda.body = inner;
                    inner = wrapper;
                }
                Expr* e = interp.alloc_expr();
                e.tag = E_LAMBDA;
                e.lambda = mem::malloc(ExprLambda.sizeof);
                e.lambda.param = params[0];
                e.lambda.param_count = 1;
                e.lambda.params = (SymbolId*)mem::malloc(SymbolId.sizeof * 1);
                e.lambda.params[0] = params[0];
                e.lambda.has_rest = false;
                e.lambda.rest_param = 0;
                e.lambda.has_typed_params = false;
                e.lambda.param_annotations = null;
                e.lambda.body = inner;
                params.free();
                return e;
            }
            if ((uint)sym == (uint)interp.sym_let) {
                // (let (name1 val1 name2 val2 ...) body)
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Value* bindings_list = rest.cons_val.car;
                rest = rest.cons_val.cdr;
                Expr* body = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                // Build nested lets from flat-pair bindings
                if (!is_cons(bindings_list)) {
                    return body;
                }
                // Collect flat-pair bindings: name val name val ...
                List{SymbolId} names;
                List{Value*} inits;
                Value* bp = bindings_list;
                while (is_cons(bp)) {
                    Value* name_v = bp.cons_val.car;
                    if (name_v != null && name_v.tag == SYMBOL) {
                        names.push(name_v.sym_val);
                        bp = bp.cons_val.cdr;
                        if (is_cons(bp)) {
                            inits.push(bp.cons_val.car);
                            bp = bp.cons_val.cdr;
                        } else {
                            inits.push(make_nil(interp));
                        }
                    } else {
                        bp = bp.cons_val.cdr;
                    }
                }
                // Build from inside out
                Expr* inner_body = body;
                for (usz i = names.len(); i > 0; i--) {
                    Expr* e = interp.alloc_expr();
                    e.tag = E_LET;
                    e.let_expr.name = names[i - 1];
                    e.let_expr.init = value_to_expr(inits[i - 1], interp);
                    e.let_expr.body = inner_body;
                    e.let_expr.is_recursive = false;
                    inner_body = e;
                }
                names.free(); inits.free();
                return inner_body;
            }
            if ((uint)sym == (uint)interp.sym_set) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_SET;
                Value* name_val = rest.cons_val.car;
                e.set_expr.name = (name_val != null && name_val.tag == SYMBOL) ? name_val.sym_val : 0;
                rest = rest.cons_val.cdr;
                e.set_expr.value = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_and) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_AND;
                e.and_expr.left = value_to_expr(rest.cons_val.car, interp);
                rest = rest.cons_val.cdr;
                e.and_expr.right = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_or) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_OR;
                e.or_expr.left = value_to_expr(rest.cons_val.car, interp);
                rest = rest.cons_val.cdr;
                e.or_expr.right = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_quasiquote) {
                Value* rest = val.cons_val.cdr;
                Expr* e = interp.alloc_expr();
                e.tag = E_QUASIQUOTE;
                e.quasiquote.body = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_unquote) {
                Value* rest = val.cons_val.cdr;
                Expr* e = interp.alloc_expr();
                e.tag = E_UNQUOTE;
                e.unquote.body = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_unquote_splicing) {
                Value* rest = val.cons_val.cdr;
                Expr* e = interp.alloc_expr();
                e.tag = E_UNQUOTE_SPLICING;
                e.unquote_splicing.body = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
            if ((uint)sym == (uint)interp.sym_signal) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Expr* e = interp.alloc_expr();
                e.tag = E_PERFORM;
                Value* tag_val = rest.cons_val.car;
                e.perform.tag = (tag_val != null && tag_val.tag == SYMBOL) ? tag_val.sym_val : 0;
                rest = rest.cons_val.cdr;
                e.perform.arg = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                return e;
            }
        }

        // Generic function call: (func arg1 arg2 ...)
        // Count the list length
        usz len = list_length(val);
        if (len == 0) {
            Expr* e = interp.alloc_expr();
            e.tag = E_LIT;
            e.lit.value = make_nil(interp);
            return e;
        }

        Expr* func_e = value_to_expr(val.cons_val.car, interp);
        List{Expr*} args;
        Value* rest = val.cons_val.cdr;
        while (is_cons(rest)) {
            args.push(value_to_expr(rest.cons_val.car, interp));
            rest = rest.cons_val.cdr;
        }
        Expr* e = interp.alloc_expr();
        e.tag = E_CALL;
        e.call = mem::malloc(ExprCall.sizeof);
        e.call.func = func_e;
        e.call.arg_count = args.len();
        e.call.args = (Expr**)mem::malloc(Expr*.sizeof * args.len());
        for (usz i = 0; i < args.len(); i++) { e.call.args[i] = args[i]; }
        args.free();
        return e;
    }

    // Fallback: return as literal
    Expr* e = interp.alloc_expr();
    e.tag = E_LIT;
    e.lit.value = val;
    return e;
}

/**
 * Check if a symbol is a registered macro.
 * Returns the macro definition, or null if not a macro.
 */
fn MacroDef* lookup_macro(SymbolId name, Interp* interp) {
    usz hash_slot = (usz)name % interp.macro_hash_capacity;
    for (usz probe = 0; probe < interp.macro_hash_capacity; probe++) {
        usz idx = interp.macro_hash_index[hash_slot];
        if (idx == usz.max) return null;  // Empty slot — not found
        if ((uint)interp.macro_table[idx].name == (uint)name) {
            return &interp.macro_table[idx];
        }
        hash_slot = (hash_slot + 1) % interp.macro_hash_capacity;
    }
    return null;
}

/**
 * Collect pattern variable names from a Pattern into a flat array.
 * Used by eval_define_macro to distinguish pattern vars from template literals.
 */
fn void collect_pattern_vars(Pattern* pat, SymbolId[]* vars, usz* count) {
    if (pat == null) return;
    if (*count >= 32) {
        io::printfn("WARNING: macro has more than 32 pattern variables, some bindings may be lost");
        return;
    }
    switch (pat.tag) {
        case PAT_VAR: {
            (*vars)[*count] = pat.var_name;
            (*count)++;
        }
        case PAT_CONS: {
            collect_pattern_vars(pat.car_pat, vars, count);
            collect_pattern_vars(pat.cdr_pat, vars, count);
        }
        case PAT_SEQ: {
            for (usz i = 0; i < pat.elem_count; i++) {
                collect_pattern_vars(pat.elements[i], vars, count);
            }
            // Rest binding is also a pattern var
            if (pat.rest_pos != REST_NONE && *count < 32) {
                (*vars)[*count] = pat.rest_binding;
                (*count)++;
            }
        }
        case PAT_CONSTRUCTOR: {
            for (usz i = 0; i < pat.ctor_sub_count; i++) {
                collect_pattern_vars(pat.ctor_sub_patterns[i], vars, count);
            }
        }
        default: {}
    }
}

/**
 * Walk a template Value* tree and capture bindings for non-pattern-var,
 * non-gensym, non-special-form symbols from the current global env.
 * This creates a SNAPSHOT of the definition-time bindings.
 */
fn void capture_template_bindings(Value* tmpl, SymbolId* pat_vars, usz pat_var_count,
                                  CapturedBinding* bindings, usz* binding_count,
                                  Interp* interp) {
    if (tmpl == null) return;
    if (*binding_count >= 32) {
        io::printfn("WARNING: macro captures more than 32 bindings, some hygiene may be lost");
        return;
    }

    if (tmpl.tag == SYMBOL) {
        SymbolId sym = tmpl.sym_val;

        // Skip pattern variables
        for (usz i = 0; i < pat_var_count; i++) {
            if ((uint)pat_vars[i] == (uint)sym) return;
        }

        // Skip auto-gensym symbols (ending with #)
        char[] sym_name = interp.symbols.get_name(sym);
        if (sym_name.len > 0 && sym_name[sym_name.len - 1] == '#') return;

        // Skip special form symbols
        if (is_special_form_symbol(sym, interp)) return;

        // Skip if already captured
        for (usz i = 0; i < *binding_count; i++) {
            if ((uint)bindings[i].sym == (uint)sym) return;
        }

        // Look up in global env — if defined, capture it
        if (interp.global_env != null) {
            Value* val = interp.global_env.lookup(sym);
            if (val != null && *binding_count < 32) {
                bindings[*binding_count].sym = sym;
                bindings[*binding_count].value = val;
                (*binding_count)++;
            }
        }
        return;
    }

    if (tmpl.tag == CONS) {
        capture_template_bindings(tmpl.cons_val.car, pat_vars, pat_var_count,
                                  bindings, binding_count, interp);
        capture_template_bindings(tmpl.cons_val.cdr, pat_vars, pat_var_count,
                                  bindings, binding_count, interp);
    }
}

/**
 * Evaluate a define-macro expression.
 * (define [macro] name (pattern1 template1) ...)
 * Captures definition-time bindings for template hygiene.
 */
<* @require expr != null : "eval_define_macro: null expr"
   @require interp != null : "eval_define_macro: null interp" *>
fn EvalResult eval_define_macro(Expr* expr, Env* env, Interp* interp) {
    if (interp.macro_count >= interp.macro_capacity) interp.grow_macro_table();
    MacroDef* mdef = &interp.macro_table[interp.macro_count];
    mdef.name = expr.define_macro.name;
    mdef.clause_count = expr.define_macro.clause_count;
    mdef.captured_count = 0;

    for (usz i = 0; i < expr.define_macro.clause_count; i++) {
        mdef.clauses[i] = expr.define_macro.clauses[i];
    }

    // Capture definition-time bindings for hygiene.
    // For each clause, collect pattern vars, then scan template for literals
    // that have current global-env bindings, and snapshot those values.
    for (usz i = 0; i < expr.define_macro.clause_count; i++) {
        // Collect pattern variable names for this clause
        SymbolId[32] pat_vars;
        usz pat_var_count = 0;
        SymbolId[] pat_vars_slice = &pat_vars;
        collect_pattern_vars(mdef.clauses[i].pattern, &pat_vars_slice, &pat_var_count);

        // Walk template and capture bindings
        main::ScopeRegion* saved_scope = interp.current_scope;
        interp.current_scope = interp.root_scope;
        capture_template_bindings(mdef.clauses[i].tmpl, &pat_vars, pat_var_count,
                                  &mdef.captured_bindings, &mdef.captured_count, interp);
        interp.current_scope = saved_scope;
    }

    interp.macro_count++;

    // Insert into macro hash index
    usz macro_idx = interp.macro_count - 1;
    usz hash_slot = (usz)mdef.name % interp.macro_hash_capacity;
    for (;;) {
        usz existing = interp.macro_hash_index[hash_slot];
        if (existing == usz.max) {
            // Empty slot — insert here
            interp.macro_hash_index[hash_slot] = macro_idx;
            break;
        }
        if ((uint)interp.macro_table[existing].name == (uint)mdef.name) {
            // Same name (redefinition) — update to point to new entry
            interp.macro_hash_index[hash_slot] = macro_idx;
            break;
        }
        hash_slot = (hash_slot + 1) % interp.macro_hash_capacity;
    }

    return eval_ok(make_nil(interp));
}

/**
 * expand_macros_in_expr — Pre-JIT macro expansion pass.
 *
 * Walks an AST and expands macros before JIT compilation. Within E_BEGIN
 * blocks, processes sequentially: E_DEFMACRO children are evaluated to register
 * the macro, then subsequent E_CALL children are expanded if they invoke a
 * known macro.
 *
 * Returns the (possibly transformed) expression. Mutates the tree in-place
 * where possible (e.g. replacing E_BEGIN children).
 */
fn Expr* expand_macros_in_expr(Expr* expr, Interp* interp) {
    if (expr == null) return expr;

    switch (expr.tag) {
        case E_BEGIN: {
            for (usz i = 0; i < expr.begin.expr_count; i++) {
                Expr* child = expr.begin.exprs[i];
                if (child != null && child.tag == E_DEFMACRO) {
                    // Register macro immediately so subsequent exprs can use it
                    eval_define_macro(child, interp.global_env, interp);
                } else {
                    expr.begin.exprs[i] = expand_macros_in_expr(child, interp);
                }
            }
            return expr;
        }
        case E_CALL: {
            if (expr.call.func != null && expr.call.func.tag == E_VAR) {
                MacroDef* mdef = lookup_macro(expr.call.func.var_expr.name, interp);
                if (mdef != null) {
                    EvalResult r = expand_pattern_macro(mdef, expr, interp);
                    if (!r.error.has_error) {
                        Expr* expanded = value_to_expr(r.value, interp);
                        return expand_macros_in_expr(expanded, interp);
                    }
                }
            }
            // Recurse into function and args
            expr.call.func = expand_macros_in_expr(expr.call.func, interp);
            for (usz i = 0; i < expr.call.arg_count; i++) {
                expr.call.args[i] = expand_macros_in_expr(expr.call.args[i], interp);
            }
            return expr;
        }
        case E_IF: {
            expr.if_expr.test = expand_macros_in_expr(expr.if_expr.test, interp);
            expr.if_expr.then_branch = expand_macros_in_expr(expr.if_expr.then_branch, interp);
            expr.if_expr.else_branch = expand_macros_in_expr(expr.if_expr.else_branch, interp);
            return expr;
        }
        case E_LET: {
            expr.let_expr.init = expand_macros_in_expr(expr.let_expr.init, interp);
            expr.let_expr.body = expand_macros_in_expr(expr.let_expr.body, interp);
            return expr;
        }
        case E_DEFINE: {
            expr.define.value = expand_macros_in_expr(expr.define.value, interp);
            return expr;
        }
        case E_LAMBDA: {
            expr.lambda.body = expand_macros_in_expr(expr.lambda.body, interp);
            return expr;
        }
        case E_APP: {
            expr.app.func = expand_macros_in_expr(expr.app.func, interp);
            expr.app.arg = expand_macros_in_expr(expr.app.arg, interp);
            return expr;
        }
        case E_AND: {
            expr.and_expr.left = expand_macros_in_expr(expr.and_expr.left, interp);
            expr.and_expr.right = expand_macros_in_expr(expr.and_expr.right, interp);
            return expr;
        }
        case E_OR: {
            expr.or_expr.left = expand_macros_in_expr(expr.or_expr.left, interp);
            expr.or_expr.right = expand_macros_in_expr(expr.or_expr.right, interp);
            return expr;
        }
        case E_SET: {
            expr.set_expr.value = expand_macros_in_expr(expr.set_expr.value, interp);
            return expr;
        }
        case E_RESET: {
            expr.reset.body = expand_macros_in_expr(expr.reset.body, interp);
            return expr;
        }
        case E_SHIFT: {
            expr.shift.body = expand_macros_in_expr(expr.shift.body, interp);
            return expr;
        }
        case E_PERFORM: {
            expr.perform.arg = expand_macros_in_expr(expr.perform.arg, interp);
            return expr;
        }
        case E_RESOLVE: {
            expr.resolve.value = expand_macros_in_expr(expr.resolve.value, interp);
            return expr;
        }
        case E_HANDLE: {
            expr.handle.body = expand_macros_in_expr(expr.handle.body, interp);
            for (usz i = 0; i < expr.handle.clause_count; i++) {
                expr.handle.clauses[i].handler_body = expand_macros_in_expr(expr.handle.clauses[i].handler_body, interp);
            }
            return expr;
        }
        case E_MATCH: {
            expr.match.scrutinee = expand_macros_in_expr(expr.match.scrutinee, interp);
            for (usz i = 0; i < expr.match.clause_count; i++) {
                expr.match.clauses[i].result = expand_macros_in_expr(expr.match.clauses[i].result, interp);
            }
            return expr;
        }
        case E_INDEX: {
            expr.index.collection = expand_macros_in_expr(expr.index.collection, interp);
            expr.index.index = expand_macros_in_expr(expr.index.index, interp);
            return expr;
        }
        case E_MODULE: {
            for (usz i = 0; i < expr.module_expr.body_count; i++) {
                Expr* child = expr.module_expr.body[i];
                if (child != null && child.tag == E_DEFMACRO) {
                    eval_define_macro(child, interp.global_env, interp);
                } else {
                    expr.module_expr.body[i] = expand_macros_in_expr(child, interp);
                }
            }
            return expr;
        }
        default:
            // E_LIT, E_VAR, E_QUOTE, E_PATH, E_IMPORT, E_DEFTYPE,
            // E_DEFABSTRACT, E_DEFUNION, E_DEFALIAS, E_DEFEFFECT,
            // E_DEFMACRO, E_QUASIQUOTE, E_UNQUOTE, E_UNQUOTE_SPLICING
            return expr;
    }
}

/**
 * Check if a symbol is a special form keyword (if, begin, let, lambda, etc.).
 * Special forms are recognized by SymbolId in value_to_expr, so they don't need
 * hygienic resolution — they always produce the correct Expr tag regardless of
 * any user-defined shadowing.
 */
fn bool is_special_form_symbol(SymbolId sym, Interp* interp) {
    return (uint)sym == (uint)interp.sym_if
        || (uint)sym == (uint)interp.sym_begin
        || (uint)sym == (uint)interp.sym_let
        || (uint)sym == (uint)interp.sym_lambda
        || (uint)sym == (uint)interp.sym_define
        || (uint)sym == (uint)interp.sym_quote
        || (uint)sym == (uint)interp.sym_set
        || (uint)sym == (uint)interp.sym_and
        || (uint)sym == (uint)interp.sym_or
        || (uint)sym == (uint)interp.sym_reset
        || (uint)sym == (uint)interp.sym_shift
        || (uint)sym == (uint)interp.sym_signal
        || (uint)sym == (uint)interp.sym_handle
        || (uint)sym == (uint)interp.sym_match
        || (uint)sym == (uint)interp.sym_quasiquote
        || (uint)sym == (uint)interp.sym_unquote
        || (uint)sym == (uint)interp.sym_unquote_splicing
        || (uint)sym == (uint)interp.sym_defmacro
        || (uint)sym == (uint)interp.sym_dotdot
        || (uint)sym == (uint)interp.sym_macro
        || (uint)sym == (uint)interp.sym_true
        || (uint)sym == (uint)interp.sym_false
        || (uint)sym == (uint)interp.sym_module
        || (uint)sym == (uint)interp.sym_import
        || (uint)sym == (uint)interp.sym_export;
}

/**
 * Expand a pattern-based macro.
 * Converts macro call args to a value list, tries each clause's pattern,
 * and on first match, expands the template with bindings.
 */
fn EvalResult expand_pattern_macro(MacroDef* mdef, Expr* call_expr, Interp* interp) {
    // Convert each arg Expr* to Value* and build into a cons list
    usz arg_count = call_expr.call.arg_count;
    Value* args_list = make_nil(interp);
    // Build right-to-left to get proper list order
    for (usz i = arg_count; i > 0; i--) {
        Value* arg_val = expr_to_value(call_expr.call.args[i - 1], interp);
        args_list = make_cons(interp, arg_val, args_list);
    }

    // Try each clause
    for (usz i = 0; i < mdef.clause_count; i++) {
        MatchResult match_result = match_pattern(mdef.clauses[i].pattern, args_list, interp);
        if (match_result.matched) {
            // Expand template with bindings; fresh gensym table per expansion
            GensymTable gensyms;
            gensyms.count = 0;
            Value* expanded = expand_template(mdef.clauses[i].tmpl, &match_result, interp, &gensyms,
                                              &mdef.captured_bindings, mdef.captured_count);
            match_result.cleanup();
            return eval_ok(expanded);
        }
    }

    return eval_error("no macro clause matched");
}

/**
 * Look up or create a gensym for an auto-gensym symbol (ending with #).
 * Within a single macro expansion, the same #-symbol always maps to the same gensym.
 */
fn SymbolId lookup_or_create_gensym(SymbolId original, GensymTable* table, Interp* interp) {
    // Check if already mapped
    for (usz i = 0; i < table.count; i++) {
        if ((uint)table.mappings[i].original == (uint)original) {
            return table.mappings[i].generated;
        }
    }
    // Create new gensym
    char[32] buf;
    usz len = 0;
    buf[0] = 'g'; buf[1] = '#'; len = 2;
    // Convert counter to string
    interp.gensym_counter++;
    usz counter = interp.gensym_counter;
    if (counter == 0) {
        buf[len++] = '0';
    } else {
        char[16] digits;
        usz dcount = 0;
        usz tmp = counter;
        while (tmp > 0) {
            digits[dcount++] = (char)('0' + (tmp % 10));
            tmp /= 10;
        }
        for (usz i = dcount; i > 0; i--) {
            buf[len++] = digits[i - 1];
        }
    }
    SymbolId gsym = interp.symbols.intern(buf[:len]);
    if (gsym == INVALID_SYMBOL_ID) return INVALID_SYMBOL_ID;
    // Store mapping
    assert(table.count < 16, "too many auto-gensyms in macro template");
    table.mappings[table.count].original = original;
    table.mappings[table.count].generated = gsym;
    table.count++;
    return gsym;
}

/**
 * Expand a macro template with pattern bindings.
 * Symbols in bindings are substituted; literal symbols stay.
 * ".." before a symbol splices the bound list.
 * Symbols ending with # are auto-replaced with unique gensyms for hygiene.
 *
 * HYGIENE: Template literal symbols (not pattern vars, not gensyms, not special forms)
 * are resolved through captured_bindings — a snapshot of definition-time values.
 * This prevents expansion-site shadowing from capturing macro-internal references.
 */
fn Value* expand_template(Value* tmpl, MatchResult* bindings, Interp* interp, GensymTable* gensyms,
                          CapturedBinding* captured, usz captured_count) {
    if (tmpl == null) return make_nil(interp);

    switch (tmpl.tag) {
        case SYMBOL: {
            // 1. Check if this symbol is bound in the pattern — substitute
            for (usz i = 0; i < bindings.binding_count; i++) {
                if ((uint)bindings.bindings[i].name == (uint)tmpl.sym_val) {
                    return bindings.bindings[i].value;
                }
            }
            // 2. Check for auto-gensym (symbol ending with #) — generate unique name
            char[] sym_name = interp.symbols.get_name(tmpl.sym_val);
            if (sym_name.len > 0 && sym_name[sym_name.len - 1] == '#') {
                SymbolId gsym = lookup_or_create_gensym(tmpl.sym_val, gensyms, interp);
                return make_symbol(interp, gsym);
            }
            // 3. Hygienic resolution: check captured definition-time bindings.
            //    If this symbol was bound at macro definition time, embed the
            //    snapshotted value directly to prevent expansion-site shadowing.
            for (usz i = 0; i < captured_count; i++) {
                if ((uint)captured[i].sym == (uint)tmpl.sym_val) {
                    return captured[i].value;
                }
            }
            return tmpl;
        }
        case CONS:
            return expand_template_list(tmpl, bindings, interp, gensyms, captured, captured_count);
        default:
            // INT, STRING, NIL — return as-is
            return tmpl;
    }
}

/**
 * Expand a template list, handling ".." splice.
 * When we see (.. var rest...) in a list, we splice var's bound value.
 */
fn Value* expand_template_list(Value* list, MatchResult* bindings, Interp* interp, GensymTable* gensyms,
                               CapturedBinding* captured, usz captured_count) {
    if (is_nil(list)) return make_nil(interp);
    if (!is_cons(list)) return expand_template(list, bindings, interp, gensyms, captured, captured_count);

    Value* head = list.cons_val.car;

    // Check for splice: (.. var ...)
    if (head != null && head.tag == SYMBOL && head.sym_val == interp.sym_dotdot) {
        // Next element is the variable to splice
        Value* rest_after_dotdot = list.cons_val.cdr;
        if (is_cons(rest_after_dotdot)) {
            Value* var_sym = rest_after_dotdot.cons_val.car;
            Value* remaining = rest_after_dotdot.cons_val.cdr;

            // Look up the variable in bindings
            Value* spliced = make_nil(interp);
            if (var_sym != null && var_sym.tag == SYMBOL) {
                for (usz i = 0; i < bindings.binding_count; i++) {
                    if ((uint)bindings.bindings[i].name == (uint)var_sym.sym_val) {
                        spliced = bindings.bindings[i].value;
                        break;
                    }
                }
            }

            // Expand the rest of the template
            Value* expanded_rest = expand_template_list(remaining, bindings, interp, gensyms, captured, captured_count);

            // Append spliced list to expanded_rest
            return append_values(spliced, expanded_rest, interp);
        }
    }

    // Normal element: expand car and recurse on cdr
    Value* expanded_car = expand_template(head, bindings, interp, gensyms, captured, captured_count);
    Value* expanded_cdr = expand_template_list(list.cons_val.cdr, bindings, interp, gensyms, captured, captured_count);
    return make_cons(interp, expanded_car, expanded_cdr);
}

/**
 * Append two value lists.
 */
fn Value* append_values(Value* a, Value* b, Interp* interp, usz depth = 0) {
    if (is_nil(a)) return b;
    if (!is_cons(a)) return b;  // not a proper list, just return b
    if (depth >= 10000) return make_nil(interp);
    return make_cons(interp, a.cons_val.car, append_values(a.cons_val.cdr, b, interp, depth + 1));
}

// =============================================================================
// SECTION 2.7: MODULE SYSTEM
// =============================================================================

/**
 * Find a module by name in the module table.
 */
fn Module* find_module(SymbolId name, Interp* interp) {
    usz hash_slot = (usz)name % interp.module_hash_capacity;
    for (usz probe = 0; probe < interp.module_hash_capacity; probe++) {
        usz idx = interp.module_hash_index[hash_slot];
        if (idx == usz.max) return null;  // Empty slot — not found
        if ((uint)interp.modules[idx].name == (uint)name) {
            return &interp.modules[idx];
        }
        hash_slot = (hash_slot + 1) % interp.module_hash_capacity;
    }
    return null;
}

// Module eval/import — handled by JIT (jit.c3)
