#define _POSIX_C_SOURCE 200809L
#include "omni_eval.h"
#include "../reader/omni_reader.h"
#include "../util/dstring.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <setjmp.h>

// Forward declarations
static Value* eval_list(Value* exprs, Env* env);
static Value* eval_special_form(Value* op, Value* args, Env* env);
static int is_special_form(Value* sym);
static int is_truthy(Value* v);
static int match_pattern(Value* pattern, Value* subject, Env* env);
static int match_list_pattern(Value* patterns, Value* items, Env* env);
static Value* eval_quasiquote(Value* expr, Env* env);
static Value* env_to_value(Env* env);
static Env* value_to_env(Value* v, Env* parent);
static Value* eval_if(Value* args, Env* env);
static Value* eval_let(Value* args, Env* env);
static Value* eval_define(Value* args, Env* env);
static Value* eval_lambda(Value* args, Env* env);
static Value* eval_do(Value* args, Env* env);
static Value* eval_quote(Value* args, Env* env);
static Value* eval_set(Value* args, Env* env);
static Value* eval_match(Value* args, Env* env);
static Value* eval_cond(Value* args, Env* env);
static Value* eval_array(Value* args, Env* env);
static Value* eval_dict(Value* args, Env* env);
static Value* eval_for(Value* args, Env* env);
static Value* eval_foreach(Value* args, Env* env);
static Value* eval_prompt(Value* args, Env* env);
static Value* eval_control(Value* args, Env* env);

// -- Prompt Stack for Delimited Continuations --
#define MAX_PROMPT_STACK 64

typedef struct PromptFrame {
    jmp_buf jmp;
    Value* result;         // Result from control
    Value* cont_var;       // Variable name bound to continuation in control
    Value* cont_body;      // Body to execute after capture
    Env* cont_env;         // Environment for continuation body
    int captured;          // Was control invoked?
} PromptFrame;

static PromptFrame prompt_stack[MAX_PROMPT_STACK];
static int prompt_sp = 0;  // Stack pointer

// Primitive registry
#define MAX_PRIMITIVES 256
static struct {
    const char* name;
    int arity;
    PrimitiveFn fn;
} primitives[MAX_PRIMITIVES];
static int num_primitives = 0;

// Global environment for top-level definitions
static Env* global_env = NULL;

// Environment operations
Env* env_new(Env* parent) {
    Env* env = malloc(sizeof(Env));
    if (!env) return NULL;
    env->bindings = mk_nil();
    env->parent = parent;
    return env;
}

void env_free(Env* env) {
    // Note: In full implementation, would need proper GC
    if (env) free(env);
}

Env* env_extend(Env* env, Value* names, Value* values) {
    Env* new_env = env_new(env);
    if (!new_env) return NULL;

    while (!is_nil(names) && !is_nil(values)) {
        Value* name = car(names);
        Value* value = car(values);

        // Handle rest parameter ..
        if (name && name->tag == T_SYM && strcmp(name->s, "..") == 0) {
            names = cdr(names);
            if (!is_nil(names)) {
                name = car(names);
                env_define(new_env, name, values);
            }
            break;
        }

        env_define(new_env, name, value);
        names = cdr(names);
        values = cdr(values);
    }

    return new_env;
}

Value* env_lookup(Env* env, Value* name) {
    if (!name || name->tag != T_SYM) return NULL;

    while (env) {
        Value* bindings = env->bindings;
        while (!is_nil(bindings)) {
            Value* pair = car(bindings);
            if (pair && pair->tag == T_CELL) {
                Value* key = car(pair);
                if (key && key->tag == T_SYM && strcmp(key->s, name->s) == 0) {
                    return cdr(pair);
                }
            }
            bindings = cdr(bindings);
        }
        env = env->parent;
    }
    return NULL;
}

void env_define(Env* env, Value* name, Value* value) {
    if (!env || !name || name->tag != T_SYM) return;
    Value* pair = mk_cell(name, value);
    env->bindings = mk_cell(pair, env->bindings);
}

// Convert Env to a Value (association list for closure capture)
static Value* env_to_value(Env* env) {
    // For closures, we flatten the entire env chain into one list
    Value* result = mk_nil();
    while (env) {
        Value* bindings = env->bindings;
        while (!is_nil(bindings)) {
            result = mk_cell(car(bindings), result);
            bindings = cdr(bindings);
        }
        env = env->parent;
    }
    return result;
}

// Convert Value (association list) back to Env for execution
static Env* value_to_env(Value* v, Env* parent) {
    Env* env = env_new(parent);
    if (!env) return NULL;
    env->bindings = v;
    return env;
}

void env_set(Env* env, Value* name, Value* value) {
    if (!name || name->tag != T_SYM) return;

    while (env) {
        Value* bindings = env->bindings;
        while (!is_nil(bindings)) {
            Value* pair = car(bindings);
            if (pair && pair->tag == T_CELL) {
                Value* key = car(pair);
                if (key && key->tag == T_SYM && strcmp(key->s, name->s) == 0) {
                    pair->cell.cdr = value;
                    return;
                }
            }
            bindings = cdr(bindings);
        }
        env = env->parent;
    }
}

// Check if symbol is a special form
static int is_special_form(Value* sym) {
    if (!sym || sym->tag != T_SYM) return 0;
    const char* forms[] = {
        "if", "let", "define", "lambda", "fn", "do", "begin",
        "quote", "set!", "match", "cond", "when", "unless",
        "array", "dict", "type", "for", "foreach", "->",
        "quasiquote", "with-meta", "module", "import", "export",
        "prompt", "control",  // Delimited continuations
        NULL
    };
    for (int i = 0; forms[i]; i++) {
        if (strcmp(sym->s, forms[i]) == 0) return 1;
    }
    return 0;
}

