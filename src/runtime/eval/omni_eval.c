#define _POSIX_C_SOURCE 200809L
#include "omni_eval.h"
#include "../reader/omni_reader.h"
#include "../pika/omni_grammar.h"
#include "../util/dstring.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <setjmp.h>
#include <math.h>
#include <dlfcn.h>

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
static Value* eval_put_bang(Value* args, Env* env);
static Value* eval_update_bang(Value* args, Env* env);
static Value* eval_update(Value* args, Env* env);
static Value* eval_path(Value* args, Env* env);
static Value* eval_type(Value* args, Env* env);
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
static Value* eval_try_catch(Value* args, Env* env);
static Value* eval_with_open_file(Value* args, Env* env);

// Helper functions - forward declarations
static int is_array(Value* v);
static int is_dict(Value* v);
static Value* prim_get(Value* args);
static Value* prim_dict_set(Value* args);
static Value* prim_array_set(Value* args);

// Effect type system - forward declarations for (define {effect ...})
typedef enum {
    RECOVERY_ONE_SHOT = 0,   // Can resume at most once (default)
    RECOVERY_MULTI_SHOT,     // Can resume multiple times
    RECOVERY_TAIL,           // Tail-resumptive (optimized)
    RECOVERY_ABORT           // Never resumes (like exceptions)
} RecoveryModeOmni;

typedef struct EffectTypeDef EffectTypeDef;
static EffectTypeDef* register_effect_type_full(const char* name, RecoveryModeOmni mode,
                                                 Value* payload_type, Value* returns_type);
// Restart compatibility via effects
static Value* eval_with_restarts(Value* args, Env* env);
static Value* eval_call_restart(Value* args, Env* env);
// Effect debugging
static Value* eval_effect_trace(Value* args, Env* env);
static Value* eval_effect_stack(Value* args, Env* env);
// Call stack debugging
static Value* eval_call_stack(Value* args, Env* env);
static Value* eval_stack_trace(Value* args, Env* env);
// Fiber scoped construct
static Value* eval_with_fibers(Value* args, Env* env);

// Module system
static Value* eval_module(Value* args, Env* env);
static Value* eval_import(Value* args, Env* env);
static Value* eval_export(Value* args, Env* env);

// Macro expansion (hygienic syntax transformers)
static Value* try_macro_expand(Value* expr, Env* env);
static int syntax_match(Value* pattern, Value* input, Value* literals, Env* bindings);
static Value* syntax_expand(Value* template, Env* bindings, int gensym_counter);

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
 * Module Registry
 * ============================================================
 * Modules provide namespace isolation. Each module has its own
 * environment and an export list of public symbols.
 */
#define MAX_MODULES 64

typedef struct Module {
    char* name;           // Module name (e.g., "math", "io")
    Env* env;             // Module's environment with all definitions
    Value* exports;       // List of exported symbol names
    int loaded;           // Has body been evaluated?
} Module;

static Module module_registry[MAX_MODULES];
static int num_modules = 0;
static Module* current_module = NULL;  // Module being defined (for export)

/* ============================================================
 * Metadata Table
 * ============================================================
 * Associates values with their metadata. Uses a simple linear
 * scan (could be upgraded to hash table for performance).
 */
#define MAX_METADATA_ENTRIES 4096

typedef struct MetadataEntry {
    Value* value;     // The value (key)
    Value* metadata;  // The metadata (value)
} MetadataEntry;

static MetadataEntry metadata_table[MAX_METADATA_ENTRIES];
static int num_metadata_entries = 0;

// Set metadata for a value
static void metadata_set(Value* val, Value* meta) {
    if (!val || !meta) return;

    // Check if already exists
    for (int i = 0; i < num_metadata_entries; i++) {
        if (metadata_table[i].value == val) {
            metadata_table[i].metadata = meta;
            return;
        }
    }

    // Add new entry
    if (num_metadata_entries < MAX_METADATA_ENTRIES) {
        metadata_table[num_metadata_entries].value = val;
        metadata_table[num_metadata_entries].metadata = meta;
        num_metadata_entries++;
    }
}

// Get metadata for a value
static Value* metadata_get(Value* val) {
    if (!val) return mk_nil();

    for (int i = 0; i < num_metadata_entries; i++) {
        if (metadata_table[i].value == val) {
            return metadata_table[i].metadata;
        }
    }
    return mk_nil();
}

// Find module by name
static Module* find_module(const char* name) {
    for (int i = 0; i < num_modules; i++) {
        if (strcmp(module_registry[i].name, name) == 0) {
            return &module_registry[i];
        }
    }
    return NULL;
}

// Register a new module
static Module* register_module(const char* name, Env* parent_env) {
    if (num_modules >= MAX_MODULES) return NULL;
    Module* m = &module_registry[num_modules++];
    m->name = strdup(name);
    m->env = env_new(parent_env);  // Inherit from parent (for primitives)
    m->exports = mk_nil();
    m->loaded = 0;
    return m;
}

