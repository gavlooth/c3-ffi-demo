/*
 * string_utils.c - String manipulation utilities for OmniLisp
 *
 * Core string operations:
 *   - string-split: Split string by delimiter
 *   - string-join: Join list of strings with delimiter
 *   - string-replace: Replace substring
 *   - string-trim: Remove whitespace from ends
 *   - string-upcase: Convert to uppercase
 *   - string-lowcase: Convert to lowercase
 *   - string-length: Get string length
 *   - string-concat: Concatenate strings
 *   - string-substr: Get substring
 *   - string-contains: Check if string contains substring
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "../include/omni.h"
#include "internal_types.h"

/*
 * Helper: Extract string value from Obj
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
 * Helper: Duplicate string
 */
static char* str_dup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* result = malloc(len + 1);
    strcpy(result, s);
    return result;
}

/* ============================================================================
 * String Operations
 * ============================================================================*/

/*
 * string-length: Get length of string
 * Args:
 *   - str_obj: String object
 * Returns: Integer length
 */
Obj* prim_string_length(Obj* str_obj) {
    const char* s = obj_to_cstr(str_obj);
    if (!s) return mk_int(0);
    return mk_int((long)strlen(s));
}

/*
 * string-split: Split string by delimiter
 * Args:
 *   - delim_obj: Delimiter string
 *   - str_obj: String to split
 * Returns: List of substrings
 */
Obj* prim_string_split(Obj* delim_obj, Obj* str_obj) {
    const char* delim = obj_to_cstr(delim_obj);
    const char* str = obj_to_cstr(str_obj);

    if (!str) return NULL;
    if (!delim || strlen(delim) == 0) {
        /* Empty delimiter: return list with single string */
        return mk_pair(cstr_to_obj(str), NULL);
    }

    Obj* result_list = NULL;
    Obj* list_tail = NULL;
    char* str_copy = str_dup(str);
    char* token = strtok(str_copy, delim);

    while (token != NULL) {
        Obj* token_obj = cstr_to_obj(token);
        Obj* new_pair = mk_pair(token_obj, NULL);

        if (!result_list) {
            result_list = new_pair;
        } else {
            list_tail->b = new_pair;
        }
        list_tail = new_pair;

        token = strtok(NULL, delim);
    }

    free(str_copy);
    return result_list;
}

/*
 * string-join: Join list of strings with delimiter
 * Args:
 *   - delim_obj: Delimiter string
 *   - list_obj: List of strings
 * Returns: Joined string
 */
Obj* prim_string_join(Obj* delim_obj, Obj* list_obj) {
    const char* delim = obj_to_cstr(delim_obj);
    if (!delim) delim = "";

    /* Calculate total length */
    size_t total_len = 0;
    int count = 0;
    Obj* p = list_obj;
    while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
        const char* s = obj_to_cstr(p->a);
        if (s) {
            total_len += strlen(s);
            if (count > 0) total_len += strlen(delim);
            count++;
        }
        p = p->b;
    }

    if (count == 0) return cstr_to_obj("");

    /* Build joined string */
    char* result = malloc(total_len + 1);
    result[0] = '\0';

    p = list_obj;
    int first = 1;
    while (p && IS_BOXED(p) && p->tag == TAG_PAIR) {
        const char* s = obj_to_cstr(p->a);
        if (s) {
            if (!first) strcat(result, delim);
            strcat(result, s);
            first = 0;
        }
        p = p->b;
    }

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

/*
 * string-replace: Replace substring with another
 * Args:
 *   - old_obj: Substring to replace
 *   - new_obj: Replacement substring
 *   - str_obj: Input string
 * Returns: Modified string
 */
