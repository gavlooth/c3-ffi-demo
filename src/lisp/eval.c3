module lisp;

import std::io;
import std::collections::list;
import main;

// =============================================================================
// GNU READLINE FFI DECLARATIONS
// =============================================================================
// readline returns a malloc'd string (null on EOF), must be freed with mem::free.
// add_history adds a line to the interactive history.

extern fn char* readline(char* prompt) @extern("readline");
extern fn void add_history(char* line) @extern("add_history");

// =============================================================================
// LIBDL FFI DECLARATIONS
// =============================================================================

extern fn void* dlopen(char* filename, int flags) @extern("dlopen");
extern fn void* dlsym(void* handle, char* symbol) @extern("dlsym");
extern fn int dlclose(void* handle) @extern("dlclose");
extern fn char* dlerror() @extern("dlerror");
const int RTLD_LAZY = 1;

// Function pointer aliases for FFI calls (all args as long for x86_64 ABI)
alias FfiFn0 = fn long();
alias FfiFn1 = fn long(long);
alias FfiFn2 = fn long(long, long);
alias FfiFn3 = fn long(long, long, long);
alias FfiFn4 = fn long(long, long, long, long);
alias FfiFn5 = fn long(long, long, long, long, long);
alias FfiFn6 = fn long(long, long, long, long, long, long);

// =============================================================================
// SECTION 1: EVALUATOR CORE
// =============================================================================

/**
 * EvalError - Error result from evaluation.
 */
struct EvalError {
    bool        has_error;
    char[256]   message;
    usz         line;     // Source line where error occurred (0 if unknown)
    usz         column;   // Source column where error occurred (0 if unknown)
}

/**
 * EvalResult - Result of evaluation (value or error).
 */
struct EvalResult {
    Value*     value;
    EvalError  error;
}

fn EvalResult eval_ok(Value* v) @inline {
    return { .value = v, .error = { .has_error = false } };
}

fn EvalResult eval_error(char[] msg) {
    EvalResult r;
    r.value = null;
    r.error.has_error = true;
    r.error.line = 0;
    r.error.column = 0;
    usz len = msg.len;
    if (len > 255) len = 255;
    for (usz i = 0; i < len; i++) {
        r.error.message[i] = msg[i];
    }
    r.error.message[len] = 0;
    return r;
}

/**
 * Create an error with source location from an expression.
 */
fn EvalResult eval_error_expr(char[] msg, Expr* expr) {
    EvalResult r;
    r.value = null;
    r.error.has_error = true;
    if (expr != null) {
        r.error.line = expr.loc_line;
        r.error.column = expr.loc_column;
    } else {
        r.error.line = 0;
        r.error.column = 0;
    }
    usz len = msg.len;
    if (len > 255) len = 255;
    for (usz i = 0; i < len; i++) {
        r.error.message[i] = msg[i];
    }
    r.error.message[len] = 0;
    return r;
}

/**
 * Evaluate an expression in an environment.
 */
fn EvalResult eval(Expr* expr, Env* env, Interp* interp) {
    interp.eval_depth++;
    defer interp.eval_depth--;
    if (interp.eval_depth > interp.max_eval_depth) {
        return eval_error("stack overflow: maximum eval depth exceeded");
    }

    for (;;) {
        if (expr == null) {
            return eval_ok(make_nil(interp));
        }

        switch (expr.tag) {
            case E_LIT:
                return eval_ok(expr.lit.value);

            case E_VAR:
                return eval_var(expr, env, interp);

            case E_LAMBDA:
                return eval_lambda(expr, env, interp);

            case E_APP: {
                // Inline with TCO for closure application
                EvalResult func_result = eval(expr.app.func, env, interp);
                if (func_result.error.has_error) return func_result;
                EvalResult arg_result = eval(expr.app.arg, env, interp);
                if (arg_result.error.has_error) return arg_result;
                Value* func = func_result.value;
                Value* arg = arg_result.value;
                if (func != null && func.tag == ValueTag.CLOSURE) {
                    // Handle variadic closure via E_APP (single arg)
                    if (func.closure_val.has_rest) {
                        usz pc = func.closure_val.param_count;
                        if (pc == 0) {
                            // (lambda (.. args) body) with 1 arg: args = (arg)
                            Env* new_env = func.closure_val.env;
                            Value* rest_list = make_cons(interp, arg, make_nil(interp));
                            new_env = new_env.extend(interp, func.closure_val.rest_param, rest_list);
                            env = new_env;
                            expr = func.closure_val.body;
                            continue;  // TCO!
                        } else if (pc == 1) {
                            // (lambda (x .. rest) body) with 1 arg: x=arg, rest=nil
                            Env* new_env = func.closure_val.env;
                            new_env = new_env.extend(interp, func.closure_val.params[0], arg);
                            new_env = new_env.extend(interp, func.closure_val.rest_param, make_nil(interp));
                            env = new_env;
                            expr = func.closure_val.body;
                            continue;  // TCO!
                        } else {
                            return eval_error("too few arguments for variadic lambda");
                        }
                    }
                    if (!func.closure_val.has_param) {
                        // Zero-arg closure called with arg: ignore arg, eval body
                        env = func.closure_val.env;
                        expr = func.closure_val.body;
                        continue;  // TCO!
                    }
                    env = func.closure_val.env.extend(interp, func.closure_val.param, arg);
                    expr = func.closure_val.body;
                    continue;  // TCO!
                }
                return apply(func, arg, interp);
            }

            case E_IF: {
                // Inline with TCO for both branches
                EvalResult test_result = eval(expr.if_expr.test, env, interp);
                if (test_result.error.has_error) return test_result;
                if (!is_falsy(test_result.value, interp)) {
                    expr = expr.if_expr.then_branch;
                } else {
                    expr = expr.if_expr.else_branch;
                }
                continue;  // TCO!
            }

            case E_LET: {
                // Inline with TCO for body evaluation
                if (expr.let_expr.is_recursive) {
                    // Recursive let binding: (let ^rec ((name init)) body)
                    Value* placeholder = make_nil(interp);
                    Env* rec_env = env.extend(interp, expr.let_expr.name, placeholder);
                    EvalResult init_result = eval(expr.let_expr.init, rec_env, interp);
                    if (init_result.error.has_error) return init_result;
                    Value* actual_value = init_result.value;
                    // If it's a closure, patch its captured env to include self-reference
                    if (is_closure(actual_value)) {
                        actual_value.closure_val.env = rec_env;
                    }
                    // Update the placeholder binding with actual value
                    for (usz bi = 0; bi < rec_env.binding_count; bi++) {
                        if ((uint)rec_env.bindings[bi].name == (uint)expr.let_expr.name) {
                            rec_env.bindings[bi].value = actual_value;
                            break;
                        }
                    }
                    env = rec_env;
                    expr = expr.let_expr.body;
                    continue;  // TCO!
                } else {
                    // Normal (non-recursive) let binding
                    EvalResult init_result = eval(expr.let_expr.init, env, interp);
                    if (init_result.error.has_error) return init_result;
                    env = env.extend(interp, expr.let_expr.name, init_result.value);
                    expr = expr.let_expr.body;
                    continue;  // TCO!
                }
            }

            case E_DEFINE:
                return eval_define(expr, env, interp);

            case E_QUOTE:
                return eval_ok(expr.quote.datum);

            case E_RESET:
                return eval_reset(expr, env, interp);

            case E_SHIFT:
                return eval_shift(expr, env, interp);

            case E_PERFORM:
                return eval_perform(expr, env, interp);

            case E_HANDLE:
                return eval_handle(expr, env, interp);

            case E_INDEX:
                return eval_index(expr, env, interp);

            case E_PATH:
                return eval_path(expr, env, interp);

            case E_MATCH: {
                // Inline with TCO for matched clause
                EvalResult scrutinee_result = eval(expr.match.scrutinee, env, interp);
                if (scrutinee_result.error.has_error) return scrutinee_result;
                Value* val = scrutinee_result.value;
                bool found = false;
                for (usz i = 0; i < expr.match.clause_count; i++) {
                    MatchClause* clause = &expr.match.clauses[i];
                    MatchResult match_result = match_pattern(clause.pattern, val, interp);
                    if (match_result.matched) {
                        Env* clause_env = make_env(interp, env);
                        for (usz j = 0; j < match_result.binding_count; j++) {
                            clause_env.define(
                                match_result.bindings[j].name,
                                match_result.bindings[j].value
                            );
                        }
                        env = clause_env;
                        expr = clause.result;
                        found = true;
                        break;
                    }
                }
                if (found) continue;  // TCO!
                return eval_error_expr("no pattern matched in match expression", expr);
            }

            case E_AND: {
                EvalResult left = eval(expr.and_expr.left, env, interp);
                if (left.error.has_error) return left;
                if (is_falsy(left.value, interp)) return left;
                expr = expr.and_expr.right;
                continue;  // TCO!
            }

            case E_OR: {
                EvalResult left = eval(expr.or_expr.left, env, interp);
                if (left.error.has_error) return left;
                if (!is_falsy(left.value, interp)) return left;
                expr = expr.or_expr.right;
                continue;  // TCO!
            }

            case E_CALL: {
                // Check for macro expansion
                if (expr.call.func.tag == E_VAR) {
                    MacroDef* macro_def = lookup_macro(expr.call.func.var_expr.name, interp);
                    if (macro_def != null) {
                        // Pattern-based macro expansion
                        EvalResult macro_result = expand_pattern_macro(macro_def, expr, interp);
                        if (macro_result.error.has_error) return macro_result;
                        expr = value_to_expr(macro_result.value, interp);
                        continue;  // TCO: re-evaluate expanded form
                    }
                }

                // Inline with TCO for closure last-arg application
                EvalResult func_result = eval(expr.call.func, env, interp);
                if (func_result.error.has_error) return func_result;
                Value* func = func_result.value;

                usz arg_count = expr.call.arg_count;
                Value*[16] evaluated_args;
                for (usz i = 0; i < arg_count; i++) {
                    EvalResult ar = eval(expr.call.args[i], env, interp);
                    if (ar.error.has_error) return ar;
                    evaluated_args[i] = ar.value;
                }

                if (func == null) return eval_error("cannot call null");

                switch (func.tag) {
                    case PRIMITIVE: {
                        // Call primitive directly with all args
                        Value* result = func.prim_val.func(evaluated_args[:arg_count], null, interp);
                        if (is_error(result)) {
                            return eval_error(result.str_val.chars[:result.str_val.len]);
                        }
                        return eval_ok(result);
                    }

                    case CLOSURE: {
                        // Check for variadic closure
                        if (func.closure_val.has_rest) {
                            usz pc = func.closure_val.param_count;
                            if (arg_count < pc) {
                                return eval_error("too few arguments for variadic lambda");
                            }
                            // Bind fixed params
                            Env* new_env = func.closure_val.env;
                            for (usz i = 0; i < pc; i++) {
                                new_env = new_env.extend(interp, func.closure_val.params[i], evaluated_args[i]);
                            }
                            // Build rest list from remaining args (right to left)
                            Value* rest_list = make_nil(interp);
                            if (arg_count > pc) {
                                for (usz i = arg_count; i > pc; i--) {
                                    rest_list = make_cons(interp, evaluated_args[i - 1], rest_list);
                                }
                            }
                            new_env = new_env.extend(interp, func.closure_val.rest_param, rest_list);
                            env = new_env;
                            expr = func.closure_val.body;
                            continue;  // TCO!
                        }

                        // Check for zero-arg closure: (lambda () body)
                        if (!func.closure_val.has_param) {
                            if (arg_count == 0) {
                                env = func.closure_val.env;
                                expr = func.closure_val.body;
                                continue;  // TCO!
                            }
                            return eval_error("zero-arg lambda called with arguments");
                        }

                        // Non-variadic: Apply args via currying with TCO on last arg
                        if (arg_count == 0) return eval_ok(func);
                        Value* current = func;
                        // Apply first N-1 args via recursive apply (not tail position)
                        for (usz i = 0; i < arg_count - 1; i++) {
                            EvalResult r = apply(current, evaluated_args[i], interp);
                            if (r.error.has_error) return r;
                            current = r.value;
                        }
                        // Last arg: TCO if current is still a closure
                        if (current != null && current.tag == ValueTag.CLOSURE) {
                            env = current.closure_val.env.extend(interp, current.closure_val.param, evaluated_args[arg_count - 1]);
                            expr = current.closure_val.body;
                            continue;  // TCO!
                        }
                        return apply(current, evaluated_args[arg_count - 1], interp);
                    }

                    case PARTIAL_PRIM: {
                        // Prepend accumulated args, then call primitive
                        Value*[16] all_args;
                        all_args[0] = func.partial_val.first_arg;
                        for (usz i = 0; i < arg_count; i++) {
                            all_args[i + 1] = evaluated_args[i];
                        }
                        Value* presult = func.partial_val.func(all_args[:arg_count + 1], null, interp);
                        if (is_error(presult)) {
                            return eval_error(presult.str_val.chars[:presult.str_val.len]);
                        }
                        return eval_ok(presult);
                    }

                    case CONTINUATION: {
                        // Apply with first arg
                        if (arg_count > 0) {
                            return apply_continuation(func, evaluated_args[0], interp);
                        }
                        return apply_continuation(func, make_nil(interp), interp);
                    }

                    default:
                        return eval_error("not a function");
                }
            }

            case E_BEGIN: {
                // Evaluate all expressions, return the last with TCO
                usz count = expr.begin.expr_count;
                if (count == 0) {
                    return eval_ok(make_nil(interp));
                }
                // Evaluate all but the last expression for side effects
                for (usz i = 0; i < count - 1; i++) {
                    EvalResult r = eval(expr.begin.exprs[i], env, interp);
                    if (r.error.has_error) return r;
                }
                // Last expression: TCO
                expr = expr.begin.exprs[count - 1];
                continue;  // TCO!
            }

            case E_SET: {
                EvalResult val_result = eval(expr.set_expr.value, env, interp);
                if (val_result.error.has_error) return val_result;
                // Try local env first, then global
                if (!env.set(expr.set_expr.name, val_result.value)) {
                    if (interp.global_env == null || !interp.global_env.set(expr.set_expr.name, val_result.value)) {
                        return eval_error("set!: unbound variable");
                    }
                }
                return eval_ok(val_result.value);
            }

            case E_QUASIQUOTE:
                return eval_quasiquote(expr.quasiquote.body, env, interp, 0);

            case E_UNQUOTE:
                return eval_error("unquote outside of quasiquote");

            case E_UNQUOTE_SPLICING:
                return eval_error("unquote-splicing outside of quasiquote");

            case E_DEFMACRO:
                return eval_define_macro(expr, env, interp);

            case E_MODULE:
                return eval_module(expr, env, interp);

            case E_IMPORT:
                return eval_import(expr, env, interp);

            default:
                return eval_error("unknown expression type");
        }
    }
}

// =============================================================================
// SECTION 2: EXPRESSION EVALUATORS
// =============================================================================

fn EvalResult eval_var(Expr* expr, Env* env, Interp* interp) {
    SymbolId name = expr.var_expr.name;

    // First check local environment
    Value* val = env.lookup(name);
    if (val != null) {
        return eval_ok(val);
    }

    // Then check global environment
    if (interp.global_env != null) {
        val = interp.global_env.lookup(name);
        if (val != null) {
            return eval_ok(val);
        }
    }

    // Unbound variable
    char[] sym_name = interp.symbols.get_name(name);
    io::printfn("Error: unbound variable '%s'", (ZString)sym_name);
    return eval_error_expr("unbound variable", expr);
}

fn EvalResult eval_lambda(Expr* expr, Env* env, Interp* interp) {
    // Allocate closures in root_region so env pointers remain valid
    main::RegionHandle saved = interp.current_frame;
    interp.current_frame = interp.root_region;

    // Check for zero-arg lambda (sentinel param)
    if ((uint)expr.lambda.param == 0xFFFFFFFF && !expr.lambda.has_rest) {
        Value* closure = make_closure_no_param(interp, expr.lambda.body, env);
        // Promote captured env to root_region if in temp frame
        if (saved.region_id != interp.root_region.region_id) {
            closure.closure_val.env = deep_copy_env(env, interp);
        }
        interp.current_frame = saved;
        return eval_ok(closure);
    }

    // Create a closure capturing the current environment
    Value* closure = make_closure(interp, expr.lambda.param, expr.lambda.body, env);

    // Copy variadic info from ExprLambda to Closure
    closure.closure_val.param_count = expr.lambda.param_count;
    for (usz i = 0; i < expr.lambda.param_count; i++) {
        closure.closure_val.params[i] = expr.lambda.params[i];
    }
    closure.closure_val.has_rest = expr.lambda.has_rest;
    closure.closure_val.rest_param = expr.lambda.rest_param;

    // For variadic with zero fixed params: (lambda (.. args) body)
    if (expr.lambda.has_rest && expr.lambda.param_count == 0) {
        closure.closure_val.has_param = false;
        closure.closure_val.param = 0;
    }

    // Promote captured env to root_region if in temp frame
    if (saved.region_id != interp.root_region.region_id) {
        closure.closure_val.env = deep_copy_env(env, interp);
    }

    interp.current_frame = saved;
    return eval_ok(closure);
}

fn EvalResult eval_app(Expr* expr, Env* env, Interp* interp) {
    // Evaluate function
    EvalResult func_result = eval(expr.app.func, env, interp);
    if (func_result.error.has_error) {
        return func_result;
    }
    Value* func = func_result.value;

    // Evaluate argument
    EvalResult arg_result = eval(expr.app.arg, env, interp);
    if (arg_result.error.has_error) {
        return arg_result;
    }
    Value* arg = arg_result.value;

    return apply(func, arg, interp);
}

fn EvalResult eval_call(Expr* expr, Env* env, Interp* interp) {
    // Evaluate function
    EvalResult func_result = eval(expr.call.func, env, interp);
    if (func_result.error.has_error) {
        return func_result;
    }
    Value* func = func_result.value;

    // Evaluate all arguments
    Value*[16] evaluated_args;
    usz arg_count = expr.call.arg_count;
    for (usz i = 0; i < arg_count; i++) {
        EvalResult arg_result = eval(expr.call.args[i], env, interp);
        if (arg_result.error.has_error) {
            return arg_result;
        }
        evaluated_args[i] = arg_result.value;
    }

    if (func == null) {
        return eval_error("cannot call null");
    }

    switch (func.tag) {
        case PRIMITIVE:
            // Call primitive directly with all args
            Value* result = func.prim_val.func(evaluated_args[:arg_count], null, interp);
            if (is_error(result)) {
                return eval_error(result.str_val.chars[:result.str_val.len]);
            }
            return eval_ok(result);

        case CLOSURE:
            // Check for variadic closure
            if (func.closure_val.has_rest) {
                usz pc = func.closure_val.param_count;
                if (arg_count < pc) {
                    return eval_error("too few arguments for variadic lambda");
                }
                Env* new_env = func.closure_val.env;
                for (usz i = 0; i < pc; i++) {
                    new_env = new_env.extend(interp, func.closure_val.params[i], evaluated_args[i]);
                }
                Value* rest_list = make_nil(interp);
                if (arg_count > pc) {
                    for (usz i = arg_count; i > pc; i--) {
                        rest_list = make_cons(interp, evaluated_args[i - 1], rest_list);
                    }
                }
                new_env = new_env.extend(interp, func.closure_val.rest_param, rest_list);
                return eval(func.closure_val.body, new_env, interp);
            }

            // Non-variadic: apply args one at a time via currying
            Value* current = func;
            for (usz i = 0; i < arg_count; i++) {
                EvalResult r = apply(current, evaluated_args[i], interp);
                if (r.error.has_error) return r;
                current = r.value;
            }
            return eval_ok(current);

        case PARTIAL_PRIM:
            // Prepend accumulated args, then call primitive
            Value*[16] all_args;
            all_args[0] = func.partial_val.first_arg;
            for (usz i = 0; i < arg_count; i++) {
                all_args[i + 1] = evaluated_args[i];
            }
            Value* presult = func.partial_val.func(all_args[:arg_count + 1], null, interp);
            if (is_error(presult)) {
                return eval_error(presult.str_val.chars[:presult.str_val.len]);
            }
            return eval_ok(presult);

        case CONTINUATION:
            // Apply with first arg
            if (arg_count > 0) {
                return apply_continuation(func, evaluated_args[0], interp);
            }
            return apply_continuation(func, make_nil(interp), interp);

        default:
            return eval_error("not a function");
    }
}

fn EvalResult eval_if(Expr* expr, Env* env, Interp* interp) {
    // Evaluate test
    EvalResult test_result = eval(expr.if_expr.test, env, interp);
    if (test_result.error.has_error) {
        return test_result;
    }

    // Check truthiness: nil and false are falsy, everything else is truthy
    bool is_true = !is_falsy(test_result.value, interp);

    if (is_true) {
        return eval(expr.if_expr.then_branch, env, interp);
    } else {
        return eval(expr.if_expr.else_branch, env, interp);
    }
}

/**
 * Evaluate a short-circuit 'and' expression.
 * (and left right) - returns left if falsy, otherwise evaluates and returns right
 */
fn EvalResult eval_and(Expr* expr, Env* env, Interp* interp) {
    // Evaluate left operand
    EvalResult left = eval(expr.and_expr.left, env, interp);
    if (left.error.has_error) {
        return left;
    }

    // Short-circuit: if left is falsy, return it without evaluating right
    if (is_falsy(left.value, interp)) {
        return left;
    }

    // Left is truthy, evaluate and return right
    return eval(expr.and_expr.right, env, interp);
}

/**
 * Evaluate a short-circuit 'or' expression.
 * (or left right) - returns left if truthy, otherwise evaluates and returns right
 */
fn EvalResult eval_or(Expr* expr, Env* env, Interp* interp) {
    // Evaluate left operand
    EvalResult left = eval(expr.or_expr.left, env, interp);
    if (left.error.has_error) {
        return left;
    }

    // Short-circuit: if left is truthy, return it without evaluating right
    if (!is_falsy(left.value, interp)) {
        return left;
    }

    // Left is falsy, evaluate and return right
    return eval(expr.or_expr.right, env, interp);
}

fn EvalResult eval_let(Expr* expr, Env* env, Interp* interp) {
    if (expr.let_expr.is_recursive) {
        // Recursive let binding: (let ^rec ((name init)) body)
        // 1. Create a placeholder value (nil initially)
        Value* placeholder = make_nil(interp);

        // 2. Extend environment with name bound to placeholder
        Env* rec_env = env.extend(interp, expr.let_expr.name, placeholder);

        // 3. Evaluate init in the extended environment (so it can reference itself)
        EvalResult init_result = eval(expr.let_expr.init, rec_env, interp);
        if (init_result.error.has_error) {
            return init_result;
        }

        // 4. Update the binding with the actual value
        // For closures, we need to update the closure's captured env to include itself
        Value* actual_value = init_result.value;

        if (is_closure(actual_value)) {
            // For closures, update the captured environment to point to rec_env
            // which contains the binding. This creates the self-reference.
            actual_value.closure_val.env = rec_env;
        }

        // Update the placeholder in the environment
        // Find and update the binding in rec_env
        for (usz i = 0; i < rec_env.binding_count; i++) {
            if ((uint)rec_env.bindings[i].name == (uint)expr.let_expr.name) {
                rec_env.bindings[i].value = actual_value;
                break;
            }
        }

        // 5. Evaluate body in the extended environment
        return eval(expr.let_expr.body, rec_env, interp);
    } else {
        // Normal (non-recursive) let binding
        // Evaluate init expression
        EvalResult init_result = eval(expr.let_expr.init, env, interp);
        if (init_result.error.has_error) {
            return init_result;
        }

        // Extend environment with new binding
        Env* new_env = env.extend(interp, expr.let_expr.name, init_result.value);

        // Evaluate body in extended environment
        return eval(expr.let_expr.body, new_env, interp);
    }
}

fn EvalResult eval_define(Expr* expr, Env* env, Interp* interp) {
    // Evaluate value
    EvalResult val_result = eval(expr.define.value, env, interp);
    if (val_result.error.has_error) {
        return val_result;
    }

    // Copy to root_region for globals (so value survives temp frame release)
    Value* stored_val = val_result.value;
    if (interp.current_frame.region_id != interp.root_region.region_id) {
        stored_val = copy_to_parent(stored_val, interp, interp.root_region);
    }

    // Add to global environment
    interp.global_env.define(expr.define.name, stored_val);

    return eval_ok(stored_val);
}

// =============================================================================
// SECTION 2.4: FRAME PUSH/POP (ESCAPE-COPY)
// =============================================================================

// NOTE: Frame push/pop integration is deferred. While copy_to_parent correctly
// handles simple values and cons lists, closures capture Env* pointers that may
// live in child frames. Releasing a child frame would create dangling env
// pointers in escaped closures (e.g., (define (f x) (lambda (y) (+ x y)))).
// Additionally, let ^rec creates env cycles (closure→env→binding→closure) that
// would require cycle-aware copying. A proper solution needs either:
// (1) Full mark-and-sweep GC, (2) Reference counting with cycle detection, or
// (3) Cycle-aware env chain copying with a visited set.
// For now, current_frame == root_region always. This function is retained for
// future GC integration.

/**
 * Copy a Value and everything it transitively references to a target region.
 * Used to promote return values out of a child frame before releasing it.
 */
