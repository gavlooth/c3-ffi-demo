/*
 * test_type_id.c
 *
 * Tests for Type ID mapping functions.
 *
 * Purpose:
 *   Verify that type_name_to_type_id, type_id_to_name, is_valid_type_id,
 *   type_id_can_inline, and type_id_inline_threshold correctly map between
 *   type names and TypeID enum values with proper validation.
 *
 * Why this matters:
 *   Type ID mapping is a core function used throughout the compiler for
 *   type checking, code generation, and memory management decisions.
 *   Incorrect mappings lead to wrong code generation or runtime crashes.
 *
 * Contract:
 *   - type_name_to_type_id: Maps type name strings to TypeID, handles aliases and unknown types
 *   - type_id_to_name: Maps TypeID to canonical type name string
 *   - is_valid_type_id: Returns true for valid TypeID range [0, TYPE_ID_MAX)
 *   - type_id_can_inline: Returns true for types that can be inline-allocated
 *   - type_id_inline_threshold: Returns size threshold for inline allocation
 *   - All functions handle NULL/invalid inputs gracefully
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../analysis/type_id.h"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN_TEST(name) do { \
    printf("  %s: ", #name); \
    name(); \
    tests_run++; \
    tests_passed++; \
    printf("\033[32mPASS\033[0m\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\033[31mFAIL\033[0m (line %d: %s)\n", __LINE__, #cond); \
        tests_run++; \
        return; \
    } \
} while(0)

/* ========== Test: Basic type name mappings ========== */

TEST(test_type_name_basic_int) {
    /* Canonical "Int" should map to TYPE_ID_INT */
    TypeID tid = type_name_to_type_id("Int");
    ASSERT(tid == TYPE_ID_INT);
}

TEST(test_type_name_alias_int) {
    /* Aliases should also map to TYPE_ID_INT */
    ASSERT(type_name_to_type_id("int") == TYPE_ID_INT);
    ASSERT(type_name_to_type_id("long") == TYPE_ID_INT);
}

TEST(test_type_name_basic_float) {
    TypeID tid = type_name_to_type_id("Float");
    ASSERT(tid == TYPE_ID_FLOAT);
}

TEST(test_type_name_alias_float) {
    ASSERT(type_name_to_type_id("float") == TYPE_ID_FLOAT);
    ASSERT(type_name_to_type_id("double") == TYPE_ID_FLOAT);
}

TEST(test_type_name_char) {
    ASSERT(type_name_to_type_id("Char") == TYPE_ID_CHAR);
    ASSERT(type_name_to_type_id("char") == TYPE_ID_CHAR);
}

/* ========== Test: Collection type mappings ========== */

TEST(test_type_name_pair_aliases) {
    /* Multiple names should map to TYPE_ID_PAIR */
    ASSERT(type_name_to_type_id("Pair") == TYPE_ID_PAIR);
    ASSERT(type_name_to_type_id("Cons") == TYPE_ID_PAIR);
    ASSERT(type_name_to_type_id("Cell") == TYPE_ID_PAIR);
    ASSERT(type_name_to_type_id("cons") == TYPE_ID_PAIR);
}

TEST(test_type_name_collections) {
    ASSERT(type_name_to_type_id("Array") == TYPE_ID_ARRAY);
    ASSERT(type_name_to_type_id("String") == TYPE_ID_STRING);
    ASSERT(type_name_to_type_id("string") == TYPE_ID_STRING);
    ASSERT(type_name_to_type_id("Symbol") == TYPE_ID_SYMBOL);
    ASSERT(type_name_to_type_id("Sym") == TYPE_ID_SYMBOL);
}

TEST(test_type_name_dict_aliases) {
    ASSERT(type_name_to_type_id("Dict") == TYPE_ID_DICT);
    ASSERT(type_name_to_type_id("Dictionary") == TYPE_ID_DICT);
    ASSERT(type_name_to_type_id("Map") == TYPE_ID_DICT);
}

/* ========== Test: Function and reference types ========== */

TEST(test_type_name_function_aliases) {
    ASSERT(type_name_to_type_id("Closure") == TYPE_ID_CLOSURE);
    ASSERT(type_name_to_type_id("Lambda") == TYPE_ID_CLOSURE);
    ASSERT(type_name_to_type_id("Function") == TYPE_ID_CLOSURE);
}

TEST(test_type_name_ref_types) {
    ASSERT(type_name_to_type_id("Box") == TYPE_ID_BOX);
    ASSERT(type_name_to_type_id("Ref") == TYPE_ID_BOX);
}

/* ========== Test: Concurrency types ========== */

