/*
 * test_syntax_sugar.c
 *
 * Purpose:
 *   Contract tests for OmniLisp *surface syntax* sugars that must be stable:
 *     1) `:name` is reader sugar for `(quote name)` and should be identical to `'name`.
 *     2) `^:key` is reserved syntax (NOT a normal symbol) that is still representable
 *        in the AST as a marker symbol whose string starts with "^:".
 *     3) Repeated metadata keys ("double metadata") must resolve deterministically:
 *        last-wins, matching the project standardization decision.
 *
 * Why this file exists:
 *   The codebase has historically had multiple competing syntaxes in docs/tests.
 *   These tests pin down the canonical choices so parser and analysis cannot drift.
 *
 * NOTE:
 *   This is a *parser+analysis* test. It does not test runtime printing.
 */

#include "../parser/parser.h"
#include "../analysis/analysis.h"
#include "../ast/ast.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----------------------------- Test Helpers ----------------------------- */

static OmniValue* must_parse(const char* src) {
    OmniValue* v = omni_parse_string(src);
    if (!v || omni_is_error(v) || omni_is_nil(v)) {
        fprintf(stderr, "Parse failed for: %s\n", src);
        assert(0 && "parse failed");
    }
    return v;
}

	static void assert_sym_eq(OmniValue* v, const char* s) {
	    assert(v);
	    if (!omni_is_sym(v)) {
	        /*
	         * This test suite is intentionally strict about AST shapes.
	         * When the shape does not match, we print as much context as possible
	         * to make debugging parser regressions straightforward.
	         */
	        char* rendered = omni_value_to_string(v);
	        fprintf(stderr,
	                "assert_sym_eq: expected SYM \"%s\" but got tag=%s, rendered=%s\n",
	                s,
	                omni_tag_name(v->tag),
	                rendered ? rendered : "<null>");
	        free(rendered);
	        assert(0 && "expected OMNI_SYM");
	    }
	    assert(v->str_val);
	    assert(strcmp(v->str_val, s) == 0);
	}

static void assert_is_quoted_symbol(OmniValue* expr, const char* sym_name) {
    /*
     * Expected shape:
     *   (quote <sym>)
     *
     * In list terms:
     *   expr = (quote . ((sym) . nil))
     */
    assert(expr);
    assert(omni_is_cell(expr));

    OmniValue* head = omni_car(expr);
    assert_sym_eq(head, "quote");

    OmniValue* args = omni_cdr(expr);
    assert(omni_is_cell(args));

    OmniValue* quoted = omni_car(args);
    assert_sym_eq(quoted, sym_name);

    OmniValue* tail = omni_cdr(args);
    assert(omni_is_nil(tail));
}

/* ------------------------------- Test Cases ------------------------------ */

static void test_colon_symbol_sugar_equivalence(void) {
    /*
     * Canonical decision:
     *   :foo is *pure sugar* for 'foo.
     *
     * Therefore, parsing ":foo" must produce the same AST as parsing "'foo".
     * (We intentionally normalize toward the quote form.)
     */
    OmniValue* a = must_parse(":foo");
    OmniValue* b = must_parse("'foo");

    assert_is_quoted_symbol(a, "foo");
    assert_is_quoted_symbol(b, "foo");
}

static void test_metadata_key_token_is_parseable(void) {
    /*
     * Canonical decision:
     *   ^:parent (and other ^:keys) are reserved syntax tokens, not normal symbols.
     *
     * However, the AST still represents them as a symbol marker with str_val "^:parent"
     * so the analysis phase can recognize and consume them (e.g., in define/let).
     */
    OmniValue* expr = must_parse("(define ^:parent {Any} {abstract X} [])");
    assert(omni_is_cell(expr));
    assert_sym_eq(omni_car(expr), "define");

    /* Second element should be the metadata marker */
    OmniValue* args = omni_cdr(expr);
    assert(omni_is_cell(args));
    OmniValue* meta_key = omni_car(args);
    assert_sym_eq(meta_key, "^:parent");
}

static void test_double_metadata_last_wins(void) {
    /*
     * Canonical decision:
     *   When the same metadata key appears multiple times ("double metadata"),
     *   the *last* occurrence wins.
     *
     * Here, we define an abstract type X with two parent declarations:
     *   ^:parent {A} then ^:parent {B}
     *
     * The resolved parent of X must be "B".
     */
    OmniValue* expr = must_parse("(define ^:parent {A} ^:parent {B} {abstract X} [])");

    AnalysisContext* ctx = omni_analysis_new();
    assert(ctx);
    omni_analyze(ctx, expr);

    TypeDef* x = omni_get_type(ctx, "X");
    assert(x);
    assert(x->parent);
    assert(strcmp(x->parent, "B") == 0 && "duplicate ^:parent must be last-wins");

    omni_analysis_free(ctx);
}

static void test_double_metadata_last_wins_with_meta_list(void) {
    /*
     * The analyzer also supports an explicit `(with-meta <meta> <obj>)` wrapper.
     *
     * In that representation, <meta> can be a LIST of metadata entries:
     *   ((^:parent {A}) (^:parent {B}) ...)
     *
     * Canonical decision (same as the prefix marker form):
     *   Duplicate metadata keys are deterministic and last-wins.
     */
    OmniValue* expr = must_parse("(define (with-meta ((^:parent {A}) (^:parent {B})) {abstract X}) [])");

    AnalysisContext* ctx = omni_analysis_new();
    assert(ctx);
    omni_analyze(ctx, expr);

    TypeDef* x = omni_get_type(ctx, "X");
    assert(x);
    assert(x->parent);
    assert(strcmp(x->parent, "B") == 0 && "duplicate ^:parent must be last-wins (with-meta list form)");

    omni_analysis_free(ctx);
}

int main(void) {
    test_colon_symbol_sugar_equivalence();
    test_metadata_key_token_is_parseable();
    test_double_metadata_last_wins();
    test_double_metadata_last_wins_with_meta_list();

    printf("PASS: test_syntax_sugar\n");
    return 0;
}
