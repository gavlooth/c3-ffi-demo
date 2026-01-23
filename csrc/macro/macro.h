/*
 * OmniLisp Hygienic Macro System
 *
 * Scheme-style syntax-rules pattern matching with mark-based hygiene.
 * Macros are expanded at compile-time between parsing and codegen.
 *
 * Target syntax:
 *   (define [syntax name]
 *     [literals symbol1 symbol2 ...]     ;; optional
 *     [(name pattern1) template1]
 *     [(name pattern2) template2])
 */

#ifndef OMNILISP_MACRO_H
#define OMNILISP_MACRO_H

#include "../ast/ast.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Forward Declarations ============== */

typedef struct MacroPatternVar MacroPatternVar;
typedef struct MacroRule MacroRule;
typedef struct MacroDef MacroDef;
typedef struct HygieneContext HygieneContext;
typedef struct RenameEntry RenameEntry;
typedef struct MacroExpander MacroExpander;
typedef struct PatternBinding PatternBinding;
typedef struct MatchResult MatchResult;

/* ============== Pattern Variable ============== */

/*
 * Pattern variable extracted from a macro pattern.
 * Tracks whether followed by ellipsis (...) and nesting depth.
 */
struct MacroPatternVar {
    char* name;                     /* Variable name */
    bool is_ellipsis;               /* Followed by ... */
    int depth;                      /* Ellipsis nesting depth (0 = not under ellipsis) */
    MacroPatternVar* next;          /* Linked list */
};

/* ============== Macro Rule ============== */

/*
 * Single pattern -> template transformation rule.
 * Rules are tried in order until one matches.
 */
struct MacroRule {
    OmniValue* pattern;             /* (name pat1 pat2 ...) */
    OmniValue* template;            /* Expansion template */
    MacroPatternVar* vars;          /* Extracted pattern variables */
    MacroRule* next;                /* Linked list */
};

/* ============== Macro Definition ============== */

/*
 * Complete macro definition with name, literals, and rules.
 */
struct MacroDef {
    char* name;                     /* Macro name */
    char** literals;                /* Symbols that match literally */
    size_t literal_count;
    MacroRule* rules;               /* Rules tried in order */
    int definition_mark;            /* Hygiene mark at definition time */
    MacroDef* next;                 /* Linked list for expander's macro table */
};

/* ============== Hygiene Context ============== */

/*
 * Rename table entry for hygiene tracking.
 * Maps (original_name, mark) -> renamed_name
 */
struct RenameEntry {
    char* original;                 /* Original symbol name */
    char* renamed;                  /* Renamed (hygienic) symbol name */
    int mark;                       /* Mark at which rename occurred */
    RenameEntry* next;
};

/*
 * Hygiene context tracks current mark and rename table.
 */
struct HygieneContext {
    int current_mark;               /* Incremented on each macro expansion */
    RenameEntry* renames;           /* Rename table */
};

/* ============== Pattern Binding ============== */

/*
 * Single binding from pattern matching.
 * For ellipsis patterns, stores array of matched values.
 */
struct PatternBinding {
    char* var_name;                 /* Pattern variable name */
    OmniValue** values;             /* Array of bound values (1 for non-ellipsis) */
    size_t value_count;             /* Number of values */
    int depth;                      /* Ellipsis depth for this binding */
    PatternBinding* next;           /* Linked list */
};

/*
 * Result of pattern matching attempt.
 */
struct MatchResult {
    bool success;                   /* Whether match succeeded */
    PatternBinding* bindings;       /* Variable bindings if successful */
};

/* ============== Macro Expander ============== */

/*
 * Main macro expansion state.
 */
struct MacroExpander {
    MacroDef* macros;               /* Registered macro definitions */
    HygieneContext* hygiene;        /* Hygiene tracking */
    int expansion_depth;            /* Current expansion depth */
    int max_depth;                  /* Maximum depth (default: 1000) */
    int gensym_counter;             /* Counter for generating unique symbols */
    OmniArena* arena;               /* Arena for allocations */
    char** errors;                  /* Error messages */
    size_t error_count;
    size_t error_capacity;
};

/* ============== Expander Lifecycle ============== */

/* Create a new macro expander */
MacroExpander* omni_macro_expander_new(void);

/* Free macro expander resources */
void omni_macro_expander_free(MacroExpander* exp);

/* Reset expander state (keeps registered macros) */
void omni_macro_expander_reset(MacroExpander* exp);

/* ============== Error Handling ============== */

/* Check if expander has errors */
bool omni_macro_expander_has_errors(MacroExpander* exp);