// Main evaluation function
Value* omni_eval(Value* expr, Env* env) {
    if (!expr) return mk_nil();

    switch (expr->tag) {
        case T_INT:
        case T_CODE:
        case T_PRIM:
        case T_LAMBDA:
        case T_ERROR:
            return expr;

        case T_NIL:
            return expr;

        case T_SYM: {
            // Special symbols
            if (strcmp(expr->s, "true") == 0) return mk_sym("true");
            if (strcmp(expr->s, "false") == 0) return mk_sym("false");
            if (strcmp(expr->s, "nothing") == 0) return mk_nil();

            // Environment lookup
            Value* value = env_lookup(env, expr);
            if (value) return value;

            // Check primitives
            for (int i = 0; i < num_primitives; i++) {
                if (strcmp(primitives[i].name, expr->s) == 0) {
                    return mk_prim(NULL); // Return marker, actual lookup in apply
                }
            }

            char msg[128];
            snprintf(msg, sizeof(msg), "Undefined symbol: %s", expr->s);
            return mk_error(msg);
        }

        case T_CELL: {
            // Empty list evaluates to itself
            if (is_nil(expr)) return expr;

            Value* op = car(expr);
            Value* args = cdr(expr);

            // Special forms
            if (op && op->tag == T_SYM) {
                if (is_special_form(op)) {
                    return eval_special_form(op, args, env);
                }
            }

            // Function application
            Value* fn = omni_eval(op, env);
            if (is_error(fn)) return fn;

            // Evaluate arguments
            Value* evaled_args = eval_list(args, env);
            if (is_error(evaled_args)) return evaled_args;

            return omni_apply(fn, evaled_args, env);
        }

        default:
            return expr;
    }
}

// Evaluate a list of expressions
static Value* eval_list(Value* exprs, Env* env) {
    if (is_nil(exprs)) return mk_nil();

    Value* result = mk_nil();
    Value** tail = &result;

    while (!is_nil(exprs)) {
        Value* val = omni_eval(car(exprs), env);
        if (is_error(val)) return val;

        Value* cell = mk_cell(val, mk_nil());
        *tail = cell;
        tail = &cell->cell.cdr;

        exprs = cdr(exprs);
    }

    return result;
}

// Special form dispatch
static Value* eval_special_form(Value* op, Value* args, Env* env) {
    const char* name = op->s;

    if (strcmp(name, "if") == 0) return eval_if(args, env);
    if (strcmp(name, "let") == 0) return eval_let(args, env);
    if (strcmp(name, "define") == 0) return eval_define(args, env);
    if (strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0) return eval_lambda(args, env);
    if (strcmp(name, "do") == 0 || strcmp(name, "begin") == 0) return eval_do(args, env);
    if (strcmp(name, "quote") == 0) return eval_quote(args, env);
    if (strcmp(name, "set!") == 0) return eval_set(args, env);
    if (strcmp(name, "match") == 0) return eval_match(args, env);
    if (strcmp(name, "cond") == 0) return eval_cond(args, env);
    if (strcmp(name, "array") == 0) return eval_array(args, env);
    if (strcmp(name, "dict") == 0) return eval_dict(args, env);
    if (strcmp(name, "for") == 0) return eval_for(args, env);
    if (strcmp(name, "foreach") == 0) return eval_foreach(args, env);

    // Lambda shorthand: (-> x body) or (-> (x y) body)
    if (strcmp(name, "->") == 0) {
        Value* params = car(args);
        Value* body = car(cdr(args));
        if (params->tag == T_SYM) {
            // Single param
            params = mk_cell(params, mk_nil());
        } else if (params->tag == T_CELL && car(params)->tag == T_SYM && strcmp(car(params)->s, "array") == 0) {
            // Array literal used as params
            params = cdr(params);
        }
        return mk_lambda(params, body, env_to_value(env));
    }

    // when/unless - expand to match semantics
    if (strcmp(name, "when") == 0) {
        Value* test = omni_eval(car(args), env);
        if (is_error(test)) return test;
        Value* bool_val = mk_sym(is_truthy(test) ? "true" : "false");
        // Match against true - if matches, run body
        Env* match_env = env_new(env);
        if (match_pattern(mk_sym("true"), bool_val, match_env)) {
            Value* result = eval_do(cdr(args), match_env);
            env_free(match_env);
            return result;
        }
        env_free(match_env);
        return mk_nil();
    }

    if (strcmp(name, "unless") == 0) {
        Value* test = omni_eval(car(args), env);
        if (is_error(test)) return test;
        Value* bool_val = mk_sym(is_truthy(test) ? "true" : "false");
        // Match against false - if matches, run body
        Env* match_env = env_new(env);
        if (match_pattern(mk_sym("false"), bool_val, match_env)) {
            Value* result = eval_do(cdr(args), match_env);
            env_free(match_env);
            return result;
        }
        env_free(match_env);
        return mk_nil();
    }

    // with-meta - metadata application
    if (strcmp(name, "with-meta") == 0) {
        // For now, just evaluate the form ignoring metadata
        Value* meta = car(args);
        Value* form = car(cdr(args));
        (void)meta; // TODO: attach metadata
        return omni_eval(form, env);
    }

    // quasiquote
    if (strcmp(name, "quasiquote") == 0) {
        return eval_quasiquote(car(args), env);
    }

    // Delimited continuations
    if (strcmp(name, "prompt") == 0) return eval_prompt(args, env);
    if (strcmp(name, "control") == 0) return eval_control(args, env);

    char msg[128];
    snprintf(msg, sizeof(msg), "Unknown special form: %s", name);
    return mk_error(msg);
}

