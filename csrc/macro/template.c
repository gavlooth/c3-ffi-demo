/*
 * OmniLisp Macro Template Substitution
 *
 * Substitutes pattern bindings into templates with ellipsis unfolding
 * and hygiene marking for macro-introduced symbols.
 */

#include "macro.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============== Binding Lookup ============== */

// REVIEWED:NAIVE
bool omni_macro_is_bound(const char* name, PatternBinding* bindings) {
    for (PatternBinding* b = bindings; b; b = b->next) {
        if (strcmp(b->var_name, name) == 0) {
            return true;
        }
    }
    return false;
}

// REVIEWED:NAIVE
PatternBinding* omni_macro_get_binding(const char* name, PatternBinding* bindings) {
    for (PatternBinding* b = bindings; b; b = b->next) {
        if (strcmp(b->var_name, name) == 0) {
            return b;
        }
    }
    return NULL;
}

/* ============== Ellipsis Helpers ============== */

/*
 * Collect all pattern variables used in a template.
 */
// REVIEWED:NAIVE
static void collect_template_vars(OmniValue* template, char*** vars, size_t* count, size_t* capacity) {
    if (!template) return;

    if (omni_is_sym(template)) {
        const char* name = template->str_val;
        /* Skip ellipsis */
        if (strcmp(name, "...") == 0) return;

        /* Check if already collected */
        for (size_t i = 0; i < *count; i++) {
            if (strcmp((*vars)[i], name) == 0) return;
        }

        /* Add to list */
        if (*count >= *capacity) {
            *capacity = *capacity ? *capacity * 2 : 8;
            *vars = realloc(*vars, *capacity * sizeof(char*));
        }
        (*vars)[(*count)++] = strdup(name);
        return;
    }

    if (omni_is_cell(template)) {
        OmniValue* curr = template;
        while (omni_is_cell(curr)) {
            collect_template_vars(omni_car(curr), vars, count, capacity);
            curr = omni_cdr(curr);
        }
        return;
    }

    if (omni_is_array(template)) {
        for (size_t i = 0; i < omni_array_len(template); i++) {
            collect_template_vars(omni_array_get(template, i), vars, count, capacity);
        }
        return;
    }
}

/*
 * Get the number of repetitions for ellipsis unfolding.
 * Returns the minimum count across all ellipsis-bound variables in the template.
 */
size_t omni_macro_ellipsis_count(PatternBinding* bindings, OmniValue* template) {
    char** vars = NULL;
    size_t var_count = 0;
    size_t var_capacity = 0;

    collect_template_vars(template, &vars, &var_count, &var_capacity);

    size_t min_count = SIZE_MAX;
    bool found_ellipsis_var = false;

    for (size_t i = 0; i < var_count; i++) {
        PatternBinding* b = omni_macro_get_binding(vars[i], bindings);
        if (b && b->depth > 0) {
            found_ellipsis_var = true;
            if (b->value_count < min_count) {
                min_count = b->value_count;
            }
        }
        free(vars[i]);
    }
    free(vars);

    return found_ellipsis_var ? min_count : 0;
}

/*
 * Create sliced bindings for ellipsis iteration.
 * Returns new bindings where ellipsis variables have only the i-th value.
 */
PatternBinding* omni_macro_slice_bindings(MacroExpander* exp,
                                           PatternBinding* bindings,
                                           size_t index) {
    PatternBinding* result = NULL;
    PatternBinding* tail = NULL;

    for (PatternBinding* b = bindings; b; b = b->next) {
        PatternBinding* copy = malloc(sizeof(PatternBinding));
        copy->var_name = strdup(b->var_name);
        copy->next = NULL;

        if (b->depth > 0 && b->value_count > 0) {
            /* Ellipsis binding - take single value at index */
            copy->values = malloc(sizeof(OmniValue*));
            copy->values[0] = (index < b->value_count) ? b->values[index] : omni_nil;
            copy->value_count = 1;
            copy->depth = b->depth - 1;
        } else {
            /* Non-ellipsis binding - copy as-is */
            copy->values = malloc(b->value_count * sizeof(OmniValue*));
            memcpy(copy->values, b->values, b->value_count * sizeof(OmniValue*));
            copy->value_count = b->value_count;
            copy->depth = b->depth;
        }

        if (tail) {
            tail->next = copy;
        } else {
            result = copy;
        }
        tail = copy;
    }

    return result;
}

/*
 * Free sliced bindings.
 */
static void free_sliced_bindings(PatternBinding* bindings) {
    while (bindings) {
        PatternBinding* next = bindings->next;
        free(bindings->var_name);
        free(bindings->values);
        free(bindings);
        bindings = next;
    }
}

/* ============== Template Substitution ============== */

/*
 * Substitute bindings into a template recursively.
 */