/* DIRECTIVE: NO CHANNELS - test_type_name_channel removed */

TEST(test_type_name_thread) {
    ASSERT(type_name_to_type_id("Thread") == TYPE_ID_THREAD);
}

/* ========== Test: Control flow and special types ========== */

TEST(test_type_name_error) {
    ASSERT(type_name_to_type_id("Error") == TYPE_ID_ERROR);
}

TEST(test_type_name_atom_aliases) {
    ASSERT(type_name_to_type_id("Atom") == TYPE_ID_ATOM);
    ASSERT(type_name_to_type_id("Atomic") == TYPE_ID_ATOM);
}

TEST(test_type_name_tuple) {
    ASSERT(type_name_to_type_id("Tuple") == TYPE_ID_TUPLE);
    ASSERT(type_name_to_type_id("NamedTuple") == TYPE_ID_NAMED_TUPLE);
}

/* ========== Test: Type system types ========== */

TEST(test_type_name_generic) {
    ASSERT(type_name_to_type_id("Generic") == TYPE_ID_GENERIC);
}

TEST(test_type_name_kind) {
    ASSERT(type_name_to_type_id("Kind") == TYPE_ID_KIND);
    ASSERT(type_name_to_type_id("Type") == TYPE_ID_KIND);
}

TEST(test_type_name_nothing_aliases) {
    /* Nothing/Unit/Void should all map to TYPE_ID_NOTHING */
    ASSERT(type_name_to_type_id("Nothing") == TYPE_ID_NOTHING);
    ASSERT(type_name_to_type_id("Unit") == TYPE_ID_NOTHING);
    ASSERT(type_name_to_type_id("Void") == TYPE_ID_NOTHING);
}

/* ========== Test: Edge case - NULL and unknown types ========== */

TEST(test_type_name_null_input) {
    /* NULL input should return TYPE_ID_GENERIC */
    TypeID tid = type_name_to_type_id(NULL);
    ASSERT(tid == TYPE_ID_GENERIC);
}

TEST(test_type_name_unknown_type) {
    /* Unknown type name should return TYPE_ID_GENERIC */
    TypeID tid = type_name_to_type_id("UnknownTypeXYZ");
    ASSERT(tid == TYPE_ID_GENERIC);
}

TEST(test_type_name_empty_string) {
    /* Empty string should return TYPE_ID_GENERIC */
    TypeID tid = type_name_to_type_id("");
    ASSERT(tid == TYPE_ID_GENERIC);
}

/* ========== Test: TypeID to name reverse mapping ========== */

TEST(test_type_id_to_name_int) {
    const char* name = type_id_to_name(TYPE_ID_INT);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Int") == 0);
}

TEST(test_type_id_to_name_float) {
    const char* name = type_id_to_name(TYPE_ID_FLOAT);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Float") == 0);
}

TEST(test_type_id_to_name_char) {
    const char* name = type_id_to_name(TYPE_ID_CHAR);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Char") == 0);
}

TEST(test_type_id_to_name_pair) {
    const char* name = type_id_to_name(TYPE_ID_PAIR);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Pair") == 0);
}

TEST(test_type_id_to_name_array) {
    const char* name = type_id_to_name(TYPE_ID_ARRAY);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Array") == 0);
}

TEST(test_type_id_to_name_string) {
    const char* name = type_id_to_name(TYPE_ID_STRING);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "String") == 0);
}

TEST(test_type_id_to_name_symbol) {
    const char* name = type_id_to_name(TYPE_ID_SYMBOL);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Symbol") == 0);
}

TEST(test_type_id_to_name_dict) {
    const char* name = type_id_to_name(TYPE_ID_DICT);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Dict") == 0);
}

TEST(test_type_id_to_name_closure) {
    const char* name = type_id_to_name(TYPE_ID_CLOSURE);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Closure") == 0);
}

TEST(test_type_id_to_name_box) {
    const char* name = type_id_to_name(TYPE_ID_BOX);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Box") == 0);
}

TEST(test_type_id_to_name_channel) {
    const char* name = type_id_to_name(TYPE_ID_CHANNEL);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Channel") == 0);
}

TEST(test_type_id_to_name_thread) {
    const char* name = type_id_to_name(TYPE_ID_THREAD);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Thread") == 0);
}

TEST(test_type_id_to_name_error) {
    const char* name = type_id_to_name(TYPE_ID_ERROR);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Error") == 0);
}

TEST(test_type_id_to_name_atom) {
    const char* name = type_id_to_name(TYPE_ID_ATOM);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Atom") == 0);
}