// Truthiness check (only false and nothing are falsy)
static int is_truthy(Value* v) {
    if (!v || v->tag == T_NIL) return 0;
    if (v->tag == T_SYM && strcmp(v->s, "false") == 0) return 0;
    return 1;
}

// Special form implementations
// if expands to match: (if cond then else) → (match bool [true then] [_ else])
static Value* eval_if(Value* args, Env* env) {
    // Evaluate condition and normalize to true/false for pattern matching
    Value* cond = omni_eval(car(args), env);
    if (is_error(cond)) return cond;
    Value* bool_val = mk_sym(is_truthy(cond) ? "true" : "false");

    Value* then_branch = car(cdr(args));
    Value* else_args = cdr(cdr(args));
    Value* else_branch = is_nil(else_args) ? mk_nil() : car(else_args);

    // Use match_pattern to check against true
    Env* match_env = env_new(env);
    if (match_pattern(mk_sym("true"), bool_val, match_env)) {
        Value* result = omni_eval(then_branch, match_env);
        env_free(match_env);
        return result;
    }
    env_free(match_env);

    // Fall through to else (matches _ pattern)
    return omni_eval(else_branch, env);
}

static Value* eval_let(Value* args, Env* env) {
    // Check for metadata like ^:seq or ^:rec
    Value* bindings;
    Value* body;
    int sequential = 0;
    int recursive = 0;

    // Check if first element is metadata
    Value* first = car(args);
    if (first && first->tag == T_CELL && car(first) && car(first)->tag == T_SYM) {
        if (strcmp(car(first)->s, "with-meta") == 0) {
            // Extract metadata
            // For now just skip
            args = cdr(args);
        }
    }

    // Check for named let: (let name [bindings] body)
    if (car(args) && car(args)->tag == T_SYM) {
        Value* loop_name = car(args);
        bindings = car(cdr(args));
        body = cdr(cdr(args));

        // Create recursive environment
        Env* loop_env = env_new(env);
        // Process bindings
        if (bindings && bindings->tag == T_CELL && car(bindings)->tag == T_SYM &&
            strcmp(car(bindings)->s, "array") == 0) {
            bindings = cdr(bindings);
        }

        // Extract names and initial values
        Value* names = mk_nil();
        Value** ntail = &names;
        Value* vals = mk_nil();
        Value** vtail = &vals;

        while (!is_nil(bindings)) {
            Value* name = car(bindings);
            bindings = cdr(bindings);
            if (is_nil(bindings)) break;
            Value* val = omni_eval(car(bindings), env);
            if (is_error(val)) return val;
            bindings = cdr(bindings);

            *ntail = mk_cell(name, mk_nil());
            ntail = &(*ntail)->cell.cdr;
            *vtail = mk_cell(val, mk_nil());
            vtail = &(*vtail)->cell.cdr;
        }

        // Create the loop lambda - need to make it self-referential
        // First define a placeholder, then create lambda, then update
        Value* placeholder = mk_nil();
        env_define(loop_env, loop_name, placeholder);

        Value* loop_lambda = mk_lambda(names, car(body), env_to_value(loop_env));

        // Update the binding to point to actual lambda
        env_set(loop_env, loop_name, loop_lambda);
        // Also update lambda's captured env
        loop_lambda->lam.env = env_to_value(loop_env);

        // Initial call
        return omni_apply(loop_lambda, vals, loop_env);
    }

    // Regular let
    bindings = car(args);
    body = cdr(args);

    // Handle array-style bindings: [x 1 y 2]
    if (bindings && bindings->tag == T_CELL && car(bindings) &&
        car(bindings)->tag == T_SYM && strcmp(car(bindings)->s, "array") == 0) {
        bindings = cdr(bindings);
    }

    Env* let_env = env_new(env);

    // Process bindings in pairs
    while (!is_nil(bindings)) {
        Value* name = car(bindings);
        bindings = cdr(bindings);
        if (is_nil(bindings)) {
            return mk_error("Odd number of forms in let bindings");
        }

        // Check for type annotation [name {Type} value]
        Value* value_or_type = car(bindings);
        bindings = cdr(bindings);

        Value* value;
        if (value_or_type && value_or_type->tag == T_CELL &&
            car(value_or_type) && car(value_or_type)->tag == T_SYM &&
            strcmp(car(value_or_type)->s, "type") == 0) {
            // Skip type, get value
            if (is_nil(bindings)) {
                return mk_error("Missing value after type annotation");
            }
            value = car(bindings);
            bindings = cdr(bindings);
        } else {
            value = value_or_type;
        }

        Value* evaled = sequential ? omni_eval(value, let_env) : omni_eval(value, env);
        if (is_error(evaled)) return evaled;
        env_define(let_env, name, evaled);
    }

    // Evaluate body
    Value* result = mk_nil();
    while (!is_nil(body)) {
        result = omni_eval(car(body), let_env);
        if (is_error(result)) return result;
        body = cdr(body);
    }
    return result;
}

static Value* eval_define(Value* args, Env* env) {
    Value* first = car(args);

    // Function definition: (define (name params...) body)
    if (first && first->tag == T_CELL) {
        Value* name = car(first);
        Value* params = cdr(first);
        Value* body = car(cdr(args));

        // Wrap body in do if multiple expressions
        if (!is_nil(cdr(cdr(args)))) {
            body = mk_cell(mk_sym("do"), cdr(args));
        }

        Value* lambda = mk_lambda(params, body, env_to_value(env));
        env_define(env, name, lambda);
        return name;
    }

    // Variable definition: (define name value)
    Value* name = first;
    Value* value = omni_eval(car(cdr(args)), env);
    if (is_error(value)) return value;
    env_define(env, name, value);
    return name;
}

