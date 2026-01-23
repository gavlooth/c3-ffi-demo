/*
 * Pika Parser Core Implementation
 *
 * Based on the Pika parsing algorithm - a packrat PEG parser.
 * Uses right-to-left pass with memoization for O(n) parsing.
 */

#include "pika.h"
#include <stdlib.h>
#include <string.h>
#include <wyhash.h>

/* ============== Pattern Cache (T-wire-pika-compile-03) ============== */

/*
 * Pattern cache entry for storing compiled patterns.
 * Uses string-based keys to cache PikaState objects for identical patterns.
 *
 * Cache key: Combination of pattern string + rules array hash
 * Cache value: PikaState* (compiled pattern ready for matching)
 */
typedef struct PatternCacheEntry {
    char* pattern;                    /* Pattern string (cache key part 1) */
    size_t rules_hash;                /* Hash of rules array (cache key part 2) */
    PikaState* compiled_state;        /* Cached compiled parser state */
    struct PatternCacheEntry* next;   /* Next entry in bucket (for chaining) */
} PatternCacheEntry;

/*
 * Pattern cache structure.
 * Simple hash table with string keys and chaining for collision resolution.
 */
typedef struct PatternCache {
    PatternCacheEntry** buckets;
    size_t bucket_count;
    size_t entry_count;
} PatternCache;

/* Global pattern cache instance */
static PatternCache* g_pattern_cache = NULL;

/* Initial number of buckets for the pattern cache */
#define PATTERN_CACHE_INITIAL_BUCKETS 32

/*
 * String hashing function using wyhash.
 * wyhash is a high-quality hash that passes BigCrush/PractRand tests.
 * See: https://github.com/wangyi-fudan/wyhash (public domain)
 */
static size_t hash_string(const char* str) {
    if (!str) return 0;
    return (size_t)wyhash(str, strlen(str), 0, _wyp);
}

/* Compute hash for a rules array */
static size_t hash_rules(PikaRule* rules, int num_rules) {
    if (!rules || num_rules <= 0) return 0;

    /* Simple hash combining rule types and key data */
    size_t hash = 0;
    for (int i = 0; i < num_rules; i++) {
        /* Hash the rule type */
        hash = (hash << 5) + hash + (size_t)rules[i].type;

        /* Hash relevant data based on rule type */
        switch (rules[i].type) {
            case PIKA_TERMINAL:
                if (rules[i].data.str) {
                    hash = (hash << 5) + hash + hash_string(rules[i].data.str);
                }
                break;
            case PIKA_RANGE:
                hash = (hash << 5) + hash + rules[i].data.range.min;
                hash = (hash << 5) + hash + rules[i].data.range.max;
                break;
            case PIKA_SEQ:
            case PIKA_ALT:
                hash = (hash << 5) + hash + rules[i].data.children.count;
                break;
            case PIKA_REF:
                hash = (hash << 5) + hash + rules[i].data.ref.subrule;
                break;
            default:
                /* For other rule types, just use type */
                break;
        }
    }
    return hash;
}

/* Initialize the global pattern cache */
static void pattern_cache_init(void) {
    if (g_pattern_cache) return; /* Already initialized */

    g_pattern_cache = malloc(sizeof(PatternCache));
    if (!g_pattern_cache) return;

    g_pattern_cache->bucket_count = PATTERN_CACHE_INITIAL_BUCKETS;
    g_pattern_cache->entry_count = 0;

    g_pattern_cache->buckets = calloc(PATTERN_CACHE_INITIAL_BUCKETS, sizeof(PatternCacheEntry*));
    if (!g_pattern_cache->buckets) {
        free(g_pattern_cache);
        g_pattern_cache = NULL;
    }
}

/* Clean up the global pattern cache */
static void pattern_cache_cleanup(void) {
    if (!g_pattern_cache) return;

    /* Free all entries, their pattern strings, and cached PikaState objects */
    for (size_t i = 0; i < g_pattern_cache->bucket_count; i++) {
        PatternCacheEntry* entry = g_pattern_cache->buckets[i];
        while (entry) {
            PatternCacheEntry* next = entry->next;
            free(entry->pattern);
            /* Free the cached PikaState object */
            if (entry->compiled_state) {
                pika_free(entry->compiled_state);
            }
            free(entry);
            entry = next;
        }
        g_pattern_cache->buckets[i] = NULL;
    }

    free(g_pattern_cache->buckets);
    free(g_pattern_cache);
    g_pattern_cache = NULL;
}