// Check if symbol is exported from module
static int is_exported(Module* m, const char* sym_name) {
    Value* exports = m->exports;
    while (!is_nil(exports)) {
        Value* exp = car(exports);
        if (exp && exp->tag == T_SYM && strcmp(exp->s, sym_name) == 0) {
            return 1;
        }
        exports = cdr(exports);
    }
    return 0;
}

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
            return new_env;  // Rest param handled, done
        }

        env_define(new_env, name, value);
        names = cdr(names);
        values = cdr(values);
    }

    // Handle rest parameter when no values remain
    // e.g., (define (f .. args) ...) called as (f)
    if (!is_nil(names)) {
        Value* name = car(names);
        if (name && name->tag == T_SYM && strcmp(name->s, "..") == 0) {
            names = cdr(names);
            if (!is_nil(names)) {
                name = car(names);
                env_define(new_env, name, mk_nil());  // Bind to empty list
            }
        }
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
        "if", "let", "define", "lambda", "fn", "λ", "do", "begin",
        "quote", "set!", "match", "cond", "when", "unless",
        "array", "dict", "type", "for", "foreach", "->",
        "quasiquote", "with-meta", "module", "import", "export",
        "prompt", "control",  // Delimited continuations
        "handler-case", "handler-bind", "restart-case", "signal",  // Condition system
        "invoke-restart", "find-restart",  // Restart invocation
        "try",  // try/catch/finally
        "defeffect", "handle", "perform", "resume",  // Effect system (algebraic effects)
        "with-restarts", "call-restart",  // CL-style restart compatibility via effects
        "effect-trace", "effect-stack",  // Effect debugging
        "call-stack", "stack-trace",  // Call stack debugging
        "with-fibers",  // Fiber scoped construct
        "with-open-file",  // File I/O with auto-close
        "|>", "pipe",  // Pipe/threading operator
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

            // Symbols (with : shorthand) are self-evaluating
            if (expr->s[0] == ':') return expr;

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

            // Special forms (checked first, before macro expansion)
            if (op && op->tag == T_SYM) {
                if (is_special_form(op)) {
                    return eval_special_form(op, args, env);
                }
            }

            // Try macro expansion
            Value* expanded = try_macro_expand(expr, env);
            if (expanded) {
                if (is_error(expanded)) return expanded;
                // Recursively evaluate the expanded form
                return omni_eval(expanded, env);
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

// Helper: substitute placeholder in expression with value
// Supports both "it" and "_" as placeholders
static int is_placeholder(Value* expr) {
    return expr && expr->tag == T_SYM &&
           (strcmp(expr->s, "it") == 0 || strcmp(expr->s, "_") == 0);
}

static Value* subst_placeholder(Value* expr, Value* val) {
    if (!expr) return expr;

    // If it's a placeholder symbol, replace with val
    if (is_placeholder(expr)) {
        return val;
    }

    // If it's a list, recursively substitute
    if (expr->tag == T_CELL) {
        Value* new_car = subst_placeholder(car(expr), val);
        Value* new_cdr = subst_placeholder(cdr(expr), val);
        return mk_cell(new_car, new_cdr);
    }

    // Otherwise return as-is
    return expr;
}

// Check if expression contains placeholder (it or _)
static int has_placeholder(Value* expr) {
    if (!expr) return 0;

    if (is_placeholder(expr)) {
        return 1;
    }

    if (expr->tag == T_CELL) {
        return has_placeholder(car(expr)) || has_placeholder(cdr(expr));
    }

    return 0;
}

/*
 * Pipe operator: (|> value (f arg) (g arg) ...)
 * Threads value through function calls.
 * - By default, inserts value as first argument: (|> x (f a)) -> (f x a)
 * - With 'it' or '_' placeholder, substitutes there: (|> x (f a it)) -> (f a x)
 */
static Value* eval_pipe(Value* args, Env* env) {
    if (is_nil(args)) {
        return mk_error("|>: expected at least one argument");
    }

    // Evaluate the initial value
    Value* val = omni_eval(car(args), env);
    if (is_error(val)) return val;

    // Create a temporary binding for the piped value
    // This avoids re-evaluation issues when substituting
    static int pipe_counter = 0;
    char temp_name[32];
    snprintf(temp_name, sizeof(temp_name), "__pipe_val_%d__", pipe_counter++);
    Value* temp_sym = mk_sym(temp_name);

    // Process each subsequent form
    Value* forms = cdr(args);
    while (!is_nil(forms)) {
        Value* form = car(forms);

        // Bind current value to temp variable
        Env* pipe_env = env_new(env);
        env_define(pipe_env, temp_sym, val);

        // Build the call expression
        Value* call_expr;

        if (form->tag == T_CELL) {
            // It's a function call form
            if (has_placeholder(form)) {
                // Substitute placeholder with temp symbol reference
                call_expr = subst_placeholder(form, temp_sym);
            } else {
                // Insert temp symbol as first argument
                // (f a b) -> (f temp a b)
                Value* fn = car(form);
                Value* orig_args = cdr(form);
                call_expr = mk_cell(fn, mk_cell(temp_sym, orig_args));
            }
        } else if (form->tag == T_SYM) {
            // Just a function name - call it with temp symbol as sole argument
            call_expr = mk_cell(form, mk_cell(temp_sym, mk_nil()));
        } else {
            env_free(pipe_env);
            return mk_error("|>: expected function call or symbol");
        }

        // Evaluate the call in the extended environment
        val = omni_eval(call_expr, pipe_env);
        env_free(pipe_env);

        if (is_error(val)) return val;

        forms = cdr(forms);
    }

    return val;
}

// Special form dispatch
static Value* eval_special_form(Value* op, Value* args, Env* env) {
    const char* name = op->s;

    if (strcmp(name, "if") == 0) return eval_if(args, env);
    if (strcmp(name, "let") == 0) return eval_let(args, env);
    if (strcmp(name, "define") == 0) return eval_define(args, env);
    if (strcmp(name, "lambda") == 0 || strcmp(name, "fn") == 0 || strcmp(name, "λ") == 0) return eval_lambda(args, env);
    if (strcmp(name, "do") == 0 || strcmp(name, "begin") == 0) return eval_do(args, env);
    if (strcmp(name, "quote") == 0) return eval_quote(args, env);
    if (strcmp(name, "set!") == 0) return eval_set(args, env);
    if (strcmp(name, "match") == 0) return eval_match(args, env);
    if (strcmp(name, "cond") == 0) return eval_cond(args, env);
    if (strcmp(name, "array") == 0) return eval_array(args, env);
    if (strcmp(name, "dict") == 0) return eval_dict(args, env);
    if (strcmp(name, "for") == 0) return eval_for(args, env);
    if (strcmp(name, "foreach") == 0) return eval_foreach(args, env);
    if (strcmp(name, "put!") == 0) return eval_put_bang(args, env);
    if (strcmp(name, "update!") == 0) return eval_update_bang(args, env);
    if (strcmp(name, "update") == 0) return eval_update(args, env);
    if (strcmp(name, "path") == 0) return eval_path(args, env);
    if (strcmp(name, "type") == 0) return eval_type(args, env);

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
        // Evaluate the metadata and form, then attach
        Value* meta = omni_eval(car(args), env);
        if (is_error(meta)) return meta;
        Value* form = car(cdr(args));
        Value* result = omni_eval(form, env);
        if (is_error(result)) return result;

        // Attach metadata to the result
        metadata_set(result, meta);
        return result;
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

    // try/catch/finally
    if (strcmp(name, "try") == 0) return eval_try_catch(args, env);

    // with-open-file
    if (strcmp(name, "with-open-file") == 0) return eval_with_open_file(args, env);

    // named-tuple removed - use dicts instead

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

    // Fiber scoped construct
    if (strcmp(name, "with-fibers") == 0) return eval_with_fibers(args, env);

    // Module system
    if (strcmp(name, "module") == 0) return eval_module(args, env);
    if (strcmp(name, "import") == 0) return eval_import(args, env);
    if (strcmp(name, "export") == 0) return eval_export(args, env);

    // Pipe operator: (|> value (f arg) (g arg) ...)
    if (strcmp(name, "|>") == 0) return eval_pipe(args, env);
    if (strcmp(name, "pipe") == 0) return eval_pipe(args, env);  // Alias

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

        // Support destructuring: if name is a pattern, destructure
        // Simple case: name is a symbol - just bind it
        if (name && name->tag == T_SYM) {
            env_define(let_env, name, evaled);
        } else {
            // Pattern matching for destructuring
            if (!match_pattern(name, evaled, let_env)) {
                return mk_error("let: destructuring pattern does not match value");
            }
        }
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

    // Handle metadata: (define ^:meta name value) -> (define (with-meta :meta name) value)
    // Extract metadata and unwrap
    Value* define_metadata = NULL;
    if (first && first->tag == T_CELL) {
        Value* maybe_with_meta = car(first);
        if (maybe_with_meta && maybe_with_meta->tag == T_SYM &&
            strcmp(maybe_with_meta->s, "with-meta") == 0) {
            // Extract: (with-meta meta-expr name)
            Value* meta_expr = car(cdr(first));
            Value* actual_name = car(cdr(cdr(first)));

            // Evaluate the metadata expression
            define_metadata = omni_eval(meta_expr, env);
            if (is_error(define_metadata)) return define_metadata;

            // Replace first with the actual name
            first = actual_name;
        }
    }

    // Function definition: (define (name params...) body)
    if (first && first->tag == T_CELL) {
        Value* head = car(first);

        // Check for syntax definition: (define [syntax name] ...)
        // [syntax name] parses as (array syntax name)
        if (head && head->tag == T_SYM && strcmp(head->s, "array") == 0) {
            Value* arr_contents = cdr(first);
            if (!is_nil(arr_contents) && car(arr_contents) &&
                car(arr_contents)->tag == T_SYM &&
                strcmp(car(arr_contents)->s, "syntax") == 0) {
                // It's a syntax definition!
                Value* name = car(cdr(arr_contents));
                if (!name || name->tag != T_SYM) {
                    return mk_error("define [syntax ...]: expected name");
                }

                // Parse optional #literals and collect rules
                Value* rest = cdr(args);
                Value* literals = mk_nil();
                Value* rules = mk_nil();
                Value** rules_tail = &rules;

                // Check for #literals declaration (symbol starting with #literals)
                // Since we don't have reader macros yet, accept (literals ...) form too
                while (!is_nil(rest)) {
                    Value* item = car(rest);

                    // Check for #literals (...) - but we'll accept a symbol #literals
                    // followed by a list, or (literals ...) form
                    if (item && item->tag == T_SYM &&
                        (strcmp(item->s, "#literals") == 0 ||
                         strcmp(item->s, "literals") == 0)) {
                        // Next item should be the literals list
                        rest = cdr(rest);
                        if (!is_nil(rest)) {
                            Value* lit_list = car(rest);
                            // Handle array wrapper
                            if (lit_list && lit_list->tag == T_CELL &&
                                car(lit_list) && car(lit_list)->tag == T_SYM &&
                                strcmp(car(lit_list)->s, "array") == 0) {
                                literals = cdr(lit_list);
                            } else {
                                literals = lit_list;
                            }
                            rest = cdr(rest);
                        }
                        continue;
                    }

                    // Check for (literals ...) or [literals ...] form
                    if (item && item->tag == T_CELL) {
                        Value* item_head = car(item);
                        if (item_head && item_head->tag == T_SYM) {
                            // Skip array wrapper
                            if (strcmp(item_head->s, "array") == 0) {
                                Value* inner = cdr(item);
                                if (!is_nil(inner) && car(inner) &&
                                    car(inner)->tag == T_SYM &&
                                    strcmp(car(inner)->s, "literals") == 0) {
                                    literals = cdr(inner);
                                    rest = cdr(rest);
                                    continue;
                                }
                            } else if (strcmp(item_head->s, "literals") == 0) {
                                literals = cdr(item);
                                rest = cdr(rest);
                                continue;
                            }
                        }
                    }

                    // Otherwise, it's a rule: (pattern template) or [pattern template]
                    // Each rule is stored as (pattern . (template . nil))
                    Value* rule = item;
                    // Handle array wrapper
                    if (rule && rule->tag == T_CELL && car(rule) &&
                        car(rule)->tag == T_SYM &&
                        strcmp(car(rule)->s, "array") == 0) {
                        rule = cdr(rule);
                    }

                    if (rule && rule->tag == T_CELL) {
                        Value* pattern = car(rule);
                        Value* template = car(cdr(rule));

                        // Store as ((pattern template))
                        Value* rule_pair = mk_cell(pattern, mk_cell(template, mk_nil()));
                        *rules_tail = mk_cell(rule_pair, mk_nil());
                        rules_tail = &((*rules_tail)->cell.cdr);
                    }

                    rest = cdr(rest);
                }

                // Create the syntax transformer
                Value* syntax = mk_syntax(name->s, literals, rules, env_to_value(env));
                env_define(env, name, syntax);
                return name;
            }

            // Check for grammar definition: (define [grammar name] [rule1 clause] [rule2 clause] ...)
            // [grammar name] parses as (array grammar name)
            if (!is_nil(arr_contents) && car(arr_contents) &&
                car(arr_contents)->tag == T_SYM &&
                strcmp(car(arr_contents)->s, "grammar") == 0) {
                // It's a grammar definition!
                Value* name = car(cdr(arr_contents));
                if (!name || name->tag != T_SYM) {
                    return mk_error("define [grammar ...]: expected name");
                }
                
                Value* rules_list = cdr(args);
                char* error = NULL;
                PikaGrammar* g = omni_compile_grammar_from_value(rules_list, &error);
                if (!g) {
                    Value* err = mk_error(error ? error : "Grammar compilation failed");
                    if (error) free(error);
                    return err;
                }
                
                Value* grammar_val = mk_grammar(g, name->s);
                env_define(env, name, grammar_val);
                return name;
            }
        }

        // Check for struct definition: (define {struct Name} [field1] [field2] ...)
        // {struct Name} parses as (type struct Name)
        if (head && head->tag == T_SYM && strcmp(head->s, "type") == 0) {
            Value* type_contents = cdr(first);
            if (!is_nil(type_contents) && car(type_contents) &&
                car(type_contents)->tag == T_SYM &&
                strcmp(car(type_contents)->s, "struct") == 0) {
                // It's a struct definition!
                Value* struct_name = car(cdr(type_contents));
                if (!struct_name || struct_name->tag != T_SYM) {
                    return mk_error("define {struct ...}: expected name");
                }
                const char* name_str = struct_name->s;

                // Collect field names from [field] forms
                Value* fields = mk_nil();
                Value** fields_tail = &fields;
                int field_count = 0;

                Value* rest = cdr(args);
                while (!is_nil(rest)) {
                    Value* field_form = car(rest);
                    // [field] parses as (array field)
                    if (field_form && field_form->tag == T_CELL) {
                        Value* fhead = car(field_form);
                        if (fhead && fhead->tag == T_SYM && strcmp(fhead->s, "array") == 0) {
                            Value* field_name = car(cdr(field_form));
                            if (field_name && field_name->tag == T_SYM) {
                                *fields_tail = mk_cell(field_name, mk_nil());
                                fields_tail = &((*fields_tail)->cell.cdr);
                                field_count++;
                            }
                        }
                    }
                    rest = cdr(rest);
                }

                // Create constructor: (lambda (f1 f2 ...) (dict '__struct__ 'Name 'f1 f1 'f2 f2 ...))
                // Build the dict call
                Value* dict_args = mk_nil();
                Value** dict_tail = &dict_args;

                // Add __struct__ field
                *dict_tail = mk_cell(mk_cell(mk_sym("quote"), mk_cell(mk_sym("__struct__"), mk_nil())), mk_nil());
                dict_tail = &((*dict_tail)->cell.cdr);
                *dict_tail = mk_cell(mk_cell(mk_sym("quote"), mk_cell(struct_name, mk_nil())), mk_nil());
                dict_tail = &((*dict_tail)->cell.cdr);

                // Add fields
                Value* f = fields;
                while (!is_nil(f)) {
                    Value* fname = car(f);
                    // Quoted field name as key
                    *dict_tail = mk_cell(mk_cell(mk_sym("quote"), mk_cell(fname, mk_nil())), mk_nil());
                    dict_tail = &((*dict_tail)->cell.cdr);
                    // Field value (just the symbol, will be parameter)
                    *dict_tail = mk_cell(fname, mk_nil());
                    dict_tail = &((*dict_tail)->cell.cdr);
                    f = cdr(f);
                }

                Value* dict_call = mk_cell(mk_sym("dict"), dict_args);
                Value* constructor = mk_lambda(fields, dict_call, env_to_value(env));
                env_define(env, struct_name, constructor);

                // Create predicate: Name? checks if dict and __struct__ == 'Name
                // (lambda (x) (if (dict? x) (= (get x '__struct__) 'Name) false))
                char pred_name[256];
                snprintf(pred_name, sizeof(pred_name), "%s?", name_str);
                Value* pred_param = mk_sym("__x__");
                Value* dict_check = mk_cell(mk_sym("dict?"), mk_cell(pred_param, mk_nil()));
                Value* get_struct = mk_cell(mk_sym("get"),
                                   mk_cell(pred_param,
                                   mk_cell(mk_cell(mk_sym("quote"),
                                          mk_cell(mk_sym("__struct__"), mk_nil())), mk_nil())));
                Value* quoted_name = mk_cell(mk_sym("quote"), mk_cell(struct_name, mk_nil()));
                Value* eq_check = mk_cell(mk_sym("="),
                                 mk_cell(get_struct,
                                 mk_cell(quoted_name, mk_nil())));
                // (if (dict? x) (= ...) false)
                Value* if_check = mk_cell(mk_sym("if"),
                                 mk_cell(dict_check,
                                 mk_cell(eq_check,
                                 mk_cell(mk_sym("false"), mk_nil()))));
                Value* predicate = mk_lambda(mk_cell(pred_param, mk_nil()), if_check, env_to_value(env));
                env_define(env, mk_sym(pred_name), predicate);

                return struct_name;
            }

            // Check for enum definition: (define {enum Name} Val1 Val2 ...)
            // or with data: (define {enum Option} (Some [value]) None)
            // {enum Name} parses as (type enum Name)
            // {enum Option T} parses as (type enum Option T) with type params
            if (car(type_contents)->tag == T_SYM &&
                strcmp(car(type_contents)->s, "enum") == 0) {
                // It's an enum definition!
                Value* enum_name = car(cdr(type_contents));
                if (!enum_name || enum_name->tag != T_SYM) {
                    return mk_error("define {enum ...}: expected name");
                }
                const char* name_str = enum_name->s;

                // Collect type parameters (optional): {enum Option T} -> T is param
                Value* type_params = cdr(cdr(type_contents));  // May be nil

                // Process variants from remaining args
                Value* variant_names = mk_nil();
                Value** names_tail = &variant_names;
                int variant_count = 0;

                Value* rest = cdr(args);
                int index = 0;
                while (!is_nil(rest)) {
                    Value* variant = car(rest);

                    if (variant && variant->tag == T_SYM) {
                        // Simple variant: None, True, False, etc.
                        // Create: (dict '__enum__ 'EnumName '__variant__ 'VariantName '__index__ idx)
                        Value* variant_dict = mk_cell(mk_sym("dict"),
                            mk_cell(mk_cell(mk_sym("quote"), mk_cell(mk_sym("__enum__"), mk_nil())),
                            mk_cell(mk_cell(mk_sym("quote"), mk_cell(enum_name, mk_nil())),
                            mk_cell(mk_cell(mk_sym("quote"), mk_cell(mk_sym("__variant__"), mk_nil())),
                            mk_cell(mk_cell(mk_sym("quote"), mk_cell(variant, mk_nil())),
                            mk_cell(mk_cell(mk_sym("quote"), mk_cell(mk_sym("__index__"), mk_nil())),
                            mk_cell(mk_int(index), mk_nil())))))));
                        Value* variant_val = omni_eval(variant_dict, env);
                        if (is_error(variant_val)) return variant_val;
                        env_define(env, variant, variant_val);

                        // Add to variant names list
                        *names_tail = mk_cell(variant, mk_nil());
                        names_tail = &((*names_tail)->cell.cdr);
                        variant_count++;
                        index++;

                    } else if (variant && variant->tag == T_CELL) {
                        // Data variant: (Some [value]) or (Point [x] [y])
                        Value* var_head = car(variant);
                        if (!var_head || var_head->tag != T_SYM) {
                            rest = cdr(rest);
                            continue;
                        }
                        Value* var_name = var_head;
                        const char* var_name_str = var_name->s;

                        // Collect field names from [field] forms
                        // [field] parses as (array field) or (array field (type T))
                        Value* fields = mk_nil();
                        Value** fields_tail = &fields;
                        int field_count = 0;

                        Value* var_rest = cdr(variant);
                        while (!is_nil(var_rest)) {
                            Value* field_form = car(var_rest);
                            if (field_form && field_form->tag == T_CELL) {
                                Value* arr_head = car(field_form);
                                if (arr_head && arr_head->tag == T_SYM &&
                                    strcmp(arr_head->s, "array") == 0) {
                                    // Extract field name
                                    Value* field_name = car(cdr(field_form));
                                    if (field_name && field_name->tag == T_SYM) {
                                        *fields_tail = mk_cell(field_name, mk_nil());
                                        fields_tail = &((*fields_tail)->cell.cdr);
                                        field_count++;
                                    }
                                }
                            }
                            var_rest = cdr(var_rest);
                        }

                        if (field_count > 0) {
                            // Create constructor function for data variant
                            // (fn (field1 field2 ...) (dict '__enum__ 'Enum ...))
                            // Build dict expression with all fields
                            Value* dict_args = mk_nil();
                            Value** dict_tail = &dict_args;

                            // __enum__
                            *dict_tail = mk_cell(mk_cell(mk_sym("quote"), mk_cell(mk_sym("__enum__"), mk_nil())), mk_nil());
                            dict_tail = &((*dict_tail)->cell.cdr);
                            *dict_tail = mk_cell(mk_cell(mk_sym("quote"), mk_cell(enum_name, mk_nil())), mk_nil());
                            dict_tail = &((*dict_tail)->cell.cdr);

                            // __variant__
                            *dict_tail = mk_cell(mk_cell(mk_sym("quote"), mk_cell(mk_sym("__variant__"), mk_nil())), mk_nil());
                            dict_tail = &((*dict_tail)->cell.cdr);
                            *dict_tail = mk_cell(mk_cell(mk_sym("quote"), mk_cell(var_name, mk_nil())), mk_nil());
                            dict_tail = &((*dict_tail)->cell.cdr);

                            // __index__
                            *dict_tail = mk_cell(mk_cell(mk_sym("quote"), mk_cell(mk_sym("__index__"), mk_nil())), mk_nil());
                            dict_tail = &((*dict_tail)->cell.cdr);
                            *dict_tail = mk_cell(mk_int(index), mk_nil());
                            dict_tail = &((*dict_tail)->cell.cdr);

                            // Add each data field
                            Value* f = fields;
                            while (!is_nil(f)) {
                                Value* fname = car(f);
                                *dict_tail = mk_cell(mk_cell(mk_sym("quote"), mk_cell(fname, mk_nil())), mk_nil());
                                dict_tail = &((*dict_tail)->cell.cdr);
                                *dict_tail = mk_cell(fname, mk_nil());  // Reference to param
                                dict_tail = &((*dict_tail)->cell.cdr);
                                f = cdr(f);
                            }

                            Value* dict_expr = mk_cell(mk_sym("dict"), dict_args);
                            Value* constructor = mk_lambda(fields, dict_expr, env_to_value(env));
                            env_define(env, var_name, constructor);
                        } else {
                            // No fields - simple variant but written as (Name)
                            Value* variant_dict = mk_cell(mk_sym("dict"),
                                mk_cell(mk_cell(mk_sym("quote"), mk_cell(mk_sym("__enum__"), mk_nil())),
                                mk_cell(mk_cell(mk_sym("quote"), mk_cell(enum_name, mk_nil())),
                                mk_cell(mk_cell(mk_sym("quote"), mk_cell(mk_sym("__variant__"), mk_nil())),
                                mk_cell(mk_cell(mk_sym("quote"), mk_cell(var_name, mk_nil())),
                                mk_cell(mk_cell(mk_sym("quote"), mk_cell(mk_sym("__index__"), mk_nil())),
                                mk_cell(mk_int(index), mk_nil())))))));
                            Value* variant_val = omni_eval(variant_dict, env);
                            if (is_error(variant_val)) return variant_val;
                            env_define(env, var_name, variant_val);
                        }

                        // Add to variant names list
                        *names_tail = mk_cell(var_name, mk_nil());
                        names_tail = &((*names_tail)->cell.cdr);
                        variant_count++;
                        index++;
                    }

                    rest = cdr(rest);
                }

                if (variant_count == 0) {
                    return mk_error("define {enum ...}: expected at least one variant");
                }

                // Create predicate: EnumName? checks if dict and __enum__ == 'EnumName
                char pred_name[256];
                snprintf(pred_name, sizeof(pred_name), "%s?", name_str);
                Value* pred_param = mk_sym("__x__");
                Value* dict_check = mk_cell(mk_sym("dict?"), mk_cell(pred_param, mk_nil()));
                Value* get_enum = mk_cell(mk_sym("get"),
                                   mk_cell(pred_param,
                                   mk_cell(mk_cell(mk_sym("quote"),
                                          mk_cell(mk_sym("__enum__"), mk_nil())), mk_nil())));
                Value* quoted_name = mk_cell(mk_sym("quote"), mk_cell(enum_name, mk_nil()));
                Value* eq_check = mk_cell(mk_sym("="),
                                 mk_cell(get_enum,
                                 mk_cell(quoted_name, mk_nil())));
                Value* if_check = mk_cell(mk_sym("if"),
                                 mk_cell(dict_check,
                                 mk_cell(eq_check,
                                 mk_cell(mk_sym("false"), mk_nil()))));
                Value* predicate = mk_lambda(mk_cell(pred_param, mk_nil()), if_check, env_to_value(env));
                env_define(env, mk_sym(pred_name), predicate);

                // Create per-variant predicates: Some? None? etc.
                Value* vn = variant_names;
                while (!is_nil(vn)) {
                    Value* vname = car(vn);
                    char var_pred_name[256];
                    snprintf(var_pred_name, sizeof(var_pred_name), "%s?", vname->s);

                    Value* vparam = mk_sym("__x__");
                    Value* vdict_check = mk_cell(mk_sym("dict?"), mk_cell(vparam, mk_nil()));
                    Value* vget_variant = mk_cell(mk_sym("get"),
                                       mk_cell(vparam,
                                       mk_cell(mk_cell(mk_sym("quote"),
                                              mk_cell(mk_sym("__variant__"), mk_nil())), mk_nil())));
                    Value* vquoted_name = mk_cell(mk_sym("quote"), mk_cell(vname, mk_nil()));
                    Value* veq_check = mk_cell(mk_sym("="),
                                     mk_cell(vget_variant,
                                     mk_cell(vquoted_name, mk_nil())));
                    Value* vif_check = mk_cell(mk_sym("if"),
                                     mk_cell(vdict_check,
                                     mk_cell(veq_check,
                                     mk_cell(mk_sym("false"), mk_nil()))));
                    Value* vpredicate = mk_lambda(mk_cell(vparam, mk_nil()), vif_check, env_to_value(env));
                    env_define(env, mk_sym(var_pred_name), vpredicate);

                    vn = cdr(vn);
                }

                // Also define EnumName as a list of all variant names for iteration
                env_define(env, enum_name, variant_names);

                return enum_name;
            }

            // Check for effect definition: (define {effect Name} mode ...)
            // {effect Name} parses as (type effect Name)
            if (car(type_contents)->tag == T_SYM &&
                strcmp(car(type_contents)->s, "effect") == 0) {
                // It's an effect definition!
                Value* effect_name = car(cdr(type_contents));
                if (!effect_name || effect_name->tag != T_SYM) {
                    return mk_error("define {effect ...}: expected name");
                }

                // Parse mode from next arg (required)
                Value* rest = cdr(args);
                if (is_nil(rest)) {
                    return mk_error("define {effect ...}: expected mode (:one-shot, :multi-shot, :abort, :tail)");
                }

                Value* mode_arg = car(rest);
                RecoveryModeOmni mode = RECOVERY_ONE_SHOT;  // default

                // Extract mode string - handle symbol or quoted form
                const char* mode_str = NULL;
                if (mode_arg && mode_arg->tag == T_SYM) {
                    mode_str = mode_arg->s;
                } else if (mode_arg && mode_arg->tag == T_CELL) {
                    // Handle (quote sym) form from :keyword syntax
                    Value* head = car(mode_arg);
                    if (head && head->tag == T_SYM && strcmp(head->s, "quote") == 0) {
                        Value* sym = car(cdr(mode_arg));
                        if (sym && sym->tag == T_SYM) {
                            mode_str = sym->s;
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
                        char msg[256];
                        snprintf(msg, sizeof(msg), "define {effect ...}: unknown mode '%s'", mode_str);
                        return mk_error(msg);
                    }
                }

                // Parse optional (payload X) and (returns Y) forms
                Value* payload_type = NULL;
                Value* returns_type = NULL;

                rest = cdr(rest);  // Move past mode
                while (!is_nil(rest)) {
                    Value* form = car(rest);
                    if (form && form->tag == T_CELL) {
                        Value* form_head = car(form);
                        if (form_head && form_head->tag == T_SYM) {
                            if (strcmp(form_head->s, "payload") == 0) {
                                // (payload Type) - store the type expression
                                payload_type = car(cdr(form));
                            } else if (strcmp(form_head->s, "returns") == 0) {
                                // (returns Type) - store the type expression
                                returns_type = car(cdr(form));
                            }
                        }
                    }
                    rest = cdr(rest);
                }

                // Register the effect type with full info
                register_effect_type_full(effect_name->s, mode, payload_type, returns_type);

                // Return the effect name symbol
                return effect_name;
            }
        }

        // Check for destructuring pattern: (define [a b c] value)
        // [a b c] parses as (array a b c) where first element is not a special keyword
        if (head && head->tag == T_SYM && strcmp(head->s, "array") == 0) {
            // This is a destructuring pattern
            Value* pattern = cdr(first);  // Skip "array" marker
            Value* value_expr = car(cdr(args));
            Value* value = omni_eval(value_expr, env);
            if (is_error(value)) return value;

            // Bind each variable in the pattern
            int index = 0;
            Value* dest_p = pattern;
            while (!is_nil(dest_p)) {
                Value* var_name = car(dest_p);
                if (!var_name || var_name->tag != T_SYM) {
                    return mk_error("define: destructuring pattern must contain symbols");
                }

                // Handle rest pattern
                if (strcmp(var_name->s, "..") == 0) {
                    // Bind rest of values to rest_name
                    dest_p = cdr(dest_p);
                    if (is_nil(dest_p)) {
                        return mk_error("define: expected name after ..");
                    }
                    Value* rest_name = car(dest_p);
                    if (!rest_name || rest_name->tag != T_SYM) {
                        return mk_error("define: expected symbol after ..");
                    }

                    // Collect remaining values
                    Value* rest_values = mk_nil();
                    Value** rest_tail = &rest_values;
                    Value* v = value;
                    int skip = index;
                    while (!is_nil(v) && skip > 0) {
                        v = cdr(v);
                        skip--;
                    }
                    while (!is_nil(v)) {
                        *rest_tail = mk_cell(car(v), mk_nil());
                        rest_tail = &((*rest_tail)->cell.cdr);
                        v = cdr(v);
                    }
                    env_define(env, rest_name, rest_values);
                    break;
                }

                // Get value at index
                Value* v = value;
                int i = index;
                while (!is_nil(v) && i > 0) {
                    v = cdr(v);
                    i--;
                }
                Value* elem_val = is_nil(v) ? mk_nothing() : car(v);
                env_define(env, var_name, elem_val);

                dest_p = cdr(dest_p);
                index++;
            }

            // Return first variable name
            return car(pattern);
        }
    }

    // Variable definition: (define name value)
    // OR Function definition: (define name [params...] body...)
    Value* name = first;
    Value* next_arg = car(cdr(args));
    
    // Check if it's a function definition: (define name [args] body)
    if (next_arg && next_arg->tag == T_CELL && car(next_arg)->tag == T_SYM &&
        strcmp(car(next_arg)->s, "array") == 0 && !is_nil(cdr(cdr(args)))) {
        
        Value* params = cdr(next_arg); // Unwrap array
        Value* body = cdr(cdr(args));
        Value* lambda_args = mk_cell(params, body);
        Value* lambda = eval_lambda(lambda_args, env);
        if (is_error(lambda)) return lambda;
        
        // Attach metadata if present
        if (define_metadata) {
            metadata_set(lambda, define_metadata);
        }
        env_define(env, name, lambda);
        return name;
    }

    Value* value = omni_eval(next_arg, env);
    if (is_error(value)) return value;
    // Attach metadata if present
    if (define_metadata) {
        metadata_set(value, define_metadata);
    }
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

    // Process params to extract defaults
    // [name default] parses as (array name default)
    Value* processed_params = mk_nil();
    Value** params_tail = &processed_params;
    Value* defaults = mk_nil();
    Value** defaults_tail = &defaults;
    int has_defaults = 0;

    Value* p = params;
    while (!is_nil(p)) {
        Value* param = car(p);

        if (param && param->tag == T_CELL) {
            Value* head = car(param);
            if (head && head->tag == T_SYM && strcmp(head->s, "array") == 0) {
                // [name default] form - extract name and default
                Value* name = car(cdr(param));
                Value* default_expr = car(cdr(cdr(param)));
                *params_tail = mk_cell(name, mk_nil());
                params_tail = &((*params_tail)->cell.cdr);
                *defaults_tail = mk_cell(default_expr, mk_nil());
                defaults_tail = &((*defaults_tail)->cell.cdr);
                has_defaults = 1;
                p = cdr(p);
                continue;
            }
        }

        // Plain param (symbol) or rest marker (..)
        *params_tail = mk_cell(param, mk_nil());
        params_tail = &((*params_tail)->cell.cdr);
        // Use NOTHING as marker for "no default" (distinguishes from NIL which is valid default)
        *defaults_tail = mk_cell(mk_nothing(), mk_nil());
        defaults_tail = &((*defaults_tail)->cell.cdr);
        p = cdr(p);
    }

    // Wrap body in do if multiple expressions
    if (!is_nil(cdr(cdr(args)))) {
        body = mk_cell(mk_sym("do"), cdr(args));
    }

    if (has_defaults) {
        return mk_lambda_with_defaults(processed_params, body, env_to_value(env), defaults);
    } else {
        return mk_lambda(processed_params, body, env_to_value(env));
    }
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
        Value* pattern = car(clauses);
        clauses = cdr(clauses);
        
        if (is_nil(clauses)) {
            return mk_error("match: odd number of forms in branches");
        }
        
        Value* result_expr = car(clauses);
        clauses = cdr(clauses);

        // Try to match pattern
        Env* match_env = env_new(env);
        int matched = match_pattern(pattern, subject, match_env);

        if (matched) {
            Value* result = omni_eval(result_expr, match_env);
            env_free(match_env);
            return result;
        }

        env_free(match_env);
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

        // Check for integrated :when guard: (pattern ... :when guard)
        Value* p_iter = pattern;
        Value* guard_expr = NULL;
        Value* clean_pattern = pattern;
        
        while (!is_nil(p_iter) && p_iter->tag == T_CELL) {
            Value* item = car(p_iter);
            if (item && item->tag == T_SYM && strcmp(item->s, ":when") == 0) {
                guard_expr = car(cdr(p_iter));
                /* Create a copy of the pattern without the :when clause for matching */
                /* This is a bit inefficient but correct for now */
                clean_pattern = mk_nil();
                Value** tail = &clean_pattern;
                Value* q = pattern;
                while (q != p_iter) {
                    *tail = mk_cell(car(q), mk_nil());
                    tail = &((*tail)->cell.cdr);
                    q = cdr(q);
                }
                break;
            }
            p_iter = cdr(p_iter);
        }

        int matched = 0;
        Value* clean_op = car(clean_pattern);

        // Empty list/array pattern
        if (is_nil(clean_pattern)) {
            matched = is_nil(subject);
        }
        // Array pattern: (array ...)
        else if (clean_op && clean_op->tag == T_SYM && strcmp(clean_op->s, "array") == 0) {
            if (subject && subject->tag == T_CELL) {
                Value* subj_items = subject;
                if (car(subject)->tag == T_SYM && strcmp(car(subject)->s, "array") == 0) {
                    subj_items = cdr(subject);
                }
                matched = match_list_pattern(cdr(clean_pattern), subj_items, env);
            }
        }
        // List pattern: (list ...)
        else if (clean_op && clean_op->tag == T_SYM && strcmp(clean_op->s, "list") == 0) {
            matched = match_list_pattern(cdr(clean_pattern), subject, env);
        }
        // cons pattern: (cons h t)
        else if (clean_op && clean_op->tag == T_SYM && strcmp(clean_op->s, "cons") == 0) {
            if (subject && subject->tag == T_CELL) {
                Value* head_pat = car(cdr(clean_pattern));
                Value* tail_pat = car(cdr(cdr(clean_pattern)));
                matched = match_pattern(head_pat, car(subject), env) &&
                          match_pattern(tail_pat, cdr(subject), env);
            }
        }
        // or pattern: (or p1 p2 ...)
        else if (clean_op && clean_op->tag == T_SYM && strcmp(clean_op->s, "or") == 0) {
            Value* alts = cdr(clean_pattern);
            while (!is_nil(alts)) {
                if (match_pattern(car(alts), subject, env)) {
                    matched = 1;
                    break;
                }
                alts = cdr(alts);
            }
        }
        // and pattern: (and p1 p2 ...)
        else if (clean_op && clean_op->tag == T_SYM && strcmp(clean_op->s, "and") == 0) {
            Value* conjs = cdr(clean_pattern);
            matched = 1;
            while (!is_nil(conjs)) {
                if (!match_pattern(car(conjs), subject, env)) {
                    matched = 0;
                    break;
                }
                conjs = cdr(conjs);
            }
        }
        // Constructor pattern: (Some v) or (Point x y)
        else if (clean_op && clean_op->tag == T_SYM) {
            /* ... rest of existing constructor matching logic using clean_pattern and clean_op ... */
            /* I'll use a placeholder here and then provide the full block in next step if needed, 
             * or just continue the logic */
            if (subject && subject->tag == T_CELL &&
                car(subject) && car(subject)->tag == T_SYM &&
                strcmp(car(subject)->s, "__dict__") == 0) {
                /* ... (Copy-paste the existing dict matching logic but use clean_pattern) ... */
                Value* entries = cdr(subject);
                Value* variant_name = NULL;
                Value* e = entries;
                while (!is_nil(e)) {
                    Value* pair = car(e);
                    if (pair && pair->tag == T_CELL) {
                        Value* k = car(pair);
                        if (k && k->tag == T_SYM && strcmp(k->s, "__variant__") == 0) {
                            variant_name = cdr(pair);
                            if (variant_name && variant_name->tag == T_CELL) variant_name = car(variant_name);
                            break;
                        }
                    }
                    e = cdr(e);
                }
                if (variant_name && variant_name->tag == T_SYM && strcmp(variant_name->s, clean_op->s) == 0) {
                    Value* pat_vars = cdr(clean_pattern);
                    Value* field_values = mk_nil();
                    Value** fv_tail = &field_values;
                    e = entries;
                    while (!is_nil(e)) {
                        Value* pair = car(e);
                        if (pair && pair->tag == T_CELL) {
                            Value* k = car(pair);
                            if (k && k->tag == T_SYM && strcmp(k->s, "__enum__") != 0 && strcmp(k->s, "__variant__") != 0 && strcmp(k->s, "__index__") != 0) {
                                Value* v = cdr(pair);
                                if (v && v->tag == T_CELL) v = car(v);
                                *fv_tail = mk_cell(v, mk_nil());
                                fv_tail = &((*fv_tail)->cell.cdr);
                            }
                        }
                        e = cdr(e);
                    }
                    matched = match_list_pattern(pat_vars, field_values, env);
                }
            } else if (subject && subject->tag == T_CELL && car(subject) && car(subject)->tag == T_SYM && strcmp(car(subject)->s, clean_op->s) == 0) {
                matched = match_list_pattern(cdr(clean_pattern), cdr(subject), env);
            }
        }

        if (matched && guard_expr) {
            Value* guard_res = omni_eval(guard_expr, env);
            matched = is_truthy(guard_res);
        }
        
        return matched;
    }
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

/* ============================================================
 * Hygienic Macro Expansion (syntax-rules style)
 * ============================================================
 *
 * Design: Position-based semantics (Proposal A)
 *   - Bare identifiers in patterns are bindings
 *   - Same identifiers in templates are references
 *   - #literals (...) declares literal matches
 *   - (literal ...) in templates protects from further expansion
 *   - ... for ellipsis/repetition
 */

// Gensym counter for hygiene
static int gensym_counter = 0;

// Check if symbol is in the literals list
static int is_literal_sym(Value* sym, Value* literals) {
    if (!sym || sym->tag != T_SYM) return 0;
    while (!is_nil(literals)) {
        Value* lit = car(literals);
        if (lit && lit->tag == T_SYM && strcmp(lit->s, sym->s) == 0) {
            return 1;
        }
        literals = cdr(literals);
    }
    return 0;
}

// Check if a symbol is the ellipsis marker
static int is_ellipsis(Value* v) {
    return v && v->tag == T_SYM && strcmp(v->s, "...") == 0;
}

// Count how many elements in a list
static int list_length(Value* list) {
    int len = 0;
    while (!is_nil(list)) {
        len++;
        list = cdr(list);
    }
    return len;
}

// Get nth element of a list (0-indexed)
static Value* list_ref(Value* list, int n) {
    while (n > 0 && !is_nil(list)) {
        list = cdr(list);
        n--;
    }
    return is_nil(list) ? mk_nil() : car(list);
}

// Debug flag for syntax matching
static int syntax_debug = 0;

// Syntax pattern matching
// bindings is an Env where we store pattern variable -> value mappings
// For ellipsis patterns, we store pattern variable -> list of values
static int syntax_match(Value* pattern, Value* input, Value* literals, Env* bindings) {
    if (!pattern) return input == NULL;

    if (syntax_debug) {
        char* ps = val_to_str(pattern);
        char* is = val_to_str(input);
        fprintf(stderr, "syntax_match: pattern=%s input=%s\n", ps, is);
        free(ps);
        free(is);
    }

    // Literal nil matches nil
    if (is_nil(pattern)) {
        return is_nil(input);
    }

    // Wildcard _ matches anything
    if (pattern->tag == T_SYM && strcmp(pattern->s, "_") == 0) {
        return 1;
    }

    // Literal symbols (declared in #literals) match exactly
    if (pattern->tag == T_SYM && is_literal_sym(pattern, literals)) {
        return input && input->tag == T_SYM && strcmp(input->s, pattern->s) == 0;
    }

    // Keywords (:foo) match literally
    if (pattern->tag == T_SYM && pattern->s[0] == ':') {
        return input && input->tag == T_SYM && strcmp(input->s, pattern->s) == 0;
    }

    // Pattern variable (bare identifier) - bind it
    if (pattern->tag == T_SYM) {
        env_define(bindings, pattern, input);
        return 1;
    }

    // Literal int matches exact int
    if (pattern->tag == T_INT) {
        return input && input->tag == T_INT && input->i == pattern->i;
    }

    // Literal string matches exact string
    if (pattern->tag == T_STRING) {
        return input && input->tag == T_STRING &&
               pattern->str.len == input->str.len &&
               memcmp(pattern->str.data, input->str.data, pattern->str.len) == 0;
    }

    // List pattern
    if (pattern->tag == T_CELL) {
        // Handle array wrapper in pattern
        Value* pat_items = pattern;
        if (car(pattern) && car(pattern)->tag == T_SYM &&
            strcmp(car(pattern)->s, "array") == 0) {
            pat_items = cdr(pattern);
        }

        // Input must be a list
        if (!input || (input->tag != T_CELL && !is_nil(input))) return 0;

        // Handle array wrapper in input
        Value* inp_items = input;
        if (input && input->tag == T_CELL && car(input) &&
            car(input)->tag == T_SYM && strcmp(car(input)->s, "array") == 0) {
            inp_items = cdr(input);
        }

        // Match list elements with ellipsis support
        while (!is_nil(pat_items)) {
            Value* pat_elem = car(pat_items);
            Value* next_pat = cdr(pat_items);

            // Check if next element is ellipsis
            if (syntax_debug) {
                char* pe = val_to_str(pat_elem);
                char* np = val_to_str(next_pat);
                fprintf(stderr, "  pat_elem=%s next_pat=%s is_ellipsis=%d\n",
                        pe, np, !is_nil(next_pat) && is_ellipsis(car(next_pat)));
                free(pe);
                free(np);
            }
            if (!is_nil(next_pat) && is_ellipsis(car(next_pat))) {
                if (syntax_debug) fprintf(stderr, "  -> ELLIPSIS match!\n");
                // Ellipsis pattern: match zero or more
                // Collect all remaining matches for this pattern
                Value* matches = mk_nil();
                Value** match_tail = &matches;

                // Figure out how many elements to consume
                // Count required elements after the ellipsis
                int required_after = 0;
                Value* after = cdr(next_pat);  // skip the ellipsis
                while (!is_nil(after)) {
                    if (!is_ellipsis(car(after))) required_after++;
                    after = cdr(after);
                }

                // Consume inp_items until we have exactly required_after left
                while (list_length(inp_items) > required_after) {
                    // Try to match pat_elem against current input
                    Env* sub_bindings = env_new(NULL);
                    if (!syntax_match(pat_elem, car(inp_items), literals, sub_bindings)) {
                        env_free(sub_bindings);
                        return 0;
                    }

                    // Collect the matched values
                    // For each binding in sub_bindings, append to the corresponding list
                    *match_tail = mk_cell(car(inp_items), mk_nil());
                    match_tail = &((*match_tail)->cell.cdr);

                    // Also track individual variable bindings for ellipsis
                    // We need to collect all bindings from sub_bindings
                    // For simplicity, just store the whole match
                    env_free(sub_bindings);
                    inp_items = cdr(inp_items);
                }

                // Store the matches under the pattern variable
                // For simple pattern like x ..., store list under x
                if (pat_elem->tag == T_SYM && !is_literal_sym(pat_elem, literals)) {
                    env_define(bindings, pat_elem, matches);
                }

                // Skip ellipsis in pattern
                pat_items = cdr(next_pat);
                continue;
            }

            // Regular (non-ellipsis) match
            if (is_nil(inp_items)) return 0;  // Not enough input

            if (!syntax_match(pat_elem, car(inp_items), literals, bindings)) {
                return 0;
            }

            pat_items = cdr(pat_items);
            inp_items = cdr(inp_items);
        }

        // All pattern elements consumed; input should be exhausted too
        return is_nil(inp_items);
    }

    return 0;
}

// Generate a fresh hygienically renamed symbol
static Value* gensym(const char* base, int counter) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s##%d", base, counter);
    return mk_sym(buf);
}

// Syntax template expansion
// Replaces pattern variables with their bound values
// Handles ellipsis by repeating the sub-template
static Value* syntax_expand(Value* template, Env* bindings, int counter) {
    if (!template) return mk_nil();

    // Literal values pass through
    if (template->tag == T_INT || template->tag == T_STRING ||
        template->tag == T_CHAR || template->tag == T_FLOAT) {
        return template;
    }

    // Nil passes through
    if (is_nil(template)) return template;

    // Symbol: look up in bindings, or return as-is (possibly renamed for hygiene)
    if (template->tag == T_SYM) {
        Value* bound = env_lookup(bindings, template);
        if (bound) {
            return bound;
        }
        // Not bound - return as-is (macro-introduced identifier)
        // For full hygiene, we'd rename it, but for now return as-is
        return template;
    }

    // List: process recursively, handling ellipsis
    if (template->tag == T_CELL) {
        // Check for (literal ...) escape
        if (car(template) && car(template)->tag == T_SYM &&
            strcmp(car(template)->s, "literal") == 0) {
            // Return the contents without expansion
            return car(cdr(template));
        }

        // Handle array wrapper
        int is_array = car(template) && car(template)->tag == T_SYM &&
                       strcmp(car(template)->s, "array") == 0;
        Value* items = is_array ? cdr(template) : template;

        Value* result = mk_nil();
        Value** result_tail = &result;

        while (!is_nil(items)) {
            Value* elem = car(items);
            Value* next = cdr(items);

            // Check if followed by ellipsis
            if (!is_nil(next) && is_ellipsis(car(next))) {
                // Ellipsis expansion: repeat elem for each element in the bound list
                // The elem should contain a pattern variable that was bound to a list
                // Find which variable in elem was bound to a list
                if (elem->tag == T_SYM) {
                    Value* bound_list = env_lookup(bindings, elem);
                    if (bound_list && bound_list->tag == T_CELL) {
                        // Splice in each element
                        while (!is_nil(bound_list)) {
                            *result_tail = mk_cell(car(bound_list), mk_nil());
                            result_tail = &((*result_tail)->cell.cdr);
                            bound_list = cdr(bound_list);
                        }
                    }
                } else {
                    // Complex ellipsis pattern - need to iterate
                    // For now, just expand once
                    Value* expanded = syntax_expand(elem, bindings, counter);
                    *result_tail = mk_cell(expanded, mk_nil());
                    result_tail = &((*result_tail)->cell.cdr);
                }
                items = cdr(next);  // skip ellipsis
                continue;
            }

            // Regular expansion
            Value* expanded = syntax_expand(elem, bindings, counter);
            *result_tail = mk_cell(expanded, mk_nil());
            result_tail = &((*result_tail)->cell.cdr);
            items = cdr(items);
        }

        if (is_array) {
            return mk_cell(mk_sym("array"), result);
        }
        return result;
    }

    return template;
}

// Try to expand a form as a macro
// Returns NULL if not a macro call, expanded form otherwise
static Value* try_macro_expand(Value* expr, Env* env) {
    if (!expr || expr->tag != T_CELL) return NULL;
    if (is_nil(expr)) return NULL;

    Value* head = car(expr);
    if (!head || head->tag != T_SYM) return NULL;

    // Look up head in environment
    Value* macro_val = env_lookup(env, head);
    if (!macro_val || !is_syntax(macro_val)) return NULL;

    // It's a macro! Try to match and expand
    Value* literals = macro_val->syntax.literals;
    Value* rules = macro_val->syntax.rules;

    // The macro name is always treated as a literal (matched exactly)
    // Add it to the literals list for matching
    Value* literals_with_name = mk_cell(head, literals);

    // Try each rule in order
    while (!is_nil(rules)) {
        Value* rule = car(rules);
        Value* pattern = car(rule);
        Value* template = car(cdr(rule));

        // Create fresh bindings environment for this match attempt
        Env* bindings = env_new(NULL);

        // Pattern should be (macro-name arg1 arg2 ...)
        // Match against the full expr
        if (syntax_match(pattern, expr, literals_with_name, bindings)) {
            // Match succeeded! Expand the template
            gensym_counter++;
            Value* expanded = syntax_expand(template, bindings, gensym_counter);
            env_free(bindings);
            return expanded;
        }

        env_free(bindings);
        rules = cdr(rules);
    }

    // No rule matched - error
    char msg[256];
    snprintf(msg, sizeof(msg), "syntax error: no matching clause for macro %s", head->s);
    return mk_error(msg);
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
        Value* key = omni_eval(car(args), env);  // Evaluate keys too
        if (is_error(key)) return key;
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

static Value* eval_put_bang(Value* args, Env* env) {
    Value* target_expr = car(args);
    Value* value_expr = car(cdr(args));
    Value* value = omni_eval(value_expr, env);
    if (is_error(value)) return value;

    if (target_expr->tag == T_CELL && car(target_expr)->tag == T_SYM && strcmp(car(target_expr)->s, "path") == 0) {
        Value* path_args = cdr(target_expr);
        Value* root_expr = car(path_args);
        Value* segments = cdr(path_args);
        
        Value* root = omni_eval(root_expr, env);
        if (is_error(root)) return root;
        
        Value* current = root;
        while (!is_nil(segments) && !is_nil(cdr(segments))) {
            Value* seg = car(segments);
            Value* key = (seg->tag == T_SYM) ? seg : omni_eval(car(cdr(seg)), env);
            Value* get_args = mk_cell(current, mk_cell(key, mk_nil()));
            current = prim_get(get_args);
            if (is_error(current)) return current;
            segments = cdr(segments);
        }
        
        Value* final_seg = car(segments);
        Value* final_key = (final_seg->tag == T_SYM) ? final_seg : omni_eval(car(cdr(final_seg)), env);
        
        // Unified set logic
        if (is_dict(current)) {
            prim_dict_set(mk_cell(current, mk_cell(final_key, mk_cell(value, mk_nil()))));
        } else if (is_array(current)) {
            prim_array_set(mk_cell(current, mk_cell(final_key, mk_cell(value, mk_nil()))));
        } else {
            return mk_error("put!: target is not a mutable collection");
        }
        return root;
    }
    return mk_error("put!: target must be a path");
}

static Value* eval_update_bang(Value* args, Env* env) {
    Value* target_expr = car(args);
    Value* fn_expr = car(cdr(args));
    Value* fn = omni_eval(fn_expr, env);
    if (is_error(fn)) return fn;

    Value* current_val = omni_eval(target_expr, env);
    if (is_error(current_val)) return current_val;

    Value* new_val = omni_apply(fn, mk_cell(current_val, mk_nil()), env);
    if (is_error(new_val)) return new_val;

    Value* put_args = mk_cell(target_expr, mk_cell(new_val, mk_nil()));
    return eval_put_bang(put_args, env);
}

static Value* eval_update(Value* args, Env* env) {
    // Functional update: for now, identical to update! but should ideally copy if not unique
    return eval_update_bang(args, env);
}

static Value* eval_path(Value* args, Env* env) {
    Value* root_expr = car(args);
    Value* segments = cdr(args);
    Value* current = omni_eval(root_expr, env);
    if (is_error(current)) return current;

    while (!is_nil(segments)) {
        Value* seg = car(segments);
        Value* key = (seg->tag == T_SYM) ? seg : omni_eval(car(cdr(seg)), env);
        Value* get_args = mk_cell(current, mk_cell(key, mk_nil()));
        current = prim_get(get_args);
        if (is_error(current)) return current;
        segments = cdr(segments);
    }
    return current;
}

static Value* eval_type(Value* args, Env* env) {
    Value* name_v = car(args);
    if (!name_v || name_v->tag != T_SYM) return mk_error("type: expected name");
    // For now, just return the symbol as a placeholder for the type
    // TODO: Implement proper type literals when the type system is ready
    return name_v;
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
// Helper to check if any arg in list is a float
static int has_float(Value* args) {
    while (!is_nil(args)) {
        Value* v = car(args);
        if (v && v->tag == T_FLOAT) return 1;
        args = cdr(args);
    }
    return 0;
}

static Value* prim_add(Value* args) {
    if (has_float(args)) {
        double result = 0.0;
        while (!is_nil(args)) {
            Value* v = car(args);
            if (is_numeric(v)) result += to_double(v);
            args = cdr(args);
        }
        return mk_float(result);
    } else {
        long result = 0;
        while (!is_nil(args)) {
            Value* v = car(args);
            if (v && v->tag == T_INT) result += v->i;
            args = cdr(args);
        }
        return mk_int(result);
    }
}

static Value* prim_sub(Value* args) {
    if (is_nil(args)) return mk_int(0);
    Value* first = car(args);
    if (!is_numeric(first)) return mk_error("- requires numbers");

    if (has_float(args)) {
        double result = to_double(first);
        args = cdr(args);
        if (is_nil(args)) return mk_float(-result);
        while (!is_nil(args)) {
            Value* v = car(args);
            if (is_numeric(v)) result -= to_double(v);
            args = cdr(args);
        }
        return mk_float(result);
    } else {
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
}

static Value* prim_mul(Value* args) {
    if (has_float(args)) {
        double result = 1.0;
        while (!is_nil(args)) {
            Value* v = car(args);
            if (is_numeric(v)) result *= to_double(v);
            args = cdr(args);
        }
        return mk_float(result);
    } else {
        long result = 1;
        while (!is_nil(args)) {
            Value* v = car(args);
            if (v && v->tag == T_INT) result *= v->i;
            args = cdr(args);
        }
        return mk_int(result);
    }
}

static Value* prim_div(Value* args) {
    if (is_nil(args)) return mk_int(1);
    Value* first = car(args);
    if (!is_numeric(first)) return mk_error("/ requires numbers");

    // Division always returns float if there are multiple args
    double result = to_double(first);
    args = cdr(args);
    if (is_nil(args)) {
        // Single argument: return reciprocal
        return mk_float(1.0 / result);
    }
    while (!is_nil(args)) {
        Value* v = car(args);
        if (is_numeric(v)) {
            double d = to_double(v);
            if (d == 0.0) return mk_error("Division by zero");
            result /= d;
        }
        args = cdr(args);
    }
    // Return int if result is whole number and no floats in args
    if (!has_float(args) && result == (long)result) {
        return mk_int((long)result);
    }
    return mk_float(result);
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
    if (!is_numeric(a) || !is_numeric(b))
        return mk_error("< requires two numbers");
    return mk_sym(to_double(a) < to_double(b) ? "true" : "false");
}

static Value* prim_gt(Value* args) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (!is_numeric(a) || !is_numeric(b))
        return mk_error("> requires two numbers");
    return mk_sym(to_double(a) > to_double(b) ? "true" : "false");
}

static Value* prim_le(Value* args) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (!is_numeric(a) || !is_numeric(b))
        return mk_error("<= requires two numbers");
    return mk_sym(to_double(a) <= to_double(b) ? "true" : "false");
}

static Value* prim_ge(Value* args) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (!is_numeric(a) || !is_numeric(b))
        return mk_error(">= requires two numbers");
    return mk_sym(to_double(a) >= to_double(b) ? "true" : "false");
}

static Value* prim_eq(Value* args) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (!a || !b) return mk_sym("false");
    // Allow int/float comparison
    if (is_numeric(a) && is_numeric(b)) {
        return mk_sym(to_double(a) == to_double(b) ? "true" : "false");
    }
    if (a->tag != b->tag) return mk_sym("false");
    switch (a->tag) {
        case T_INT: return mk_sym(a->i == b->i ? "true" : "false");
        case T_FLOAT: return mk_sym(a->f == b->f ? "true" : "false");
        case T_SYM:
        case T_CODE: return mk_sym(strcmp(a->s, b->s) == 0 ? "true" : "false");
        case T_STRING: return mk_sym(a->str.len == b->str.len &&
                                     memcmp(a->str.data, b->str.data, a->str.len) == 0 ? "true" : "false");
        case T_CHAR: return mk_sym(a->codepoint == b->codepoint ? "true" : "false");
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

// error - create an error value with a message
static Value* prim_error(Value* args) {
    Value* msg = car(args);
    if (msg && is_string(msg)) {
        return mk_error(msg->str.data);
    } else if (msg && msg->tag == T_SYM) {
        return mk_error(msg->s);
    }
    return mk_error("error");
}

// empty? - returns true for empty list or empty string
static Value* prim_empty(Value* args) {
    Value* v = car(args);
    if (is_nil(v)) return mk_sym("true");
    if (is_string(v) && string_len(v) == 0) return mk_sym("true");
    return mk_sym("false");
}

// String primitives
static Value* prim_string_q(Value* args) {
    return mk_sym(is_string(car(args)) ? "true" : "false");
}

static Value* prim_char_q(Value* args) {
    return mk_sym(is_char(car(args)) ? "true" : "false");
}

static Value* prim_symbol_q(Value* args) {
    Value* v = car(args);
    return mk_sym((v && v->tag == T_SYM) ? "true" : "false");
}

static Value* prim_string_length(Value* args) {
    Value* s = car(args);
    if (!is_string(s)) return mk_error("string-length: expected string");
    return mk_int((long)string_len(s));
}

static Value* prim_string_append(Value* args) {
    // Collect all strings and append
    DString* ds = ds_new();
    if (!ds) return mk_error("OOM");

    while (!is_nil(args)) {
        Value* s = car(args);
        if (is_string(s)) {
            ds_append_len(ds, string_data(s), string_len(s));
        } else if (s && s->tag == T_CHAR) {
            // Convert char to string
            char buf[8] = {0};
            if (s->codepoint < 128) {
                buf[0] = (char)s->codepoint;
                ds_append(ds, buf);
            } else {
                // Simple UTF-8 encoding (up to 3 bytes for now)
                if (s->codepoint < 0x800) {
                    buf[0] = 0xC0 | (s->codepoint >> 6);
                    buf[1] = 0x80 | (s->codepoint & 0x3F);
                } else {
                    buf[0] = 0xE0 | (s->codepoint >> 12);
                    buf[1] = 0x80 | ((s->codepoint >> 6) & 0x3F);
                    buf[2] = 0x80 | (s->codepoint & 0x3F);
                }
                ds_append(ds, buf);
            }
        } else {
            ds_free(ds);
            return mk_error("string-append: expected string or char");
        }
        args = cdr(args);
    }

    size_t len = ds_len(ds);
    char* data = ds_take(ds);
    Value* result = mk_string(data, len);
    free(data);
    return result;
}

// Helper to convert value to string representation for str interpolation
static void value_to_str(Value* v, DString* ds);

static Value* prim_str(Value* args) {
    // Convert all arguments to strings and concatenate
    DString* ds = ds_new();
    if (!ds) return mk_error("OOM");

    while (!is_nil(args)) {
        Value* v = car(args);
        value_to_str(v, ds);
        args = cdr(args);
    }

    size_t len = ds_len(ds);
    char* data = ds_take(ds);
    Value* result = mk_string(data, len);
    free(data);
    return result;
}

static void value_to_str(Value* v, DString* ds) {
    if (!v) {
        ds_append(ds, "nil");
        return;
    }

    char buf[64];
    switch (v->tag) {
        case T_INT:
            snprintf(buf, sizeof(buf), "%ld", v->i);
            ds_append(ds, buf);
            break;
        case T_FLOAT:
            snprintf(buf, sizeof(buf), "%g", v->f);
            ds_append(ds, buf);
            break;
        case T_STRING:
            ds_append_len(ds, string_data(v), string_len(v));
            break;
        case T_CHAR:
            if (v->codepoint < 128) {
                buf[0] = (char)v->codepoint;
                buf[1] = '\0';
                ds_append(ds, buf);
            } else {
                // UTF-8 encode
                if (v->codepoint < 0x800) {
                    buf[0] = 0xC0 | (v->codepoint >> 6);
                    buf[1] = 0x80 | (v->codepoint & 0x3F);
                    buf[2] = '\0';
                } else {
                    buf[0] = 0xE0 | (v->codepoint >> 12);
                    buf[1] = 0x80 | ((v->codepoint >> 6) & 0x3F);
                    buf[2] = 0x80 | (v->codepoint & 0x3F);
                    buf[3] = '\0';
                }
                ds_append(ds, buf);
            }
            break;
        case T_SYM:
            ds_append(ds, v->s);
            break;
        case T_NIL:
            ds_append(ds, "()");
            break;
        case T_NOTHING:
            ds_append(ds, "nothing");
            break;
        case T_ERROR:
            ds_append(ds, "<error: ");
            ds_append(ds, v->s);
            ds_append(ds, ">");
            break;
        default:
            snprintf(buf, sizeof(buf), "<object>");
            ds_append(ds, buf);
            break;
    }
}

static Value* prim_string_ref(Value* args) {
    Value* s = car(args);
    Value* idx = car(cdr(args));
    if (!is_string(s)) return mk_error("string-ref: expected string");
    if (!idx || idx->tag != T_INT) return mk_error("string-ref: expected integer index");

    size_t i = (size_t)idx->i;
    if (i >= string_len(s)) return mk_error("string-ref: index out of bounds");

    // For now, return byte as char (TODO: proper UTF-8)
    return mk_char((unsigned char)string_data(s)[i]);
}

static Value* prim_substring(Value* args) {
    Value* s = car(args);
    Value* start_v = car(cdr(args));
    Value* end_v = car(cdr(cdr(args)));

    if (!is_string(s)) return mk_error("substring: expected string");
    if (!start_v || start_v->tag != T_INT) return mk_error("substring: expected start index");
    if (!end_v || end_v->tag != T_INT) return mk_error("substring: expected end index");

    size_t start = (size_t)start_v->i;
    size_t end = (size_t)end_v->i;

    return substring(s, start, end);
}

static Value* prim_string_to_list(Value* args) {
    Value* s = car(args);
    if (!is_string(s)) return mk_error("string->list: expected string");

    Value* result = mk_nil();
    Value** tail = &result;

    // For now, treat as bytes (TODO: proper UTF-8)
    for (size_t i = 0; i < string_len(s); i++) {
        Value* ch = mk_char((unsigned char)string_data(s)[i]);
        Value* cell = mk_cell(ch, mk_nil());
        *tail = cell;
        tail = &cell->cell.cdr;
    }

    return result;
}

static Value* prim_list_to_string(Value* args) {
    Value* lst = car(args);

    DString* ds = ds_new();
    if (!ds) return mk_error("OOM");

    while (!is_nil(lst)) {
        Value* ch = car(lst);
        if (!is_char(ch)) {
            ds_free(ds);
            return mk_error("list->string: expected list of chars");
        }
        if (ch->codepoint < 128) {
            ds_append_char(ds, (char)ch->codepoint);
        } else {
            // UTF-8 encoding
            char buf[4] = {0};
            if (ch->codepoint < 0x800) {
                buf[0] = 0xC0 | (ch->codepoint >> 6);
                buf[1] = 0x80 | (ch->codepoint & 0x3F);
            } else {
                buf[0] = 0xE0 | (ch->codepoint >> 12);
                buf[1] = 0x80 | ((ch->codepoint >> 6) & 0x3F);
                buf[2] = 0x80 | (ch->codepoint & 0x3F);
            }
            ds_append(ds, buf);
        }
        lst = cdr(lst);
    }

    size_t len = ds_len(ds);
    char* data = ds_take(ds);
    Value* result = mk_string(data, len);
    free(data);
    return result;
}

// Math primitives
static Value* prim_float_q(Value* args) {
    return mk_sym(is_float(car(args)) ? "true" : "false");
}

static Value* prim_int_q(Value* args) {
    Value* v = car(args);
    return mk_sym(v && v->tag == T_INT ? "true" : "false");
}

static Value* prim_number_q(Value* args) {
    return mk_sym(is_numeric(car(args)) ? "true" : "false");
}

static Value* prim_sin(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("sin: expected number");
    return mk_float(sin(to_double(v)));
}

static Value* prim_cos(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("cos: expected number");
    return mk_float(cos(to_double(v)));
}

static Value* prim_tan(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("tan: expected number");
    return mk_float(tan(to_double(v)));
}

static Value* prim_asin(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("asin: expected number");
    return mk_float(asin(to_double(v)));
}

static Value* prim_acos(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("acos: expected number");
    return mk_float(acos(to_double(v)));
}

static Value* prim_atan(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("atan: expected number");
    return mk_float(atan(to_double(v)));
}

static Value* prim_atan2(Value* args) {
    Value* y = car(args);
    Value* x = car(cdr(args));
    if (!is_numeric(y) || !is_numeric(x)) return mk_error("atan2: expected two numbers");
    return mk_float(atan2(to_double(y), to_double(x)));
}

static Value* prim_exp(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("exp: expected number");
    return mk_float(exp(to_double(v)));
}

static Value* prim_log(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("log: expected number");
    double d = to_double(v);
    if (d <= 0) return mk_error("log: domain error");
    return mk_float(log(d));
}

static Value* prim_log10(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("log10: expected number");
    double d = to_double(v);
    if (d <= 0) return mk_error("log10: domain error");
    return mk_float(log10(d));
}

static Value* prim_sqrt(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("sqrt: expected number");
    double d = to_double(v);
    if (d < 0) return mk_error("sqrt: domain error");
    return mk_float(sqrt(d));
}

static Value* prim_pow(Value* args) {
    Value* base = car(args);
    Value* exp = car(cdr(args));
    if (!is_numeric(base) || !is_numeric(exp)) return mk_error("pow: expected two numbers");
    return mk_float(pow(to_double(base), to_double(exp)));
}

static Value* prim_abs(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("abs: expected number");
    if (v->tag == T_INT) {
        return mk_int(v->i < 0 ? -v->i : v->i);
    }
    return mk_float(fabs(v->f));
}

static Value* prim_floor(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("floor: expected number");
    return mk_float(floor(to_double(v)));
}

static Value* prim_ceil(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("ceil: expected number");
    return mk_float(ceil(to_double(v)));
}

static Value* prim_round(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("round: expected number");
    return mk_float(round(to_double(v)));
}

static Value* prim_truncate(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("truncate: expected number");
    return mk_float(trunc(to_double(v)));
}

static Value* prim_to_int(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("->int: expected number");
    return mk_int((long)to_double(v));
}

static Value* prim_to_float(Value* args) {
    Value* v = car(args);
    if (!is_numeric(v)) return mk_error("->float: expected number");
    return mk_float(to_double(v));
}

// Constants
// Note: pi, e, inf, nan are defined as constants in init_env(), not as primitives

static Value* prim_nan_q(Value* args) {
    Value* v = car(args);
    if (!is_float(v)) return mk_sym("false");
    return mk_sym(isnan(v->f) ? "true" : "false");
}

static Value* prim_inf_q(Value* args) {
    Value* v = car(args);
    if (!is_float(v)) return mk_sym("false");
    return mk_sym(isinf(v->f) ? "true" : "false");
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

// ===== Array primitives =====

// Helper: check if value is an array (__array__ marker)
static int is_array(Value* v) {
    return v && v->tag == T_CELL && car(v) && car(v)->tag == T_SYM &&
           strcmp(car(v)->s, "__array__") == 0;
}

// Helper: get array elements (skips __array__ marker)
static Value* array_elements(Value* arr) {
    return is_array(arr) ? cdr(arr) : mk_nil();
}

static Value* prim_array_q(Value* args) {
    return mk_sym(is_array(car(args)) ? "true" : "false");
}

static Value* prim_array_length(Value* args) {
    Value* arr = car(args);
    if (!is_array(arr)) return mk_error("array-length: expected array");
    Value* elems = array_elements(arr);
    long len = 0;
    while (!is_nil(elems)) {
        len++;
        elems = cdr(elems);
    }
    return mk_int(len);
}

static Value* prim_array_ref(Value* args) {
    Value* arr = car(args);
    Value* idx = car(cdr(args));
    if (!is_array(arr)) return mk_error("array-ref: expected array");
    if (!idx || idx->tag != T_INT) return mk_error("array-ref: expected integer index");

    long i = idx->i;
    if (i < 0) return mk_error("array-ref: negative index");

    Value* elems = array_elements(arr);
    while (i > 0 && !is_nil(elems)) {
        elems = cdr(elems);
        i--;
    }
    if (is_nil(elems)) return mk_error("array-ref: index out of bounds");
    return car(elems);
}

static Value* prim_array_set(Value* args) {
    Value* arr = car(args);
    Value* idx = car(cdr(args));
    Value* val = car(cdr(cdr(args)));
    if (!is_array(arr)) return mk_error("array-set!: expected array");
    if (!idx || idx->tag != T_INT) return mk_error("array-set!: expected integer index");

    long i = idx->i;
    if (i < 0) return mk_error("array-set!: negative index");

    Value* elems = array_elements(arr);
    while (i > 0 && !is_nil(elems)) {
        elems = cdr(elems);
        i--;
    }
    if (is_nil(elems)) return mk_error("array-set!: index out of bounds");

    // Mutate the car of this cell
    elems->cell.car = val;
    return val;
}

static Value* prim_array_slice(Value* args) {
    Value* arr = car(args);
    Value* start_v = car(cdr(args));
    Value* end_v = car(cdr(cdr(args)));

    if (!is_array(arr)) return mk_error("array-slice: expected array");
    if (!start_v || start_v->tag != T_INT) return mk_error("array-slice: expected integer start");

    long start = start_v->i;
    long end = -1;  // -1 means to end
    if (end_v && end_v->tag == T_INT) {
        end = end_v->i;
    }

    if (start < 0) return mk_error("array-slice: negative start");

    Value* elems = array_elements(arr);

    // Skip to start
    long pos = 0;
    while (pos < start && !is_nil(elems)) {
        elems = cdr(elems);
        pos++;
    }

    // Collect elements until end
    Value* result = mk_nil();
    Value** tail = &result;
    while (!is_nil(elems) && (end < 0 || pos < end)) {
        Value* cell = mk_cell(car(elems), mk_nil());
        *tail = cell;
        tail = &cell->cell.cdr;
        elems = cdr(elems);
        pos++;
    }

    return mk_cell(mk_sym("__array__"), result);
}

// Tuples and named-tuples removed - use lists and dicts instead

// ===== Partial Application =====

// Partial application creates a closure that captures fn and initial args
static Value* prim_partial(Value* args) {
    // (partial fn arg1 arg2 ...) -> closure that appends remaining args and calls fn
    Value* fn = car(args);
    Value* captured_args = cdr(args);

    if (!fn) return mk_error("partial: expected function");

    // Create a dict to hold the partial application data
    // Structure: (__partial__ fn captured_args)
    return mk_cell(mk_sym("__partial__"), mk_cell(fn, mk_cell(captured_args, mk_nil())));
}

static Value* prim_partial_q(Value* args) {
    Value* v = car(args);
    int is_partial = v && v->tag == T_CELL && car(v) && car(v)->tag == T_SYM &&
                     strcmp(car(v)->s, "__partial__") == 0;
    return mk_sym(is_partial ? "true" : "false");
}

// ===== compose functions =====
static Value* prim_compose(Value* args) {
    // (compose f g) -> (__compose__ f g)
    Value* f = car(args);
    Value* g = car(cdr(args));
    if (!f || !g) return mk_error("compose: expected two functions");
    return mk_cell(mk_sym("__compose__"), mk_cell(f, mk_cell(g, mk_nil())));
}

// ===== Dict primitives =====

// Helper: check if value is a dict (__dict__ marker)
static int is_dict(Value* v) {
    return v && v->tag == T_CELL && car(v) && car(v)->tag == T_SYM &&
           strcmp(car(v)->s, "__dict__") == 0;
}

// Helper: get dict entries (skips __dict__ marker)
static Value* dict_entries(Value* dict) {
    return is_dict(dict) ? cdr(dict) : mk_nil();
}

static Value* prim_dict_q(Value* args) {
    return mk_sym(is_dict(car(args)) ? "true" : "false");
}

static Value* prim_dict_ref(Value* args) {
    Value* dict = car(args);
    Value* key = car(cdr(args));
    if (!is_dict(dict)) return mk_error("dict-ref: expected dict");

    Value* entries = dict_entries(dict);
    while (!is_nil(entries)) {
        Value* pair = car(entries);
        Value* k = car(pair);
        // Compare keys (symbols by string, others by identity)
        if (k && key && k->tag == T_SYM && key->tag == T_SYM) {
            if (strcmp(k->s, key->s) == 0) return cdr(pair);
        } else if (k == key) {
            return cdr(pair);
        }
        entries = cdr(entries);
    }
    return mk_nothing();  // Key not found
}

static Value* prim_dict_set(Value* args) {
    Value* dict = car(args);
    Value* key = car(cdr(args));
    Value* val = car(cdr(cdr(args)));
    if (!is_dict(dict)) return mk_error("dict-set!: expected dict");

    Value* entries = dict_entries(dict);
    while (!is_nil(entries)) {
        Value* pair = car(entries);
        Value* k = car(pair);
        if (k && key && k->tag == T_SYM && key->tag == T_SYM) {
            if (strcmp(k->s, key->s) == 0) {
                pair->cell.cdr = val;  // Mutate existing
                return val;
            }
        } else if (k == key) {
            pair->cell.cdr = val;
            return val;
        }
        entries = cdr(entries);
    }

    // Key not found, add new entry at end
    Value* new_pair = mk_cell(key, val);
    Value* new_entry = mk_cell(new_pair, mk_nil());

    // Find end of entries list
    Value* tail = cdr(dict);
    if (is_nil(tail)) {
        dict->cell.cdr = new_entry;
    } else {
        while (!is_nil(cdr(tail))) {
            tail = cdr(tail);
        }
        tail->cell.cdr = new_entry;
    }
    return val;
}

static Value* prim_keys(Value* args) {
    Value* dict = car(args);
    if (!is_dict(dict)) return mk_error("keys: expected dict");

    Value* entries = dict_entries(dict);
    Value* result = mk_nil();
    Value** tail = &result;

    while (!is_nil(entries)) {
        Value* pair = car(entries);
        Value* cell = mk_cell(car(pair), mk_nil());
        *tail = cell;
        tail = &cell->cell.cdr;
        entries = cdr(entries);
    }
    return result;
}

static Value* prim_values(Value* args) {
    Value* dict = car(args);
    if (!is_dict(dict)) return mk_error("values: expected dict");

    Value* entries = dict_entries(dict);
    Value* result = mk_nil();
    Value** tail = &result;

    while (!is_nil(entries)) {
        Value* pair = car(entries);
        Value* cell = mk_cell(cdr(pair), mk_nil());
        *tail = cell;
        tail = &cell->cell.cdr;
        entries = cdr(entries);
    }
    return result;
}

static Value* prim_dict_contains(Value* args) {
    Value* dict = car(args);
    Value* key = car(cdr(args));
    if (!is_dict(dict)) return mk_error("dict-contains?: expected dict");

    Value* entries = dict_entries(dict);
    while (!is_nil(entries)) {
        Value* pair = car(entries);
        Value* k = car(pair);
        if (k && key && k->tag == T_SYM && key->tag == T_SYM) {
            if (strcmp(k->s, key->s) == 0) return mk_sym("true");
        } else if (k == key) {
            return mk_sym("true");
        }
        entries = cdr(entries);
    }
    return mk_sym("false");
}

/**
 * (get obj key)
 * Generic accessor that works on both arrays and dicts.
 * For arrays: key must be an integer index
 * For dicts: key can be any value (typically keyword/symbol)
 */
static Value* prim_get(Value* args) {
    Value* obj = car(args);
    Value* key = car(cdr(args));

    if (!obj) return mk_error("get: nil object");

    // Array access
    if (is_array(obj)) {
        if (!key || key->tag != T_INT) {
            return mk_error("get: array access requires integer index");
        }
        long i = key->i;
        if (i < 0) return mk_error("get: negative index");

        Value* elems = array_elements(obj);
        while (i > 0 && !is_nil(elems)) {
            elems = cdr(elems);
            i--;
        }
        if (is_nil(elems)) return mk_error("get: index out of bounds");
        return car(elems);
    }

    // Dict access
    if (is_dict(obj)) {
        Value* entries = dict_entries(obj);
        while (!is_nil(entries)) {
            Value* pair = car(entries);
            Value* k = car(pair);
            // Compare keys
            if (k && key) {
                if (k->tag == T_SYM && key->tag == T_SYM) {
                    if (strcmp(k->s, key->s) == 0) return cdr(pair);
                } else if (k->tag == T_INT && key->tag == T_INT) {
                    if (k->i == key->i) return cdr(pair);
                } else if (k->tag == T_STRING && key->tag == T_STRING) {
                    if (k->str.len == key->str.len &&
                        memcmp(k->str.data, key->str.data, k->str.len) == 0) {
                        return cdr(pair);
                    }
                } else if (k == key) {
                    return cdr(pair);
                }
            }
            entries = cdr(entries);
        }
        return mk_nothing();  // Key not found
    }

    // String character access
    if (is_string(obj)) {
        if (!key || key->tag != T_INT) {
            return mk_error("get: string access requires integer index");
        }
        long i = key->i;
        if (i < 0 || (size_t)i >= string_len(obj)) {
            return mk_error("get: string index out of bounds");
        }
        return mk_char((unsigned char)string_data(obj)[i]);
    }

    return mk_error("get: expected array, dict, or string");
}

// ===== File I/O primitives =====

static Value* prim_open(Value* args) {
    Value* filename = car(args);
    Value* mode = car(cdr(args));

    if (!is_string(filename)) return mk_error("open: expected string filename");

    const char* fname = string_data(filename);
    const char* fmode = "r";
    int modenum = 0;

    if (mode && mode->tag == T_SYM) {
        const char* ms = mode->s;
        // Skip leading colon for keywords
        if (ms[0] == ':') ms++;

        if (strcmp(ms, "read") == 0 || strcmp(ms, "r") == 0) {
            fmode = "r";
            modenum = 0;
        } else if (strcmp(ms, "write") == 0 || strcmp(ms, "w") == 0) {
            fmode = "w";
            modenum = 1;
        } else if (strcmp(ms, "append") == 0 || strcmp(ms, "a") == 0) {
            fmode = "a";
            modenum = 2;
        } else {
            return mk_error("open: unknown mode (use :read, :write, or :append)");
        }
    }

    FILE* fp = fopen(fname, fmode);
    if (!fp) return mk_error("open: cannot open file");

    return mk_port(fp, fname, modenum);
}

static Value* prim_close(Value* args) {
    Value* port = car(args);
    if (!is_port(port)) return mk_error("close: expected port");
    if (port->port.closed) return mk_nothing();

    if (port->port.fp) {
        fclose(port->port.fp);
        port->port.fp = NULL;
    }
    port->port.closed = 1;
    return mk_nothing();
}

static Value* prim_read_line(Value* args) {
    Value* port = car(args);
    if (!is_port(port)) return mk_error("read-line: expected port");
    if (port->port.closed) return mk_error("read-line: port is closed");
    if (!port->port.fp) return mk_error("read-line: invalid port");

    char* line = NULL;
    size_t len = 0;
    ssize_t nread = getline(&line, &len, port->port.fp);

    if (nread == -1) {
        free(line);
        return mk_nothing();  // EOF
    }

    // Remove trailing newline
    if (nread > 0 && line[nread - 1] == '\n') {
        line[nread - 1] = '\0';
        nread--;
    }

    Value* result = mk_string(line, (size_t)nread);
    free(line);
    return result;
}

static Value* prim_read_all(Value* args) {
    Value* port = car(args);
    if (!is_port(port)) return mk_error("read-all: expected port");
    if (port->port.closed) return mk_error("read-all: port is closed");
    if (!port->port.fp) return mk_error("read-all: invalid port");

    // Get file size
    long start = ftell(port->port.fp);
    fseek(port->port.fp, 0, SEEK_END);
    long end = ftell(port->port.fp);
    fseek(port->port.fp, start, SEEK_SET);

    size_t size = (size_t)(end - start);
    char* buf = malloc(size + 1);
    if (!buf) return mk_error("read-all: out of memory");

    size_t nread = fread(buf, 1, size, port->port.fp);
    buf[nread] = '\0';

    Value* result = mk_string(buf, nread);
    free(buf);
    return result;
}

static Value* prim_write_string(Value* args) {
    Value* port = car(args);
    Value* str = car(cdr(args));

    if (!is_port(port)) return mk_error("write-string: expected port");
    if (port->port.closed) return mk_error("write-string: port is closed");
    if (!port->port.fp) return mk_error("write-string: invalid port");
    if (!is_string(str)) return mk_error("write-string: expected string");

    size_t len = string_len(str);
    const char* data = string_data(str);
    size_t written = fwrite(data, 1, len, port->port.fp);

    return mk_int((long)written);
}

static Value* prim_write_line(Value* args) {
    Value* port = car(args);
    Value* str = car(cdr(args));

    if (!is_port(port)) return mk_error("write-line: expected port");
    if (port->port.closed) return mk_error("write-line: port is closed");
    if (!port->port.fp) return mk_error("write-line: invalid port");
    if (!is_string(str)) return mk_error("write-line: expected string");

    size_t len = string_len(str);
    const char* data = string_data(str);
    fwrite(data, 1, len, port->port.fp);
    fputc('\n', port->port.fp);

    return mk_int((long)(len + 1));
}

static Value* prim_flush(Value* args) {
    Value* port = car(args);
    if (!is_port(port)) return mk_error("flush: expected port");
    if (port->port.closed) return mk_error("flush: port is closed");
    if (port->port.fp) fflush(port->port.fp);
    return mk_nothing();
}

static Value* prim_port_q(Value* args) {
    return mk_sym(is_port(car(args)) ? "true" : "false");
}

static Value* prim_eof_q(Value* args) {
    Value* port = car(args);
    if (!is_port(port)) return mk_error("eof?: expected port");
    if (port->port.closed || !port->port.fp) return mk_sym("true");
    return mk_sym(feof(port->port.fp) ? "true" : "false");
}

static Value* prim_file_exists(Value* args) {
    Value* filename = car(args);
    if (!is_string(filename)) return mk_error("file-exists?: expected string");
    FILE* fp = fopen(string_data(filename), "r");
    if (fp) {
        fclose(fp);
        return mk_sym("true");
    }
    return mk_sym("false");
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
        case T_STRING:  return mk_sym("string");
        case T_CHAR:    return mk_sym("char");
        case T_FLOAT:   return mk_sym("float");
        case T_PORT:    return mk_sym("port");
        case T_SYNTAX:  return mk_sym("syntax");
        case T_FFI_LIB: return mk_sym("ffi-lib");
        case T_FFI_PTR: return mk_sym("ffi-ptr");
        case T_THREAD:  return mk_sym("thread");
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
                     "fiber (state=%d)", v->proc.state);
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
 * Fiber System Implementation (ucontext-based true coroutines)
 * ============================================================
 *
 * True cooperative fibers using ucontext for stack switching.
 * Each fiber has its own stack and can yield at any point.
 *
 * Primitives:
 *   (fiber thunk)  - Create a paused fiber from a thunk
 *   (resume f)     - Step into fiber, run until yield/done
 *   (resume f val) - Resume with a value (returned by yield)
 *   (yield)        - Yield nothing from current fiber
 *   (yield val)    - Yield value from current fiber
 *   (spawn f)      - Run fiber under scheduler
 *   (join f)       - Wait for spawned fiber to finish
 *   (fiber? x)     - Check if x is a fiber
 *   (fiber-done? f)- Check if fiber is done
 */

#include <ucontext.h>

// Fiber scheduler state
static Value* fiber_run_queue = NULL;     // List of ready fibers
static Value* fiber_current = NULL;       // Currently running fiber
static int fiber_scheduler_running = 0;   // Is scheduler active?

// Stack of resumer contexts for nested fiber resume
#define MAX_FIBER_DEPTH 64
static ucontext_t fiber_resumer_stack[MAX_FIBER_DEPTH];
static int fiber_resumer_sp = 0;  // Stack pointer (points to next free slot)

// Forward declarations
static void fiber_entry(void);
static Value* fiber_resume_internal(Value* fiber, Value* resume_val);

/*
 * Entry point for fiber execution.
 * Called via makecontext when fiber starts.
 */
static void fiber_entry(void) {
    Value* fiber = fiber_current;
    if (!fiber || fiber->tag != T_PROCESS) {
        // Shouldn't happen, but guard against it
        return;
    }

    FiberContext* ctx = fiber->proc.fiber_ctx;

    // Execute the thunk
    Value* result = omni_apply(fiber->proc.thunk, mk_nil(), global_env);

    // Fiber completed
    fiber->proc.state = PROC_DONE;
    fiber->proc.result = result;
    ctx->yield_value = result;

    // Switch back to resumer (use top of stack)
    if (fiber_resumer_sp > 0) {
        swapcontext(&ctx->ctx, &fiber_resumer_stack[fiber_resumer_sp - 1]);
    }
}

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
 * Internal resume implementation using ucontext.
 * Uses a stack of resumer contexts to support nested fiber resume.
 */
static Value* fiber_resume_internal(Value* fiber, Value* resume_val) {
    if (fiber->proc.state == PROC_DONE) {
        return fiber->proc.result ? fiber->proc.result : mk_nothing();
    }

    FiberContext* ctx = fiber->proc.fiber_ctx;
    if (!ctx) {
        return mk_error("fiber: missing fiber context");
    }

    // Check for stack overflow
    if (fiber_resumer_sp >= MAX_FIBER_DEPTH) {
        return mk_error("fiber: maximum nesting depth exceeded");
    }

    // Save current fiber and set this one as current
    Value* prev_fiber = fiber_current;
    fiber_current = fiber;
    fiber->proc.state = PROC_RUNNING;

    // Set the resume value (will be returned by yield in the fiber)
    ctx->yield_value = resume_val;

    if (!ctx->started) {
        // First time: initialize the fiber's context
        ctx->started = 1;

        if (getcontext(&ctx->ctx) == -1) {
            fiber_current = prev_fiber;
            return mk_error("fiber: getcontext failed");
        }

        // Set up the fiber's stack
        ctx->ctx.uc_stack.ss_sp = ctx->stack;
        ctx->ctx.uc_stack.ss_size = FIBER_STACK_SIZE;
        ctx->ctx.uc_link = NULL;  // We handle return via swapcontext

        // Set entry point
        makecontext(&ctx->ctx, fiber_entry, 0);
    }

    // Push resumer context onto stack and switch to fiber
    int my_slot = fiber_resumer_sp++;
    if (swapcontext(&fiber_resumer_stack[my_slot], &ctx->ctx) == -1) {
        fiber_resumer_sp--;
        fiber_current = prev_fiber;
        return mk_error("fiber: swapcontext failed");
    }

    // We're back! Pop the stack
    fiber_resumer_sp--;

    // Restore previous fiber
    fiber_current = prev_fiber;

    // Return the yield/result value
    return ctx->yield_value ? ctx->yield_value : mk_nothing();
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

    return fiber_resume_internal(fiber, resume_val);
}

/*
 * (yield)
 * (yield val)
 * Yield from current fiber, suspending execution and returning val to resumer.
 * When resumed, yield returns the value passed to resume.
 */
static Value* prim_yield(Value* args) {
    if (!fiber_current) {
        return mk_error("yield: not inside a fiber");
    }

    Value* yield_val = car(args);
    if (!yield_val) yield_val = mk_nothing();

    FiberContext* ctx = fiber_current->proc.fiber_ctx;
    if (!ctx) {
        return mk_error("yield: fiber context missing");
    }

    if (fiber_resumer_sp <= 0) {
        return mk_error("yield: no resumer context");
    }

    // Set yield value for resumer to receive
    ctx->yield_value = yield_val;
    fiber_current->proc.state = PROC_YIELDED;

    // Switch back to resumer context (top of stack)
    if (swapcontext(&ctx->ctx, &fiber_resumer_stack[fiber_resumer_sp - 1]) == -1) {
        return mk_error("yield: swapcontext failed");
    }

    // We're resumed! Return the value passed to resume
    return ctx->yield_value ? ctx->yield_value : mk_nothing();
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
 * If fiber parks (waiting on channel), runs the scheduler to let other fibers progress.
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

    // Run fiber to completion, handling parked state
    while (fiber->proc.state != PROC_DONE) {
        // If fiber is parked (waiting on channel), run other fibers
        if (fiber->proc.state == PROC_PARKED) {
            // Run one step of another fiber from the queue
            if (fiber_run_queue && !is_nil(fiber_run_queue)) {
                Value* other = car(fiber_run_queue);
                fiber_run_queue = cdr(fiber_run_queue);

                if (other && other->tag == T_PROCESS &&
                    other->proc.state != PROC_DONE &&
                    other->proc.state != PROC_PARKED) {
                    Value* step_result = fiber_resume_internal(other, mk_nothing());
                    if (is_error(step_result) && other->proc.state != PROC_DONE) {
                        return step_result;
                    }
                    // Re-queue if not done and not parked
                    if (other->proc.state != PROC_DONE && other->proc.state != PROC_PARKED) {
                        if (!fiber_run_queue || is_nil(fiber_run_queue)) {
                            fiber_run_queue = mk_cell(other, mk_nil());
                        } else {
                            Value* tail = fiber_run_queue;
                            while (cdr(tail) && !is_nil(cdr(tail))) tail = cdr(tail);
                            tail->cell.cdr = mk_cell(other, mk_nil());
                        }
                    }
                }
            } else {
                // No other fibers to run - deadlock or channel should have completed
                // Check if our fiber is still parked
                if (fiber->proc.state == PROC_PARKED) {
                    return mk_error("join: deadlock - fiber parked with no other fibers");
                }
            }
            continue;  // Check fiber state again
        }

        // Resume the fiber
        if (fiber->proc.state == PROC_READY || fiber->proc.state == PROC_YIELDED) {
            Value* result = fiber_resume_internal(fiber, mk_nothing());
            if (is_error(result) && fiber->proc.state != PROC_DONE) {
                return result;
            }
        }
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
        if (fiber->proc.state == PROC_PARKED) continue;  // Skip parked fibers

        // Step the fiber
        Value* result = fiber_resume_internal(fiber, mk_nothing());
        if (is_error(result) && fiber->proc.state != PROC_DONE) {
            fiber_scheduler_running = 0;
            return result;
        }

        // If fiber not done and not parked, re-queue it
        if (fiber->proc.state != PROC_DONE && fiber->proc.state != PROC_PARKED) {
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
 * with-fibers: Scoped Fiber Execution
 * ============================================================
 *
 * (with-fibers body...)
 *
 * Creates a fiber scope where:
 * 1. Body expressions are evaluated
 * 2. Any fibers spawned during body execution are tracked
 * 3. All spawned fibers are run to completion
 * 4. Fiber resources are cleaned up
 * 5. Returns the value of the last body expression
 */
static void free_fiber_ctx(FiberContext* ctx) {
    if (!ctx) return;
    if (ctx->stack) free(ctx->stack);
    free(ctx);
}

static Value* eval_with_fibers(Value* args, Env* env) {
    // Save previous scheduler state
    Value* saved_run_queue = fiber_run_queue;
    int saved_scheduler_running = fiber_scheduler_running;

    // Start fresh scope
    fiber_run_queue = NULL;
    fiber_scheduler_running = 0;

    // Evaluate body expressions
    Value* result = mk_nothing();
    while (args && !is_nil(args)) {
        result = omni_eval(car(args), env);
        if (is_error(result)) {
            // On error, cleanup and restore
            // Free any remaining fibers in queue
            Value* q = fiber_run_queue;
            while (q && !is_nil(q)) {
                Value* f = car(q);
                if (f && f->tag == T_PROCESS && f->proc.fiber_ctx) {
                    free_fiber_ctx(f->proc.fiber_ctx);
                    f->proc.fiber_ctx = NULL;
                }
                q = cdr(q);
            }
            fiber_run_queue = saved_run_queue;
            fiber_scheduler_running = saved_scheduler_running;
            return result;
        }
        args = cdr(args);
    }

    // Run all spawned fibers to completion
    fiber_scheduler_running = 1;

    while (fiber_run_queue && !is_nil(fiber_run_queue)) {
        // Pop first fiber from queue
        Value* fiber = car(fiber_run_queue);
        fiber_run_queue = cdr(fiber_run_queue);

        if (!fiber || fiber->tag != T_PROCESS) continue;
        if (fiber->proc.state == PROC_DONE) {
            // Cleanup done fiber
            if (fiber->proc.fiber_ctx) {
                free_fiber_ctx(fiber->proc.fiber_ctx);
                fiber->proc.fiber_ctx = NULL;
            }
            continue;
        }
        if (fiber->proc.state == PROC_PARKED) continue;  // Skip parked fibers

        // Step the fiber
        Value* step_result = fiber_resume_internal(fiber, mk_nothing());
        if (is_error(step_result) && fiber->proc.state != PROC_DONE) {
            // Cleanup on error
            Value* q = fiber_run_queue;
            while (q && !is_nil(q)) {
                Value* f = car(q);
                if (f && f->tag == T_PROCESS && f->proc.fiber_ctx) {
                    free_fiber_ctx(f->proc.fiber_ctx);
                    f->proc.fiber_ctx = NULL;
                }
                q = cdr(q);
            }
            if (fiber->proc.fiber_ctx) {
                free_fiber_ctx(fiber->proc.fiber_ctx);
                fiber->proc.fiber_ctx = NULL;
            }
            fiber_run_queue = saved_run_queue;
            fiber_scheduler_running = saved_scheduler_running;
            return step_result;
        }

        // If fiber done, cleanup
        if (fiber->proc.state == PROC_DONE) {
            if (fiber->proc.fiber_ctx) {
                free_fiber_ctx(fiber->proc.fiber_ctx);
                fiber->proc.fiber_ctx = NULL;
            }
        } else if (fiber->proc.state != PROC_PARKED) {
            // Re-queue if not done and not parked
            if (!fiber_run_queue || is_nil(fiber_run_queue)) {
                fiber_run_queue = mk_cell(fiber, mk_nil());
            } else {
                // Append to end
                Value* tail = fiber_run_queue;
                while (cdr(tail) && !is_nil(cdr(tail))) {
                    tail = cdr(tail);
                }
                tail->cell.cdr = mk_cell(fiber, mk_nil());
            }
        }
    }

    // Restore previous state
    fiber_run_queue = saved_run_queue;
    fiber_scheduler_running = saved_scheduler_running;

    return result;
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

        // Wake up any waiting receiver
        if (ch->recv_waiters && !is_nil(ch->recv_waiters)) {
            Value* waiter = car(ch->recv_waiters);
            ch->recv_waiters = cdr(ch->recv_waiters);

            if (waiter && waiter->tag == T_PROCESS) {
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
        }

        return mk_sym("ok");
    }

    // Must wait - park current fiber and yield to scheduler
    if (fiber_current) {
        Value* this_fiber = fiber_current;  // Capture before yield
        FiberContext* ctx = this_fiber->proc.fiber_ctx;
        if (!ctx) return mk_error("send: missing fiber context");

        this_fiber->proc.state = PROC_PARKED;
        this_fiber->proc.park_value = val;
        // Add to send waiters as (fiber . value) pair
        Value* waiter = mk_cell(this_fiber, val);
        if (!ch->send_waiters) ch->send_waiters = mk_nil();
        ch->send_waiters = mk_cell(waiter, ch->send_waiters);

        // Actually yield to scheduler (use top of resumer stack)
        ctx->yield_value = mk_nothing();
        if (fiber_resumer_sp > 0) {
            swapcontext(&ctx->ctx, &fiber_resumer_stack[fiber_resumer_sp - 1]);
        }

        // Resumed! Return ok
        return mk_sym("ok");
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

    // Must wait - park current fiber and yield to scheduler
    if (fiber_current) {
        Value* this_fiber = fiber_current;  // Capture before yield
        FiberContext* ctx = this_fiber->proc.fiber_ctx;
        if (!ctx) return mk_error("recv: missing fiber context");

        this_fiber->proc.state = PROC_PARKED;
        if (!ch->recv_waiters) ch->recv_waiters = mk_nil();
        ch->recv_waiters = mk_cell(this_fiber, ch->recv_waiters);

        // Actually yield to scheduler (use top of resumer stack)
        ctx->yield_value = mk_nothing();
        if (fiber_resumer_sp > 0) {
            swapcontext(&ctx->ctx, &fiber_resumer_stack[fiber_resumer_sp - 1]);
        }

        // Resumed! Check if we got a direct handoff or need to read from buffer
        if (this_fiber->proc.park_value) {
            Value* val = this_fiber->proc.park_value;
            this_fiber->proc.park_value = NULL;  // Clear after use
            return val;
        }

        // No direct handoff, try buffer (sender put value in buffer then woke us)
        if (ch->capacity > 0 && ch->count > 0) {
            Value* val = ch->buffer[ch->head];
            ch->head = (ch->head + 1) % ch->capacity;
            ch->count--;
            return val;
        }

        // Channel was closed while we were waiting
        if (ch->closed) {
            return mk_nothing();
        }

        // Shouldn't reach here in normal operation
        return mk_nothing();
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

/* ============================================================
 * FFI Primitives
 * ============================================================ */

/*
 * (ffi/load "library.so")
 * Load a shared library and return a handle.
 */
static Value* prim_ffi_load(Value* args) {
    Value* name_arg = car(args);
    if (!name_arg) return mk_error("ffi/load: expected library name");

    const char* name;
    if (name_arg->tag == T_STRING) {
        name = name_arg->str.data;
    } else if (name_arg->tag == T_SYM) {
        name = name_arg->s;
    } else {
        return mk_error("ffi/load: expected string or symbol for library name");
    }

    // Try to load the library
    void* handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        char errbuf[512];
        snprintf(errbuf, sizeof(errbuf), "ffi/load: %s", dlerror());
        return mk_error(errbuf);
    }

    return mk_ffi_lib(handle, name);
}

/*
 * (ffi/close lib)
 * Close a library handle.
 */
static Value* prim_ffi_close(Value* args) {
    Value* lib = car(args);
    if (!lib || lib->tag != T_FFI_LIB) {
        return mk_error("ffi/close: expected ffi-lib");
    }

    if (lib->ffi_lib.handle) {
        dlclose(lib->ffi_lib.handle);
        lib->ffi_lib.handle = NULL;
    }

    return mk_nothing();
}

/*
 * (ffi/symbol lib "name")
 * Get a symbol from a library.
 */
static Value* prim_ffi_symbol(Value* args) {
    Value* lib = car(args);
    Value* name_arg = car(cdr(args));

    if (!lib || lib->tag != T_FFI_LIB) {
        return mk_error("ffi/symbol: first argument must be ffi-lib");
    }
    if (!lib->ffi_lib.handle) {
        return mk_error("ffi/symbol: library handle is closed");
    }

    const char* name;
    if (!name_arg) {
        return mk_error("ffi/symbol: expected symbol name");
    } else if (name_arg->tag == T_STRING) {
        name = name_arg->str.data;
    } else if (name_arg->tag == T_SYM) {
        name = name_arg->s;
    } else {
        return mk_error("ffi/symbol: expected string or symbol for name");
    }

    dlerror();  // Clear any existing error
    void* sym = dlsym(lib->ffi_lib.handle, name);
    char* err = dlerror();
    if (err) {
        char errbuf[512];
        snprintf(errbuf, sizeof(errbuf), "ffi/symbol: %s", err);
        return mk_error(errbuf);
    }

    return mk_ffi_ptr(sym, name, 0);
}

/*
 * (ffi/call ptr signature arg ...)
 * Call a foreign function with the given signature.
 *
 * Signature is a list like: (-> arg-types... return-type)
 * Supported types: 'int 'double 'void 'ptr 'string
 *
 * Example: (ffi/call sin-fn '(-> double double) 3.14159)
 */
static Value* prim_ffi_call(Value* args) {
    Value* fn_ptr = car(args);
    Value* sig = car(cdr(args));
    Value* call_args = cdr(cdr(args));

    if (!fn_ptr || fn_ptr->tag != T_FFI_PTR) {
        return mk_error("ffi/call: first argument must be ffi-ptr");
    }
    if (!fn_ptr->ffi_ptr.ptr) {
        return mk_error("ffi/call: null function pointer");
    }
    if (!sig || sig->tag != T_CELL) {
        return mk_error("ffi/call: expected signature list (-> arg-types return-type)");
    }

    // Parse signature: (-> arg-type... return-type)
    if (!car(sig) || car(sig)->tag != T_SYM || strcmp(car(sig)->s, "->") != 0) {
        return mk_error("ffi/call: signature must start with '->'");
    }

    // Count args and get return type
    Value* sig_rest = cdr(sig);
    int sig_len = 0;
    Value* sig_iter = sig_rest;
    while (!is_nil(sig_iter)) {
        sig_len++;
        sig_iter = cdr(sig_iter);
    }

    if (sig_len < 1) {
        return mk_error("ffi/call: signature must have at least a return type");
    }

    // Last element is return type
    Value* return_type = NULL;
    sig_iter = sig_rest;
    for (int i = 0; i < sig_len - 1; i++) {
        sig_iter = cdr(sig_iter);
    }
    return_type = car(sig_iter);

    // Collect argument types (all but last)
    int num_arg_types = sig_len - 1;

    // Validate return type
    if (!return_type || return_type->tag != T_SYM) {
        return mk_error("ffi/call: return type must be a symbol");
    }
    const char* ret_type_str = return_type->s;

    // Now we need to call the function. Without libffi, we handle common signatures
    // by casting the function pointer to the appropriate type.
    void* fn = fn_ptr->ffi_ptr.ptr;

    // Handle common function signatures
    if (num_arg_types == 0) {
        // No arguments
        if (strcmp(ret_type_str, "void") == 0) {
            ((void (*)(void))fn)();
            return mk_nothing();
        } else if (strcmp(ret_type_str, "int") == 0) {
            int result = ((int (*)(void))fn)();
            return mk_int(result);
        } else if (strcmp(ret_type_str, "double") == 0) {
            double result = ((double (*)(void))fn)();
            return mk_float(result);
        } else if (strcmp(ret_type_str, "ptr") == 0) {
            void* result = ((void* (*)(void))fn)();
            return mk_ffi_ptr(result, NULL, 0);
        }
    } else if (num_arg_types == 1) {
        // One argument
        Value* arg1_type = car(sig_rest);
        if (!arg1_type || arg1_type->tag != T_SYM) {
            return mk_error("ffi/call: argument type must be a symbol");
        }
        Value* arg1 = car(call_args);

        // Convert argument
        if (strcmp(arg1_type->s, "double") == 0) {
            double a1 = 0.0;
            if (arg1 && arg1->tag == T_FLOAT) a1 = arg1->f;
            else if (arg1 && arg1->tag == T_INT) a1 = (double)arg1->i;
            else return mk_error("ffi/call: expected numeric argument for double");

            if (strcmp(ret_type_str, "double") == 0) {
                double result = ((double (*)(double))fn)(a1);
                return mk_float(result);
            } else if (strcmp(ret_type_str, "int") == 0) {
                int result = ((int (*)(double))fn)(a1);
                return mk_int(result);
            } else if (strcmp(ret_type_str, "void") == 0) {
                ((void (*)(double))fn)(a1);
                return mk_nothing();
            }
        } else if (strcmp(arg1_type->s, "int") == 0) {
            long a1 = 0;
            if (arg1 && arg1->tag == T_INT) a1 = arg1->i;
            else if (arg1 && arg1->tag == T_FLOAT) a1 = (long)arg1->f;
            else return mk_error("ffi/call: expected numeric argument for int");

            if (strcmp(ret_type_str, "int") == 0) {
                int result = ((int (*)(int))fn)((int)a1);
                return mk_int(result);
            } else if (strcmp(ret_type_str, "double") == 0) {
                double result = ((double (*)(int))fn)((int)a1);
                return mk_float(result);
            } else if (strcmp(ret_type_str, "void") == 0) {
                ((void (*)(int))fn)((int)a1);
                return mk_nothing();
            } else if (strcmp(ret_type_str, "ptr") == 0) {
                void* result = ((void* (*)(int))fn)((int)a1);
                return mk_ffi_ptr(result, NULL, 0);
            }
        } else if (strcmp(arg1_type->s, "ptr") == 0) {
            void* a1 = NULL;
            if (arg1 && arg1->tag == T_FFI_PTR) a1 = arg1->ffi_ptr.ptr;
            else if (is_nil(arg1) || is_nothing(arg1)) a1 = NULL;
            else return mk_error("ffi/call: expected ffi-ptr for ptr argument");

            if (strcmp(ret_type_str, "void") == 0) {
                ((void (*)(void*))fn)(a1);
                return mk_nothing();
            } else if (strcmp(ret_type_str, "int") == 0) {
                int result = ((int (*)(void*))fn)(a1);
                return mk_int(result);
            } else if (strcmp(ret_type_str, "ptr") == 0) {
                void* result = ((void* (*)(void*))fn)(a1);
                return mk_ffi_ptr(result, NULL, 0);
            }
        } else if (strcmp(arg1_type->s, "string") == 0) {
            const char* a1 = "";
            if (arg1 && arg1->tag == T_STRING) a1 = arg1->str.data;
            else if (arg1 && arg1->tag == T_SYM) a1 = arg1->s;
            else return mk_error("ffi/call: expected string argument");

            if (strcmp(ret_type_str, "int") == 0) {
                int result = ((int (*)(const char*))fn)(a1);
                return mk_int(result);
            } else if (strcmp(ret_type_str, "void") == 0) {
                ((void (*)(const char*))fn)(a1);
                return mk_nothing();
            } else if (strcmp(ret_type_str, "ptr") == 0) {
                void* result = ((void* (*)(const char*))fn)(a1);
                return mk_ffi_ptr(result, NULL, 0);
            }
        }
    } else if (num_arg_types == 2) {
        // Two arguments
        Value* arg1_type = car(sig_rest);
        Value* arg2_type = car(cdr(sig_rest));
        Value* arg1 = car(call_args);
        Value* arg2 = car(cdr(call_args));

        if (!arg1_type || arg1_type->tag != T_SYM ||
            !arg2_type || arg2_type->tag != T_SYM) {
            return mk_error("ffi/call: argument types must be symbols");
        }

        // Handle common two-argument signatures
        if (strcmp(arg1_type->s, "double") == 0 && strcmp(arg2_type->s, "double") == 0) {
            double a1 = arg1 && is_numeric(arg1) ? to_double(arg1) : 0.0;
            double a2 = arg2 && is_numeric(arg2) ? to_double(arg2) : 0.0;

            if (strcmp(ret_type_str, "double") == 0) {
                double result = ((double (*)(double, double))fn)(a1, a2);
                return mk_float(result);
            } else if (strcmp(ret_type_str, "int") == 0) {
                int result = ((int (*)(double, double))fn)(a1, a2);
                return mk_int(result);
            }
        } else if (strcmp(arg1_type->s, "int") == 0 && strcmp(arg2_type->s, "int") == 0) {
            long a1 = arg1 && arg1->tag == T_INT ? arg1->i : 0;
            long a2 = arg2 && arg2->tag == T_INT ? arg2->i : 0;

            if (strcmp(ret_type_str, "int") == 0) {
                int result = ((int (*)(int, int))fn)((int)a1, (int)a2);
                return mk_int(result);
            } else if (strcmp(ret_type_str, "ptr") == 0) {
                void* result = ((void* (*)(int, int))fn)((int)a1, (int)a2);
                return mk_ffi_ptr(result, NULL, 0);
            }
        } else if (strcmp(arg1_type->s, "ptr") == 0 && strcmp(arg2_type->s, "int") == 0) {
            void* a1 = arg1 && arg1->tag == T_FFI_PTR ? arg1->ffi_ptr.ptr : NULL;
            long a2 = arg2 && arg2->tag == T_INT ? arg2->i : 0;

            if (strcmp(ret_type_str, "int") == 0) {
                int result = ((int (*)(void*, int))fn)(a1, (int)a2);
                return mk_int(result);
            } else if (strcmp(ret_type_str, "ptr") == 0) {
                void* result = ((void* (*)(void*, int))fn)(a1, (int)a2);
                return mk_ffi_ptr(result, NULL, 0);
            }
        } else if (strcmp(arg1_type->s, "string") == 0 && strcmp(arg2_type->s, "string") == 0) {
            const char* a1 = arg1 && arg1->tag == T_STRING ? arg1->str.data : "";
            const char* a2 = arg2 && arg2->tag == T_STRING ? arg2->str.data : "";

            if (strcmp(ret_type_str, "int") == 0) {
                int result = ((int (*)(const char*, const char*))fn)(a1, a2);
                return mk_int(result);
            } else if (strcmp(ret_type_str, "ptr") == 0) {
                void* result = ((void* (*)(const char*, const char*))fn)(a1, a2);
                return mk_ffi_ptr(result, NULL, 0);
            }
        }
    }

    return mk_error("ffi/call: unsupported signature (use libffi for more complex signatures)");
}

/*
 * (ffi/lib? x)
 * Returns true if x is an FFI library handle.
 */
static Value* prim_ffi_lib_q(Value* args) {
    Value* v = car(args);
    return mk_sym((v && v->tag == T_FFI_LIB) ? "true" : "false");
}

/*
 * (ffi/ptr? x)
 * Returns true if x is an FFI pointer.
 */
static Value* prim_ffi_ptr_q(Value* args) {
    Value* v = car(args);
    return mk_sym((v && v->tag == T_FFI_PTR) ? "true" : "false");
}

/*
 * (ffi/null? ptr)
 * Returns true if the pointer is null.
 */
static Value* prim_ffi_null_q(Value* args) {
    Value* v = car(args);
    if (!v) return mk_sym("true");
    if (v->tag == T_FFI_PTR) {
        return mk_sym(v->ffi_ptr.ptr == NULL ? "true" : "false");
    }
    return mk_sym("false");
}

/*
 * (ffi/null)
 * Returns a null pointer.
 */
static Value* prim_ffi_null(Value* args) {
    (void)args;
    return mk_ffi_ptr(NULL, "null", 0);
}

/*
 * (ffi/ptr-address ptr)
 * Returns the raw address as an integer (for debugging).
 */
static Value* prim_ffi_ptr_address(Value* args) {
    Value* v = car(args);
    if (!v || v->tag != T_FFI_PTR) {
        return mk_error("ffi/ptr-address: expected ffi-ptr");
    }
    return mk_int((long)(intptr_t)v->ffi_ptr.ptr);
}

/*
 * (ffi/malloc size)
 * Allocate memory via malloc.
 */
static Value* prim_ffi_malloc(Value* args) {
    Value* size_arg = car(args);
    if (!size_arg || size_arg->tag != T_INT) {
        return mk_error("ffi/malloc: expected integer size");
    }

    void* ptr = malloc((size_t)size_arg->i);
    if (!ptr) {
        return mk_error("ffi/malloc: allocation failed");
    }

    return mk_ffi_ptr(ptr, "malloc", 1);  // owned = 1
}

/*
 * (ffi/free ptr)
 * Free memory allocated by ffi/malloc.
 */
static Value* prim_ffi_free(Value* args) {
    Value* ptr_arg = car(args);
    if (!ptr_arg || ptr_arg->tag != T_FFI_PTR) {
        return mk_error("ffi/free: expected ffi-ptr");
    }

    if (ptr_arg->ffi_ptr.ptr) {
        free(ptr_arg->ffi_ptr.ptr);
        ptr_arg->ffi_ptr.ptr = NULL;
        ptr_arg->ffi_ptr.owned = 0;
    }

    return mk_nothing();
}

/*
 * (ffi/ptr-read-int ptr)
 * Read an int from a pointer location.
 */
static Value* prim_ffi_ptr_read_int(Value* args) {
    Value* ptr_arg = car(args);
    if (!ptr_arg || ptr_arg->tag != T_FFI_PTR) {
        return mk_error("ffi/ptr-read-int: expected ffi-ptr");
    }
    if (!ptr_arg->ffi_ptr.ptr) {
        return mk_error("ffi/ptr-read-int: null pointer");
    }

    int value = *((int*)ptr_arg->ffi_ptr.ptr);
    return mk_int(value);
}

/*
 * (ffi/ptr-write-int ptr value)
 * Write an int to a pointer location.
 */
static Value* prim_ffi_ptr_write_int(Value* args) {
    Value* ptr_arg = car(args);
    Value* val_arg = car(cdr(args));

    if (!ptr_arg || ptr_arg->tag != T_FFI_PTR) {
        return mk_error("ffi/ptr-write-int: expected ffi-ptr");
    }
    if (!ptr_arg->ffi_ptr.ptr) {
        return mk_error("ffi/ptr-write-int: null pointer");
    }
    if (!val_arg || val_arg->tag != T_INT) {
        return mk_error("ffi/ptr-write-int: expected integer value");
    }

    *((int*)ptr_arg->ffi_ptr.ptr) = (int)val_arg->i;
    return mk_nothing();
}

/*
 * (ffi/ptr-read-double ptr)
 * Read a double from a pointer location.
 */
static Value* prim_ffi_ptr_read_double(Value* args) {
    Value* ptr_arg = car(args);
    if (!ptr_arg || ptr_arg->tag != T_FFI_PTR) {
        return mk_error("ffi/ptr-read-double: expected ffi-ptr");
    }
    if (!ptr_arg->ffi_ptr.ptr) {
        return mk_error("ffi/ptr-read-double: null pointer");
    }

    double value = *((double*)ptr_arg->ffi_ptr.ptr);
    return mk_float(value);
}

/*
 * (ffi/ptr-write-double ptr value)
 * Write a double to a pointer location.
 */
static Value* prim_ffi_ptr_write_double(Value* args) {
    Value* ptr_arg = car(args);
    Value* val_arg = car(cdr(args));

    if (!ptr_arg || ptr_arg->tag != T_FFI_PTR) {
        return mk_error("ffi/ptr-write-double: expected ffi-ptr");
    }
    if (!ptr_arg->ffi_ptr.ptr) {
        return mk_error("ffi/ptr-write-double: null pointer");
    }
    if (!val_arg || !is_numeric(val_arg)) {
        return mk_error("ffi/ptr-write-double: expected numeric value");
    }

    *((double*)ptr_arg->ffi_ptr.ptr) = to_double(val_arg);
    return mk_nothing();
}

/*
 * (ffi/ptr-read-string ptr)
 * Read a null-terminated string from a pointer.
 */
static Value* prim_ffi_ptr_read_string(Value* args) {
    Value* ptr_arg = car(args);
    if (!ptr_arg || ptr_arg->tag != T_FFI_PTR) {
        return mk_error("ffi/ptr-read-string: expected ffi-ptr");
    }
    if (!ptr_arg->ffi_ptr.ptr) {
        return mk_error("ffi/ptr-read-string: null pointer");
    }

    const char* str = (const char*)ptr_arg->ffi_ptr.ptr;
    return mk_string_cstr(str);
}

/* ============================================================
 * Metadata Primitives
 * ============================================================ */

/*
 * (meta x)
 * Returns the metadata attached to x, or nil if none.
 */
static Value* prim_meta(Value* args) {
    Value* v = car(args);
    return metadata_get(v);
}

/*
 * (set-meta! x meta)
 * Sets the metadata of x to meta. Returns x.
 */
static Value* prim_set_meta(Value* args) {
    Value* v = car(args);
    Value* meta = car(cdr(args));
    if (!v) return mk_error("set-meta!: expected value");
    metadata_set(v, meta);
    return v;
}

/*
 * (has-meta? x key)
 * Returns true if x has metadata with the given key (for dict metadata).
 */
static Value* prim_has_meta_q(Value* args) {
    Value* v = car(args);
    Value* key = car(cdr(args));
    Value* meta = metadata_get(v);

    if (is_nil(meta)) return mk_sym("false");

    // If metadata is a dict (T_CELL with :__dict__ marker), check for key
    if (meta->tag == T_CELL) {
        Value* iter = meta;
        while (!is_nil(iter)) {
            Value* entry = car(iter);
            if (entry && entry->tag == T_CELL) {
                Value* k = car(entry);
                // Compare keys (symbols)
                if (k && k->tag == T_SYM && key && key->tag == T_SYM) {
                    if (strcmp(k->s, key->s) == 0) {
                        return mk_sym("true");
                    }
                }
            }
            iter = cdr(iter);
        }
    }

    // If metadata is a single symbol (like :private), compare directly
    if (meta->tag == T_SYM && key && key->tag == T_SYM) {
        return mk_sym(strcmp(meta->s, key->s) == 0 ? "true" : "false");
    }

    return mk_sym("false");
}

/*
 * (get-meta x key [default])
 * Gets a specific metadata key from x's metadata dict.
 */
static Value* prim_get_meta(Value* args) {
    Value* v = car(args);
    Value* key = car(cdr(args));
    Value* def = car(cdr(cdr(args)));
    Value* meta = metadata_get(v);

    if (is_nil(meta)) return def ? def : mk_nil();

    // If metadata is a dict, search for key
    if (meta->tag == T_CELL) {
        Value* iter = meta;
        while (!is_nil(iter)) {
            Value* entry = car(iter);
            if (entry && entry->tag == T_CELL) {
                Value* k = car(entry);
                if (k && k->tag == T_SYM && key && key->tag == T_SYM) {
                    if (strcmp(k->s, key->s) == 0) {
                        return cdr(entry) ? car(cdr(entry)) : mk_sym("true");
                    }
                }
            }
            iter = cdr(iter);
        }
    }

    // If metadata is a single symbol matching key, return true
    if (meta->tag == T_SYM && key && key->tag == T_SYM) {
        if (strcmp(meta->s, key->s) == 0) {
            return mk_sym("true");
        }
    }

    return def ? def : mk_nil();
}

/*
 * (private? x)
 * Returns true if x has ^:private metadata.
 */
static Value* prim_private_q(Value* args) {
    Value* v = car(args);
    Value* meta = metadata_get(v);

    if (is_nil(meta)) return mk_sym("false");

    // Check if meta is :private symbol
    if (meta->tag == T_SYM && strcmp(meta->s, "private") == 0) {
        return mk_sym("true");
    }

    return mk_sym("false");
}

/*
 * (deprecated? x)
 * Returns true if x has ^:deprecated metadata.
 */
static Value* prim_deprecated_q(Value* args) {
    Value* v = car(args);
    Value* meta = metadata_get(v);

    if (is_nil(meta)) return mk_sym("false");

    // Check if meta is :deprecated symbol
    if (meta->tag == T_SYM && strcmp(meta->s, "deprecated") == 0) {
        return mk_sym("true");
    }

    return mk_sym("false");
}

/*
 * (doc x)
 * Returns the docstring from x's metadata, or nil if none.
 */
static Value* prim_doc(Value* args) {
    Value* v = car(args);
    Value* meta = metadata_get(v);

    if (is_nil(meta)) return mk_nil();

    // If meta is a string, it's the docstring
    if (meta->tag == T_STRING) {
        return meta;
    }

    return mk_nil();
}

// ============================================================
// Box Primitives (mutable reference cells)
// ============================================================

/*
 * (box value) -> box
 * Creates a mutable reference cell containing value.
 */
static Value* prim_box(Value* args) {
    Value* val = car(args);
    return mk_box(val);
}

/*
 * (box? x) -> bool
 * Returns true if x is a box.
 */
static Value* prim_box_q(Value* args) {
    return is_box(car(args)) ? mk_sym("true") : mk_sym("false");
}

/*
 * (box-ref box) -> value
 * Returns the current value in the box.
 */
static Value* prim_box_ref(Value* args) {
    Value* b = car(args);
    if (!is_box(b)) {
        return mk_error("box-ref: expected box");
    }
    Value* v = box_get(b);
    return v ? v : mk_nil();
}

/*
 * (box-set! box value) -> value
 * Sets the box to contain value, returns value.
 */
static Value* prim_box_set(Value* args) {
    Value* b = car(args);
    Value* val = car(cdr(args));
    if (!is_box(b)) {
        return mk_error("box-set!: expected box");
    }
    box_set(b, val);
    return val;
}

// ============================================================
// Thread Primitives
// ============================================================

// Thread entry point - wrapper function for pthread_create
typedef struct ThreadArgs {
    Value* thunk;
    Value* thread_val;
} ThreadArgs;

static void* thread_entry(void* arg) {
    ThreadArgs* ta = (ThreadArgs*)arg;
    Value* thunk = ta->thunk;
    Value* thread_val = ta->thread_val;
    free(ta);

    // Call the thunk with no arguments
    Value* result = omni_apply(thunk, mk_nil(), global_env);

    // Store result and mark as done
    thread_val->thread.result = result;
    thread_val->thread.done = 1;

    return NULL;
}

/*
 * (thread thunk) -> thread
 * Creates a new system thread that will execute thunk.
 * The thread starts immediately upon creation.
 */
static Value* prim_thread(Value* args) {
    Value* thunk = car(args);
    if (!thunk || (thunk->tag != T_LAMBDA && thunk->tag != T_PRIM)) {
        return mk_error("thread: expected function");
    }

    Value* t = mk_thread(thunk);
    if (!t) return mk_error("thread: allocation failed");

    // Allocate thread args
    ThreadArgs* ta = malloc(sizeof(ThreadArgs));
    if (!ta) return mk_error("thread: allocation failed");
    ta->thunk = thunk;
    ta->thread_val = t;

    // Create and start the thread
    int err = pthread_create(&t->thread.tid, NULL, thread_entry, ta);
    if (err != 0) {
        free(ta);
        return mk_error("thread: pthread_create failed");
    }
    t->thread.started = 1;

    return t;
}

/*
 * (thread-join t) -> result
 * Waits for thread t to complete and returns its result.
 */
static Value* prim_thread_join(Value* args) {
    Value* t = car(args);
    if (!is_thread(t)) {
        return mk_error("thread-join: expected thread");
    }

    if (!t->thread.started) {
        return mk_error("thread-join: thread not started");
    }

    if (t->thread.joined) {
        // Already joined, just return result
        return t->thread.result ? t->thread.result : mk_nil();
    }

    // Wait for thread to complete
    pthread_join(t->thread.tid, NULL);
    t->thread.joined = 1;

    return t->thread.result ? t->thread.result : mk_nil();
}

/*
 * (thread? x) -> bool
 * Returns true if x is a thread.
 */
static Value* prim_thread_q(Value* args) {
    return is_thread(car(args)) ? mk_sym("true") : mk_sym("false");
}

/*
 * (thread-done? t) -> bool
 * Returns true if thread t has finished executing.
 */
static Value* prim_thread_done_q(Value* args) {
    Value* t = car(args);
    if (!is_thread(t)) {
        return mk_error("thread-done?: expected thread");
    }
    return t->thread.done ? mk_sym("true") : mk_sym("false");
}

/*
 * (thread-result t) -> result
 * Returns the result of a completed thread without blocking.
 * Returns nothing if thread is not done.
 */
static Value* prim_thread_result(Value* args) {
    Value* t = car(args);
    if (!is_thread(t)) {
        return mk_error("thread-result: expected thread");
    }
    if (!t->thread.done) {
        return mk_nothing();
    }
    return t->thread.result ? t->thread.result : mk_nil();
}

// ============================================================
// Atomic Operations (using boxes for simplicity)
// ============================================================

// Simple mutex for atomic operations
static pthread_mutex_t atomic_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * (atomic-ref box) -> value
 * Atomically reads the value in a box.
 */
static Value* prim_atomic_ref(Value* args) {
    Value* box = car(args);
    if (!is_box(box)) {
        return mk_error("atomic-ref: expected box");
    }

    pthread_mutex_lock(&atomic_mutex);
    Value* result = box_get(box);
    pthread_mutex_unlock(&atomic_mutex);

    return result ? result : mk_nil();
}

/*
 * (atomic-set! box value) -> value
 * Atomically sets the value in a box.
 */
static Value* prim_atomic_set(Value* args) {
    Value* box = car(args);
    Value* val = car(cdr(args));
    if (!is_box(box)) {
        return mk_error("atomic-set!: expected box");
    }

    pthread_mutex_lock(&atomic_mutex);
    box_set(box, val);
    pthread_mutex_unlock(&atomic_mutex);

    return val;
}

/*
 * (atomic-cas! box expected new) -> bool
 * Atomically compares box contents with expected value,
 * and if equal, sets to new value. Returns true if swap occurred.
 */
static Value* prim_atomic_cas(Value* args) {
    Value* box = car(args);
    Value* expected = car(cdr(args));
    Value* new_val = car(cdr(cdr(args)));

    if (!is_box(box)) {
        return mk_error("atomic-cas!: expected box");
    }

    pthread_mutex_lock(&atomic_mutex);
    Value* current = box_get(box);

    // Simple equality check (pointer equality for now, value equality for ints/floats)
    int eq = 0;
    if (current == expected) {
        eq = 1;
    } else if (current && expected && current->tag == T_INT && expected->tag == T_INT) {
        eq = (current->i == expected->i);
    } else if (current && expected && current->tag == T_FLOAT && expected->tag == T_FLOAT) {
        eq = (current->f == expected->f);
    } else if (current && expected && current->tag == T_SYM && expected->tag == T_SYM) {
        eq = (strcmp(current->s, expected->s) == 0);
    }

    if (eq) {
        box_set(box, new_val);
    }
    pthread_mutex_unlock(&atomic_mutex);

    return eq ? mk_sym("true") : mk_sym("false");
}

/*
 * (atomic-swap! box fn) -> new-value
 * Atomically applies fn to the box contents and stores the result.
 * Returns the new value.
 */
static Value* prim_atomic_swap(Value* args) {
    Value* box = car(args);
    Value* fn = car(cdr(args));

    if (!is_box(box)) {
        return mk_error("atomic-swap!: expected box");
    }
    if (!fn || (fn->tag != T_LAMBDA && fn->tag != T_PRIM)) {
        return mk_error("atomic-swap!: expected function");
    }

    pthread_mutex_lock(&atomic_mutex);
    Value* current = box_get(box);
    Value* new_val = omni_apply(fn, mk_cell(current, mk_nil()), global_env);
    if (!is_error(new_val)) {
        box_set(box, new_val);
    }
    pthread_mutex_unlock(&atomic_mutex);

    return new_val;
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
    register_primitive("error", 1, prim_error);
    register_primitive("empty?", 1, prim_empty);
    register_primitive("list", -1, prim_list);

    // String primitives
    register_primitive("string?", 1, prim_string_q);
    register_primitive("char?", 1, prim_char_q);
    register_primitive("symbol?", 1, prim_symbol_q);
    register_primitive("string-length", 1, prim_string_length);
    register_primitive("string-append", -1, prim_string_append);
    register_primitive("str", -1, prim_str);  // String interpolation helper
    register_primitive("string-ref", 2, prim_string_ref);
    register_primitive("substring", 3, prim_substring);
    register_primitive("string->list", 1, prim_string_to_list);
    register_primitive("list->string", 1, prim_list_to_string);

    // Math primitives
    register_primitive("float?", 1, prim_float_q);
    register_primitive("int?", 1, prim_int_q);
    register_primitive("number?", 1, prim_number_q);
    register_primitive("sin", 1, prim_sin);
    register_primitive("cos", 1, prim_cos);
    register_primitive("tan", 1, prim_tan);
    register_primitive("asin", 1, prim_asin);
    register_primitive("acos", 1, prim_acos);
    register_primitive("atan", 1, prim_atan);
    register_primitive("atan2", 2, prim_atan2);
    register_primitive("exp", 1, prim_exp);
    register_primitive("log", 1, prim_log);
    register_primitive("log10", 1, prim_log10);
    register_primitive("sqrt", 1, prim_sqrt);
    register_primitive("pow", 2, prim_pow);
    register_primitive("abs", 1, prim_abs);
    register_primitive("floor", 1, prim_floor);
    register_primitive("ceil", 1, prim_ceil);
    register_primitive("round", 1, prim_round);
    register_primitive("truncate", 1, prim_truncate);
    register_primitive("->int", 1, prim_to_int);
    register_primitive("->float", 1, prim_to_float);
    // Math constants (pi, e, inf, nan) are defined as direct values below
    register_primitive("nan?", 1, prim_nan_q);
    register_primitive("inf?", 1, prim_inf_q);

    register_primitive("print", -1, prim_print);
    register_primitive("println", -1, prim_println);
    register_primitive("range", -1, prim_range);
    register_primitive("reduce", 3, prim_reduce);
    register_primitive("map", 2, prim_map);
    register_primitive("filter", 2, prim_filter);
    register_primitive("length", 1, prim_length);

    // Array primitives
    register_primitive("array?", 1, prim_array_q);
    register_primitive("array-length", 1, prim_array_length);
    register_primitive("array-ref", 2, prim_array_ref);
    register_primitive("array-set!", 3, prim_array_set);
    register_primitive("array-slice", -1, prim_array_slice);

    // Tuples and named-tuples removed - use lists and dicts

    // Functional primitives
    register_primitive("partial", -1, prim_partial);
    register_primitive("partial?", 1, prim_partial_q);
    register_primitive("compose", 2, prim_compose);

    // Dict primitives
    register_primitive("dict?", 1, prim_dict_q);
    register_primitive("dict-ref", 2, prim_dict_ref);
    register_primitive("dict-set!", 3, prim_dict_set);
    register_primitive("dict-contains?", 2, prim_dict_contains);
    register_primitive("keys", 1, prim_keys);
    register_primitive("values", 1, prim_values);

    // Generic accessor (works on arrays, dicts, strings)
    register_primitive("get", 2, prim_get);

    // File I/O primitives
    register_primitive("open", 2, prim_open);
    register_primitive("close", 1, prim_close);
    register_primitive("read-line", 1, prim_read_line);
    register_primitive("read-all", 1, prim_read_all);
    register_primitive("write-string", 2, prim_write_string);
    register_primitive("write-line", 2, prim_write_line);
    register_primitive("flush", 1, prim_flush);
    register_primitive("port?", 1, prim_port_q);
    register_primitive("eof?", 1, prim_eof_q);
    register_primitive("file-exists?", 1, prim_file_exists);

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

    // FFI primitives
    register_primitive("ffi/load", 1, prim_ffi_load);
    register_primitive("ffi/close", 1, prim_ffi_close);
    register_primitive("ffi/symbol", 2, prim_ffi_symbol);
    register_primitive("ffi/call", -1, prim_ffi_call);
    register_primitive("ffi/lib?", 1, prim_ffi_lib_q);
    register_primitive("ffi/ptr?", 1, prim_ffi_ptr_q);
    register_primitive("ffi/null?", 1, prim_ffi_null_q);
    register_primitive("ffi/null", 0, prim_ffi_null);
    register_primitive("ffi/ptr-address", 1, prim_ffi_ptr_address);
    register_primitive("ffi/malloc", 1, prim_ffi_malloc);
    register_primitive("ffi/free", 1, prim_ffi_free);
    register_primitive("ffi/ptr-read-int", 1, prim_ffi_ptr_read_int);
    register_primitive("ffi/ptr-write-int", 2, prim_ffi_ptr_write_int);
    register_primitive("ffi/ptr-read-double", 1, prim_ffi_ptr_read_double);
    register_primitive("ffi/ptr-write-double", 2, prim_ffi_ptr_write_double);
    register_primitive("ffi/ptr-read-string", 1, prim_ffi_ptr_read_string);

    // Metadata primitives
    register_primitive("meta", 1, prim_meta);
    register_primitive("set-meta!", 2, prim_set_meta);
    register_primitive("has-meta?", 2, prim_has_meta_q);
    register_primitive("get-meta", -1, prim_get_meta);
    register_primitive("private?", 1, prim_private_q);
    register_primitive("deprecated?", 1, prim_deprecated_q);
    register_primitive("doc", 1, prim_doc);

    // Box primitives (mutable reference cells)
    register_primitive("box", 1, prim_box);
    register_primitive("box?", 1, prim_box_q);
    register_primitive("box-ref", 1, prim_box_ref);
    register_primitive("box-set!", 2, prim_box_set);

    // Thread primitives
    register_primitive("thread", 1, prim_thread);
    register_primitive("thread-join", 1, prim_thread_join);
    register_primitive("thread?", 1, prim_thread_q);
    register_primitive("thread-done?", 1, prim_thread_done_q);
    register_primitive("thread-result", 1, prim_thread_result);

    // Atomic operations
    register_primitive("atomic-ref", 1, prim_atomic_ref);
    register_primitive("atomic-set!", 2, prim_atomic_set);
    register_primitive("atomic-cas!", 3, prim_atomic_cas);
    register_primitive("atomic-swap!", 2, prim_atomic_swap);

    // Define primitive functions in environment
    for (int i = 0; i < num_primitives; i++) {
        // Create wrapper lambda for primitives
        // This is a hack - proper implementation would have a different mechanism
        Value* prim_val = mk_prim(NULL);
        // Store index in prim for lookup
        prim_val->prim = (PrimFn)(intptr_t)i;
        env_define(global_env, mk_sym(primitives[i].name), prim_val);
    }

    // Define math constants as direct float values (not functions)
    env_define(global_env, mk_sym("pi"), mk_float(3.14159265358979323846));
    env_define(global_env, mk_sym("e"), mk_float(2.71828182845904523536));
    env_define(global_env, mk_sym("inf"), mk_float(1.0 / 0.0));  // +Infinity
    env_define(global_env, mk_sym("nan"), mk_float(0.0 / 0.0));  // NaN

    return global_env;
}

// Fill in default values for missing arguments
// Returns a new args list with defaults filled in
// Helper: check if a value is a keyword symbol (starts with :)
static int is_keyword_sym(Value* v) {
    return v && v->tag == T_SYM && v->s[0] == ':';
}

// Helper: get the param name from a keyword arg (:foo) -> "foo"
static const char* get_keyword_name(Value* v) {
    if (!is_keyword_sym(v)) return NULL;
    return v->s + 1;  // Skip the :
}

// Helper: extract keyword args from args list
// Returns a new list of positional args, and stores keyword args in *kw_out
// Keyword args are in pairs: :name value :name2 value2 ...
static Value* extract_keyword_args(Value* args, Value** kw_out) {
    Value* positional = mk_nil();
    Value** pos_tail = &positional;
    Value* keywords = mk_nil();
    Value** kw_tail = &keywords;

    while (!is_nil(args)) {
        Value* arg = car(args);

        // Check if this is a keyword (symbol like :foo)
        if (is_keyword_sym(arg)) {
            // Get the keyword name and value
            args = cdr(args);
            if (is_nil(args)) break;  // No value for keyword

            Value* kw_val = car(args);
            args = cdr(args);

            // Store as (name . value) - extract name from :name symbol
            Value* name_sym = mk_sym(get_keyword_name(arg));
            Value* pair = mk_cell(name_sym, kw_val);  // (name . value)
            *kw_tail = mk_cell(pair, mk_nil());
            kw_tail = &((*kw_tail)->cell.cdr);
        } else {
            // Positional arg
            *pos_tail = mk_cell(arg, mk_nil());
            pos_tail = &((*pos_tail)->cell.cdr);
            args = cdr(args);
        }
    }

    *kw_out = keywords;
    return positional;
}

// Helper: bind keyword args to environment by matching param names
static void bind_keyword_args(Env* env, Value* params, Value* kw_args) {
    while (!is_nil(kw_args)) {
        Value* pair = car(kw_args);
        Value* name = car(pair);
        Value* value = cdr(pair);

        // Check if this keyword matches a parameter
        Value* p = params;
        while (!is_nil(p)) {
            Value* param = car(p);
            if (param && param->tag == T_SYM && name && name->tag == T_SYM) {
                if (strcmp(param->s, name->s) == 0) {
                    // Match found - bind the value
                    env_define(env, name, value);
                    break;
                }
            }
            p = cdr(p);
        }

        kw_args = cdr(kw_args);
    }
}

static Value* fill_defaults(Value* params, Value* args, Value* defaults, Env* def_env) {
    Value* result = mk_nil();
    Value** tail = &result;

    while (!is_nil(params)) {
        Value* param = car(params);
        Value* default_expr = car(defaults);

        // Handle rest parameter ..
        if (param && param->tag == T_SYM && strcmp(param->s, "..") == 0) {
            // Rest params get remaining args (no defaults apply)
            *tail = args;
            break;
        }

        if (!is_nil(args)) {
            // Argument provided - use it
            *tail = mk_cell(car(args), mk_nil());
            tail = &((*tail)->cell.cdr);
            args = cdr(args);
        } else if (!is_nothing(default_expr)) {
            // No argument but has default - evaluate default in definition env
            Value* default_val = omni_eval(default_expr, def_env);
            if (is_error(default_val)) return default_val;
            *tail = mk_cell(default_val, mk_nil());
            tail = &((*tail)->cell.cdr);
        } else {
            // No argument and no default - this is an error, but let env_extend handle it
            break;
        }

        params = cdr(params);
        defaults = cdr(defaults);
    }

    return result;
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

        // Extract keyword arguments from args
        Value* kw_args = mk_nil();
        Value* positional_args = extract_keyword_args(args, &kw_args);

        // Fill in defaults for missing positional arguments
        Value* effective_args = positional_args;
        if (fn->lam.defaults) {
            effective_args = fill_defaults(fn->lam.params, positional_args, fn->lam.defaults, closure_env);
            if (is_error(effective_args)) {
                call_stack_pop();
                return effective_args;
            }
        }

        Env* call_env = env_extend(closure_env, fn->lam.params, effective_args);
        if (!call_env) {
            call_stack_pop();
            return mk_error("OOM in apply");
        }

        // Bind keyword arguments (overrides positional and defaults)
        if (!is_nil(kw_args)) {
            bind_keyword_args(call_env, fn->lam.params, kw_args);
        }

        Value* result = omni_eval(fn->lam.body, call_env);
        call_stack_pop();
        return result;
    }

    // Partial application: (__partial__ fn captured_args)
    if (fn->tag == T_CELL && car(fn) && car(fn)->tag == T_SYM &&
        strcmp(car(fn)->s, "__partial__") == 0) {
        Value* rest = cdr(fn);
        Value* inner_fn = car(rest);
        Value* captured = car(cdr(rest));

        // Concatenate captured args with new args
        Value* all_args = mk_nil();
        Value** tail = &all_args;

        // Add captured args first
        while (!is_nil(captured)) {
            *tail = mk_cell(car(captured), mk_nil());
            tail = &((*tail)->cell.cdr);
            captured = cdr(captured);
        }

        // Add new args
        while (!is_nil(args)) {
            *tail = mk_cell(car(args), mk_nil());
            tail = &((*tail)->cell.cdr);
            args = cdr(args);
        }

        return omni_apply(inner_fn, all_args, env);
    }

    // Compose: (__compose__ f g) => f(g(x))
    if (fn->tag == T_CELL && car(fn) && car(fn)->tag == T_SYM &&
        strcmp(car(fn)->s, "__compose__") == 0) {
        Value* rest = cdr(fn);
        Value* f = car(rest);
        Value* g = car(cdr(rest));

        // First apply g to args
        Value* g_result = omni_apply(g, args, env);
        if (is_error(g_result)) return g_result;

        // Then apply f to g's result
        return omni_apply(f, mk_cell(g_result, mk_nil()), env);
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
 * (try expr
 *   (catch e handler-body)
 *   (finally cleanup-body))
 *
 * Simple try/catch/finally that wraps the condition system
 */
static Value* eval_try_catch(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("try: missing expression");

    Value* expr = car(args);
    Value* clauses = cdr(args);

    // Find catch and finally clauses
    Value* catch_var = NULL;
    Value* catch_body = NULL;
    Value* finally_body = NULL;

    while (!is_nil(clauses)) {
        Value* clause = car(clauses);
        if (clause && clause->tag == T_CELL) {
            Value* clause_head = car(clause);
            if (clause_head && clause_head->tag == T_SYM) {
                if (strcmp(clause_head->s, "catch") == 0) {
                    catch_var = car(cdr(clause));
                    catch_body = cdr(cdr(clause));
                } else if (strcmp(clause_head->s, "finally") == 0) {
                    finally_body = cdr(clause);
                }
            }
        }
        clauses = cdr(clauses);
    }

    // Build handler for catch clause
    Value* handlers = mk_nil();
    if (catch_var && catch_body) {
        // Create a handler that binds the error to catch_var
        Value* handler_entry = mk_cell(mk_sym(":error"),
                                       mk_cell(catch_var, catch_body));
        handlers = mk_cell(handler_entry, mk_nil());
    }

    // Push handler frame
    HandlerFrame* frame = push_handler_frame(handlers);
    if (!frame) return mk_error("try: out of memory");

    Value* result = mk_nothing();
    int caught = 0;

    if (setjmp(frame->jmp) == 0) {
        // Normal execution
        result = omni_eval(expr, env);

        // Check if result is an error that should be caught
        if (result && result->tag == T_ERROR && catch_var && catch_body) {
            caught = 1;
            // Bind error to catch_var and evaluate catch_body
            // Wrap the error as a string so it doesn't propagate through eval_list
            Env* catch_env = env_new(env);
            Value* error_val = mk_string_cstr(result->s);  // Convert error to string
            env_define(catch_env, catch_var, error_val);

            result = eval_do(catch_body, catch_env);
        }
    } else {
        // Jumped from signal - error was caught
        caught = 1;
        result = frame->result;
    }

    pop_handler_frame();

    // Execute finally block (always runs)
    if (finally_body) {
        Value* finally_result = eval_do(finally_body, env);
        // finally result is discarded unless it's an error
        if (is_error(finally_result)) {
            return finally_result;
        }
    }

    return result;
}

/*
 * (with-open-file [f "filename" :mode] body...)
 *
 * Opens file, binds to f, executes body, closes file automatically
 * Mode defaults to :read
 */
static Value* eval_with_open_file(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("with-open-file: missing binding");

    Value* binding = car(args);
    Value* body = cdr(args);

    // binding should be [var filename] or [var filename :mode]
    // which parses as (array var filename) or (array var filename :mode)
    if (!binding || binding->tag != T_CELL) {
        return mk_error("with-open-file: expected [var filename] binding");
    }

    Value* binding_head = car(binding);
    if (!binding_head || binding_head->tag != T_SYM ||
        strcmp(binding_head->s, "array") != 0) {
        return mk_error("with-open-file: expected [var filename] binding");
    }

    Value* var = car(cdr(binding));
    Value* filename_expr = car(cdr(cdr(binding)));
    Value* mode_expr = car(cdr(cdr(cdr(binding))));

    if (!var || var->tag != T_SYM) {
        return mk_error("with-open-file: expected symbol as variable name");
    }

    // Evaluate filename
    Value* filename = omni_eval(filename_expr, env);
    if (is_error(filename)) return filename;

    // Evaluate mode (default :read)
    Value* mode = mk_sym(":read");
    if (mode_expr && !is_nil(mode_expr)) {
        mode = omni_eval(mode_expr, env);
        if (is_error(mode)) return mode;
    }

    // Open the file
    Value* open_args = mk_cell(filename, mk_cell(mode, mk_nil()));
    Value* port = prim_open(open_args);
    if (is_error(port)) return port;

    // Create new environment with var bound to port
    Env* body_env = env_new(env);
    env_define(body_env, var, port);

    // Execute body
    Value* result = mk_nothing();
    Value* error = NULL;

    while (!is_nil(body)) {
        result = omni_eval(car(body), body_env);
        if (is_error(result)) {
            error = result;
            break;
        }
        body = cdr(body);
    }

    // Always close the port
    prim_close(mk_cell(port, mk_nil()));

    // Return error if one occurred
    if (error) return error;

    return result;
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

// Effect type definition (RecoveryModeOmni is forward declared near top of file)
struct EffectTypeDef {
    const char* name;
    RecoveryModeOmni mode;
    Value* payload_type;   // Type of value passed to perform (default: Nothing)
    Value* returns_type;   // Type of value returned by resume (default: Nothing)
    struct EffectTypeDef* next;
};

// Effect type registry
static EffectTypeDef* effect_type_registry = NULL;

// Register or find an effect type
// payload_type and returns_type can be NULL (defaults to Nothing)
static EffectTypeDef* register_effect_type_full(const char* name, RecoveryModeOmni mode,
                                                 Value* payload_type, Value* returns_type) {
    // Check if already exists
    for (EffectTypeDef* t = effect_type_registry; t; t = t->next) {
        if (strcmp(t->name, name) == 0) {
            t->mode = mode;  // Update mode if already registered
            t->payload_type = payload_type;
            t->returns_type = returns_type;
            return t;
        }
    }
    // Create new
    EffectTypeDef* t = malloc(sizeof(EffectTypeDef));
    t->name = strdup(name);
    t->mode = mode;
    t->payload_type = payload_type;
    t->returns_type = returns_type;
    t->next = effect_type_registry;
    effect_type_registry = t;
    return t;
}

// Backwards-compatible wrapper (defaults to Nothing/Nothing)
static EffectTypeDef* register_effect_type(const char* name, RecoveryModeOmni mode) {
    return register_effect_type_full(name, mode, NULL, NULL);
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
            // This is a fiber resume - use ucontext-based implementation
            Value* resume_val = mk_nothing();
            if (!is_nil(cdr(args))) {
                resume_val = omni_eval(car(cdr(args)), env);
                if (is_error(resume_val)) return resume_val;
            }

            return fiber_resume_internal(first, resume_val);
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

/* ============================================================
 * Module System Implementation
 * ============================================================
 *
 * (module name
 *   (export sym1 sym2 ...)
 *   body...)
 *
 * (import module-name)
 * (import module-name :only (sym1 sym2))
 * (import module-name :as alias)
 *
 * (export sym1 sym2 ...)  ; within a module definition
 */

/**
 * (module name body...)
 *
 * Creates a new module with the given name. The body is evaluated
 * in a fresh environment that inherits from global. Export forms
 * within the body specify which symbols are public.
 */
static Value* eval_module(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("module: missing name");

    Value* name = car(args);
    if (!name || name->tag != T_SYM) {
        return mk_error("module: name must be a symbol");
    }

    // Check if module already exists
    Module* existing = find_module(name->s);
    if (existing && existing->loaded) {
        return mk_error("module: already defined");
    }

    // Create or get module
    Module* m = existing ? existing : register_module(name->s, env);
    if (!m) return mk_error("module: registry full");

    // Save current module and set this one
    Module* prev_module = current_module;
    current_module = m;

    // Evaluate body in module's environment
    Value* body = cdr(args);
    Value* result = mk_nothing();

    while (!is_nil(body)) {
        result = omni_eval(car(body), m->env);
        if (is_error(result)) {
            current_module = prev_module;
            return result;
        }
        body = cdr(body);
    }

    m->loaded = 1;
    current_module = prev_module;

    // Return the module name
    return name;
}

/**
 * (export sym1 sym2 ...)
 *
 * Within a module, declares which symbols are exported (public).
 * Can only be used inside a module definition.
 */
static Value* eval_export(Value* args, Env* env) {
    (void)env;

    if (!current_module) {
        return mk_error("export: must be used inside a module");
    }

    // Add each symbol to the exports list
    while (!is_nil(args)) {
        Value* sym = car(args);
        if (!sym || sym->tag != T_SYM) {
            return mk_error("export: expected symbols");
        }

        // Add to exports if not already there
        if (!is_exported(current_module, sym->s)) {
            current_module->exports = mk_cell(sym, current_module->exports);
        }

        args = cdr(args);
    }

    return mk_sym("ok");
}

/**
 * (import module-name)
 * (import module-name :only (sym1 sym2))
 * (import module-name :as alias)
 *
 * Imports symbols from a module into the current environment.
 */
static Value* eval_import(Value* args, Env* env) {
    if (is_nil(args)) return mk_error("import: missing module name");

    Value* mod_name = car(args);
    if (!mod_name || mod_name->tag != T_SYM) {
        return mk_error("import: module name must be a symbol");
    }

    // Find the module
    Module* m = find_module(mod_name->s);
    if (!m) {
        char msg[128];
        snprintf(msg, sizeof(msg), "import: module '%s' not found", mod_name->s);
        return mk_error(msg);
    }

    if (!m->loaded) {
        char msg[128];
        snprintf(msg, sizeof(msg), "import: module '%s' not loaded", mod_name->s);
        return mk_error(msg);
    }

    // Parse import options
    Value* options = cdr(args);
    Value* only_syms = NULL;
    const char* alias = NULL;

    while (!is_nil(options)) {
        Value* opt = car(options);
        if (opt && opt->tag == T_SYM) {
            const char* opt_name = opt->s;
            // Handle keywords (skip leading colon)
            if (opt_name[0] == ':') opt_name++;

            if (strcmp(opt_name, "only") == 0) {
                options = cdr(options);
                if (!is_nil(options)) {
                    only_syms = car(options);
                    // Handle array wrapper
                    if (only_syms && only_syms->tag == T_CELL &&
                        car(only_syms) && car(only_syms)->tag == T_SYM &&
                        strcmp(car(only_syms)->s, "array") == 0) {
                        only_syms = cdr(only_syms);
                    }
                }
            } else if (strcmp(opt_name, "as") == 0) {
                options = cdr(options);
                if (!is_nil(options)) {
                    Value* alias_sym = car(options);
                    if (alias_sym && alias_sym->tag == T_SYM) {
                        alias = alias_sym->s;
                    }
                }
            }
        }
        options = cdr(options);
    }

    // Import symbols
    if (alias) {
        // Import entire module as namespace: alias.symbol
        // For now, just bind module name to a dict-like structure
        Value* mod_dict = mk_cell(mk_sym("__module__"), mk_nil());
        Value** tail = &mod_dict->cell.cdr;

        Value* exports = m->exports;
        while (!is_nil(exports)) {
            Value* sym = car(exports);
            if (sym && sym->tag == T_SYM) {
                Value* val = env_lookup(m->env, sym);
                if (val) {
                    Value* pair = mk_cell(sym, val);
                    *tail = mk_cell(pair, mk_nil());
                    tail = &(*tail)->cell.cdr;
                }
            }
            exports = cdr(exports);
        }

        env_define(env, mk_sym(alias), mod_dict);
    } else if (only_syms) {
        // Import only specified symbols
        while (!is_nil(only_syms)) {
            Value* sym = car(only_syms);
            if (sym && sym->tag == T_SYM) {
                if (!is_exported(m, sym->s)) {
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                             "import: '%s' is not exported from '%s'",
                             sym->s, mod_name->s);
                    return mk_error(msg);
                }

                Value* val = env_lookup(m->env, sym);
                if (val) {
                    env_define(env, sym, val);
                }
            }
            only_syms = cdr(only_syms);
        }
    } else {
        // Import all exported symbols
        Value* exports = m->exports;
        while (!is_nil(exports)) {
            Value* sym = car(exports);
            if (sym && sym->tag == T_SYM) {
                Value* val = env_lookup(m->env, sym);
                if (val) {
                    env_define(env, sym, val);
                }
            }
            exports = cdr(exports);
        }
    }

    return mk_sym("ok");
}