fn Value* copy_to_parent(Value* v, Interp* interp, main::RegionHandle target) {
    if (v == null) return null;
    main::RegionHandle saved = interp.current_frame;
    interp.current_frame = target;

    Value* result;
    switch (v.tag) {
        case NIL:
            result = make_nil(interp);
        case INT:
            result = make_int(interp, v.int_val);
        case STRING:
            result = make_string(interp, v.str_val.chars[:v.str_val.len]);
        case SYMBOL:
            result = make_symbol(interp, v.sym_val);
        case CONS: {
            // Need to restore frame for recursive calls
            interp.current_frame = saved;
            Value* new_car = copy_to_parent(v.cons_val.car, interp, target);
            Value* new_cdr = copy_to_parent(v.cons_val.cdr, interp, target);
            interp.current_frame = target;
            result = make_cons(interp, new_car, new_cdr);
        }
        case CLOSURE:
            result = v;  // closures already in root_region
        case PRIMITIVE:
            result = v;
        case PARTIAL_PRIM:
            result = v;
        case CONTINUATION:
            result = v;
        case HASHMAP:
            result = v;  // hash maps allocated via malloc
        case FFI_HANDLE:
            result = v;  // FFI handles allocated in root_region
        case ERROR:
            result = make_error(interp, v.str_val.chars[:v.str_val.len]);
        default:
            result = v;
    }
    interp.current_frame = saved;
    return result;
}

/**
 * Deep-copy an env chain to root_region so closures survive frame release.
 * Stops at global_env (already in root_region).
 */
fn Env* deep_copy_env(Env* env, Interp* interp, usz depth = 0) {
    if (env == null || env == interp.global_env) return env;
    if (depth >= 256) return null;
    main::RegionHandle saved = interp.current_frame;
    interp.current_frame = interp.root_region;
    Env* new_env = interp.alloc_env();
    interp.current_frame = saved;
    new_env.binding_count = env.binding_count;
    for (usz i = 0; i < env.binding_count; i++) {
        new_env.bindings[i].name = env.bindings[i].name;
        new_env.bindings[i].value = copy_to_parent(
            env.bindings[i].value, interp, interp.root_region);
    }
    new_env.parent = deep_copy_env(env.parent, interp, depth + 1);
    return new_env;
}

// =============================================================================
// SECTION 2.5: QUASIQUOTE
// =============================================================================

/**
 * Evaluate a quasiquote template at the given nesting depth.
 * Bawden's algorithm: walk AST, evaluate unquoted parts, quote the rest.
 */
fn EvalResult eval_quasiquote(Expr* tmpl, Env* env, Interp* interp, usz depth) {
    if (depth > 64) return eval_error("quasiquote nesting too deep (max 64)");
    if (tmpl == null) return eval_ok(make_nil(interp));

    switch (tmpl.tag) {
        case E_UNQUOTE: {
            if (depth == 0) {
                // Evaluate the unquoted expression
                return eval(tmpl.unquote.body, env, interp);
            }
            // Nested: reduce depth, rebuild (unquote ...)
            EvalResult inner = eval_quasiquote(tmpl.unquote.body, env, interp, depth - 1);
            if (inner.error.has_error) return inner;
            Value* sym = make_symbol(interp, interp.sym_unquote);
            Value* rest = make_cons(interp, inner.value, make_nil(interp));
            return eval_ok(make_cons(interp, sym, rest));
        }
        case E_UNQUOTE_SPLICING: {
            if (depth == 0) {
                return eval_error(",@ in non-list context");
            }
            EvalResult inner = eval_quasiquote(tmpl.unquote_splicing.body, env, interp, depth - 1);
            if (inner.error.has_error) return inner;
            Value* sym = make_symbol(interp, interp.sym_unquote_splicing);
            Value* rest = make_cons(interp, inner.value, make_nil(interp));
            return eval_ok(make_cons(interp, sym, rest));
        }
        case E_QUASIQUOTE: {
            // Nested quasiquote: increase depth
            EvalResult inner = eval_quasiquote(tmpl.quasiquote.body, env, interp, depth + 1);
            if (inner.error.has_error) return inner;
            Value* sym = make_symbol(interp, interp.sym_quasiquote);
            Value* rest = make_cons(interp, inner.value, make_nil(interp));
            return eval_ok(make_cons(interp, sym, rest));
        }
        case E_LIT:
            return eval_ok(tmpl.lit.value);
        case E_VAR:
            // Quote the variable — return it as a symbol
            return eval_ok(make_symbol(interp, tmpl.var_expr.name));
        case E_CALL: {
            // List form: expand each element
            return qq_expand_call(tmpl, env, interp, depth);
        }
        case E_APP: {
            // Two-element list
            EvalResult f = eval_quasiquote(tmpl.app.func, env, interp, depth);
            if (f.error.has_error) return f;
            EvalResult a = eval_quasiquote(tmpl.app.arg, env, interp, depth);
            if (a.error.has_error) return a;
            Value* rest = make_cons(interp, a.value, make_nil(interp));
            return eval_ok(make_cons(interp, f.value, rest));
        }
        case E_QUOTE:
            return eval_ok(tmpl.quote.datum);
        default:
            return eval_ok(make_nil(interp));
    }
}

/**
 * Expand an array of expressions inside quasiquote into a list Value.
 * Handles ,@ splice by appending evaluated lists.
 */
fn EvalResult qq_expand_elements(Expr*[] elements, Env* env, Interp* interp, usz depth) {
    Value* result = make_nil(interp);
    for (usz i = elements.len; i > 0; i--) {
        Expr* elem = elements[i - 1];
        if (elem.tag == E_UNQUOTE_SPLICING && depth == 0) {
            EvalResult splice_result = eval(elem.unquote_splicing.body, env, interp);
            if (splice_result.error.has_error) return splice_result;
            Value* spliced = splice_result.value;
            if (is_nil(spliced)) {
                // Nothing to splice
            } else if (is_cons(spliced)) {
                Value*[64] items;
                usz item_count = 0;
                Value* tmp = spliced;
                while (is_cons(tmp) && item_count < 64) {
                    items[item_count] = tmp.cons_val.car;
                    item_count++;
                    tmp = tmp.cons_val.cdr;
                }
                if (is_cons(tmp)) {
                    return eval_error("quasiquote splice: too many items (max 64)");
                }
                for (usz j = item_count; j > 0; j--) {
                    result = make_cons(interp, items[j - 1], result);
                }
            } else {
                return eval_error(",@ value is not a list");
            }
        } else {
            EvalResult expanded = eval_quasiquote(elem, env, interp, depth);
            if (expanded.error.has_error) return expanded;
            result = make_cons(interp, expanded.value, result);
        }
    }
    return eval_ok(result);
}

/**
 * Expand a call expression inside quasiquote.
 * Handles ,@ splice by appending evaluated lists.
 */
