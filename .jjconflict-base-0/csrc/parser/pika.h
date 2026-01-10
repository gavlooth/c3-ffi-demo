/*
 * Pika Parser - Packrat PEG Parser
 *
 * Based on the Pika parsing algorithm.
 * Adapted for OmniLisp C toolchain.
 */

#ifndef PIKA_PARSER_H
#define PIKA_PARSER_H

#include "../ast/ast.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
struct PikaState;
struct PikaMatch;

/* Match result */
typedef struct PikaMatch {
    bool matched;
    size_t len;
    OmniValue* val;  /* Cached AST node */
} PikaMatch;

/* Semantic action callback */
typedef OmniValue* (*PikaActionFn)(struct PikaState* state, size_t pos, PikaMatch match);

/* Output mode for parser */
typedef enum {
    PIKA_OUTPUT_AST,      /* Default: Return processed AST nodes (via semantic actions) */
    PIKA_OUTPUT_STRING    /* Return raw matched text as OMNI_STRING */
} PikaOutputMode;

/* Rule types */
typedef enum {
    PIKA_TERMINAL,  /* Literal string */
    PIKA_RANGE,     /* Character range [a-z] */
    PIKA_ANY,       /* Any character (.) */
    PIKA_SEQ,       /* Sequence (A B C) */
    PIKA_ALT,       /* Prioritized Choice (A / B / C) */
    PIKA_REP,       /* Zero-or-more (A*) */
    PIKA_POS,       /* One-or-more (A+) */
    PIKA_OPT,       /* Optional (A?) */
    PIKA_NOT,       /* Negative lookahead (!A) */
    PIKA_AND,       /* Positive lookahead (&A) */
    PIKA_REF        /* Reference to another rule (by ID) */
} PikaRuleType;

/* Rule definition */
typedef struct PikaRule {
    PikaRuleType type;
    union {
        const char* str;
        struct { char min; char max; } range;
        struct { int* subrules; int count; } children;
        struct { int subrule; } ref;
    } data;
    const char* name;       /* Optional name for debugging */
    PikaActionFn action;    /* Semantic action */
} PikaRule;

/* Parser state */
typedef struct PikaState {
    const char* input;
    size_t input_len;

    int num_rules;
    PikaRule* rules;

    /* Output mode: AST (default) or STRING (raw text) */
    PikaOutputMode output_mode;

    /* Memoization table: [input_len + 1][num_rules] */
    PikaMatch* table;
} PikaState;

/* ============== Public API ============== */

/* Create a new parser state */
PikaState* pika_new(const char* input, PikaRule* rules, int num_rules);

/* Free parser state */
void pika_free(PikaState* state);

/* Set the output mode for the parser */
void pika_set_output_mode(PikaState* state, PikaOutputMode mode);

/* Run the parser and return the result of the root rule at position 0 */
OmniValue* pika_run(PikaState* state, int root_rule_id);

/* Get match at position for rule (useful for semantic actions) */
PikaMatch* pika_get_match(PikaState* state, size_t pos, int rule_id);

/* Convenience function: Run pattern matching in one call
 * Creates PikaState, runs parser, and returns result.
 * Caller is responsible for freeing the returned OmniValue (if applicable).
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
OmniValue* omni_pika_match(const char* input, PikaRule* rules, int num_rules, int root_rule);

/* Extract captured substrings from match results
 * This function extracts the matched text for specified sub-rules at their respective positions.
 * Useful for extracting capture groups from complex pattern matches.
 *
 * Parameters:
 *   - state: Parser state (must have already run pika_run)
 *   - rule_ids: Array of rule IDs to extract (indices into state->rules)
 *   - positions: Array of positions where each rule was matched (same length as rule_ids)
 *   - num_captures: Number of rule IDs in the array
 *
 * Returns:
 *   - OmniValue* representing an array of captured strings (OMNI_ARRAY)
 *   - Each element is an OMNI_STRING containing the matched text
 *   - NULL if state is invalid or allocation failed
 *   - Empty array if captures array is NULL or num_captures is 0
 *
 * Example:
 *   // After matching email pattern "user@host", extract user and host parts
 *   // Rule 1 (user) matches at position 0, Rule 2 (host) matches at position 5
 *   int capture_ids[] = {1, 2};        // Rule IDs for "user" and "host"
 *   size_t positions[] = {0, 5};       // Positions where each rule matched
 *   OmniValue* captures = pika_extract_captures(state, capture_ids, positions, 2);
 *   // captures->array.data[0] = "user", captures->array.data[1] = "host"
 */
