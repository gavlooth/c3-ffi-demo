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

#ifdef __cplusplus
}
#endif

#endif /* PIKA_PARSER_H */