fn EvalResult qq_expand_call(Expr* tmpl, Env* env, Interp* interp, usz depth) {
    // Build result list from all elements (func + args)
    // First, expand the function position
    usz total = tmpl.call.arg_count + 1;  // func + args
    Expr*[17] elements;
    elements[0] = tmpl.call.func;
    for (usz i = 0; i < tmpl.call.arg_count; i++) {
        elements[i + 1] = tmpl.call.args[i];
    }

    // Build result right-to-left
    Value* result = make_nil(interp);
    for (usz i = total; i > 0; i--) {
        Expr* elem = elements[i - 1];
        if (elem.tag == E_UNQUOTE_SPLICING && depth == 0) {
            // Splice: evaluate body, append with result
            EvalResult splice_result = eval(elem.unquote_splicing.body, env, interp);
            if (splice_result.error.has_error) return splice_result;
            // Append splice_result.value to result
            Value* spliced = splice_result.value;
            if (is_nil(spliced)) {
                // Nothing to splice
            } else if (is_cons(spliced)) {
                // Collect splice items, then prepend in order
                Value*[64] items;
                usz item_count = 0;
                Value* tmp = spliced;
                while (is_cons(tmp) && item_count < 64) {
                    items[item_count] = tmp.cons_val.car;
                    item_count++;
                    tmp = tmp.cons_val.cdr;
                }
                if (is_cons(tmp)) {
                    return eval_error("quasiquote splice: too many items (max 64)");
                }
                // Prepend in reverse (right-to-left)
                for (usz j = item_count; j > 0; j--) {
                    result = make_cons(interp, items[j - 1], result);
                }
            } else {
                return eval_error(",@ value is not a list");
            }
        } else {
            EvalResult expanded = eval_quasiquote(elem, env, interp, depth);
            if (expanded.error.has_error) return expanded;
            result = make_cons(interp, expanded.value, result);
        }
    }
    return eval_ok(result);
}

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

    if (val.tag == INT || val.tag == STRING) {
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
                Expr* e = interp.alloc_expr();
                e.tag = E_BEGIN;
                e.begin.expr_count = 0;
                while (is_cons(rest) && e.begin.expr_count < 16) {
                    e.begin.exprs[e.begin.expr_count] = value_to_expr(rest.cons_val.car, interp);
                    e.begin.expr_count++;
                    rest = rest.cons_val.cdr;
                }
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
                SymbolId[16] params;
                usz param_count = 0;
                Value* p = params_list;
                while (is_cons(p) && param_count < 16) {
                    if (p.cons_val.car != null && p.cons_val.car.tag == SYMBOL) {
                        params[param_count] = p.cons_val.car.sym_val;
                        param_count++;
                    }
                    p = p.cons_val.cdr;
                }

                if (param_count == 0) {
                    // Zero-arg lambda
                    Expr* e = interp.alloc_expr();
                    e.tag = E_LAMBDA;
                    e.lambda.param = (SymbolId)0xFFFFFFFF;
                    e.lambda.param_count = 0;
                    e.lambda.has_rest = false;
                    e.lambda.rest_param = 0;
                    e.lambda.body = body;
                    return e;
                }
                if (param_count == 1) {
                    Expr* e = interp.alloc_expr();
                    e.tag = E_LAMBDA;
                    e.lambda.param = params[0];
                    e.lambda.param_count = 1;
                    e.lambda.params[0] = params[0];
                    e.lambda.has_rest = false;
                    e.lambda.rest_param = 0;
                    e.lambda.body = body;
                    return e;
                }
                // Multi-param: desugar to nested lambdas
                Expr* inner = body;
                for (usz i = param_count; i > 1; i--) {
                    Expr* wrapper = interp.alloc_expr();
                    wrapper.tag = E_LAMBDA;
                    wrapper.lambda.param = params[i - 1];
                    wrapper.lambda.param_count = 1;
                    wrapper.lambda.params[0] = params[i - 1];
                    wrapper.lambda.has_rest = false;
                    wrapper.lambda.rest_param = 0;
                    wrapper.lambda.body = inner;
                    inner = wrapper;
                }
                Expr* e = interp.alloc_expr();
                e.tag = E_LAMBDA;
                e.lambda.param = params[0];
                e.lambda.param_count = 1;
                e.lambda.params[0] = params[0];
                e.lambda.has_rest = false;
                e.lambda.rest_param = 0;
                e.lambda.body = inner;
                return e;
            }
            if ((uint)sym == (uint)interp.sym_let) {
                Value* rest = val.cons_val.cdr;
                if (!is_cons(rest)) return value_to_expr(make_nil(interp), interp);
                Value* bindings_list = rest.cons_val.car;
                rest = rest.cons_val.cdr;
                Expr* body = is_cons(rest) ? value_to_expr(rest.cons_val.car, interp) : value_to_expr(make_nil(interp), interp);
                // Build nested lets from bindings
                if (!is_cons(bindings_list)) {
                    return body;
                }
                // Collect all bindings
                SymbolId[16] names;
                Value*[16] inits;
                usz count = 0;
                Value* bp = bindings_list;
                while (is_cons(bp) && count < 16) {
                    Value* binding = bp.cons_val.car;
                    if (is_cons(binding)) {
                        Value* name_v = binding.cons_val.car;
                        names[count] = (name_v != null && name_v.tag == SYMBOL) ? name_v.sym_val : 0;
                        inits[count] = is_cons(binding.cons_val.cdr) ? binding.cons_val.cdr.cons_val.car : make_nil(interp);
                        count++;
                    }
                    bp = bp.cons_val.cdr;
                }
                // Build from inside out
                Expr* inner_body = body;
                for (usz i = count; i > 0; i--) {
                    Expr* e = interp.alloc_expr();
                    e.tag = E_LET;
                    e.let_expr.name = names[i - 1];
                    e.let_expr.init = value_to_expr(inits[i - 1], interp);
                    e.let_expr.body = inner_body;
                    e.let_expr.is_recursive = false;
                    inner_body = e;
                }
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
            if ((uint)sym == (uint)interp.sym_perform) {
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

        Expr* e = interp.alloc_expr();
        e.tag = E_CALL;
        e.call.func = value_to_expr(val.cons_val.car, interp);
        e.call.arg_count = 0;
        Value* rest = val.cons_val.cdr;
        while (is_cons(rest) && e.call.arg_count < 64) {
            e.call.args[e.call.arg_count] = value_to_expr(rest.cons_val.car, interp);
            e.call.arg_count++;
            rest = rest.cons_val.cdr;
        }
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
    usz hash_slot = (usz)name % MACRO_HASH_SIZE;
    for (usz probe = 0; probe < MACRO_HASH_SIZE; probe++) {
        usz idx = interp.macro_hash_index[hash_slot];
        if (idx == usz.max) return null;  // Empty slot — not found
        if ((uint)interp.macro_table[idx].name == (uint)name) {
            return &interp.macro_table[idx];
        }
        hash_slot = (hash_slot + 1) % MACRO_HASH_SIZE;
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
fn EvalResult eval_define_macro(Expr* expr, Env* env, Interp* interp) {
    if (interp.macro_count >= 64) {
        return eval_error("macro table exhausted (max 64 macros)");
    }
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
        capture_template_bindings(mdef.clauses[i].tmpl, &pat_vars, pat_var_count,
                                  &mdef.captured_bindings, &mdef.captured_count, interp);
    }

    interp.macro_count++;

    // Insert into macro hash index
    usz macro_idx = interp.macro_count - 1;
    usz hash_slot = (usz)mdef.name % MACRO_HASH_SIZE;
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
        hash_slot = (hash_slot + 1) % MACRO_HASH_SIZE;
    }

    return eval_ok(make_nil(interp));
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
        || (uint)sym == (uint)interp.sym_perform
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
    if (head != null && head.tag == SYMBOL && (uint)head.sym_val == (uint)interp.sym_dotdot) {
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
    usz hash_slot = (usz)name % MODULE_HASH_SIZE;
    for (usz probe = 0; probe < MODULE_HASH_SIZE; probe++) {
        usz idx = interp.module_hash_index[hash_slot];
        if (idx == usz.max) return null;  // Empty slot — not found
        if ((uint)interp.modules[idx].name == (uint)name) {
            return &interp.modules[idx];
        }
        hash_slot = (hash_slot + 1) % MODULE_HASH_SIZE;
    }
    return null;
}

/**
 * Evaluate a module definition: (module name (export ...) body...)
 *
 * Creates a fresh env inheriting global (so primitives/stdlib are available),
 * evaluates all body expressions in that env, records exported symbols,
 * and stores the module in interp.modules.
 */
fn EvalResult eval_module(Expr* expr, Env* env, Interp* interp) {
    SymbolId name = expr.module_expr.name;

    // Check for duplicate module
    if (find_module(name, interp) != null) {
        return eval_error("module already defined");
    }

    if (interp.module_count >= 32) {
        return eval_error("module table exhausted (max 32 modules)");
    }
    Module* mod = &interp.modules[interp.module_count];
    mod.name = name;
    mod.loaded = false;  // Set to false during loading (circular detection)
    mod.path_len = 0;
    mod.export_count = expr.module_expr.export_count;
    for (usz i = 0; i < expr.module_expr.export_count; i++) {
        mod.exports[i] = expr.module_expr.exports[i];
    }

    // Create fresh env inheriting global
    Env* mod_env = make_env(interp, interp.global_env);
    mod.env = mod_env;
    interp.module_count++;

    // Insert into module hash index
    usz mod_idx = interp.module_count - 1;
    usz mh_slot = (usz)name % MODULE_HASH_SIZE;
    while (interp.module_hash_index[mh_slot] != usz.max) {
        mh_slot = (mh_slot + 1) % MODULE_HASH_SIZE;
    }
    interp.module_hash_index[mh_slot] = mod_idx;

    // Temporarily swap global_env to module env so that (define ...) goes
    // into the module env instead of the real global env
    Env* saved_global = interp.global_env;
    interp.global_env = mod_env;

    // Evaluate body expressions in module env
    EvalResult last_result = eval_ok(make_nil(interp));
    for (usz i = 0; i < expr.module_expr.body_count; i++) {
        last_result = eval(expr.module_expr.body[i], mod_env, interp);
        if (last_result.error.has_error) {
            interp.global_env = saved_global;
            return last_result;
        }
    }

    // Restore global env
    interp.global_env = saved_global;
    mod.loaded = true;
    return eval_ok(make_nil(interp));
}

/**
 * Evaluate an import: (import name) or (import "path")
 *
 * For symbol imports: looks up module in interp.modules.
 * For path imports: reads file, parses, evaluates in fresh env, then imports.
 * Copies exported bindings into the current environment.
 */
fn EvalResult eval_import(Expr* expr, Env* env, Interp* interp) {
    SymbolId name = expr.import_expr.name;

    // Look up already-loaded module
    Module* mod = find_module(name, interp);

    if (mod != null) {
        // Check for circular import
        if (!mod.loaded) {
            return eval_error("circular import detected");
        }
    } else if (expr.import_expr.has_path) {
        // Load from explicit file path
        char[] path = expr.import_expr.path[:expr.import_expr.path_len];
        EvalResult load_result = load_module_from_file(path, name, interp);
        if (load_result.error.has_error) return load_result;
        mod = find_module(name, interp);
    } else {
        // Try lib/<name>.pika
        char[] sym_name = interp.symbols.get_name(name);
        // Validate total path length: "lib/" (4) + name + ".pika" (5) must fit in 511 bytes
        if (4 + sym_name.len + 5 > 511) {
            return eval_error("module path too long");
        }
        char[512] path_buf;
        usz plen = 0;
        // Build "lib/"
        path_buf[0] = 'l'; path_buf[1] = 'i'; path_buf[2] = 'b'; path_buf[3] = '/';
        plen = 4;
        for (usz i = 0; i < sym_name.len; i++) {
            path_buf[plen] = sym_name[i];
            plen++;
        }
        // Append ".pika"
        path_buf[plen] = '.'; plen++;
        path_buf[plen] = 'p'; plen++;
        path_buf[plen] = 'i'; plen++;
        path_buf[plen] = 'k'; plen++;
        path_buf[plen] = 'a'; plen++;

        char[] path = path_buf[:plen];
        EvalResult load_result = load_module_from_file(path, name, interp);
        if (load_result.error.has_error) return load_result;
        mod = find_module(name, interp);
    }

    if (mod == null) {
        return eval_error("module not found");
    }

    // Copy exported bindings to current environment
    for (usz i = 0; i < mod.export_count; i++) {
        Value* val = mod.env.lookup(mod.exports[i]);
        if (val != null) {
            env.define(mod.exports[i], val);
        }
    }

    return eval_ok(make_nil(interp));
}

/**
 * Load a module from a file path. Reads the file, parses all expressions,
 * evaluates them in a fresh module env.
 */
fn EvalResult load_module_from_file(char[] path, SymbolId name, Interp* interp) {
    // Read file
    if (try content = io::file::load_temp((String)path)) {
        char[] source = content;

        // Parse all expressions
        Lexer lex;
        lex.init(source);
        Parser p;
        p.init(&lex, interp);

        // We expect the file to contain a (module ...) form
        // or multiple top-level defines that we wrap as a module
        Expr*[64] exprs;
        usz expr_count = 0;
        while (!lex.at_end()) {
            assert(expr_count < 64, "too many expressions in module file");
            exprs[expr_count] = p.parse_expr();
            expr_count++;
        }

        // If first expression is a module form, evaluate it directly
        if (expr_count > 0 && exprs[0].tag == E_MODULE) {
            return eval(exprs[0], interp.global_env, interp);
        }

        // Otherwise, wrap all expressions as an implicit module
        // with all defines exported
        if (interp.module_count >= 32) {
            return eval_error("module table exhausted (max 32 modules)");
        }
        Module* mod = &interp.modules[interp.module_count];
        mod.name = name;
        mod.loaded = false;
        for (usz i = 0; i < path.len && i < 255; i++) {
            mod.path[i] = path[i];
        }
        mod.path_len = path.len;
        mod.export_count = 0;

        Env* mod_env = make_env(interp, interp.global_env);
        mod.env = mod_env;
        interp.module_count++;

        // Insert into module hash index
        usz mod_idx2 = interp.module_count - 1;
        usz mh_slot2 = (usz)name % MODULE_HASH_SIZE;
        while (interp.module_hash_index[mh_slot2] != usz.max) {
            mh_slot2 = (mh_slot2 + 1) % MODULE_HASH_SIZE;
        }
        interp.module_hash_index[mh_slot2] = mod_idx2;

        // Swap global_env to module env so defines go into module
        Env* saved_global = interp.global_env;
        interp.global_env = mod_env;

        // Evaluate all expressions, collect defined names as exports
        for (usz i = 0; i < expr_count; i++) {
            EvalResult r = eval(exprs[i], mod_env, interp);
            if (r.error.has_error) {
                interp.global_env = saved_global;
                return r;
            }
            // Auto-export defines
            if (exprs[i].tag == E_DEFINE && mod.export_count < 128) {
                mod.exports[mod.export_count] = exprs[i].define.name;
                mod.export_count++;
            }
        }

        interp.global_env = saved_global;
        mod.loaded = true;
        return eval_ok(make_nil(interp));
    }

    return eval_error("cannot read module file");
}

// =============================================================================
// SECTION 3: CONTINUATIONS (RESET/SHIFT)
// =============================================================================

/**
 * Evaluate a reset expression.
 *
 * (reset body) establishes a delimiter for shift operations.
 */
fn EvalResult eval_reset(Expr* expr, Env* env, Interp* interp) {
    usz saved_shift_counter = interp.shift_counter;
    interp.shift_counter = 0;

    if (interp.reset_depth >= 16) return eval_error("reset stack overflow: too many nested reset expressions");
    interp.reset_depth++;

    // Store reset body/env for continuation capture
    interp.reset_body_stack[interp.reset_depth - 1] = expr.reset.body;
    interp.reset_env_stack[interp.reset_depth - 1] = env;

    EvalResult result = eval(expr.reset.body, env, interp);

    interp.shift_counter = saved_shift_counter;

    interp.reset_depth--;

    if (result.error.has_error) {
        interp.shift_occurred = false;
        return result;
    }

    if (interp.shift_occurred && interp.reset_depth == interp.shift_target_depth) {
        interp.shift_occurred = false;
        return eval_ok(interp.shift_result);
    }

    return result;
}

/**
 * Evaluate a shift expression.
 *
 * (shift k body) captures the continuation up to the enclosing reset
 * and binds it to k in body.
 */
fn EvalResult eval_shift(Expr* expr, Env* env, Interp* interp) {
    // REPLAY CHECK: if we're replaying a continuation, return the substituted value
    if (interp.cont_substituting && !interp.cont_is_effect) {
        usz current_index = interp.shift_counter;
        interp.shift_counter++;
        if (current_index < interp.cont_target_shift_index) {
            return eval_ok(interp.shift_prior_results[current_index]);
        }
        if (current_index == interp.cont_target_shift_index) {
            interp.cont_substituting = false;
            if (current_index < 16) {
                interp.shift_prior_results[current_index] = interp.cont_substitution;
            }
            return eval_ok(interp.cont_substitution);
        }
    }

    if (interp.reset_depth == 0) {
        return eval_error("shift outside of reset");
    }

    // Allocate CapturedCont in root region
    CapturedCont cc_init;
    main::ObjectHandle cc_handle = main::allocate_in(interp.root_region, CapturedCont, cc_init);
    CapturedCont* cc = main::dereference_as(CapturedCont, cc_handle);
    cc.reset_body = interp.reset_body_stack[interp.reset_depth - 1];
    cc.reset_env = interp.reset_env_stack[interp.reset_depth - 1];
    cc.is_effect = false;
    cc.has_handler = false;
    cc.shift_index = interp.shift_counter;
    cc.shift_prior_count = interp.shift_counter;
    for (usz si = 0; si < cc.shift_prior_count && si < 16; si++) {
        cc.shift_prior_results[si] = interp.shift_prior_results[si];
    }
    interp.shift_counter++;

    // Create continuation with captured state
    Continuation* k = interp.alloc_lisp_continuation();
    k.data = cc;

    Value* k_val = make_continuation(interp, k);

    // Extend environment with k bound to the continuation
    Env* shift_env = env.extend(interp, expr.shift.k_name, k_val);

    // Evaluate shift body with k bound
    EvalResult body_result = eval(expr.shift.body, shift_env, interp);

    if (body_result.error.has_error) {
        return body_result;
    }

    // The result of the shift body becomes the result of the reset
    interp.shift_occurred = true;
    interp.shift_target_depth = interp.reset_depth - 1;
    interp.shift_result = body_result.value;

    return body_result;
}

// =============================================================================
// SECTION 4: EFFECT HANDLERS
// =============================================================================

/**
 * Evaluate a perform expression.
 *
 * (perform tag arg) signals an effect with the given tag and argument.
 */
fn EvalResult eval_perform(Expr* expr, Env* env, Interp* interp) {
    // Evaluate the argument
    EvalResult arg_result = eval(expr.perform.arg, env, interp);
    if (arg_result.error.has_error) {
        return arg_result;
    }

    SymbolId tag = expr.perform.tag;

    // REPLAY CHECK: if we're replaying an effect continuation, return the substituted value
    if (interp.cont_substituting && interp.cont_is_effect &&
        (uint)interp.cont_effect_tag == (uint)tag) {
        usz current_index = interp.effect_perform_counter;
        interp.effect_perform_counter++;
        if (current_index < interp.cont_target_perform_index) {
            return eval_ok(interp.effect_prior_results[current_index]);
        }
        if (current_index == interp.cont_target_perform_index) {
            interp.cont_substituting = false;
            if (current_index < 16) {
                interp.effect_prior_results[current_index] = interp.cont_substitution;
            }
            return eval_ok(interp.cont_substitution);
        }
    }

    // Search handler stack for a matching handler
    for (isz i = (isz)interp.handler_count - 1; i >= 0; i--) {
        EffectHandler* h = &interp.handler_stack[(usz)i];

        for (usz j = 0; j < h.clause_count; j++) {
            if ((uint)h.clauses[j].effect_tag == (uint)tag) {
                // Found matching handler
                EffectClause* clause = &h.clauses[j];

                // Allocate CapturedCont in root region
                CapturedCont cc_init;
                main::ObjectHandle cc_handle = main::allocate_in(interp.root_region, CapturedCont, cc_init);
                CapturedCont* cc = main::dereference_as(CapturedCont, cc_handle);
                cc.reset_body = h.body_expr;
                cc.reset_env = h.body_env;
                cc.is_effect = true;
                cc.effect_tag = tag;
                // Copy handler for reinstallation during replay
                cc.handler_copy = *h;
                cc.has_handler = true;
                cc.perform_index = interp.effect_perform_counter;
                cc.prior_count = interp.effect_perform_counter;
                for (usz pi = 0; pi < cc.prior_count && pi < 16; pi++) {
                    cc.prior_results[pi] = interp.effect_prior_results[pi];
                }
                interp.effect_perform_counter++;

                // Create continuation with captured state
                Continuation* k = interp.alloc_lisp_continuation();
                k.data = cc;
                Value* k_val = make_continuation(interp, k);

                // Extend handler's environment with k and arg bindings
                Env* clause_env = h.handler_env.extend(interp, clause.k_name, k_val);
                clause_env = clause_env.extend(interp, clause.arg_name, arg_result.value);

                // Pop handler before evaluating handler body (prevent re-entry)
                interp.handler_count--;

                // Evaluate handler body
                EvalResult result = eval(clause.handler_body, clause_env, interp);
                // Signal to eval_handle that the handler was invoked
                if (!result.error.has_error) {
                    interp.effect_occurred = true;
                    interp.effect_result = result.value;
                }
                return result;
            }
        }
    }

    // No handler found — during multi-perform replays, later performs may
    // reach here after the handler was already consumed by an earlier perform.
    // The effect_occurred flag carries the correct result upward, so this
    // error path is benign in that scenario.
    return eval_error("unhandled effect");
}

/**
 * Evaluate a handle expression.
 *
 * (handle body ((tag k x) handler) ...) installs effect handlers.
 */
fn EvalResult eval_handle(Expr* expr, Env* env, Interp* interp) {
    if (interp.handler_count >= 16) return eval_error("handler stack overflow: too many nested handlers");

    EffectHandler* h = &interp.handler_stack[interp.handler_count];
    if (expr.handle.clause_count > MAX_EFFECT_CLAUSES) {
        return eval_error("too many effect handler clauses (max 8)");
    }
    h.clause_count = expr.handle.clause_count;
    h.handler_env = env;
    h.body_expr = expr.handle.body;   // Store for continuation capture
    h.body_env = env;                 // Store for continuation capture

    for (usz i = 0; i < expr.handle.clause_count; i++) {
        h.tags[i] = expr.handle.clauses[i].effect_tag;
        h.clauses[i] = expr.handle.clauses[i];
    }

    usz saved_perform_counter = interp.effect_perform_counter;
    interp.effect_perform_counter = 0;

    interp.handler_count++;

    // Evaluate body with handler installed
    EvalResult result = eval(expr.handle.body, env, interp);

    interp.effect_perform_counter = saved_perform_counter;

    // If an effect handler was invoked, its result bypasses intermediate computation.
    // Note: eval_perform already popped the handler, so don't pop again.
    if (interp.effect_occurred) {
        interp.effect_occurred = false;
        return eval_ok(interp.effect_result);
    }

    // No effect occurred, pop handler normally
    interp.handler_count--;

    return result;
}

/**
 * Evaluate an index expression: arr.[0], dict.['key]
 * OmniLisp dot-bracket notation for collection access.
 */
fn EvalResult eval_index(Expr* expr, Env* env, Interp* interp) {
    // Evaluate the collection
    EvalResult coll_result = eval(expr.index.collection, env, interp);
    if (coll_result.error.has_error) {
        return coll_result;
    }
    Value* collection = coll_result.value;

    // Evaluate the index
    EvalResult idx_result = eval(expr.index.index, env, interp);
    if (idx_result.error.has_error) {
        return idx_result;
    }
    Value* index = idx_result.value;

    // Handle list/cons indexing with integer
    if (is_cons(collection) && is_int(index)) {
        long idx = index.int_val;
        if (idx < 0) {
            return eval_error("negative list index");
        }
        Value* current = collection;
        for (long i = 0; i < idx; i++) {
            if (!is_cons(current)) {
                return eval_error("list index out of bounds");
            }
            current = cdr(current);
        }
        if (!is_cons(current)) {
            return eval_error("list index out of bounds");
        }
        return eval_ok(car(current));
    }

    // Handle string indexing with integer
    if (is_string(collection) && is_int(index)) {
        long idx = index.int_val;
        if (idx < 0 || (usz)idx >= collection.str_val.len) {
            return eval_error("string index out of bounds");
        }
        // Return character as integer (ASCII value)
        return eval_ok(make_int(interp, (long)collection.str_val.chars[(usz)idx]));
    }

    return eval_error("cannot index this type");
}

/**
 * Look up a key in an association list.
 * An alist is a list of pairs: ((key1 . val1) (key2 . val2) ...)
 * Returns the value associated with the key, or null if not found.
 */
fn Value* alist_lookup(Value* alist, SymbolId key, Interp* interp) {
    // Iterate through the alist
    while (is_cons(alist)) {
        Value* pair = car(alist);

        // Each element should be a cons cell (key . value)
        if (is_cons(pair)) {
            Value* pair_key = car(pair);

            // Check if the key matches (compare as symbols)
            if (is_symbol(pair_key) && (uint)pair_key.sym_val == (uint)key) {
                return cdr(pair);
            }
        }

        alist = cdr(alist);
    }

    return null;  // Key not found
}

/**
 * Evaluate a path expression: point.x, person.address.city
 * Field access notation.
 *
 * For multi-segment paths like person.address.city:
 * 1. Look up the first segment as a variable
 * 2. For each subsequent segment, look it up as a key in an association list
 */
fn EvalResult eval_path(Expr* expr, Env* env, Interp* interp) {
    if (expr.path.segment_count == 0) {
        return eval_error("empty path");
    }

    // Look up the first segment as a variable
    Value* current = env.lookup(expr.path.segments[0]);
    if (current == null) {
        current = interp.global_env.lookup(expr.path.segments[0]);
    }
    if (current == null) {
        return eval_error("unbound path root variable");
    }

    // For single-segment paths, just return the value
    if (expr.path.segment_count == 1) {
        return eval_ok(current);
    }

    // For each remaining segment, look up in alist
    for (usz i = 1; i < expr.path.segment_count; i++) {
        SymbolId key = expr.path.segments[i];
        Value* found = alist_lookup(current, key, interp);
        if (found == null) {
            return eval_error("path segment not found in alist");
        }
        current = found;
    }

    return eval_ok(current);
}

// =============================================================================
// SECTION 4.5: PATTERN MATCHING
// =============================================================================

/**
 * MatchResult - Result of attempting to match a pattern against a value.
 * If successful, contains bindings produced by the match.
 */
struct MatchResult {
    bool matched;
    Binding[MAX_BINDINGS] bindings;
    usz binding_count;
}

/**
 * Auto-gensym support for pattern macros.
 * Symbols ending with # are auto-replaced with unique gensyms.
 * The mapping ensures the same #-symbol maps to the same gensym within one expansion.
 */
struct GensymMapping {
    SymbolId original;   // The foo# symbol
    SymbolId generated;  // The g#N gensym
}

struct GensymTable {
    GensymMapping[16] mappings;
    usz count;
}

fn MatchResult match_fail() @inline {
    return { .matched = false, .binding_count = 0 };
}

fn MatchResult match_ok() @inline {
    return { .matched = true, .binding_count = 0 };
}

fn void MatchResult.add_binding(MatchResult* self, SymbolId name, Value* value) {
    assert(self.binding_count < MAX_BINDINGS, "too many pattern bindings");
    self.bindings[self.binding_count].name = name;
    self.bindings[self.binding_count].value = value;
    self.binding_count++;
}

fn void MatchResult.merge(MatchResult* self, MatchResult* other) {
    for (usz i = 0; i < other.binding_count; i++) {
        self.add_binding(other.bindings[i].name, other.bindings[i].value);
    }
}

/**
 * Check if two values are equal (for literal pattern matching).
 */
fn bool values_equal(Value* a, Value* b, usz depth = 0) {
    if (a == null && b == null) return true;
    if (a == null || b == null) return false;
    if (depth >= 256) return false;
    if (a.tag != b.tag) return false;

    switch (a.tag) {
        case NIL:
            return true;
        case INT:
            return a.int_val == b.int_val;
        case STRING:
            if (a.str_val.len != b.str_val.len) return false;
            for (usz i = 0; i < a.str_val.len; i++) {
                if (a.str_val.chars[i] != b.str_val.chars[i]) return false;
            }
            return true;
        case SYMBOL:
            return (uint)a.sym_val == (uint)b.sym_val;
        case CONS:
            return values_equal(a.cons_val.car, b.cons_val.car, depth + 1) &&
                   values_equal(a.cons_val.cdr, b.cons_val.cdr, depth + 1);
        default:
            return a == b;  // Pointer equality for closures, etc.
    }
}

/**
 * Get the length of a list.
 */
fn usz get_list_length(Value* v) {
    usz len = 0;
    while (is_cons(v)) {
        len++;
        v = cdr(v);
    }
    return len;
}

/**
 * Get the nth element of a list.
 */
fn Value* get_list_nth(Value* v, usz n) {
    for (usz i = 0; i < n; i++) {
        if (!is_cons(v)) return null;
        v = cdr(v);
    }
    if (!is_cons(v)) return null;
    return car(v);
}

/**
 * Get the rest of a list starting at index n.
 */
fn Value* get_list_rest(Value* v, usz n, Interp* interp) {
    for (usz i = 0; i < n; i++) {
        if (!is_cons(v)) return make_nil(interp);
        v = cdr(v);
    }
    return v;
}

/**
 * Match a pattern against a value.
 */
fn MatchResult match_pattern(Pattern* pat, Value* val, Interp* interp) {
    switch (pat.tag) {
        case PAT_WILDCARD:
            // Wildcard matches anything
            return match_ok();

        case PAT_VAR:
            // Variable matches anything and binds
            MatchResult r = match_ok();
            r.add_binding(pat.var_name, val);
            return r;

        case PAT_LIT:
            // Literal matches if values are equal
            if (values_equal(pat.lit_value, val)) {
                return match_ok();
            }
            return match_fail();

        case PAT_QUOTE:
            // Quoted pattern matches quoted datum
            if (values_equal(pat.quote_datum, val)) {
                return match_ok();
            }
            return match_fail();

        case PAT_CONS:
            // Cons pattern matches cons cells
            if (!is_cons(val)) {
                return match_fail();
            }
            MatchResult car_result = match_pattern(pat.car_pat, car(val), interp);
            if (!car_result.matched) {
                return match_fail();
            }
            MatchResult cdr_result = match_pattern(pat.cdr_pat, cdr(val), interp);
            if (!cdr_result.matched) {
                return match_fail();
            }
            car_result.merge(&cdr_result);
            return car_result;

        case PAT_SEQ:
            // Sequence pattern matches lists
            return match_seq_pattern(pat, val, interp);

        default:
            return match_fail();
    }
}

/**
 * Match a sequence pattern against a value.
 */
fn MatchResult match_seq_pattern(Pattern* pat, Value* val, Interp* interp) {
    usz list_len = get_list_length(val);
    usz elem_count = pat.elem_count;
    RestPosition rest_pos = pat.rest_pos;

    switch (rest_pos) {
        case REST_NONE:
            // [a b c] - exact match, list must have exactly elem_count elements
            if (list_len != elem_count) {
                return match_fail();
            }
            MatchResult result = match_ok();
            for (usz i = 0; i < elem_count; i++) {
                Value* elem = get_list_nth(val, i);
                MatchResult elem_result = match_pattern(pat.elements[i], elem, interp);
                if (!elem_result.matched) {
                    return match_fail();
                }
                result.merge(&elem_result);
            }
            return result;

        case REST_START:
            // [.. last] - match suffix, elements match the last N items
            if (list_len < elem_count) {
                return match_fail();
            }
            usz skip = list_len - elem_count;
            MatchResult start_result = match_ok();
            for (usz i = 0; i < elem_count; i++) {
                Value* elem = get_list_nth(val, skip + i);
                MatchResult elem_result = match_pattern(pat.elements[i], elem, interp);
                if (!elem_result.matched) {
                    return match_fail();
                }
                start_result.merge(&elem_result);
            }
            return start_result;

        case REST_MIDDLE:
            // [head .. tail] - elements match prefix, rest bound to rest_binding
            if (list_len < elem_count) {
                return match_fail();
            }
            MatchResult mid_result = match_ok();
            // Match prefix elements
            for (usz i = 0; i < elem_count; i++) {
                Value* elem = get_list_nth(val, i);
                MatchResult elem_result = match_pattern(pat.elements[i], elem, interp);
                if (!elem_result.matched) {
                    return match_fail();
                }
                mid_result.merge(&elem_result);
            }
            // Bind the rest
            Value* rest = get_list_rest(val, elem_count, interp);
            mid_result.add_binding(pat.rest_binding, rest);
            return mid_result;

        case REST_END:
            // [x y ..] - match prefix, ignore rest
            if (list_len < elem_count) {
                return match_fail();
            }
            MatchResult end_result = match_ok();
            for (usz i = 0; i < elem_count; i++) {
                Value* elem = get_list_nth(val, i);
                MatchResult elem_result = match_pattern(pat.elements[i], elem, interp);
                if (!elem_result.matched) {
                    return match_fail();
                }
                end_result.merge(&elem_result);
            }
            return end_result;

        default:
            return match_fail();
    }
}

/**
 * Evaluate a match expression.
 * (match expr (pattern1 result1) (pattern2 result2) ...)
 */
fn EvalResult eval_match(Expr* expr, Env* env, Interp* interp) {
    // Evaluate the scrutinee
    EvalResult scrutinee_result = eval(expr.match.scrutinee, env, interp);
    if (scrutinee_result.error.has_error) {
        return scrutinee_result;
    }
    Value* val = scrutinee_result.value;

    // Try each clause
    for (usz i = 0; i < expr.match.clause_count; i++) {
        MatchClause* clause = &expr.match.clauses[i];

        // Try to match the pattern
        MatchResult match_result = match_pattern(clause.pattern, val, interp);

        if (match_result.matched) {
            // Create new environment with pattern bindings
            Env* clause_env = make_env(interp, env);
            for (usz j = 0; j < match_result.binding_count; j++) {
                clause_env.define(
                    match_result.bindings[j].name,
                    match_result.bindings[j].value
                );
            }

            // Evaluate the result expression
            return eval(clause.result, clause_env, interp);
        }
    }

    // No pattern matched
    return eval_error_expr("no pattern matched in match expression", expr);
}

// =============================================================================
// SECTION 5: APPLICATION
// =============================================================================

/**
 * Apply a function to an argument.
 */
fn EvalResult apply(Value* func, Value* arg, Interp* interp) {
    if (func == null) {
        return eval_error("cannot apply null");
    }

    switch (func.tag) {
        case CLOSURE:
            return apply_closure(func, arg, interp);

        case PRIMITIVE:
            return apply_primitive(func, arg, interp);

        case PARTIAL_PRIM:
            return apply_partial(func, arg, interp);

        case CONTINUATION:
            return apply_continuation(func, arg, interp);

        default:
            return eval_error("not a function");
    }
}

fn EvalResult apply_closure(Value* closure, Value* arg, Interp* interp) {
    // Handle zero-arg closure: (lambda () body) - ignore the arg
    if (!closure.closure_val.has_param && !closure.closure_val.has_rest) {
        return eval(closure.closure_val.body, closure.closure_val.env, interp);
    }

    // Handle variadic closure called via single-arg application (E_APP path)
    if (closure.closure_val.has_rest) {
        usz pc = closure.closure_val.param_count;
        if (pc == 0) {
            // (lambda (.. args) body) called with 1 arg: args = (arg)
            Env* new_env = closure.closure_val.env;
            Value* rest_list = make_cons(interp, arg, make_nil(interp));
            new_env = new_env.extend(interp, closure.closure_val.rest_param, rest_list);
            return eval(closure.closure_val.body, new_env, interp);
        } else if (pc == 1) {
            // (lambda (x .. rest) body) called with 1 arg: x=arg, rest=nil
            Env* new_env = closure.closure_val.env;
            new_env = new_env.extend(interp, closure.closure_val.params[0], arg);
            new_env = new_env.extend(interp, closure.closure_val.rest_param, make_nil(interp));
            return eval(closure.closure_val.body, new_env, interp);
        } else {
            return eval_error("too few arguments for variadic lambda");
        }
    }

    // Extend the closure's captured environment with the parameter binding
    Env* call_env = closure.closure_val.env.extend(
        interp,
        closure.closure_val.param,
        arg
    );

    // Evaluate the body in the extended environment
    return eval(closure.closure_val.body, call_env, interp);
}

fn EvalResult apply_primitive(Value* prim, Value* arg, Interp* interp) {
    int arity = prim.prim_val.arity;

    // For unary or variadic primitives, apply immediately
    if (arity <= 1 || arity == -1) {
        Value*[8] args;
        args[0] = arg;
        Value* result = prim.prim_val.func(args[:1], null, interp);
        if (is_error(result)) {
            return eval_error(result.str_val.chars[:result.str_val.len]);
        }
        return eval_ok(result);
    }

    // For multi-arity primitives, create a partial application
    Value* partial = interp.alloc_value();
    partial.tag = PARTIAL_PRIM;
    partial.partial_val.func = prim.prim_val.func;
    partial.partial_val.first_arg = arg;
    partial.partial_val.remaining = arity - 1;

    return eval_ok(partial);
}

fn EvalResult apply_partial(Value* partial, Value* arg, Interp* interp) {
    // Apply the second argument to complete the partial application
    Value*[8] args;
    args[0] = partial.partial_val.first_arg;
    args[1] = arg;

    Value* result = partial.partial_val.func(args[:2], null, interp);
    if (is_error(result)) {
        return eval_error(result.str_val.chars[:result.str_val.len]);
    }
    return eval_ok(result);
}

fn EvalResult apply_continuation(Value* k_val, Value* arg, Interp* interp) {
    if (k_val.cont_val == null) {
        return eval_error("cannot resume continuation: null continuation");
    }

    CapturedCont* cc = (CapturedCont*)k_val.cont_val.data;
    if (cc == null) {
        return eval_error("cannot resume continuation: no captured state");
    }

    // Set up substitution for the replay
    interp.cont_substitution = arg;
    interp.cont_substituting = true;
    interp.cont_is_effect = cc.is_effect;
    if (cc.is_effect) {
        interp.cont_effect_tag = cc.effect_tag;
    }

    EvalResult result;

    if (cc.is_effect && cc.has_handler) {
        // Effect continuation: reinstall the handler before replaying
        if (interp.handler_count >= 16) return eval_error("handler stack overflow: too many nested handlers during continuation resume");
        interp.handler_stack[interp.handler_count] = cc.handler_copy;
        interp.handler_stack[interp.handler_count].body_expr = cc.reset_body;
        interp.handler_stack[interp.handler_count].body_env = cc.reset_env;
        interp.handler_count++;

        interp.cont_target_perform_index = cc.perform_index;
        interp.effect_perform_counter = 0;
        for (usz i = 0; i < cc.prior_count && i < 16; i++) {
            interp.effect_prior_results[i] = cc.prior_results[i];
        }

        result = eval(cc.reset_body, cc.reset_env, interp);

        // If a nested perform fired during replay and was handled,
        // effect_occurred is set. Catch it here (like eval_handle does).
        if (interp.effect_occurred) {
            interp.effect_occurred = false;
            result = eval_ok(interp.effect_result);
            // Handler was already popped by eval_perform, don't pop again
        } else {
            interp.handler_count--;
        }
    } else {
        // Shift continuation: just re-evaluate the reset body
        interp.cont_target_shift_index = cc.shift_index;
        interp.shift_counter = 0;
        for (usz i = 0; i < cc.shift_prior_count && i < 16; i++) {
            interp.shift_prior_results[i] = cc.shift_prior_results[i];
        }

        result = eval(cc.reset_body, cc.reset_env, interp);
    }

    // Clean up substitution state (in case it wasn't consumed)
    interp.cont_substituting = false;

    return result;
}

// =============================================================================
// SECTION 6: HELPER FUNCTIONS
// =============================================================================

/**
 * Check if a value is falsy (nil or false symbol).
 */
fn bool is_falsy(Value* v, Interp* interp) {
    if (v == null) return true;
    if (v.tag == NIL) return true;
    if (v.tag == SYMBOL && (uint)v.sym_val == (uint)interp.sym_false) return true;
    return false;
}

// =============================================================================
// SECTION 7: PRIMITIVES
// =============================================================================

// Curried primitive helper - stores first argument and waits for second
struct CurriedPrim {
    Value* first_arg;
    PrimitiveFn binary_fn;
}

fn Value* prim_add(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "+: expected at least 1 argument");

    if (args.len == 1) {
        // Unary +: return numeric value (identity for numbers)
        if (!is_int(args[0])) return make_error(interp, "+: expected number argument");
        long a = args[0].int_val;
        return make_int(interp, a);
    }

    if (!is_int(args[0])) return make_error(interp, "+: expected number argument");
    if (!is_int(args[1])) return make_error(interp, "+: expected number argument");
    long a = args[0].int_val;
    long b = args[1].int_val;
    return make_int(interp, a + b);
}

fn Value* prim_sub(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "-: expected at least 1 argument");
    if (args.len == 1) {
        // Unary negation: (- 5) => -5
        if (!is_int(args[0])) return make_error(interp, "-: expected number argument");
        long a = args[0].int_val;
        return make_int(interp, -a);
    }
    if (!is_int(args[0])) return make_error(interp, "-: expected number argument");
    if (!is_int(args[1])) return make_error(interp, "-: expected number argument");
    long a = args[0].int_val;
    long b = args[1].int_val;
    return make_int(interp, a - b);
}

fn Value* prim_mul(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "*: expected 2 arguments");
    if (!is_int(args[0])) return make_error(interp, "*: expected number argument");
    if (!is_int(args[1])) return make_error(interp, "*: expected number argument");
    long a = args[0].int_val;
    long b = args[1].int_val;
    return make_int(interp, a * b);
}

fn Value* prim_div(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "/: expected 2 arguments");
    if (!is_int(args[0])) return make_error(interp, "/: expected number argument");
    if (!is_int(args[1])) return make_error(interp, "/: expected number argument");
    long a = args[0].int_val;
    long b = args[1].int_val;
    if (b == 0) return make_error(interp, "/: division by zero");
    return make_int(interp, a / b);
}

fn Value* prim_mod(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "modulo: expected 2 arguments");
    if (!is_int(args[0])) return make_error(interp, "modulo: expected number argument");
    if (!is_int(args[1])) return make_error(interp, "modulo: expected number argument");
    long a = args[0].int_val;
    long b = args[1].int_val;
    if (b == 0) return make_error(interp, "modulo: division by zero");
    return make_int(interp, a % b);
}

fn Value* prim_eq(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "=: expected 2 arguments");
    bool eq = values_equal(args[0], args[1]);
    return eq ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_lt(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "<: expected 2 arguments");
    if (!is_int(args[0]) || !is_int(args[1])) return make_error(interp, "<: expected number arguments");
    bool lt = args[0].int_val < args[1].int_val;
    return lt ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_gt(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, ">: expected 2 arguments");
    if (!is_int(args[0]) || !is_int(args[1])) return make_error(interp, ">: expected number arguments");
    bool gt = args[0].int_val > args[1].int_val;
    return gt ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_le(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "<=: expected 2 arguments");
    if (!is_int(args[0]) || !is_int(args[1])) return make_error(interp, "<=: expected number arguments");
    bool le = args[0].int_val <= args[1].int_val;
    return le ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_ge(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, ">=: expected 2 arguments");
    if (!is_int(args[0]) || !is_int(args[1])) return make_error(interp, ">=: expected number arguments");
    bool ge = args[0].int_val >= args[1].int_val;
    return ge ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_cons(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "cons: expected 2 arguments");
    return make_cons(interp, args[0], args[1]);
}

fn Value* prim_car(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "car: expected 1 argument");
    if (!is_cons(args[0])) return make_error(interp, "car: argument must be a pair");
    return args[0].cons_val.car;
}

fn Value* prim_cdr(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "cdr: expected 1 argument");
    if (!is_cons(args[0])) return make_error(interp, "cdr: argument must be a pair");
    return args[0].cons_val.cdr;
}

