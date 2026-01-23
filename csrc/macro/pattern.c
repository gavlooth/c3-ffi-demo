/*
 * OmniLisp Macro Pattern Matching
 *
 * Matches input expressions against syntax-rules style patterns.
 * Supports ellipsis patterns (...) for zero-or-more matching.
 */

#include "macro.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============== Helper Functions ============== */

// REVIEWED:NAIVE
/*
 * Check if a symbol is in the literals list.
 */
bool omni_macro_is_literal(const char* sym, char** literals, size_t literal_count) {
    if (!sym || !literals) return false;

    for (size_t i = 0; i < literal_count; i++) {
        if (strcmp(sym, literals[i]) == 0) {
            return true;
        }
    }
    return false;
}

/*
 * Create a new pattern binding.
 */
static PatternBinding* new_binding(const char* name, OmniValue* value) {
    PatternBinding* b = malloc(sizeof(PatternBinding));
    memset(b, 0, sizeof(PatternBinding));
    b->var_name = strdup(name);
    b->values = malloc(sizeof(OmniValue*));
    b->values[0] = value;
    b->value_count = 1;
    b->depth = 0;
    return b;
}

/*
 * Create an ellipsis binding with initial capacity.
 */
static PatternBinding* new_ellipsis_binding(const char* name, int depth) {
    PatternBinding* b = malloc(sizeof(PatternBinding));
    memset(b, 0, sizeof(PatternBinding));
    b->var_name = strdup(name);
    b->values = NULL;
    b->value_count = 0;
    b->depth = depth;
    return b;
}

/*
 * Add a value to an ellipsis binding.
 */
static void binding_add_value(PatternBinding* b, OmniValue* value) {
    b->values = realloc(b->values, (b->value_count + 1) * sizeof(OmniValue*));
    b->values[b->value_count++] = value;
}

/*
 * Create a successful match result.
 */
static MatchResult* match_success(PatternBinding* bindings) {
    MatchResult* r = malloc(sizeof(MatchResult));
    r->success = true;
    r->bindings = bindings;
    return r;
}

/*
 * Create a failed match result.
 */
static MatchResult* match_failure(void) {
    MatchResult* r = malloc(sizeof(MatchResult));
    r->success = false;
    r->bindings = NULL;
    return r;
}

/*
 * Merge two binding lists.
 */
static PatternBinding* merge_bindings(PatternBinding* a, PatternBinding* b) {
    if (!a) return b;
    if (!b) return a;

    PatternBinding* tail = a;
    while (tail->next) tail = tail->next;
    tail->next = b;
    return a;
}

// REVIEWED:NAIVE
/*
 * Find a binding by name.
 */
static PatternBinding* find_binding(PatternBinding* bindings, const char* name) {
    for (PatternBinding* b = bindings; b; b = b->next) {
        if (strcmp(b->var_name, name) == 0) {
            return b;
        }
    }
    return NULL;
}

/*
 * Deep copy bindings.
 */
static PatternBinding* copy_bindings(PatternBinding* bindings) {
    if (!bindings) return NULL;

    PatternBinding* result = NULL;
    PatternBinding* tail = NULL;

    for (PatternBinding* b = bindings; b; b = b->next) {
        PatternBinding* copy = malloc(sizeof(PatternBinding));
        copy->var_name = strdup(b->var_name);
        copy->values = malloc(b->value_count * sizeof(OmniValue*));
        memcpy(copy->values, b->values, b->value_count * sizeof(OmniValue*));
        copy->value_count = b->value_count;
        copy->depth = b->depth;
        copy->next = NULL;

        if (tail) {
            tail->next = copy;
        } else {
            result = copy;
        }
        tail = copy;
    }

    return result;
}

/* ============== Pattern Variable Extraction ============== */

/*
 * Extract all pattern variables from a pattern.
 * This builds a list of variable names, marking those followed by ellipsis.
 */
