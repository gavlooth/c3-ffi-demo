/*
 * OmniLisp Macro Expander
 *
 * Main expansion loop, macro registration, and program transformation.
 */

#include "macro.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ============== Constants ============== */

#define DEFAULT_MAX_DEPTH 1000
#define INITIAL_ERROR_CAPACITY 8
#define INITIAL_EXPR_CAPACITY 64

/* ============== Expander Lifecycle ============== */

MacroExpander* omni_macro_expander_new(void) {
    MacroExpander* exp = malloc(sizeof(MacroExpander));
    if (!exp) return NULL;

    memset(exp, 0, sizeof(MacroExpander));
    exp->max_depth = DEFAULT_MAX_DEPTH;
    exp->hygiene = omni_macro_hygiene_new();
    exp->arena = omni_arena_new(4096);

    return exp;
}

void omni_macro_expander_free(MacroExpander* exp) {
    if (!exp) return;

    /* Free macro definitions */
    MacroDef* def = exp->macros;
    while (def) {
        MacroDef* next = def->next;

        /* Free rules */
        MacroRule* rule = def->rules;
        while (rule) {
            MacroRule* rnext = rule->next;

            /* Free pattern variables */
            MacroPatternVar* var = rule->vars;
            while (var) {
                MacroPatternVar* vnext = var->next;
                free(var->name);
                free(var);
                var = vnext;
            }

            free(rule);
            rule = rnext;
        }

        /* Free literals */
        for (size_t i = 0; i < def->literal_count; i++) {
            free(def->literals[i]);
        }
        free(def->literals);
        free(def->name);
        free(def);

        def = next;
    }

    /* Free errors */
    for (size_t i = 0; i < exp->error_count; i++) {
        free(exp->errors[i]);
    }
    free(exp->errors);

    /* Free hygiene context */
    omni_macro_hygiene_free(exp->hygiene);

    /* Free arena */
    if (exp->arena) {
        omni_arena_free(exp->arena);
    }

    free(exp);
}

void omni_macro_expander_reset(MacroExpander* exp) {
    if (!exp) return;
    exp->expansion_depth = 0;
    /* Clear errors but keep macros */
    for (size_t i = 0; i < exp->error_count; i++) {
        free(exp->errors[i]);
    }
    exp->error_count = 0;
}

/* ============== Error Handling ============== */

bool omni_macro_expander_has_errors(MacroExpander* exp) {
    return exp && exp->error_count > 0;
}

size_t omni_macro_expander_error_count(MacroExpander* exp) {
    return exp ? exp->error_count : 0;
}

const char* omni_macro_expander_get_error(MacroExpander* exp, size_t index) {
    if (!exp || index >= exp->error_count) return NULL;
    return exp->errors[index];
}

void omni_macro_add_error(MacroExpander* exp, const char* fmt, ...) {
    if (!exp) return;

    if (exp->error_count >= exp->error_capacity) {
        exp->error_capacity = exp->error_capacity ? exp->error_capacity * 2 : INITIAL_ERROR_CAPACITY;
        exp->errors = realloc(exp->errors, exp->error_capacity * sizeof(char*));
    }

    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    exp->errors[exp->error_count++] = strdup(buf);
}

/* ============== Syntax Definition Detection ============== */

/*
 * Check if expression is: (define [syntax name] ...)
 */
bool omni_macro_is_syntax_define(OmniValue* expr) {
    if (!omni_is_cell(expr)) return false;

    OmniValue* head = omni_car(expr);
    if (!omni_is_sym(head) || !omni_sym_eq_str(head, "define")) return false;

    /* Second element should be [syntax name] array */
    OmniValue* second = omni_car(omni_cdr(expr));
    if (!omni_is_array(second)) return false;
    if (omni_array_len(second) != 2) return false;

    OmniValue* first_elem = omni_array_get(second, 0);
    return omni_is_sym(first_elem) && omni_sym_eq_str(first_elem, "syntax");
}

/* ============== Macro Registration ============== */