fn Value* prim_null(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_symbol(interp, interp.sym_true);
    bool is_null = is_nil(args[0]);
    return is_null ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_pair(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool is_pair = is_cons(args[0]);
    return is_pair ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (length lst) -> int
 * Returns the number of elements in a list
 */
fn Value* prim_length(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "length: expected 1 argument");

    // nil has length 0
    if (is_nil(args[0])) {
        return make_int(interp, 0);
    }

    // Count cons cells
    if (!is_cons(args[0])) {
        return make_error(interp, "length: expected a list");
    }

    long count = 0;
    Value* current = args[0];
    while (is_cons(current)) {
        count++;
        current = current.cons_val.cdr;
    }
    return make_int(interp, count);
}

fn Value* prim_print(Value*[] args, Env* env, Interp* interp) {
    if (args.len >= 1) {
        print_value(args[0], &interp.symbols);
    }
    return make_nil(interp);
}

fn Value* prim_println(Value*[] args, Env* env, Interp* interp) {
    if (args.len >= 1) {
        println_value(args[0], &interp.symbols);
    }
    return make_nil(interp);
}

fn Value* prim_newline(Value*[] args, Env* env, Interp* interp) {
    io::printn("");
    return make_nil(interp);
}

fn Value* prim_not(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_symbol(interp, interp.sym_true);
    bool falsy = is_falsy(args[0], interp);
    return falsy ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

fn Value* prim_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len == 0) return make_nil(interp);

    Value* result = make_nil(interp);
    for (isz i = (isz)args.len - 1; i >= 0; i--) {
        result = make_cons(interp, args[(usz)i], result);
    }
    return result;
}

// =============================================================================

// =============================================================================
// SECTION 7.5: STRING PRIMITIVES
// =============================================================================

/**
 * (string-append s1 s2 ...) -> concatenated string
 * Variadic: concatenates all string arguments
 */
fn Value* prim_string_append(Value*[] args, Env* env, Interp* interp) {
    if (args.len == 0) return make_string(interp, "");

    // Calculate total length and validate all args are strings
    usz total_len = 0;
    for (usz i = 0; i < args.len; i++) {
        if (!is_string(args[i])) {
            return make_error(interp, "string-append: expected string arguments");
        }
        total_len += args[i].str_val.len;
    }

    // Clamp to max length
    if (total_len > MAX_STRING_LEN - 1) total_len = MAX_STRING_LEN - 1;

    char[MAX_STRING_LEN] buffer;
    usz pos = 0;

    for (usz i = 0; i < args.len && pos < MAX_STRING_LEN - 1; i++) {
        usz arg_len = args[i].str_val.len;
        for (usz j = 0; j < arg_len && pos < MAX_STRING_LEN - 1; j++) {
            buffer[pos++] = args[i].str_val.chars[j];
        }
    }
    buffer[pos] = 0;

    return make_string(interp, buffer[:pos]);
}

/**
 * (string-join sep list) -> joined string
 * Join list of strings with separator
 */
fn Value* prim_string_join(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "string-join: expected 2 arguments");
    if (!is_string(args[0])) return make_error(interp, "string-join: expected string separator");

    // Allow nil list to return empty string
    if (is_nil(args[1])) {
        return make_string(interp, "");
    }

    if (!is_cons(args[1])) {
        return make_error(interp, "string-join: expected list as second argument");
    }

    char* sep = &args[0].str_val.chars;
    usz sep_len = args[0].str_val.len;

    char[MAX_STRING_LEN] buffer;
    usz pos = 0;
    bool first = true;

    Value* list = args[1];
    while (is_cons(list)) {
        Value* elem = list.cons_val.car;
        list = list.cons_val.cdr;

        if (!first && pos < MAX_STRING_LEN - 1) {
            // Add separator
            for (usz i = 0; i < sep_len && pos < MAX_STRING_LEN - 1; i++) {
                buffer[pos++] = sep[i];
            }
        }
        first = false;

        if (is_string(elem)) {
            for (usz i = 0; i < elem.str_val.len && pos < MAX_STRING_LEN - 1; i++) {
                buffer[pos++] = elem.str_val.chars[i];
            }
        }
    }
    buffer[pos] = 0;

    // Truncate if too long
    if (pos > MAX_STRING_LEN - 1) pos = MAX_STRING_LEN - 1;
    return make_string(interp, buffer[:pos]);
}

/**
 * (substring s start end) -> substring
 */
fn Value* prim_substring(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3) return make_error(interp, "substring: expected 3 arguments");
    if (!is_string(args[0])) return make_error(interp, "substring: expected string as first argument");
    if (!is_int(args[1]) || !is_int(args[2])) {
        return make_error(interp, "substring: expected integer indices");
    }

    usz len = args[0].str_val.len;
    long start = args[1].int_val;
    long end = args[2].int_val;

    // Handle negative indices (Python-style)
    if (start < 0) start = (long)len + start;
    if (end < 0) end = (long)len + end;

    // Clamp to bounds
    if (start < 0) start = 0;
    if (end > (long)len) end = (long)len;
    if (start >= end) return make_string(interp, "");

    usz sub_len = (usz)(end - start);
    char[MAX_STRING_LEN] buffer;
    for (usz i = 0; i < sub_len && i < MAX_STRING_LEN - 1; i++) {
        buffer[i] = args[0].str_val.chars[(usz)start + i];
    }
    if (sub_len > MAX_STRING_LEN - 1) sub_len = MAX_STRING_LEN - 1;
    buffer[sub_len] = 0;

    return make_string(interp, buffer[:sub_len]);
}

/**
 * (string-split s sep) -> list of strings
 */
fn Value* prim_string_split(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "string-split: expected 2 arguments");
    if (!is_string(args[0]) || !is_string(args[1])) {
        return make_error(interp, "string-split: expected string arguments");
    }
    if (args[1].str_val.len == 0) {
        return make_error(interp, "string-split: empty separator");
    }

    char* str = &args[0].str_val.chars;
    usz str_len = args[0].str_val.len;
    // NOTE: Currently only supports single-character delimiters.
    // The first character of the separator string is used.
    char delim = args[1].str_val.chars[0];

    // Collect parts in reverse order, then reverse
    Value* parts = make_nil(interp);
    usz start = 0;

    for (usz i = 0; i <= str_len; i++) {
        if (i == str_len || str[i] == delim) {
            // Extract substring [start..i)
            usz part_len = i - start;
            if (part_len > MAX_STRING_LEN - 1) part_len = MAX_STRING_LEN - 1;
            char[MAX_STRING_LEN] buffer;
            for (usz j = 0; j < part_len; j++) {
                buffer[j] = str[start + j];
            }
            buffer[part_len] = 0;
            Value* part = make_string(interp, buffer[:part_len]);
            parts = make_cons(interp, part, parts);
            start = i + 1;
        }
    }

    // Reverse the list
    Value* result = make_nil(interp);
    while (is_cons(parts)) {
        result = make_cons(interp, parts.cons_val.car, result);
        parts = parts.cons_val.cdr;
    }
    return result;
}

/**
 * (string-length s) -> int
 */
fn Value* prim_string_length(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_int(interp, 0);
    }
    return make_int(interp, (long)args[0].str_val.len);
}

/**
 * (string->list s) -> list of single-char strings
 */
fn Value* prim_string_to_list(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "string->list: expected 1 argument");
    if (!is_string(args[0])) {
        return make_error(interp, "string->list: expected string argument");
    }

    Value* result = make_nil(interp);
    usz len = args[0].str_val.len;

    // Build list in reverse order
    for (isz i = (isz)len - 1; i >= 0; i--) {
        char[2] ch;
        ch[0] = args[0].str_val.chars[(usz)i];
        ch[1] = 0;
        Value* char_str = make_string(interp, ch[0..1]);
        result = make_cons(interp, char_str, result);
    }
    return result;
}

/**
 * (list->string chars) -> string
 * Convert a list of single-character strings to a string
 */
fn Value* prim_list_to_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) {
        return make_error(interp, "list->string: expected 1 argument");
    }

    // Allow nil to return empty string
    if (is_nil(args[0])) {
        return make_string(interp, "");
    }

    if (!is_cons(args[0])) {
        return make_error(interp, "list->string: expected a list");
    }

    char[MAX_STRING_LEN] buffer;
    usz pos = 0;

    Value* list = args[0];
    while (is_cons(list) && pos < MAX_STRING_LEN - 1) {
        Value* elem = list.cons_val.car;
        list = list.cons_val.cdr;

        if (is_string(elem) && elem.str_val.len > 0) {
            // Take first character of the string
            buffer[pos++] = elem.str_val.chars[0];
        }
    }
    buffer[pos] = 0;

    return make_string(interp, buffer[:pos]);
}

/**
 * (string-upcase s) -> uppercase string
 */
fn Value* prim_string_upcase(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "string-upcase: expected 1 argument");
    if (!is_string(args[0])) {
        return make_error(interp, "string-upcase: expected string argument");
    }

    char[MAX_STRING_LEN] buffer;
    usz len = args[0].str_val.len;
    if (len > MAX_STRING_LEN - 1) len = MAX_STRING_LEN - 1;

    for (usz i = 0; i < len; i++) {
        char c = args[0].str_val.chars[i];
        // Convert lowercase a-z to uppercase A-Z
        if (c >= 'a' && c <= 'z') {
            buffer[i] = c - 32;  // 'a' - 'A' = 32
        } else {
            buffer[i] = c;
        }
    }
    buffer[len] = 0;

    return make_string(interp, buffer[:len]);
}

/**
 * (string-downcase s) -> lowercase string
 */
fn Value* prim_string_downcase(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "string-downcase: expected 1 argument");
    if (!is_string(args[0])) {
        return make_error(interp, "string-downcase: expected string argument");
    }

    char[MAX_STRING_LEN] buffer;
    usz len = args[0].str_val.len;
    if (len > MAX_STRING_LEN - 1) len = MAX_STRING_LEN - 1;

    for (usz i = 0; i < len; i++) {
        char c = args[0].str_val.chars[i];
        // Convert uppercase A-Z to lowercase a-z
        if (c >= 'A' && c <= 'Z') {
            buffer[i] = c + 32;  // 'a' - 'A' = 32
        } else {
            buffer[i] = c;
        }
    }
    buffer[len] = 0;

    return make_string(interp, buffer[:len]);
}

/**
 * (string-trim s) -> string with leading/trailing whitespace removed
 */
fn Value* prim_string_trim(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_error(interp, "string-trim: expected 1 argument");
    if (!is_string(args[0])) {
        return make_error(interp, "string-trim: expected string argument");
    }

    char* str = &args[0].str_val.chars;
    usz len = args[0].str_val.len;

    // Find start (skip leading whitespace)
    usz start = 0;
    while (start < len && (str[start] == ' ' || str[start] == '\t' ||
                           str[start] == '\n' || str[start] == '\r')) {
        start++;
    }

    // Find end (skip trailing whitespace)
    usz end = len;
    while (end > start && (str[end - 1] == ' ' || str[end - 1] == '\t' ||
                           str[end - 1] == '\n' || str[end - 1] == '\r')) {
        end--;
    }

    // Empty result
    if (start >= end) {
        return make_string(interp, "");
    }

    // Copy trimmed string
    usz trim_len = end - start;
    if (trim_len > MAX_STRING_LEN - 1) trim_len = MAX_STRING_LEN - 1;

    char[MAX_STRING_LEN] buffer;
    for (usz i = 0; i < trim_len; i++) {
        buffer[i] = str[start + i];
    }
    buffer[trim_len] = 0;

    return make_string(interp, buffer[:trim_len]);
}

/**
 * (string? v) -> true if v is a string
 */
fn Value* prim_is_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool result = is_string(args[0]);
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (int? v) -> true if v is an integer
 */
fn Value* prim_is_int(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool result = is_int(args[0]);
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (symbol? v) -> true if v is a symbol
 */
fn Value* prim_is_symbol(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool result = args[0] != null && args[0].tag == SYMBOL;
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (closure? v) -> true if v is a closure (user-defined function)
 */
fn Value* prim_is_closure(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool result = args[0] != null && args[0].tag == CLOSURE;
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

/**
 * (continuation? v) -> true if v is a continuation
 */
fn Value* prim_is_continuation(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    bool result = args[0] != null && args[0].tag == CONTINUATION;
    return result ? make_symbol(interp, interp.sym_true) : make_nil(interp);
}

// =============================================================================
// SECTION 7.6: FILE I/O PRIMITIVES
// =============================================================================

/**
 * (read-file path) -> string contents or nil on error
 */
fn Value* prim_read_file(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_nil(interp);
    }

    // Get path as slice (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];

    // Try to load file using C3's file::load_temp
    if (try content = io::file::load_temp((String)path)) {
        return make_string(interp, content);
    }
    return make_nil(interp);
}

/**
 * (write-file path content) -> true on success, nil on error
 */
fn Value* prim_write_file(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2 || !is_string(args[0]) || !is_string(args[1])) {
        return make_nil(interp);
    }

    // Get path and content as slices (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];
    char[] content = args[1].str_val.chars[:args[1].str_val.len];

    // Open file for writing
    if (try file = io::file::open((String)path, "w")) {
        if (try bytes_written = file.write(content)) {
            file.close()!!;
            return make_symbol(interp, interp.sym_true);
        }
        file.close()!!;
    }
    return make_nil(interp);
}

/**
 * (file-exists? path) -> true if file exists, nil otherwise
 */
fn Value* prim_file_exists(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_nil(interp);
    }

    // Get path as slice (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];

    // Use file::is_file to check existence
    if (io::file::is_file((String)path)) {
        return make_symbol(interp, interp.sym_true);
    }
    return make_nil(interp);
}

/**
 * (read-lines path) -> list of strings (one per line)
 */
fn Value* prim_read_lines(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_nil(interp);
    }

    // Get path as slice (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];

    // Try to load file using C3's file::load_temp
    if (try content = io::file::load_temp((String)path)) {
        // Split content into lines
        Value* lines = make_nil(interp);
        usz start = 0;
        usz content_len = content.len;

        for (usz i = 0; i <= content_len; i++) {
            if (i == content_len || content[i] == '\n') {
                // Extract line [start..i)
                usz line_len = i - start;
                // Skip trailing \r if present (Windows line endings)
                if (line_len > 0 && content[start + line_len - 1] == '\r') {
                    line_len--;
                }
                if (line_len > MAX_STRING_LEN - 1) line_len = MAX_STRING_LEN - 1;
                char[MAX_STRING_LEN] buffer;
                for (usz j = 0; j < line_len; j++) {
                    buffer[j] = content[start + j];
                }
                buffer[line_len] = 0;
                Value* line = make_string(interp, buffer[:line_len]);
                lines = make_cons(interp, line, lines);
                start = i + 1;
            }
        }

        // Reverse the list to get correct order
        Value* result = make_nil(interp);
        while (is_cons(lines)) {
            result = make_cons(interp, lines.cons_val.car, result);
            lines = lines.cons_val.cdr;
        }
        return result;
    }
    return make_nil(interp);
}

/**
 * (load path) -> evaluates all expressions in the file, returns last result
 * Reads a script file and evaluates it in the current environment.
 */
fn Value* prim_load(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) {
        return make_nil(interp);
    }

    // Get path as slice (using :len for exclusive end)
    char[] path = args[0].str_val.chars[:args[0].str_val.len];

    // Try to load file
    if (try content = io::file::load_temp((String)path)) {
        char[] source = content;

        // Run all expressions in the file
        EvalResult r = run_program(source, interp);
        if (r.error.has_error) {
            return make_nil(interp);
        }
        return r.value;
    }
    return make_nil(interp);
}

fn Value* prim_string_to_number(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_string(args[0])) return make_nil(interp);
    char[] s = args[0].str_val.chars[:args[0].str_val.len];
    if (s.len == 0) return make_nil(interp);
    usz i = 0;
    bool negative = false;
    if (s[0] == '-') { negative = true; i = 1; }
    if (i >= s.len) return make_nil(interp);
    long val = 0;
    while (i < s.len) {
        if (s[i] < '0' || s[i] > '9') return make_nil(interp);
        val = val * 10 + (long)(s[i] - '0');
        i++;
    }
    if (negative) val = -val;
    return make_int(interp, val);
}

fn Value* prim_number_to_string(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || !is_int(args[0])) return make_error(interp, "number->string: expected int");
    long n = args[0].int_val;
    char[24] buf;
    usz len = 0;
    if (n == 0) {
        buf[0] = '0';
        len = 1;
    } else {
        bool negative = n < 0;
        if (negative) n = -n;
        // Reverse digit extraction
        usz start = 0;
        while (n > 0) {
            buf[len] = (char)('0' + (char)(n % 10));
            len++;
            n = n / 10;
        }
        if (negative) { buf[len] = '-'; len++; }
        // Reverse in-place
        for (usz j = 0; j < len / 2; j++) {
            char tmp = buf[j];
            buf[j] = buf[len - 1 - j];
            buf[len - 1 - j] = tmp;
        }
    }
    return make_string(interp, buf[:len]);
}

fn Value* prim_gensym(Value*[] args, Env* env, Interp* interp) {
    char[64] buf;
    buf[0] = 'g'; buf[1] = '#';
    usz len = 2;
    interp.gensym_counter++;
    long n = (long)interp.gensym_counter;
    // Convert number to string
    char[20] digits;
    usz dlen = 0;
    if (n == 0) { digits[0] = '0'; dlen = 1; }
    else {
        while (n > 0) { digits[dlen] = (char)('0' + (char)(n % 10)); dlen++; n = n / 10; }
    }
    for (usz j = 0; j < dlen; j++) {
        buf[len + j] = digits[dlen - 1 - j];
    }
    len += dlen;
    SymbolId id = interp.symbols.intern(buf[:len]);
    if (id == INVALID_SYMBOL_ID) return make_error(interp, "symbol table exhausted");
    return make_symbol(interp, id);
}

fn Value* prim_hashmap_p(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1) return make_nil(interp);
    if (args[0] != null && args[0].tag == HASHMAP) return make_symbol(interp, interp.sym_true);
    return make_nil(interp);
}

// =============================================================================
// HASH MAP OPERATIONS
// =============================================================================

fn uint fnv1a(char[] data) {
    uint h = 2166136261;
    for (usz i = 0; i < data.len; i++) { h ^= (uint)data[i]; h *= 16777619; }
    return h;
}

fn uint murmur_finalizer(uint h) {
    h ^= h >> 16; h *= 0x85ebca6b; h ^= h >> 13; h *= 0xc2b2ae35; h ^= h >> 16;
    return h;
}

fn uint hash_value(Value* key) {
    if (key == null) return 0;
    switch (key.tag) {
        case INT: return murmur_finalizer((uint)key.int_val);
        case STRING: return fnv1a(key.str_val.chars[:key.str_val.len]);
        case SYMBOL: return murmur_finalizer((uint)key.sym_val);
        default: return 0;
    }
}

fn HashMap* hashmap_new(uint capacity, Interp* interp) {
    // Allocate HashMap struct in root_region
    HashMap hm;
    hm.capacity = capacity;
    hm.count = 0;
    hm.mask = capacity - 1;
    main::ObjectHandle hm_handle = main::allocate_in(interp.root_region, HashMap, hm);
    HashMap* map = main::dereference_as(HashMap, hm_handle);

    // Allocate entries array via malloc (contiguous needed for indexing)
    map.entries = (HashEntry*)mem::malloc(HashEntry.sizeof * capacity);
    for (uint i = 0; i < capacity; i++) {
        map.entries[i].key = null;
        map.entries[i].value = null;
    }

    return map;
}

fn Value* hashmap_get(HashMap* map, Value* key) {
    uint h = hash_value(key) & map.mask;
    for (uint i = 0; i < map.capacity; i++) {
        uint idx = (h + i) & map.mask;
        HashEntry* entry = &map.entries[idx];
        if (entry.key == null) return null;  // empty slot
        if (values_equal(entry.key, key)) return entry.value;
    }
    return null;
}

fn void hashmap_grow(HashMap* map, Interp* interp) {
    uint old_cap = map.capacity;
    HashEntry* old_entries = map.entries;
    uint new_cap = old_cap * 2;
    uint new_mask = new_cap - 1;

    // Allocate new entries array via malloc
    HashEntry* new_entries = (HashEntry*)mem::malloc(HashEntry.sizeof * new_cap);
    for (uint i = 0; i < new_cap; i++) {
        new_entries[i].key = null;
        new_entries[i].value = null;
    }

    map.entries = new_entries;
    map.capacity = new_cap;
    map.mask = new_mask;
    map.count = 0;

    // Rehash
    for (uint i = 0; i < old_cap; i++) {
        if (old_entries[i].key != null) {
            uint h = hash_value(old_entries[i].key) & new_mask;
            for (uint j = 0; j < new_cap; j++) {
                uint idx = (h + j) & new_mask;
                if (new_entries[idx].key == null) {
                    new_entries[idx].key = old_entries[i].key;
                    new_entries[idx].value = old_entries[i].value;
                    map.count++;
                    break;
                }
            }
        }
    }
    mem::free(old_entries);
}

fn void hashmap_set(HashMap* map, Value* key, Value* value, Interp* interp) {
    // Grow at 70% load
    if (map.count * 10 >= map.capacity * 7) {
        hashmap_grow(map, interp);
    }

    uint h = hash_value(key) & map.mask;
    for (uint i = 0; i < map.capacity; i++) {
        uint idx = (h + i) & map.mask;
        HashEntry* entry = &map.entries[idx];
        if (entry.key == null) {
            entry.key = key;
            entry.value = value;
            map.count++;
            return;
        }
        if (values_equal(entry.key, key)) {
            entry.value = value;
            return;
        }
    }
}

fn bool hashmap_remove(HashMap* map, Value* key) {
    uint h = hash_value(key) & map.mask;
    for (uint i = 0; i < map.capacity; i++) {
        uint idx = (h + i) & map.mask;
        HashEntry* entry = &map.entries[idx];
        if (entry.key == null) return false;
        if (values_equal(entry.key, key)) {
            // Backward shift delete
            uint j = idx;
            for (;;) {
                uint next = (j + 1) & map.mask;
                if (map.entries[next].key == null) break;
                uint natural = hash_value(map.entries[next].key) & map.mask;
                // Check if next entry would be displaced by removing j
                // If natural position is between (j+1) and next, it's fine where it is
                bool should_move;
                if (j < next) {
                    should_move = natural <= j || natural > next;
                } else {
                    should_move = natural <= j && natural > next;
                }
                if (!should_move) break;
                map.entries[j] = map.entries[next];
                j = next;
            }
            map.entries[j].key = null;
            map.entries[j].value = null;
            map.count--;
            return true;
        }
    }
    return false;
}

fn Value* make_hashmap(Interp* interp, uint capacity) {
    // Allocate value in root_region so it persists
    main::RegionHandle saved = interp.current_frame;
    interp.current_frame = interp.root_region;
    Value* v = interp.alloc_value();
    v.tag = HASHMAP;
    v.hashmap_val = hashmap_new(capacity, interp);
    interp.current_frame = saved;
    return v;
}

// Hash map primitives
fn Value* prim_hash_map(Value*[] args, Env* env, Interp* interp) {
    if (args.len % 2 != 0) return make_error(interp, "hash-map: expected even number of args");
    uint cap = 16;
    while (cap < (uint)args.len * 2) cap *= 2;
    Value* v = make_hashmap(interp, cap);
    for (usz i = 0; i < args.len; i += 2) {
        hashmap_set(v.hashmap_val, args[i], args[i + 1], interp);
    }
    return v;
}

fn Value* prim_hash_ref(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "hash-ref: expected 2 args");
    if (args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "hash-ref: expected hash-map");
    Value* result = hashmap_get(args[0].hashmap_val, args[1]);
    if (result == null) return make_nil(interp);
    return result;
}

fn Value* prim_hash_set(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3) return make_error(interp, "hash-set!: expected 3 args");
    if (args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "hash-set!: expected hash-map");
    hashmap_set(args[0].hashmap_val, args[1], args[2], interp);
    return args[0];
}

fn Value* prim_hash_has(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "hash-has?: expected 2 args");
    if (args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "hash-has?: expected hash-map");
    Value* result = hashmap_get(args[0].hashmap_val, args[1]);
    if (result != null) return make_symbol(interp, interp.sym_true);
    return make_nil(interp);
}

fn Value* prim_hash_remove(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "hash-remove!: expected 2 args");
    if (args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "hash-remove!: expected hash-map");
    hashmap_remove(args[0].hashmap_val, args[1]);
    return args[0];
}

fn Value* prim_hash_keys(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "hash-keys: expected hash-map");
    HashMap* map = args[0].hashmap_val;
    Value* result = make_nil(interp);
    for (uint i = 0; i < map.capacity; i++) {
        if (map.entries[i].key != null) {
            result = make_cons(interp, map.entries[i].key, result);
        }
    }
    return result;
}

fn Value* prim_hash_values(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "hash-values: expected hash-map");
    HashMap* map = args[0].hashmap_val;
    Value* result = make_nil(interp);
    for (uint i = 0; i < map.capacity; i++) {
        if (map.entries[i].key != null) {
            result = make_cons(interp, map.entries[i].value, result);
        }
    }
    return result;
}

fn Value* prim_hash_count(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != HASHMAP) return make_error(interp, "hash-count: expected hash-map");
    return make_int(interp, (long)args[0].hashmap_val.count);
}

// =============================================================================
// SECTION 7.8: FFI PRIMITIVES
// =============================================================================

/**
 * Convert a Pika value to a long based on FFI type symbol.
 * Returns the value as a long suitable for passing through function pointer casts.
 */
fn long ffi_value_to_long(Value* val, char[] type_name, Interp* interp) {
    if (type_name.len == 3 && type_name[0] == 'i' && type_name[1] == 'n' && type_name[2] == 't') {
        // 'int
        if (val != null && val.tag == INT) return val.int_val;
        return 0;
    }
    if (type_name.len == 4 && type_name[0] == 's' && type_name[1] == 'i' && type_name[2] == 'z' && type_name[3] == 'e') {
        // 'size
        if (val != null && val.tag == INT) return val.int_val;
        return 0;
    }
    if (type_name.len == 6 && type_name[0] == 's' && type_name[1] == 't' && type_name[2] == 'r') {
        // 'string
        if (val != null && val.tag == STRING) return (long)&val.str_val.chars;
        return 0;
    }
    if (type_name.len == 3 && type_name[0] == 'p' && type_name[1] == 't' && type_name[2] == 'r') {
        // 'ptr
        if (val != null && val.tag == FFI_HANDLE) return (long)val.ffi_val.lib_handle;
        if (val != null && val.tag == INT) return val.int_val;
        return 0;
    }
    if (type_name.len == 6 && type_name[0] == 'd' && type_name[1] == 'o' && type_name[2] == 'u') {
        // 'double — cast int to double bits through union trick
        if (val != null && val.tag == INT) return val.int_val;
        return 0;
    }
    return 0;
}