TEST(test_type_id_to_name_tuple) {
    const char* name = type_id_to_name(TYPE_ID_TUPLE);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Tuple") == 0);
}

TEST(test_type_id_to_name_named_tuple) {
    const char* name = type_id_to_name(TYPE_ID_NAMED_TUPLE);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "NamedTuple") == 0);
}

TEST(test_type_id_to_name_generic) {
    const char* name = type_id_to_name(TYPE_ID_GENERIC);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Generic") == 0);
}

TEST(test_type_id_to_name_kind) {
    const char* name = type_id_to_name(TYPE_ID_KIND);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Kind") == 0);
}

TEST(test_type_id_to_name_nothing) {
    const char* name = type_id_to_name(TYPE_ID_NOTHING);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Nothing") == 0);
}

/* ========== Test: TypeID to name - invalid TypeID ========== */

TEST(test_type_id_to_name_invalid) {
    /* Invalid TypeID should return "Unknown" */
    const char* name = type_id_to_name((TypeID)999);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Unknown") == 0);
}

TEST(test_type_id_to_name_negative) {
    /* Negative TypeID should return "Unknown" */
    const char* name = type_id_to_name((TypeID)-1);
    ASSERT(name != NULL);
    ASSERT(strcmp(name, "Unknown") == 0);
}

/* ========== Test: Valid TypeID validation ========== */

TEST(test_is_valid_type_id_valid) {
    /* All known valid TypeIDs should be valid */
    ASSERT(is_valid_type_id(TYPE_ID_INT) == true);
    ASSERT(is_valid_type_id(TYPE_ID_FLOAT) == true);
    ASSERT(is_valid_type_id(TYPE_ID_CHAR) == true);
    ASSERT(is_valid_type_id(TYPE_ID_PAIR) == true);
    ASSERT(is_valid_type_id(TYPE_ID_ARRAY) == true);
    ASSERT(is_valid_type_id(TYPE_ID_STRING) == true);
    ASSERT(is_valid_type_id(TYPE_ID_SYMBOL) == true);
    ASSERT(is_valid_type_id(TYPE_ID_DICT) == true);
    ASSERT(is_valid_type_id(TYPE_ID_CLOSURE) == true);
    ASSERT(is_valid_type_id(TYPE_ID_BOX) == true);
    ASSERT(is_valid_type_id(TYPE_ID_CHANNEL) == true);
    ASSERT(is_valid_type_id(TYPE_ID_THREAD) == true);
    ASSERT(is_valid_type_id(TYPE_ID_ERROR) == true);
    ASSERT(is_valid_type_id(TYPE_ID_ATOM) == true);
    ASSERT(is_valid_type_id(TYPE_ID_TUPLE) == true);
    ASSERT(is_valid_type_id(TYPE_ID_NAMED_TUPLE) == true);
    ASSERT(is_valid_type_id(TYPE_ID_GENERIC) == true);
    ASSERT(is_valid_type_id(TYPE_ID_KIND) == true);
    ASSERT(is_valid_type_id(TYPE_ID_NOTHING) == true);
}

TEST(test_is_valid_type_id_boundary_last) {
    /* Last valid TypeID (TYPE_ID_NOTHING) should be valid */
    ASSERT(is_valid_type_id(TYPE_ID_NOTHING) == true);
}

TEST(test_is_valid_type_id_invalid_high) {
    /* TypeID >= TYPE_ID_MAX should be invalid */
    ASSERT(is_valid_type_id(TYPE_ID_MAX) == false);
    ASSERT(is_valid_type_id((TypeID)999) == false);
}

TEST(test_is_valid_type_id_invalid_negative) {
    /* Negative TypeID should be invalid */
    ASSERT(is_valid_type_id((TypeID)-1) == false);
    ASSERT(is_valid_type_id((TypeID)-999) == false);
}

/* ========== Test: Inline allocation capability ========== */

TEST(test_type_id_can_inline_true_types) {
    /* Types that CAN be inline-allocated */
    ASSERT(type_id_can_inline(TYPE_ID_INT) == true);
    ASSERT(type_id_can_inline(TYPE_ID_FLOAT) == true);
    ASSERT(type_id_can_inline(TYPE_ID_CHAR) == true);
    ASSERT(type_id_can_inline(TYPE_ID_PAIR) == true);
    ASSERT(type_id_can_inline(TYPE_ID_SYMBOL) == true);
    ASSERT(type_id_can_inline(TYPE_ID_BOX) == true);
    ASSERT(type_id_can_inline(TYPE_ID_ERROR) == true);
    ASSERT(type_id_can_inline(TYPE_ID_ATOM) == true);
    ASSERT(type_id_can_inline(TYPE_ID_TUPLE) == true);
    ASSERT(type_id_can_inline(TYPE_ID_NAMED_TUPLE) == true);
    ASSERT(type_id_can_inline(TYPE_ID_NOTHING) == true);
}

