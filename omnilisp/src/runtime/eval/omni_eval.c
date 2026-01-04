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
static Value* eval_handler_case(Value* args, Env* env);
static Value* eval_handler_bind(Value* args, Env* env);
static Value* eval_restart_case(Value* args, Env* env);
static Value* eval_signal(Value* args, Env* env);
static Value* eval_invoke_restart(Value* args, Env* env);
static Value* eval_find_restart(Value* args, Env* env);
// Effect system (algebraic effects)
static Value* eval_defeffect(Value* args, Env* env);
static Value* eval_handle(Value* args, Env* env);
static Value* eval_perform(Value* args, Env* env);
static Value* eval_resume(Value* args, Env* env);
// Restart compatibility via effects
static Value* eval_with_restarts(Value* args, Env* env);
static Value* eval_call_restart(Value* args, Env* env);
// Effect debugging
static Value* eval_effect_trace(Value* args, Env* env);
static Value* eval_effect_stack(Value* args, Env* env);
// Call stack debugging
static Value* eval_call_stack(Value* args, Env* env);
static Value* eval_stack_trace(Value* args, Env* env);

// -- Logical Call Stack for Debugging --
#define MAX_CALL_STACK 256

typedef struct CallFrame {
    const char* name;       // Function/form name
    const char* source;     // Source file (if available)
    int line;               // Line number (if available)
} CallFrame;

static CallFrame call_stack[MAX_CALL_STACK];
static int call_sp = 0;

static void call_stack_push(const char* name, const char* source, int line) {
    if (call_sp < MAX_CALL_STACK) {
        call_stack[call_sp].name = name;
        call_stack[call_sp].source = source;
        call_stack[call_sp].line = line;
        call_sp++;
    }
}

static void call_stack_pop(void) {
    if (call_sp > 0) call_sp--;
}

static void call_stack_print(void) {
    fprintf(stderr, "Call stack (%d frames):\n", call_sp);
    for (int i = call_sp - 1; i >= 0; i--) {
        CallFrame* f = &call_stack[i];
        fprintf(stderr, "  [%d] %s", call_sp - 1 - i, f->name ? f->name : "<anonymous>");
        if (f->source && f->line > 0) {
            fprintf(stderr, " at %s:%d", f->source, f->line);
        }
        fprintf(stderr, "\n");
    }
}

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

/* ============================================================
 * Symbol Suggestions ("did you mean?")
 * ============================================================ */

// Levenshtein edit distance between two strings
static int edit_distance(const char* s1, const char* s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);

    // Use stack for small strings, or limit comparison
    if (len1 > 50 || len2 > 50) return 100;  // Too long, don't bother

    // Create DP table on stack
    int dp[51][51];

    for (int i = 0; i <= len1; i++) dp[i][0] = i;
    for (int j = 0; j <= len2; j++) dp[0][j] = j;

    for (int i = 1; i <= len1; i++) {
        for (int j = 1; j <= len2; j++) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;
            int del = dp[i-1][j] + 1;
            int ins = dp[i][j-1] + 1;
            int sub = dp[i-1][j-1] + cost;
            dp[i][j] = del < ins ? (del < sub ? del : sub) : (ins < sub ? ins : sub);
        }
    }

    return dp[len1][len2];
}

// Find similar symbols and append suggestions to buffer
// Returns number of suggestions added
static int find_similar_symbols(const char* name, Env* env, char* buf, int buf_size) {
    #define MAX_SUGGESTIONS 3
    const char* suggestions[MAX_SUGGESTIONS];
    int distances[MAX_SUGGESTIONS];
    int num_suggestions = 0;

    // Initialize with high distances
    for (int i = 0; i < MAX_SUGGESTIONS; i++) {
        distances[i] = 999;
        suggestions[i] = NULL;
    }

    // Helper to check if a suggestion already exists
    #define IS_DUPLICATE(sym) ({ \
        int dup = 0; \
        for (int d = 0; d < num_suggestions; d++) { \
            if (suggestions[d] && strcmp(suggestions[d], sym) == 0) { \
                dup = 1; break; \
            } \
        } dup; \
    })

    // Helper to insert a suggestion
    #define INSERT_SUGGESTION(sym, dist) do { \
        if (!IS_DUPLICATE(sym)) { \
            for (int j = 0; j < MAX_SUGGESTIONS; j++) { \
                if (dist < distances[j]) { \
                    for (int k = MAX_SUGGESTIONS - 1; k > j; k--) { \
                        distances[k] = distances[k-1]; \
                        suggestions[k] = suggestions[k-1]; \
                    } \
                    distances[j] = dist; \
                    suggestions[j] = sym; \
                    if (num_suggestions < MAX_SUGGESTIONS) num_suggestions++; \
                    break; \
                } \
            } \
        } \
    } while(0)

    // Check primitives first
    for (int i = 0; i < num_primitives; i++) {
        const char* prim_name = primitives[i].name;
        int dist = edit_distance(name, prim_name);

        // Only consider close matches (distance <= 2)
        if (dist <= 2 && dist > 0) {
            INSERT_SUGGESTION(prim_name, dist);
        }
    }

    // Check environment bindings
    Env* e = env;
    while (e) {
        Value* bindings = e->bindings;
        while (!is_nil(bindings)) {
            Value* pair = car(bindings);
            if (pair && pair->tag == T_CELL) {
                Value* key = car(pair);
                if (key && key->tag == T_SYM) {
                    int dist = edit_distance(name, key->s);
                    if (dist <= 2 && dist > 0) {
                        INSERT_SUGGESTION(key->s, dist);
                    }
                }
            }
            bindings = cdr(bindings);
        }
        e = e->parent;
    }

    #undef IS_DUPLICATE
    #undef INSERT_SUGGESTION

    // Format suggestions into buffer (avoiding duplicates in output too)
    if (num_suggestions > 0) {
        int pos = strlen(buf);
        pos += snprintf(buf + pos, buf_size - pos, ". Did you mean: ");
        int printed = 0;
        for (int i = 0; i < MAX_SUGGESTIONS && suggestions[i]; i++) {
            // Check if we already printed this suggestion
            int already_printed = 0;
            for (int p = 0; p < i; p++) {
                if (suggestions[p] && strcmp(suggestions[p], suggestions[i]) == 0) {
                    already_printed = 1;
                    break;
                }
            }
            if (!already_printed) {
                if (printed > 0) pos += snprintf(buf + pos, buf_size - pos, ", ");
                pos += snprintf(buf + pos, buf_size - pos, "'%s'", suggestions[i]);
                printed++;
            }
        }
        snprintf(buf + pos, buf_size - pos, "?");
    }

    return num_suggestions;
    #undef MAX_SUGGESTIONS
}

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
        "handler-case", "handler-bind", "restart-case", "signal",  // Condition system
        "invoke-restart", "find-restart",  // Restart invocation
        "defeffect", "handle", "perform", "resume",  // Effect system (algebraic effects)
        "with-restarts", "call-restart",  // CL-style restart compatibility via effects
        "effect-trace", "effect-stack",  // Effect debugging
        "call-stack", "stack-trace",  // Call stack debugging
        NULL
    };
    for (int i = 0; forms[i]; i++) {
        if (strcmp(sym->s, forms[i]) == 0) return 1;
    }
    return 0;
}