/**
 * Convert a C long result back to a Pika value based on FFI return type symbol.
 */
fn Value* ffi_long_to_value(long result, char[] type_name, Interp* interp) {
    if (type_name.len == 3 && type_name[0] == 'i' && type_name[1] == 'n' && type_name[2] == 't') {
        return make_int(interp, result);
    }
    if (type_name.len == 4 && type_name[0] == 's' && type_name[1] == 'i' && type_name[2] == 'z' && type_name[3] == 'e') {
        return make_int(interp, result);
    }
    if (type_name.len == 6 && type_name[0] == 's' && type_name[1] == 't' && type_name[2] == 'r') {
        // 'string — result is a char*
        char* ptr = (char*)(uptr)result;
        if (ptr == null) return make_nil(interp);
        // Measure length
        usz len = 0;
        while (ptr[len] != 0 && len < MAX_STRING_LEN - 1) len++;
        return make_string(interp, ptr[:len]);
    }
    if (type_name.len == 4 && type_name[0] == 'v' && type_name[1] == 'o' && type_name[2] == 'i' && type_name[3] == 'd') {
        return make_nil(interp);
    }
    if (type_name.len == 3 && type_name[0] == 'p' && type_name[1] == 't' && type_name[2] == 'r') {
        return make_int(interp, result);
    }
    if (type_name.len == 6 && type_name[0] == 'd' && type_name[1] == 'o' && type_name[2] == 'u') {
        return make_int(interp, result);
    }
    return make_nil(interp);
}

/**
 * (ffi-open "libc.so.6") => V_FFI_HANDLE
 */
fn Value* prim_ffi_open(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != STRING) {
        return make_error(interp, "ffi-open: expected string argument");
    }
    char[] name = args[0].str_val.chars[:args[0].str_val.len];
    void* handle = dlopen((ZString)&args[0].str_val.chars, RTLD_LAZY);
    if (handle == null) {
        char* err = dlerror();
        if (err != null) {
            // Build error message
            usz elen = 0;
            while (err[elen] != 0 && elen < 200) elen++;
            return make_error(interp, err[:elen]);
        }
        return make_error(interp, "ffi-open: dlopen failed");
    }
    return make_ffi_handle(interp, handle, name);
}

/**
 * (ffi-call handle "func_name" 'return-type arg1 'type1 arg2 'type2 ...)
 * Variadic: minimum 3 args (handle, name, return type).
 * After return type, args come in pairs: (value, type_symbol).
 */
fn Value* prim_ffi_call(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 3) {
        return make_error(interp, "ffi-call: expected at least 3 args (handle, name, return-type)");
    }
    // Arg 0: FFI handle
    if (args[0] == null || args[0].tag != FFI_HANDLE) {
        return make_error(interp, "ffi-call: first arg must be an ffi-handle");
    }
    if (args[0].ffi_val.lib_handle == null) {
        return make_error(interp, "ffi-call: handle is closed");
    }
    // Arg 1: function name (string)
    if (args[1] == null || args[1].tag != STRING) {
        return make_error(interp, "ffi-call: second arg must be a string (function name)");
    }
    // Arg 2: return type (symbol)
    if (args[2] == null || args[2].tag != SYMBOL) {
        return make_error(interp, "ffi-call: third arg must be a symbol (return type)");
    }

    // Look up the function
    void* fn_ptr = dlsym(args[0].ffi_val.lib_handle, (ZString)&args[1].str_val.chars);
    if (fn_ptr == null) {
        char* err = dlerror();
        if (err != null) {
            usz elen = 0;
            while (err[elen] != 0 && elen < 200) elen++;
            return make_error(interp, err[:elen]);
        }
        return make_error(interp, "ffi-call: dlsym failed");
    }

    // Parse arg pairs: (value, type_symbol) after the first 3 args
    usz remaining = args.len - 3;
    if (remaining % 2 != 0) {
        return make_error(interp, "ffi-call: args after return-type must be (value type) pairs");
    }
    usz c_argc = remaining / 2;
    if (c_argc > 6) {
        return make_error(interp, "ffi-call: max 6 C arguments supported");
    }

    // Convert args to longs
    long[6] c_args;
    for (usz i = 0; i < c_argc; i++) {
        Value* val = args[3 + i * 2];
        Value* type_sym = args[3 + i * 2 + 1];
        if (type_sym == null || type_sym.tag != SYMBOL) {
            return make_error(interp, "ffi-call: type annotation must be a symbol");
        }
        char[] type_name = interp.symbols.get_name(type_sym.sym_val);
        c_args[i] = ffi_value_to_long(val, type_name, interp);
    }

    // Get return type name
    char[] ret_type = interp.symbols.get_name(args[2].sym_val);

    // Call the function based on arg count
    long result;
    switch (c_argc) {
        case 0:
            result = ((FfiFn0)(uptr)fn_ptr)();
        case 1:
            result = ((FfiFn1)(uptr)fn_ptr)(c_args[0]);
        case 2:
            result = ((FfiFn2)(uptr)fn_ptr)(c_args[0], c_args[1]);
        case 3:
            result = ((FfiFn3)(uptr)fn_ptr)(c_args[0], c_args[1], c_args[2]);
        case 4:
            result = ((FfiFn4)(uptr)fn_ptr)(c_args[0], c_args[1], c_args[2], c_args[3]);
        case 5:
            result = ((FfiFn5)(uptr)fn_ptr)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4]);
        case 6:
            result = ((FfiFn6)(uptr)fn_ptr)(c_args[0], c_args[1], c_args[2], c_args[3], c_args[4], c_args[5]);
        default:
            return make_error(interp, "ffi-call: unreachable arg count");
    }

    return ffi_long_to_value(result, ret_type, interp);
}

/**
 * (ffi-close handle) => nil
 */
fn Value* prim_ffi_close(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 1 || args[0] == null || args[0].tag != FFI_HANDLE) {
        return make_error(interp, "ffi-close: expected ffi-handle argument");
    }
    if (args[0].ffi_val.lib_handle != null) {
        dlclose(args[0].ffi_val.lib_handle);
        args[0].ffi_val.lib_handle = null;
    }
    return make_nil(interp);
}

/**
 * (ffi-sym handle "func_name") => int (pointer as integer)
 */
fn Value* prim_ffi_sym(Value*[] args, Env* env, Interp* interp) {
    if (args.len < 2) return make_error(interp, "ffi-sym: expected 2 args");
    if (args[0] == null || args[0].tag != FFI_HANDLE) {
        return make_error(interp, "ffi-sym: first arg must be ffi-handle");
    }
    if (args[0].ffi_val.lib_handle == null) {
        return make_error(interp, "ffi-sym: handle is closed");
    }
    if (args[1] == null || args[1].tag != STRING) {
        return make_error(interp, "ffi-sym: second arg must be a string");
    }
    void* sym = dlsym(args[0].ffi_val.lib_handle, (ZString)&args[1].str_val.chars);
    if (sym == null) return make_int(interp, 0);
    return make_int(interp, (long)(uptr)sym);
}

// SECTION 8: INTERPRETER INITIALIZATION
// =============================================================================

/**
 * Register all primitive functions in the interpreter.
 */
fn void register_primitives(Interp* interp) {
    // Register true, false, and nil as symbols bound to their values
    Value* true_val = make_symbol(interp, interp.sym_true);
    Value* false_val = make_nil(interp);  // false is nil
    Value* nil_val = make_nil(interp);
    interp.global_env.define(interp.sym_true, true_val);
    interp.global_env.define(interp.sym_false, false_val);
    SymbolId sym_nil = interp.symbols.intern("nil");
    interp.global_env.define(sym_nil, nil_val);

    // Arithmetic
    register_prim(interp, "+", &prim_add, 2);
    register_prim(interp, "-", &prim_sub, 2);
    register_prim(interp, "*", &prim_mul, 2);
    register_prim(interp, "/", &prim_div, 2);
    register_prim(interp, "%", &prim_mod, 2);

    // Comparison
    register_prim(interp, "=", &prim_eq, 2);
    register_prim(interp, "<", &prim_lt, 2);
    register_prim(interp, ">", &prim_gt, 2);
    register_prim(interp, "<=", &prim_le, 2);
    register_prim(interp, ">=", &prim_ge, 2);

    // List operations
    register_prim(interp, "cons", &prim_cons, 2);
    register_prim(interp, "car", &prim_car, 1);
    register_prim(interp, "cdr", &prim_cdr, 1);
    register_prim(interp, "null?", &prim_null, 1);
    register_prim(interp, "pair?", &prim_pair, 1);
    register_prim(interp, "list", &prim_list, -1);
    register_prim(interp, "length", &prim_length, 1);

    // Boolean
    register_prim(interp, "not", &prim_not, 1);

    // I/O
    register_prim(interp, "print", &prim_print, 1);
    register_prim(interp, "println", &prim_println, 1);
    register_prim(interp, "newline", &prim_newline, 0);

    // String primitives
    register_prim(interp, "string-append", &prim_string_append, -1);
    register_prim(interp, "string-join", &prim_string_join, 2);
    register_prim(interp, "substring", &prim_substring, 3);
    register_prim(interp, "string-split", &prim_string_split, 2);
    register_prim(interp, "string-length", &prim_string_length, 1);
    register_prim(interp, "string->list", &prim_string_to_list, 1);
    register_prim(interp, "list->string", &prim_list_to_string, 1);
    register_prim(interp, "string-upcase", &prim_string_upcase, 1);
    register_prim(interp, "string-downcase", &prim_string_downcase, 1);
    register_prim(interp, "string-trim", &prim_string_trim, 1);
    register_prim(interp, "string?", &prim_is_string, 1);
    register_prim(interp, "int?", &prim_is_int, 1);
    register_prim(interp, "symbol?", &prim_is_symbol, 1);
    register_prim(interp, "closure?", &prim_is_closure, 1);
    register_prim(interp, "continuation?", &prim_is_continuation, 1);

    // File I/O primitives
    register_prim(interp, "read-file", &prim_read_file, 1);
    register_prim(interp, "write-file", &prim_write_file, 2);
    register_prim(interp, "file-exists?", &prim_file_exists, 1);
    register_prim(interp, "read-lines", &prim_read_lines, 1);
    register_prim(interp, "load", &prim_load, 1);

    // Conversion primitives
    register_prim(interp, "string->number", &prim_string_to_number, 1);
    register_prim(interp, "number->string", &prim_number_to_string, 1);

    // Gensym
    register_prim(interp, "gensym", &prim_gensym, 0);

    // Hash map primitives
    register_prim(interp, "hash-map?", &prim_hashmap_p, 1);
    register_prim(interp, "hash-map", &prim_hash_map, -1);
    register_prim(interp, "hash-ref", &prim_hash_ref, 2);
    register_prim(interp, "hash-set!", &prim_hash_set, 3);
    register_prim(interp, "hash-has?", &prim_hash_has, 2);
    register_prim(interp, "hash-remove!", &prim_hash_remove, 2);
    register_prim(interp, "hash-keys", &prim_hash_keys, 1);
    register_prim(interp, "hash-values", &prim_hash_values, 1);
    register_prim(interp, "hash-count", &prim_hash_count, 1);

    // FFI primitives
    register_prim(interp, "ffi-open", &prim_ffi_open, 1);
    register_prim(interp, "ffi-call", &prim_ffi_call, -1);
    register_prim(interp, "ffi-close", &prim_ffi_close, 1);
    register_prim(interp, "ffi-sym", &prim_ffi_sym, 2);
}

fn void register_prim(Interp* interp, char[] name, PrimitiveFn func, int arity) {
    SymbolId sym = interp.symbols.intern(name);
    Value* prim = make_primitive(interp, name, func, arity);
    interp.global_env.define(sym, prim);
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

    EvalResult result = eval_ok(make_nil(interp));

    foreach (expr : exprs) {
        result = eval(expr, interp.global_env, interp);
        if (result.error.has_error) {
            break;
        }
    }

    exprs.free();
    return result;
}

/**
 * Run a single expression.
 */