MacroPatternVar* omni_macro_extract_pattern_vars(MacroExpander* exp,
                                                  OmniValue* pattern,
                                                  char** literals,
                                                  size_t literal_count,
                                                  int depth) {
    MacroPatternVar* result = NULL;
    MacroPatternVar* tail = NULL;

    if (!pattern || omni_is_nil(pattern)) {
        return NULL;
    }

    /* Symbol: potential pattern variable */
    if (omni_is_sym(pattern)) {
        /* Skip literals and special symbols */
        const char* name = pattern->str_val;
        if (omni_macro_is_literal(name, literals, literal_count)) {
            return NULL;
        }
        if (strcmp(name, "...") == 0 || strcmp(name, "_") == 0) {
            return NULL;
        }

        MacroPatternVar* var = malloc(sizeof(MacroPatternVar));
        var->name = strdup(name);
        var->is_ellipsis = false;
        var->depth = depth;
        var->next = NULL;
        return var;
    }

    /* List: recurse through elements */
    if (omni_is_cell(pattern)) {
        OmniValue* curr = pattern;
        size_t idx = 0;

        while (omni_is_cell(curr)) {
            OmniValue* elem = omni_car(curr);
            OmniValue* next = omni_cdr(curr);

            /* Check if followed by ellipsis */
            bool has_ellipsis = false;
            if (omni_is_cell(next) && omni_macro_is_ellipsis(omni_car(next))) {
                has_ellipsis = true;
            }

            /* Recurse with appropriate depth */
            int new_depth = has_ellipsis ? depth + 1 : depth;
            MacroPatternVar* vars = omni_macro_extract_pattern_vars(exp, elem,
                                                                     literals, literal_count,
                                                                     new_depth);

            /* Mark variables under ellipsis */
            if (has_ellipsis) {
                for (MacroPatternVar* v = vars; v; v = v->next) {
                    v->is_ellipsis = true;
                }
            }

// REVIEWED:NAIVE
            /* Append to result */
            if (vars) {
                if (tail) {
                    tail->next = vars;
                } else {
                    result = vars;
                }
                while (tail && tail->next) tail = tail->next;
                if (!tail) tail = vars;
                while (tail && tail->next) tail = tail->next;
            }

            /* Skip ellipsis in iteration */
            if (has_ellipsis && omni_is_cell(next)) {
                curr = omni_cdr(next);
            } else {
                curr = next;
            }
            idx++;
        }

        return result;
    }

    /* Array: similar to list */
    if (omni_is_array(pattern)) {
        size_t len = omni_array_len(pattern);

        for (size_t i = 0; i < len; i++) {
            OmniValue* elem = omni_array_get(pattern, i);

            /* Check if followed by ellipsis */
            bool has_ellipsis = false;
            if (i + 1 < len && omni_macro_is_ellipsis(omni_array_get(pattern, i + 1))) {
                has_ellipsis = true;
            }

            /* Skip ellipsis symbol itself */
            if (omni_macro_is_ellipsis(elem)) {
                continue;
            }

            int new_depth = has_ellipsis ? depth + 1 : depth;
            MacroPatternVar* vars = omni_macro_extract_pattern_vars(exp, elem,
                                                                     literals, literal_count,
                                                                     new_depth);

            if (has_ellipsis) {
                for (MacroPatternVar* v = vars; v; v = v->next) {
                    v->is_ellipsis = true;
                }
            }

            if (vars) {
                if (tail) {
                    tail->next = vars;
                } else {
                    result = vars;
                }
                while (tail && tail->next) tail = tail->next;
                if (!tail) tail = vars;
                while (tail && tail->next) tail = tail->next;
            }
        }

        return result;
    }

    return NULL;
}

/* ============== Pattern Matching ============== */

/* Forward declaration */
static MatchResult* match_pattern_internal(MacroExpander* exp,
                                            OmniValue* pattern,
                                            OmniValue* input,
                                            char** literals,
                                            size_t literal_count,
                                            int depth);

/*
 * Match ellipsis pattern: (pat ...) matches zero or more elements.
 */
static MatchResult* match_ellipsis(MacroExpander* exp,
                                    OmniValue* sub_pattern,
                                    OmniValue* input_rest,
                                    char** literals,
                                    size_t literal_count,
                                    int depth) {
    /* Extract variables from sub-pattern for creating bindings */
    MacroPatternVar* vars = omni_macro_extract_pattern_vars(exp, sub_pattern,
                                                             literals, literal_count,
                                                             depth);

    /* Create empty bindings for all variables */
    PatternBinding* bindings = NULL;
    PatternBinding* btail = NULL;

    for (MacroPatternVar* v = vars; v; v = v->next) {
        PatternBinding* b = new_ellipsis_binding(v->name, depth);

        if (btail) {
            btail->next = b;
        } else {
            bindings = b;
        }
        btail = b;
    }

    /* Match zero or more elements */
    OmniValue* curr = input_rest;
    while (omni_is_cell(curr)) {
        OmniValue* elem = omni_car(curr);

        MatchResult* sub_match = match_pattern_internal(exp, sub_pattern, elem,
                                                         literals, literal_count, depth + 1);

        if (!sub_match->success) {
            /* Stop matching here - remaining elements will be matched by next pattern */
            omni_macro_match_result_free(sub_match);
            break;
        }

        // REVIEWED:NAIVE
        /* Add matched values to ellipsis bindings */
        for (PatternBinding* sb = sub_match->bindings; sb; sb = sb->next) {
            PatternBinding* target = find_binding(bindings, sb->var_name);
            if (target) {
                for (size_t i = 0; i < sb->value_count; i++) {
                    binding_add_value(target, sb->values[i]);
                }
            }
        }

        omni_macro_match_result_free(sub_match);
        curr = omni_cdr(curr);
    }

    /* Free extracted vars */
    while (vars) {
        MacroPatternVar* next = vars->next;
        free(vars->name);
        free(vars);
        vars = next;
    }

    return match_success(bindings);
}

