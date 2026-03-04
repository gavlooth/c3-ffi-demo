module lisp;

import std::collections::list;
import main;

fn EvalResult parser_error_to_eval_result(Parser* p) {
    EvalResult r;
    r.value = null;
    r.error.has_error = true;
    r.error.line = p.error_line;
    r.error.column = p.error_col;
    usz len = p.error_msg_len;
    if (len > 255) len = 255;
    for (usz i = 0; i < len; i++) {
        r.error.message[i] = p.error_msg[i];
    }
    r.error.message[len] = 0;
    return r;
}

fn bool run_execute_expr(Expr* expr, Interp* interp, Value** result_value, EvalResult* result_error) {
    if (interp.flags.jit_enabled) {
        JitFn f = jit_compile(expr, interp);
        if (f == null) {
            *result_error = eval_error_expr("JIT compilation failed", expr);
            return false;
        }
        *result_value = jit_exec(f, interp);
        return true;
    }
    *result_value = jit_eval(expr, interp.global_env, interp);
    return true;
}

fn Value* run_promote_result(Value* jit_result, Interp* interp, main::ScopeRegion* child_scope, main::ScopeRegion* saved_scope, bool* child_spliced) {
    *child_spliced = false;
    if (jit_result == null) return null;

    Value* original_result = jit_result;
    PromotionContext boundary_ctx = {};
    promotion_context_begin(interp, &boundary_ctx, PROMOTION_CTX_BUDGET_RETURN);
    interp.current_scope = saved_scope;

    if (child_scope.refcount == 1) {
        if (jit_result.scope_gen != child_scope.escape_generation) {
            interp.current_scope = child_scope;
            jit_result = boundary_promote_to_escape(jit_result, interp);
            interp.current_scope = saved_scope;
        }
        if (boundary_ctx.aborted) {
            jit_result = boundary_copy_from_releasing_scope(
                original_result,
                interp,
                child_scope,
                COPY_SITE_RUN_JIT_RESULT
            );
        }
        if (!boundary_ctx.aborted && jit_result.scope_gen == child_scope.escape_generation) {
            if (boundary_try_scope_splice_escapes(saved_scope, child_scope)) {
                *child_spliced = true;
            }
        }
    }

    if (!*child_spliced && main::is_in_scope((void*)jit_result, child_scope)) {
        jit_result = boundary_copy_from_releasing_scope(
            jit_result,
            interp,
            child_scope,
            COPY_SITE_RUN_JIT_RESULT
        );
    }
    promotion_context_end(interp, &boundary_ctx);
    return jit_result;
}

/**
 * Run a complete program.
 */
fn EvalResult run_program(char[] source, Interp* interp) {
    Lexer lex;
    lex.init(source);
    Parser p;
    p.init(&lex, interp);

    List{Expr*} exprs;
    while (!lex.at_end() && !p.has_error) {
        exprs.push(p.parse_expr());
    }

    if (p.has_error) {
        exprs.free();
        return parser_error_to_eval_result(&p);
    }

    EvalResult result = eval_ok(make_nil(interp));

    foreach (expr : exprs) {
        // GC JIT states between top-level expressions (safe: no JIT code on stack)
        jit_gc();

        // Expand macros before JIT compilation
        expr = expand_macros_in_expr(expr, interp);
        if (interp.flags.jit_enabled) {
            JitFn f = jit_compile(expr, interp);
            if (f != null) {
                Value* jit_result = jit_exec(f, interp);
                if (jit_result != null && jit_result.tag == ERROR) {
                    result = eval_error_expr(jit_result.str_chars[:jit_result.str_len], expr);
                } else {
                    result = eval_ok(jit_result);
                }
                if (result.error.has_error) break;
                continue;
            }
            result = eval_error_expr("JIT compilation failed", expr);
            break;
        }
        Value* interpreted = jit_eval(expr, interp.global_env, interp);
        if (interpreted != null && interpreted.tag == ERROR) {
            result = eval_error_expr(interpreted.str_chars[:interpreted.str_len], expr);
            break;
        }
        result = eval_ok(interpreted);
    }

    exprs.free();
    return result;
}

/**
 * Run a single expression.
 */
fn EvalResult run(char[] source, Interp* interp) {
    // GC JIT states between top-level evaluations (safe: no JIT code on stack)
    jit_gc();

    Lexer lex;
    lex.init(source);
    Parser p;
    p.init(&lex, interp);

    Expr* expr = p.parse_expr();

    if (p.has_error) {
        return parser_error_to_eval_result(&p);
    }

    // JIT path: compile + execute in child region + child scope
    expr = expand_macros_in_expr(expr, interp);

    // Push child scope — temporaries freed after eval
    main::ScopeRegion* saved_scope = null;
    main::ScopeRegion* child_scope = boundary_push_child_scope(interp, &saved_scope);
    if (child_scope == null) {
        return eval_error("run: failed to create child scope");
    }
    defer boundary_pop_child_scope(interp, saved_scope, child_scope);

    EvalResult result;
    Value* jit_result = null;
    if (!run_execute_expr(expr, interp, &jit_result, &result)) {
        return result;
    }

    bool child_spliced = false;
    jit_result = run_promote_result(jit_result, interp, child_scope, saved_scope, &child_spliced);
    if (child_spliced) child_scope = null;

    if (jit_result != null && jit_result.tag == ERROR) {
        result = eval_error_expr(jit_result.str_chars[:jit_result.str_len], expr);
    } else {
        result = eval_ok(jit_result);
    }

    return result;
}