fn EvalResult run(char[] source, Interp* interp) {
    Lexer lex;
    lex.init(source);
    Parser p;
    p.init(&lex, interp);

    Expr* expr = p.parse_expr();

    if (p.has_error) {
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

    return eval(expr, interp.global_env, interp);
}

/**
 * Register standard library functions defined in Pika itself.
 * Called after register_primitives().
 */
fn void register_stdlib(Interp* interp) {
    // =========================================================================
    // Standard Macros (defined before HOFs since macros may be used by them)
    // =========================================================================

    // when: (when test body...) - evaluate body if test is truthy
    run("(define [macro] when ([test .. body] (if test (begin .. body) nil)))", interp);

    // unless: (unless test body...) - evaluate body if test is falsy
    run("(define [macro] unless ([test .. body] (if test nil (begin .. body))))", interp);

    // cond: (cond test1 body1 test2 body2 ...) - multi-clause conditional
    run("(define [macro] cond ([] nil) ([test body .. rest] (if test body (cond .. rest))))", interp);

    // with-trampoline: effect-based trampoline for stack-safe non-tail recursion.
    // Usage: (with-trampoline (lambda () (fib 10)))
    // Inside the thunk, use (perform bounce (lambda () ...)) to bounce.
    // Thunks use a dummy parameter: (lambda (xx) body), called with (thunk nil)
    run("(define with-trampoline (lambda (thunk) (handle (thunk nil) ((bounce k next-thunk) (k (with-trampoline next-thunk))))))", interp);

    // =========================================================================
    // Higher-Order Functions (all curried, list-based)
    // =========================================================================

    // map: (map f lst) - apply f to each element, return new list
    // Iterative: accumulator + reverse for O(1) stack via TCO
    run("(define map (lambda (f) (lambda (lst) (let loop ((xs lst) (acc nil)) (if (null? xs) (reverse acc) (loop (cdr xs) (cons (f (car xs)) acc)))))))", interp);

    // filter: (filter pred lst) - keep elements where (pred x) is truthy
    // Iterative: accumulator + reverse for O(1) stack via TCO
    run("(define filter (lambda (pred) (lambda (lst) (let loop ((xs lst) (acc nil)) (if (null? xs) (reverse acc) (if (pred (car xs)) (loop (cdr xs) (cons (car xs) acc)) (loop (cdr xs) acc)))))))", interp);

    // foldl: (foldl f acc lst) - left fold
    run("(define foldl (lambda (f) (lambda (acc) (lambda (lst) (if (null? lst) acc (((foldl f) ((f acc) (car lst))) (cdr lst)))))))", interp);

    // foldr: (foldr f init lst) - right fold
    run("(define foldr (lambda (f) (lambda (init) (lambda (lst) (if (null? lst) init ((f (car lst)) (((foldr f) init) (cdr lst))))))))", interp);

    // append: (append a b) - concatenate two lists
    run("(define append (lambda (a) (lambda (b) (if (null? a) b (cons (car a) ((append (cdr a)) b))))))", interp);

    // reverse: (reverse lst) - reverse a list using foldl
    run("(define reverse (lambda (lst) (((foldl (lambda (acc) (lambda (x) (cons x acc)))) nil) lst)))", interp);

    // compose: (compose f g) - function composition, returns (lambda (x) (f (g x)))
    run("(define compose (lambda (f) (lambda (g) (lambda (x) (f (g x))))))", interp);

    // id: identity function
    run("(define id (lambda (x) x))", interp);

    // nth: (nth n lst) - get nth element (0-indexed)
    run("(define nth (lambda (n) (lambda (lst) (if (= n 0) (car lst) ((nth (- n 1)) (cdr lst))))))", interp);

    // take: (take n lst) - first n elements
    run("(define take (lambda (n) (lambda (lst) (if (= n 0) nil (if (null? lst) nil (cons (car lst) ((take (- n 1)) (cdr lst))))))))", interp);

    // drop: (drop n lst) - skip first n elements
    run("(define drop (lambda (n) (lambda (lst) (if (= n 0) lst (if (null? lst) nil ((drop (- n 1)) (cdr lst)))))))", interp);

    // zip: (zip a b) - zip two lists into list of pairs
    run("(define zip (lambda (a) (lambda (b) (if (or (null? a) (null? b)) nil (cons (cons (car a) (car b)) ((zip (cdr a)) (cdr b)))))))", interp);

    // range: (range n) - list from 0 to n-1
    run("(define range (lambda (n) (let ^rec ((helper (lambda (i) (if (= i n) nil (cons i (helper (+ i 1))))))) (helper 0))))", interp);

    // for-each: (for-each f lst) - apply f to each element for side effects, return nil
    run("(define for-each (lambda (f) (lambda (lst) (if (null? lst) nil (let ((_r (f (car lst)))) ((for-each f) (cdr lst)))))))", interp);

    // any?: (any? pred lst) - true if pred is truthy for any element
    run("(define any? (lambda (pred) (lambda (lst) (if (null? lst) nil (if (pred (car lst)) true ((any? pred) (cdr lst)))))))", interp);

    // every?: (every? pred lst) - true if pred is truthy for all elements
    run("(define every? (lambda (pred) (lambda (lst) (if (null? lst) true (if (pred (car lst)) ((every? pred) (cdr lst)) nil)))))", interp);

    // =========================================================================
    // Error Handling Convention (via algebraic effects)
    // =========================================================================

    // try: run a thunk, catch errors via 'raise' effect
    // Usage: ((try (lambda (xx) body)) (lambda (msg) handler))
    run("(define try (lambda (thunk) (lambda (handler) (handle (thunk nil) ((raise k msg) (handler msg))))))", interp);

    // assert!: check condition, raise if false
    run("(define assert! (lambda (condition) (lambda (msg) (if condition true (perform raise msg)))))", interp);

    // =========================================================================
    // Association List Helpers
    // =========================================================================

    // assoc: (assoc key alist) - find pair with matching key
    run("(define assoc (lambda (key) (lambda (alist) (if (null? alist) nil (if (= (car (car alist)) key) (car alist) ((assoc key) (cdr alist)))))))", interp);

    // assoc-ref: (assoc-ref key alist) - get value for key (cdr of pair)
    run("(define assoc-ref (lambda (key) (lambda (alist) (let ((pair ((assoc key) alist))) (if (null? pair) nil (cdr pair))))))", interp);
}

// =============================================================================
// SECTION 9: REPL
// =============================================================================

/**
 * Read a line of input from stdin.
 * Returns the number of characters read (not including null terminator).
 */
fn usz read_line(char[] buffer) {
    usz len = 0;
    usz max_len = buffer.len - 1;
    while (len < max_len) {
        if (try c = io::stdin().read_byte()) {
            if (c == '\n') {
                break;
            }
            buffer[len] = c;
            len++;
        } else {
            break;  // EOF or error
        }
    }
    buffer[len] = 0;  // Null terminate
    return len;
}

/**
 * Read-Eval-Print-Loop.
 */
// Count net parenthesis depth in a string, skipping chars inside "..." strings
// and ; line comments. Returns the net depth (opens - closes).
fn int count_paren_depth(char[] input) {
    int depth = 0;
    bool in_string = false;
    bool in_comment = false;
    bool escape_next = false;

    for (usz i = 0; i < input.len; i++) {
        char c = input[i];

        // Handle escape sequences inside strings
        if (in_string) {
            if (escape_next) {
                escape_next = false;
                continue;
            }
            if (c == '\\') {
                escape_next = true;
                continue;
            }
            if (c == '"') {
                in_string = false;
            }
            continue;
        }

        // Comments run to end of line
        if (in_comment) {
            if (c == '\n') {
                in_comment = false;
            }
            continue;
        }

        // Normal mode
        if (c == ';') {
            in_comment = true;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
        }
    }
    return depth;
}

fn void repl(Interp* interp) {
    io::printn("Pika Lisp REPL (type 'quit' or 'exit' to leave, Ctrl-D for EOF)");
    io::printn("---");

    // Buffer for accumulating multi-line input
    char[8192] buf;
    usz buf_len = 0;

    while (true) {
        // Determine prompt: primary or continuation
        char* prompt;
        if (buf_len == 0) {
            prompt = "pika> ";
        } else {
            prompt = "...   ";
        }

        // Use GNU readline for line editing and history support
        char* line = readline(prompt);

        // readline returns null on EOF (Ctrl-D)
        if (line == null) {
            if (buf_len > 0) {
                // Cancel incomplete expression on Ctrl-D
                io::printn("");
                buf_len = 0;
                continue;
            }
            io::printn("\nGoodbye!");
            break;
        }

        // Compute length of the line
        usz len = 0;
        while (line[len] != 0) {
            len++;
        }

        // If we're in continuation mode and get an empty line, cancel
        if (buf_len > 0 && len == 0) {
            io::printn("Input cancelled.");
            buf_len = 0;
            mem::free(line);
            continue;
        }

        // Skip empty lines on primary prompt (don't add to history)
        if (buf_len == 0 && len == 0) {
            mem::free(line);
            continue;
        }

        // Check for quit/exit on primary prompt only
        if (buf_len == 0) {
            if (len >= 4 && line[0] == 'q' && line[1] == 'u' &&
                line[2] == 'i' && line[3] == 't') {
                mem::free(line);
                io::printn("Goodbye!");
                break;
            }

            if (len >= 4 && line[0] == 'e' && line[1] == 'x' &&
                line[2] == 'i' && line[3] == 't') {
                mem::free(line);
                io::printn("Goodbye!");
                break;
            }
        }

        // Append line to buffer (with space separator if continuing)
        if (buf_len > 0) {
            // Add a space separator between lines
            if (buf_len < 8191) {
                buf[buf_len] = ' ';
                buf_len++;
            }
        }

        // Copy line into buffer
        usz copy_len = len;
        if (buf_len + copy_len > 8191) {
            copy_len = 8191 - buf_len;
        }
        for (usz i = 0; i < copy_len; i++) {
            buf[buf_len + i] = line[i];
        }
        buf_len += copy_len;

        // Free the readline-allocated string
        mem::free(line);

        // Check paren depth to see if expression is complete
        char[] accumulated = buf[:buf_len];
        int depth = count_paren_depth(accumulated);

        if (depth > 0) {
            // Unmatched open parens — prompt for more input
            continue;
        }

        // Expression looks complete (balanced or no parens)
        // Add the full accumulated expression to history
        buf[buf_len] = 0; // null-terminate for add_history
        add_history(&buf[0]);

        // Parse and evaluate in temp region
        char[] input = buf[:buf_len];

        // Push temp frame
        main::RegionHandle saved_frame = interp.current_frame;
        interp.current_frame = main::create_region(saved_frame);

        EvalResult r = run(input, interp);

        // Copy result to parent before releasing
        if (!r.error.has_error && r.value != null) {
            r.value = copy_to_parent(r.value, interp, saved_frame);
        }

        // Pop temp frame
        main::release_region(interp.current_frame);
        interp.current_frame = saved_frame;

        if (r.error.has_error) {
            // Print error with location if available
            usz msg_len = 0;
            while (msg_len < 256 && r.error.message[msg_len] != 0) {
                msg_len++;
            }

            if (r.error.line > 0) {
                io::printf("Error at line %d, column %d: ", (int)r.error.line, (int)r.error.column);
                for (usz i = 0; i < msg_len; i++) {
                    io::printf("%c", r.error.message[i]);
                }
                io::printn("");
            } else {
                io::print("Error: ");
                for (usz i = 0; i < msg_len; i++) {
                    io::printf("%c", r.error.message[i]);
                }
                io::printn("");
            }
        } else {
            // Print the result
            print_value(r.value, &interp.symbols);
            io::printn("");
        }

        // Reset buffer for next expression
        buf_len = 0;
    }
}

// =============================================================================
// SECTION 10: TESTS
// =============================================================================

// ---------------------------------------------------------------------------
// Unified Test Helpers — each runs BOTH interpreter AND JIT on the same expr
// ---------------------------------------------------------------------------

fn void setup(Interp* interp, char[] code) {
    run(code, interp);
}

fn void test_eq(Interp* interp, char[] name, char[] expr, long expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_int(ri.value) && ri.value.int_val == expected;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = f(interp);
            jit_ok = jv != null && jv.tag == INT && jv.int_val == expected;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_truthy(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && !is_nil(ri.value) && !(is_symbol(ri.value) && (uint)ri.value.sym_val == (uint)interp.sym_false);

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = f(interp);
            jit_ok = jv != null && !is_nil(jv) && !(is_symbol(jv) && (uint)jv.sym_val == (uint)interp.sym_false);
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_nil(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_nil(ri.value);

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = f(interp);
            jit_ok = jv != null && is_nil(jv);
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_error(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = ri.error.has_error;
    if (interp_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (expected error)", (ZString)name);
        (*fail)++;
    }
}

fn void test_str(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_string(ri.value);

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = f(interp);
            jit_ok = jv != null && jv.tag == STRING;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_tag(Interp* interp, char[] name, char[] expr, ValueTag expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && ri.value != null && ri.value.tag == expected;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = f(interp);
            jit_ok = jv != null && jv.tag == expected;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

fn void test_gt(Interp* interp, char[] name, char[] expr, long min, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    bool interp_ok = !ri.error.has_error && is_int(ri.value) && ri.value.int_val > min;

    Expr* ast = parse_for_jit(expr, interp);
    bool jit_ok = false;
    if (ast != null) {
        JitFn f = jit_compile(ast, interp);
        if (f != null) {
            Value* jv = f(interp);
            jit_ok = jv != null && jv.tag == INT && jv.int_val > min;
        }
    }

    if (interp_ok && jit_ok) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s (interp=%s, jit=%s)", (ZString)name, interp_ok ? "ok" : "FAIL", jit_ok ? "ok" : "FAIL");
        (*fail)++;
    }
}

// Interp-only: for tests that involve stateful side effects or features
// the JIT cannot handle (modules, error paths, println, etc.)
fn void test_eq_interp(Interp* interp, char[] name, char[] expr, long expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && is_int(ri.value) && ri.value.int_val == expected) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        if (!ri.error.has_error && is_int(ri.value)) { io::printfn("[FAIL] %s (got %d)", (ZString)name, ri.value.int_val); }
        else { io::printfn("[FAIL] %s", (ZString)name); }
        (*fail)++;
    }
}

fn void test_truthy_interp(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && !is_nil(ri.value) && !(is_symbol(ri.value) && (uint)ri.value.sym_val == (uint)interp.sym_false)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

fn void test_nil_interp(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && is_nil(ri.value)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

fn void test_str_interp(Interp* interp, char[] name, char[] expr, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && is_string(ri.value)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

fn void test_tag_interp(Interp* interp, char[] name, char[] expr, ValueTag expected, int* pass, int* fail) {
    EvalResult ri = run(expr, interp);
    if (!ri.error.has_error && ri.value != null && ri.value.tag == expected) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        io::printfn("[FAIL] %s", (ZString)name);
        (*fail)++;
    }
}

// JIT-only: for stateful tests that would double-mutate if run through both interp and JIT
fn void test_eq_jit(Interp* interp, char[] name, char[] source, long expected, int* pass, int* fail) {
    Expr* ast = parse_for_jit(source, interp);
    if (ast == null) { (*fail)++; io::printfn("[FAIL] %s (JIT parse error)", (ZString)name); return; }
    JitFn f = jit_compile(ast, interp);
    if (f == null) { (*fail)++; io::printfn("[FAIL] %s (JIT compile error)", (ZString)name); return; }
    Value* result = f(interp);
    if (result != null && result.tag == INT && result.int_val == expected) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        (*fail)++;
        if (result != null && result.tag == INT) { io::printfn("[FAIL] %s (JIT got %d)", (ZString)name, result.int_val); }
        else { io::printfn("[FAIL] %s", (ZString)name); }
    }
}

fn void test_nil_jit(Interp* interp, char[] name, char[] source, int* pass, int* fail) {
    Expr* ast = parse_for_jit(source, interp);
    if (ast == null) { (*fail)++; io::printfn("[FAIL] %s (JIT parse error)", (ZString)name); return; }
    JitFn f = jit_compile(ast, interp);
    if (f == null) { (*fail)++; io::printfn("[FAIL] %s (JIT compile error)", (ZString)name); return; }
    Value* result = f(interp);
    if (result == null || is_nil(result)) {
        io::printfn("[PASS] %s", (ZString)name);
        (*pass)++;
    } else {
        (*fail)++;
        io::printfn("[FAIL] %s (JIT expected nil)", (ZString)name);
    }
}

fn void run_lisp_tests() {
    io::printn("=== Unified Tests (Interpreter + JIT) ===");

    Interp* interp = (Interp*)mem::malloc(Interp.sizeof);
    interp.init();
    register_primitives(interp);
    register_stdlib(interp);

    int pass = 0;
    int fail = 0;

    run_basic_tests(interp, &pass, &fail);
    run_memory_stress_tests(interp, &pass, &fail);
    run_list_closure_tests(interp, &pass, &fail);
    run_arithmetic_comparison_tests(interp, &pass, &fail);
    run_string_type_tests(interp, &pass, &fail);
    run_advanced_tests(interp, &pass, &fail);

    io::printfn("\n=== Unified Tests: %d passed, %d failed ===", pass, fail);
    assert(fail == 0, "tests failed");

    run_compiler_tests(interp);
}

fn void run_basic_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Basic Tests ---");

    // Literals & bindings
    test_eq(interp, "integer literal 42", "42", 42, pass, fail);
    setup(interp, "(define x 10)");
    test_eq(interp, "variable binding x => 10", "x", 10, pass, fail);
    test_eq(interp, "lambda identity", "((lambda (x) x) 5)", 5, pass, fail);
    test_eq(interp, "let binding", "(let ((y 7)) y)", 7, pass, fail);
    test_eq(interp, "if true", "(if true 1 2)", 1, pass, fail);

    // Primitives
    setup(interp, "(define add +)");
    test_tag(interp, "quote returns list", "(quote (a b c))", CONS, pass, fail);

    // Closure
    setup(interp, "(define make-adder (lambda (n) (lambda (x) n)))");
    test_tag(interp, "nested lambda", "(make-adder 5)", CLOSURE, pass, fail);

    // Cons/car/cdr
    test_eq(interp, "car (cons 1 2) => 1", "(car (cons 1 2))", 1, pass, fail);
    test_eq(interp, "cdr (cons 1 2) => 2", "(cdr (cons 1 2))", 2, pass, fail);

    // Print (interp only, side effect)
    { io::print("[PASS] Print: "); run("(println (quote hello))", interp); (*pass)++; }

    // Source location tracking
    {
        EvalResult r = run("undefined_var", interp);
        if (r.error.has_error && r.error.line == 1 && r.error.column == 1) {
            io::printn("[PASS] source location tracking");
            (*pass)++;
        } else {
            io::printn("[FAIL] source location tracking");
            (*fail)++;
        }
    }

    // Dot-bracket indexing
    setup(interp, "(define mylist (quote (10 20 30 40 50)))");
    test_eq(interp, "mylist.[0] => 10", "mylist.[0]", 10, pass, fail);
    test_eq(interp, "mylist.[2] => 30", "mylist.[2]", 30, pass, fail);
    setup(interp, "(define idx 3)");
    test_eq(interp, "mylist.[idx] => 40", "mylist.[idx]", 40, pass, fail);

    // Pattern matching basics
    test_eq(interp, "match literal", "(match 42 (42 1) (x 0))", 1, pass, fail);
    test_eq(interp, "match variable", "(match 5 (x x))", 5, pass, fail);
    test_eq(interp, "match wildcard", "(match 99 (_ 1))", 1, pass, fail);
    test_eq(interp, "match [a b c]", "(match (quote (1 2 3)) ([a b c] (+ a (+ b c))))", 6, pass, fail);
    test_eq(interp, "match [head .. tail]", "(match (quote (10 20 30)) ([head .. tail] head))", 10, pass, fail);
    test_eq(interp, "match [a b ..]", "(match (quote (1 2 3 4 5)) ([a b ..] (+ a b)))", 3, pass, fail);
    test_eq(interp, "match [.. last]", "(match (quote (1 2 3 4 5)) ([.. last] last))", 5, pass, fail);
    test_eq(interp, "match fallthrough", "(match 42 (0 100) (1 200) (x x))", 42, pass, fail);
    test_eq(interp, "match [_ second _]", "(match (quote (1 2 3)) ([_ second _] second))", 2, pass, fail);

    // Length
    test_eq(interp, "length '() => 0", "(length (quote ()))", 0, pass, fail);
    test_eq(interp, "length '(1 2 3 4 5) => 5", "(length (quote (1 2 3 4 5)))", 5, pass, fail);

    // Path access
    setup(interp, "(define namepair (cons (quote name) \"Alice\"))");
    setup(interp, "(define agepair (cons (quote age) 30))");
    setup(interp, "(define testperson (cons namepair (cons agepair (quote ()))))");
    test_str(interp, "testperson.name => string", "testperson.name", pass, fail);
    test_eq(interp, "testperson.age => 30", "testperson.age", 30, pass, fail);

    // Nested path
    setup(interp, "(define citypair (cons (quote city) \"NYC\"))");
    setup(interp, "(define zippair (cons (quote zip) 10001))");
    setup(interp, "(define testaddress (cons citypair (cons zippair (quote ()))))");
    setup(interp, "(define namepair2 (cons (quote name) \"Bob\"))");
    setup(interp, "(define addrpair (cons (quote address) testaddress))");
    setup(interp, "(define testperson2 (cons namepair2 (cons addrpair (quote ()))))");
    test_str(interp, "testperson2.address.city => string", "testperson2.address.city", pass, fail);

    // Recursive let
    test_eq(interp, "let ^rec factorial 5 => 120", "(let ^rec ((fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))) (fact 5))", 120, pass, fail);
    test_eq(interp, "non-recursive let", "(let ((x 10)) (+ x 5))", 15, pass, fail);

    // And/or
    test_truthy(interp, "and true true", "(and true true)", pass, fail);
    test_nil(interp, "and false true => nil", "(and false true)", pass, fail);
    test_nil(interp, "and true false => nil", "(and true false)", pass, fail);
    test_eq(interp, "and 1 42 => 42", "(and 1 42)", 42, pass, fail);
    test_truthy(interp, "or true false", "(or true false)", pass, fail);
    test_nil(interp, "or false nil => nil", "(or false nil)", pass, fail);
    test_eq(interp, "or 42 99 => 42", "(or 42 99)", 42, pass, fail);
    test_eq(interp, "or nil 99 => 99", "(or nil 99)", 99, pass, fail);
    test_nil(interp, "and short-circuit", "(and nil undefined_var)", pass, fail);
    test_eq(interp, "or short-circuit", "(or 1 undefined_var)", 1, pass, fail);

    // Truthiness
    test_eq(interp, "0 is truthy", "(if 0 42 99)", 42, pass, fail);

    // Reset/shift
    test_eq(interp, "shift aborts", "(reset (+ 1 (shift k 42)))", 42, pass, fail);
    test_eq(interp, "shift k resumes", "(reset (+ 1 (shift k (k 10))))", 11, pass, fail);
    test_eq(interp, "reset passthrough", "(reset (+ 10 20))", 30, pass, fail);

    // Effect handlers
    test_eq(interp, "handle without effect", "(handle 42 ((ask k x) (k 0)))", 42, pass, fail);
    test_eq(interp, "handle + perform + resume", "(handle (+ 1 (perform ask 0)) ((ask k x) (k 10)))", 11, pass, fail);
    test_eq(interp, "handle abort", "(handle (+ 1 (perform ask 5)) ((ask k x) x))", 5, pass, fail);

    // Empty string truthy
    test_eq(interp, "empty string is truthy", "(if \"\" 1 0)", 1, pass, fail);
}

fn void run_memory_stress_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Memory Stress Tests ---");

    // Deep recursion
    setup(interp, "(define sum-to (lambda (n) (if (= n 0) 0 (+ n (sum-to (- n 1))))))");
    test_eq(interp, "deep recursion sum 1..50 => 1275", "(sum-to 50)", 1275, pass, fail);

    // Closure factory
    setup(interp, "(define make-counter (lambda (start) (lambda (x) (+ start x))))");
    setup(interp, "(define c1 (make-counter 10))");
    setup(interp, "(define c2 (make-counter 20))");
    setup(interp, "(define c3 (make-counter 30))");
    test_eq(interp, "closure c1 5 => 15", "(c1 5)", 15, pass, fail);
    test_eq(interp, "closure c2 5 => 25", "(c2 5)", 25, pass, fail);
    test_eq(interp, "closure c3 5 => 35", "(c3 5)", 35, pass, fail);

    // String operations
    test_eq(interp, "string-length hello world => 11", "(string-length \"hello world\")", 11, pass, fail);
    test_str(interp, "string-upcase hello => string", "(string-upcase \"hello\")", pass, fail);

    // Pattern matching stress
    setup(interp, "(define data (quote (1 2 3 4 5 6 7 8 9 10)))");
    test_eq(interp, "match [a b c .. rest]", "(match data ([a b c .. rest] (+ a (+ b c))))", 6, pass, fail);
    test_eq(interp, "match [.. last] => 10", "(match data ([.. last] last))", 10, pass, fail);

    // Fibonacci
    setup(interp, "(define fib (lambda (n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))))");
    test_eq(interp, "fib 15 => 610", "(fib 15)", 610, pass, fail);

    // Map over list
    setup(interp, "(define mymap (lambda (f) (lambda (lst) (if (null? lst) (quote ()) (cons (f (car lst)) ((mymap f) (cdr lst)))))))");
    setup(interp, "(define double (lambda (x) (* x 2)))");
    test_eq(interp, "map double car => 2", "(car ((mymap double) (quote (1 2 3 4 5))))", 2, pass, fail);
    setup(interp, "(define mapped ((mymap double) (quote (1 2 3 4 5))))");
    test_eq(interp, "map double last => 10", "(match mapped ([.. last] last))", 10, pass, fail);

    // Nested let 10 levels
    test_eq(interp, "nested let 10 levels => 55", "(let ((a 1)) (let ((b 2)) (let ((c 3)) (let ((d 4)) (let ((e 5)) (let ((f 6)) (let ((g 7)) (let ((h 8)) (let ((i 9)) (let ((j 10)) (+ a (+ b (+ c (+ d (+ e (+ f (+ g (+ h (+ i j)))))))))))))))))))))", 55, pass, fail);

    // Repeated evaluation
    setup(interp, "(define mem-counter 0)");
    for (int i = 0; i < 100; i++) { run("(define mem-counter (+ mem-counter 1))", interp); }
    test_eq(interp, "repeated eval 100x", "mem-counter", 100, pass, fail);

    // Large list
    setup(interp, "(define build-list-helper (lambda (n) (lambda (acc) (if (= n 0) acc ((build-list-helper (- n 1)) (cons n acc))))))");
    setup(interp, "(define biglist ((build-list-helper 50) (quote ())))");
    test_eq(interp, "build-list 50 elements", "(length biglist)", 50, pass, fail);

    // Fold
    setup(interp, "(define fold-helper (lambda (f) (lambda (init) (lambda (lst) (if (null? lst) init (((fold-helper f) (f init (car lst))) (cdr lst)))))))");
    test_eq(interp, "fold sum 1..10 => 55", "(((fold-helper +) 0) (quote (1 2 3 4 5 6 7 8 9 10)))", 55, pass, fail);
}

fn void run_arithmetic_comparison_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Arithmetic & Comparison Tests ---");

    // Addition
    test_eq(interp, "(+ 1 2) => 3", "(+ 1 2)", 3, pass, fail);
    test_eq(interp, "(+ 0 0) => 0", "(+ 0 0)", 0, pass, fail);
    test_eq(interp, "(+ -5 3) => -2", "(+ -5 3)", -2, pass, fail);
    test_eq(interp, "(+ 100 -100) => 0", "(+ 100 -100)", 0, pass, fail);
    test_eq(interp, "(+ 1000000 2000000) => 3000000", "(+ 1000000 2000000)", 3000000, pass, fail);

    // Subtraction
    test_eq(interp, "(- 10 3) => 7", "(- 10 3)", 7, pass, fail);
    test_eq(interp, "(- 3 10) => -7", "(- 3 10)", -7, pass, fail);
    test_eq(interp, "(- 0 0) => 0", "(- 0 0)", 0, pass, fail);
    test_eq(interp, "(- -5 -3) => -2", "(- -5 -3)", -2, pass, fail);

    // Multiplication
    test_eq(interp, "(* 3 4) => 12", "(* 3 4)", 12, pass, fail);
    test_eq(interp, "(* 0 999) => 0", "(* 0 999)", 0, pass, fail);
    test_eq(interp, "(* 1 42) => 42", "(* 1 42)", 42, pass, fail);
    test_eq(interp, "(* -3 4) => -12", "(* -3 4)", -12, pass, fail);
    test_eq(interp, "(* -3 -4) => 12", "(* -3 -4)", 12, pass, fail);

    // Division
    test_eq(interp, "(/ 10 2) => 5", "(/ 10 2)", 5, pass, fail);
    test_eq(interp, "(/ 7 2) => 3", "(/ 7 2)", 3, pass, fail);
    test_eq(interp, "(/ 0 5) => 0", "(/ 0 5)", 0, pass, fail);
    test_eq(interp, "(/ -10 3) => -3", "(/ -10 3)", -3, pass, fail);

    // Modulo
    test_eq(interp, "(% 10 3) => 1", "(% 10 3)", 1, pass, fail);
    test_eq(interp, "(% 9 3) => 0", "(% 9 3)", 0, pass, fail);
    test_eq(interp, "(% 7 10) => 7", "(% 7 10)", 7, pass, fail);

    // Nested arithmetic
    test_eq(interp, "(+ 1 (+ 2 3)) => 6", "(+ 1 (+ 2 3))", 6, pass, fail);
    test_eq(interp, "(* 2 (+ 3 4)) => 14", "(* 2 (+ 3 4))", 14, pass, fail);
    test_eq(interp, "(- (* 5 5) (+ 10 5)) => 10", "(- (* 5 5) (+ 10 5))", 10, pass, fail);
    test_eq(interp, "(/ (+ 10 20) (- 10 4)) => 5", "(/ (+ 10 20) (- 10 4))", 5, pass, fail);

    // Equality
    test_truthy(interp, "(= 5 5) => true", "(= 5 5)", pass, fail);
    test_nil(interp, "(= 5 6) => nil", "(= 5 6)", pass, fail);
    test_truthy(interp, "(= 0 0) => true", "(= 0 0)", pass, fail);
    test_truthy(interp, "(= -1 -1) => true", "(= -1 -1)", pass, fail);

    // Less than
    test_truthy(interp, "(< 1 2) => true", "(< 1 2)", pass, fail);
    test_nil(interp, "(< 2 1) => nil", "(< 2 1)", pass, fail);
    test_nil(interp, "(< 1 1) => nil", "(< 1 1)", pass, fail);
    test_truthy(interp, "(< -5 0) => true", "(< -5 0)", pass, fail);

    // Greater than
    test_truthy(interp, "(> 2 1) => true", "(> 2 1)", pass, fail);
    test_nil(interp, "(> 1 2) => nil", "(> 1 2)", pass, fail);
    test_nil(interp, "(> 1 1) => nil", "(> 1 1)", pass, fail);

    // Less or equal
    test_truthy(interp, "(<= 1 2) => true", "(<= 1 2)", pass, fail);
    test_truthy(interp, "(<= 2 2) => true", "(<= 2 2)", pass, fail);
    test_nil(interp, "(<= 3 2) => nil", "(<= 3 2)", pass, fail);

    // Greater or equal
    test_truthy(interp, "(>= 2 1) => true", "(>= 2 1)", pass, fail);
    test_truthy(interp, "(>= 2 2) => true", "(>= 2 2)", pass, fail);
    test_nil(interp, "(>= 1 2) => nil", "(>= 1 2)", pass, fail);

    // Comparison in conditionals
    test_eq(interp, "(if (< 1 2) 10 20) => 10", "(if (< 1 2) 10 20)", 10, pass, fail);
    test_eq(interp, "(if (> 1 2) 10 20) => 20", "(if (> 1 2) 10 20)", 20, pass, fail);
    test_eq(interp, "(if (= (+ 2 3) 5) 1 0) => 1", "(if (= (+ 2 3) 5) 1 0)", 1, pass, fail);
}

fn void run_string_type_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- String & Type Predicate Tests ---");

    // String length
    test_eq(interp, "string-length hello => 5", "(string-length \"hello\")", 5, pass, fail);
    test_eq(interp, "string-length empty => 0", "(string-length \"\")", 0, pass, fail);
    test_eq(interp, "string-length a => 1", "(string-length \"a\")", 1, pass, fail);

    // String case/trim/append/substring
    test_str(interp, "string-upcase hello", "(string-upcase \"hello\")", pass, fail);
    test_str(interp, "string-downcase HELLO", "(string-downcase \"HELLO\")", pass, fail);
    test_str(interp, "string-upcase empty", "(string-upcase \"\")", pass, fail);
    test_str(interp, "string-trim", "(string-trim \"  hello  \")", pass, fail);
    test_str(interp, "string-trim noop", "(string-trim \"hello\")", pass, fail);
    test_str(interp, "string-append", "(string-append \"hello\" \" world\")", pass, fail);
    test_str(interp, "string-append empty", "(string-append \"\" \"abc\")", pass, fail);
    test_str(interp, "substring 0 5", "(substring \"hello world\" 0 5)", pass, fail);
    test_str(interp, "substring 6 11", "(substring \"hello world\" 6 11)", pass, fail);

    // String predicates
    test_truthy(interp, "string? hello => true", "(string? \"hello\")", pass, fail);
    test_nil(interp, "string? 42 => nil", "(string? 42)", pass, fail);
    test_nil(interp, "string? nil => nil", "(string? nil)", pass, fail);

    // int?
    test_truthy(interp, "int? 42 => true", "(int? 42)", pass, fail);
    test_truthy(interp, "int? 0 => true", "(int? 0)", pass, fail);
    test_truthy(interp, "int? -5 => true", "(int? -5)", pass, fail);
    test_nil(interp, "int? hello => nil", "(int? \"hello\")", pass, fail);
    test_nil(interp, "int? nil => nil", "(int? nil)", pass, fail);

    // symbol?
    test_truthy(interp, "symbol? foo => true", "(symbol? (quote foo))", pass, fail);
    test_nil(interp, "symbol? 42 => nil", "(symbol? 42)", pass, fail);
    test_nil(interp, "symbol? hello => nil", "(symbol? \"hello\")", pass, fail);

    // closure?
    test_truthy(interp, "closure? lambda => true", "(closure? (lambda (x) x))", pass, fail);
    test_nil(interp, "closure? 42 => nil", "(closure? 42)", pass, fail);
    test_nil(interp, "closure? hello => nil", "(closure? \"hello\")", pass, fail);

    // null?
    test_truthy(interp, "null? nil => true", "(null? nil)", pass, fail);
    test_truthy(interp, "null? '() => true", "(null? (quote ()))", pass, fail);
    test_nil(interp, "null? 42 => nil", "(null? 42)", pass, fail);
    test_nil(interp, "null? '(1 2) => nil", "(null? (quote (1 2)))", pass, fail);

    // pair?
    test_truthy(interp, "pair? cons => true", "(pair? (cons 1 2))", pass, fail);
    test_truthy(interp, "pair? list => true", "(pair? (quote (1 2 3)))", pass, fail);
    test_nil(interp, "pair? nil => nil", "(pair? nil)", pass, fail);
    test_nil(interp, "pair? 42 => nil", "(pair? 42)", pass, fail);

    // not
    test_truthy(interp, "not nil => true", "(not nil)", pass, fail);
    test_truthy(interp, "not false => true", "(not false)", pass, fail);
    test_nil(interp, "not true => nil", "(not true)", pass, fail);
    test_nil(interp, "not 42 => nil", "(not 42)", pass, fail);
    test_nil(interp, "not 0 => nil (truthy)", "(not 0)", pass, fail);
    test_nil(interp, "not empty-str => nil (truthy)", "(not \"\")", pass, fail);
}

fn void run_list_closure_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- List & Closure Tests ---");

    // cons/car/cdr
    test_eq(interp, "car (cons 1 2) => 1", "(car (cons 1 2))", 1, pass, fail);
    test_eq(interp, "cdr (cons 1 2) => 2", "(cdr (cons 1 2))", 2, pass, fail);
    test_eq(interp, "car (cons (+ 1 2) 4) => 3", "(car (cons (+ 1 2) 4))", 3, pass, fail);
    test_eq(interp, "cadr => 2", "(car (cdr (cons 1 (cons 2 (cons 3 nil)))))", 2, pass, fail);
    test_eq(interp, "caddr => 3", "(car (cdr (cdr (cons 1 (cons 2 (cons 3 nil))))))", 3, pass, fail);

    // Length
    test_eq(interp, "length '() => 0", "(length (quote ()))", 0, pass, fail);
    test_eq(interp, "length '(1) => 1", "(length (quote (1)))", 1, pass, fail);
    test_eq(interp, "length '(1..10) => 10", "(length (quote (1 2 3 4 5 6 7 8 9 10)))", 10, pass, fail);
    test_eq(interp, "length cons => 2", "(length (cons 1 (cons 2 nil)))", 2, pass, fail);

    // Quote
    test_eq(interp, "car '(10 20 30) => 10", "(car (quote (10 20 30)))", 10, pass, fail);
    test_eq(interp, "length '(a b c d e) => 5", "(length (quote (a b c d e)))", 5, pass, fail);
    test_truthy(interp, "null? '() => true", "(null? (quote ()))", pass, fail);

    // List indexing
    setup(interp, "(define idx-list (quote (10 20 30)))");
    test_eq(interp, "idx-list.[0] => 10", "idx-list.[0]", 10, pass, fail);
    test_eq(interp, "idx-list.[1] => 20", "idx-list.[1]", 20, pass, fail);
    test_eq(interp, "idx-list.[2] => 30", "idx-list.[2]", 30, pass, fail);

    // Closures
    setup(interp, "(define make-adder2 (lambda (n) (lambda (x) (+ n x))))");
    setup(interp, "(define add10 (make-adder2 10))");
    setup(interp, "(define add20 (make-adder2 20))");
    test_eq(interp, "add10 5 => 15", "(add10 5)", 15, pass, fail);
    test_eq(interp, "add20 5 => 25", "(add20 5)", 25, pass, fail);

    // Composition
    setup(interp, "(define compose (lambda (f) (lambda (g) (lambda (x) (f (g x))))))");
    setup(interp, "(define double2 (lambda (x) (* x 2)))");
    setup(interp, "(define inc (lambda (x) (+ x 1)))");
    setup(interp, "(define double-then-inc ((compose inc) double2))");
    test_eq(interp, "compose inc double 5 => 11", "(double-then-inc 5)", 11, pass, fail);

    // Multi-capture
    setup(interp, "(define make-linear (lambda (a) (lambda (b) (lambda (x) (+ (* a x) b)))))");
    setup(interp, "(define f2x3 ((make-linear 2) 3))");
    test_eq(interp, "f(x)=2x+3, f(10)=23", "(f2x3 10)", 23, pass, fail);

    // Scoping
    test_eq(interp, "let shadowing", "(let ((x 10)) (let ((x 20)) x))", 20, pass, fail);
    setup(interp, "(define outer-x 100)");
    setup(interp, "(let ((outer-x 999)) outer-x)");
    test_eq(interp, "let no leak", "outer-x", 100, pass, fail);

    setup(interp, "(define scope-test 42)");
    setup(interp, "(define scope-fn (lambda (scope-test) scope-test))");
    test_eq(interp, "lambda scope param", "(scope-fn 99)", 99, pass, fail);
    test_eq(interp, "global preserved", "scope-test", 42, pass, fail);

    // Recursive let
    test_eq(interp, "let ^rec fact 6 => 720", "(let ^rec ((f (lambda (n) (if (= n 0) 1 (* n (f (- n 1))))))) (f 6))", 720, pass, fail);

    // Mutual recursion
    setup(interp, "(define is-even (lambda (n) (if (= n 0) true (is-odd (- n 1)))))");
    setup(interp, "(define is-odd (lambda (n) (if (= n 0) false (is-even (- n 1)))))");
    test_truthy(interp, "even 10 => true", "(is-even 10)", pass, fail);
    test_truthy(interp, "odd 7 => true", "(is-odd 7)", pass, fail);

    // Higher-order
    setup(interp, "(define apply-twice (lambda (f) (lambda (x) (f (f x)))))");
    setup(interp, "(define add3 (lambda (x) (+ x 3)))");
    test_eq(interp, "apply-twice add3 10 => 16", "((apply-twice add3) 10)", 16, pass, fail);

    // Identity
    setup(interp, "(define id (lambda (x) x))");
    test_eq(interp, "id 42 => 42", "(id 42)", 42, pass, fail);
    test_str(interp, "id hello => string", "(id \"hello\")", pass, fail);
    test_nil(interp, "id nil => nil", "(id nil)", pass, fail);
}