/*
 * Match a pattern against input.
 */
static MatchResult* match_pattern_internal(MacroExpander* exp,
                                            OmniValue* pattern,
                                            OmniValue* input,
                                            char** literals,
                                            size_t literal_count,
                                            int depth) {
    /* Null or nil pattern matches nil input */
    if (!pattern || omni_is_nil(pattern)) {
        if (!input || omni_is_nil(input)) {
            return match_success(NULL);
        }
        return match_failure();
    }

    /* Underscore (_) matches anything */
    if (omni_is_sym(pattern) && strcmp(pattern->str_val, "_") == 0) {
        return match_success(NULL);
    }

    /* Literal symbol - must match exactly */
    if (omni_is_sym(pattern)) {
        const char* name = pattern->str_val;

        if (omni_macro_is_literal(name, literals, literal_count)) {
            /* Must be identical symbol */
            if (omni_is_sym(input) && strcmp(input->str_val, name) == 0) {
                return match_success(NULL);
            }
            return match_failure();
        }

        /* Pattern variable - bind to input */
        PatternBinding* b = new_binding(name, input);
        return match_success(b);
    }

    /* Literal values - must match exactly */
    if (omni_is_int(pattern)) {
        if (omni_is_int(input) && input->int_val == pattern->int_val) {
            return match_success(NULL);
        }
        return match_failure();
    }

    if (omni_is_float(pattern)) {
        if (omni_is_float(input) && input->float_val == pattern->float_val) {
            return match_success(NULL);
        }
        return match_failure();
    }

    if (omni_is_string(pattern)) {
        if (omni_is_string(input) && strcmp(input->str_val, pattern->str_val) == 0) {
            return match_success(NULL);
        }
        return match_failure();
    }

    if (omni_is_keyword(pattern)) {
        if (omni_is_keyword(input) && strcmp(input->str_val, pattern->str_val) == 0) {
            return match_success(NULL);
        }
        return match_failure();
    }

    /* List pattern */
    if (omni_is_cell(pattern)) {
        if (!omni_is_cell(input)) {
            return match_failure();
        }

        PatternBinding* bindings = NULL;
        OmniValue* p_curr = pattern;
        OmniValue* i_curr = input;

        while (omni_is_cell(p_curr)) {
            OmniValue* p_elem = omni_car(p_curr);
            OmniValue* p_next = omni_cdr(p_curr);

            /* Check for ellipsis */
            if (omni_is_cell(p_next) && omni_macro_is_ellipsis(omni_car(p_next))) {
                /* Match rest with ellipsis */
                MatchResult* ellipsis_match = match_ellipsis(exp, p_elem, i_curr,
                                                              literals, literal_count, depth);

                if (!ellipsis_match->success) {
                    omni_macro_match_result_free(ellipsis_match);
                    /* Free bindings accumulated so far */
                    omni_macro_match_result_free(match_success(bindings));
                    return match_failure();
                }

                bindings = merge_bindings(bindings, ellipsis_match->bindings);
                ellipsis_match->bindings = NULL;
                omni_macro_match_result_free(ellipsis_match);

                /* Skip ellipsis and continue with rest of pattern */
                p_curr = omni_cdr(p_next);
                /* All remaining input was consumed by ellipsis (greedy) */
                i_curr = omni_nil;
                continue;
            }

            /* Need an input element */
            if (!omni_is_cell(i_curr)) {
                omni_macro_match_result_free(match_success(bindings));
                return match_failure();
            }

            OmniValue* i_elem = omni_car(i_curr);

            /* Match element */
            MatchResult* elem_match = match_pattern_internal(exp, p_elem, i_elem,
                                                              literals, literal_count, depth);

            if (!elem_match->success) {
                omni_macro_match_result_free(elem_match);
                omni_macro_match_result_free(match_success(bindings));
                return match_failure();
            }

            bindings = merge_bindings(bindings, elem_match->bindings);
            elem_match->bindings = NULL;
            omni_macro_match_result_free(elem_match);

            p_curr = p_next;
            i_curr = omni_cdr(i_curr);
        }

        /* Pattern exhausted - input should also be exhausted */
        if (!omni_is_nil(i_curr)) {
            omni_macro_match_result_free(match_success(bindings));
            return match_failure();
        }

        return match_success(bindings);
    }

    /* Array pattern */
    if (omni_is_array(pattern)) {
        if (!omni_is_array(input)) {
            return match_failure();
        }

        PatternBinding* bindings = NULL;
        size_t p_len = omni_array_len(pattern);
        size_t i_len = omni_array_len(input);
        size_t p_idx = 0;
        size_t i_idx = 0;

        while (p_idx < p_len) {
            OmniValue* p_elem = omni_array_get(pattern, p_idx);

            /* Skip ellipsis symbol */
            if (omni_macro_is_ellipsis(p_elem)) {
                p_idx++;
                continue;
            }

            /* Check for ellipsis following this element */
            bool has_ellipsis = (p_idx + 1 < p_len &&
                                 omni_macro_is_ellipsis(omni_array_get(pattern, p_idx + 1)));

            if (has_ellipsis) {
                /* Match zero or more from input */
                MacroPatternVar* vars = omni_macro_extract_pattern_vars(exp, p_elem,
                                                                         literals, literal_count,
                                                                         depth);

                /* Create empty bindings */
                PatternBinding* ellipsis_bindings = NULL;
                PatternBinding* btail = NULL;
                for (MacroPatternVar* v = vars; v; v = v->next) {
                    PatternBinding* b = new_ellipsis_binding(v->name, depth);
                    if (btail) btail->next = b;
                    else ellipsis_bindings = b;
                    btail = b;
                }

                /* Match remaining elements */
                while (i_idx < i_len) {
                    OmniValue* i_elem = omni_array_get(input, i_idx);
                    MatchResult* sub = match_pattern_internal(exp, p_elem, i_elem,
                                                               literals, literal_count, depth + 1);

                    if (!sub->success) {
                        omni_macro_match_result_free(sub);
                        break;
                    }

                    for (PatternBinding* sb = sub->bindings; sb; sb = sb->next) {
                        PatternBinding* target = find_binding(ellipsis_bindings, sb->var_name);
                        if (target) {
                            for (size_t k = 0; k < sb->value_count; k++) {
                                binding_add_value(target, sb->values[k]);
                            }
                        }
                    }

                    omni_macro_match_result_free(sub);
                    i_idx++;
                }

                bindings = merge_bindings(bindings, ellipsis_bindings);

                /* Free vars */
                while (vars) {
                    MacroPatternVar* next = vars->next;
                    free(vars->name);
                    free(vars);
                    vars = next;
                }

                p_idx += 2; /* Skip element and ellipsis */
                continue;
            }

            /* Regular element match */
            if (i_idx >= i_len) {
                omni_macro_match_result_free(match_success(bindings));
                return match_failure();
            }

            OmniValue* i_elem = omni_array_get(input, i_idx);
            MatchResult* elem_match = match_pattern_internal(exp, p_elem, i_elem,
                                                              literals, literal_count, depth);

            if (!elem_match->success) {
                omni_macro_match_result_free(elem_match);
                omni_macro_match_result_free(match_success(bindings));
                return match_failure();
            }

            bindings = merge_bindings(bindings, elem_match->bindings);
            elem_match->bindings = NULL;
            omni_macro_match_result_free(elem_match);

            p_idx++;
            i_idx++;
        }

        /* Both should be exhausted */
        if (i_idx != i_len) {
            omni_macro_match_result_free(match_success(bindings));
            return match_failure();
        }

        return match_success(bindings);
    }

    /* Unknown pattern type - fail */
    return match_failure();
}

/* ============== Public API ============== */

MatchResult* omni_macro_match_pattern(MacroExpander* exp,
                                       OmniValue* pattern,
                                       OmniValue* input,
                                       char** literals,
                                       size_t literal_count) {
    return match_pattern_internal(exp, pattern, input, literals, literal_count, 0);
}

void omni_macro_match_result_free(MatchResult* result) {
    if (!result) return;

    PatternBinding* b = result->bindings;
    while (b) {
        PatternBinding* next = b->next;
        free(b->var_name);
        free(b->values);
        free(b);
        b = next;
    }

    free(result);
}