/* Get error count */
size_t omni_macro_expander_error_count(MacroExpander* exp);

/* Get error message at index */
const char* omni_macro_expander_get_error(MacroExpander* exp, size_t index);

/* Add an error message */
void omni_macro_add_error(MacroExpander* exp, const char* fmt, ...);

/* ============== Macro Registration ============== */

/* Check if expression is a syntax definition */
bool omni_macro_is_syntax_define(OmniValue* expr);

/* Register a macro from a syntax definition */
bool omni_macro_register(MacroExpander* exp, OmniValue* syntax_def);

/* Lookup a macro by name */
MacroDef* omni_macro_lookup(MacroExpander* exp, const char* name);

/* ============== Main Expansion API ============== */

/*
 * Expand all macros in a program.
 * - Collects macro definitions (removes them from expressions)
 * - Expands until fixed point
 * Returns modified expression array; count may change.
 */
OmniValue** omni_macro_expand_program(MacroExpander* exp,
                                       OmniValue** exprs,
                                       size_t* count);

/*
 * Expand a single expression recursively.
 * Returns the expanded expression (may be same as input if no expansion).
 */
OmniValue* omni_macro_expand_expr(MacroExpander* exp, OmniValue* expr);

/* ============== Pattern Matching (pattern.c) ============== */

/*
 * Match a pattern against input, producing bindings.
 * Returns MatchResult with success flag and bindings.
 */
MatchResult* omni_macro_match_pattern(MacroExpander* exp,
                                       OmniValue* pattern,
                                       OmniValue* input,
                                       char** literals,
                                       size_t literal_count);

/* Free a match result */
void omni_macro_match_result_free(MatchResult* result);

/* Extract pattern variables from a pattern */
MacroPatternVar* omni_macro_extract_pattern_vars(MacroExpander* exp,
                                                  OmniValue* pattern,
                                                  char** literals,
                                                  size_t literal_count,
                                                  int depth);

/* Check if a symbol is in the literals list */
bool omni_macro_is_literal(const char* sym, char** literals, size_t literal_count);

/* ============== Template Substitution (template.c) ============== */

/*
 * Substitute pattern bindings into a template.
 * Handles ellipsis unfolding and hygiene marking.
 */
OmniValue* omni_macro_substitute(MacroExpander* exp,
                                  OmniValue* template,
                                  PatternBinding* bindings,
                                  int mark);

/* Get the number of values for an ellipsis binding */
size_t omni_macro_ellipsis_count(PatternBinding* bindings, OmniValue* template);

/* Slice bindings for ellipsis iteration */
PatternBinding* omni_macro_slice_bindings(MacroExpander* exp,
                                           PatternBinding* bindings,
                                           size_t index);

/* Check if a symbol is bound in the bindings */
bool omni_macro_is_bound(const char* name, PatternBinding* bindings);

/* Get a binding by name */
PatternBinding* omni_macro_get_binding(const char* name, PatternBinding* bindings);

/* ============== Hygiene (hygiene.c) ============== */

/* Create a new hygiene context */
HygieneContext* omni_macro_hygiene_new(void);

/* Free hygiene context */
void omni_macro_hygiene_free(HygieneContext* ctx);

/* Generate a fresh symbol with prefix */
OmniValue* omni_macro_gensym(MacroExpander* exp, const char* prefix);

/* Apply hygiene mark to a symbol */
OmniValue* omni_macro_apply_mark(MacroExpander* exp, OmniValue* sym, int mark);

/* Lookup rename in hygiene context */
RenameEntry* omni_macro_lookup_rename(HygieneContext* ctx,
                                       const char* original,
                                       int mark);

/* Record a rename in hygiene context */
void omni_macro_record_rename(HygieneContext* ctx,
                               const char* original,
                               const char* renamed,
                               int mark);

/* Get the next hygiene mark */
int omni_macro_next_mark(MacroExpander* exp);

/* ============== Utility Functions ============== */

/* Check if next element in list is ellipsis (...) */
bool omni_macro_next_is_ellipsis(OmniValue* list, size_t index);

/* Check if a value is the ellipsis symbol */
bool omni_macro_is_ellipsis(OmniValue* v);

/* Deep copy an OmniValue */
OmniValue* omni_macro_deep_copy(OmniValue* v);

/* Print a macro for debugging */
void omni_macro_print_def(MacroDef* def);

/* Print bindings for debugging */
void omni_macro_print_bindings(PatternBinding* bindings);

#ifdef __cplusplus
}
#endif

#endif /* OMNILISP_MACRO_H */