static Value* eval_lambda(Value* args, Env* env) {
    Value* params = car(args);
    Value* body = car(cdr(args));

    // Handle array-style params: [x y z]
    if (params && params->tag == T_CELL && car(params) &&
        car(params)->tag == T_SYM && strcmp(car(params)->s, "array") == 0) {
        params = cdr(params);
    }

    // Wrap body in do if multiple expressions
    if (!is_nil(cdr(cdr(args)))) {
        body = mk_cell(mk_sym("do"), cdr(args));
    }

    return mk_lambda(params, body, env_to_value(env));
}

static Value* eval_do(Value* args, Env* env) {
    Value* result = mk_nil();
    while (!is_nil(args)) {
        result = omni_eval(car(args), env);
        if (is_error(result)) return result;
        args = cdr(args);
    }
    return result;
}

static Value* eval_quote(Value* args, Env* env) {
    (void)env;
    return car(args);
}

static Value* eval_set(Value* args, Env* env) {
    Value* target = car(args);
    Value* value = omni_eval(car(cdr(args)), env);
    if (is_error(value)) return value;

    // Simple variable set
    if (target->tag == T_SYM) {
        env_set(env, target, value);
        return mk_nil();
    }

    // TODO: Handle field access (obj.field) and index access (arr.(i))
    return mk_error("set! target must be a symbol");
}

static Value* eval_match(Value* args, Env* env) {
    Value* subject = omni_eval(car(args), env);
    if (is_error(subject)) return subject;

    Value* clauses = cdr(args);
    while (!is_nil(clauses)) {
        Value* clause = car(clauses);

        // Clause format: [pattern result] or [pattern :when guard result]
        if (!clause || clause->tag != T_CELL) {
            clauses = cdr(clauses);
            continue;
        }

        // Handle array wrapper
        if (car(clause)->tag == T_SYM && strcmp(car(clause)->s, "array") == 0) {
            clause = cdr(clause);
        }

        Value* pattern = car(clause);
        Value* rest = cdr(clause);
        Value* guard = NULL;
        Value* result;

        // Check for :when guard
        if (!is_nil(rest) && car(rest) && car(rest)->tag == T_SYM) {
            Value* kw = car(rest);
            if (kw->tag == T_CELL && car(kw)->tag == T_SYM &&
                strcmp(car(kw)->s, "quote") == 0) {
                // Quoted keyword
                Value* inner = car(cdr(kw));
                if (inner && inner->tag == T_SYM && strcmp(inner->s, "when") == 0) {
                    rest = cdr(rest);
                    guard = car(rest);
                    rest = cdr(rest);
                }
            }
        }

        result = car(rest);

        // Try to match pattern
        Env* match_env = env_new(env);
        int matched = match_pattern(pattern, subject, match_env);

        if (matched) {
            // Check guard if present
            if (guard) {
                Value* guard_result = omni_eval(guard, match_env);
                if (!is_truthy(guard_result)) {
                    env_free(match_env);
                    clauses = cdr(clauses);
                    continue;
                }
            }
            return omni_eval(result, match_env);
        }

        env_free(match_env);
        clauses = cdr(clauses);
    }

    return mk_error("No matching clause in match");
}

// Pattern matching helper
static int match_pattern(Value* pattern, Value* subject, Env* env) {
    if (!pattern) return subject == NULL;

    // Wildcard
    if (pattern->tag == T_SYM && strcmp(pattern->s, "_") == 0) {
        return 1;
    }

    // else keyword
    if (pattern->tag == T_SYM && strcmp(pattern->s, "else") == 0) {
        return 1;
    }

    // Literal true - matches only the symbol true
    if (pattern->tag == T_SYM && strcmp(pattern->s, "true") == 0) {
        return subject && subject->tag == T_SYM && strcmp(subject->s, "true") == 0;
    }

    // Literal false - matches only the symbol false
    if (pattern->tag == T_SYM && strcmp(pattern->s, "false") == 0) {
        return subject && subject->tag == T_SYM && strcmp(subject->s, "false") == 0;
    }

    // Variable binding (symbols other than true/false/_/else)
    if (pattern->tag == T_SYM) {
        env_define(env, pattern, subject);
        return 1;
    }

    // Literal int
    if (pattern->tag == T_INT) {
        return subject && subject->tag == T_INT && subject->i == pattern->i;
    }

    // Literal string
    if (pattern->tag == T_CODE) {
        return subject && subject->tag == T_CODE &&
               strcmp(subject->s, pattern->s) == 0;
    }

    // List/array pattern
    if (pattern->tag == T_CELL) {
        Value* op = car(pattern);

        // Empty list/array pattern
        if (is_nil(pattern)) {
            return is_nil(subject);
        }

        // Array pattern: (array ...)
        if (op && op->tag == T_SYM && strcmp(op->s, "array") == 0) {
            if (!subject || subject->tag != T_CELL) return 0;
            // Skip "array" in subject too if present
            Value* subj_items = subject;
            if (car(subject)->tag == T_SYM && strcmp(car(subject)->s, "array") == 0) {
                subj_items = cdr(subject);
            }
            return match_list_pattern(cdr(pattern), subj_items, env);
        }

        // List pattern: (list ...)
        if (op && op->tag == T_SYM && strcmp(op->s, "list") == 0) {
            return match_list_pattern(cdr(pattern), subject, env);
        }

        // cons pattern: (cons h t)
        if (op && op->tag == T_SYM && strcmp(op->s, "cons") == 0) {
            if (!subject || subject->tag != T_CELL) return 0;
            Value* head_pat = car(cdr(pattern));
            Value* tail_pat = car(cdr(cdr(pattern)));
            return match_pattern(head_pat, car(subject), env) &&
                   match_pattern(tail_pat, cdr(subject), env);
        }

        // or pattern: (or p1 p2 ...)
        if (op && op->tag == T_SYM && strcmp(op->s, "or") == 0) {
            Value* alts = cdr(pattern);
            while (!is_nil(alts)) {
                if (match_pattern(car(alts), subject, env)) return 1;
                alts = cdr(alts);
            }
            return 0;
        }

        // and pattern: (and p1 p2 ...)
        if (op && op->tag == T_SYM && strcmp(op->s, "and") == 0) {
            Value* conjs = cdr(pattern);
            while (!is_nil(conjs)) {
                if (!match_pattern(car(conjs), subject, env)) return 0;
                conjs = cdr(conjs);
            }
            return 1;
        }

        // Constructor pattern: (Some v) or (Point x y)
        if (op && op->tag == T_SYM) {
            // Check if subject has same constructor
            if (subject && subject->tag == T_CELL &&
                car(subject) && car(subject)->tag == T_SYM &&
                strcmp(car(subject)->s, op->s) == 0) {
                return match_list_pattern(cdr(pattern), cdr(subject), env);
            }
            return 0;
        }
    }

    return 0;
}

