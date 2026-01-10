/*
 * test_rcg_region_value.c - Test region-aware value constructors
 *
 * Tests that all the mk_*_region functions correctly allocate
 * values in the given region.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/memory/region_core.h"
#include "../src/memory/region_value.h"

int main(void) {
    printf("Testing RCG Region-Aware Value Constructors...\n\n");

    // Test 1: Scalar values
    printf("[Test 1] Scalar values (T_INT, T_CHAR, T_FLOAT)...\n");
    Region* r1 = region_create();

    Value* i = mk_int_region(r1, 42);
    assert(i != NULL);
    assert(i->tag == T_INT);
    assert(i->i == 42);

    Value* c = mk_char_region(r1, 'A');
    assert(c != NULL);
    assert(c->tag == T_CHAR);
    assert(c->codepoint == 'A');

    Value* f = mk_float_region(r1, 3.14);
    assert(f != NULL);
    assert(f->tag == T_FLOAT);
    assert(f->f == 3.14);

    region_exit(r1);
    printf("  PASSED\n\n");

    // Test 2: Singletons (nil, nothing)
    printf("[Test 2] Singleton values (nil, nothing)...\n");
    Region* r2 = region_create();

    Value* nil1 = mk_nil_region(r2);
    Value* nil2 = mk_nil_region(NULL);
    assert(nil1 == nil2);  // Same singleton
    assert(nil1->tag == T_NIL);

    Value* nothing1 = mk_nothing_region(r2);
    Value* nothing2 = mk_nothing_region(NULL);
    assert(nothing1 == nothing2);  // Same singleton
    assert(nothing1->tag == T_NOTHING);

    region_exit(r2);
    printf("  PASSED\n\n");

    // Test 3: String values
    printf("[Test 3] String values (T_SYM, T_CODE, T_STRING, T_ERROR)...\n");
    Region* r3 = region_create();

    Value* sym = mk_sym_region(r3, "example");
    assert(sym != NULL);
    assert(sym->tag == T_SYM);
    assert(strcmp(sym->s, "example") == 0);

    Value* code = mk_code_region(r3, "(+ 1 2)");
    assert(code != NULL);
    assert(code->tag == T_CODE);
    assert(strcmp(code->s, "(+ 1 2)") == 0);

    Value* str = mk_string_region(r3, "Hello", 5);
    assert(str != NULL);
    assert(str->tag == T_STRING);
    assert(str->str.len == 5);
    assert(memcmp(str->str.data, "Hello", 5) == 0);

    Value* cstr = mk_string_cstr_region(r3, "World");
    assert(cstr != NULL);
    assert(cstr->tag == T_STRING);
    assert(cstr->str.len == 5);
    assert(strcmp(cstr->str.data, "World") == 0);

    Value* err = mk_error_region(r3, "test error");
    assert(err != NULL);
    assert(err->tag == T_ERROR);
    assert(strcmp(err->s, "test error") == 0);

    region_exit(r3);
    printf("  PASSED\n\n");

    // Test 4: Cell/Pairs
    printf("[Test 4] Cell/Pair construction...\n");
    Region* r4 = region_create();

    Value* car = mk_int_region(r4, 1);
    Value* cdr = mk_int_region(r4, 2);
    Value* cell = mk_cell_region(r4, car, cdr);

    assert(cell != NULL);
    assert(cell->tag == T_CELL);
    assert(cell->cell.car == car);
    assert(cell->cell.cdr == cdr);
    assert(cell->cell.car->i == 1);
    assert(cell->cell.cdr->i == 2);

    region_exit(r4);
    printf("  PASSED\n\n");

    // Test 5: Lambda/Closure
    printf("[Test 5] Lambda/Closure construction...\n");
    Region* r5 = region_create();

    Value* params = mk_nil_region(r5);
    Value* body = mk_nil_region(r5);
    Value* env = mk_nil_region(r5);
    Value* lam = mk_lambda_region(r5, params, body, env);

    assert(lam != NULL);
    assert(lam->tag == T_LAMBDA);
    assert(lam->lam.params == params);
    assert(lam->lam.body == body);
    assert(lam->lam.env == env);

    region_exit(r5);
    printf("  PASSED\n\n");

    // Test 6: Box
    printf("[Test 6] Box construction...\n");
    Region* r6 = region_create();

    Value* boxed = mk_int_region(r6, 100);
    Value* box = mk_box_region(r6, boxed);

    assert(box != NULL);
    assert(box->tag == T_BOX);
    assert(box->box_value == boxed);
    assert(box->box_value->i == 100);

    region_exit(r6);
    printf("  PASSED\n\n");

    // Test 7: Complex nested structure
    printf("[Test 7] Complex nested structure ((1 . 2) . (3 . 4))...\n");
    Region* r7 = region_create();

    Value* v1 = mk_int_region(r7, 1);
    Value* v2 = mk_int_region(r7, 2);
    Value* v3 = mk_int_region(r7, 3);
    Value* v4 = mk_int_region(r7, 4);

    Value* pair1 = mk_cell_region(r7, v1, v2);
    Value* pair2 = mk_cell_region(r7, v3, v4);
    Value* root = mk_cell_region(r7, pair1, pair2);

    assert(root != NULL);
    assert(root->tag == T_CELL);
    assert(root->cell.car->cell.car->i == 1);
    assert(root->cell.car->cell.cdr->i == 2);
    assert(root->cell.cdr->cell.car->i == 3);
    assert(root->cell.cdr->cell.cdr->i == 4);

    region_exit(r7);
    printf("  PASSED\n\n");

    // Test 8: Region independence
    printf("[Test 8] Region independence...\n");
    Region* r_src = region_create();
    Region* r_dst = region_create();

    Value* src_val = mk_int_region(r_src, 999);
    assert(src_val->i == 999);

    // Source region can be destroyed without affecting destination
    region_exit(r_src);

    Value* dst_val = mk_int_region(r_dst, 888);
    assert(dst_val->i == 888);

    region_exit(r_dst);
    printf("  PASSED\n\n");

    printf("=== All RCG Region-Aware Value Constructor tests PASSED ===\n");
    return 0;
}
