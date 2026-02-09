/**
 * =============================================================================
 * UNIT TESTS FOR SPARSESET DATA STRUCTURE
 * =============================================================================
 *
 * Comprehensive test suite covering:
 * - Basic operations (insert, remove, contains)
 * - Edge cases (zero key, large keys, empty set)
 * - Iteration behavior
 * - Stress testing
 *
 * Run with: run_sparse_set_tests()
 */
module main;

import std::io;

// =============================================================================
// TEST INFRASTRUCTURE
// =============================================================================

fn void print_test_start(String test_name) {
    io::printfn("  [TEST] %s...", test_name);
}

fn void print_test_pass(String test_name) {
    io::printfn("  [PASS] %s", test_name);
}

fn void print_section(String section_name) {
    io::printfn("\n=== %s ===", section_name);
}

/**
 * Helper: Create a fresh SparseSet for testing.
 * C3 Lists are zero-initialized by default, which is valid.
 */
fn SparseSet create_sparse_set() {
    SparseSet set;
    set.live_count = 0;
    return set;
}

// =============================================================================
// BASIC OPERATIONS TESTS
// =============================================================================

/**
 * Test inserting a single element and verifying it exists.
 */
fn void test_insert_single() {
    print_test_start("test_insert_single");

    SparseSet set = create_sparse_set();

    set.insert(5);

    assert(set.contains(5), "Set should contain inserted key 5");
    assert(set.live_count == 1, "Live count should be 1 after single insert");

    set.release();
    print_test_pass("test_insert_single");
}

/**
 * Test inserting multiple elements.
 */
fn void test_insert_multiple() {
    print_test_start("test_insert_multiple");

    SparseSet set = create_sparse_set();

    set.insert(1);
    set.insert(5);
    set.insert(10);
    set.insert(3);

    assert(set.contains(1), "Set should contain 1");
    assert(set.contains(5), "Set should contain 5");
    assert(set.contains(10), "Set should contain 10");
    assert(set.contains(3), "Set should contain 3");
    assert(set.live_count == 4, "Live count should be 4");

    set.release();
    print_test_pass("test_insert_multiple");
}

/**
 * Test that inserting duplicates is idempotent (no duplicates created).
 */
fn void test_insert_duplicate() {
    print_test_start("test_insert_duplicate");

    SparseSet set = create_sparse_set();

    set.insert(7);
    set.insert(7);
    set.insert(7);

    assert(set.contains(7), "Set should contain 7");
    assert(set.live_count == 1, "Live count should still be 1 after duplicate inserts");

    // Verify only one entry in packed_keys live region
    uint[] keys = set.get_live_keys();
    assert(keys.len == 1, "Should have exactly one live key");
    assert(keys[0] == 7, "The single key should be 7");

    set.release();
    print_test_pass("test_insert_duplicate");
}

/**
 * Test removing a single element.
 */
fn void test_remove_single() {
    print_test_start("test_remove_single");

    SparseSet set = create_sparse_set();

    set.insert(42);
    assert(set.contains(42), "Set should contain 42 after insert");

    set.remove(42);
    assert(!set.contains(42), "Set should not contain 42 after removal");
    assert(set.live_count == 0, "Live count should be 0 after removing only element");

    set.release();
    print_test_pass("test_remove_single");
}

/**
 * Test that removing a nonexistent element is a no-op.
 */
fn void test_remove_nonexistent() {
    print_test_start("test_remove_nonexistent");

    SparseSet set = create_sparse_set();

    set.insert(1);
    set.insert(2);
    uint count_before = set.live_count;

    // Remove element that was never inserted
    set.remove(999);

    assert(set.live_count == count_before, "Live count should not change when removing nonexistent key");
    assert(set.contains(1), "Existing keys should remain");
    assert(set.contains(2), "Existing keys should remain");

    set.release();
    print_test_pass("test_remove_nonexistent");
}

/**
 * Test that contains returns false for empty set.
 */
fn void test_contains_empty() {
    print_test_start("test_contains_empty");

    SparseSet set = create_sparse_set();

    assert(!set.contains(0), "Empty set should not contain 0");
    assert(!set.contains(1), "Empty set should not contain 1");
    assert(!set.contains(100), "Empty set should not contain 100");
    assert(!set.contains(999999), "Empty set should not contain large key");

    set.release();
    print_test_pass("test_contains_empty");
}

/**
 * Test that contains returns false after removal.
 */
fn void test_contains_after_remove() {
    print_test_start("test_contains_after_remove");

    SparseSet set = create_sparse_set();

    set.insert(10);
    set.insert(20);
    set.insert(30);

    set.remove(20);

    assert(set.contains(10), "10 should still be present");
    assert(!set.contains(20), "20 should not be present after removal");
    assert(set.contains(30), "30 should still be present");
    assert(set.live_count == 2, "Live count should be 2");

    set.release();
    print_test_pass("test_contains_after_remove");
}

