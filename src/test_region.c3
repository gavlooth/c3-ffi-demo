/**
 * =============================================================================
 * UNIT TESTS FOR REGION (Isolated Testing)
 * =============================================================================
 *
 * Comprehensive test suite covering Region in isolation:
 *
 * 1. Typed Allocation
 *    - region_test_allocate_int
 *    - region_test_allocate_struct
 *    - region_test_allocate_returns_valid_handle
 *    - region_test_live_object_count_incremented
 *
 * 2. Raw Allocation
 *    - region_test_allocate_raw_bytes
 *    - region_test_allocate_raw_type_id
 *
 * 3. Object Deallocation
 *    - region_test_free_object_live
 *    - region_test_free_object_decrements_count
 *    - region_test_free_object_forwarded
 *    - region_test_free_object_dead_noop
 *
 * 4. Destructor Integration
 *    - region_test_destructor_called_on_free
 *    - region_test_destructor_not_called_if_not_registered
 *
 * 5. Handle Validity After Operations
 *    - region_test_handle_valid_after_alloc
 *    - region_test_handle_invalid_after_free
 *
 * 6. Multiple Allocations
 *    - region_test_multiple_allocations
 *    - region_test_interleaved_alloc_free
 *
 * Note: These tests exercise Region in isolation. RegionRegistry tests
 * will cover promotion and ghost table scenarios.
 *
 * Run with: run_region_tests()
 */
module main;

import std::io;

// =============================================================================
// TEST INFRASTRUCTURE
// =============================================================================

fn void print_region_test_start(String test_name) {
    io::printfn("  [TEST] %s...", test_name);
}

fn void print_region_test_pass(String test_name) {
    io::printfn("  [PASS] %s", test_name);
}

fn void print_region_section(String section_name) {
    io::printfn("\n=== %s ===", section_name);
}

// =============================================================================
// TEST STRUCTURES
// =============================================================================

/**
 * Simple struct for testing typed allocation.
 */
struct RegionTestPoint {
    int x;
    int y;
}

/**
 * Larger struct for testing allocation.
 */
struct RegionTestData {
    int id;
    double value;
    char[8] name;
}

// =============================================================================
// DESTRUCTOR TRACKING
// =============================================================================

// Global counters to verify destructor calls
int g_region_test_destructor_calls = 0;
int g_region_test_destructor_last_value = 0;

/**
 * Test destructor that tracks calls.
 */
fn void region_test_destructor(void* ptr) {
    g_region_test_destructor_calls++;
    // Capture the value for verification
    int* val = (int*)ptr;
    g_region_test_destructor_last_value = *val;
}

/**
 * Reset destructor tracking counters.
 */
fn void region_reset_destructor_tracking() {
    g_region_test_destructor_calls = 0;
    g_region_test_destructor_last_value = 0;
}

// =============================================================================
// HELPER: Create an isolated Region for testing
// =============================================================================

/**
 * Create a fresh Region for testing with a given ID.
 * The region is fully initialized but not part of a registry.
 */
fn Region region_create_test_region(RegionId id, Generation gen) {
    Region region;
    region.id = id;
    region.generation = gen;
    region.refcount = (RefCount)1;
    region.parent = NO_PARENT;
    region.live_object_count = 0;

    // Initialize Pool's slot_count to 0 to prevent destroy_all from iterating garbage
    region.pool.slot_count = 0;

    return region;
}

/**
 * Cleanup a test region.
 */
fn void region_cleanup_test_region(Region* region) {
    region.destroy_internals();
}

// =============================================================================
// SECTION 1: TYPED ALLOCATION TESTS
// =============================================================================

/**
 * region_test_allocate_int - Allocate an int and verify the value is stored correctly.
 */
fn void region_test_allocate_int() {
    print_region_test_start("region_test_allocate_int");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    int value = 42;
    ObjectHandle handle = region.allocate_typed(int, value);

    // Verify the value is stored correctly
    ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
    assert(!record.forwarded, "Record should not be forwarded after allocation");

    void* data = region.pool.get_data(record.live_info.pool_id);
    int* stored = (int*)data;
    assert(*stored == 42, "Stored value should be 42");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_allocate_int");
}

