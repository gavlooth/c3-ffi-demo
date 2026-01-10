/*
 * test_parse_all.c
 *
 * Purpose:
 *   Regression tests for the "whole program" parser entrypoint:
 *     omni_parser_parse_all() -> (exprs[], count)
 *
 * Why this matters:
 *   The CLI/compiler uses omni_parser_parse_all() to compile programs provided on
 *   stdin or from files. If parse_all fails to return expressions, the compiler
 *   reports "No expressions to compile" even for valid source text.
 *
 * Contract:
 *   - A single expression program should produce count==1.
 *   - Multiple expressions should produce a stable, ordered array.
 */

#include "../parser/parser.h"
#include "../ast/ast.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void assert_parses_to_count(const char* src, size_t expected_count) {
    OmniParser* p = omni_parser_new(src);
    assert(p);

    size_t n = 0;
    OmniValue** exprs = omni_parser_parse_all(p, &n);

    if (n != expected_count) {
        fprintf(stderr, "Expected %zu expressions but got %zu for source:\n%s\n",
                expected_count, n, src);
        /* Helpful rendering of whatever we did parse. */
        if (exprs) {
            for (size_t i = 0; exprs[i]; i++) {
                char* s = omni_value_to_string(exprs[i]);
                fprintf(stderr, "  expr[%zu] = %s\n", i, s ? s : "<null>");
                free(s);
            }
        }
        assert(0 && "unexpected parse_all expression count");
    }

    free(exprs);
    omni_parser_free(p);
}

static void test_parse_all_single_expr(void) {
    assert_parses_to_count("(+ 1 2)\n", 1);
}

static void test_parse_all_two_exprs(void) {
    assert_parses_to_count("(define x 1)\n(+ x 2)\n", 2);
}

int main(void) {
    test_parse_all_single_expr();
    test_parse_all_two_exprs();
    printf("PASS: test_parse_all\n");
    return 0;
}