// =============================================================================
// EDGE CASES TESTS
// =============================================================================

/**
 * Test that key 0 works correctly.
 */
fn void test_insert_zero() {
    print_test_start("test_insert_zero");

    SparseSet set = create_sparse_set();

    set.insert(0);

    assert(set.contains(0), "Set should contain key 0");
    assert(set.live_count == 1, "Live count should be 1");

    // Verify it's in the live keys
    uint[] keys = set.get_live_keys();
    assert(keys.len == 1, "Should have one live key");
    assert(keys[0] == 0, "The key should be 0");

    // Test removal of key 0
    set.remove(0);
    assert(!set.contains(0), "Key 0 should be removed");
    assert(set.live_count == 0, "Live count should be 0");

    set.release();
    print_test_pass("test_insert_zero");
}

/**
 * Test that sparse array grows correctly for large keys.
 */
fn void test_insert_large_key() {
    print_test_start("test_insert_large_key");

    SparseSet set = create_sparse_set();

    // Insert a small key first
    set.insert(5);

    // Insert a much larger key - sparse array must grow
    set.insert(1000);

    assert(set.contains(5), "Should still contain 5");
    assert(set.contains(1000), "Should contain 1000 after growth");
    assert(set.live_count == 2, "Live count should be 2");

    // Verify sparse array grew
    assert(set.key_position_index.len() > 1000, "Sparse array should have grown to accommodate key 1000");

    set.release();
    print_test_pass("test_insert_large_key");
}

/**
 * Test removing the last element makes set empty.
 */
fn void test_remove_last_element() {
    print_test_start("test_remove_last_element");

    SparseSet set = create_sparse_set();

    set.insert(100);
    set.insert(200);
    set.insert(300);

    set.remove(100);
    assert(set.live_count == 2, "Live count should be 2");

    set.remove(200);
    assert(set.live_count == 1, "Live count should be 1");

    set.remove(300);
    assert(set.live_count == 0, "Live count should be 0");
    assert(!set.contains(100), "Should not contain 100");
    assert(!set.contains(200), "Should not contain 200");
    assert(!set.contains(300), "Should not contain 300");

    // Get live keys should be empty
    uint[] keys = set.get_live_keys();
    assert(keys.len == 0, "Live keys should be empty");

    set.release();
    print_test_pass("test_remove_last_element");
}

/**
 * Test swap-and-pop maintains correctness.
 * When removing a non-last element, the last element is swapped in.
 */
fn void test_swap_and_pop_correctness() {
    print_test_start("test_swap_and_pop_correctness");

    SparseSet set = create_sparse_set();

    // Insert in order: 10, 20, 30, 40
    set.insert(10);
    set.insert(20);
    set.insert(30);
    set.insert(40);

    // Remove 20 (middle element) - 40 should be swapped into its place
    set.remove(20);

    // All remaining should still be findable
    assert(set.contains(10), "10 should be present");
    assert(!set.contains(20), "20 should not be present");
    assert(set.contains(30), "30 should be present");
    assert(set.contains(40), "40 should be present (even though it moved)");
    assert(set.live_count == 3, "Live count should be 3");

    // Remove 10 (first element)
    set.remove(10);
    assert(!set.contains(10), "10 should not be present");
    assert(set.contains(30), "30 should be present");
    assert(set.contains(40), "40 should be present");
    assert(set.live_count == 2, "Live count should be 2");

    // Verify iteration still works
    uint[] keys = set.get_live_keys();
    assert(keys.len == 2, "Should have 2 live keys");

    // Both 30 and 40 should be in the keys (order may vary due to swap)
    bool found_30 = false;
    bool found_40 = false;
    foreach (key : keys) {
        if (key == 30) found_30 = true;
        if (key == 40) found_40 = true;
    }
    assert(found_30, "30 should be in live keys");
    assert(found_40, "40 should be in live keys");

    set.release();
    print_test_pass("test_swap_and_pop_correctness");
}

// =============================================================================
// ITERATION TESTS
// =============================================================================

/**
 * Test get_live_keys returns empty slice for empty set.
 */
fn void test_get_live_keys_empty() {
    print_test_start("test_get_live_keys_empty");

    SparseSet set = create_sparse_set();

    uint[] keys = set.get_live_keys();
    assert(keys.len == 0, "Live keys should be empty for new set");

    set.release();
    print_test_pass("test_get_live_keys_empty");
}

/**
 * Test that all inserted elements are present in live keys.
 */
