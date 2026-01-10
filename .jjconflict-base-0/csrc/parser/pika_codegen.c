/*
 * Pika Grammar to Code Transformation
 *
 * This module implements code generation for Pika grammar rules,
 * converting PikaRule structures into executable C code strings.
 *
 * The generated code can be compiled and executed for pattern matching,
 * providing an alternative to the runtime interpreter in pika_core.c.
 */

#include "pika.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Buffer for generated code */
typedef struct CodeBuffer {
    char* data;
    size_t size;
    size_t capacity;
} CodeBuffer;

/* Initialize a code buffer */
static CodeBuffer* code_buffer_new(size_t initial_capacity) {
    CodeBuffer* buf = malloc(sizeof(CodeBuffer));
    if (!buf) return NULL;

    buf->data = malloc(initial_capacity);
    if (!buf->data) {
        free(buf);
        return NULL;
    }

    buf->size = 0;
    buf->capacity = initial_capacity;
    buf->data[0] = '\0';
    return buf;
}

/* Free a code buffer */
static void code_buffer_free(CodeBuffer* buf) {
    if (!buf) return;
    if (buf->data) free(buf->data);
    free(buf);
}

/* Append string to code buffer, growing if needed */
static bool code_buffer_append(CodeBuffer* buf, const char* str) {
    size_t len = strlen(str);
    size_t new_size = buf->size + len;

    /* Grow buffer if needed */
    if (new_size >= buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        if (new_capacity < new_size + 1) {
            new_capacity = new_size + 1;
        }

        char* new_data = realloc(buf->data, new_capacity);
        if (!new_data) return false;

        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    /* Append string */
    memcpy(buf->data + buf->size, str, len + 1);  /* +1 for null terminator */
    buf->size = new_size;
    return true;
}

/* Append formatted string to code buffer */
static bool code_buffer_appendf(CodeBuffer* buf, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    /* Calculate required size */
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (len < 0) {
        va_end(args);
        return false;
    }

    /* Grow buffer if needed */
    size_t new_size = buf->size + (size_t)len;
    if (new_size >= buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        if (new_capacity < new_size + 1) {
            new_capacity = new_size + 1;
        }

        char* new_data = realloc(buf->data, new_capacity);
        if (!new_data) {
            va_end(args);
            return false;
        }

        buf->data = new_data;
        buf->capacity = new_capacity;
    }

    /* Write formatted string */
    vsnprintf(buf->data + buf->size, buf->capacity - buf->size, fmt, args);
    buf->size = new_size;
    va_end(args);
    return true;
}

/* Forward declaration for recursive code generation */
static bool pika_codegen_rule_recursive(PikaRule* rule, CodeBuffer* buf, int rule_id, const char* func_name);

/* Generate code for a terminal rule (literal string match) */
static bool codegen_terminal(PikaRule* rule, CodeBuffer* buf) {
    if (!rule->data.str) {
        return code_buffer_append(buf, "/* ERROR: Uninitialized terminal rule */ false");
    }

    /* Generate: (pos + len <= input_len && strncmp(input + pos, "literal", len) == 0) */
    code_buffer_appendf(buf, "(pos + %zu <= input_len && strncmp(input + pos, \"%s\", %zu) == 0)",
                        strlen(rule->data.str), rule->data.str, strlen(rule->data.str));
    return true;
}

/* Generate code for a character range rule */
static bool codegen_range(PikaRule* rule, CodeBuffer* buf) {
    /* Generate: (pos < input_len && input[pos] >= 'a' && input[pos] <= 'z') */
    code_buffer_appendf(buf, "(pos < input_len && input[pos] >= '%c' && input[pos] <= '%c')",
                        rule->data.range.min, rule->data.range.max);
    return true;
}

/* Generate code for any character (.) */
static bool codegen_any(CodeBuffer* buf) {
    code_buffer_append(buf, "(pos < input_len)");
    return true;
}

/* Generate code for sequence (A B C) */
static bool codegen_seq(PikaRule* rule, CodeBuffer* buf, int rule_id, const char* func_name) {
    code_buffer_append(buf, "({\n");
    code_buffer_append(buf, "    size_t current_pos = pos;\n");
    code_buffer_append(buf, "    bool all_matched = true;\n");

    for (int i = 0; i < rule->data.children.count; i++) {
        int subrule = rule->data.children.subrules[i];
        code_buffer_appendf(buf, "    if (!%s_rule_%d(input, input_len, &current_pos)) {\n",
                            func_name, subrule);
        code_buffer_append(buf, "        all_matched = false;\n");
        if (i < rule->data.children.count - 1) {
            code_buffer_append(buf, "    }\n");
        } else {
            code_buffer_append(buf, "    }\n    }\n");
        }
    }

    code_buffer_append(buf, "    if (all_matched) {\n");
    code_buffer_append(buf, "        *result_pos = current_pos;\n");
    code_buffer_append(buf, "        return true;\n");
    code_buffer_append(buf, "    }\n");
    code_buffer_append(buf, "    return false;\n");
    code_buffer_append(buf, "})");

    return true;
}

/* Generate code for prioritized choice (A / B / C) */
static bool codegen_alt(PikaRule* rule, CodeBuffer* buf, int rule_id, const char* func_name) {
    code_buffer_append(buf, "({\n");
    code_buffer_append(buf, "    size_t alt_pos;\n");

    for (int i = 0; i < rule->data.children.count; i++) {
        int subrule = rule->data.children.subrules[i];
        code_buffer_appendf(buf, "    alt_pos = pos;\n", subrule);
        code_buffer_appendf(buf, "    if (%s_rule_%d(input, input_len, &alt_pos)) {\n",
                            func_name, subrule);
        code_buffer_append(buf, "        *result_pos = alt_pos;\n");
        code_buffer_append(buf, "        return true;\n");
        code_buffer_append(buf, "    }\n");
    }

    code_buffer_append(buf, "    return false;\n");
    code_buffer_append(buf, "})");

    return true;
}

/* Generate code for repetition (A* or A+) */
static bool codegen_rep(PikaRule* rule, CodeBuffer* buf, int rule_id, const char* func_name, bool is_plus) {
    int subrule = rule->data.children.subrules[0];

    if (!is_plus) {
        /* Zero or more: A* */
        code_buffer_append(buf, "({\n");
        code_buffer_append(buf, "    size_t rep_pos = pos;\n");
        code_buffer_appendf(buf, "    while (%s_rule_%d(input, input_len, &rep_pos)) {\n",
                            func_name, subrule);
        code_buffer_append(buf, "        /* Keep matching */\n");
        code_buffer_append(buf, "    }\n");
        code_buffer_append(buf, "    *result_pos = rep_pos;\n");
        code_buffer_append(buf, "    return true;  /* Always succeeds (matches zero or more) */\n");
        code_buffer_append(buf, "})");
    } else {
        /* One or more: A+ */
        code_buffer_append(buf, "({\n");
        code_buffer_append(buf, "    size_t rep_pos = pos;\n");
        code_buffer_appendf(buf, "    if (!%s_rule_%d(input, input_len, &rep_pos)) {\n",
                            func_name, subrule);
        code_buffer_append(buf, "        return false;  /* Must match at least one */\n");
        code_buffer_append(buf, "    }\n");
        code_buffer_appendf(buf, "    while (%s_rule_%d(input, input_len, &rep_pos)) {\n",
                            func_name, subrule);
        code_buffer_append(buf, "        /* Keep matching */\n");
        code_buffer_append(buf, "    }\n");
        code_buffer_append(buf, "    *result_pos = rep_pos;\n");
        code_buffer_append(buf, "    return true;\n");
        code_buffer_append(buf, "})");
    }

    return true;
}

/* Generate code for optional (A?) */
static bool codegen_opt(PikaRule* rule, CodeBuffer* buf, int rule_id, const char* func_name) {
    int subrule = rule->data.children.subrules[0];

    code_buffer_append(buf, "({\n");
    code_buffer_append(buf, "    size_t opt_pos = pos;\n");
    code_buffer_appendf(buf, "    if (%s_rule_%d(input, input_len, &opt_pos)) {\n",
                        func_name, subrule);
    code_buffer_append(buf, "        *result_pos = opt_pos;\n");
    code_buffer_append(buf, "    } else {\n");
    code_buffer_append(buf, "        *result_pos = pos;  /* Match empty */\n");
    code_buffer_append(buf, "    }\n");
    code_buffer_append(buf, "    return true;  /* Always succeeds */\n");
    code_buffer_append(buf, "})");

    return true;
}

/* Generate code for negative lookahead (!A) */
static bool codegen_not(PikaRule* rule, CodeBuffer* buf, int rule_id, const char* func_name) {
    int subrule = rule->data.children.subrules[0];

    code_buffer_append(buf, "({\n");
    code_buffer_append(buf, "    size_t not_pos = pos;\n");
    code_buffer_appendf(buf, "    if (!%s_rule_%d(input, input_len, &not_pos)) {\n",
                        func_name, subrule);
    code_buffer_append(buf, "        *result_pos = pos;  /* Consumes nothing */\n");
    code_buffer_append(buf, "        return true;\n");
    code_buffer_append(buf, "    }\n");
    code_buffer_append(buf, "    return false;\n");
    code_buffer_append(buf, "})");

    return true;
}

/* Generate code for positive lookahead (&A) */
static bool codegen_and(PikaRule* rule, CodeBuffer* buf, int rule_id, const char* func_name) {
    int subrule = rule->data.children.subrules[0];

    code_buffer_append(buf, "({\n");
    code_buffer_append(buf, "    size_t and_pos = pos;\n");
    code_buffer_appendf(buf, "    if (%s_rule_%d(input, input_len, &and_pos)) {\n",
                        func_name, subrule);
    code_buffer_append(buf, "        *result_pos = pos;  /* Consumes nothing */\n");
    code_buffer_append(buf, "        return true;\n");
    code_buffer_append(buf, "    }\n");
    code_buffer_append(buf, "    return false;\n");
    code_buffer_append(buf, "})");

    return true;
}

/* Generate code for rule reference */
static bool codegen_ref(PikaRule* rule, CodeBuffer* buf, int rule_id, const char* func_name) {
    int target_rule = rule->data.ref.subrule;
    code_buffer_appendf(buf, "%s_rule_%d(input, input_len, result_pos)",
                        func_name, target_rule);
    return true;
}

/* Generate code for a single rule */
static bool pika_codegen_rule_recursive(PikaRule* rule, CodeBuffer* buf, int rule_id, const char* func_name) {
    if (!rule || !buf) return false;

    switch (rule->type) {
        case PIKA_TERMINAL:
            return codegen_terminal(rule, buf);

        case PIKA_RANGE:
            return codegen_range(rule, buf);

        case PIKA_ANY:
            return codegen_any(buf);

        case PIKA_SEQ:
            return codegen_seq(rule, buf, rule_id, func_name);

        case PIKA_ALT:
            return codegen_alt(rule, buf, rule_id, func_name);

        case PIKA_REP:
            return codegen_rep(rule, buf, rule_id, func_name, false);  /* Zero or more */

        case PIKA_POS:
            return codegen_rep(rule, buf, rule_id, func_name, true);  /* One or more */

        case PIKA_OPT:
            return codegen_opt(rule, buf, rule_id, func_name);

        case PIKA_NOT:
            return codegen_not(rule, buf, rule_id, func_name);

        case PIKA_AND:
            return codegen_and(rule, buf, rule_id, func_name);

        case PIKA_REF:
            return codegen_ref(rule, buf, rule_id, func_name);

        default:
            return code_buffer_append(buf, "/* ERROR: Unknown rule type */ false");
    }
}

/* Main entry point: Generate C code for a grammar rule
 *
 * This function generates a C function that implements the given PikaRule.
 * The generated function takes the form:
 *
 *   bool <name>_rule_<id>(const char* input, size_t input_len, size_t* result_pos)
 *
 * The function returns true if the rule matches, and false otherwise.
 * On success, result_pos is updated to the position after the match.
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
 * Caller is responsible for freeing the returned string.
 *
 * Example:
 *   PikaRule rules[] = { ... };
 *   char* code = pika_codegen_rule(&rules[0], 0, "arithmetic");
 *   if (code) {
 *       printf("Generated code:\n%s\n", code);
 *       free(code);
 *   }
 */
char* pika_codegen_rule(PikaRule* rule, int rule_id, const char* name) {
    if (!rule) return NULL;
    if (!name || name[0] == '\0') name = "pika";

    /* Create code buffer */
    CodeBuffer* buf = code_buffer_new(4096);
    if (!buf) return NULL;

    /* Generate function signature */
    code_buffer_appendf(buf, "/* Generated matcher for rule %d: %s */\n",
                        rule_id, rule->name ? rule->name : "(unnamed)");
    code_buffer_appendf(buf, "bool %s_rule_%d(const char* input, size_t input_len, size_t* result_pos) {\n",
                        name, rule_id);

    /* Generate function body */
    code_buffer_append(buf, "    size_t pos = *result_pos;\n");
    code_buffer_append(buf, "    ");

    /* Generate the matching logic */
    if (!pika_codegen_rule_recursive(rule, buf, rule_id, name)) {
        code_buffer_free(buf);
        return NULL;
    }

    code_buffer_append(buf, ";\n}\n");

    /* Extract generated code string */
    char* result = strdup(buf->data);
    code_buffer_free(buf);

    return result;
}

/* Generate C code for an entire grammar (array of rules)
 *
 * This function generates C code for all rules in a grammar,
 * creating a complete matcher module.
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
 * Caller is responsible for freeing the returned string.
 */
char* pika_codegen_grammar(PikaRule* rules, int num_rules, const char* name) {
    if (!rules || num_rules <= 0) return NULL;
    if (!name || name[0] == '\0') name = "pika";

    /* Create code buffer */
    CodeBuffer* buf = code_buffer_new(16384);  /* Larger buffer for full grammar */
    if (!buf) return NULL;

    /* Add file header */
    code_buffer_append(buf, "/*\n");
    code_buffer_appendf(buf, " * Generated Pika Grammar Matcher: %s\n", name);
    code_buffer_append(buf, " *\n");
    code_buffer_append(buf, " * This file was automatically generated by pika_codegen_grammar().\n");
    code_buffer_append(buf, " * Do not edit manually.\n");
    code_buffer_append(buf, " */\n\n");
    code_buffer_append(buf, "#include <stdbool.h>\n");
    code_buffer_append(buf, "#include <stddef.h>\n");
    code_buffer_append(buf, "#include <string.h>\n\n");

    /* Generate forward declarations for all rules */
    code_buffer_append(buf, "/* Forward declarations */\n");
    for (int i = 0; i < num_rules; i++) {
        code_buffer_appendf(buf, "bool %s_rule_%d(const char* input, size_t input_len, size_t* result_pos);\n",
                            name, i);
    }
    code_buffer_append(buf, "\n");

    /* Generate each rule function */
    code_buffer_append(buf, "/* Rule implementations */\n");
    for (int i = 0; i < num_rules; i++) {
        char* rule_code = pika_codegen_rule(&rules[i], i, name);
        if (!rule_code) {
            code_buffer_free(buf);
            return NULL;
        }

        code_buffer_append(buf, rule_code);
        free(rule_code);
        code_buffer_append(buf, "\n");
    }

    /* Generate convenience wrapper function */
    code_buffer_append(buf, "/* Convenience wrapper: Match root rule (rule 0) */\n");
    code_buffer_appendf(buf, "bool %s_match(const char* input, size_t input_len, size_t* result_pos) {\n", name);
    code_buffer_appendf(buf, "    return %s_rule_0(input, input_len, result_pos);\n", name);
    code_buffer_append(buf, "}\n");

    /* Extract generated code string */
    char* result = strdup(buf->data);
    code_buffer_free(buf);

    return result;
}