/*
 * Parse a syntax definition and register the macro.
 *
 * Expected forms:
 *   (define [syntax name]
 *     [literals sym1 sym2 ...]   ;; optional
 *     [(name pat...) template]
 *     ...)
 */
bool omni_macro_register(MacroExpander* exp, OmniValue* syntax_def) {
    if (!exp || !syntax_def) return false;

    /* Extract macro name from [syntax name] */
    OmniValue* syntax_spec = omni_car(omni_cdr(syntax_def));
    OmniValue* macro_name_val = omni_array_get(syntax_spec, 1);

    if (!omni_is_sym(macro_name_val)) {
        omni_macro_add_error(exp, "Macro name must be a symbol");
        return false;
    }

    const char* macro_name = macro_name_val->str_val;

    /* Create macro definition */
    MacroDef* def = malloc(sizeof(MacroDef));
    memset(def, 0, sizeof(MacroDef));
    def->name = strdup(macro_name);
    def->definition_mark = exp->hygiene->current_mark;

    /* Parse rest of definition: literals and rules */
    OmniValue* rest = omni_cdr(omni_cdr(syntax_def));

    /* Check for optional [literals ...] clause */
    OmniValue* first = omni_car(rest);
    if (omni_is_array(first) && omni_array_len(first) > 0) {
        OmniValue* lit_head = omni_array_get(first, 0);
        if (omni_is_sym(lit_head) && omni_sym_eq_str(lit_head, "literals")) {
            /* Extract literal symbols */
            size_t lit_count = omni_array_len(first) - 1;
            def->literals = malloc(lit_count * sizeof(char*));
            def->literal_count = lit_count;

            for (size_t i = 0; i < lit_count; i++) {
                OmniValue* lit = omni_array_get(first, i + 1);
                if (omni_is_sym(lit)) {
                    def->literals[i] = strdup(lit->str_val);
                } else {
                    def->literals[i] = strdup("");
                }
            }

            rest = omni_cdr(rest);
        }
    }

    /* Parse rules: each is [(name pattern...) template] */
    MacroRule* last_rule = NULL;
    while (!omni_is_nil(rest)) {
        OmniValue* rule_arr = omni_car(rest);

        if (!omni_is_array(rule_arr) || omni_array_len(rule_arr) != 2) {
            omni_macro_add_error(exp, "Invalid macro rule in '%s': expected [pattern template]", macro_name);
            rest = omni_cdr(rest);
            continue;
        }

        OmniValue* pattern = omni_array_get(rule_arr, 0);
        OmniValue* template = omni_array_get(rule_arr, 1);

        /* Pattern must be a list starting with macro name */
        if (!omni_is_cell(pattern)) {
            omni_macro_add_error(exp, "Pattern must be a list in macro '%s'", macro_name);
            rest = omni_cdr(rest);
            continue;
        }

        /* Create rule */
        MacroRule* rule = malloc(sizeof(MacroRule));
        memset(rule, 0, sizeof(MacroRule));
        rule->pattern = pattern;
        rule->template = template;

        /* Extract pattern variables */
        rule->vars = omni_macro_extract_pattern_vars(exp, pattern,
                                                      def->literals, def->literal_count, 0);

        /* Add to rule list */
        if (last_rule) {
            last_rule->next = rule;
        } else {
            def->rules = rule;
        }
        last_rule = rule;

        rest = omni_cdr(rest);
    }

    if (!def->rules) {
        omni_macro_add_error(exp, "Macro '%s' has no valid rules", macro_name);
        free(def->name);
        free(def->literals);
        free(def);
        return false;
    }

    /* Add to macro table (prepend for shadowing semantics) */
    def->next = exp->macros;
    exp->macros = def;

    return true;
}

// REVIEWED:NAIVE
MacroDef* omni_macro_lookup(MacroExpander* exp, const char* name) {
    if (!exp || !name) return NULL;

    for (MacroDef* def = exp->macros; def; def = def->next) {
        if (strcmp(def->name, name) == 0) {
            return def;
        }
    }
    return NULL;
}

/* ============== Main Expansion ============== */

/*
 * Expand a single macro call using matching rule.
 */
