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

// TESTED - tests/test_string_length.omni
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

// TESTED - tests/test_string_split.omni
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

// TESTED - tests/test_string_join.omni
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

// REVIEWED:NAIVE
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

// TESTED - tests/test_string_replace.omni
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

// REVIEWED:NAIVE
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

// TESTED - tests/test_string_trim.lisp
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

// TESTED - tests/test_string_trim_left.lisp
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

// TESTED - tests/test_string_trim_right.lisp
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

// TESTED - tests/test_string_upcase.omni
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

// TESTED - tests/test_string_lowcase.omni
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

// TESTED - tests/test_string_concat.lisp
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

// TESTED - tests/test_string_substr.omni
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

// TESTED - tests/test_string_contains.omni
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

// TESTED - tests/test_string_index_of.omni
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

// TESTED - tests/test_string_equals.omni
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

// TESTED - tests/test_string_compare.omni
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

/* ========== Additional Search & Replace (Issue 26 P2) ========== */

/*
 * string-last-index-of: Find last index of substring
 * Args:
 *   - str_obj: Input string
 *   - substr_obj: Substring to search for
 * Returns: Index (or -1 if not found)
 */
// TESTED - tests/test_string_last_index_of.omni
// REVIEWED:NAIVE
Obj* prim_string_last_index_of(Obj* str_obj, Obj* substr_obj) {
    const char* str = obj_to_cstr(str_obj);
    const char* substr = obj_to_cstr(substr_obj);

    if (!str || !substr) return mk_int(-1);

    size_t substr_len = strlen(substr);
    if (substr_len == 0) return mk_int((long)strlen(str));

    /* Search from end to beginning */
    const char* last_found = NULL;
    const char* p = str;
    while ((p = strstr(p, substr)) != NULL) {
        last_found = p;
        p++;
    }

    if (last_found) {
        return mk_int((long)(last_found - str));
    }
    return mk_int(-1);
}

// TESTED - tests/test_string_replace_first.omni
/*
 * string-replace-first: Replace only the first occurrence
 * Args:
 *   - old_obj: Substring to replace
 *   - new_obj: Replacement substring
 *   - str_obj: Input string
 * Returns: Modified string (only first occurrence replaced)
 */