/**
 * region_test_allocate_struct - Allocate a struct and verify all fields are correct.
 */
fn void region_test_allocate_struct() {
    print_region_test_start("region_test_allocate_struct");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    RegionTestPoint point = { .x = 10, .y = 20 };
    ObjectHandle handle = region.allocate_typed(RegionTestPoint, point);

    // Verify the struct is stored correctly
    ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
    assert(!record.forwarded, "Record should not be forwarded after allocation");

    void* data = region.pool.get_data(record.live_info.pool_id);
    RegionTestPoint* stored = (RegionTestPoint*)data;
    assert(stored.x == 10, "Stored x should be 10");
    assert(stored.y == 20, "Stored y should be 20");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_allocate_struct");
}

/**
 * region_test_allocate_returns_valid_handle - Verify handle fields are set correctly.
 */
fn void region_test_allocate_returns_valid_handle() {
    print_region_test_start("region_test_allocate_returns_valid_handle");

    Region region = region_create_test_region((RegionId)5, (Generation)3);

    int value = 100;
    ObjectHandle handle = region.allocate_typed(int, value);

    // Verify handle fields
    assert((uint)handle.region_id == 5, "Handle region_id should match region ID");
    assert((uint)handle.generation > 0, "Handle generation should be > 0");

    // Verify generation matches the slot's generation
    ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
    assert(record.generation == handle.generation, "Handle generation should match slot generation");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_allocate_returns_valid_handle");
}

/**
 * region_test_live_object_count_incremented - Verify live_object_count tracks allocations.
 */
fn void region_test_live_object_count_incremented() {
    print_region_test_start("region_test_live_object_count_incremented");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    assert(region.live_object_count == 0, "Initial live_object_count should be 0");

    int v1 = 1;
    region.allocate_typed(int, v1);
    assert(region.live_object_count == 1, "live_object_count should be 1 after first alloc");

    int v2 = 2;
    region.allocate_typed(int, v2);
    assert(region.live_object_count == 2, "live_object_count should be 2 after second alloc");

    int v3 = 3;
    region.allocate_typed(int, v3);
    assert(region.live_object_count == 3, "live_object_count should be 3 after third alloc");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_live_object_count_incremented");
}

// =============================================================================
// SECTION 2: RAW ALLOCATION TESTS
// =============================================================================

/**
 * region_test_allocate_raw_bytes - Verify raw data is stored correctly.
 */
fn void region_test_allocate_raw_bytes() {
    print_region_test_start("region_test_allocate_raw_bytes");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    // Create some raw data
    char[8] raw_data = { 'H', 'E', 'L', 'L', 'O', '!', '!', '!' };
    ObjectHandle handle = region.allocate_raw(&raw_data, 8, char.typeid);

    // Verify the raw data is stored correctly
    ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
    assert(!record.forwarded, "Record should not be forwarded after raw allocation");
    assert(record.live_info.size == 8, "Size should be 8 bytes");

    void* data = region.pool.get_data(record.live_info.pool_id);
    char* stored = (char*)data;
    assert(stored[0] == 'H', "First byte should be 'H'");
    assert(stored[4] == 'O', "Fifth byte should be 'O'");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_allocate_raw_bytes");
}

/**
 * region_test_allocate_raw_type_id - Verify typeid is preserved in raw allocation.
 */
fn void region_test_allocate_raw_type_id() {
    print_region_test_start("region_test_allocate_raw_type_id");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    RegionTestData test_data = { .id = 42, .value = 3.14, .name = {'T', 'E', 'S', 'T', 0, 0, 0, 0} };
    ObjectHandle handle = region.allocate_raw(&test_data, RegionTestData.sizeof, RegionTestData.typeid);

    // Verify typeid is preserved
    ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
    assert(record.live_info.type_id == RegionTestData.typeid, "Type ID should be preserved");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_allocate_raw_type_id");
}

// =============================================================================
// SECTION 3: OBJECT DEALLOCATION TESTS
// =============================================================================

/**
 * region_test_free_object_live - Verify LIVE object is properly freed.
 */
