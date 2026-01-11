#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../ast/ast.h"
#include "../codegen/codegen.h"

/*
 * Non-lexical region end tests (Issue 3 P2).
 *
 * Goal (straight-line only):
 * Prove that codegen can emit a non-lexical region end for the main
 * scratch region when it becomes dead *before* the end of main().
 *
 * Reality check / constructive criticism:
 * The compiler currently allocates most values in `_local_region` via
 * helpers like `mk_int()` and `mk_cell()`. That means you cannot just
 * emit `region_exit(_local_region)` "early" and then keep allocating
 * more values in the same `_local_region`.
 *
 * Therefore, the minimal safe and testable form of Issue 3 P2 is:
 * - Between top-level expressions in `main()`, after the result is
 *   printed and freed, exit+destroy the current region and then create
 *   a fresh region for the next expression.
 *
 * This test asserts on the generated C to ensure there is *more than
 * one* `region_exit(_local_region);` inside `main()` when we compile a
 * program with multiple top-level expressions.
 */

static int tests_run;
static int tests_passed;

/*
 * These compiler-side tests are executed as separate binaries from the
 * `csrc/tests/Makefile` harness. A failing assertion must terminate the
 * process with a non-zero exit code, otherwise `make test` will happily
 * print "PASS" even when the check failed.
 *
 * Constructive criticism:
 * Many existing `csrc/tests/test_*.c` files use a "return from test()"
 * pattern that does not propagate failure reliably. This file uses an
 * explicit `exit(1)` so failures are not silently ignored.
 */
#define ASSERT_OR_DIE(cond)						\
	do {								\
		if (!(cond)) {						\
			fprintf(stderr,					\
				"\033[31mFAIL\033[0m:%d: %s\n",	\
				__LINE__, #cond);			\
			exit(1);					\
		}							\
	} while (0)

#define TEST(name) static void name(void)
#define RUN_TEST(name)							\
	do {								\
		printf("  %s: ", #name);				\
		fflush(stdout);						\
		name();							\
		tests_run++;						\
		tests_passed++;						\
		printf("\033[32mPASS\033[0m\n");			\
	} while (0)

static OmniValue *mk_sym(const char *name)
{
	return omni_new_sym(name);
}

static OmniValue *mk_int(long val)
{
	return omni_new_int(val);
}

static OmniValue *mk_cons(OmniValue *car, OmniValue *cdr)
{
	return omni_new_cell(car, cdr);
}

static OmniValue *mk_list2(OmniValue *a, OmniValue *b)
{
	return mk_cons(a, mk_cons(b, omni_nil));
}

static OmniValue *mk_list3(OmniValue *a, OmniValue *b, OmniValue *c)
{
	return mk_cons(a, mk_cons(b, mk_cons(c, omni_nil)));
}

/*
 * Count occurrences of a substring within a bounded range.
 * This keeps the assertion stable by only scanning `main()`'s text.
 */
TEST(test_main_emits_midstream_region_exit_for_multiple_exprs)
{
	/*
	 * Program:
	 *   (let ((x (cons 1 2))) x)
	 *   0
	 *
	 * Expected generated C:
	 * - `main()` contains one region_exit at the very end (cleanup)
	 * - AND one additional region_exit between the two expression blocks
	 *   (Issue 3 P2 non-lexical region end for straight-line top-level).
	 */

	OmniValue *bindings = mk_cons(
		mk_list2(mk_sym("x"), mk_list3(mk_sym("cons"),
					       mk_int(1), mk_int(2))),
		omni_nil
	);
	OmniValue *expr1 = mk_list3(mk_sym("let"), bindings, mk_sym("x"));
	OmniValue *expr2 = mk_int(0);
	OmniValue *exprs[2] = { expr1, expr2 };

	CodeGenContext *cg = omni_codegen_new_buffer();
	ASSERT_OR_DIE(cg != NULL);

	omni_codegen_program(cg, exprs, 2);

	char *output = omni_codegen_get_output(cg);
	ASSERT_OR_DIE(output != NULL);

	const char *main_start = strstr(output, "int main(void)");
	ASSERT_OR_DIE(main_start != NULL);

	/*
	 * Regression gate:
	 * We require a main()-specific marker comment for the non-lexical
	 * end insertion, otherwise counting `region_exit(_local_region);`
	 * is ambiguous (nested helper functions may also use that name).
	 */
	const char *marker = strstr(main_start,
		"ISSUE 3 P2: Non-lexical main() region end");
	if (!marker) {
		fprintf(stderr, "\n--- Generated main() (prefix) ---\n");
		fprintf(stderr, "%.*s\n", 1200, main_start);
		fprintf(stderr, "--- End generated main() ---\n");
		exit(1);
	}

	free(output);
	omni_codegen_free(cg);
}

int main(void)
{
	printf("\n\033[33m=== Non-Lexical Region End Tests (Issue 3 P2) ===\033[0m\n");

	RUN_TEST(test_main_emits_midstream_region_exit_for_multiple_exprs);

	printf("\n\033[33m=== Summary ===\033[0m\n");
	printf("  Total:  %d\n", tests_run);
	printf("  \033[32mPassed: %d\033[0m\n", tests_passed);
	printf("  Failed: 0\n");

	return 0;
}