static OmniValue* expand_macro_call(MacroExpander* exp, OmniValue* call, MacroDef* def) {
    /* Try each rule in order */
    for (MacroRule* rule = def->rules; rule; rule = rule->next) {
        MatchResult* match = omni_macro_match_pattern(exp,
                                                       rule->pattern,
                                                       call,
                                                       def->literals,
                                                       def->literal_count);

        if (match && match->success) {
            /* Get fresh mark for this expansion */
            int mark = omni_macro_next_mark(exp);

            /* Substitute bindings into template */
            OmniValue* result = omni_macro_substitute(exp,
                                                       rule->template,
                                                       match->bindings,
                                                       mark);

            omni_macro_match_result_free(match);
            return result;
        }

        omni_macro_match_result_free(match);
    }

    /* No rule matched - return original */
    omni_macro_add_error(exp, "No matching rule for macro '%s'", def->name);
    return call;
}

/*
 * Check if a form is a binding form that introduces new variables.
 */
static bool is_binding_form(OmniValue* expr) {
    if (!omni_is_cell(expr)) return false;
    OmniValue* head = omni_car(expr);
    if (!omni_is_sym(head)) return false;

    const char* name = head->str_val;
    return strcmp(name, "let") == 0 ||
           strcmp(name, "lambda") == 0 ||
           strcmp(name, "fn") == 0 ||
           strcmp(name, "define") == 0 ||
           strcmp(name, "letrec") == 0 ||
           strcmp(name, "let*") == 0;
}

OmniValue* omni_macro_expand_expr(MacroExpander* exp, OmniValue* expr) {
    if (!exp || !expr) return expr;

    /* Check expansion depth limit */
    if (exp->expansion_depth > exp->max_depth) {
        omni_macro_add_error(exp, "Macro expansion depth exceeded (possible infinite loop)");
        return expr;
    }

    /* Non-list: return as-is */
    if (!omni_is_cell(expr)) {
        return expr;
    }

    /* Skip syntax definitions - they're handled separately */
    if (omni_macro_is_syntax_define(expr)) {
        return expr;
    }

    OmniValue* head = omni_car(expr);

    /* Check for macro call */
    if (omni_is_sym(head)) {
        MacroDef* def = omni_macro_lookup(exp, head->str_val);
        if (def) {
            exp->expansion_depth++;
            OmniValue* expanded = expand_macro_call(exp, expr, def);
            exp->expansion_depth--;

            /* Recursively expand the result */
            if (expanded != expr) {
                return omni_macro_expand_expr(exp, expanded);
            }
            return expanded;
        }
    }

    /* Recursively expand subexpressions */
    /* Build new list with expanded elements */
    OmniValue* result = omni_nil;
    OmniValue* tail = NULL;

    OmniValue* curr = expr;
    while (omni_is_cell(curr)) {
        OmniValue* elem = omni_car(curr);
        OmniValue* expanded_elem = omni_macro_expand_expr(exp, elem);

        OmniValue* new_cell = omni_new_cell(expanded_elem, omni_nil);
        if (tail) {
            tail->cell.cdr = new_cell;
        } else {
            result = new_cell;
        }
        tail = new_cell;

        curr = omni_cdr(curr);
    }

    /* Handle improper lists */
    if (!omni_is_nil(curr)) {
        if (tail) {
            tail->cell.cdr = omni_macro_expand_expr(exp, curr);
        }
    }

    return result;
}