fn void region_test_free_object_live() {
    print_region_test_start("region_test_free_object_live");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    int value = 999;
    ObjectHandle handle = region.allocate_typed(int, value);

    // Verify object is LIVE
    ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
    assert(!record.forwarded, "Object should not be forwarded before free");

    // Free the object
    region.free_object(handle);

    // After free, generation is bumped (invalidating old handles)
    // Note: We need to get the record again as the slot was released
    ObjectRecord* after_record = region.slot_table.get_record(handle.slot_id);
    assert((uint)after_record.generation != (uint)handle.generation, "Generation should be bumped after free");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_free_object_live");
}

/**
 * region_test_free_object_decrements_count - Verify live_object_count is decremented.
 */
fn void region_test_free_object_decrements_count() {
    print_region_test_start("region_test_free_object_decrements_count");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    int v1 = 1;
    int v2 = 2;
    ObjectHandle h1 = region.allocate_typed(int, v1);
    ObjectHandle h2 = region.allocate_typed(int, v2);

    assert(region.live_object_count == 2, "live_object_count should be 2");

    region.free_object(h1);
    assert(region.live_object_count == 1, "live_object_count should be 1 after first free");

    region.free_object(h2);
    assert(region.live_object_count == 0, "live_object_count should be 0 after second free");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_free_object_decrements_count");
}

/**
 * region_test_free_object_forwarded - Verify FORWARDED stub is cleaned up.
 */
fn void region_test_free_object_forwarded() {
    print_region_test_start("region_test_free_object_forwarded");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    int value = 42;
    ObjectHandle handle = region.allocate_typed(int, value);
    uint initial_count = region.live_object_count;

    // Manually set the record to forwarded to simulate promotion
    ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
    record.forwarded = true;
    record.forward_target = INVALID_OBJECT_HANDLE;  // Doesn't matter for this test

    // Free the forwarded object
    region.free_object(handle);

    // The slot should be released (generation bumped, invalidating old handles)
    ObjectRecord* after_record = region.slot_table.get_record(handle.slot_id);
    assert((uint)after_record.generation != (uint)handle.generation, "Generation should be bumped after free");

    // Note: live_object_count was already incremented in allocate, but since
    // we manually changed to FORWARDED, the count wasn't adjusted. In real
    // promotion, the count would be handled by the promotion logic.
    // For this test, we just verify the slot is properly released.

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_free_object_forwarded");
}

/**
 * region_test_free_object_dead_noop - Verify DEAD object free is a no-op.
 */
fn void region_test_free_object_dead_noop() {
    print_region_test_start("region_test_free_object_dead_noop");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    int value = 42;
    ObjectHandle handle = region.allocate_typed(int, value);

    // Free the object
    region.free_object(handle);

    uint count_after_first_free = region.live_object_count;

    // The handle's generation now mismatches the slot's generation.
    // So the old handle is now stale.
    ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
    assert((uint)record.generation != (uint)handle.generation, "Generation should be bumped after first free");

    // Freeing with stale handle should be a no-op (generation mismatch)
    region.free_object(handle);

    // Count should remain unchanged
    assert(region.live_object_count == count_after_first_free,
           "live_object_count should not change when freeing DEAD object");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_free_object_dead_noop");
}

// =============================================================================
// SECTION 4: DESTRUCTOR INTEGRATION TESTS
// =============================================================================

/**
 * Struct with a destructor for testing.
 */
struct RegionDestructibleInt {
    int value;
}

/**
 * region_test_destructor_called_on_free - Verify destructor is called when freeing.
 */
fn void region_test_destructor_called_on_free() {
    print_region_test_start("region_test_destructor_called_on_free");

    // Reset tracking
    region_reset_destructor_tracking();

    // Register destructor for RegionDestructibleInt
    g_destructor_registry.register(RegionDestructibleInt.typeid, &region_test_destructor);

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    RegionDestructibleInt obj = { .value = 12345 };
    ObjectHandle handle = region.allocate_typed(RegionDestructibleInt, obj);

    assert(g_region_test_destructor_calls == 0, "Destructor should not be called on alloc");

    // Free the object - should call destructor
    region.free_object(handle);

    assert(g_region_test_destructor_calls == 1, "Destructor should be called once on free");
    assert(g_region_test_destructor_last_value == 12345,
           "Destructor should receive correct value");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_destructor_called_on_free");
}

