/*
 * test_vmem_arena_main.c - Standalone test runner for vmem_arena
 *
 * Compile: gcc -O2 -Wall -Wextra -o test_vmem_arena test_vmem_arena_main.c
 * Run: ./test_vmem_arena
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include vmem_arena implementation */
#define VMEM_ARENA_IMPLEMENTATION
#include "../../third_party/arena/vmem_arena.h"

/* Test counters */
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;

/* Include test cases */
#include "test_vmem_arena.c"

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	printf("VMemArena Test Suite\n");
	printf("====================\n");
	printf("Testing O(1) virtual memory arena allocator\n");

	run_vmem_arena_tests();

	printf("\n====================\n");
	printf("Tests: %d | Passed: %d | Failed: %d\n",
	       tests_run, tests_passed, tests_failed);

	return tests_failed > 0 ? 1 : 0;
}
