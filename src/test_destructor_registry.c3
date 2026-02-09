/**
 * =============================================================================
 * DESTRUCTOR REGISTRY TESTS
 * =============================================================================
 *
 * TDD tests for DestructorRegistry O(1) lookup implementation.
 * These tests are written first (red phase), then the implementation is
 * updated to make them pass (green phase).
 *
 * Run with: run_destructor_registry_tests()
 */
module main;

import std::io;

// =============================================================================
// TEST HELPER FUNCTIONS
// =============================================================================

/**
 * Simple assertion helper that prints pass/fail status.
 */
fn void dr_assert_true(bool condition, String test_name) {
    if (condition) {
        io::printfn("  [PASS] %s", test_name);
    } else {
        io::printfn("  [FAIL] %s", test_name);
        unreachable("Test assertion failed");
    }
}

fn void dr_assert_eq(usz expected, usz actual, String test_name) {
    if (expected == actual) {
        io::printfn("  [PASS] %s", test_name);
    } else {
        io::printfn("  [FAIL] %s: expected %d, got %d", test_name, expected, actual);
        unreachable("Test assertion failed");
    }
}

fn void dr_print_test_start(String test_name) {
    io::printfn("  [TEST] %s...", test_name);
}

// =============================================================================
// TEST DESTRUCTORS
// =============================================================================

// Global counters to verify destructor calls
int g_dr_destructor_a_calls = 0;
int g_dr_destructor_b_calls = 0;
int g_dr_destructor_c_calls = 0;

fn void dr_destructor_a(void* ptr) {
    g_dr_destructor_a_calls++;
}

fn void dr_destructor_b(void* ptr) {
    g_dr_destructor_b_calls++;
}

fn void dr_destructor_c(void* ptr) {
    g_dr_destructor_c_calls++;
}

// =============================================================================
// TEST CASES
// =============================================================================

/**
 * Test: Register and find a destructor.
 * Verifies basic register/find functionality.
 */
fn void test_dr_register_and_find() {
    dr_print_test_start("register_and_find");

    DestructorRegistry reg;
    reg.init();

    reg.register(int.typeid, &dr_destructor_a);

    bool has_value = false;
    DestructorFn actual_func;
    if (try f = reg.find(int.typeid)) {
        has_value = true;
        actual_func = f;
    }
    dr_assert_true(has_value, "find returns non-null for registered type");
    dr_assert_true(actual_func == &dr_destructor_a, "find returns correct destructor function");

    reg.release();
}

/**
 * Test: Find returns null for unregistered type.
 */
fn void test_dr_find_not_registered() {
    dr_print_test_start("find_not_registered");

    DestructorRegistry reg;
    reg.init();

    bool has_value = false;
    if (try f = reg.find(float.typeid)) {
        has_value = true;
    }
    dr_assert_true(!has_value, "find returns null for unregistered type");

    reg.release();
}

/**
 * Test: Multiple registrations with different types.
 */
fn void test_dr_multiple_registrations() {
    dr_print_test_start("multiple_registrations");

    DestructorRegistry reg;
    reg.init();

    reg.register(int.typeid, &dr_destructor_a);
    reg.register(float.typeid, &dr_destructor_b);
    reg.register(double.typeid, &dr_destructor_c);

    bool has_int = false;
    bool has_float = false;
    bool has_double = false;
    DestructorFn func_int;
    DestructorFn func_float;
    DestructorFn func_double;

    if (try f = reg.find(int.typeid)) { has_int = true; func_int = f; }
    if (try f = reg.find(float.typeid)) { has_float = true; func_float = f; }
    if (try f = reg.find(double.typeid)) { has_double = true; func_double = f; }

    dr_assert_true(has_int, "find int after multiple registrations");
    dr_assert_true(has_float, "find float after multiple registrations");
    dr_assert_true(has_double, "find double after multiple registrations");

    dr_assert_true(func_int == &dr_destructor_a, "int maps to destructor_a");
    dr_assert_true(func_float == &dr_destructor_b, "float maps to destructor_b");
    dr_assert_true(func_double == &dr_destructor_c, "double maps to destructor_c");

    reg.release();
}

/**
 * Test: Many registrations (stress test for O(1) lookup).
 * Creates a struct for each "type" to simulate many typeids.
 */
struct DrTestType0 { int x; }
struct DrTestType1 { int x; }
struct DrTestType2 { int x; }
struct DrTestType3 { int x; }
struct DrTestType4 { int x; }
struct DrTestType5 { int x; }
struct DrTestType6 { int x; }
struct DrTestType7 { int x; }
struct DrTestType8 { int x; }
struct DrTestType9 { int x; }