static int match_list_pattern(Value* patterns, Value* items, Env* env) {
    while (!is_nil(patterns) && !is_nil(items)) {
        Value* pat = car(patterns);

        // Rest pattern: .. name
        if (pat && pat->tag == T_SYM && strcmp(pat->s, "..") == 0) {
            patterns = cdr(patterns);
            if (!is_nil(patterns)) {
                env_define(env, car(patterns), items);
            }
            return 1;
        }

        if (!match_pattern(pat, car(items), env)) return 0;

        patterns = cdr(patterns);
        items = cdr(items);
    }

    return is_nil(patterns) && is_nil(items);
}

// cond expands to chained match: each [test result] matches test against true
static Value* eval_cond(Value* args, Env* env) {
    while (!is_nil(args)) {
        Value* clause = car(args);

        // Handle array wrapper
        if (clause && clause->tag == T_CELL && car(clause) &&
            car(clause)->tag == T_SYM && strcmp(car(clause)->s, "array") == 0) {
            clause = cdr(clause);
        }

        Value* test = car(clause);
        Value* result = car(cdr(clause));

        // else clause - matches _ pattern (always succeeds)
        if (test && test->tag == T_SYM && strcmp(test->s, "else") == 0) {
            // _ pattern always matches - just evaluate result
            return omni_eval(result, env);
        }

        // Evaluate test and normalize to bool for pattern matching
        Value* test_result = omni_eval(test, env);
        if (is_error(test_result)) return test_result;
        Value* bool_val = mk_sym(is_truthy(test_result) ? "true" : "false");

        // Match against true pattern
        Env* match_env = env_new(env);
        if (match_pattern(mk_sym("true"), bool_val, match_env)) {
            Value* res = omni_eval(result, match_env);
            env_free(match_env);
            return res;
        }
        env_free(match_env);

        args = cdr(args);
    }

    return mk_nil();
}

static Value* eval_array(Value* args, Env* env) {
    Value* result = mk_nil();
    Value** tail = &result;

    while (!is_nil(args)) {
        Value* val = omni_eval(car(args), env);
        if (is_error(val)) return val;

        Value* cell = mk_cell(val, mk_nil());
        *tail = cell;
        tail = &cell->cell.cdr;

        args = cdr(args);
    }

    // Wrap in array marker
    return mk_cell(mk_sym("__array__"), result);
}

static Value* eval_dict(Value* args, Env* env) {
    Value* result = mk_nil();
    Value** tail = &result;

    while (!is_nil(args)) {
        Value* key = car(args);
        args = cdr(args);
        if (is_nil(args)) break;

        Value* val = omni_eval(car(args), env);
        if (is_error(val)) return val;
        args = cdr(args);

        Value* pair = mk_cell(key, val);
        Value* cell = mk_cell(pair, mk_nil());
        *tail = cell;
        tail = &cell->cell.cdr;
    }

    return mk_cell(mk_sym("__dict__"), result);
}

static Value* eval_for(Value* args, Env* env) {
    // (for [x (range 1 10)] expr)
    Value* bindings = car(args);
    Value* body = car(cdr(args));

    // Handle array wrapper
    if (bindings && bindings->tag == T_CELL && car(bindings) &&
        car(bindings)->tag == T_SYM && strcmp(car(bindings)->s, "array") == 0) {
        bindings = cdr(bindings);
    }

    // Extract name and iterator
    Value* name = car(bindings);
    Value* iter_expr = car(cdr(bindings));

    Value* iter = omni_eval(iter_expr, env);
    if (is_error(iter)) return iter;

    Value* results = mk_nil();
    Value** tail = &results;

    // Iterate
    while (!is_nil(iter)) {
        Env* iter_env = env_new(env);
        env_define(iter_env, name, car(iter));

        Value* val = omni_eval(body, iter_env);
        if (is_error(val)) return val;

        Value* cell = mk_cell(val, mk_nil());
        *tail = cell;
        tail = &cell->cell.cdr;

        iter = cdr(iter);
        env_free(iter_env);
    }

    return results;
}

static Value* eval_foreach(Value* args, Env* env) {
    // (foreach [x items] body...)
    Value* bindings = car(args);
    Value* body = cdr(args);

    if (bindings && bindings->tag == T_CELL && car(bindings) &&
        car(bindings)->tag == T_SYM && strcmp(car(bindings)->s, "array") == 0) {
        bindings = cdr(bindings);
    }

    Value* name = car(bindings);
    Value* iter_expr = car(cdr(bindings));

    Value* iter = omni_eval(iter_expr, env);
    if (is_error(iter)) return iter;

    while (!is_nil(iter)) {
        Env* iter_env = env_new(env);
        env_define(iter_env, name, car(iter));

        Value* current = body;
        while (!is_nil(current)) {
            Value* result = omni_eval(car(current), iter_env);
            if (is_error(result)) return result;
            current = cdr(current);
        }

        iter = cdr(iter);
        env_free(iter_env);
    }

    return mk_nil();
}