/* Look up a pattern in the cache */
static PikaState* pattern_cache_get(const char* pattern, PikaRule* rules, int num_rules) {
    if (!g_pattern_cache || !pattern || !rules || num_rules <= 0) {
        return NULL;
    }

    /* Compute cache key: hash of pattern string + rules hash */
    size_t pattern_hash = hash_string(pattern);
    size_t rules_hash_val = hash_rules(rules, num_rules);
    size_t combined_hash = pattern_hash ^ rules_hash_val;

    size_t bucket = combined_hash % g_pattern_cache->bucket_count;

    /* Search for matching entry in bucket */
    PatternCacheEntry* entry = g_pattern_cache->buckets[bucket];
    while (entry) {
        /* Check if this entry matches our pattern and rules */
        if (entry->rules_hash == rules_hash_val &&
            strcmp(entry->pattern, pattern) == 0) {
            /* Found cached compiled state */
            return entry->compiled_state;
        }
        entry = entry->next;
    }

    return NULL; /* Not found in cache */
}

/* Add a compiled pattern to the cache */
static void pattern_cache_put(const char* pattern, PikaRule* rules, int num_rules, PikaState* compiled) {
    if (!g_pattern_cache || !pattern || !rules || num_rules <= 0 || !compiled) {
        return;
    }

    /* Compute cache key */
    size_t pattern_hash = hash_string(pattern);
    size_t rules_hash_val = hash_rules(rules, num_rules);
    size_t combined_hash = pattern_hash ^ rules_hash_val;

    size_t bucket = combined_hash % g_pattern_cache->bucket_count;

    /* Create new cache entry */
    PatternCacheEntry* entry = malloc(sizeof(PatternCacheEntry));
    if (!entry) return;

    /* Duplicate pattern string for cache key */
    entry->pattern = strdup(pattern);
    if (!entry->pattern) {
        free(entry);
        return;
    }

    entry->rules_hash = rules_hash_val;
    entry->compiled_state = compiled;

    /* Add to bucket (at head of chain) */
    entry->next = g_pattern_cache->buckets[bucket];
    g_pattern_cache->buckets[bucket] = entry;
    g_pattern_cache->entry_count++;
}

/* Clear the pattern cache (useful for testing or memory management) */
void pika_pattern_cache_clear(void) {
    pattern_cache_cleanup();
}

/* Get pattern cache statistics (for debugging/monitoring) */
void pika_pattern_cache_stats(PatternCacheStats* stats) {
    if (!stats) return;
    if (!g_pattern_cache) {
        stats->entry_count = 0;
        stats->bucket_count = 0;
        return;
    }
    stats->entry_count = g_pattern_cache->entry_count;
    stats->bucket_count = g_pattern_cache->bucket_count;
}