OmniValue** omni_macro_expand_program(MacroExpander* exp,
                                       OmniValue** exprs,
                                       size_t* count) {
    if (!exp || !exprs || !count || *count == 0) return exprs;

    /* Pass 1: Collect and register macro definitions */
    size_t new_count = 0;
    OmniValue** new_exprs = malloc(*count * sizeof(OmniValue*));

    for (size_t i = 0; i < *count; i++) {
        if (omni_macro_is_syntax_define(exprs[i])) {
            /* Register macro and remove from expression list */
            omni_macro_register(exp, exprs[i]);
        } else {
            new_exprs[new_count++] = exprs[i];
        }
    }

    /* Replace original array */
    free(exprs);
    exprs = new_exprs;
    *count = new_count;

    /* If no macros registered, return unchanged */
    if (!exp->macros) {
        return exprs;
    }

    /* Pass 2: Expand until fixed point */
    bool changed = true;
    int passes = 0;

    while (changed && passes < exp->max_depth) {
        changed = false;
        passes++;

        for (size_t i = 0; i < *count; i++) {
            OmniValue* original = exprs[i];
            OmniValue* expanded = omni_macro_expand_expr(exp, original);

            if (expanded != original) {
                exprs[i] = expanded;
                changed = true;
            }
        }

        /* Check for errors */
        if (omni_macro_expander_has_errors(exp)) {
            break;
        }
    }

    if (passes >= exp->max_depth) {
        omni_macro_add_error(exp, "Macro expansion did not converge after %d passes", passes);
    }

    return exprs;
}

/* ============== Utility Functions ============== */

bool omni_macro_is_ellipsis(OmniValue* v) {
    return omni_is_sym(v) && strcmp(v->str_val, "...") == 0;
}

bool omni_macro_next_is_ellipsis(OmniValue* list, size_t index) {
    /* Navigate to index + 1 */
    OmniValue* curr = list;
    for (size_t i = 0; i <= index && omni_is_cell(curr); i++) {
        curr = omni_cdr(curr);
    }

    if (!omni_is_cell(curr)) return false;
    return omni_macro_is_ellipsis(omni_car(curr));
}

OmniValue* omni_macro_deep_copy(OmniValue* v) {
    if (!v || omni_is_nil(v)) return omni_nil;

    switch (v->tag) {
        case OMNI_INT:
            return omni_new_int(v->int_val);
        case OMNI_FLOAT:
            return omni_new_float(v->float_val);
        case OMNI_SYM:
            return omni_new_sym(v->str_val);
        case OMNI_STRING:
            return omni_new_string(v->str_val);
        case OMNI_KEYWORD:
            return omni_new_keyword(v->str_val);
        case OMNI_CHAR:
            return omni_new_char(v->int_val);
        case OMNI_CELL:
            return omni_new_cell(omni_macro_deep_copy(v->cell.car),
                                  omni_macro_deep_copy(v->cell.cdr));
        case OMNI_ARRAY: {
            OmniValue* arr = omni_new_array(v->array.len);
            for (size_t i = 0; i < v->array.len; i++) {
                omni_array_push(arr, omni_macro_deep_copy(v->array.data[i]));
            }
            return arr;
        }
        case OMNI_NIL:
            return omni_nil;
        default:
            /* For other types, return as-is (they shouldn't appear in patterns/templates) */
            return v;
    }
}

void omni_macro_print_def(MacroDef* def) {
    if (!def) return;

    printf("Macro: %s\n", def->name);
    printf("  Literals: ");
    for (size_t i = 0; i < def->literal_count; i++) {
        printf("%s ", def->literals[i]);
    }
    printf("\n");

    int rule_num = 1;
    for (MacroRule* rule = def->rules; rule; rule = rule->next) {
        printf("  Rule %d:\n", rule_num++);
        printf("    Pattern: %s\n", omni_value_to_string(rule->pattern));
        printf("    Template: %s\n", omni_value_to_string(rule->template));
        printf("    Vars: ");
        for (MacroPatternVar* var = rule->vars; var; var = var->next) {
            printf("%s%s ", var->name, var->is_ellipsis ? "..." : "");
        }
        printf("\n");
    }
}

void omni_macro_print_bindings(PatternBinding* bindings) {
    printf("Bindings:\n");
    for (PatternBinding* b = bindings; b; b = b->next) {
        printf("  %s = ", b->var_name);
        if (b->value_count == 1) {
            printf("%s\n", omni_value_to_string(b->values[0]));
        } else {
            printf("[");
            for (size_t i = 0; i < b->value_count; i++) {
                if (i > 0) printf(", ");
                printf("%s", omni_value_to_string(b->values[i]));
            }
            printf("]\n");
        }
    }
}