TEST(test_type_id_can_inline_false_types) {
    /* Types that CANNOT be inline-allocated */
    ASSERT(type_id_can_inline(TYPE_ID_ARRAY) == false);
    ASSERT(type_id_can_inline(TYPE_ID_STRING) == false);
    ASSERT(type_id_can_inline(TYPE_ID_DICT) == false);
    ASSERT(type_id_can_inline(TYPE_ID_CLOSURE) == false);
    ASSERT(type_id_can_inline(TYPE_ID_CHANNEL) == false);
    ASSERT(type_id_can_inline(TYPE_ID_THREAD) == false);
    ASSERT(type_id_can_inline(TYPE_ID_GENERIC) == false);
    ASSERT(type_id_can_inline(TYPE_ID_KIND) == false);
}

TEST(test_type_id_can_inline_invalid) {
    /* Invalid TypeID should return false */
    ASSERT(type_id_can_inline((TypeID)-1) == false);
    ASSERT(type_id_can_inline(TYPE_ID_MAX) == false);
    ASSERT(type_id_can_inline((TypeID)999) == false);
}

/* ========== Test: Inline threshold values ========== */

TEST(test_type_id_inline_threshold_inlineable) {
    /* Check specific threshold values for inlineable types */
    ASSERT(type_id_inline_threshold(TYPE_ID_INT) == 16);
    ASSERT(type_id_inline_threshold(TYPE_ID_FLOAT) == 16);
    ASSERT(type_id_inline_threshold(TYPE_ID_CHAR) == 8);
    ASSERT(type_id_inline_threshold(TYPE_ID_PAIR) == 56);
    ASSERT(type_id_inline_threshold(TYPE_ID_SYMBOL) == 24);
    ASSERT(type_id_inline_threshold(TYPE_ID_BOX) == 32);
    ASSERT(type_id_inline_threshold(TYPE_ID_ERROR) == 32);
    ASSERT(type_id_inline_threshold(TYPE_ID_ATOM) == 16);
    ASSERT(type_id_inline_threshold(TYPE_ID_TUPLE) == 48);
    ASSERT(type_id_inline_threshold(TYPE_ID_NAMED_TUPLE) == 64);
    ASSERT(type_id_inline_threshold(TYPE_ID_NOTHING) == 8);
}

TEST(test_type_id_inline_threshold_non_inlineable) {
    /* Non-inlineable types should return 0 */
    ASSERT(type_id_inline_threshold(TYPE_ID_ARRAY) == 0);
    ASSERT(type_id_inline_threshold(TYPE_ID_STRING) == 0);
    ASSERT(type_id_inline_threshold(TYPE_ID_DICT) == 0);
    ASSERT(type_id_inline_threshold(TYPE_ID_CLOSURE) == 0);
    ASSERT(type_id_inline_threshold(TYPE_ID_CHANNEL) == 0);
    ASSERT(type_id_inline_threshold(TYPE_ID_THREAD) == 0);
    ASSERT(type_id_inline_threshold(TYPE_ID_GENERIC) == 0);
    ASSERT(type_id_inline_threshold(TYPE_ID_KIND) == 0);
}

TEST(test_type_id_inline_threshold_invalid) {
    /* Invalid TypeID should return 0 */
    ASSERT(type_id_inline_threshold((TypeID)-1) == 0);
    ASSERT(type_id_inline_threshold(TYPE_ID_MAX) == 0);
    ASSERT(type_id_inline_threshold((TypeID)999) == 0);
}

/* ========== Test: Round-trip consistency ========== */

TEST(test_round_trip_int) {
    /* "Int" -> TYPE_ID_INT -> "Int" */
    TypeID tid = type_name_to_type_id("Int");
    const char* name = type_id_to_name(tid);
    ASSERT(strcmp(name, "Int") == 0);
}

TEST(test_round_trip_via_alias) {
    /* "int" (alias) -> TYPE_ID_INT -> "Int" (canonical) */
    TypeID tid = type_name_to_type_id("int");
    const char* name = type_id_to_name(tid);
    ASSERT(strcmp(name, "Int") == 0);
}

TEST(test_round_trip_pair) {
    /* "Pair" -> TYPE_ID_PAIR -> "Pair" */
    TypeID tid = type_name_to_type_id("Pair");
    const char* name = type_id_to_name(tid);
    ASSERT(strcmp(name, "Pair") == 0);
}