Obj* prim_string_replace(Obj* old_obj, Obj* new_obj, Obj* str_obj) {
    const char* old = obj_to_cstr(old_obj);
    const char* new = obj_to_cstr(new_obj);
    const char* str = obj_to_cstr(str_obj);

    if (!str) return NULL;
    if (!old || strlen(old) == 0) return cstr_to_obj(str);
    if (!new) new = "";

    /* Count occurrences and calculate new size */
    int count = 0;
    size_t old_len = strlen(old);
    size_t new_len = strlen(new);
    const char* p = str;

    while ((p = strstr(p, old)) != NULL) {
        count++;
        p += old_len;
    }

    if (count == 0) return cstr_to_obj(str);

    size_t result_len = strlen(str) + count * (new_len - old_len);
    char* result = malloc(result_len + 1);
    result[0] = '\0';

    /* Build result string */
    p = str;
    const char* last_pos = str;

    while ((p = strstr(p, old)) != NULL) {
        strncat(result, last_pos, p - last_pos);
        strcat(result, new);
        p += old_len;
        last_pos = p;
    }

    strcat(result, last_pos);

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

/*
 * string-trim: Remove whitespace from both ends
 * Args:
 *   - str_obj: Input string
 * Returns: Trimmed string
 */
Obj* prim_string_trim(Obj* str_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    /* Skip leading whitespace */
    while (*str && isspace((unsigned char)*str)) str++;

    /* Find end */
    const char* end = str + strlen(str);
    while (end > str && isspace((unsigned char)*(end - 1))) end--;

    /* Create trimmed string */
    size_t len = end - str;
    char* trimmed = malloc(len + 1);
    strncpy(trimmed, str, len);
    trimmed[len] = '\0';

    Obj* result = cstr_to_obj(trimmed);
    free(trimmed);
    return result;
}

/*
 * string-trim-left: Remove leading whitespace
 */
Obj* prim_string_trim_left(Obj* str_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    while (*str && isspace((unsigned char)*str)) str++;

    Obj* result = cstr_to_obj(str);
    return result;
}

/*
 * string-trim-right: Remove trailing whitespace
 */
Obj* prim_string_trim_right(Obj* str_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    const char* end = str + strlen(str);
    while (end > str && isspace((unsigned char)*(end - 1))) end--;

    size_t len = end - str;
    char* trimmed = malloc(len + 1);
    strncpy(trimmed, str, len);
    trimmed[len] = '\0';

    Obj* result = cstr_to_obj(trimmed);
    free(trimmed);
    return result;
}

/*
 * string-upcase: Convert to uppercase
 * Args:
 *   - str_obj: Input string
 * Returns: Uppercase string
 */
Obj* prim_string_upcase(Obj* str_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    size_t len = strlen(str);
    char* result = malloc(len + 1);

    for (size_t i = 0; i < len; i++) {
        result[i] = (char)toupper((unsigned char)str[i]);
    }
    result[len] = '\0';

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

/*
 * string-lowcase: Convert to lowercase
 * Args:
 *   - str_obj: Input string
 * Returns: Lowercase string
 */
Obj* prim_string_lowcase(Obj* str_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    size_t len = strlen(str);
    char* result = malloc(len + 1);

    for (size_t i = 0; i < len; i++) {
        result[i] = (char)tolower((unsigned char)str[i]);
    }
    result[len] = '\0';

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

/*
 * string-concat: Concatenate strings
 * Args:
 *   - str1_obj: First string
 *   - str2_obj: Second string
 * Returns: Concatenated string
 */
Obj* prim_string_concat(Obj* str1_obj, Obj* str2_obj) {
    const char* str1 = obj_to_cstr(str1_obj);
    const char* str2 = obj_to_cstr(str2_obj);

    if (!str1) str1 = "";
    if (!str2) str2 = "";

    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);
    char* result = malloc(len1 + len2 + 1);

    strcpy(result, str1);
    strcat(result, str2);

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

/*
 * string-substr: Get substring
 * Args:
 *   - str_obj: Input string
 *   - start_obj: Start index (0-based)
 *   - length_obj: Length (or -1 for rest of string)
 * Returns: Substring
 */
Obj* prim_string_substr(Obj* str_obj, Obj* start_obj, Obj* length_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    long start = obj_to_int(start_obj);
    long length = obj_to_int(length_obj);

    size_t str_len = strlen(str);

    /* Handle negative start (count from end) */
    if (start < 0) start = str_len + start;
    if (start < 0) start = 0;
    if ((size_t)start > str_len) return cstr_to_obj("");

    /* Handle negative length (rest of string) */
    if (length < 0) length = str_len - start;

    size_t end = start + length;
    if (end > str_len) end = str_len;

    size_t substr_len = end - start;
    char* result = malloc(substr_len + 1);
    strncpy(result, str + start, substr_len);
    result[substr_len] = '\0';

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

/*
 * string-contains: Check if string contains substring
 * Args:
 *   - str_obj: Input string
 *   - substr_obj: Substring to search for
 * Returns: Boolean
 */
Obj* prim_string_contains(Obj* str_obj, Obj* substr_obj) {
    const char* str = obj_to_cstr(str_obj);
    const char* substr = obj_to_cstr(substr_obj);

    if (!str || !substr) return mk_bool(0);

    int found = (strstr(str, substr) != NULL);
    return mk_bool(found);
}

/*
 * string-index-of: Find first index of substring
 * Args:
 *   - str_obj: Input string
 *   - substr_obj: Substring to search for
 * Returns: Index (or -1 if not found)
 */
Obj* prim_string_index_of(Obj* str_obj, Obj* substr_obj) {
    const char* str = obj_to_cstr(str_obj);
    const char* substr = obj_to_cstr(substr_obj);

    if (!str || !substr) return mk_int(-1);

    const char* found = strstr(str, substr);
    if (found) {
        return mk_int((long)(found - str));
    }
    return mk_int(-1);
}

/*
 * string-equals: Compare two strings for equality
 * Args:
 *   - str1_obj: First string
 *   - str2_obj: Second string
 * Returns: Boolean
 */
Obj* prim_string_equals(Obj* str1_obj, Obj* str2_obj) {
    const char* str1 = obj_to_cstr(str1_obj);
    const char* str2 = obj_to_cstr(str2_obj);

    if (!str1 && !str2) return mk_bool(1);
    if (!str1 || !str2) return mk_bool(0);

    int equal = (strcmp(str1, str2) == 0);
    return mk_bool(equal);
}

/*
 * string-compare: Compare two strings
 * Args:
 *   - str1_obj: First string
 *   - str2_obj: Second string
 * Returns: Integer (<0 if str1<str2, 0 if equal, >0 if str1>str2)
 */
Obj* prim_string_compare(Obj* str1_obj, Obj* str2_obj) {
    const char* str1 = obj_to_cstr(str1_obj);
    const char* str2 = obj_to_cstr(str2_obj);

    if (!str1) str1 = "";
    if (!str2) str2 = "";

    int result = strcmp(str1, str2);
    return mk_int(result);
}
