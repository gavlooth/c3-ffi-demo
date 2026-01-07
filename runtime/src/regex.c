/*
 * regex.c - High-level Regex API for OmniLisp
 *
 * Implements regex primitives using POSIX regex functions.
 * This is a temporary implementation until full Pika integration is available.
 *
 * API:
 *   - re-match: Match first occurrence
 *   - re-find-all: Find all non-overlapping matches
 *   - re-split: Split by pattern
 *   - re-replace: Search and replace
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <stdbool.h>
#include "../include/omni.h"
#include "internal_types.h"

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
    return mk_sym_region(omni_get_global_region(), s);  /* Using mk_sym_region for strings */
}

/*
 * re-match: Match first occurrence of pattern in input
 * Args:
 *   - pattern_obj: Regex pattern string
 *   - input_obj: Input string to search
 * Returns: First matched substring as string, or NULL if no match
 */
Obj* prim_re_match(Obj* pattern_obj, Obj* input_obj) {
    const char* pattern = obj_to_cstr(pattern_obj);
    const char* input = obj_to_cstr(input_obj);

    if (!pattern || !input) return NULL;

    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[128];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        fprintf(stderr, "re-match: Invalid regex '%s': %s\n", pattern, errbuf);
        return NULL;
    }

    regmatch_t match;
    ret = regexec(&regex, input, 1, &match, 0);

    Obj* result = NULL;
    if (ret == 0 && match.rm_so != -1) {
        size_t match_len = match.rm_eo - match.rm_so;
        char* matched_str = malloc(match_len + 1);
        strncpy(matched_str, input + match.rm_so, match_len);
        matched_str[match_len] = '\0';
        result = cstr_to_obj(matched_str);
        free(matched_str);
    }

    regfree(&regex);
    return result;
}

/*
 * re-find-all: Find all non-overlapping matches
 * Args:
 *   - pattern_obj: Regex pattern string
 *   - input_obj: Input string to search
 * Returns: List of matched substrings (as strings)
 */
Obj* prim_re_find_all(Obj* pattern_obj, Obj* input_obj) {
    const char* pattern = obj_to_cstr(pattern_obj);
    const char* input = obj_to_cstr(input_obj);

    if (!pattern || !input) return NULL;

    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[128];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        fprintf(stderr, "re-find-all: Invalid regex '%s': %s\n", pattern, errbuf);
        return NULL;
    }

    Obj* result_list = NULL;
    Obj* list_tail = NULL;
    const char* p = input;
    regmatch_t match;

    while (*p && regexec(&regex, p, 1, &match, 0) == 0) {
        if (match.rm_so == -1) break;

        size_t match_len = match.rm_eo - match.rm_so;
        char* matched_str = malloc(match_len + 1);
        strncpy(matched_str, p + match.rm_so, match_len);
        matched_str[match_len] = '\0';

        Obj* matched_obj = cstr_to_obj(matched_str);
        Obj* new_pair = mk_pair(matched_obj, NULL);

        if (!result_list) {
            result_list = new_pair;
        } else {
            list_tail->b = new_pair;
        }
        list_tail = new_pair;

        free(matched_str);

        /* Move past this match */
        p += match.rm_eo;

        /* Avoid infinite loop on zero-length matches */
        if (match.rm_so == match.rm_eo) {
            p++;
        }
    }

    regfree(&regex);
    return result_list;
}

/*
 * re-split: Split string by pattern
 * Args:
 *   - pattern_obj: Regex pattern string (delimiter)
 *   - input_obj: Input string to split
 * Returns: List of substrings
 */