/**
 * region_test_destructor_not_called_if_not_registered - Verify no crash when no destructor.
 */
fn void region_test_destructor_not_called_if_not_registered() {
    print_region_test_start("region_test_destructor_not_called_if_not_registered");

    // Reset tracking
    region_reset_destructor_tracking();

    // Note: We're using RegionTestPoint which has no registered destructor
    Region region = region_create_test_region((RegionId)1, (Generation)1);

    RegionTestPoint point = { .x = 100, .y = 200 };
    ObjectHandle handle = region.allocate_typed(RegionTestPoint, point);

    // Free the object - should not crash even without destructor
    region.free_object(handle);

    // The global destructor counter should still be 0 (RegionTestPoint has no destructor)
    // This just verifies no crash occurred
    assert(g_region_test_destructor_calls == 0,
           "Destructor should not be called for unregistered type");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_destructor_not_called_if_not_registered");
}

// =============================================================================
// SECTION 5: HANDLE VALIDITY TESTS
// =============================================================================

/**
 * region_test_handle_valid_after_alloc - Verify handle can be used to access data.
 */
fn void region_test_handle_valid_after_alloc() {
    print_region_test_start("region_test_handle_valid_after_alloc");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    int value = 777;
    ObjectHandle handle = region.allocate_typed(int, value);

    // Verify we can access the data through the handle
    ObjectRecord* record = region.slot_table.get_record(handle.slot_id);

    // Generation should match
    assert((uint)record.generation == (uint)handle.generation,
           "Handle generation should match slot generation");

    // Object should not be forwarded (i.e., is live)
    assert(!record.forwarded, "Object should not be forwarded");

    // Data should be accessible and correct
    void* data = region.pool.get_data(record.live_info.pool_id);
    int* stored = (int*)data;
    assert(*stored == 777, "Data should be accessible and correct");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_handle_valid_after_alloc");
}

/**
 * region_test_handle_invalid_after_free - Verify handle generation mismatches after free.
 */
fn void region_test_handle_invalid_after_free() {
    print_region_test_start("region_test_handle_invalid_after_free");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    int value = 888;
    ObjectHandle handle = region.allocate_typed(int, value);

    Generation gen_before = handle.generation;

    // Free the object
    region.free_object(handle);

    // Get the slot's new generation
    ObjectRecord* record = region.slot_table.get_record(handle.slot_id);
    Generation gen_after = record.generation;

    // Generation should have been bumped, invalidating the handle
    assert((uint)gen_after != (uint)gen_before,
           "Slot generation should be bumped after free, invalidating old handle");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_handle_invalid_after_free");
}

// =============================================================================
// SECTION 6: MULTIPLE ALLOCATIONS TESTS
// =============================================================================

/**
 * region_test_multiple_allocations - Verify many objects can coexist.
 */
fn void region_test_multiple_allocations() {
    print_region_test_start("region_test_multiple_allocations");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    const int NUM_OBJECTS = 50;
    ObjectHandle[50] handles;

    // Allocate many objects
    for (int i = 0; i < NUM_OBJECTS; i++) {
        int value = i * 10;
        handles[i] = region.allocate_typed(int, value);
    }

    assert(region.live_object_count == NUM_OBJECTS,
           "live_object_count should equal number of allocations");

    // Verify all objects are accessible and have correct values
    for (int i = 0; i < NUM_OBJECTS; i++) {
        ObjectRecord* record = region.slot_table.get_record(handles[i].slot_id);
        assert(!record.forwarded, "All objects should not be forwarded");
        assert((uint)record.generation == (uint)handles[i].generation, "Generations should match");

        void* data = region.pool.get_data(record.live_info.pool_id);
        int* stored = (int*)data;
        assert(*stored == i * 10, "Each object should have correct value");
    }

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_multiple_allocations");
}

/**
 * region_test_interleaved_alloc_free - Verify mixed alloc/free operations work correctly.
 */
