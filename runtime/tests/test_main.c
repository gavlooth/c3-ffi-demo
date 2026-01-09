/* OmniLisp Runtime Test Suite - Single Compilation Unit */
/* Define POSIX features FIRST before any includes */
#define _POSIX_C_SOURCE 200809L

#include "test_framework.h"
#include "../src/util/hashmap.c"
#include "../src/memory/arena_core.c"
#include "../src/memory/region_core.c"
#include "../src/memory/transmigrate.c"
#include "../src/memory/region_value.c"
#include "../src/memory/region_metadata.c"
#include "../src/runtime.c"
#include "../src/math_numerics.c"

/* Test counter definitions */
int tests_run = 0;
int tests_passed = 0;
int tests_failed = 0;
const char* current_suite = NULL;

static int run_slow_tests_enabled(void) {
    const char* level = getenv("RUNTIME_TEST_LEVEL");
    if (!level) return 0;
    return strcmp(level, "slow") == 0 || strcmp(level, "all") == 0 || strcmp(level, "1") == 0;
}

/* Include all test files directly */
#include "test_constructors.c"
#include "test_memory.c"
#include "test_primitives.c"
#include "test_lists.c"
#include "test_closures.c"
#include "test_tagged_pointers.c"
/* Note: test_arena.c, test_scc.c, test_weak_refs.c, test_borrowref.c, test_deferred.c, test_sym_concurrency.c, and test_component.c use deprecated/internal APIs - commented out */
/* #include "test_arena.c" */
/* #include "test_scc.c" */
#include "test_concurrency.c"
/* #include "test_weak_refs.c" */
/* #include "test_borrowref.c" */
/* #include "test_deferred.c" */
#include "test_channel_semantics.c"
/* #include "test_sym_concurrency.c" */
/* #include "test_component.c" */
/* Note: test_stress.c uses deprecated/internal APIs - commented out */
/* #include "test_stress.c" */
#include "test_stress_memory.c"
#include "test_edge_cases_memory.c"
#include "test_performance.c"
#include "test_stress_comprehensive.c"
#include "test_performance_comprehensive.c"
#include "test_inline_buf_splice.c"
#include "test_transmigrate_immediates.c"
#include "test_immediate_array_transmigrate.c"
#include "test_transmigrate_pair_batch.c"
#include "test_transmigrate_external_ptrs.c"
#include "test_array_boxed_flag.c"
#include "test_transmigrate_boxed_scalars.c"
#include "test_transmigrate_forwarding_table.c"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("OmniLisp Runtime Test Suite\n");
    printf("==========================\n");
    printf("Comprehensive testing of C runtime\n\n");

    /* Core functionality tests */
    run_constructor_tests();
    run_memory_tests();
    run_primitive_tests();

    /* Data structure tests */
    run_list_tests();
    run_closure_tests();

    /* Tagged pointer tests */
    run_tagged_pointer_tests();

    /* Memory management tests */
    /* Note: Arena, SCC, WeakRefs, BorrowRef, and Deferred tests use deprecated/internal APIs - commented out */
    /* run_arena_tests(); */
    /* run_scc_tests(); */
    /* run_weak_refs_tests(); */
    /* run_borrowref_tests(); */
    /* run_deferred_tests(); */
    run_channel_semantics_tests();
    /* run_component_tests(); */

    if (run_slow_tests_enabled()) {
        run_performance_tests();
        run_concurrency_tests();
        /* run_sym_concurrency_tests(); */
        /* run_stress_tests(); */
        run_stress_memory_tests();
        run_edge_case_memory_tests();
        run_comprehensive_stress_tests();  /* 400+ new stress tests */
        run_comprehensive_performance_tests();  /* 200+ new performance tests */
    }

    /* Phase 31.4: Inline Buf Splice Soundness Tests */
    run_inline_buf_splice_tests();

    /* Phase 32.1: Immediate Root Fast-Path Tests */
    run_transmigrate_immediates_tests();

    /* Phase 33.3: Immediate Array Transmigration Fast-Path Tests */
    run_immediate_array_transmigrate_tests();

    /* Phase 33.4: Pair Batch Allocator Soundness Tests */
    run_transmigrate_pair_batch_tests();

    /* Phase 34.1: External Pointer Filtering Tests */
    run_transmigrate_external_ptrs_tests();

    /* Phase 34.2: Array Boxed Flag Tests */
    run_array_boxed_flag_tests();

    /* Correctness: Boxed Scalar Transmigration */
    run_transmigrate_boxed_scalars_tests();

    /* Phase 35 (P0): Forwarding table remap mode selection */
    run_transmigrate_forwarding_table_tests();

    TEST_EXIT();
}