fn void test_dr_many_registrations() {
    dr_print_test_start("many_registrations");

    DestructorRegistry reg;
    reg.init();

    // Register 10 different types
    reg.register(DrTestType0.typeid, &dr_destructor_a);
    reg.register(DrTestType1.typeid, &dr_destructor_a);
    reg.register(DrTestType2.typeid, &dr_destructor_a);
    reg.register(DrTestType3.typeid, &dr_destructor_a);
    reg.register(DrTestType4.typeid, &dr_destructor_a);
    reg.register(DrTestType5.typeid, &dr_destructor_a);
    reg.register(DrTestType6.typeid, &dr_destructor_a);
    reg.register(DrTestType7.typeid, &dr_destructor_a);
    reg.register(DrTestType8.typeid, &dr_destructor_a);
    reg.register(DrTestType9.typeid, &dr_destructor_b);

    // Verify lookup for last type
    bool has_value = false;
    DestructorFn func;
    if (try f = reg.find(DrTestType9.typeid)) { has_value = true; func = f; }
    dr_assert_true(has_value, "find last registered type");
    dr_assert_true(func == &dr_destructor_b, "last type maps to destructor_b");

    // Verify first type still works
    bool has_first = false;
    DestructorFn func_first;
    if (try f = reg.find(DrTestType0.typeid)) { has_first = true; func_first = f; }
    dr_assert_true(has_first, "find first registered type after many");
    dr_assert_true(func_first == &dr_destructor_a, "first type still maps to destructor_a");

    reg.release();
}

/**
 * Test: Lookup performance verification.
 * This test verifies that lookup is O(1) by checking it works correctly
 * with many registered types. True timing would require benchmarking infrastructure.
 */
fn void test_dr_lookup_performance() {
    dr_print_test_start("lookup_performance");

    DestructorRegistry reg;
    reg.init();

    // Register built-in types
    reg.register(int.typeid, &dr_destructor_a);
    reg.register(uint.typeid, &dr_destructor_a);
    reg.register(long.typeid, &dr_destructor_a);
    reg.register(ulong.typeid, &dr_destructor_a);
    reg.register(short.typeid, &dr_destructor_a);
    reg.register(ushort.typeid, &dr_destructor_a);
    reg.register(char.typeid, &dr_destructor_a);
    reg.register(float.typeid, &dr_destructor_a);
    reg.register(double.typeid, &dr_destructor_a);
    reg.register(bool.typeid, &dr_destructor_b);

    // Lookup should be O(1), not O(n)
    // With O(1), looking up the last registered type should be just as fast
    bool has_value = false;
    DestructorFn func;
    if (try f = reg.find(bool.typeid)) { has_value = true; func = f; }
    dr_assert_true(has_value, "O(1) lookup finds last registered type");
    dr_assert_true(func == &dr_destructor_b, "O(1) lookup returns correct function");

    // Verify unregistered type returns null quickly
    bool has_not_found = false;
    if (try f = reg.find(ichar.typeid)) { has_not_found = true; }
    dr_assert_true(!has_not_found, "O(1) lookup returns null for unregistered");

    reg.release();
}

/**
 * Test: Overwrite registration for same type.
 */
fn void test_dr_overwrite_registration() {
    dr_print_test_start("overwrite_registration");

    DestructorRegistry reg;
    reg.init();

    reg.register(int.typeid, &dr_destructor_a);
    reg.register(int.typeid, &dr_destructor_b);  // Overwrite

    bool has_value = false;
    DestructorFn func;
    if (try f = reg.find(int.typeid)) { has_value = true; func = f; }
    dr_assert_true(has_value, "find after overwrite returns non-null");
    // After overwrite, should return the new destructor
    dr_assert_true(func == &dr_destructor_b, "overwrite replaces with new destructor");

    reg.release();
}

/**
 * Test: Destructor is actually callable.
 */
fn void test_dr_destructor_callable() {
    dr_print_test_start("destructor_callable");

    g_dr_destructor_a_calls = 0;

    DestructorRegistry reg;
    reg.init();

    reg.register(int.typeid, &dr_destructor_a);

    if (try f = reg.find(int.typeid)) {
        int dummy = 42;
        f(&dummy);
    }

    dr_assert_eq(1, (usz)g_dr_destructor_a_calls, "destructor was called once");

    reg.release();
}

// =============================================================================
// TEST RUNNER
// =============================================================================

fn void run_destructor_registry_tests() {
    io::printfn("\n=== DestructorRegistry O(1) Tests ===");

    test_dr_register_and_find();
    test_dr_find_not_registered();
    test_dr_multiple_registrations();
    test_dr_many_registrations();
    test_dr_lookup_performance();
    test_dr_overwrite_registration();
    test_dr_destructor_callable();

    io::printfn("\n=== All DestructorRegistry Tests Passed ===");
}