fn void region_test_interleaved_alloc_free() {
    print_region_test_start("region_test_interleaved_alloc_free");

    Region region = region_create_test_region((RegionId)1, (Generation)1);

    // Allocate 3 objects
    int v1 = 100;
    int v2 = 200;
    int v3 = 300;
    ObjectHandle h1 = region.allocate_typed(int, v1);
    ObjectHandle h2 = region.allocate_typed(int, v2);
    ObjectHandle h3 = region.allocate_typed(int, v3);

    assert(region.live_object_count == 3, "Should have 3 objects");

    // Free the middle one
    region.free_object(h2);
    assert(region.live_object_count == 2, "Should have 2 objects after free");

    // Allocate a new object - may reuse h2's slot
    int v4 = 400;
    ObjectHandle h4 = region.allocate_typed(int, v4);
    assert(region.live_object_count == 3, "Should have 3 objects again");

    // Verify remaining objects
    ObjectRecord* r1 = region.slot_table.get_record(h1.slot_id);
    assert(!r1.forwarded && (uint)r1.generation == (uint)h1.generation, "h1 should still be valid");

    ObjectRecord* r3 = region.slot_table.get_record(h3.slot_id);
    assert(!r3.forwarded && (uint)r3.generation == (uint)h3.generation, "h3 should still be valid");

    ObjectRecord* r4 = region.slot_table.get_record(h4.slot_id);
    assert(!r4.forwarded && (uint)r4.generation == (uint)h4.generation, "h4 should be valid");

    // h2's old generation should no longer match if slot was reused
    ObjectRecord* r2_slot = region.slot_table.get_record(h2.slot_id);
    if ((uint)h4.slot_id == (uint)h2.slot_id) {
        // Slot was reused
        assert((uint)r2_slot.generation != (uint)h2.generation,
               "Old handle generation should not match reused slot");
    } else {
        // Slot was not reused (generation was bumped, invalidating old handle)
        assert((uint)r2_slot.generation != (uint)h2.generation, "Old handle generation should not match");
    }

    // Verify values
    void* d1 = region.pool.get_data(r1.live_info.pool_id);
    assert(*(int*)d1 == 100, "h1 value should be 100");

    void* d3 = region.pool.get_data(r3.live_info.pool_id);
    assert(*(int*)d3 == 300, "h3 value should be 300");

    void* d4 = region.pool.get_data(r4.live_info.pool_id);
    assert(*(int*)d4 == 400, "h4 value should be 400");

    // Free all remaining
    region.free_object(h1);
    region.free_object(h3);
    region.free_object(h4);

    assert(region.live_object_count == 0, "Should have 0 objects after freeing all");

    region_cleanup_test_region(&region);
    print_region_test_pass("region_test_interleaved_alloc_free");
}

// =============================================================================
// TEST RUNNER
// =============================================================================

/**
 * Run all Region unit tests and print results.
 */
fn void run_region_tests() {
    io::printfn("\n");
    io::printfn("================================================================================");
    io::printfn("                         REGION UNIT TEST SUITE");
    io::printfn("================================================================================");

    print_region_section("1. Typed Allocation");
    region_test_allocate_int();
    region_test_allocate_struct();
    region_test_allocate_returns_valid_handle();
    region_test_live_object_count_incremented();

    print_region_section("2. Raw Allocation");
    region_test_allocate_raw_bytes();
    region_test_allocate_raw_type_id();

    print_region_section("3. Object Deallocation");
    region_test_free_object_live();
    region_test_free_object_decrements_count();
    region_test_free_object_forwarded();
    region_test_free_object_dead_noop();

    print_region_section("4. Destructor Integration");
    region_test_destructor_called_on_free();
    region_test_destructor_not_called_if_not_registered();

    print_region_section("5. Handle Validity After Operations");
    region_test_handle_valid_after_alloc();
    region_test_handle_invalid_after_free();

    print_region_section("6. Multiple Allocations");
    region_test_multiple_allocations();
    region_test_interleaved_alloc_free();

    io::printfn("\n================================================================================");
    io::printfn("                       ALL REGION TESTS PASSED!");
    io::printfn("================================================================================\n");

    // Chain to alignment tests
    run_alignment_tests();
}