Obj* prim_re_split(Obj* pattern_obj, Obj* input_obj) {
    const char* pattern = obj_to_cstr(pattern_obj);
    const char* input = obj_to_cstr(input_obj);

    if (!pattern || !input) return NULL;

    /* Empty pattern returns list with input string */
    if (strlen(pattern) == 0) {
        return mk_pair(cstr_to_obj(input), NULL);
    }

    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[128];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        fprintf(stderr, "re-split: Invalid regex '%s': %s\n", pattern, errbuf);
        return NULL;
    }

    Obj* result_list = NULL;
    Obj* list_tail = NULL;
    const char* p = input;
    regmatch_t match;

    while (*p) {
        ret = regexec(&regex, p, 1, &match, 0);

        if (ret == REG_NOMATCH || match.rm_so == -1) {
            /* No more matches, add rest of string */
            Obj* segment_obj = cstr_to_obj(p);
            Obj* new_pair = mk_pair(segment_obj, NULL);
            if (!result_list) {
                result_list = new_pair;
            } else {
                list_tail->b = new_pair;
            }
            list_tail = new_pair;
            break;
        }

        /* Add segment before match */
        if (match.rm_so > 0) {
            char* segment = malloc(match.rm_so + 1);
            strncpy(segment, p, match.rm_so);
            segment[match.rm_so] = '\0';

            Obj* segment_obj = cstr_to_obj(segment);
            Obj* new_pair = mk_pair(segment_obj, NULL);
            if (!result_list) {
                result_list = new_pair;
            } else {
                list_tail->b = new_pair;
            }
            list_tail = new_pair;
            free(segment);
        }

        /* Move past the delimiter */
        p += match.rm_eo;

        /* Avoid infinite loop on zero-length matches */
        if (match.rm_so == match.rm_eo) {
            p++;
        }
    }

    regfree(&regex);
    return result_list;
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
Obj* prim_re_replace(Obj* pattern_obj, Obj* replacement_obj, Obj* input_obj, Obj* global_obj) {
    const char* pattern = obj_to_cstr(pattern_obj);
    const char* replacement = obj_to_cstr(replacement_obj);
    const char* input = obj_to_cstr(input_obj);

    if (!pattern || !input) return NULL;
    if (!replacement) replacement = "";

    int global = obj_to_bool(global_obj);

    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[128];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        fprintf(stderr, "re-replace: Invalid regex '%s': %s\n", pattern, errbuf);
        return cstr_to_obj(input);  /* Return original on error */
    }

    /* Allocate result buffer (worst case: 2x input size) */
    size_t result_size = strlen(input) * 2 + 1;
    char* result = malloc(result_size);
    result[0] = '\0';

    const char* p = input;
    regmatch_t match;
    int first_only = !global;

    while (*p) {
        ret = regexec(&regex, p, 1, &match, 0);

        if (ret == REG_NOMATCH || match.rm_so == -1) {
            /* No more matches, append rest */
            strncat(result, p, result_size - strlen(result) - 1);
            break;
        }

        /* Append text before match */
        strncat(result, p, match.rm_so);

        /* Append replacement */
        strcat(result, replacement);

        /* Move past the match */
        p += match.rm_eo;

        /* Stop after first match if not global */
        if (first_only) {
            strncat(result, p, result_size - strlen(result) - 1);
            break;
        }

        /* Avoid infinite loop on zero-length matches */
        if (match.rm_so == match.rm_eo) {
            strncat(result, p, 1);
            p++;
        }
    }

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    regfree(&regex);
    return result_obj;
}

/*
 * Helper: Check if pattern matches entire string (anchor at start and end)
 * Similar to re-match but requires full string match
 */
Obj* prim_re_fullmatch(Obj* pattern_obj, Obj* input_obj) {
    const char* pattern = obj_to_cstr(pattern_obj);
    const char* input = obj_to_cstr(input_obj);

    if (!pattern || !input) return mk_bool(0);

    regex_t regex;
    int ret = regcomp(&regex, pattern, REG_EXTENDED);
    if (ret != 0) {
        return mk_bool(0);
    }

    regmatch_t match;
    ret = regexec(&regex, input, 1, &match, 0);

    int full_match = (ret == 0 && match.rm_so == 0 && match.rm_eo == (regoff_t)strlen(input));

    regfree(&regex);
    return mk_bool(full_match);
}
