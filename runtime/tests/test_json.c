/*
 * test_json.c - Tests for JSON parsing and generation
 *
 * Verifies:
 * - JSON string parsing (prim_json_parse)
 * - JSON stringification (prim_json_stringify)
 * - JSON type mapping (object→dict, array→array, string→string, number→int/float, boolean→bool, null→nothing)
 * - Malformed JSON error handling
 * - Nested structure parsing and generation
 */

#include "../include/omni.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %s: ", name); } while (0)
#define PASS() do { printf("PASS\n"); } while (0)
#define FAIL(msg) do { tests_failed++; printf("FAIL - %s\n", msg); return; } while (0)

#define ASSERT(cond) do { if (!(cond)) { FAIL(#cond); } } while (0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) { FAIL("ASSERT_EQ failed"); } } while (0)
#define ASSERT_EQ_FLOAT(a, b, eps) do { if (fabs((a) - (b)) > (eps)) { FAIL("ASSERT_EQ_FLOAT failed"); } } while (0)
#define ASSERT_NOT_NULL(p) do { if ((p) == NULL) { FAIL("NULL"); } } while (0)
#define ASSERT_STR_EQ(a, b) do { if (strcmp((a), (b)) != 0) { char _msg[256]; snprintf(_msg, sizeof(_msg), "'%s' != '%s'", (a), (b)); FAIL(_msg); } } while (0)

/* ==================== JSON String Parsing Tests ==================== */

void test_json_parse_string_basic(void) {
    TEST("json-parse: basic string");
    Obj* input = mk_sym("\"hello world\"");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    /* Parse returns a string object */
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING || result->tag == TAG_SYM);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_string_empty(void) {
    TEST("json-parse: empty string");
    Obj* input = mk_sym("\"\"");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING || result->tag == TAG_SYM);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_string_escaped(void) {
    TEST("json-parse: string with escape sequences");
    Obj* input = mk_sym("\"hello\\nworld\\t!\"");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING || result->tag == TAG_SYM);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

/* ==================== JSON Number Parsing Tests ==================== */