// Main evaluation function
Value* omni_eval(Value* expr, Env* env) {
    if (!expr) return mk_nothing();

    switch (expr->tag) {
        case T_INT:
        case T_CODE:
        case T_PRIM:
        case T_LAMBDA:
        case T_ERROR:
            return expr;

        case T_NIL:
        case T_NOTHING:
            return expr;

        case T_SYM: {
            // Special symbols
            if (strcmp(expr->s, "true") == 0) return mk_sym("true");
            if (strcmp(expr->s, "false") == 0) return mk_sym("false");
            if (strcmp(expr->s, "nothing") == 0) return mk_nothing();

            // Environment lookup
            Value* value = env_lookup(env, expr);
            if (value) return value;

            // Check primitives
            for (int i = 0; i < num_primitives; i++) {
                if (strcmp(primitives[i].name, expr->s) == 0) {
                    return mk_prim(NULL); // Return marker, actual lookup in apply
                }
            }

            char msg[256];
            snprintf(msg, sizeof(msg), "Undefined symbol: %s", expr->s);
            find_similar_symbols(expr->s, env, msg, sizeof(msg));
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
        return mk_nothing();
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
        return mk_nothing();
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

    // Condition system
    if (strcmp(name, "handler-case") == 0) return eval_handler_case(args, env);
    if (strcmp(name, "handler-bind") == 0) return eval_handler_bind(args, env);
    if (strcmp(name, "restart-case") == 0) return eval_restart_case(args, env);
    if (strcmp(name, "signal") == 0) return eval_signal(args, env);
    if (strcmp(name, "invoke-restart") == 0) return eval_invoke_restart(args, env);
    if (strcmp(name, "find-restart") == 0) return eval_find_restart(args, env);

    // Effect system (algebraic effects)
    if (strcmp(name, "defeffect") == 0) return eval_defeffect(args, env);
    if (strcmp(name, "handle") == 0) return eval_handle(args, env);
    if (strcmp(name, "perform") == 0) return eval_perform(args, env);
    if (strcmp(name, "resume") == 0) return eval_resume(args, env);

    // CL-style restart compatibility via effects
    if (strcmp(name, "with-restarts") == 0) return eval_with_restarts(args, env);
    if (strcmp(name, "call-restart") == 0) return eval_call_restart(args, env);

    // Effect debugging
    if (strcmp(name, "effect-trace") == 0) return eval_effect_trace(args, env);
    if (strcmp(name, "effect-stack") == 0) return eval_effect_stack(args, env);

    // Call stack debugging
    if (strcmp(name, "call-stack") == 0) return eval_call_stack(args, env);
    if (strcmp(name, "stack-trace") == 0) return eval_stack_trace(args, env);

    char msg[128];
    snprintf(msg, sizeof(msg), "Unknown special form: %s", name);
    return mk_error(msg);
}

// Truthiness check (only false and nothing are falsy)
static int is_truthy(Value* v) {
    if (!v || v->tag == T_NOTHING) return 0;
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
    Value* else_branch = is_nil(else_args) ? mk_nothing() : car(else_args);

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
    Value* result = mk_nothing();
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
        return mk_nothing();
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

    // Literal nothing
    if (pattern->tag == T_NOTHING) {
        return subject && subject->tag == T_NOTHING;
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

    return mk_nothing();
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

    return mk_nothing();
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
        case T_NOTHING: return mk_sym("true");
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

static Value* prim_nothing(Value* args) {
    Value* v = car(args);
    return mk_sym((v && v->tag == T_NOTHING) ? "true" : "false");
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
    return mk_nothing();
}

static Value* prim_println(Value* args) {
    prim_print(args);
    printf("\n");
    return mk_nothing();
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

/* ============================================================
 * Type Introspection Primitives
 * ============================================================ */

/*
 * (type-of x) -> symbol
 *
 * Returns a symbol representing the type of x.
 */
static Value* prim_type_of(Value* args) {
    Value* v = car(args);
    if (!v) return mk_sym("nothing");

    switch (v->tag) {
        case T_INT:     return mk_sym("integer");
        case T_SYM:     return mk_sym("symbol");
        case T_CELL:    return mk_sym("list");
        case T_NIL:     return mk_sym("list");
        case T_NOTHING: return mk_sym("nothing");
        case T_PRIM:    return mk_sym("primitive");
        case T_MENV:    return mk_sym("meta-env");
        case T_CODE:    return mk_sym("code");
        case T_LAMBDA:  return mk_sym("function");
        case T_ERROR:   return mk_sym("error");
        case T_BOX:     return mk_sym("box");
        case T_CONT:    return mk_sym("continuation");
        case T_CHAN:    return mk_sym("channel");
        case T_PROCESS: return mk_sym("process");
        case T_BOUNCE:  return mk_sym("bounce");
        default:        return mk_sym("unknown");
    }
}

/*
 * (describe x) -> string
 *
 * Returns a human-readable description of x.
 */
static Value* prim_describe(Value* args) {
    Value* v = car(args);
    char buffer[512];

    if (!v) {
        snprintf(buffer, sizeof(buffer), "nothing: the unit value");
        return mk_code(buffer);
    }

    switch (v->tag) {
        case T_INT:
            snprintf(buffer, sizeof(buffer),
                     "integer: %ld (type: integer, size: 8 bytes)", v->i);
            break;
        case T_SYM:
            snprintf(buffer, sizeof(buffer),
                     "symbol: %s (interned string)", v->s);
            break;
        case T_CELL: {
            int len = 0;
            Value* p = v;
            while (p && p->tag == T_CELL) { len++; p = cdr(p); }
            snprintf(buffer, sizeof(buffer),
                     "list: proper list with %d elements", len);
            break;
        }
        case T_NIL:
            snprintf(buffer, sizeof(buffer), "list: empty list ()");
            break;
        case T_NOTHING:
            snprintf(buffer, sizeof(buffer),
                     "nothing: the unit/absence value (falsy)");
            break;
        case T_PRIM:
            snprintf(buffer, sizeof(buffer),
                     "primitive: built-in function");
            break;
        case T_LAMBDA:
            snprintf(buffer, sizeof(buffer),
                     "function: user-defined lambda with captured environment");
            break;
        case T_ERROR:
            snprintf(buffer, sizeof(buffer),
                     "error: %s", v->s ? v->s : "<unknown>");
            break;
        case T_BOX:
            snprintf(buffer, sizeof(buffer),
                     "box: mutable reference cell");
            break;
        case T_CONT:
            snprintf(buffer, sizeof(buffer),
                     "continuation: captured control context (tag=%d)", v->cont.tag);
            break;
        case T_CHAN:
            snprintf(buffer, sizeof(buffer),
                     "channel: CSP channel (capacity=%d)", v->chan.capacity);
            break;
        case T_PROCESS:
            snprintf(buffer, sizeof(buffer),
                     "process: green thread (state=%d)", v->proc.state);
            break;
        case T_BOUNCE:
            snprintf(buffer, sizeof(buffer),
                     "bounce: trampoline thunk");
            break;
        case T_MENV:
            snprintf(buffer, sizeof(buffer),
                     "meta-env: meta-level environment");
            break;
        case T_CODE:
            snprintf(buffer, sizeof(buffer),
                     "code: string literal or code fragment");
            break;
        default:
            snprintf(buffer, sizeof(buffer), "unknown type");
            break;
    }

    return mk_code(buffer);
}

/*
 * (methods-of x) -> list of symbols
 *
 * Returns a list of primitives/methods applicable to values of this type.
 */
static Value* prim_methods_of(Value* args) {
    Value* v = car(args);
    Value* result = mk_nil();

    if (!v) {
        // nothing has few methods
        result = mk_cell(mk_sym("nothing?"), result);
        return result;
    }

    switch (v->tag) {
        case T_INT:
            result = mk_cell(mk_sym("type-of"), result);
            result = mk_cell(mk_sym("describe"), result);
            result = mk_cell(mk_sym("+"), result);
            result = mk_cell(mk_sym("-"), result);
            result = mk_cell(mk_sym("*"), result);
            result = mk_cell(mk_sym("/"), result);
            result = mk_cell(mk_sym("%"), result);
            result = mk_cell(mk_sym("<"), result);
            result = mk_cell(mk_sym(">"), result);
            result = mk_cell(mk_sym("="), result);
            break;
        case T_SYM:
            result = mk_cell(mk_sym("type-of"), result);
            result = mk_cell(mk_sym("describe"), result);
            result = mk_cell(mk_sym("="), result);
            break;
        case T_CELL:
        case T_NIL:
            result = mk_cell(mk_sym("type-of"), result);
            result = mk_cell(mk_sym("describe"), result);
            result = mk_cell(mk_sym("car"), result);
            result = mk_cell(mk_sym("cdr"), result);
            result = mk_cell(mk_sym("cons"), result);
            result = mk_cell(mk_sym("null?"), result);
            result = mk_cell(mk_sym("length"), result);
            result = mk_cell(mk_sym("map"), result);
            result = mk_cell(mk_sym("filter"), result);
            result = mk_cell(mk_sym("reduce"), result);
            break;
        case T_LAMBDA:
        case T_PRIM:
            result = mk_cell(mk_sym("type-of"), result);
            result = mk_cell(mk_sym("describe"), result);
            result = mk_cell(mk_sym("apply"), result);
            break;
        case T_BOX:
            result = mk_cell(mk_sym("type-of"), result);
            result = mk_cell(mk_sym("describe"), result);
            result = mk_cell(mk_sym("box-get"), result);
            result = mk_cell(mk_sym("box-set!"), result);
            break;
        case T_CHAN:
            result = mk_cell(mk_sym("type-of"), result);
            result = mk_cell(mk_sym("describe"), result);
            result = mk_cell(mk_sym("send!"), result);
            result = mk_cell(mk_sym("recv!"), result);
            result = mk_cell(mk_sym("close!"), result);
            break;
        default:
            result = mk_cell(mk_sym("type-of"), result);
            result = mk_cell(mk_sym("describe"), result);
            break;
    }

    return result;
}

/*
 * (type? x type-sym) -> bool
 *
 * Check if x is of the given type.
 */
static Value* prim_type_q(Value* args) {
    Value* v = car(args);
    Value* type_sym = car(cdr(args));

    if (!type_sym || type_sym->tag != T_SYM) {
        return mk_sym("false");
    }

    const char* want = type_sym->s;
    const char* have = NULL;

    if (!v) have = "nothing";
    else switch (v->tag) {
        case T_INT:     have = "integer"; break;
        case T_SYM:     have = "symbol"; break;
        case T_CELL:    have = "list"; break;
        case T_NIL:     have = "list"; break;
        case T_NOTHING: have = "nothing"; break;
        case T_PRIM:    have = "primitive"; break;
        case T_LAMBDA:  have = "function"; break;
        case T_ERROR:   have = "error"; break;
        case T_BOX:     have = "box"; break;
        case T_CONT:    have = "continuation"; break;
        case T_CHAN:    have = "channel"; break;
        case T_PROCESS: have = "process"; break;
        case T_BOUNCE:  have = "bounce"; break;
        default:        have = "unknown"; break;
    }

    return mk_sym(strcmp(want, have) == 0 ? "true" : "false");
}

/* ============================================================
 * Fiber System Implementation
 * ============================================================
 *
 * Cooperative fibers for OmniLisp. Each fiber is a lightweight
 * execution context that can be paused (yield) and resumed.
 *
 * Primitives:
 *   (fiber thunk)  - Create a paused fiber from a thunk
 *   (resume f)     - Step into fiber, run until yield/done
 *   (yield)        - Yield nothing from current fiber
 *   (yield val)    - Yield value from current fiber
 *   (spawn f)      - Run fiber under scheduler
 *   (join f)       - Wait for spawned fiber to finish
 *   (fiber? x)     - Check if x is a fiber
 *   (fiber-done? f)- Check if fiber is done
 */

// Fiber scheduler state
static Value* fiber_run_queue = NULL;     // List of ready fibers
static Value* fiber_current = NULL;       // Currently running fiber
static int fiber_scheduler_running = 0;   // Is scheduler active?

// Forward declaration for eval
static Value* fiber_step(Value* fiber, Value* resume_val, Env* env);

/*
 * (fiber thunk)
 * Create a paused fiber from a thunk (zero-argument function).
 * Returns a fiber that can be resumed.
 */
static Value* prim_fiber(Value* args) {
    Value* thunk = car(args);
    if (!thunk || (thunk->tag != T_LAMBDA && thunk->tag != T_PRIM)) {
        return mk_error("fiber: expected a thunk (function)");
    }
    return mk_process(thunk);
}

/*
 * (resume f)
 * (resume f val)
 * Step into fiber f, resuming execution.
 * If fiber yields, returns the yielded value.
 * If fiber completes, returns the result.
 */
static Value* prim_resume(Value* args) {
    Value* fiber = car(args);
    Value* resume_val = car(cdr(args));
    if (!resume_val) resume_val = mk_nothing();

    if (!fiber || fiber->tag != T_PROCESS) {
        return mk_error("resume: expected a fiber");
    }

    if (fiber->proc.state == PROC_DONE) {
        return fiber->proc.result ? fiber->proc.result : mk_nothing();
    }

    // Use global_env for fiber execution
    return fiber_step(fiber, resume_val, global_env);
}

/*
 * Step a fiber - run until yield or completion.
 * This is the core of fiber execution.
 */
static Value* fiber_step(Value* fiber, Value* resume_val, Env* env) {
    if (fiber->proc.state == PROC_DONE) {
        return fiber->proc.result ? fiber->proc.result : mk_nothing();
    }

    // Save current fiber and set new one
    Value* prev_fiber = fiber_current;
    fiber_current = fiber;
    fiber->proc.state = PROC_RUNNING;
    fiber->proc.park_value = resume_val;

    Value* result;

    if (fiber->proc.cont) {
        // Resume from saved continuation
        // The continuation is a captured T_CONT that expects the resume value
        Value* cont = fiber->proc.cont;
        if (cont && cont->tag == T_CONT && cont->cont.fn) {
            result = cont->cont.fn(resume_val);
        } else {
            result = mk_error("fiber: invalid saved continuation");
        }
    } else {
        // First run - call the thunk
        result = omni_apply(fiber->proc.thunk, mk_nil(), env);
    }

    // Check if fiber yielded (result will be nothing and state is PARKED)
    if (fiber->proc.state == PROC_PARKED) {
        // Fiber yielded - return the yielded value
        result = fiber->proc.park_value ? fiber->proc.park_value : mk_nothing();
        fiber->proc.state = PROC_READY;  // Ready to resume
    } else {
        // Fiber completed
        fiber->proc.state = PROC_DONE;
        fiber->proc.result = result;
        fiber->proc.cont = NULL;
    }

    fiber_current = prev_fiber;
    return result;
}

/*
 * (yield)
 * (yield val)
 * Yield from current fiber, returning val to the resumer.
 */
static Value* prim_yield(Value* args) {
    if (!fiber_current) {
        return mk_error("yield: not inside a fiber");
    }

    Value* yield_val = car(args);
    if (!yield_val) yield_val = mk_nothing();

    // Park the fiber with the yield value
    fiber_current->proc.state = PROC_PARKED;
    fiber_current->proc.park_value = yield_val;

    // Return yield value (will be returned by resume)
    return yield_val;
}

/*
 * (spawn f)
 * Add fiber to the scheduler run queue.
 * Returns the fiber.
 */
static Value* prim_spawn(Value* args) {
    Value* fiber = car(args);
    if (!fiber || fiber->tag != T_PROCESS) {
        return mk_error("spawn: expected a fiber");
    }

    if (fiber->proc.state == PROC_DONE) {
        return fiber;  // Already done, nothing to spawn
    }

    // Add to run queue
    fiber->proc.state = PROC_READY;
    fiber_run_queue = mk_cell(fiber, fiber_run_queue ? fiber_run_queue : mk_nil());

    return fiber;
}

/*
 * (join f)
 * Wait for fiber f to complete and return its result.
 * If fiber is already done, returns result immediately.
 */
static Value* prim_join(Value* args) {
    Value* fiber = car(args);
    if (!fiber || fiber->tag != T_PROCESS) {
        return mk_error("join: expected a fiber");
    }

    // If done, return result
    if (fiber->proc.state == PROC_DONE) {
        return fiber->proc.result ? fiber->proc.result : mk_nothing();
    }

    // Run fiber to completion
    while (fiber->proc.state != PROC_DONE) {
        Value* result = fiber_step(fiber, mk_nothing(), global_env);
        if (is_error(result)) return result;
    }

    return fiber->proc.result ? fiber->proc.result : mk_nothing();
}

/*
 * (run-fibers)
 * Run all spawned fibers until completion.
 * Returns when run queue is empty.
 */
static Value* prim_run_fibers(Value* args) {
    (void)args;

    if (fiber_scheduler_running) {
        return mk_error("run-fibers: scheduler already running");
    }

    fiber_scheduler_running = 1;

    while (fiber_run_queue && !is_nil(fiber_run_queue)) {
        // Pop first fiber from queue
        Value* fiber = car(fiber_run_queue);
        fiber_run_queue = cdr(fiber_run_queue);

        if (!fiber || fiber->tag != T_PROCESS) continue;
        if (fiber->proc.state == PROC_DONE) continue;

        // Step the fiber
        Value* result = fiber_step(fiber, mk_nothing(), global_env);
        if (is_error(result)) {
            fiber_scheduler_running = 0;
            return result;
        }

        // If fiber not done, re-queue it
        if (fiber->proc.state != PROC_DONE) {
            // Append to end of queue
            if (!fiber_run_queue || is_nil(fiber_run_queue)) {
                fiber_run_queue = mk_cell(fiber, mk_nil());
            } else {
                // Find tail and append
                Value* tail = fiber_run_queue;
                while (cdr(tail) && !is_nil(cdr(tail))) {
                    tail = cdr(tail);
                }
                tail->cell.cdr = mk_cell(fiber, mk_nil());
            }
        }
    }

    fiber_scheduler_running = 0;
    return mk_sym("ok");
}

/*
 * (fiber? x)
 * Returns true if x is a fiber.
 */
static Value* prim_fiber_q(Value* args) {
    Value* v = car(args);
    return mk_sym((v && v->tag == T_PROCESS) ? "true" : "false");
}

/*
 * (fiber-done? f)
 * Returns true if fiber f is done.
 */
static Value* prim_fiber_done_q(Value* args) {
    Value* fiber = car(args);
    if (!fiber || fiber->tag != T_PROCESS) {
        return mk_error("fiber-done?: expected a fiber");
    }
    return mk_sym(fiber->proc.state == PROC_DONE ? "true" : "false");
}

/* ============================================================
 * Channel Implementation
 * ============================================================
 *
 * CSP-style channels for fiber communication.
 *
 * Primitives:
 *   (chan)        - Create unbuffered channel
 *   (chan n)      - Create buffered channel with capacity n
 *   (send ch val) - Send value to channel
 *   (recv ch)     - Receive value from channel
 *   (chan-close ch) - Close channel
 *   (chan? x)     - Check if x is a channel
 */

/*
 * (chan)
 * (chan capacity)
 * Create a channel. Default is unbuffered (capacity 0).
 */
static Value* prim_chan(Value* args) {
    int capacity = 0;
    Value* cap_arg = car(args);
    if (cap_arg && cap_arg->tag == T_INT) {
        capacity = (int)cap_arg->i;
        if (capacity < 0) capacity = 0;
    }
    return mk_chan(capacity);
}

/*
 * (send ch val)
 * Send value to channel.
 * For unbuffered: parks if no receiver waiting.
 * For buffered: parks if buffer full.
 */
static Value* prim_send(Value* args) {
    Value* ch_val = car(args);
    Value* val = car(cdr(args));

    if (!ch_val || ch_val->tag != T_CHAN) {
        return mk_error("send: expected a channel");
    }

    Channel* ch = ch_val->chan.ch;
    if (ch->closed) {
        return mk_error("send: channel is closed");
    }

    // Check for waiting receiver
    if (ch->recv_waiters && !is_nil(ch->recv_waiters)) {
        // Direct handoff to receiver
        Value* waiter = car(ch->recv_waiters);
        ch->recv_waiters = cdr(ch->recv_waiters);

        if (waiter && waiter->tag == T_PROCESS) {
            waiter->proc.park_value = val;
            waiter->proc.state = PROC_READY;
            // Re-queue the receiver
            if (fiber_run_queue && !is_nil(fiber_run_queue)) {
                Value* tail = fiber_run_queue;
                while (cdr(tail) && !is_nil(cdr(tail))) tail = cdr(tail);
                tail->cell.cdr = mk_cell(waiter, mk_nil());
            } else {
                fiber_run_queue = mk_cell(waiter, mk_nil());
            }
        }
        return mk_sym("ok");
    }

    // Buffered channel with space
    if (ch->capacity > 0 && ch->count < ch->capacity) {
        ch->buffer[ch->tail] = val;
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        return mk_sym("ok");
    }

    // Must wait - park current fiber
    if (fiber_current) {
        fiber_current->proc.state = PROC_PARKED;
        fiber_current->proc.park_value = val;
        // Add to send waiters as (fiber . value) pair
        Value* waiter = mk_cell(fiber_current, val);
        if (!ch->send_waiters) ch->send_waiters = mk_nil();
        ch->send_waiters = mk_cell(waiter, ch->send_waiters);
        return mk_nothing();  // Will be resumed when receiver arrives
    }

    return mk_error("send: would block (no receiver, not in fiber context)");
}

/*
 * (recv ch)
 * Receive value from channel.
 * Parks if no value available.
 */
static Value* prim_recv(Value* args) {
    Value* ch_val = car(args);

    if (!ch_val || ch_val->tag != T_CHAN) {
        return mk_error("recv: expected a channel");
    }

    Channel* ch = ch_val->chan.ch;

    // Check for waiting sender
    if (ch->send_waiters && !is_nil(ch->send_waiters)) {
        Value* waiter_pair = car(ch->send_waiters);
        ch->send_waiters = cdr(ch->send_waiters);

        Value* waiter = car(waiter_pair);
        Value* val = cdr(waiter_pair);

        if (waiter && waiter->tag == T_PROCESS) {
            waiter->proc.state = PROC_READY;
            // Re-queue the sender
            if (fiber_run_queue && !is_nil(fiber_run_queue)) {
                Value* tail = fiber_run_queue;
                while (cdr(tail) && !is_nil(cdr(tail))) tail = cdr(tail);
                tail->cell.cdr = mk_cell(waiter, mk_nil());
            } else {
                fiber_run_queue = mk_cell(waiter, mk_nil());
            }
        }
        return val;
    }

    // Buffered channel with data
    if (ch->capacity > 0 && ch->count > 0) {
        Value* val = ch->buffer[ch->head];
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;
        return val;
    }

    // Channel closed with no data
    if (ch->closed) {
        return mk_nothing();
    }

    // Must wait - park current fiber
    if (fiber_current) {
        fiber_current->proc.state = PROC_PARKED;
        if (!ch->recv_waiters) ch->recv_waiters = mk_nil();
        ch->recv_waiters = mk_cell(fiber_current, ch->recv_waiters);
        return mk_nothing();  // Will be resumed when sender arrives
    }

    return mk_error("recv: would block (no sender, not in fiber context)");
}

/*
 * (chan-close ch)
 * Close a channel.
 */
static Value* prim_chan_close(Value* args) {
    Value* ch_val = car(args);

    if (!ch_val || ch_val->tag != T_CHAN) {
        return mk_error("chan-close: expected a channel");
    }

    Channel* ch = ch_val->chan.ch;
    ch->closed = 1;

    // Wake up all waiters
    while (ch->recv_waiters && !is_nil(ch->recv_waiters)) {
        Value* waiter = car(ch->recv_waiters);
        ch->recv_waiters = cdr(ch->recv_waiters);
        if (waiter && waiter->tag == T_PROCESS) {
            waiter->proc.park_value = mk_nothing();
            waiter->proc.state = PROC_READY;
        }
    }

    return mk_sym("ok");
}

/*
 * (chan? x)
 * Returns true if x is a channel.
 */
static Value* prim_chan_q(Value* args) {
    Value* v = car(args);
    return mk_sym((v && v->tag == T_CHAN) ? "true" : "false");
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
    register_primitive("nothing?", 1, prim_nothing);
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

    // Type introspection primitives
    register_primitive("type-of", 1, prim_type_of);
    register_primitive("describe", 1, prim_describe);
    register_primitive("methods-of", 1, prim_methods_of);
    register_primitive("type?", 2, prim_type_q);

    // Fiber primitives
    register_primitive("fiber", 1, prim_fiber);
    register_primitive("resume", -1, prim_resume);
    register_primitive("yield", -1, prim_yield);
    register_primitive("spawn", 1, prim_spawn);
    register_primitive("join", 1, prim_join);
    register_primitive("run-fibers", 0, prim_run_fibers);
    register_primitive("fiber?", 1, prim_fiber_q);
    register_primitive("fiber-done?", 1, prim_fiber_done_q);

    // Channel primitives
    register_primitive("chan", -1, prim_chan);
    register_primitive("send", 2, prim_send);
    register_primitive("recv", 1, prim_recv);
    register_primitive("chan-close", 1, prim_chan_close);
    register_primitive("chan?", 1, prim_chan_q);

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
            // Track primitive calls
            call_stack_push(primitives[idx].name, NULL, 0);
            Value* result = primitives[idx].fn(args);
            call_stack_pop();
            return result;
        }
        return mk_error("Invalid primitive index");
    }

    // Lambda application
    if (fn->tag == T_LAMBDA) {
        // Track lambda calls
        call_stack_push("<lambda>", NULL, 0);

        // Convert captured environment from Value* to Env*
        Env* closure_env = NULL;
        if (fn->lam.env && !is_nil(fn->lam.env)) {
            closure_env = value_to_env(fn->lam.env, global_env);
        } else {
            closure_env = global_env;
        }
        Env* call_env = env_extend(closure_env, fn->lam.params, args);
        if (!call_env) {
            call_stack_pop();
            return mk_error("OOM in apply");
        }
        Value* result = omni_eval(fn->lam.body, call_env);
        call_stack_pop();
        return result;
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

/* ============================================================
 * Condition System Implementation
 * ============================================================
 *
 * Syntax:
 *   (handler-case expr
 *     (:error (e) handler-body)
 *     (:type-error (e) handler-body))
 *
 *   (handler-bind ((:error handler-fn))
 *     body)
 *
 *   (restart-case expr
 *     (use-value (v) v)
 *     (abort () nil))
 *
 *   (signal condition)
 *   (invoke-restart name &optional value)
 *   (find-restart name)
 */

// Handler stack for condition handling
#define MAX_HANDLER_STACK 64

typedef struct HandlerFrame {
    jmp_buf jmp;
    Value* handlers;       // List of (type . handler-fn) pairs
    Value* result;         // Result from handler
    int handled;           // Was condition handled?
    struct HandlerFrame* parent;
} HandlerFrame;

static HandlerFrame* handler_stack = NULL;

// Restart stack for restart-case
typedef struct RestartFrame {
    jmp_buf jmp;
    Value* restarts;       // List of (name params . body) entries
    Value* result;         // Result from restart
    int invoked;           // Was restart invoked?
    Env* env;              // Environment for restart bodies
    struct RestartFrame* parent;
} RestartFrame;

static RestartFrame* restart_stack = NULL;

// Push a handler frame
static HandlerFrame* push_handler_frame(Value* handlers) {
    HandlerFrame* frame = malloc(sizeof(HandlerFrame));
    if (!frame) return NULL;
    frame->handlers = handlers;
    frame->result = NULL;
    frame->handled = 0;
    frame->parent = handler_stack;
    handler_stack = frame;
    return frame;
}

// Pop a handler frame
static void pop_handler_frame(void) {
    if (handler_stack) {
        HandlerFrame* old = handler_stack;
        handler_stack = handler_stack->parent;
        free(old);
    }
}

// Push a restart frame
static RestartFrame* push_restart_frame(Value* restarts, Env* env) {
    RestartFrame* frame = malloc(sizeof(RestartFrame));
    if (!frame) return NULL;
    frame->restarts = restarts;
    frame->result = NULL;
    frame->invoked = 0;
    frame->env = env;
    frame->parent = restart_stack;
    restart_stack = frame;
    return frame;
}

// Pop a restart frame
static void pop_restart_frame(void) {
    if (restart_stack) {
        RestartFrame* old = restart_stack;
        restart_stack = restart_stack->parent;
        free(old);
    }
}

// Find a handler for a condition type
static Value* find_handler(const char* condition_type) {
    HandlerFrame* frame = handler_stack;
    while (frame) {
        Value* handlers = frame->handlers;
        while (!is_nil(handlers)) {
            Value* handler = car(handlers);
            if (handler && handler->tag == T_CELL) {
                Value* type = car(handler);
                if (type && type->tag == T_SYM) {
                    // Check for exact match or :error catching all errors
                    if (strcmp(type->s, condition_type) == 0) {
                        return cdr(handler);  // Return handler function
                    }
                    // :error catches all error subtypes
                    if (strcmp(type->s, ":error") == 0 &&
                        strstr(condition_type, "error") != NULL) {
                        return cdr(handler);
                    }
                }
            }
            handlers = cdr(handlers);
        }
        frame = frame->parent;
    }
    return NULL;
}

// Find a restart by name
static RestartFrame* find_restart_frame(const char* name) {
    RestartFrame* frame = restart_stack;
    while (frame) {
        Value* restarts = frame->restarts;
        while (!is_nil(restarts)) {
            Value* restart = car(restarts);
            if (restart && restart->tag == T_CELL) {
                Value* rname = car(restart);
                if (rname && rname->tag == T_SYM &&
                    strcmp(rname->s, name) == 0) {
                    return frame;
                }
            }
            restarts = cdr(restarts);
        }
        frame = frame->parent;
    }
    return NULL;
}

// Get restart definition by name from a frame
static Value* get_restart_def(RestartFrame* frame, const char* name) {
    Value* restarts = frame->restarts;
    while (!is_nil(restarts)) {
        Value* restart = car(restarts);
        if (restart && restart->tag == T_CELL) {
            Value* rname = car(restart);
            if (rname && rname->tag == T_SYM &&
                strcmp(rname->s, name) == 0) {
                return restart;
            }
        }
        restarts = cdr(restarts);
    }
    return NULL;
}

/*
 * (handler-case expr
 *   (:error (e) (print e))
 *   (:type-error (e) (print "type error")))
 */
static Value* eval_handler_case(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("handler-case: missing expression");

    Value* expr = car(args);
    Value* clauses = cdr(args);

    // Build handler list from clauses
    Value* handlers = mk_nil();
    Value* clause = clauses;
    while (!is_nil(clause)) {
        Value* c = car(clause);
        if (c && c->tag == T_CELL) {
            Value* type = car(c);
            Value* rest = cdr(c);
            // rest is ((var) body...)
            if (!is_nil(rest)) {
                Value* params = car(rest);
                Value* body = cdr(rest);
                // Create a lambda for the handler
                Value* handler_lambda = mk_lambda(params, mk_cell(mk_sym("do"), body), env_to_value(env));
                // Add (type . handler) pair
                handlers = mk_cell(mk_cell(type, handler_lambda), handlers);
            }
        }
        clause = cdr(clause);
    }

    // Push handler frame
    HandlerFrame* frame = push_handler_frame(handlers);
    if (!frame) return mk_error("handler-case: out of memory");

    if (setjmp(frame->jmp) == 0) {
        // Normal execution
        Value* result = omni_eval(expr, env);
        pop_handler_frame();

        // Check if result is an error that should be handled
        if (result && result->tag == T_ERROR) {
            const char* error_type = ":error";
            Value* handler = find_handler(error_type);
            if (handler) {
                // Apply handler to the error
                Value* handler_args = mk_cell(result, mk_nil());
                return omni_apply(handler, handler_args, env);
            }
        }
        return result;
    } else {
        // Condition was signaled and we jumped here
        Value* result = frame->result;
        pop_handler_frame();
        return result;
    }
}

/*
 * (handler-bind ((:error (lambda (e) (invoke-restart 'use-value 42))))
 *   body)
 *
 * handler-bind allows handlers to invoke restarts without unwinding
 */
static Value* eval_handler_bind(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("handler-bind: missing bindings");

    Value* bindings = car(args);
    Value* body = cdr(args);

    // Build handler list from bindings
    Value* handlers = mk_nil();
    while (!is_nil(bindings)) {
        Value* binding = car(bindings);
        if (binding && binding->tag == T_CELL) {
            Value* type = car(binding);
            Value* handler_expr = car(cdr(binding));
            Value* handler = omni_eval(handler_expr, env);
            if (is_error(handler)) return handler;
            handlers = mk_cell(mk_cell(type, handler), handlers);
        }
        bindings = cdr(bindings);
    }

    // Push handler frame
    HandlerFrame* frame = push_handler_frame(handlers);
    if (!frame) return mk_error("handler-bind: out of memory");

    if (setjmp(frame->jmp) == 0) {
        // Execute body
        Value* result = eval_do(body, env);
        pop_handler_frame();
        return result;
    } else {
        // Jumped from signal
        Value* result = frame->result;
        pop_handler_frame();
        return result;
    }
}

/*
 * (restart-case expr
 *   (use-value (v) v)
 *   (abort () nil))
 */
static Value* eval_restart_case(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("restart-case: missing expression");

    Value* expr = car(args);
    Value* restarts = cdr(args);

    // Push restart frame
    RestartFrame* frame = push_restart_frame(restarts, env);
    if (!frame) return mk_error("restart-case: out of memory");

    if (setjmp(frame->jmp) == 0) {
        // Normal execution
        Value* result = omni_eval(expr, env);
        pop_restart_frame();
        return result;
    } else {
        // Restart was invoked
        Value* result = frame->result;
        pop_restart_frame();
        return result;
    }
}

/*
 * (signal condition)
 * Signal a condition, looking for handlers
 */
static Value* eval_signal(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("signal: missing condition");

    Value* condition = omni_eval(car(args), env);
    if (is_error(condition)) return condition;

    // Determine condition type
    const char* cond_type = ":error";
    if (condition->tag == T_SYM) {
        cond_type = condition->s;
    } else if (condition->tag == T_CELL) {
        Value* type = car(condition);
        if (type && type->tag == T_SYM) {
            cond_type = type->s;
        }
    }

    // Find a handler
    Value* handler = find_handler(cond_type);
    if (handler) {
        // Call the handler with the condition
        Value* handler_args = mk_cell(condition, mk_nil());
        Value* result = omni_apply(handler, handler_args, env);

        // If we have a handler frame, set the result and jump
        if (handler_stack) {
            handler_stack->result = result;
            handler_stack->handled = 1;
            longjmp(handler_stack->jmp, 1);
        }
        return result;
    }

    // No handler found - return the condition as an error
    if (condition->tag == T_ERROR) {
        return condition;
    }
    char msg[256];
    snprintf(msg, sizeof(msg), "Unhandled condition: %s", cond_type);
    return mk_error(msg);
}

/*
 * (invoke-restart name &optional value)
 */
static Value* eval_invoke_restart(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("invoke-restart: missing restart name");

    Value* name_val = omni_eval(car(args), env);
    if (is_error(name_val)) return name_val;

    const char* name = NULL;
    if (name_val->tag == T_SYM) {
        name = name_val->s;
    } else {
        return mk_error("invoke-restart: name must be a symbol");
    }

    // Get optional value argument
    Value* value = mk_nil();
    if (!is_nil(cdr(args))) {
        value = omni_eval(car(cdr(args)), env);
        if (is_error(value)) return value;
    }

    // Find restart
    RestartFrame* frame = find_restart_frame(name);
    if (!frame) {
        char msg[128];
        snprintf(msg, sizeof(msg), "No restart named '%s' is available", name);
        return mk_error(msg);
    }

    // Get restart definition
    Value* restart_def = get_restart_def(frame, name);
    if (!restart_def) {
        return mk_error("invoke-restart: restart not found");
    }

    // restart_def is (name (params...) body...)
    Value* params = car(cdr(restart_def));
    Value* body = cdr(cdr(restart_def));

    // Create environment with params bound to value
    Env* restart_env = env_new(frame->env);
    if (!is_nil(params)) {
        Value* param = car(params);
        if (param && param->tag == T_SYM) {
            env_define(restart_env, param, value);
        }
    }

    // Evaluate body
    Value* result = eval_do(body, restart_env);
    env_free(restart_env);

    // Set result and jump back to restart-case
    frame->result = result;
    frame->invoked = 1;
    longjmp(frame->jmp, 1);

    // Never reached
    return result;
}

/*
 * (find-restart name) -> restart or nil
 */
static Value* eval_find_restart(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("find-restart: missing name");

    Value* name_val = omni_eval(car(args), env);
    if (is_error(name_val)) return name_val;

    const char* name = NULL;
    if (name_val->tag == T_SYM) {
        name = name_val->s;
    } else {
        return mk_nothing();  // Not a symbol, no restart found
    }

    RestartFrame* frame = find_restart_frame(name);
    if (frame) {
        // Return the restart name as a truthy value
        return name_val;
    }
    return mk_nothing();
}

/* ============================================================
 * Effect System (Algebraic Effects)
 * ============================================================
 *
 * Syntax:
 *   (defeffect name mode)  - Define an effect with recovery mode
 *   mode is one of: :one-shot, :multi-shot, :abort, :tail
 *
 *   (handle body
 *     (effect-name (payload resume) handler-body)
 *     ...)
 *
 *   (perform effect-name payload)
 *
 *   (resume value) - inside handler, continue suspended computation
 *
 * Example:
 *   (defeffect ask :one-shot)
 *   (handle
 *     (+ 1 (perform ask nothing))
 *     (ask (payload _) (resume 42)))
 *   => 43
 */

// Recovery modes for effects
typedef enum {
    RECOVERY_ONE_SHOT = 0,   // Can resume at most once (default)
    RECOVERY_MULTI_SHOT,     // Can resume multiple times
    RECOVERY_TAIL,           // Tail-resumptive (optimized)
    RECOVERY_ABORT           // Never resumes (like exceptions)
} RecoveryModeOmni;

// Effect type definition
typedef struct EffectTypeDef {
    const char* name;
    RecoveryModeOmni mode;
    struct EffectTypeDef* next;
} EffectTypeDef;

// Effect type registry
static EffectTypeDef* effect_type_registry = NULL;

// Register or find an effect type
static EffectTypeDef* register_effect_type(const char* name, RecoveryModeOmni mode) {
    // Check if already exists
    for (EffectTypeDef* t = effect_type_registry; t; t = t->next) {
        if (strcmp(t->name, name) == 0) {
            t->mode = mode;  // Update mode if already registered
            return t;
        }
    }
    // Create new
    EffectTypeDef* t = malloc(sizeof(EffectTypeDef));
    t->name = strdup(name);
    t->mode = mode;
    t->next = effect_type_registry;
    effect_type_registry = t;
    return t;
}

static EffectTypeDef* find_effect_type(const char* name) {
    for (EffectTypeDef* t = effect_type_registry; t; t = t->next) {
        if (strcmp(t->name, name) == 0) {
            return t;
        }
    }
    return NULL;  // Unknown effect - treated as one-shot
}

// Effect handler stack
#define MAX_EFFECT_STACK 64

typedef struct EffectFrame {
    jmp_buf jmp;
    Value* handlers;      // List of (effect-name handler-lambda) pairs
    Value* result;        // Result from handler
    Value* resume_value;  // Value passed to resume
    Value* continuation;  // Captured continuation for resume
    Env* env;             // Environment
    int handled;          // Was an effect handled?
    int resumed;          // Was resume called?
} EffectFrame;

// Current performing effect info (for resume validation)
typedef struct {
    const char* effect_name;
    RecoveryModeOmni mode;
    int resume_count;      // How many times resume has been called
} CurrentPerformInfo;

static CurrentPerformInfo current_perform = { NULL, RECOVERY_ONE_SHOT, 0 };

// Effect trace buffer for debugging
#define MAX_EFFECT_TRACE 256
typedef struct {
    const char* effect_name;
    const char* source_info;  // Optional source location
    int depth;                // Handler stack depth at time of perform
} EffectTraceEntry;

static EffectTraceEntry effect_trace_buffer[MAX_EFFECT_TRACE];
static int effect_trace_count = 0;
static int effect_trace_enabled = 0;

static void record_effect_trace(const char* name, int depth) {
    if (!effect_trace_enabled) return;
    if (effect_trace_count >= MAX_EFFECT_TRACE) {
        // Shift buffer to make room
        memmove(&effect_trace_buffer[0], &effect_trace_buffer[1],
                (MAX_EFFECT_TRACE - 1) * sizeof(EffectTraceEntry));
        effect_trace_count = MAX_EFFECT_TRACE - 1;
    }
    effect_trace_buffer[effect_trace_count].effect_name = name;
    effect_trace_buffer[effect_trace_count].source_info = NULL;
    effect_trace_buffer[effect_trace_count].depth = depth;
    effect_trace_count++;
}

static void clear_effect_trace(void) {
    effect_trace_count = 0;
}

static EffectFrame effect_stack[MAX_EFFECT_STACK];
static int effect_sp = 0;

static EffectFrame* push_effect_frame(Value* handlers, Env* env) {
    if (effect_sp >= MAX_EFFECT_STACK) return NULL;
    EffectFrame* frame = &effect_stack[effect_sp++];
    frame->handlers = handlers;
    frame->result = mk_nothing();
    frame->resume_value = mk_nothing();
    frame->continuation = mk_nothing();
    frame->env = env;
    frame->handled = 0;
    frame->resumed = 0;
    return frame;
}

static void pop_effect_frame(void) {
    if (effect_sp > 0) effect_sp--;
}

static EffectFrame* current_effect_frame(void) {
    return effect_sp > 0 ? &effect_stack[effect_sp - 1] : NULL;
}

// Find handler for an effect name in the stack
static Value* find_effect_handler(const char* effect_name, EffectFrame** out_frame) {
    for (int i = effect_sp - 1; i >= 0; i--) {
        EffectFrame* frame = &effect_stack[i];
        Value* handlers = frame->handlers;
        while (!is_nil(handlers)) {
            Value* pair = car(handlers);
            if (pair && pair->tag == T_CELL) {
                Value* name = car(pair);
                if (name && name->tag == T_SYM && strcmp(name->s, effect_name) == 0) {
                    if (out_frame) *out_frame = frame;
                    return cdr(pair);  // Return the handler lambda
                }
            }
            handlers = cdr(handlers);
        }
    }
    return NULL;
}

// Thread-local current resumption for resume form
static EffectFrame* tl_current_resumption_frame = NULL;

/*
 * (defeffect name mode)
 *
 * Define an effect with a recovery mode.
 * mode is one of: :one-shot, :multi-shot, :abort, :tail
 * Default is :one-shot if not specified.
 *
 * Examples:
 *   (defeffect fail :abort)      ; exception-like, never resumes
 *   (defeffect ask :one-shot)    ; can resume at most once
 *   (defeffect choice :multi-shot) ; can resume multiple times
 */
static Value* eval_defeffect(Value* args, Env* env) {
    (void)env;
    if (is_nil(args)) return mk_error("defeffect: missing effect name");

    Value* name = car(args);
    if (!name || name->tag != T_SYM) {
        return mk_error("defeffect: effect name must be a symbol");
    }

    // Parse mode (default is one-shot)
    RecoveryModeOmni mode = RECOVERY_ONE_SHOT;
    Value* mode_arg = car(cdr(args));

    // Extract mode string - handle both symbol and (quote symbol) forms
    // The reader parses :keyword as (quote keyword)
    const char* mode_str = NULL;
    if (mode_arg) {
        if (mode_arg->tag == T_SYM) {
            mode_str = mode_arg->s;
        } else if (mode_arg->tag == T_CELL) {
            // Check for (quote symbol) form
            Value* qop = car(mode_arg);
            if (qop && qop->tag == T_SYM && strcmp(qop->s, "quote") == 0) {
                Value* quoted = car(cdr(mode_arg));
                if (quoted && quoted->tag == T_SYM) {
                    mode_str = quoted->s;
                }
            }
        }
    }

    if (mode_str) {
        if (strcmp(mode_str, "one-shot") == 0) {
            mode = RECOVERY_ONE_SHOT;
        } else if (strcmp(mode_str, "multi-shot") == 0) {
            mode = RECOVERY_MULTI_SHOT;
        } else if (strcmp(mode_str, "abort") == 0) {
            mode = RECOVERY_ABORT;
        } else if (strcmp(mode_str, "tail") == 0) {
            mode = RECOVERY_TAIL;
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "defeffect: unknown mode '%s'", mode_str);
            return mk_error(msg);
        }
    }

    register_effect_type(name->s, mode);
    return name;  // Return the effect name
}

/*
 * (handle body clauses...)
 *
 * Each clause: (effect-name (payload resume) handler-body...)
 */
static Value* eval_handle(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("handle: missing body");

    Value* body = car(args);
    Value* clauses = cdr(args);

    // Build handler list from clauses
    // Each clause: (effect-name (params...) body...)
    // effect-name is NOT evaluated, it's a literal symbol
    Value* handlers = mk_nil();
    while (!is_nil(clauses)) {
        Value* clause = car(clauses);
        if (clause && clause->tag == T_CELL) {
            Value* effect_name = car(clause);  // Literal symbol, not evaluated
            Value* rest = cdr(clause);
            if (!is_nil(rest)) {
                Value* params = car(rest);  // (payload resume) or (payload _) etc.
                Value* handler_body = cdr(rest);
                // Create handler lambda: (fn (payload resume) body...)
                Value* handler_lambda = mk_lambda(params, mk_cell(mk_sym("do"), handler_body), env_to_value(env));
                // Store as (effect-name . handler-lambda)
                handlers = mk_cell(mk_cell(effect_name, handler_lambda), handlers);
            }
        }
        clauses = cdr(clauses);
    }

    // Push effect frame
    EffectFrame* frame = push_effect_frame(handlers, env);
    if (!frame) return mk_error("handle: stack overflow");

    if (setjmp(frame->jmp) == 0) {
        // Normal execution - evaluate body
        Value* result = omni_eval(body, env);
        pop_effect_frame();
        return result;
    } else {
        // Effect was performed and we jumped here
        if (frame->resumed) {
            // Handler called resume, continue with resume_value
            Value* result = frame->resume_value;
            pop_effect_frame();
            return result;
        } else {
            // Handler returned without resuming
            Value* result = frame->result;
            pop_effect_frame();
            return result;
        }
    }
}

// Per-perform jump buffer for resumption
static jmp_buf perform_jmp;
static Value* perform_resume_value = NULL;
static int perform_resumed = 0;

/*
 * (perform effect-name payload)
 *
 * Performs an effect, transfers control to nearest handler.
 * effect-name is a literal symbol (not evaluated).
 * Returns the value passed to resume, or the handler's return if no resume.
 *
 * Recovery mode validation:
 * - :abort effects cannot be resumed
 * - :one-shot effects can only be resumed once
 * - :multi-shot effects can be resumed multiple times
 */
static Value* eval_perform(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("perform: missing effect name");

    Value* effect_name = car(args);  // Literal symbol, not evaluated
    Value* payload_expr = car(cdr(args));

    // effect-name must be a symbol
    if (!effect_name || effect_name->tag != T_SYM) {
        return mk_error("perform: effect name must be a symbol");
    }

    // Evaluate payload (the data to pass to the handler)
    Value* payload = is_nil(cdr(args)) ? mk_nothing() : omni_eval(payload_expr, env);
    if (is_error(payload)) return payload;

    // Find handler
    EffectFrame* frame = NULL;
    Value* handler = find_effect_handler(effect_name->s, &frame);
    if (!handler) {
        char msg[128];
        snprintf(msg, sizeof(msg), "perform: unhandled effect '%s'", effect_name->s);
        return mk_error(msg);
    }

    // Look up effect type for recovery mode
    EffectTypeDef* effect_type = find_effect_type(effect_name->s);
    RecoveryModeOmni mode = effect_type ? effect_type->mode : RECOVERY_ONE_SHOT;

    // Record in effect trace if enabled
    record_effect_trace(effect_name->s, effect_sp);

    // Set up current perform info for resume validation
    current_perform.effect_name = effect_name->s;
    current_perform.mode = mode;
    current_perform.resume_count = 0;

    // Set up resumption point
    perform_resumed = 0;
    perform_resume_value = NULL;

    if (setjmp(perform_jmp) != 0) {
        // Resume was called - return the resume value
        // This continues the computation after (perform ...)
        current_perform.effect_name = NULL;
        return perform_resume_value;
    }

    // Save current frame as the resumption target
    tl_current_resumption_frame = frame;

    // Create a placeholder for resume (handler uses (resume value) form)
    Value* resume_fn = mk_sym("__resume__");

    // Apply handler: (handler payload resume)
    Value* handler_args = mk_cell(payload, mk_cell(resume_fn, mk_nil()));
    Value* result = omni_apply(handler, handler_args, frame->env);

    tl_current_resumption_frame = NULL;
    current_perform.effect_name = NULL;

    // Handler completed without calling resume
    // Jump back to handle with result
    frame->result = result;
    frame->handled = 1;
    longjmp(frame->jmp, 1);

    // Never reached
    return result;
}

/*
 * (resume value)
 *
 * Inside an effect handler, continues the suspended computation with value.
 * Jumps back to the perform point and returns value as perform's result.
 *
 * Validates recovery mode:
 * - :abort effects: resume is not allowed
 * - :one-shot effects: resume can only be called once
 * - :multi-shot effects: resume can be called multiple times
 */
static Value* eval_resume(Value* args, Env* env) {
    // First check if this is a fiber resume
    if (!is_nil(args)) {
        Value* first = omni_eval(car(args), env);
        if (is_error(first)) return first;

        if (first && first->tag == T_PROCESS) {
            // This is a fiber resume
            Value* resume_val = mk_nothing();
            if (!is_nil(cdr(args))) {
                resume_val = omni_eval(car(cdr(args)), env);
                if (is_error(resume_val)) return resume_val;
            }

            if (first->proc.state == PROC_DONE) {
                return first->proc.result ? first->proc.result : mk_nothing();
            }

            // Step the fiber
            return fiber_step(first, resume_val, env);
        }
    }

    // Effect resume - check context
    if (!tl_current_resumption_frame) {
        return mk_error("resume: not inside an effect handler (or expected a fiber)");
    }

    // Validate recovery mode
    if (current_perform.effect_name) {
        if (current_perform.mode == RECOVERY_ABORT) {
            char msg[128];
            snprintf(msg, sizeof(msg), "resume: cannot resume abortive effect '%s'",
                     current_perform.effect_name);
            return mk_error(msg);
        }

        if (current_perform.mode == RECOVERY_ONE_SHOT && current_perform.resume_count > 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "resume: one-shot effect '%s' already resumed",
                     current_perform.effect_name);
            return mk_error(msg);
        }

        current_perform.resume_count++;
    }

    Value* value = is_nil(args) ? mk_nothing() : omni_eval(car(args), env);
    if (is_error(value)) return value;

    // Store the resume value and jump back to perform
    perform_resume_value = value;
    perform_resumed = 1;
    tl_current_resumption_frame = NULL;

    // Jump back to the perform point
    longjmp(perform_jmp, 1);

    // Never reached
    return value;
}

/* ============================================================
 * CL-Style Restart Compatibility via Effects
 * ============================================================
 *
 * Provides Common Lisp-style restarts implemented using the effect system.
 *
 * Syntax:
 *   (with-restarts ((name (value) result-body) ...)
 *     body)
 *
 * This is equivalent to:
 *   (handle body
 *     (name (payload _) (resume result-body-with-value-bound)))
 *
 * To invoke a restart from a handler:
 *   (call-restart name value)
 *
 * Which is equivalent to:
 *   (perform name value)
 *
 * Example:
 *   (with-restarts
 *     ((use-value (v) v)
 *      (skip () nothing))
 *     (do
 *       (println "about to fail")
 *       (call-restart use-value 42)))
 *   => 42
 */

/*
 * (with-restarts ((name (params) body)...) expr)
 *
 * Establishes restarts using the effect system.
 * Each restart is translated to an effect handler clause.
 */
static Value* eval_with_restarts(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("with-restarts: missing restart clauses");

    Value* restarts = car(args);
    Value* body = cdr(args);

    // Build handler list from restart clauses
    // Each restart: (name (params...) result-body)
    // Becomes: (name (payload _) (resume result-body))
    Value* handlers = mk_nil();
    while (!is_nil(restarts)) {
        Value* restart = car(restarts);
        if (restart && restart->tag == T_CELL) {
            Value* restart_name = car(restart);  // Literal symbol
            Value* rest = cdr(restart);

            if (!is_nil(rest)) {
                Value* params = car(rest);       // (value) or ()
                Value* result_body = cdr(rest);  // Body expressions

                // Create handler: (fn (payload _) (do (let [value payload] result-body...)))
                // Simpler: just bind payload to first param if present
                Value* handler_params = mk_cell(mk_sym("__payload__"), mk_cell(mk_sym("_"), mk_nil()));

                // Build the result body with parameter binding
                Value* bound_body;
                if (!is_nil(params)) {
                    Value* first_param = car(params);
                    // (let [param __payload__] body...)
                    Value* bindings = mk_cell(mk_sym("array"),
                                     mk_cell(first_param,
                                     mk_cell(mk_sym("__payload__"), mk_nil())));
                    bound_body = mk_cell(mk_sym("let"),
                                 mk_cell(bindings, result_body));
                } else {
                    bound_body = mk_cell(mk_sym("do"), result_body);
                }

                // Wrap in (resume ...)
                Value* resume_body = mk_cell(mk_sym("resume"), mk_cell(bound_body, mk_nil()));
                Value* handler_body = mk_cell(resume_body, mk_nil());

                Value* handler_lambda = mk_lambda(handler_params, mk_cell(mk_sym("do"), handler_body), env_to_value(env));
                handlers = mk_cell(mk_cell(restart_name, handler_lambda), handlers);
            }
        }
        restarts = cdr(restarts);
    }

    // Push effect frame and evaluate body
    EffectFrame* frame = push_effect_frame(handlers, env);
    if (!frame) return mk_error("with-restarts: stack overflow");

    if (setjmp(frame->jmp) == 0) {
        // Normal execution - evaluate body
        Value* result = eval_do(body, env);
        pop_effect_frame();
        return result;
    } else {
        // Restart was invoked
        if (frame->resumed) {
            Value* result = frame->resume_value;
            pop_effect_frame();
            return result;
        } else {
            Value* result = frame->result;
            pop_effect_frame();
            return result;
        }
    }
}

/*
 * (call-restart name value)
 *
 * Invokes a restart by name. This is simply perform with restart semantics.
 */
static Value* eval_call_restart(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("call-restart: missing restart name");

    Value* restart_name = car(args);
    Value* value_expr = car(cdr(args));

    // restart-name must be a symbol (not evaluated)
    if (!restart_name || restart_name->tag != T_SYM) {
        return mk_error("call-restart: restart name must be a symbol");
    }

    // Evaluate the value to pass to the restart
    Value* value = is_nil(cdr(args)) ? mk_nothing() : omni_eval(value_expr, env);
    if (is_error(value)) return value;

    // Find handler (restart) in effect stack
    EffectFrame* frame = NULL;
    Value* handler = find_effect_handler(restart_name->s, &frame);
    if (!handler) {
        char msg[128];
        snprintf(msg, sizeof(msg), "call-restart: no restart named '%s'", restart_name->s);
        return mk_error(msg);
    }

    // Record in effect trace if enabled
    record_effect_trace(restart_name->s, effect_sp);

    // Restarts are always one-shot resumable
    current_perform.effect_name = restart_name->s;
    current_perform.mode = RECOVERY_ONE_SHOT;
    current_perform.resume_count = 0;

    // Set up resumption point
    perform_resumed = 0;
    perform_resume_value = NULL;

    if (setjmp(perform_jmp) != 0) {
        current_perform.effect_name = NULL;
        return perform_resume_value;
    }

    tl_current_resumption_frame = frame;

    // Apply handler with value as payload
    Value* handler_args = mk_cell(value, mk_cell(mk_sym("__resume__"), mk_nil()));
    Value* result = omni_apply(handler, handler_args, frame->env);

    tl_current_resumption_frame = NULL;
    current_perform.effect_name = NULL;

    frame->result = result;
    frame->handled = 1;
    longjmp(frame->jmp, 1);

    return result;
}

/* ============================================================
 * Effect Debugging
 * ============================================================ */

/*
 * (effect-trace cmd)
 *
 * Control effect tracing:
 *   (effect-trace :on)     - Enable tracing
 *   (effect-trace :off)    - Disable tracing
 *   (effect-trace :clear)  - Clear the trace buffer
 *   (effect-trace :print)  - Print the trace
 *   (effect-trace)         - Return trace as a list
 */
static Value* eval_effect_trace(Value* args, Env* env) {
    (void)env;

    if (is_nil(args)) {
        // Return trace as a list
        Value* result = mk_nil();
        for (int i = effect_trace_count - 1; i >= 0; i--) {
            EffectTraceEntry* entry = &effect_trace_buffer[i];
            Value* item = mk_cell(
                mk_sym(entry->effect_name ? entry->effect_name : "?"),
                mk_int(entry->depth)
            );
            result = mk_cell(item, result);
        }
        return result;
    }

    Value* cmd = car(args);

    // Handle (quote symbol) form from :keyword syntax
    const char* cmd_str = NULL;
    if (cmd && cmd->tag == T_SYM) {
        cmd_str = cmd->s;
    } else if (cmd && cmd->tag == T_CELL) {
        Value* qop = car(cmd);
        if (qop && qop->tag == T_SYM && strcmp(qop->s, "quote") == 0) {
            Value* quoted = car(cdr(cmd));
            if (quoted && quoted->tag == T_SYM) {
                cmd_str = quoted->s;
            }
        }
    }

    if (cmd_str) {
        if (strcmp(cmd_str, "on") == 0) {
            effect_trace_enabled = 1;
            return mk_sym("tracing-enabled");
        } else if (strcmp(cmd_str, "off") == 0) {
            effect_trace_enabled = 0;
            return mk_sym("tracing-disabled");
        } else if (strcmp(cmd_str, "clear") == 0) {
            clear_effect_trace();
            return mk_sym("trace-cleared");
        } else if (strcmp(cmd_str, "print") == 0) {
            printf("Effect trace (%d entries):\n", effect_trace_count);
            for (int i = 0; i < effect_trace_count; i++) {
                EffectTraceEntry* entry = &effect_trace_buffer[i];
                printf("  [%d] perform %s (depth=%d)\n",
                       i, entry->effect_name ? entry->effect_name : "?", entry->depth);
            }
            return mk_int(effect_trace_count);
        }
    }

    return mk_error("effect-trace: unknown command (use :on, :off, :clear, or :print)");
}

/*
 * (effect-stack)
 *
 * Return the current effect handler stack as a list.
 * Each element is a list of effect names handled at that level.
 */
static Value* eval_effect_stack(Value* args, Env* env) {
    (void)args;
    (void)env;

    Value* result = mk_nil();

    for (int i = effect_sp - 1; i >= 0; i--) {
        EffectFrame* frame = &effect_stack[i];
        Value* handlers_at_level = mk_nil();

        // Collect effect names from this frame
        Value* handlers = frame->handlers;
        while (!is_nil(handlers)) {
            Value* pair = car(handlers);
            if (pair && pair->tag == T_CELL) {
                Value* name = car(pair);
                if (name && name->tag == T_SYM) {
                    handlers_at_level = mk_cell(mk_sym(name->s), handlers_at_level);
                }
            }
            handlers = cdr(handlers);
        }

        result = mk_cell(handlers_at_level, result);
    }

    return result;
}

/* ============================================================
 * Call Stack Debugging
 * ============================================================ */

/*
 * (call-stack cmd)
 *
 * Control call stack operations:
 *   (call-stack)        - Return call stack as a list
 *   (call-stack :print) - Print call stack to stderr
 *   (call-stack :depth) - Return current stack depth
 *   (call-stack :clear) - Clear call stack (for testing)
 */
static Value* eval_call_stack(Value* args, Env* env) {
    (void)env;

    if (is_nil(args)) {
        // Return call stack as a list of symbols
        Value* result = mk_nil();
        for (int i = call_sp - 1; i >= 0; i--) {
            CallFrame* f = &call_stack[i];
            Value* frame_info;
            if (f->source && f->line > 0) {
                // Create (name source line) tuple
                frame_info = mk_cell(
                    mk_sym(f->name ? f->name : "<anonymous>"),
                    mk_cell(mk_sym(f->source),
                    mk_cell(mk_int(f->line), mk_nil()))
                );
            } else {
                // Just the name
                frame_info = mk_sym(f->name ? f->name : "<anonymous>");
            }
            result = mk_cell(frame_info, result);
        }
        return result;
    }

    Value* cmd = car(args);

    // Handle (quote symbol) form from :keyword syntax
    const char* cmd_str = NULL;
    if (cmd && cmd->tag == T_SYM) {
        cmd_str = cmd->s;
    } else if (cmd && cmd->tag == T_CELL) {
        Value* qop = car(cmd);
        if (qop && qop->tag == T_SYM && strcmp(qop->s, "quote") == 0) {
            Value* quoted = car(cdr(cmd));
            if (quoted && quoted->tag == T_SYM) {
                cmd_str = quoted->s;
            }
        }
    }

    if (cmd_str) {
        if (strcmp(cmd_str, "print") == 0) {
            call_stack_print();
            return mk_int(call_sp);
        } else if (strcmp(cmd_str, "depth") == 0) {
            return mk_int(call_sp);
        } else if (strcmp(cmd_str, "clear") == 0) {
            call_sp = 0;
            return mk_sym("stack-cleared");
        }
    }

    return mk_error("call-stack: unknown command (use :print, :depth, or :clear)");
}

/*
 * (stack-trace)
 *
 * Return a formatted stack trace string.
 * Prints to stderr and returns the trace as a string.
 */
static Value* eval_stack_trace(Value* args, Env* env) {
    (void)args;
    (void)env;

    // Build formatted stack trace string
    char buffer[4096];
    int pos = 0;

    pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                    "Stack trace (%d frames):\n", call_sp);

    for (int i = call_sp - 1; i >= 0 && pos < (int)sizeof(buffer) - 100; i--) {
        CallFrame* f = &call_stack[i];
        pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                        "  [%d] %s", call_sp - 1 - i,
                        f->name ? f->name : "<anonymous>");
        if (f->source && f->line > 0) {
            pos += snprintf(buffer + pos, sizeof(buffer) - pos,
                            " at %s:%d", f->source, f->line);
        }
        pos += snprintf(buffer + pos, sizeof(buffer) - pos, "\n");
    }

    // Also print to stderr
    fprintf(stderr, "%s", buffer);

    return mk_code(buffer);
}