// Quasiquote evaluation
static Value* eval_quasiquote(Value* expr, Env* env) {
    if (!expr || expr->tag != T_CELL) return expr;

    Value* op = car(expr);

    // unquote
    if (op && op->tag == T_SYM && strcmp(op->s, "unquote") == 0) {
        return omni_eval(car(cdr(expr)), env);
    }

    // unquote-splicing at top level
    if (op && op->tag == T_SYM && strcmp(op->s, "unquote-splicing") == 0) {
        return omni_eval(car(cdr(expr)), env);
    }

    // Recursively process list
    Value* result = mk_nil();
    Value** tail = &result;

    while (!is_nil(expr)) {
        Value* item = car(expr);

        // Check for unquote-splicing in element
        if (item && item->tag == T_CELL && car(item) &&
            car(item)->tag == T_SYM && strcmp(car(item)->s, "unquote-splicing") == 0) {
            Value* spliced = omni_eval(car(cdr(item)), env);
            if (is_error(spliced)) return spliced;

            // Append all items from spliced list
            while (!is_nil(spliced)) {
                Value* cell = mk_cell(car(spliced), mk_nil());
                *tail = cell;
                tail = &cell->cell.cdr;
                spliced = cdr(spliced);
            }
        } else {
            Value* processed = eval_quasiquote(item, env);
            Value* cell = mk_cell(processed, mk_nil());
            *tail = cell;
            tail = &cell->cell.cdr;
        }

        expr = cdr(expr);
    }

    return result;
}

// -- Delimited Continuations: prompt/control --

// Continuation wrapper that resumes from a saved point
typedef struct {
    Value* saved_expr;   // Expression that was being evaluated
    Env* saved_env;      // Environment at capture point
    int prompt_level;    // Which prompt to resume to
} CapturedCont;

// eval_prompt: (prompt body...)
// Establishes a delimitation boundary. If control is invoked inside,
// we longjmp back here with the captured continuation.
static Value* eval_prompt(Value* args, Env* env) {
    if (prompt_sp >= MAX_PROMPT_STACK) {
        return mk_error("Prompt stack overflow");
    }

    PromptFrame* frame = &prompt_stack[prompt_sp];
    frame->captured = 0;
    frame->result = NULL;
    frame->cont_var = NULL;
    frame->cont_body = NULL;
    frame->cont_env = NULL;

    int jmp_result = setjmp(frame->jmp);

    if (jmp_result == 0) {
        // Normal execution path
        prompt_sp++;
        Value* result = eval_do(args, env);
        prompt_sp--;
        return result;
    } else {
        // Returned from control via longjmp
        // frame->cont_var, frame->cont_body, frame->cont_env were set by control
        // We need to:
        // 1. Create a continuation function that resumes evaluation
        // 2. Bind it to cont_var
        // 3. Evaluate cont_body

        // The continuation is a lambda that, when called with a value,
        // returns that value as the result of the prompt
        // For simplicity, we create a lambda that captures the value
        Value* cont_fn = mk_lambda(
            mk_cell(mk_sym("__v__"), mk_nil()),  // params: (__v__)
            mk_sym("__v__"),                      // body: __v__ (identity)
            env_to_value(env)
        );

        // Bind continuation to the variable and evaluate body
        Env* control_env = env_new(frame->cont_env);
        env_define(control_env, frame->cont_var, cont_fn);

        Value* result = omni_eval(frame->cont_body, control_env);
        env_free(control_env);

        return result;
    }
}

// eval_control: (control k body)
// Captures the continuation up to the nearest prompt,
// binds it to k, and evaluates body.
static Value* eval_control(Value* args, Env* env) {
    if (prompt_sp <= 0) {
        return mk_error("control without prompt");
    }

    Value* k_var = car(args);
    Value* body = car(cdr(args));

    if (!k_var || k_var->tag != T_SYM) {
        return mk_error("control: first argument must be a symbol");
    }

    // Get the current prompt frame
    PromptFrame* frame = &prompt_stack[prompt_sp - 1];
    frame->captured = 1;
    frame->cont_var = k_var;
    frame->cont_body = body;
    frame->cont_env = env;

    // Pop the prompt and longjmp back
    prompt_sp--;
    longjmp(frame->jmp, 1);

    // Never reached
    return mk_nil();
}

// -- Trampoline primitives --

// prim_bounce: (bounce fn arg1 arg2 ...)
// Creates a bounce thunk for trampolining
static Value* prim_bounce(Value* args) {
    Value* fn = car(args);
    Value* fn_args = cdr(args);
    return mk_bounce(fn, fn_args);
}

// prim_trampoline: (trampoline fn arg1 arg2 ...)
// Calls fn with args, then loops while result is a bounce
static Value* prim_trampoline(Value* args);

// Helper to apply a function (needs access to omni_apply)
static Value* trampoline_apply(Value* fn, Value* args) {
    return omni_apply(fn, args, NULL);
}

static Value* prim_trampoline(Value* args) {
    Value* fn = car(args);
    Value* fn_args = cdr(args);

    // Initial call
    Value* result = trampoline_apply(fn, fn_args);

    // Loop while we get bounces
    while (is_bounce(result)) {
        Value* bounce_fn = result->bounce.fn;
        Value* bounce_args = result->bounce.args;
        result = trampoline_apply(bounce_fn, bounce_args);
    }

    return result;
}

// prim_bounce_p: (bounce? v) - check if value is a bounce
static Value* prim_bounce_p(Value* args) {
    Value* v = car(args);
    return mk_sym(is_bounce(v) ? "true" : "false");
}