TEST(test_round_trip_via_pair_alias) {
    /* "Cons" (alias) -> TYPE_ID_PAIR -> "Pair" (canonical) */
    TypeID tid = type_name_to_type_id("Cons");
    const char* name = type_id_to_name(tid);
    ASSERT(strcmp(name, "Pair") == 0);
}

/* ========== Main Test Runner ========== */

int main(void) {
    printf("Running Type ID Mapping Tests...\n");
    printf("\n");

    printf("=== Basic Type Name Mappings ===\n");
    RUN_TEST(test_type_name_basic_int);
    RUN_TEST(test_type_name_alias_int);
    RUN_TEST(test_type_name_basic_float);
    RUN_TEST(test_type_name_alias_float);
    RUN_TEST(test_type_name_char);

    printf("\n=== Collection Type Mappings ===\n");
    RUN_TEST(test_type_name_pair_aliases);
    RUN_TEST(test_type_name_collections);
    RUN_TEST(test_type_name_dict_aliases);

    printf("\n=== Function and Reference Types ===\n");
    RUN_TEST(test_type_name_function_aliases);
    RUN_TEST(test_type_name_ref_types);

    printf("\n=== Concurrency Types ===\n");
    /* DIRECTIVE: NO CHANNELS - test_type_name_channel removed */
    RUN_TEST(test_type_name_thread);

    printf("\n=== Control Flow and Special Types ===\n");
    RUN_TEST(test_type_name_error);
    RUN_TEST(test_type_name_atom_aliases);
    RUN_TEST(test_type_name_tuple);

    printf("\n=== Type System Types ===\n");
    RUN_TEST(test_type_name_generic);
    RUN_TEST(test_type_name_kind);
    RUN_TEST(test_type_name_nothing_aliases);

    printf("\n=== Edge Cases - NULL and Unknown ===\n");
    RUN_TEST(test_type_name_null_input);
    RUN_TEST(test_type_name_unknown_type);
    RUN_TEST(test_type_name_empty_string);

    printf("\n=== TypeID to Name Reverse Mapping ===\n");
    RUN_TEST(test_type_id_to_name_int);
    RUN_TEST(test_type_id_to_name_float);
    RUN_TEST(test_type_id_to_name_char);
    RUN_TEST(test_type_id_to_name_pair);
    RUN_TEST(test_type_id_to_name_array);
    RUN_TEST(test_type_id_to_name_string);
    RUN_TEST(test_type_id_to_name_symbol);
    RUN_TEST(test_type_id_to_name_dict);
    RUN_TEST(test_type_id_to_name_closure);
    RUN_TEST(test_type_id_to_name_box);
    RUN_TEST(test_type_id_to_name_channel);
    RUN_TEST(test_type_id_to_name_thread);
    RUN_TEST(test_type_id_to_name_error);
    RUN_TEST(test_type_id_to_name_atom);
    RUN_TEST(test_type_id_to_name_tuple);
    RUN_TEST(test_type_id_to_name_named_tuple);
    RUN_TEST(test_type_id_to_name_generic);
    RUN_TEST(test_type_id_to_name_kind);
    RUN_TEST(test_type_id_to_name_nothing);
    RUN_TEST(test_type_id_to_name_invalid);
    RUN_TEST(test_type_id_to_name_negative);

    printf("\n=== Valid TypeID Validation ===\n");
    RUN_TEST(test_is_valid_type_id_valid);
    RUN_TEST(test_is_valid_type_id_boundary_last);
    RUN_TEST(test_is_valid_type_id_invalid_high);
    RUN_TEST(test_is_valid_type_id_invalid_negative);

    printf("\n=== Inline Allocation Capability ===\n");
    RUN_TEST(test_type_id_can_inline_true_types);
    RUN_TEST(test_type_id_can_inline_false_types);
    RUN_TEST(test_type_id_can_inline_invalid);

    printf("\n=== Inline Threshold Values ===\n");
    RUN_TEST(test_type_id_inline_threshold_inlineable);
    RUN_TEST(test_type_id_inline_threshold_non_inlineable);
    RUN_TEST(test_type_id_inline_threshold_invalid);

    printf("\n=== Round-trip Consistency ===\n");
    RUN_TEST(test_round_trip_int);
    RUN_TEST(test_round_trip_via_alias);
    RUN_TEST(test_round_trip_pair);
    RUN_TEST(test_round_trip_via_pair_alias);

    printf("\n");
    printf("======================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);
    printf("======================================\n");

    return (tests_run == tests_passed) ? 0 : 1;
}