fn void run_advanced_tests(Interp* interp, int* pass, int* fail) {
    io::printn("\n--- Advanced Feature Tests ---");

    // Match patterns
    test_eq(interp, "match 1 => 10", "(match 1 (1 10) (2 20) (3 30))", 10, pass, fail);
    test_eq(interp, "match 2 => 20", "(match 2 (1 10) (2 20) (3 30))", 20, pass, fail);
    test_eq(interp, "match 3 => 30", "(match 3 (1 10) (2 20) (3 30))", 30, pass, fail);
    test_eq(interp, "match default => 100", "(match 99 (1 10) (2 20) (x (+ x 1)))", 100, pass, fail);
    test_eq(interp, "match computed", "(match (+ 1 1) (1 10) (2 20) (3 30))", 20, pass, fail);
    test_eq(interp, "match [a b] => 30", "(match (quote (10 20)) ([a b] (+ a b)))", 30, pass, fail);
    test_eq(interp, "match [x] => 42", "(match (quote (42)) ([x] x))", 42, pass, fail);
    test_eq(interp, "match [a b c] product", "(match (quote (2 3 4)) ([a b c] (* a (* b c))))", 24, pass, fail);
    test_eq(interp, "match [h .. t] tail len", "(match (quote (1 2 3 4 5)) ([h .. t] (length t)))", 4, pass, fail);
    test_eq(interp, "match _ wildcard", "(match 42 (_ 0))", 0, pass, fail);
    test_eq(interp, "match string hello", "(match \"hello\" (\"hello\" 1) (\"world\" 2) (x 0))", 1, pass, fail);
    test_eq(interp, "match string world", "(match \"world\" (\"hello\" 1) (\"world\" 2) (x 0))", 2, pass, fail);
    test_eq(interp, "match string default", "(match \"other\" (\"hello\" 1) (\"world\" 2) (x 0))", 0, pass, fail);

    // Continuations
    test_eq(interp, "shift discard k", "(reset (+ 100 (shift k 42)))", 42, pass, fail);
    test_eq(interp, "shift k mul", "(reset (* 2 (shift k (k 5))))", 10, pass, fail);
    test_eq(interp, "shift k expr", "(reset (+ 1 (shift k (k (+ 2 3)))))", 6, pass, fail);
    test_eq(interp, "shift use k result", "(reset (+ 1 (shift k (+ (k 10) 1000))))", 1011, pass, fail);
    test_eq(interp, "reset value", "(reset 42)", 42, pass, fail);

    // Effect handlers
    test_eq(interp, "effect modify", "(handle (+ 1 (perform get 0)) ((get k x) (k 100)))", 101, pass, fail);
    test_eq(interp, "effect arg double", "(handle (perform double 7) ((double k x) (k (* x 2))))", 14, pass, fail);
    test_eq(interp, "effect bail", "(handle (+ 1 (+ 2 (+ 3 (perform bail 42)))) ((bail k x) x))", 42, pass, fail);
    test_eq(interp, "handle no effect", "(handle (+ 10 20) ((unused k x) 999))", 30, pass, fail);

    // If/cond edge cases
    test_eq(interp, "if nil => 2", "(if nil 1 2)", 2, pass, fail);
    test_eq(interp, "if false => 2", "(if false 1 2)", 2, pass, fail);
    test_eq(interp, "if 0 => 1 (truthy)", "(if 0 1 2)", 1, pass, fail);
    test_eq(interp, "if empty-str => 1 (truthy)", "(if \"\" 1 2)", 1, pass, fail);
    test_eq(interp, "if '() => 2 (nil)", "(if (quote ()) 1 2)", 2, pass, fail);
    test_eq(interp, "if '(1) => 1 (truthy)", "(if (quote (1)) 1 2)", 1, pass, fail);
    test_eq(interp, "nested if", "(if true (if false 1 2) 3)", 2, pass, fail);
    test_eq(interp, "if with branches", "(if (= 1 1) (+ 10 20) (+ 30 40))", 30, pass, fail);

    // Error handling
    test_error(interp, "unbound variable", "nonexistent_var_xyz", pass, fail);
    test_error(interp, "call non-function", "(42 1)", pass, fail);
    test_error(interp, "shift outside reset", "(shift k 42)", pass, fail);

    // And/or advanced
    test_eq(interp, "nested and/or", "(and (or nil 5) (or false 10))", 10, pass, fail);
    test_eq(interp, "nested or/and", "(or (and nil 5) (and 1 10))", 10, pass, fail);
    test_eq(interp, "and closure truthy", "(and (lambda (x) x) 42)", 42, pass, fail);

    // Define/redefinition
    setup(interp, "(define redef-test 10)");
    test_eq(interp, "redef before", "redef-test", 10, pass, fail);
    setup(interp, "(define redef-test 20)");
    test_eq(interp, "redef after", "redef-test", 20, pass, fail);
    setup(interp, "(define computed-val (+ (* 3 4) (- 10 5)))");
    test_eq(interp, "define complex expr", "computed-val", 17, pass, fail);

    // Path access
    setup(interp, "(define ptest (cons (cons (quote x) 10) (cons (cons (quote y) 20) (quote ()))))");
    test_eq(interp, "ptest.x => 10", "ptest.x", 10, pass, fail);
    test_eq(interp, "ptest.y => 20", "ptest.y", 20, pass, fail);

    // TCO (JIT delegates closure calls to eval() which has full TCO)
    setup(interp, "(define tco-loop (lambda (n) (if (= n 0) (quote done) (tco-loop (- n 1)))))");
    test_truthy(interp, "TCO loop 10000", "(tco-loop 10000)", pass, fail);
    setup(interp, "(define sum-acc (lambda (n) (lambda (acc) (if (= n 0) acc ((sum-acc (- n 1)) (+ acc n))))))");
    test_eq(interp, "TCO sum-acc 1000 => 500500", "((sum-acc 1000) 0)", 500500, pass, fail);
    setup(interp, "(define tco-even (lambda (n) (if (= n 0) true (tco-odd (- n 1)))))");
    setup(interp, "(define tco-odd (lambda (n) (if (= n 0) nil (tco-even (- n 1)))))");
    test_truthy(interp, "TCO mutual recursion 1000", "(tco-even 1000)", pass, fail);
    setup(interp, "(define count-down (lambda (n) (match n (0 (quote done)) (n (count-down (- n 1))))))");
    test_truthy(interp, "TCO match tail call 5000", "(count-down 5000)", pass, fail);
    setup(interp, "(define let-loop (lambda (n) (let ((m (- n 1))) (if (= m 0) (quote done) (let-loop m)))))");
    test_truthy(interp, "TCO let body 5000", "(let-loop 5000)", pass, fail);

    // Multi-perform
    test_eq(interp, "multi-perform 2 sum", "(handle (+ (perform bounce 10) (perform bounce 20)) ((bounce k x) (k x)))", 30, pass, fail);
    test_eq(interp, "multi-perform 3 sum", "(handle (+ (perform bounce 1) (+ (perform bounce 2) (perform bounce 3))) ((bounce k x) (k x)))", 6, pass, fail);
    test_eq(interp, "multi-perform doubles", "(handle (+ (perform bounce 10) (perform bounce 20)) ((bounce k x) (k (* x 2))))", 60, pass, fail);
    test_eq(interp, "multi-perform abort", "(handle (+ (perform bounce 10) (perform bounce 20)) ((bounce k x) x))", 10, pass, fail);
    test_eq(interp, "multi-shift sum", "(reset (+ (shift k1 (k1 10)) (shift k2 (k2 20))))", 30, pass, fail);

    // Multi-shot continuations
    test_eq(interp, "multi-shot k twice", "(reset (+ 1 (shift k (+ (k 10) (k 20)))))", 32, pass, fail);
    test_eq(interp, "multi-shot k thrice", "(reset (* 2 (shift k (+ (k 3) (+ (k 5) (k 7))))))", 30, pass, fail);
    test_eq(interp, "multi-shot let bindings", "(reset (+ 10 (shift k (let ((a (k 1)) (b (k 2))) (+ a b)))))", 23, pass, fail);
    test_eq(interp, "multi-shot conditional", "(reset (+ 5 (shift k (if true (k 10) (k 20)))))", 15, pass, fail);
    test_eq(interp, "multi-shot effect", "(handle (+ 1 (perform dup 0)) ((dup k x) (+ (k 10) (k 20))))", 32, pass, fail);

    // TCO inside reset/shift
    setup(interp, "(define reset-loop (lambda (n acc) (if (= n 0) acc (reset-loop (- n 1) (+ acc 1)))))");
    test_eq(interp, "TCO in reset body", "(reset (reset-loop 5000 0))", 5000, pass, fail);
    setup(interp, "(define shift-loop (lambda (n acc) (if (= n 0) acc (shift-loop (- n 1) (+ acc 1)))))");
    test_eq(interp, "TCO in shift body", "(reset (+ 1 (shift k (shift-loop 5000 0))))", 5000, pass, fail);
    test_eq(interp, "TCO begin+if in reset", "(reset (begin (+ 1 2) (if true (+ 10 20) 99)))", 30, pass, fail);
    test_eq(interp, "TCO let chains in shift", "(reset (+ 1 (shift k (let ((x 10)) (let ((y 20)) (+ x y))))))", 30, pass, fail);
    setup(interp, "(define cont-use-loop (lambda (k n) (if (= n 0) (k 42) (cont-use-loop k (- n 1)))))");
    test_eq(interp, "TCO tail-recursive k use", "(reset (+ 1 (shift k (cont-use-loop k 5000))))", 43, pass, fail);
    test_eq(interp, "TCO nested reset", "(reset (+ (reset (if true 10 20)) (shift k (k 5))))", 15, pass, fail);
    setup(interp, "(define handle-loop (lambda (n) (if (= n 0) 0 (+ 1 (handle-loop (- n 1))))))");
    test_eq(interp, "TCO handler body", "(handle (perform count 0) ((count k x) (handle-loop 100)))", 100, pass, fail);

    // Trampoline (JIT delegates effects to eval which handles them)
    test_eq(interp, "trampoline passthrough", "(with-trampoline (lambda (xx) 42))", 42, pass, fail);
    test_eq(interp, "trampoline single bounce", "(with-trampoline (lambda (xx) (+ 1 (perform bounce (lambda (xx) 41)))))", 42, pass, fail);
    setup(interp, "(define fib-t (lambda (n) (if (< n 2) n (+ (perform bounce (lambda (xx) (fib-t (- n 1)))) (perform bounce (lambda (xx) (fib-t (- n 2))))))))");
    test_eq(interp, "trampoline fib 7", "(with-trampoline (lambda (xx) (fib-t 7)))", 13, pass, fail);

    // Variadic lambdas (JIT detects variadic closures and routes to fallback)
    setup(interp, "(define collect (lambda (.. args) args))");
    test_eq(interp, "variadic collect length", "(length (collect 1 2 3))", 3, pass, fail);
    test_nil(interp, "variadic collect empty", "(collect)", pass, fail);
    setup(interp, "(define head-rest (lambda (h .. t) t))");
    test_eq(interp, "variadic rest length", "(length (head-rest 1 2 3))", 2, pass, fail);
    test_nil(interp, "variadic empty rest", "(head-rest 1)", pass, fail);
    setup(interp, "(define multi-rest (lambda (a b .. rest) rest))");
    test_eq(interp, "variadic multi rest len", "(length (multi-rest 1 2 3 4 5))", 3, pass, fail);
    setup(interp, "(define head-only (lambda (h .. t) h))");
    test_eq(interp, "variadic head-only", "(head-only 42 99 100)", 42, pass, fail);
    setup(interp, "(define count-rest (lambda (h .. t) (length t)))");
    test_eq(interp, "variadic count-rest", "(count-rest 1 2 3 4 5)", 4, pass, fail);

    // Begin
    test_eq(interp, "begin returns last", "(begin 1 2 3)", 3, pass, fail);
    test_eq(interp, "begin single", "(begin 42)", 42, pass, fail);
    test_eq(interp, "begin side effects", "(begin (define begin-x 10) (define begin-y 20) (+ begin-x begin-y))", 30, pass, fail);
    test_eq(interp, "begin nested", "(begin (begin 1 2) (begin 3 4))", 4, pass, fail);
    test_eq(interp, "begin with if", "(begin (if true 10 20) (if false 30 40))", 40, pass, fail);

    // Zero-arg lambdas
    test_eq(interp, "zero-arg thunk", "((lambda () 42))", 42, pass, fail);
    test_eq(interp, "zero-arg closure", "(let ((a 10)) ((lambda () a)))", 10, pass, fail);
    test_eq(interp, "zero-arg stored thunk", "(let ((thunk (lambda () 99))) (thunk))", 99, pass, fail);
    test_eq(interp, "zero-arg begin body", "((lambda () (begin 1 2 3)))", 3, pass, fail);
    test_eq(interp, "zero-arg computation", "((lambda () (+ 10 20)))", 30, pass, fail);

    // Multi-param lambda (auto-curry)
    test_eq(interp, "multi-param two", "((lambda (x y) (+ x y)) 3 4)", 7, pass, fail);
    test_eq(interp, "multi-param three", "((lambda (x y z) (+ x (+ y z))) 1 2 3)", 6, pass, fail);
    test_eq(interp, "multi-param curry", "(let ((add (lambda (x y) (+ x y)))) ((add 10) 20))", 30, pass, fail);
    setup(interp, "(define mp-add3 (lambda (a b c) (+ a (+ b c))))");
    test_eq(interp, "multi-param defined fn", "(mp-add3 10 20 30)", 60, pass, fail);
    test_eq(interp, "multi-param full curry", "(let ((f (lambda (x y z) (+ x (+ y z))))) (((f 1) 2) 3))", 6, pass, fail);

    // Multi-binding let
    test_eq(interp, "multi-let two", "(let ((ml-x 10) (ml-y 20)) (+ ml-x ml-y))", 30, pass, fail);
    test_eq(interp, "multi-let three", "(let ((ml-a 1) (ml-b 2) (ml-c 3)) (+ ml-a (+ ml-b ml-c)))", 6, pass, fail);
    test_eq(interp, "multi-let sequential", "(let ((ml-p 10) (ml-q (+ ml-p 5))) ml-q)", 15, pass, fail);
    test_eq(interp, "multi-let with lambda", "(let ((ml-f (lambda (xx) (+ xx 1))) (ml-v (ml-f 10))) ml-v)", 11, pass, fail);
    test_eq(interp, "multi-let single", "(let ((ml-z 42)) ml-z)", 42, pass, fail);

    // Named let
    test_eq(interp, "named-let sum 1..5", "(let loop ((n 5) (acc 0)) (if (= n 0) acc (loop (- n 1) (+ acc n))))", 15, pass, fail);
    test_eq(interp, "named-let fact 10", "(let fact ((n 10) (acc 1)) (if (= n 0) acc (fact (- n 1) (* acc n))))", 3628800, pass, fail);
    test_eq(interp, "named-let fib 10", "(let fib ((n 10) (a 0) (b 1)) (if (= n 0) a (fib (- n 1) b (+ a b))))", 55, pass, fail);
    test_eq(interp, "named-let fib 20", "(let fib ((n 20) (a 0) (b 1)) (if (= n 0) a (fib (- n 1) b (+ a b))))", 6765, pass, fail);
    test_eq(interp, "named-let countdown", "(let count ((n 100)) (if (= n 0) 0 (count (- n 1))))", 0, pass, fail);
    test_eq(interp, "named-let build list car", "(car (let build ((n 3) (acc (quote ()))) (if (= n 0) acc (build (- n 1) (cons n acc)))))", 1, pass, fail);
    test_eq(interp, "named-let 10000 iters", "(let loop ((n 10000) (acc 0)) (if (= n 0) acc (loop (- n 1) (+ acc 1))))", 10000, pass, fail);

    // set!
    setup(interp, "(define set-test 10)");
    setup(interp, "(set! set-test 20)");
    test_eq(interp, "set! basic", "set-test", 20, pass, fail);

    // Counter tests are stateful (calling counter1 mutates), use JIT-only to avoid double mutation
    setup(interp, "(define make-counter (lambda () (let ((count 0)) (lambda () (begin (set! count (+ count 1)) count)))))");
    setup(interp, "(define counter1 (make-counter))");
    test_eq_jit(interp, "set! counter 1", "(counter1)", 1, pass, fail);
    test_eq_jit(interp, "set! counter 2", "(counter1)", 2, pass, fail);
    test_eq_jit(interp, "set! counter 3", "(counter1)", 3, pass, fail);

    // set! on let-local in closure (JIT detects and falls back to interpreter)
    test_eq(interp, "set! parent scope", "(let ((x 1)) (let ((f (lambda () (set! x 42)))) (begin (f) x)))", 42, pass, fail);
    test_error(interp, "set! unbound", "(set! undefined-var-xyz 1)", pass, fail);
    setup(interp, "(define sr-test 100)");
    setup(interp, "(set! sr-test (+ sr-test 1))");
    test_eq(interp, "set! read back", "sr-test", 101, pass, fail);

    // String<->number
    test_eq(interp, "string->number 42", "(string->number \"42\")", 42, pass, fail);
    test_eq(interp, "string->number -7", "(string->number \"-7\")", -7, pass, fail);
    test_nil(interp, "string->number abc => nil", "(string->number \"abc\")", pass, fail);
    test_nil(interp, "string->number empty => nil", "(string->number \"\")", pass, fail);
    test_eq(interp, "number->string round-trip 42", "(string->number (number->string 42))", 42, pass, fail);
    test_eq(interp, "number->string round-trip 0", "(string->number (number->string 0))", 0, pass, fail);
    test_eq(interp, "number->string round-trip -99", "(string->number (number->string -99))", -99, pass, fail);

    // Stack overflow
    test_error(interp, "stack overflow caught", "(let ^rec ((f (lambda (n) (+ 1 (f (+ n 1)))))) (f 0))", pass, fail);

    // Quasiquote
    test_tag(interp, "quasiquote basic", "`(a b c)", CONS, pass, fail);
    setup(interp, "(define qq-x 42)");
    test_tag(interp, "quasiquote unquote", "`(a ,qq-x c)", CONS, pass, fail);
    test_eq(interp, "quasiquote standalone", "`,(+ 1 2)", 3, pass, fail);
    setup(interp, "(define qq-lst (list 1 2 3))");
    test_eq(interp, "quasiquote splice len", "(length `(a ,@qq-lst d))", 5, pass, fail);
    setup(interp, "(define qq-empty (list))");
    test_eq(interp, "quasiquote splice empty", "(length `(a ,@qq-empty b))", 2, pass, fail);

    // Pattern macros
    setup(interp, "(define [macro] when ([test .. body] (if test (begin .. body) nil)))");
    test_eq(interp, "when true", "(when true 42)", 42, pass, fail);
    test_nil(interp, "when false", "(when false 42)", pass, fail);
    setup(interp, "(define [macro] unless ([test .. body] (if test nil (begin .. body))))");
    test_eq(interp, "unless false", "(unless false 99)", 99, pass, fail);
    test_nil(interp, "unless true", "(unless true 99)", pass, fail);
    setup(interp, "(define [macro] with-val ([name val body] (let ((name val)) body)))");
    test_eq(interp, "macro with-val", "(with-val z 100 (+ z 1))", 101, pass, fail);
    setup(interp, "(define [macro] cond ([] nil) ([test body .. rest] (if test body (cond .. rest))))");
    test_eq(interp, "cond macro", "(cond false 1 true 2 true 3)", 2, pass, fail);
    test_nil(interp, "cond all false", "(cond false 1 false 2)", pass, fail);
    setup(interp, "(define [macro] my-and ([a b] (if a b false)))");
    test_eq(interp, "my-and true", "(my-and true 42)", 42, pass, fail);
    setup(interp, "(define [macro] let1 ([name val body] (let ((name val)) body)))");
    test_eq(interp, "let1 macro", "(let1 x 10 (+ x 5))", 15, pass, fail);

    // Auto-gensym
    setup(interp, "(define [macro] swap! ([a b] (let ((tmp# a)) (begin (set! a b) (set! b tmp#)))))");
    setup(interp, "(define swap-x 10)");
    setup(interp, "(define swap-y 20)");
    setup(interp, "(swap! swap-x swap-y)");
    test_eq(interp, "swap! x", "swap-x", 20, pass, fail);
    test_eq(interp, "swap! y", "swap-y", 10, pass, fail);
    setup(interp, "(define tmp 999)");
    setup(interp, "(define hx 1)");
    setup(interp, "(define hy 2)");
    setup(interp, "(swap! hx hy)");
    test_eq(interp, "gensym hygiene", "tmp", 999, pass, fail);

    // gensym
    test_tag(interp, "gensym returns symbol", "(gensym)", SYMBOL, pass, fail);

    // Macro hygiene (def-env capture)
    setup(interp, "(define hyg-helper (lambda (x) (+ x 100)))");
    setup(interp, "(define [macro] hyg-apply ([x] (hyg-helper x)))");
    test_eq(interp, "hygiene normal", "(hyg-apply 5)", 105, pass, fail);
    setup(interp, "(define hyg-helper (lambda (x) (* x 999)))");
    test_eq(interp, "hygiene immune to shadow", "(hyg-apply 5)", 105, pass, fail);
    setup(interp, "(define [macro] hyg-inc ([x] (+ x 1)))");
    test_eq(interp, "hygiene captures +", "(hyg-inc 10)", 11, pass, fail);
    setup(interp, "(define [macro] hyg-nil-test ([x] (if x 42 nil)))");
    test_nil(interp, "hygiene nil branch", "(hyg-nil-test false)", pass, fail);
    test_eq(interp, "hygiene true branch", "(hyg-nil-test true)", 42, pass, fail);
    setup(interp, "(define hyg-a 10)");
    setup(interp, "(define hyg-b 20)");
    setup(interp, "(define [macro] hyg-sum ([x] (+ (+ hyg-a hyg-b) x)))");
    test_eq(interp, "hygiene multi literals", "(hyg-sum 5)", 35, pass, fail);
    setup(interp, "(define hyg-a 1000)");
    setup(interp, "(define hyg-b 2000)");
    test_eq(interp, "hygiene multi immune", "(hyg-sum 5)", 35, pass, fail);
    setup(interp, "(define hyg-op (lambda (x) (* x 2)))");
    setup(interp, "(define [macro] hyg-with-tmp ([x] (let ((tmp# (hyg-op x))) tmp#)))");
    test_eq(interp, "hygiene gensym+def", "(hyg-with-tmp 5)", 10, pass, fail);
    setup(interp, "(define hyg-op (lambda (x) 0))");
    test_eq(interp, "hygiene gensym+def immune", "(hyg-with-tmp 5)", 10, pass, fail);
    setup(interp, "(define x 999)");
    setup(interp, "(define [macro] hyg-id ([x] x))");
    test_eq(interp, "hygiene patvar substitutes", "(hyg-id 7)", 7, pass, fail);
    test_eq(interp, "hygiene recursive cond", "(cond false 1 false 2 true 3)", 3, pass, fail);
    setup(interp, "(define hyg-late-fn (lambda (x) (+ x 50)))");
    setup(interp, "(define [macro] hyg-late-macro ([x] (hyg-late-fn x)))");
    setup(interp, "(define hyg-late-fn (lambda (x) (- x 50)))");
    test_eq(interp, "hygiene late redef", "(hyg-late-macro 100)", 150, pass, fail);

    // Hash maps
    setup(interp, "(define hm1 (hash-map 1 10 2 20 3 30))");
    test_eq(interp, "hash-ref", "(hash-ref hm1 2)", 20, pass, fail);
    test_nil(interp, "hash-ref missing", "(hash-ref hm1 99)", pass, fail);
    setup(interp, "(hash-set! hm1 2 200)");
    test_eq(interp, "hash-set! overwrite", "(hash-ref hm1 2)", 200, pass, fail);
    test_truthy(interp, "hash-has? yes", "(hash-has? hm1 1)", pass, fail);
    test_nil(interp, "hash-has? no", "(hash-has? hm1 99)", pass, fail);
    test_eq(interp, "hash-count", "(hash-count hm1)", 3, pass, fail);
    setup(interp, "(define hm2 (hash-map 1 10 2 20 3 30))");
    setup(interp, "(hash-remove! hm2 2)");
    test_nil(interp, "hash-remove!", "(hash-has? hm2 2)", pass, fail);
    test_eq(interp, "hash-remove! count", "(hash-count hm2)", 2, pass, fail);
    setup(interp, "(define hm3 (hash-map 1 10 2 20))");
    test_eq(interp, "hash-keys len", "(length (hash-keys hm3))", 2, pass, fail);
    test_eq(interp, "hash-values len", "(length (hash-values hm3))", 2, pass, fail);
    setup(interp, "(define hm4 (hash-map \"name\" \"Alice\" \"age\" 30))");
    test_str(interp, "hash string key", "(hash-ref hm4 \"name\")", pass, fail);
    setup(interp, "(define hm5 (hash-map))");
    test_eq(interp, "hash empty count", "(hash-count hm5)", 0, pass, fail);
    test_truthy(interp, "hash-map? yes", "(hash-map? (hash-map 1 2))", pass, fail);
    test_nil(interp, "hash-map? no", "(hash-map? 42)", pass, fail);
    setup(interp, "(define hm-big (hash-map))");
    setup(interp, "(let loop ((i 0)) (if (= i 50) hm-big (begin (hash-set! hm-big i (* i i)) (loop (+ i 1)))))");
    test_eq(interp, "hash auto-grow count", "(hash-count hm-big)", 50, pass, fail);
    test_eq(interp, "hash auto-grow ref 7", "(hash-ref hm-big 7)", 49, pass, fail);

    // === MODULE SYSTEM TESTS ===
    setup(interp, "(module math-utils (export double triple) (define double (lambda (x) (* x 2))) (define triple (lambda (x) (* x 3))) (define internal (lambda (x) (+ x 100))))");
    setup(interp, "(import math-utils)");
    test_eq(interp, "module double", "(double 5)", 10, pass, fail);
    test_eq(interp, "module triple", "(triple 4)", 12, pass, fail);
    test_error(interp, "module unexported hidden", "(internal 5)", pass, fail);
    setup(interp, "(import math-utils)");
    test_eq(interp, "module re-import cached", "(double 7)", 14, pass, fail);
    test_error(interp, "module duplicate def", "(module math-utils (export) (define x 1))", pass, fail);
    setup(interp, "(module list-utils (export sum-list) (define sum-list (lambda (lst) (((foldl (lambda (a) (lambda (b) (+ a b)))) 0) lst))))");
    setup(interp, "(import list-utils)");
    test_eq(interp, "module stdlib access", "(sum-list (quote (1 2 3 4 5)))", 15, pass, fail);
    setup(interp, "(module base-mod (export base-fn) (define base-fn (lambda (x) (+ x 100))))");
    setup(interp, "(import base-mod)");
    setup(interp, "(module derived-mod (export derived-fn) (define derived-fn (lambda (x) (base-fn (* x 2)))))");
    setup(interp, "(import derived-mod)");
    test_eq(interp, "module depends on another", "(derived-fn 5)", 110, pass, fail);
    setup(interp, "(module private-mod (export) (define secret 42))");
    setup(interp, "(import private-mod)");
    test_error(interp, "module empty export hides", "secret", pass, fail);
    setup(interp, "(module closure-mod (export make-counter) (define make-counter (lambda () (let ((n 0)) (lambda () (begin (set! n (+ n 1)) n))))))");
    setup(interp, "(import closure-mod)");
    setup(interp, "(define my-counter (make-counter))");
    // Counter tests are stateful: JIT-only to avoid double mutation
    test_eq_jit(interp, "module counter 1", "(my-counter)", 1, pass, fail);
    test_eq_jit(interp, "module counter 2", "(my-counter)", 2, pass, fail);
    setup(interp, "(module tail-mod (export tail-sum) (define tail-sum (lambda (n acc) (if (= n 0) acc (tail-sum (- n 1) (+ acc n))))))");
    setup(interp, "(import tail-mod)");
    test_eq(interp, "module tail-call sum 1000", "(tail-sum 1000 0)", 500500, pass, fail);

    // === SHORTHAND DEFINE TESTS ===
    setup(interp, "(define (sh-double x) (* x 2))");
    test_eq(interp, "shorthand define", "(sh-double 21)", 42, pass, fail);
    setup(interp, "(define (sh-add3 a b c) (+ a (+ b c)))");
    test_eq(interp, "shorthand define multi", "(sh-add3 10 20 30)", 60, pass, fail);
    setup(interp, "(define (sh-answer) 42)");
    test_eq(interp, "shorthand define zero-arg", "(sh-answer)", 42, pass, fail);

    // === STDLIB TESTS ===
    test_eq(interp, "map car", "(car ((map (lambda (x) (* x 2))) (quote (1 2 3))))", 2, pass, fail);
    test_eq(interp, "map length", "(length ((map (lambda (x) (* x 2))) (quote (1 2 3))))", 3, pass, fail);
    test_nil(interp, "map empty", "((map (lambda (x) (* x 2))) (quote ()))", pass, fail);
    test_eq(interp, "filter car", "(car ((filter (lambda (x) (> x 2))) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "filter length", "(length ((filter (lambda (x) (> x 2))) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_nil(interp, "filter none", "((filter (lambda (x) (> x 100))) (quote (1 2 3)))", pass, fail);
    test_eq(interp, "foldl sum", "(((foldl (lambda (acc) (lambda (x) (+ acc x)))) 0) (quote (1 2 3 4 5)))", 15, pass, fail);
    test_eq(interp, "foldl product", "(((foldl (lambda (acc) (lambda (x) (* acc x)))) 1) (quote (1 2 3 4)))", 24, pass, fail);
    test_eq(interp, "foldr cons len", "(length (((foldr (lambda (x) (lambda (acc) (cons x acc)))) nil) (quote (1 2 3))))", 3, pass, fail);
    test_eq(interp, "foldr cons car", "(car (((foldr (lambda (x) (lambda (acc) (cons x acc)))) nil) (quote (1 2 3))))", 1, pass, fail);
    test_eq(interp, "append length", "(length ((append (quote (1 2))) (quote (3 4))))", 4, pass, fail);
    test_eq(interp, "append car", "(car ((append (quote (1 2))) (quote (3 4))))", 1, pass, fail);
    test_eq(interp, "append nil left", "(car ((append nil) (quote (1 2))))", 1, pass, fail);
    test_eq(interp, "reverse car", "(car (reverse (quote (1 2 3))))", 3, pass, fail);
    test_eq(interp, "reverse length", "(length (reverse (quote (1 2 3))))", 3, pass, fail);
    test_nil(interp, "reverse nil", "(reverse nil)", pass, fail);
    test_eq(interp, "compose *2.+1", "(((compose (lambda (x) (* x 2))) (lambda (x) (+ x 1))) 3)", 8, pass, fail);
    test_eq(interp, "compose +1.*2", "(((compose (lambda (x) (+ x 1))) (lambda (x) (* x 2))) 3)", 7, pass, fail);
    test_eq(interp, "id", "(id 42)", 42, pass, fail);
    test_eq(interp, "nth 0", "((nth 0) (quote (10 20 30 40)))", 10, pass, fail);
    test_eq(interp, "nth 2", "((nth 2) (quote (10 20 30 40)))", 30, pass, fail);
    test_eq(interp, "take length", "(length ((take 3) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "take car", "(car ((take 3) (quote (1 2 3 4 5))))", 1, pass, fail);
    test_nil(interp, "take 0", "((take 0) (quote (1 2 3)))", pass, fail);
    test_eq(interp, "drop car", "(car ((drop 2) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "drop length", "(length ((drop 2) (quote (1 2 3 4 5))))", 3, pass, fail);
    test_eq(interp, "drop 0", "(car ((drop 0) (quote (1 2 3))))", 1, pass, fail);
    test_eq(interp, "zip length", "(length ((zip (quote (1 2 3))) (quote (4 5 6))))", 3, pass, fail);
    test_eq(interp, "zip first car", "(car (car ((zip (quote (1 2 3))) (quote (4 5 6)))))", 1, pass, fail);
    test_eq(interp, "zip first cdr", "(cdr (car ((zip (quote (1 2 3))) (quote (4 5 6)))))", 4, pass, fail);
    test_eq(interp, "zip unequal", "(length ((zip (quote (1 2))) (quote (4 5 6))))", 2, pass, fail);
    test_eq(interp, "range length", "(length (range 5))", 5, pass, fail);
    test_eq(interp, "range car", "(car (range 5))", 0, pass, fail);
    test_nil(interp, "range 0", "(range 0)", pass, fail);
    test_nil(interp, "for-each", "((for-each (lambda (x) (+ x 1))) (quote (1 2 3)))", pass, fail);
    test_truthy(interp, "any? found", "((any? (lambda (x) (= x 3))) (quote (1 2 3 4)))", pass, fail);
    test_nil(interp, "any? not found", "((any? (lambda (x) (= x 99))) (quote (1 2 3 4)))", pass, fail);
    test_truthy(interp, "every? all", "((every? (lambda (x) (> x 0))) (quote (1 2 3 4)))", pass, fail);
    test_nil(interp, "every? not all", "((every? (lambda (x) (> x 2))) (quote (1 2 3 4)))", pass, fail);
    test_truthy(interp, "every? empty", "((every? (lambda (x) (> x 0))) nil)", pass, fail);
    test_eq(interp, "try/raise catch", "((try (lambda (xx) (+ 1 (perform raise 42)))) (lambda (msg) msg))", 42, pass, fail);
    test_eq(interp, "try no error", "((try (lambda (xx) (+ 1 2))) (lambda (msg) msg))", 3, pass, fail);
    test_truthy(interp, "assert! true", "((assert! (= 1 1)) \"ok\")", pass, fail);
    test_truthy(interp, "assert! false raises", "(handle ((assert! (= 1 2)) \"fail\") ((raise k msg) msg))", pass, fail);
    setup(interp, "(define test-alist (list (cons 1 10) (cons 2 20) (cons 3 30)))");
    test_eq(interp, "assoc find", "(cdr ((assoc 2) test-alist))", 20, pass, fail);
    test_nil(interp, "assoc not found", "((assoc 99) test-alist)", pass, fail);
    test_eq(interp, "assoc-ref", "((assoc-ref 3) test-alist)", 30, pass, fail);
    test_nil(interp, "assoc-ref not found", "((assoc-ref 99) test-alist)", pass, fail);
    test_eq(interp, "map+filter+foldl", "(((foldl (lambda (acc) (lambda (x) (+ acc x)))) 0) ((filter (lambda (x) (> x 3))) ((map (lambda (x) (* x 2))) (quote (1 2 3 4 5)))))", 28, pass, fail);
    test_eq(interp, "reverse+take", "(car ((take 1) (reverse (quote (1 2 3)))))", 3, pass, fail);
    test_eq(interp, "sum of squares", "(((foldl (lambda (acc) (lambda (x) (+ acc x)))) 0) ((map (lambda (x) (* x x))) (range 5)))", 30, pass, fail);

    // === FFI TESTS ===
    setup(interp, "(define test-libc (ffi-open \"libc.so.6\"))");
    test_tag(interp, "ffi-open handle", "test-libc", FFI_HANDLE, pass, fail);
    test_eq(interp, "ffi-call strlen", "(ffi-call test-libc \"strlen\" 'size \"hello\" 'string)", 5, pass, fail);
    test_eq(interp, "ffi-call abs", "(ffi-call test-libc \"abs\" 'int (- 0 42) 'int)", 42, pass, fail);
    test_gt(interp, "ffi-call getpid", "(ffi-call test-libc \"getpid\" 'int)", 0, pass, fail);
    test_eq(interp, "ffi-call atoi", "(ffi-call test-libc \"atoi\" 'int \"12345\" 'string)", 12345, pass, fail);
    test_truthy(interp, "ffi-sym strlen", "(> (ffi-sym test-libc \"strlen\") 0)", pass, fail);
    setup(interp, "(define test-libc2 (ffi-open \"libc.so.6\"))");
    // ffi-close is a side effect: JIT-only to avoid double-close
    test_nil_jit(interp, "ffi-close", "(ffi-close test-libc2)", pass, fail);
    test_error(interp, "ffi-open bad lib", "(ffi-open \"nonexistent_lib_xyz.so\")", pass, fail);
    setup(interp, "(ffi-close test-libc)");
}

// =============================================================================
// SECTION 11: COMPILER TESTS
// =============================================================================

/**
 * Check if haystack contains needle as a substring.
 */
fn bool str_contains(char[] haystack, char[] needle) {
    if (needle.len == 0) return true;
    if (needle.len > haystack.len) return false;
    for (usz i = 0; i <= haystack.len - needle.len; i++) {
        bool match = true;
        for (usz j = 0; j < needle.len; j++) {
            if (haystack[i + j] != needle[j]) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

/**
 * Compiler tests — verify that the compiler produces valid C3 output
 * for all supported features. Tests check that compile_to_c3() succeeds
 * and the output does not contain "unsupported".
 */
fn void run_compiler_tests(Interp* _unused_interp) {
    io::printn("\n=== Compiler Tests ===");
    usz pass = 0;
    usz fail = 0;

    // Use a fresh interp for compiler tests since each compile_to_c3 call
    // parses the entire stdlib prelude, consuming many expr pool entries.
    Interp* interp = (Interp*)mem::malloc(Interp.sizeof);
    interp.init();
    register_primitives(interp);

    // --- E_BEGIN tests ---

    // 1. begin with single expression
    {
        char[] code = compile_to_c3("(begin 42)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: (begin 42)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (begin 42)"); }
    }

    // 2. begin with multiple expressions
    {
        char[] code = compile_to_c3("(begin 1 2 3)", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "({");
        if (ok) { pass++; io::printn("[PASS] Compiler: (begin 1 2 3)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (begin 1 2 3)"); }
    }

    // 3. begin with define inside
    {
        char[] code = compile_to_c3("(begin (define x 1) x)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: (begin (define x 1) x)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (begin (define x 1) x)"); }
    }

    // 4. begin inside lambda body
    {
        char[] code = compile_to_c3("(lambda (x) (begin x x))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: (lambda (x) (begin x x))"); }
        else    { fail++; io::printn("[FAIL] Compiler: (lambda (x) (begin x x))"); }
    }

    // --- Zero-arg lambda tests ---

    // 5. zero-arg lambda
    {
        char[] code = compile_to_c3("((lambda () 42))", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "_unused");
        if (ok) { pass++; io::printn("[PASS] Compiler: ((lambda () 42))"); }
        else    { fail++; io::printn("[FAIL] Compiler: ((lambda () 42))"); }
    }

    // 6. zero-arg define + call
    {
        char[] code = compile_to_c3("(define thunk (lambda () 42)) (thunk)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: zero-arg define + call"); }
        else    { fail++; io::printn("[FAIL] Compiler: zero-arg define + call"); }
    }

    // 7. zero-arg in let
    {
        char[] code = compile_to_c3("(let ((f (lambda () 10))) (f))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: zero-arg in let"); }
        else    { fail++; io::printn("[FAIL] Compiler: zero-arg in let"); }
    }

    // --- Multi-param + multi-binding (parser desugar) ---

    // 8. multi-param lambda (desugared by parser to nested)
    {
        char[] code = compile_to_c3("((lambda (x y) (+ x y)) 3 4)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: multi-param lambda"); }
        else    { fail++; io::printn("[FAIL] Compiler: multi-param lambda"); }
    }

    // 9. multi-binding let (desugared by parser to nested)
    {
        char[] code = compile_to_c3("(let ((x 1) (y 2)) (+ x y))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: multi-binding let"); }
        else    { fail++; io::printn("[FAIL] Compiler: multi-binding let"); }
    }

    // 10. 3-param lambda
    {
        char[] code = compile_to_c3("((lambda (a b c) a) 1 2 3)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: 3-param lambda"); }
        else    { fail++; io::printn("[FAIL] Compiler: 3-param lambda"); }
    }

    // --- Stdlib availability ---

    // 11. map compiles
    {
        char[] code = compile_to_c3("((map (lambda (x) (+ x 1))) (list 1))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: map"); }
        else    { fail++; io::printn("[FAIL] Compiler: map"); }
    }

    // 12. filter compiles
    {
        char[] code = compile_to_c3("((filter (lambda (x) (> x 2))) (list 1 2 3))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: filter"); }
        else    { fail++; io::printn("[FAIL] Compiler: filter"); }
    }

    // 13. foldl compiles
    {
        char[] code = compile_to_c3("(((foldl (lambda (acc) (lambda (x) (+ acc x)))) 0) (list 1 2 3))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: foldl"); }
        else    { fail++; io::printn("[FAIL] Compiler: foldl"); }
    }

    // --- Existing features still work ---

    // 14. match compiles
    {
        char[] code = compile_to_c3("(match (list 1 2 3) ([a b c] (+ a c)))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: match"); }
        else    { fail++; io::printn("[FAIL] Compiler: match"); }
    }

    // 15. handle/perform compiles
    {
        char[] code = compile_to_c3("(handle (+ 1 (perform ask 0)) ((ask k x) (k 10)))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: handle/perform"); }
        else    { fail++; io::printn("[FAIL] Compiler: handle/perform"); }
    }

    // 16. zero-arg lambda with closure capture
    {
        char[] code = compile_to_c3("(let ((x 42)) (let ((f (lambda () x))) (f)))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: zero-arg closure capture"); }
        else    { fail++; io::printn("[FAIL] Compiler: zero-arg closure capture"); }
    }

    // 17. id and compose from stdlib
    {
        char[] code = compile_to_c3("(id 5)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: id"); }
        else    { fail++; io::printn("[FAIL] Compiler: id"); }
    }

    // 18. range from stdlib
    {
        char[] code = compile_to_c3("(range 5)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: range"); }
        else    { fail++; io::printn("[FAIL] Compiler: range"); }
    }

    // --- set! tests ---

    // 19. set! on global variable
    {
        char[] code = compile_to_c3("(define x 10) (set! x 20)", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "x = ");
        if (ok) { pass++; io::printn("[PASS] Compiler: (set! x 20)"); }
        else    { fail++; io::printn("[FAIL] Compiler: (set! x 20)"); }
    }

    // 20. set! in expression context
    {
        char[] code = compile_to_c3("(define x 10) (+ (set! x 20) x)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: set! in expression"); }
        else    { fail++; io::printn("[FAIL] Compiler: set! in expression"); }
    }

    // 21. set! inside lambda
    {
        char[] code = compile_to_c3("(define counter 0) (define inc! (lambda (n) (set! counter (+ counter n))))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: set! inside lambda"); }
        else    { fail++; io::printn("[FAIL] Compiler: set! inside lambda"); }
    }

    // --- dot-bracket indexing tests ---

    // 22. list indexing with .[i]
    {
        char[] code = compile_to_c3("(define lst (list 10 20 30)) lst.[1]", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_index");
        if (ok) { pass++; io::printn("[PASS] Compiler: lst.[1]"); }
        else    { fail++; io::printn("[FAIL] Compiler: lst.[1]"); }
    }

    // 23. indexing with expression
    {
        char[] code = compile_to_c3("(define lst (list 1 2 3)) lst.[(+ 0 1)]", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_index");
        if (ok) { pass++; io::printn("[PASS] Compiler: lst.[(+ 0 1)]"); }
        else    { fail++; io::printn("[FAIL] Compiler: lst.[(+ 0 1)]"); }
    }

    // --- path notation tests ---

    // 24. simple path a.b
    {
        char[] code = compile_to_c3("(define point (list (cons 'x 10) (cons 'y 20))) point.x", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_field_access");
        if (ok) { pass++; io::printn("[PASS] Compiler: point.x"); }
        else    { fail++; io::printn("[FAIL] Compiler: point.x"); }
    }

    // 25. nested path a.b.c
    {
        char[] code = compile_to_c3("(define obj (list (cons 'inner (list (cons 'val 42))))) obj.inner.val", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_field_access");
        if (ok) { pass++; io::printn("[PASS] Compiler: obj.inner.val"); }
        else    { fail++; io::printn("[FAIL] Compiler: obj.inner.val"); }
    }

    // 26. path generates make_string for field name (not raw string)
    {
        char[] code = compile_to_c3("point.x", interp);
        bool ok = str_contains(code, "make_string");
        if (ok) { pass++; io::printn("[PASS] Compiler: path uses make_string for field name"); }
        else    { fail++; io::printn("[FAIL] Compiler: path uses make_string for field name"); }
    }

    // --- reset/shift via interpreter delegation ---

    // 27. reset/shift generates rt_eval_source call
    {
        char[] code = compile_to_c3("(reset (+ 1 (shift k (k 10))))", interp);
        bool ok = str_contains(code, "rt_eval_source") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset/shift uses rt_eval_source"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset/shift uses rt_eval_source"); }
    }

    // 28. set! inside begin
    {
        char[] code = compile_to_c3("(define x 0) (begin (set! x 1) (set! x (+ x 1)) x)", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: set! inside begin"); }
        else    { fail++; io::printn("[FAIL] Compiler: set! inside begin"); }
    }

    // --- Continuation form tests ---

    // 29. reset with free variable injects rt_define_var
    {
        char[] code = compile_to_c3("(define x 5) (reset (+ x (shift k (k 10))))", interp);
        bool ok = str_contains(code, "rt_eval_source") && str_contains(code, "rt_define_var");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset with free var injects rt_define_var"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset with free var injects rt_define_var"); }
    }

    // 30. handle/perform generates rt_eval_source
    {
        char[] code = compile_to_c3("(handle (+ 1 (perform ask 0)) ((ask k x) (k 10)))", interp);
        bool ok = str_contains(code, "rt_eval_source") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: handle/perform uses rt_eval_source"); }
        else    { fail++; io::printn("[FAIL] Compiler: handle/perform uses rt_eval_source"); }
    }

    // 31. standalone perform generates rt_eval_source
    {
        char[] code = compile_to_c3("(perform raise \"error\")", interp);
        bool ok = str_contains(code, "rt_eval_source") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: perform uses rt_eval_source"); }
        else    { fail++; io::printn("[FAIL] Compiler: perform uses rt_eval_source"); }
    }

    // 32. shift generates rt_eval_source
    {
        char[] code = compile_to_c3("(shift k 42)", interp);
        bool ok = str_contains(code, "rt_eval_source") && !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: shift uses rt_eval_source"); }
        else    { fail++; io::printn("[FAIL] Compiler: shift uses rt_eval_source"); }
    }

    // 33. serialized reset source contains correct structure
    {
        char[] code = compile_to_c3("(reset (+ 1 2))", interp);
        bool ok = str_contains(code, "(reset") && str_contains(code, "rt_eval_source");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset serializes correctly"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset serializes correctly"); }
    }

    // 34. handle with multiple clauses serializes correctly
    {
        char[] code = compile_to_c3("(handle (begin (perform get 0) (perform set 1)) ((get k x) (k 42)) ((set k v) (k v)))", interp);
        bool ok = str_contains(code, "rt_eval_source") && str_contains(code, "handle");
        if (ok) { pass++; io::printn("[PASS] Compiler: handle with multiple clauses"); }
        else    { fail++; io::printn("[FAIL] Compiler: handle with multiple clauses"); }
    }

    // 35. try from stdlib (uses handle/perform internally) compiles
    {
        char[] code = compile_to_c3("((try (lambda (xx) (+ 1 2))) (lambda (msg) 0))", interp);
        bool ok = !str_contains(code, "unsupported");
        if (ok) { pass++; io::printn("[PASS] Compiler: try from stdlib"); }
        else    { fail++; io::printn("[FAIL] Compiler: try from stdlib"); }
    }

    // 36. reset with nested shift serializes correctly
    {
        char[] code = compile_to_c3("(reset (+ 1 (shift k (+ (k 10) (k 20)))))", interp);
        bool ok = str_contains(code, "rt_eval_source") && str_contains(code, "shift");
        if (ok) { pass++; io::printn("[PASS] Compiler: reset with nested shift"); }
        else    { fail++; io::printn("[FAIL] Compiler: reset with nested shift"); }
    }

    // --- FFI compiler tests ---

    // 37. ffi-open compiles without unsupported
    {
        char[] code = compile_to_c3("(ffi-open \"libc.so.6\")", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_ffi_open");
        if (ok) { pass++; io::printn("[PASS] Compiler: ffi-open"); }
        else    { fail++; io::printn("[FAIL] Compiler: ffi-open"); }
    }

    // 38. ffi-close compiles without unsupported
    {
        char[] code = compile_to_c3("(define h (ffi-open \"libc.so.6\")) (ffi-close h)", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_ffi_close");
        if (ok) { pass++; io::printn("[PASS] Compiler: ffi-close"); }
        else    { fail++; io::printn("[FAIL] Compiler: ffi-close"); }
    }

    // 39. ffi-sym compiles without unsupported
    {
        char[] code = compile_to_c3("(define h (ffi-open \"libc.so.6\")) (ffi-sym h \"strlen\")", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_ffi_sym");
        if (ok) { pass++; io::printn("[PASS] Compiler: ffi-sym"); }
        else    { fail++; io::printn("[FAIL] Compiler: ffi-sym"); }
    }

    // 40. ffi-call compiles without unsupported
    {
        char[] code = compile_to_c3("(define h (ffi-open \"libc.so.6\")) (ffi-call h \"strlen\" 'size \"hello\" 'string)", interp);
        bool ok = !str_contains(code, "unsupported") && str_contains(code, "rt_ffi_call");
        if (ok) { pass++; io::printn("[PASS] Compiler: ffi-call"); }
        else    { fail++; io::printn("[FAIL] Compiler: ffi-call"); }
    }

    mem::free(interp);
    io::printfn("\n=== Compiler Tests: %d passed, %d failed ===", pass, fail);
    assert(fail == 0, "compiler tests had failures");
}
