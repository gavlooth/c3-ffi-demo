/*
 * test_rcg_transmigrate.c - Test the transmigrate (deep copy) functionality
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/memory/transmigrate.h"
#include "../src/memory/region_value.h"
#include "../../src/runtime/types.h"

int main(void) {
    printf("Testing RCG Transmigrate...\n\n");

    // Test 1: Simple Cell Copy
    printf("[Test 1] Simple Cell Copy...\n");
    Region* r1 = region_create();
    Region* r2 = region_create();

    Value* cell = region_alloc(r1, sizeof(Value));
    cell->mark = 0;
    cell->tag = T_CELL;
    cell->type = NULL;
    cell->cell.car = region_alloc(r1, sizeof(Value));
    cell->cell.car->mark = 0;
    cell->cell.car->tag = T_INT;
    cell->cell.car->type = NULL;
    cell->cell.car->i = 42;
    cell->cell.cdr = NULL;

    Value* new_cell = transmigrate(cell, r1, r2);

    // Verify structure
    assert(new_cell != cell);
    assert(new_cell->tag == T_CELL);
    assert(new_cell->cell.car->tag == T_INT);
    assert(new_cell->cell.car->i == 42);
    assert(new_cell->cell.cdr == NULL);

    region_exit(r1);
    region_exit(r2);
    printf("  PASSED\n\n");

    // Test 2: Cycle Detection
    printf("[Test 2] Cycle Detection (P1 -> P2 -> P1)...\n");
    Region* r_src = region_create();
    Region* r_dst = region_create();

    Value* p1 = region_alloc(r_src, sizeof(Value));
    Value* p2 = region_alloc(r_src, sizeof(Value));

    p1->mark = 0;
    p1->tag = T_CELL;
    p1->type = NULL;
    p1->cell.car = mk_int_region(r_src, 1);
    p1->cell.cdr = p2;

    p2->mark = 0;
    p2->tag = T_CELL;
    p2->type = NULL;
    p2->cell.car = mk_int_region(r_src, 2);
    p2->cell.cdr = p1;

    printf("  Copying cycle...\n");
    Value* new_p1 = transmigrate(p1, r_src, r_dst);

    assert(new_p1 != p1);
    assert(new_p1->tag == T_CELL);
    assert(new_p1->cell.car->i == 1);

    Value* new_p2 = new_p1->cell.cdr;
    assert(new_p2 != p2);
    assert(new_p2->tag == T_CELL);
    assert(new_p2->cell.car->i == 2);
    assert(new_p2->cell.cdr == new_p1);

    region_exit(r_src);
    region_exit(r_dst);
    printf("  PASSED\n\n");

    // Test 3: Iterative List Copy (10k elements)
    printf("[Test 3] Iterative List Copy (10,000 elements)...\n");
    Region* r_list_src = region_create();
    Region* r_list_dst = region_create();

    Value* head = NULL;
    Value* tail = NULL;
    for (int i = 0; i < 10000; i++) {
        Value* v = mk_cell_region(r_list_src, mk_int_region(r_list_src, i), NULL);
        if (!head) {
            head = v;
        } else {
            tail->cell.cdr = v;
        }
        tail = v;
    }

    Value* new_head = transmigrate(head, r_list_src, r_list_dst);
    assert(new_head != head);
    
    Value* curr = new_head;
    for (int i = 0; i < 10000; i++) {
        assert(curr->tag == T_CELL);
        assert(curr->cell.car->i == i);
        curr = curr->cell.cdr;
    }
    assert(curr == NULL);

    region_exit(r_list_src);
    region_exit(r_list_dst);
    printf("  PASSED\n\n");

    printf("=== All RCG Transmigrate tests PASSED ===\n");
    return 0;
}