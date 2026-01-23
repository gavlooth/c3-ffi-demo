/*
 * regex.c - High-level Regex API for OmniLisp
 *
 * Implements regex primitives using Pika-style PEG matching.
 * This provides standard regex operations backed by a Pika-style engine.
 *
 * API:
 *   - re-match: Match first occurrence (anywhere in string)
 *   - re-find-all: Find all non-overlapping matches
 *   - re-split: Split by pattern
 *   - re-replace: Search and replace
 *   - re-fullmatch: Check if pattern matches entire string
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "../include/omni.h"
#include "internal_types.h"
#include "regex_compile.h"

/*
 * Helper: Extract string value from Obj
 * Returns NULL if not a string/symbol
 */
static const char* obj_to_cstr(Obj* obj) {
    if (!obj) return NULL;
    if (IS_IMMEDIATE(obj)) return NULL;
    if (IS_BOXED(obj)) {
        if (obj->tag == TAG_STRING || obj->tag == TAG_SYM) {
            return (const char*)obj->ptr;
        }
    }
    return NULL;
}

/*
 * Helper: Create string Obj from C string
 */
static Obj* cstr_to_obj(const char* s) {
    if (!s) return NULL;
    omni_ensure_global_region();
    return mk_sym_region(omni_get_global_region(), s);
}

/*
 * re-match: Match first occurrence of pattern in input (searches anywhere)
 * Args:
 *   - pattern_obj: Regex pattern string
 *   - input_obj: Input string to search
 * Returns: First matched substring as string, or NULL if no match
 */
// TESTED
Obj* prim_re_match(Obj* pattern_obj, Obj* input_obj) {
    const char* pattern = obj_to_cstr(pattern_obj);
    const char* input = obj_to_cstr(input_obj);

    if (!pattern || !input) return NULL;

    /* Compile pattern */
    PikaRegexCompiled* compiled = pika_regex_compile(pattern);
    if (!compiled) return NULL;

    const char* error = pika_regex_error(compiled);
    if (error) {
        fprintf(stderr, "re-match: %s\n", error);
        pika_regex_free(compiled);
        return NULL;
    }

    /* Search for match anywhere in input */
    Obj* result = pika_regex_search(compiled, input);

    pika_regex_free(compiled);
    return result;
}

/*
 * re-find-all: Find all non-overlapping matches
 * Args:
 *   - pattern_obj: Regex pattern string
 *   - input_obj: Input string to search
 * Returns: List of matched substrings (as strings)
 */
// TESTED
Obj* prim_re_find_all(Obj* pattern_obj, Obj* input_obj) {
    const char* pattern = obj_to_cstr(pattern_obj);
    const char* input = obj_to_cstr(input_obj);

    if (!pattern || !input) return NULL;

    PikaRegexCompiled* compiled = pika_regex_compile(pattern);
    if (!compiled) return NULL;

    const char* error = pika_regex_error(compiled);
    if (error) {
        fprintf(stderr, "re-find-all: %s\n", error);
        pika_regex_free(compiled);
        return NULL;
    }

    Obj* result = pika_regex_find_all(compiled, input);

    pika_regex_free(compiled);
    return result;
}

/*
 * re-split: Split string by pattern
 * Args:
 *   - pattern_obj: Regex pattern string (delimiter)
 *   - input_obj: Input string to split
 * Returns: List of substrings
 */
// TESTED
Obj* prim_re_split(Obj* pattern_obj, Obj* input_obj) {
    const char* pattern = obj_to_cstr(pattern_obj);
    const char* input = obj_to_cstr(input_obj);

    if (!pattern || !input) return NULL;

    /* Empty pattern returns list with input string */
    if (strlen(pattern) == 0) {
        return mk_pair(cstr_to_obj(input), NULL);
    }

    PikaRegexCompiled* compiled = pika_regex_compile(pattern);
    if (!compiled) return NULL;

    const char* error = pika_regex_error(compiled);
    if (error) {
        fprintf(stderr, "re-split: %s\n", error);
        pika_regex_free(compiled);
        return NULL;
    }

    Obj* result = pika_regex_split(compiled, input);

    pika_regex_free(compiled);
    return result;
}

/*
 * re-replace: Replace all occurrences of pattern with replacement
 * Args:
 *   - pattern_obj: Regex pattern string
 *   - replacement_obj: Replacement string
 *   - input_obj: Input string
 *   - global_obj: If true (not NULL/not false), replace all occurrences
 * Returns: Modified string
 */
// TESTED
Obj* prim_re_replace(Obj* pattern_obj, Obj* replacement_obj, Obj* input_obj, Obj* global_obj) {
    const char* pattern = obj_to_cstr(pattern_obj);
    const char* replacement = obj_to_cstr(replacement_obj);
    const char* input = obj_to_cstr(input_obj);

    if (!pattern || !input) return NULL;
    if (!replacement) replacement = "";

    int global = obj_to_bool(global_obj);

    PikaRegexCompiled* compiled = pika_regex_compile(pattern);
    if (!compiled) return cstr_to_obj(input);

    const char* error = pika_regex_error(compiled);
    if (error) {
        fprintf(stderr, "re-replace: %s\n", error);
        pika_regex_free(compiled);
        return cstr_to_obj(input);  /* Return original on error */
    }

    Obj* result = pika_regex_replace(compiled, replacement, input, global);

    pika_regex_free(compiled);
    return result;
}

/*
 * re-fullmatch: Check if pattern matches entire string
 * Args:
 *   - pattern_obj: Regex pattern string
 *   - input_obj: Input string
 * Returns: Boolean - true if entire string matches, false otherwise
 */
// TESTED
Obj* prim_re_fullmatch(Obj* pattern_obj, Obj* input_obj) {
    const char* pattern = obj_to_cstr(pattern_obj);
    const char* input = obj_to_cstr(input_obj);

    if (!pattern || !input) return mk_bool(0);

    PikaRegexCompiled* compiled = pika_regex_compile(pattern);
    if (!compiled) return mk_bool(0);

    const char* error = pika_regex_error(compiled);
    if (error) {
        pika_regex_free(compiled);
        return mk_bool(0);
    }

    Obj* result = pika_regex_fullmatch(compiled, input);

    pika_regex_free(compiled);
    return result;
}