void test_json_parse_number_int(void) {
    TEST("json-parse: integer");
    Obj* input = mk_sym("42");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(is_int(result));
    ASSERT_EQ(obj_to_int(result), 42);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_number_negative(void) {
    TEST("json-parse: negative integer");
    Obj* input = mk_sym("-123");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(is_int(result));
    ASSERT_EQ(obj_to_int(result), -123);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_number_float(void) {
    TEST("json-parse: float");
    Obj* input = mk_sym("3.14");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_FLOAT);
    ASSERT_EQ_FLOAT(obj_to_float(result), 3.14, 0.001);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_number_scientific(void) {
    TEST("json-parse: scientific notation");
    Obj* input = mk_sym("1.5e2");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_FLOAT);
    ASSERT_EQ_FLOAT(obj_to_float(result), 150.0, 0.001);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_number_zero(void) {
    TEST("json-parse: zero");
    Obj* input = mk_sym("0");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(is_int(result));
    ASSERT_EQ(obj_to_int(result), 0);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

/* ==================== JSON Boolean and Null Tests ==================== */

void test_json_parse_boolean_true(void) {
    TEST("json-parse: boolean true");
    Obj* input = mk_sym("true");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(obj_to_bool(result) == 1);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_boolean_false(void) {
    TEST("json-parse: boolean false");
    Obj* input = mk_sym("false");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(obj_to_bool(result) == 0);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_null(void) {
    TEST("json-parse: null");
    Obj* input = mk_sym("null");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(is_nothing(result));
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

/* ==================== JSON Array Parsing Tests ==================== */

void test_json_parse_array_empty(void) {
    TEST("json-parse: empty array");
    Obj* input = mk_sym("[]");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_ARRAY);
    ASSERT_EQ(array_length(result), 0);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_array_numbers(void) {
    TEST("json-parse: array of numbers");
    Obj* input = mk_sym("[1, 2, 3, 4, 5]");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_ARRAY);
    ASSERT_EQ(array_length(result), 5);
    
    /* Verify first element */
    Obj* elem = array_get(result, 0);
    ASSERT(is_int(elem));
    ASSERT_EQ(obj_to_int(elem), 1);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_array_mixed(void) {
    TEST("json-parse: array of mixed types");
    Obj* input = mk_sym("[1, \"hello\", null, true]");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_ARRAY);
    ASSERT_EQ(array_length(result), 4);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_array_nested(void) {
    TEST("json-parse: nested array");
    Obj* input = mk_sym("[1, [2, 3], 4]");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_ARRAY);
    ASSERT_EQ(array_length(result), 3);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

/* ==================== JSON Object Parsing Tests ==================== */

void test_json_parse_object_empty(void) {
    TEST("json-parse: empty object");
    Obj* input = mk_sym("{}");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_DICT);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_object_simple(void) {
    TEST("json-parse: simple object");
    Obj* input = mk_sym("{\"name\": \"Alice\", \"age\": 30}");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_DICT);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_object_nested(void) {
    TEST("json-parse: nested object");
    Obj* input = mk_sym("{\"person\": {\"name\": \"Bob\", \"age\": 25}}");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_DICT);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_object_with_array(void) {
    TEST("json-parse: object with array value");
    Obj* input = mk_sym("{\"items\": [1, 2, 3]}");
    Obj* result = prim_json_parse(input);
    
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_DICT);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

/* ==================== JSON Error Handling Tests ==================== */

void test_json_parse_error_missing_brace(void) {
    TEST("json-parse: missing closing brace");
    Obj* input = mk_sym("{\"name\": \"test\"");
    Obj* result = prim_json_parse(input);
    
    /* Should return an error object */
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_ERROR);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_error_unclosed_string(void) {
    TEST("json-parse: unclosed string");
    Obj* input = mk_sym("\"hello");
    Obj* result = prim_json_parse(input);
    
    /* Should return an error object */
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_ERROR);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_error_trailing_comma(void) {
    TEST("json-parse: trailing comma in array");
    Obj* input = mk_sym("[1, 2,]");
    Obj* result = prim_json_parse(input);
    
    /* Should return an error object */
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_ERROR);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_error_invalid_number(void) {
    TEST("json-parse: invalid number");
    Obj* input = mk_sym("12.34.56");
    Obj* result = prim_json_parse(input);
    
    /* Should return an error object */
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_ERROR);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_error_trailing_content(void) {
    TEST("json-parse: trailing content after value");
    Obj* input = mk_sym("42 extra");
    Obj* result = prim_json_parse(input);
    
    /* Should return an error object (trailing content check) */
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_ERROR);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_parse_error_empty_input(void) {
    TEST("json-parse: empty input");
    Obj* input = mk_sym("");
    Obj* result = prim_json_parse(input);
    
    /* Should return an error object */
    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_ERROR);
    
    dec_ref(input);
    dec_ref(result);
    PASS();
}

/* ==================== JSON Stringification Tests ==================== */

void test_json_stringify_string(void) {
    TEST("json-stringify: string value");
    Obj* input = mk_string("hello world");
    Obj* result = prim_json_stringify(input);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING);
    ASSERT_STR_EQ((char*)result->ptr, "\"hello world\"");

    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_stringify_int(void) {
    TEST("json-stringify: integer value");
    Obj* input = mk_int(42);
    Obj* result = prim_json_stringify(input);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING);
    ASSERT_STR_EQ((char*)result->ptr, "42");

    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_stringify_float(void) {
    TEST("json-stringify: float value");
    Obj* input = mk_float(3.14);
    Obj* result = prim_json_stringify(input);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING);
    ASSERT_STR_EQ((char*)result->ptr, "3.14");

    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_stringify_bool_true(void) {
    TEST("json-stringify: boolean true");
    Obj* input = mk_bool(1);
    Obj* result = prim_json_stringify(input);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING);
    ASSERT_STR_EQ((char*)result->ptr, "true");

    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_stringify_bool_false(void) {
    TEST("json-stringify: boolean false");
    Obj* input = mk_bool(0);
    Obj* result = prim_json_stringify(input);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING);
    ASSERT_STR_EQ((char*)result->ptr, "false");

    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_stringify_null(void) {
    TEST("json-stringify: null/nothing");
    Obj* input = mk_nothing();
    Obj* result = prim_json_stringify(input);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING);
    ASSERT_STR_EQ((char*)result->ptr, "null");

    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_stringify_array(void) {
    TEST("json-stringify: array");
    Obj* input = mk_array(3);
    array_push(input, mk_int(1));
    array_push(input, mk_int(2));
    array_push(input, mk_int(3));

    Obj* result = prim_json_stringify(input);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING);
    ASSERT_STR_EQ((char*)result->ptr, "[1,2,3]");

    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_stringify_dict(void) {
    TEST("json-stringify: dict/object");
    Obj* input = mk_dict();
    Obj* key1 = mk_string("name");
    Obj* val1 = mk_string("Alice");
    Obj* key2 = mk_string("age");
    Obj* val2 = mk_int(30);
    dict_set(input, key1, val1);
    dict_set(input, key2, val2);

    Obj* result = prim_json_stringify(input);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING);
    /* Note: Dict iteration order is non-deterministic, just verify structure */
    ASSERT(strstr((char*)result->ptr, "\"name\"") != NULL);
    ASSERT(strstr((char*)result->ptr, "\"Alice\"") != NULL);
    ASSERT(strstr((char*)result->ptr, "\"age\"") != NULL);
    ASSERT(strstr((char*)result->ptr, "30") != NULL);
    ASSERT(((char*)result->ptr)[0] == '{');
    ASSERT(((char*)result->ptr)[strlen((char*)result->ptr) - 1] == '}');

    dec_ref(key1);
    dec_ref(val1);
    dec_ref(key2);
    dec_ref(val2);
    dec_ref(input);
    dec_ref(result);
    PASS();
}

void test_json_stringify_nested(void) {
    TEST("json-stringify: nested structure");
    Obj* inner = mk_array(2);
    array_push(inner, mk_int(1));
    array_push(inner, mk_int(2));

    Obj* input = mk_dict();
    Obj* key = mk_string("values");
    dict_set(input, key, inner);

    Obj* result = prim_json_stringify(input);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_STRING);
    /* Verify nested structure exists */
    ASSERT(strstr((char*)result->ptr, "\"values\"") != NULL);
    ASSERT(strstr((char*)result->ptr, "[1,2]") != NULL);

    dec_ref(key);
    dec_ref(inner);
    dec_ref(input);
    dec_ref(result);
    PASS();
}

/* ==================== JSON Complex Structure Tests ==================== */

void test_json_parse_complex_nested(void) {
    TEST("json-parse: deeply nested structure");
    Obj* input = mk_sym("{\"users\": [{\"name\": \"Alice\", \"roles\": [\"admin\", \"user\"]}, {\"name\": \"Bob\", \"roles\": [\"user\"]}]}");
    Obj* result = prim_json_parse(input);

    ASSERT_NOT_NULL(result);
    ASSERT(IS_BOXED(result));
    ASSERT(result->tag == TAG_DICT);

    dec_ref(input);
    dec_ref(result);
    PASS();
}

/* ==================== Test Runner ==================== */

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    printf("\n=== JSON Parsing Tests ===\n");
    
    printf("\n--- JSON String Parsing ---\n");
    test_json_parse_string_basic();
    test_json_parse_string_empty();
    test_json_parse_string_escaped();
    
    printf("\n--- JSON Number Parsing ---\n");
    test_json_parse_number_int();
    test_json_parse_number_negative();
    test_json_parse_number_float();
    test_json_parse_number_scientific();
    test_json_parse_number_zero();
    
    printf("\n--- JSON Boolean and Null ---\n");
    test_json_parse_boolean_true();
    test_json_parse_boolean_false();
    test_json_parse_null();
    
    printf("\n--- JSON Array Parsing ---\n");
    test_json_parse_array_empty();
    test_json_parse_array_numbers();
    test_json_parse_array_mixed();
    test_json_parse_array_nested();
    
    printf("\n--- JSON Object Parsing ---\n");
    test_json_parse_object_empty();
    test_json_parse_object_simple();
    test_json_parse_object_nested();
    test_json_parse_object_with_array();
    
    printf("\n--- JSON Error Handling ---\n");
    test_json_parse_error_missing_brace();
    test_json_parse_error_unclosed_string();
    test_json_parse_error_trailing_comma();
    test_json_parse_error_invalid_number();
    test_json_parse_error_trailing_content();
    test_json_parse_error_empty_input();
    
    printf("\n--- JSON Complex Structures ---\n");
    test_json_parse_complex_nested();

    printf("\n--- JSON Stringification ---\n");
    test_json_stringify_string();
    test_json_stringify_int();
    test_json_stringify_float();
    test_json_stringify_bool_true();
    test_json_stringify_bool_false();
    test_json_stringify_null();
    test_json_stringify_array();
    test_json_stringify_dict();
    test_json_stringify_nested();

    printf("\n=== Summary ===\n");
    printf("  Total:  %d\n", tests_run);
    printf("  Failed: %d\n", tests_failed);
    printf("\n");
    
    return tests_failed > 0 ? 1 : 0;
}
