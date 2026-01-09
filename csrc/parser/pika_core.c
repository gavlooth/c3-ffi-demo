/*
 * Pika Parser Core Implementation
 *
 * Based on the Pika parsing algorithm - a packrat PEG parser.
 * Uses right-to-left pass with memoization for O(n) parsing.
 */

#include "pika.h"
#include <stdlib.h>
#include <string.h>

PikaState* pika_new(const char* input, PikaRule* rules, int num_rules) {
    PikaState* state = malloc(sizeof(PikaState));
    if (!state) return NULL;

    state->input = input;
    state->input_len = strlen(input);
    state->num_rules = num_rules;
    state->rules = rules;
    state->output_mode = PIKA_OUTPUT_AST;  /* Default to AST mode */

    size_t table_size = (state->input_len + 1) * num_rules;
    state->table = calloc(table_size, sizeof(PikaMatch));
    if (!state->table) {
        free(state);
        return NULL;
    }

    return state;
}

void pika_free(PikaState* state) {
    if (!state) return;
    if (state->table) free(state->table);
    free(state);
}

void pika_set_output_mode(PikaState* state, PikaOutputMode mode) {
    if (!state) return;
    state->output_mode = mode;
}

PikaMatch* pika_get_match(PikaState* state, size_t pos, int rule_id) {
    if (pos > state->input_len || rule_id < 0 || rule_id >= state->num_rules) return NULL;
    return &state->table[pos * state->num_rules + rule_id];
}

static inline PikaMatch* get_match(PikaState* state, size_t pos, int rule_id) {
    return pika_get_match(state, pos, rule_id);
}

static PikaMatch evaluate_rule(PikaState* state, size_t pos, int rule_id) {
    PikaRule* rule = &state->rules[rule_id];
    PikaMatch m = {false, 0, NULL};

    switch (rule->type) {
        case PIKA_TERMINAL: {
            if (!rule->data.str) break;  /* Uninitialized rule */
            size_t len = strlen(rule->data.str);
            if (pos + len <= state->input_len &&
                strncmp(state->input + pos, rule->data.str, len) == 0) {
                m.matched = true;
                m.len = len;
            }
            break;
        }

        case PIKA_RANGE: {
            if (pos < state->input_len) {
                char c = state->input[pos];
                if (c >= rule->data.range.min && c <= rule->data.range.max) {
                    m.matched = true;
                    m.len = 1;
                }
            }
            break;
        }

        case PIKA_ANY: {
            if (pos < state->input_len) {
                m.matched = true;
                m.len = 1;
            }
            break;
        }

        case PIKA_SEQ: {
            size_t current_pos = pos;
            bool all_matched = true;
            for (int i = 0; i < rule->data.children.count; i++) {
                PikaMatch* sub = get_match(state, current_pos, rule->data.children.subrules[i]);
                if (!sub || !sub->matched) {
                    all_matched = false;
                    break;
                }
                current_pos += sub->len;
            }
            if (all_matched) {
                m.matched = true;
                m.len = current_pos - pos;
            }
            break;
        }

        case PIKA_ALT: {
            /* PEG Prioritized Choice: First one that matches wins */
            for (int i = 0; i < rule->data.children.count; i++) {
                PikaMatch* sub = get_match(state, pos, rule->data.children.subrules[i]);
                if (sub && sub->matched) {
                    m = *sub;
                    break;
                }
            }
            break;
        }

        case PIKA_REP: {
            /* Zero or more: A* */
            PikaMatch* first = get_match(state, pos, rule->data.children.subrules[0]);
            if (first && first->matched && first->len > 0) {
                PikaMatch* rest = get_match(state, pos + first->len, rule_id);
                if (rest && rest->matched) {
                    m.matched = true;
                    m.len = first->len + rest->len;
                } else {
                    m = *first;
                }
            } else {
                /* Match empty */
                m.matched = true;
                m.len = 0;
            }
            break;
        }

        case PIKA_POS: {
            /* One or more: A+ */
            PikaMatch* first = get_match(state, pos, rule->data.children.subrules[0]);
            if (first && first->matched) {
                m.matched = true;
                m.len = first->len;

                if (pos + first->len <= state->input_len) {
                    PikaMatch* more = get_match(state, pos + first->len, rule_id);
                    if (more && more->matched) {
                        m.len += more->len;
                    }
                }
            }
            break;
        }

        case PIKA_OPT: {
            PikaMatch* sub = get_match(state, pos, rule->data.children.subrules[0]);
            if (sub && sub->matched) {
                m = *sub;
            } else {
                m.matched = true;
                m.len = 0;
            }
            break;
        }

        case PIKA_NOT: {
            PikaMatch* sub = get_match(state, pos, rule->data.children.subrules[0]);
            if (!sub || !sub->matched) {
                m.matched = true;
                m.len = 0;
            }
            break;
        }

        case PIKA_AND: {
            PikaMatch* sub = get_match(state, pos, rule->data.children.subrules[0]);
            if (sub && sub->matched) {
                m.matched = true;
                m.len = 0;
            }
            break;
        }

        case PIKA_REF: {
            PikaMatch* sub = get_match(state, pos, rule->data.ref.subrule);
            if (sub) m = *sub;
            break;
        }
    }

    return m;
}