// Primitive registration
void register_primitive(const char* name, int arity, PrimitiveFn fn) {
    if (num_primitives >= MAX_PRIMITIVES) return;
    primitives[num_primitives].name = name;
    primitives[num_primitives].arity = arity;
    primitives[num_primitives].fn = fn;
    num_primitives++;
}

// Built-in primitives
static Value* prim_add(Value* args) {
    long result = 0;
    while (!is_nil(args)) {
        Value* v = car(args);
        if (v && v->tag == T_INT) result += v->i;
        args = cdr(args);
    }
    return mk_int(result);
}

static Value* prim_sub(Value* args) {
    if (is_nil(args)) return mk_int(0);
    Value* first = car(args);
    if (!first || first->tag != T_INT) return mk_error("- requires integers");
    long result = first->i;
    args = cdr(args);
    if (is_nil(args)) return mk_int(-result);
    while (!is_nil(args)) {
        Value* v = car(args);
        if (v && v->tag == T_INT) result -= v->i;
        args = cdr(args);
    }
    return mk_int(result);
}

static Value* prim_mul(Value* args) {
    long result = 1;
    while (!is_nil(args)) {
        Value* v = car(args);
        if (v && v->tag == T_INT) result *= v->i;
        args = cdr(args);
    }
    return mk_int(result);
}

static Value* prim_div(Value* args) {
    if (is_nil(args)) return mk_int(1);
    Value* first = car(args);
    if (!first || first->tag != T_INT) return mk_error("/ requires integers");
    long result = first->i;
    args = cdr(args);
    while (!is_nil(args)) {
        Value* v = car(args);
        if (v && v->tag == T_INT && v->i != 0) result /= v->i;
        args = cdr(args);
    }
    return mk_int(result);
}

static Value* prim_mod(Value* args) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (!a || !b || a->tag != T_INT || b->tag != T_INT)
        return mk_error("% requires two integers");
    if (b->i == 0) return mk_error("Division by zero");
    return mk_int(a->i % b->i);
}

static Value* prim_lt(Value* args) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (!a || !b || a->tag != T_INT || b->tag != T_INT)
        return mk_error("< requires two integers");
    return mk_sym(a->i < b->i ? "true" : "false");
}

static Value* prim_gt(Value* args) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (!a || !b || a->tag != T_INT || b->tag != T_INT)
        return mk_error("> requires two integers");
    return mk_sym(a->i > b->i ? "true" : "false");
}

static Value* prim_le(Value* args) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (!a || !b || a->tag != T_INT || b->tag != T_INT)
        return mk_error("<= requires two integers");
    return mk_sym(a->i <= b->i ? "true" : "false");
}

static Value* prim_ge(Value* args) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (!a || !b || a->tag != T_INT || b->tag != T_INT)
        return mk_error(">= requires two integers");
    return mk_sym(a->i >= b->i ? "true" : "false");
}

static Value* prim_eq(Value* args) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (!a || !b) return mk_sym("false");
    if (a->tag != b->tag) return mk_sym("false");
    switch (a->tag) {
        case T_INT: return mk_sym(a->i == b->i ? "true" : "false");
        case T_SYM:
        case T_CODE: return mk_sym(strcmp(a->s, b->s) == 0 ? "true" : "false");
        case T_NIL: return mk_sym("true");
        default: return mk_sym(a == b ? "true" : "false");
    }
}

static Value* prim_cons(Value* args) {
    Value* h = car(args);
    Value* t = car(cdr(args));
    return mk_cell(h, t);
}

static Value* prim_car(Value* args) {
    Value* lst = car(args);
    return car(lst);
}

static Value* prim_cdr(Value* args) {
    Value* lst = car(args);
    return cdr(lst);
}

static Value* prim_null(Value* args) {
    Value* v = car(args);
    return mk_sym(is_nil(v) ? "true" : "false");
}

static Value* prim_list(Value* args) {
    return args;
}

static Value* prim_print(Value* args) {
    while (!is_nil(args)) {
        char* s = val_to_str(car(args));
        printf("%s", s);
        free(s);
        args = cdr(args);
    }
    return mk_nil();
}

static Value* prim_println(Value* args) {
    prim_print(args);
    printf("\n");
    return mk_nil();
}

static Value* prim_range(Value* args) {
    long start = 0;
    long end = 0;
    long step = 1;

    Value* first = car(args);
    Value* second = car(cdr(args));
    Value* third = car(cdr(cdr(args)));

    if (first && first->tag == T_INT) {
        if (second && second->tag == T_INT) {
            start = first->i;
            end = second->i;
            if (third && third->tag == T_INT) {
                step = third->i;
            }
        } else {
            end = first->i;
        }
    }

    Value* result = mk_nil();
    Value** tail = &result;

    if (step > 0) {
        for (long i = start; i < end; i += step) {
            Value* cell = mk_cell(mk_int(i), mk_nil());
            *tail = cell;
            tail = &cell->cell.cdr;
        }
    } else if (step < 0) {
        for (long i = start; i > end; i += step) {
            Value* cell = mk_cell(mk_int(i), mk_nil());
            *tail = cell;
            tail = &cell->cell.cdr;
        }
    }

    return result;
}

static Value* prim_reduce(Value* args) {
    Value* fn = car(args);
    Value* init = car(cdr(args));
    Value* lst = car(cdr(cdr(args)));

    Value* acc = init;
    while (!is_nil(lst)) {
        Value* call_args = mk_cell(acc, mk_cell(car(lst), mk_nil()));
        acc = omni_apply(fn, call_args, NULL);
        if (is_error(acc)) return acc;
        lst = cdr(lst);
    }

    return acc;
}