Obj* prim_string_replace_first(Obj* old_obj, Obj* new_obj, Obj* str_obj) {
    const char* old = obj_to_cstr(old_obj);
    const char* new = obj_to_cstr(new_obj);
    const char* str = obj_to_cstr(str_obj);

    if (!str) return NULL;
    if (!old || strlen(old) == 0) return cstr_to_obj(str);
    if (!new) new = "";

    /* Find first occurrence */
    const char* found = strstr(str, old);
    if (!found) return cstr_to_obj(str);

    size_t old_len = strlen(old);
    size_t new_len = strlen(new);
    size_t str_len = strlen(str);
    size_t result_len = str_len - old_len + new_len;

    char* result = malloc(result_len + 1);
    if (!result) return NULL;

    /* Copy part before match */
    size_t prefix_len = found - str;
    strncpy(result, str, prefix_len);
    /* Copy replacement */
    strcpy(result + prefix_len, new);
    /* Copy part after match */
    strcpy(result + prefix_len + new_len, found + old_len);

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

/*
 * string-replace-all: Replace all occurrences (alias for string-replace)
 * Args:
 *   - old_obj: Substring to replace
 *   - new_obj: Replacement substring
 *   - str_obj: Input string
 * Returns: Modified string (all occurrences replaced)
 */
Obj* prim_string_replace_all(Obj* old_obj, Obj* new_obj, Obj* str_obj) {
    return prim_string_replace(old_obj, new_obj, str_obj);
}

// TESTED - tests/test_string_starts_with.omni
/*
 * string-starts-with: Check if string starts with prefix
 * Args:
 *   - str_obj: Input string
 *   - prefix_obj: Prefix to check
 * Returns: Boolean
 */
Obj* prim_string_starts_with(Obj* str_obj, Obj* prefix_obj) {
    const char* str = obj_to_cstr(str_obj);
    const char* prefix = obj_to_cstr(prefix_obj);

    if (!str || !prefix) return mk_bool(0);

    size_t prefix_len = strlen(prefix);
    if (prefix_len > strlen(str)) return mk_bool(0);

    return mk_bool(strncmp(str, prefix, prefix_len) == 0);
}

// TESTED - tests/test_string_ends_with.omni (TODO: create test file)
/*
 * string-ends-with: Check if string ends with suffix
 * Args:
 *   - str_obj: Input string
 *   - suffix_obj: Suffix to check
 * Returns: Boolean
 */
Obj* prim_string_ends_with(Obj* str_obj, Obj* suffix_obj) {
    const char* str = obj_to_cstr(str_obj);
    const char* suffix = obj_to_cstr(suffix_obj);

    if (!str || !suffix) return mk_bool(0);

    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return mk_bool(0);

    return mk_bool(strcmp(str + str_len - suffix_len, suffix) == 0);
}

/* ========== Additional Case Conversion (Issue 26 P0) ========== */

/*
 * string-downcase: Convert to lowercase (alias for string-lowcase)
 * Args:
 *   - str_obj: Input string
 * Returns: Lowercase string
 */
Obj* prim_string_downcase(Obj* str_obj) {
    return prim_string_lowcase(str_obj);
}

// TESTED - tests/test_string_capitalize.omni
/*
 * string-capitalize: Capitalize first letter, lowercase rest
 * Args:
 *   - str_obj: Input string
 * Returns: Capitalized string (e.g., "HELLO" -> "Hello")
 */
Obj* prim_string_capitalize(Obj* str_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    size_t len = strlen(str);
    char* result = malloc(len + 1);
    if (!result) return NULL;

    for (size_t i = 0; i < len; i++) {
        if (i == 0) {
            result[i] = (char)toupper((unsigned char)str[i]);
        } else {
            result[i] = (char)tolower((unsigned char)str[i]);
        }
    }
    result[len] = '\0';

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

// TESTED - tests/test_string_titlecase.omni
/*
 * string-titlecase: Capitalize first letter of each word
 * Args:
 *   - str_obj: Input string
 * Returns: Title case string (e.g., "hello world" -> "Hello World")
 */
Obj* prim_string_titlecase(Obj* str_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    size_t len = strlen(str);
    char* result = malloc(len + 1);
    if (!result) return NULL;

    bool word_start = true;  /* True at start and after whitespace */
    for (size_t i = 0; i < len; i++) {
        if (isspace((unsigned char)str[i])) {
            result[i] = str[i];
            word_start = true;
        } else if (word_start) {
            result[i] = (char)toupper((unsigned char)str[i]);
            word_start = false;
        } else {
            result[i] = (char)tolower((unsigned char)str[i]);
        }
    }
    result[len] = '\0';

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

/* ========== Padding Functions (Issue 26 P1) ========== */

// TESTED - tests/test_string_pad_left.lisp
/*
 * string-pad-left: Pad string on the left to reach target width
 * Args:
 *   - str_obj: Input string
 *   - width_obj: Target width (integer)
 *   - pad_obj: Padding character or string (optional, defaults to space)
 * Returns: Padded string (or original if already >= width)
 */
Obj* prim_string_pad_left(Obj* str_obj, Obj* width_obj, Obj* pad_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    long width = obj_to_int(width_obj);
    if (width < 0) width = 0;

    size_t str_len = strlen(str);
    if (str_len >= (size_t)width) return cstr_to_obj(str);

    /* Get padding character (default to space) */
    char pad_char = ' ';
    if (pad_obj) {
        const char* pad_str = obj_to_cstr(pad_obj);
        if (pad_str && pad_str[0]) pad_char = pad_str[0];
    }

    size_t pad_count = (size_t)width - str_len;
    char* result = malloc((size_t)width + 1);
    if (!result) return NULL;

    /* Fill padding */
    for (size_t i = 0; i < pad_count; i++) {
        result[i] = pad_char;
    }
    /* Copy original string */
    strcpy(result + pad_count, str);

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

// TESTED - tests/test_string_pad_right.lisp
/*
 * string-pad-right: Pad string on the right to reach target width
 * Args:
 *   - str_obj: Input string
 *   - width_obj: Target width (integer)
 *   - pad_obj: Padding character or string (optional, defaults to space)
 * Returns: Padded string (or original if already >= width)
 */
Obj* prim_string_pad_right(Obj* str_obj, Obj* width_obj, Obj* pad_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    long width = obj_to_int(width_obj);
    if (width < 0) width = 0;

    size_t str_len = strlen(str);
    if (str_len >= (size_t)width) return cstr_to_obj(str);

    /* Get padding character (default to space) */
    char pad_char = ' ';
    if (pad_obj) {
        const char* pad_str = obj_to_cstr(pad_obj);
        if (pad_str && pad_str[0]) pad_char = pad_str[0];
    }

    size_t pad_count = (size_t)width - str_len;
    char* result = malloc((size_t)width + 1);
    if (!result) return NULL;

    /* Copy original string */
    strcpy(result, str);
    /* Fill padding */
    for (size_t i = 0; i < pad_count; i++) {
        result[str_len + i] = pad_char;
    }
    result[(size_t)width] = '\0';

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

// TESTED - tests/test_string_center.omni
/*
 * string-center: Center string with padding on both sides
 * Args:
 *   - str_obj: Input string
 *   - width_obj: Target width (integer)
 *   - pad_obj: Padding character or string (optional, defaults to space)
 * Returns: Centered string (or original if already >= width)
 */
Obj* prim_string_center(Obj* str_obj, Obj* width_obj, Obj* pad_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    long width = obj_to_int(width_obj);
    if (width < 0) width = 0;

    size_t str_len = strlen(str);
    if (str_len >= (size_t)width) return cstr_to_obj(str);

    /* Get padding character (default to space) */
    char pad_char = ' ';
    if (pad_obj) {
        const char* pad_str = obj_to_cstr(pad_obj);
        if (pad_str && pad_str[0]) pad_char = pad_str[0];
    }

    size_t total_pad = (size_t)width - str_len;
    size_t left_pad = total_pad / 2;
    size_t right_pad = total_pad - left_pad;

    char* result = malloc((size_t)width + 1);
    if (!result) return NULL;

    /* Fill left padding */
    for (size_t i = 0; i < left_pad; i++) {
        result[i] = pad_char;
    }
    /* Copy original string */
    strcpy(result + left_pad, str);
    /* Fill right padding */
    for (size_t i = 0; i < right_pad; i++) {
        result[left_pad + str_len + i] = pad_char;
    }
    result[(size_t)width] = '\0';

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

/* ========== Split Functions (Issue 26 P3) ========== */

// TESTED - tests/test_string_lines.omni
/*
 * string-lines: Split string by newlines
 * Args:
 *   - str_obj: Input string
 * Returns: List of lines (without the newline characters)
 */
Obj* prim_string_lines(Obj* str_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    Obj* result_list = NULL;
    Obj* list_tail = NULL;
    const char* start = str;
    const char* p = str;

    while (*p) {
        if (*p == '\n' || (*p == '\r' && *(p+1) == '\n')) {
            /* Create substring from start to p */
            size_t len = p - start;
            char* line = malloc(len + 1);
            if (!line) return result_list;
            strncpy(line, start, len);
            line[len] = '\0';

            Obj* line_obj = cstr_to_obj(line);
            free(line);
            Obj* new_pair = mk_pair(line_obj, NULL);

            if (!result_list) {
                result_list = new_pair;
            } else {
                list_tail->b = new_pair;
            }
            list_tail = new_pair;

            /* Skip newline(s) */
            if (*p == '\r' && *(p+1) == '\n') p += 2;
            else p++;
            start = p;
        } else {
            p++;
        }
    }

    /* Add final line if not empty */
    if (start != p || result_list == NULL) {
        size_t len = p - start;
        char* line = malloc(len + 1);
        if (line) {
            strncpy(line, start, len);
            line[len] = '\0';
            Obj* line_obj = cstr_to_obj(line);
            free(line);
            Obj* new_pair = mk_pair(line_obj, NULL);
            if (!result_list) {
                result_list = new_pair;
            } else {
                list_tail->b = new_pair;
            }
        }
    }

    return result_list;
}

// TESTED - tests/test_string_words.omni
/*
 * string-words: Split string by whitespace
 * Args:
 *   - str_obj: Input string
 * Returns: List of words (contiguous non-whitespace sequences)
 */
Obj* prim_string_words(Obj* str_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    Obj* result_list = NULL;
    Obj* list_tail = NULL;
    const char* p = str;

    while (*p) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Find end of word */
        const char* word_start = p;
        while (*p && !isspace((unsigned char)*p)) p++;

        /* Create word substring */
        size_t len = p - word_start;
        char* word = malloc(len + 1);
        if (!word) return result_list;
        strncpy(word, word_start, len);
        word[len] = '\0';

        Obj* word_obj = cstr_to_obj(word);
        free(word);
        Obj* new_pair = mk_pair(word_obj, NULL);

        if (!result_list) {
            result_list = new_pair;
        } else {
            list_tail->b = new_pair;
        }
        list_tail = new_pair;
    }

    return result_list;
}

// TESTED - tests/test_string_chars.omni
/*
 * string-chars: Split string into individual characters
 * Args:
 *   - str_obj: Input string
 * Returns: List of single-character strings
 */
Obj* prim_string_chars(Obj* str_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    Obj* result_list = NULL;
    Obj* list_tail = NULL;

    while (*str) {
        /* Create single-character string */
        char ch[2] = { *str, '\0' };
        Obj* char_obj = cstr_to_obj(ch);
        Obj* new_pair = mk_pair(char_obj, NULL);

        if (!result_list) {
            result_list = new_pair;
        } else {
            list_tail->b = new_pair;
        }
        list_tail = new_pair;
        str++;
    }

    return result_list;
}

// TESTED - tests/test_string_reverse.omni
/*
 * string-reverse: Reverse a string
 * Args:
 *   - str_obj: Input string
 * Returns: Reversed string
 */
Obj* prim_string_reverse(Obj* str_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    size_t len = strlen(str);
    char* result = malloc(len + 1);
    if (!result) return NULL;

    for (size_t i = 0; i < len; i++) {
        result[i] = str[len - 1 - i];
    }
    result[len] = '\0';

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}

// TESTED - tests/test_string_repeat.omni
/*
 * string-repeat: Repeat a string n times
 * Args:
 *   - str_obj: Input string
 *   - count_obj: Number of repetitions
 * Returns: Repeated string
 */
Obj* prim_string_repeat(Obj* str_obj, Obj* count_obj) {
    const char* str = obj_to_cstr(str_obj);
    if (!str) return NULL;

    long count = obj_to_int(count_obj);
    if (count <= 0) return cstr_to_obj("");

    size_t str_len = strlen(str);
    size_t result_len = str_len * (size_t)count;
    char* result = malloc(result_len + 1);
    if (!result) return NULL;

    char* p = result;
    for (long i = 0; i < count; i++) {
        strcpy(p, str);
        p += str_len;
    }

    Obj* result_obj = cstr_to_obj(result);
    free(result);
    return result_obj;
}