OmniValue* omni_macro_substitute(MacroExpander* exp,
                                  OmniValue* template,
                                  PatternBinding* bindings,
                                  int mark) {
    if (!template) return omni_nil;

    /* Nil */
    if (omni_is_nil(template)) {
        return omni_nil;
    }

    /* Symbol - check if bound or needs hygiene */
    if (omni_is_sym(template)) {
        const char* name = template->str_val;

        /* Check for bound pattern variable */
        PatternBinding* b = omni_macro_get_binding(name, bindings);
        if (b && b->value_count > 0) {
            /* Return the bound value (deep copy to prevent aliasing) */
            return omni_macro_deep_copy(b->values[0]);
        }

        /* Not bound - apply hygiene mark for macro-introduced symbol */
        return omni_macro_apply_mark(exp, template, mark);
    }

    /* Literal values - return as-is */
    if (omni_is_int(template) || omni_is_float(template) ||
        omni_is_string(template) || omni_is_char(template) ||
        omni_is_keyword(template)) {
        return template;
    }

    /* List template with potential ellipsis */
    if (omni_is_cell(template)) {
        OmniValue* result = omni_nil;
        OmniValue* tail = NULL;

        OmniValue* curr = template;
        while (omni_is_cell(curr)) {
            OmniValue* elem = omni_car(curr);
            OmniValue* next = omni_cdr(curr);

            /* Check for ellipsis following this element */
            bool has_ellipsis = omni_is_cell(next) && omni_macro_is_ellipsis(omni_car(next));

            if (has_ellipsis) {
                /* Unfold ellipsis */
                size_t count = omni_macro_ellipsis_count(bindings, elem);

                for (size_t i = 0; i < count; i++) {
                    PatternBinding* sliced = omni_macro_slice_bindings(exp, bindings, i);
                    OmniValue* expanded = omni_macro_substitute(exp, elem, sliced, mark);
                    free_sliced_bindings(sliced);

                    OmniValue* new_cell = omni_new_cell(expanded, omni_nil);
                    if (tail) {
                        tail->cell.cdr = new_cell;
                    } else {
                        result = new_cell;
                    }
                    tail = new_cell;
                }

                /* Skip ellipsis */
                curr = omni_cdr(next);
                continue;
            }

            /* Regular element - substitute recursively */
            OmniValue* expanded = omni_macro_substitute(exp, elem, bindings, mark);

            OmniValue* new_cell = omni_new_cell(expanded, omni_nil);
            if (tail) {
                tail->cell.cdr = new_cell;
            } else {
                result = new_cell;
            }
            tail = new_cell;

            curr = next;
        }

        /* Handle improper list tail */
        if (!omni_is_nil(curr)) {
            OmniValue* expanded_tail = omni_macro_substitute(exp, curr, bindings, mark);
            if (tail) {
                tail->cell.cdr = expanded_tail;
            } else {
                result = expanded_tail;
            }
        }

        return result;
    }

    /* Array template with potential ellipsis */
    if (omni_is_array(template)) {
        /* First pass: calculate result size */
        size_t len = omni_array_len(template);
        size_t result_size = 0;

        for (size_t i = 0; i < len; i++) {
            OmniValue* elem = omni_array_get(template, i);

            if (omni_macro_is_ellipsis(elem)) {
                continue; /* Skip ellipsis markers */
            }

            bool has_ellipsis = (i + 1 < len &&
                                 omni_macro_is_ellipsis(omni_array_get(template, i + 1)));

            if (has_ellipsis) {
                result_size += omni_macro_ellipsis_count(bindings, elem);
                i++; /* Skip ellipsis in next iteration */
            } else {
                result_size++;
            }
        }

        /* Second pass: build result */
        OmniValue* result = omni_new_array(result_size);

        for (size_t i = 0; i < len; i++) {
            OmniValue* elem = omni_array_get(template, i);

            if (omni_macro_is_ellipsis(elem)) {
                continue;
            }

            bool has_ellipsis = (i + 1 < len &&
                                 omni_macro_is_ellipsis(omni_array_get(template, i + 1)));

            if (has_ellipsis) {
                size_t count = omni_macro_ellipsis_count(bindings, elem);

                for (size_t j = 0; j < count; j++) {
                    PatternBinding* sliced = omni_macro_slice_bindings(exp, bindings, j);
                    OmniValue* expanded = omni_macro_substitute(exp, elem, sliced, mark);
                    free_sliced_bindings(sliced);

                    omni_array_push(result, expanded);
                }

                i++; /* Skip ellipsis */
            } else {
                OmniValue* expanded = omni_macro_substitute(exp, elem, bindings, mark);
                omni_array_push(result, expanded);
            }
        }

        return result;
    }

    /* Other types - return as-is */
    return template;
}