/* ============== End Pattern Cache (T-wire-pika-compile-03) ============== */

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

                /*
                 * IMPORTANT: Value propagation for non-action rules.
                 *
                 * The parser computes (matched,len) via a fixpoint. Semantic actions
                 * run only when (matched,len) changes to avoid infinite loops caused
                 * by actions allocating fresh AST nodes each time.
                 *
                 * However, rules WITHOUT semantic actions (e.g. high-level wrapper
                 * rules like R_EXPR) depend on their children's `val` fields for the
                 * final AST.
                 *
                 * Because rule evaluation runs in numeric rule-id order, it is very
                 * common that a wrapper rule like R_EXPR becomes (matched,len) stable
                 * *before* its child rule (like R_LIST) has had a chance to run its
                 * semantic action and populate `val`.
                 *
                 * If we only update memo entries when (matched,len) changes, those
                 * wrapper rules can remain with `val == NULL` forever, which makes
                 * pika_run() fall back to "matched text as symbol" or causes the
                 * compiler front-end to see an empty program.
                 *
                 * Fix:
                 *   For rules that DO NOT have semantic actions, treat a change in
                 *   the propagated `val` pointer as a change that should update the
                 *   memo table entry.
                 *
                 * We intentionally do NOT do this for action rules because their
                 * actions allocate fresh objects; pointer inequality would prevent
                 * convergence.
                 */
                bool val_changed = false;
                if (state->output_mode == PIKA_OUTPUT_AST &&
                    result.matched &&
                    state->rules[r].action == NULL &&
                    result.val != existing->val) {
                    val_changed = true;
                }

                if (result.matched != existing->matched || result.len != existing->len || val_changed) {
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

    /*
     * Semantic stabilization pass (AST mode only)
     *
     * Even with value propagation for non-action rules, action rules can still
     * produce "early" placeholder values if they ran before wrapper rules like
     * R_EXPR had a chance to propagate their child values.
     *
     * Example failure mode:
     *   - R_PROGRAM_INNER matches the input early (matched,len stabilized),
     *   - act_program_inner runs before R_EXPR has a usable `.val`,
     *   - act_program_inner returns OMNI_NIL,
     *   - later iterations fix R_EXPR.val, but R_PROGRAM_INNER does not re-run
     *     its action because (matched,len) didn't change.
     *
     * This pass re-runs ALL semantic actions once after the structural
     * (matched,len) fixpoint has converged, ensuring actions see the stabilized
     * values of their children.
     *
     * Important:
     *   We do NOT attempt to treat semantic pointer changes as part of the main
     *   convergence metric because actions allocate fresh AST nodes; pointer
     *   equality is not a meaningful notion of convergence for action rules.
     */
    if (state->output_mode == PIKA_OUTPUT_AST) {
        for (ptrdiff_t pos = (ptrdiff_t)state->input_len; pos >= 0; pos--) {
            for (int r = 0; r < state->num_rules; r++) {
                PikaMatch* m = get_match(state, (size_t)pos, r);
                if (!m || !m->matched) continue;
                if (!state->rules[r].action) continue;
                m->val = state->rules[r].action(state, (size_t)pos, *m);
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

/* Compile a pattern string for later use
 * This function creates a parser state from a pattern string and returns it.
 * The returned parser state can be used with pika_run for matching.
 *
 * This is essentially a wrapper around pika_new that provides a more ergonomic
 * API for "compiling" patterns - the pattern is the input text that will be
 * parsed, and the rules define how to parse it.
 *
 * Parameters:
 *   - pattern: Pattern string to compile (input text for the parser)
 *   - rules: Array of PikaRule definitions
 *   - num_rules: Number of rules in the array
 *
 * Returns:
 *   - PikaState* representing the compiled pattern (ready for matching)
 *   - NULL if allocation failed
 *
 * Note: Caller is responsible for freeing the returned PikaState with pika_free()
 */
PikaState* omni_compile_pattern(const char* pattern, PikaRule* rules, int num_rules) {
    /* Validate input parameters */
    if (!pattern) {
        return NULL;
    }
    if (!rules || num_rules <= 0) {
        return NULL;
    }

    /* T-wire-pika-compile-03: Initialize pattern cache on first use */
    pattern_cache_init();

    /* T-wire-pika-compile-03: Check if we have a cached compiled state */
    PikaState* cached = pattern_cache_get(pattern, rules, num_rules);
    if (cached) {
        /* Return cached compiled state (no need to recompile) */
        return cached;
    }

    /* Not in cache - create new parser state */
    PikaState* state = pika_new(pattern, rules, num_rules);
    if (!state) {
        return NULL;
    }

    /* T-wire-pika-compile-03: Cache the newly compiled state */
    pattern_cache_put(pattern, rules, num_rules, state);

    return state;
}

/* Extract captured substrings from match results
 * This function extracts the matched text for specified sub-rules at their respective positions.
 *
 * Implementation Notes:
 * - Uses the memoization table to retrieve match results for each rule at each position
 * - Extracts substrings from the input using position and length information
 * - Returns an OMNI_ARRAY of OMNI_STRING values
 */
OmniValue* pika_extract_captures(PikaState* state, int* rule_ids, size_t* positions, int num_captures) {
    /* Validate input parameters */
    if (!state) {
        return omni_new_error("pika_extract_captures: state is NULL");
    }
    if (!state->input) {
        return omni_new_error("pika_extract_captures: state input is NULL");
    }
    if (!rule_ids || !positions || num_captures <= 0) {
        /* Return empty array for no captures requested */
        return omni_new_array(0);
    }

    /* Create array to hold captured strings */
    OmniValue* result = omni_new_array(num_captures);
    if (!result) {
        return omni_new_error("pika_extract_captures: failed to allocate result array");
    }

    /* Set the array length to match num_captures (omni_new_array creates with len=0) */
    result->array.len = num_captures;

    /* Extract each capture */
    for (int i = 0; i < num_captures; i++) {
        int rule_id = rule_ids[i];
        size_t pos = positions[i];

        /* Validate rule_id */
        if (rule_id < 0 || rule_id >= state->num_rules) {
            /* Add error string as placeholder */
            result->array.data[i] = omni_new_string("<invalid rule id>");
            continue;
        }

        /* Validate position */
        if (pos > state->input_len) {
            result->array.data[i] = omni_new_string("<position out of bounds>");
            continue;
        }

        /* Get match result for this rule at the specified position */
        PikaMatch* match = pika_get_match(state, pos, rule_id);

        if (!match || !match->matched || match->len == 0) {
            /* No match for this rule - add empty string */
            result->array.data[i] = omni_new_string("");
            continue;
        }

        /* Extract the matched substring from input */
        /* Safety check: ensure we don't read past input end */
        size_t end_pos = pos + match->len;
        if (end_pos > state->input_len) {
            end_pos = state->input_len;
        }

        /* Allocate buffer for the substring */
        size_t capture_len = end_pos - pos;
        char* capture_str = malloc(capture_len + 1);
        if (!capture_str) {
            result->array.data[i] = omni_new_string("<malloc failed>");
            continue;
        }

        /* Copy the matched substring */
        memcpy(capture_str, state->input + pos, capture_len);
        capture_str[capture_len] = '\0';

        /* Create string value and add to result array */
        result->array.data[i] = omni_new_string(capture_str);
        free(capture_str);  /* omni_new_string makes its own copy */
    }

    return result;
}