static Value* prim_map(Value* args) {
    Value* fn = car(args);
    Value* lst = car(cdr(args));

    Value* result = mk_nil();
    Value** tail = &result;

    while (!is_nil(lst)) {
        Value* call_args = mk_cell(car(lst), mk_nil());
        Value* val = omni_apply(fn, call_args, NULL);
        if (is_error(val)) return val;

        Value* cell = mk_cell(val, mk_nil());
        *tail = cell;
        tail = &cell->cell.cdr;

        lst = cdr(lst);
    }

    return result;
}

static Value* prim_filter(Value* args) {
    Value* fn = car(args);
    Value* lst = car(cdr(args));

    Value* result = mk_nil();
    Value** tail = &result;

    while (!is_nil(lst)) {
        Value* call_args = mk_cell(car(lst), mk_nil());
        Value* pred_result = omni_apply(fn, call_args, NULL);
        if (is_error(pred_result)) return pred_result;

        if (is_truthy(pred_result)) {
            Value* cell = mk_cell(car(lst), mk_nil());
            *tail = cell;
            tail = &cell->cell.cdr;
        }

        lst = cdr(lst);
    }

    return result;
}

static Value* prim_length(Value* args) {
    Value* lst = car(args);
    long len = 0;
    while (!is_nil(lst)) {
        len++;
        lst = cdr(lst);
    }
    return mk_int(len);
}

static Value* prim_not(Value* args) {
    return mk_sym(is_truthy(car(args)) ? "false" : "true");
}

static Value* prim_identity(Value* args) {
    return car(args);
}

static Value* prim_and(Value* args) {
    while (!is_nil(args)) {
        if (!is_truthy(car(args))) return mk_sym("false");
        args = cdr(args);
    }
    return mk_sym("true");
}

static Value* prim_or(Value* args) {
    while (!is_nil(args)) {
        Value* v = car(args);
        if (is_truthy(v)) return v;
        args = cdr(args);
    }
    return mk_sym("false");
}

// Initialize environment with builtins
Env* omni_env_init(void) {
    if (global_env) return global_env;

    global_env = env_new(NULL);

    // Register primitives
    register_primitive("+", -1, prim_add);
    register_primitive("-", -1, prim_sub);
    register_primitive("*", -1, prim_mul);
    register_primitive("/", -1, prim_div);
    register_primitive("%", 2, prim_mod);
    register_primitive("<", 2, prim_lt);
    register_primitive(">", 2, prim_gt);
    register_primitive("<=", 2, prim_le);
    register_primitive(">=", 2, prim_ge);
    register_primitive("=", 2, prim_eq);
    register_primitive("cons", 2, prim_cons);
    register_primitive("car", 1, prim_car);
    register_primitive("cdr", 1, prim_cdr);
    register_primitive("null?", 1, prim_null);
    register_primitive("list", -1, prim_list);
    register_primitive("print", -1, prim_print);
    register_primitive("println", -1, prim_println);
    register_primitive("range", -1, prim_range);
    register_primitive("reduce", 3, prim_reduce);
    register_primitive("map", 2, prim_map);
    register_primitive("filter", 2, prim_filter);
    register_primitive("length", 1, prim_length);
    register_primitive("not", 1, prim_not);
    register_primitive("identity", 1, prim_identity);
    register_primitive("and", -1, prim_and);
    register_primitive("or", -1, prim_or);

    // Trampoline primitives
    register_primitive("bounce", -1, prim_bounce);
    register_primitive("trampoline", -1, prim_trampoline);
    register_primitive("bounce?", 1, prim_bounce_p);

    // Define primitive functions in environment
    for (int i = 0; i < num_primitives; i++) {
        // Create wrapper lambda for primitives
        // This is a hack - proper implementation would have a different mechanism
        Value* prim_val = mk_prim(NULL);
        // Store index in prim for lookup
        prim_val->prim = (PrimFn)(intptr_t)i;
        env_define(global_env, mk_sym(primitives[i].name), prim_val);
    }

    return global_env;
}

// Override apply to handle primitives
Value* omni_apply(Value* fn, Value* args, Env* env) {
    if (!fn) return mk_error("Cannot apply NULL");

    // Primitive application
    if (fn->tag == T_PRIM) {
        intptr_t idx = (intptr_t)fn->prim;
        if (idx >= 0 && idx < num_primitives) {
            return primitives[idx].fn(args);
        }
        return mk_error("Invalid primitive index");
    }

    // Lambda application
    if (fn->tag == T_LAMBDA) {
        // Convert captured environment from Value* to Env*
        Env* closure_env = NULL;
        if (fn->lam.env && !is_nil(fn->lam.env)) {
            closure_env = value_to_env(fn->lam.env, global_env);
        } else {
            closure_env = global_env;
        }
        Env* call_env = env_extend(closure_env, fn->lam.params, args);
        if (!call_env) return mk_error("OOM in apply");
        return omni_eval(fn->lam.body, call_env);
    }

    return mk_error("Not a function");
}

// Convenience evaluation functions
Value* omni_eval_string(const char* input) {
    Env* env = omni_env_init();
    Value* expr = omni_parse(input);
    if (is_error(expr)) return expr;
    return omni_eval(expr, env);
}

Value* omni_eval_file(const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return mk_error("Cannot open file");

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return mk_error("OOM");
    }

    size_t bytes_read = fread(content, 1, size, f);
    content[bytes_read] = '\0';
    fclose(f);

    Env* env = omni_env_init();
    Value* exprs = omni_parse_all(content);
    free(content);

    if (is_error(exprs)) return exprs;

    Value* result = mk_nil();
    while (!is_nil(exprs)) {
        result = omni_eval(car(exprs), env);
        if (is_error(result)) return result;
        exprs = cdr(exprs);
    }

    return result;
}