OmniValue* pika_run(PikaState* state, int root_rule_id) {
    /* Right-to-Left Pass with fixpoint iteration */
    for (ptrdiff_t pos = (ptrdiff_t)state->input_len; pos >= 0; pos--) {
        bool changed = true;
        int fixpoint_limit = state->num_rules * 2;
        int iters = 0;

        while (changed && iters < fixpoint_limit) {
            changed = false;
            iters++;

            for (int r = 0; r < state->num_rules; r++) {
                PikaMatch result = evaluate_rule(state, (size_t)pos, r);
                PikaMatch* existing = get_match(state, (size_t)pos, r);

                if (result.matched != existing->matched || result.len != existing->len) {
                    *existing = result;
                    /* Only run semantic actions in AST mode */
                    if (result.matched && state->rules[r].action && state->output_mode == PIKA_OUTPUT_AST) {
                        existing->val = state->rules[r].action(state, (size_t)pos, result);
                    }
                    changed = true;
                }
            }
        }
    }

    PikaMatch* root = get_match(state, 0, root_rule_id);
    if (root && root->matched) {
        /* In STRING mode, return raw matched text as OMNI_STRING */
        if (state->output_mode == PIKA_OUTPUT_STRING) {
            char* s = malloc(root->len + 1);
            memcpy(s, state->input, root->len);
            s[root->len] = '\0';
            OmniValue* v = omni_new_string(s);
            free(s);
            return v;
        }

        /* In AST mode (default), return processed AST node */
        if (root->val) return root->val;
        /* Fallback: return matched text as symbol if no action */
        char* s = malloc(root->len + 1);
        memcpy(s, state->input, root->len);
        s[root->len] = '\0';
        OmniValue* v = omni_new_sym(s);
        free(s);
        return v;
    }

    return omni_new_error("Parse failed");
}

/* Convenience function: Run pattern matching in one call
 * Creates PikaState, runs parser, and returns result.
 * This is the main entry point for runtime pattern matching.
 *
 * The returned OmniValue* is owned by the caller and should be freed
 * according to the value type (e.g., strings, symbols, AST nodes).
 *
 * Parameters:
 *   - input: Input string to parse
 *   - rules: Array of PikaRule definitions
 *   - num_rules: Number of rules in the array
 *   - root_rule: Index of the rule to use as root (typically 0)
 *
 * Returns:
 *   - OmniValue* representing the match result (may be AST node, string, symbol, or error)
 *   - NULL if parser state allocation failed
 */
OmniValue* omni_pika_match(const char* input, PikaRule* rules, int num_rules, int root_rule) {
    /* Validate input parameters */
    if (!input) {
        return omni_new_error("omni_pika_match: input is NULL");
    }
    if (!rules || num_rules <= 0) {
        return omni_new_error("omni_pika_match: invalid rules array");
    }
    if (root_rule < 0 || root_rule >= num_rules) {
        return omni_new_error("omni_pika_match: root_rule out of bounds");
    }

    /* Create parser state */
    PikaState* state = pika_new(input, rules, num_rules);
    if (!state) {
        return NULL;  /* Allocation failed */
    }

    /* Run the parser with the specified root rule */
    OmniValue* result = pika_run(state, root_rule);

    /* Clean up parser state */
    pika_free(state);

    return result;
}
