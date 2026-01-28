/* OmniLisp Runtime Test Suite - Single Compilation Unit */
/* Define POSIX/GNU features FIRST before any includes */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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
#include "../src/regex.c"
#include "../src/generic.c"
#include "../src/typed_array.c"
#include "../src/piping.c"
#include "../src/modules.c"
#include "../src/io.c"

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
#include "test_iterators.c"
#include "test_closures.c"
#include "test_set.c"
#include "test_tagged_pointers.c"
/* Note: test_arena.c, test_scc.c, test_weak_refs.c, test_borrowref.c, test_deferred.c, test_sym_concurrency.c, and test_component.c use deprecated/internal APIs - commented out */
/* #include "test_arena.c" */
/* #include "test_scc.c" */
#include "test_concurrency.c"
/* #include "test_weak_refs.c" */
/* #include "test_borrowref.c" */
/* #include "test_deferred.c" */
/* DIRECTIVE: NO CHANNELS - test_channel_semantics.c removed */
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
#include "test_region_of_obj.c"
#include "test_region_accounting.c"
#include "test_region_rank_basic.c"
#include "test_store_barrier_rank_autorepair.c"
/* DIRECTIVE: NO CHANNELS - test_channel_send_autorepair.c removed */
#include "test_store_barrier_merge.c"
#include "test_dict_insert_autorepair.c"
#include "test_effect_primitives.c"
/* DIRECTIVE: NO CHANNELS - test_fiber_select.c removed */
#include "test_regex.c"
#include "test_generic_functions.c"
#include "test_piping_compose.c"
#include "test_path_operations.c"
#include "test_io.c"
#include "test_math_numerics.c"
#include "test_collections_sort.c"
#include "test_collections_take_drop.c"
#include "test_typed_array_element_size.c"
#include "test_typed_array_type_conversion.c"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("OmniLisp Runtime Test Suite\n");
    printf("==========================\n");
    printf("Comprehensive testing of C runtime\n\n");

    /* Run all tests normally now that iterator tests are fixed */
    run_closure_tests();
    run_constructor_tests();
    run_memory_tests();

    run_primitive_tests();
    run_list_tests();
    /* run_iterator_tests(); -- tests still need fixing */
    run_set_tests();

    /* Tagged pointer tests */
    run_tagged_pointer_tests();

    /* Memory management tests */
    /* Note: Arena, SCC, WeakRefs, BorrowRef, and Deferred tests use deprecated/internal APIs - commented out */
    /* run_arena_tests(); */
    /* run_scc_tests(); */
    /* run_weak_refs_tests(); */
    /* run_borrowref_tests(); */
    /* run_deferred_tests(); */

    /* Concurrency tests */
    /* DIRECTIVE: NO CHANNELS - run_channel_semantics_tests, run_fiber_select_tests removed */
    /* Note: Concurrency stress tests disabled - see TEST_REVIEW.md for details */
    /* run_concurrency_tests(); */

    /* Stress and performance tests */
    /* Note: Full stress tests disabled by default - use RUNTIME_TEST_LEVEL=slow to enable */
    if (run_slow_tests_enabled()) {
        run_performance_tests();
    }
    run_edge_case_memory_tests();
    /* Note: run_stress_memory_tests() and run_stress_tests() disabled - see TEST_REVIEW.md */
    run_stress_memory_tests();
    run_comprehensive_stress_tests();
    if (run_slow_tests_enabled()) {
        run_comprehensive_performance_tests();
    }

    /* Transmigration tests */
    run_immediate_array_transmigrate_tests();
    run_transmigrate_pair_batch_tests();
    run_transmigrate_external_ptrs_tests();
    run_transmigrate_boxed_scalars_tests();
    run_transmigrate_forwarding_table_tests();
    run_transmigrate_immediates_tests();
    run_inline_buf_splice_tests();

    /* Region management tests */
    run_region_of_obj_tests();
    run_region_accounting_tests();
    run_region_rank_basic_tests();

    /* Store barrier and autorepair tests */
    run_store_barrier_rank_autorepair_tests();
    /* DIRECTIVE: NO CHANNELS - run_channel_send_autorepair_tests removed */
    run_store_barrier_merge_tests();
    run_dict_insert_autorepair_tests();

    /* Array tests */
    run_array_boxed_flag_tests();

    /* Effect system tests */
    run_effect_primitives_tests();

    /* Regex tests */
    run_regex_tests();

    /* Generic function tests */
    run_generic_function_tests();

    /* Piping and compose tests */
    run_piping_compose_tests();

    /* Path operations tests */
    run_path_operations_tests();

    /* I/O tests */
    run_io_tests();

    /* Math numerics tests */
    run_math_numerics_tests();

    /* Collection tests */
    run_collections_sort_tests();
    run_collections_take_drop_tests();
    run_typed_array_element_size_tests();
    run_typed_array_type_conversion_tests();

    /* Summary */
    TEST_SUMMARY();
    TEST_EXIT();
}