OmniValue* pika_extract_captures(PikaState* state, int* rule_ids, size_t* positions, int num_captures);

/* Compile a pattern string for later use
 * This function creates a parser state from a pattern string and returns it.
 * The returned parser state can be used with pika_run for matching.
 *
 * **CACHE BEHAVIOR (T-wire-pika-compile-03):**
 * This function uses an internal cache to avoid recreating parser states
 * for identical (pattern, rules) combinations. If the same pattern and rules
 * are requested again, the cached PikaState is returned.
 *
 * **IMPORTANT:** Due to caching, the caller should NOT free the returned
 * PikaState. Use pika_pattern_cache_clear() to clean up all cached states
 * when done (e.g., at program exit or between test runs).
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
 * Example:
 *   PikaRule rules[] = { ... pattern rules ... };
 *   PikaState* compiled = omni_compile_pattern("hello", rules, 1);
 *   if (compiled) {
 *       OmniValue* result = pika_run(compiled, 0);
 *       // ... use result ...
 *       // DO NOT call pika_free(compiled) - it's managed by the cache
 *   }
 *   pika_pattern_cache_clear();  // Clean up when done
 */
PikaState* omni_compile_pattern(const char* pattern, PikaRule* rules, int num_rules);

/* Generate C code for a single Pika grammar rule
 *
 * This function generates a C function that implements pattern matching
 * for the given PikaRule. The generated code can be compiled separately
 * for better performance than the runtime interpreter.
 *
 * Parameters:
 *   - rule: The PikaRule to generate code for
 *   - rule_id: Unique ID for this rule (used in function naming)
 *   - name: Base name for the generated function (e.g., "my_grammar")
 *
 * Returns:
 *   - Newly allocated string containing the generated C code
 *   - NULL if code generation failed
 *
 * Caller is responsible for freeing the returned string with free().
 *
 * Example:
 *   char* code = pika_codegen_rule(&rules[0], 0, "arithmetic");
 *   // Use or compile the generated code...
 *   free(code);
 */
char* pika_codegen_rule(PikaRule* rule, int rule_id, const char* name);

/* Generate C code for an entire grammar (array of rules)
 *
 * This function generates complete C code for all rules in a grammar,
 * creating a standalone matcher module.
 *
 * Parameters:
 *   - rules: Array of PikaRule structures
 *   - num_rules: Number of rules in the array
 *   - name: Base name for the generated functions
 *
 * Returns:
 *   - Newly allocated string containing the complete generated C code
 *   - NULL if code generation failed
 *
 * Caller is responsible for freeing the returned string with free().
 *
 * Example:
 *   char* code = pika_codegen_grammar(rules, 3, "arithmetic");
 *   // Save to file, compile, or use the generated code...
 *   free(code);
 */
char* pika_codegen_grammar(PikaRule* rules, int num_rules, const char* name);

/* ============== Pattern Cache API (T-wire-pika-compile-03) ============== */

/*
 * Pattern cache statistics structure
 * Used for monitoring cache usage and effectiveness
 */
typedef struct {
    size_t entry_count;  /* Number of cached patterns */
    size_t bucket_count; /* Number of buckets in the hash table */
} PatternCacheStats;

/*
 * Clear the pattern cache
 * Frees all cached patterns and resets the cache to empty state.
 * Useful for testing or memory management in long-running applications.
 *
 * Example:
 *   pika_pattern_cache_clear();  // Clear all cached patterns
 */
void pika_pattern_cache_clear(void);

/*
 * Get pattern cache statistics
 * Retrieves information about the current state of the pattern cache.
 *
 * Parameters:
 *   - stats: Pointer to PatternCacheStats structure to fill
 *
 * Example:
 *   PatternCacheStats stats;
 *   pika_pattern_cache_stats(&stats);
 *   printf("Cached patterns: %zu\n", stats.entry_count);
 */
void pika_pattern_cache_stats(PatternCacheStats* stats);

/* ============== End Pattern Cache API ============== */

#ifdef __cplusplus
}
#endif

#endif /* PIKA_PARSER_H */