fn void test_get_live_keys_order() {
    print_test_start("test_get_live_keys_order");

    SparseSet set = create_sparse_set();

    // Insert several elements
    set.insert(7);
    set.insert(3);
    set.insert(15);
    set.insert(1);
    set.insert(9);

    uint[] keys = set.get_live_keys();
    assert(keys.len == 5, "Should have 5 live keys");

    // Verify all elements are present (order may be insertion order initially)
    bool[16] found;
    foreach (key : keys) {
        found[key] = true;
    }

    assert(found[7], "7 should be in live keys");
    assert(found[3], "3 should be in live keys");
    assert(found[15], "15 should be in live keys");
    assert(found[1], "1 should be in live keys");
    assert(found[9], "9 should be in live keys");

    set.release();
    print_test_pass("test_get_live_keys_order");
}

/**
 * Test iteration after removal returns correct elements.
 */
fn void test_iteration_after_removal() {
    print_test_start("test_iteration_after_removal");

    SparseSet set = create_sparse_set();

    set.insert(1);
    set.insert(2);
    set.insert(3);
    set.insert(4);
    set.insert(5);

    // Remove some elements
    set.remove(2);
    set.remove(4);

    uint[] keys = set.get_live_keys();
    assert(keys.len == 3, "Should have 3 live keys after removal");

    // Count occurrences and verify correct elements
    uint sum = 0;
    foreach (key : keys) {
        assert(key != 2, "Removed key 2 should not be in iteration");
        assert(key != 4, "Removed key 4 should not be in iteration");
        sum += key;
    }

    // 1 + 3 + 5 = 9
    assert(sum == 9, "Sum of remaining keys should be 9 (1+3+5)");

    set.release();
    print_test_pass("test_iteration_after_removal");
}

// =============================================================================
// STRESS TESTS
// =============================================================================

/**
 * Test many insertion and removal cycles.
 */
fn void test_insert_remove_cycle() {
    print_test_start("test_insert_remove_cycle");

    SparseSet set = create_sparse_set();

    // Insert 100 elements
    for (uint i = 0; i < 100; i++) {
        set.insert(i);
    }
    assert(set.live_count == 100, "Should have 100 elements");

    // Remove even numbers
    for (uint i = 0; i < 100; i += 2) {
        set.remove(i);
    }
    assert(set.live_count == 50, "Should have 50 elements after removing evens");

    // Verify odd numbers remain
    for (uint i = 0; i < 100; i++) {
        if (i % 2 == 0) {
            assert(!set.contains(i), "Even number should be removed");
        } else {
            assert(set.contains(i), "Odd number should remain");
        }
    }

    // Re-insert even numbers
    for (uint i = 0; i < 100; i += 2) {
        set.insert(i);
    }
    assert(set.live_count == 100, "Should have 100 elements again");

    // Remove all
    for (uint i = 0; i < 100; i++) {
        set.remove(i);
    }
    assert(set.live_count == 0, "Should have 0 elements after removing all");

    set.release();
    print_test_pass("test_insert_remove_cycle");
}

/**
 * Test with a very large sparse key.
 */
fn void test_large_sparse_key() {
    print_test_start("test_large_sparse_key");

    SparseSet set = create_sparse_set();

    // Insert key 10000 (large sparse key)
    set.insert(10000);
    assert(set.contains(10000), "Should contain 10000");
    assert(set.live_count == 1, "Live count should be 1");

    // Also insert a small key
    set.insert(5);
    assert(set.contains(5), "Should contain 5");
    assert(set.contains(10000), "Should still contain 10000");
    assert(set.live_count == 2, "Live count should be 2");

    // Remove large key
    set.remove(10000);
    assert(!set.contains(10000), "10000 should be removed");
    assert(set.contains(5), "5 should remain");
    assert(set.live_count == 1, "Live count should be 1");

    // Verify sparse array is large enough
    assert(set.key_position_index.len() > 10000, "Sparse array should be larger than 10000");

    set.release();
    print_test_pass("test_large_sparse_key");
}

// =============================================================================
// TEST RUNNER
// =============================================================================

/**
 * Run all SparseSet unit tests and print results.
 */
fn void run_sparse_set_tests() {
    io::printfn("\n");
    io::printfn("================================================================================");
    io::printfn("                      SPARSESET UNIT TEST SUITE");
    io::printfn("================================================================================");

    print_section("Basic Operations");
    test_insert_single();
    test_insert_multiple();
    test_insert_duplicate();
    test_remove_single();
    test_remove_nonexistent();
    test_contains_empty();
    test_contains_after_remove();

    print_section("Edge Cases");
    test_insert_zero();
    test_insert_large_key();
    test_remove_last_element();
    test_swap_and_pop_correctness();

    print_section("Iteration");
    test_get_live_keys_empty();
    test_get_live_keys_order();
    test_iteration_after_removal();

    print_section("Stress Tests");
    test_insert_remove_cycle();
    test_large_sparse_key();

    io::printfn("\n================================================================================");
    io::printfn("                      ALL SPARSESET TESTS PASSED!");
    io::printfn("================================================================================\n");

    // Chain to Region tests
    run_region_tests();
}
