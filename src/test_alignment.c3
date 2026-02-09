/**
 * =============================================================================
 * ALIGNMENT TESTS
 * =============================================================================
 *
 * TDD tests for P0 alignment issues:
 *
 * Issue 1: Inline storage alignment (src/main.c3:345-355)
 *   - char[16] inline_data may not be aligned for types like double or SIMD
 *
 * Issue 2: Arena bump allocator (src/main.c3:479-518)
 *   - Bump allocation doesn't respect type alignment requirements
 */
module main;

import std::io;

// =============================================================================
// TEST STRUCTURES
// =============================================================================

/**
 * AlignedStruct - Requires 16-byte alignment for SIMD operations.
 * Contains double[2] which itself requires 8-byte alignment, but we
 * explicitly require 16-byte alignment for SIMD compatibility.
 */
struct AlignedStruct @align(16) {
    double[2] data;  // Requires 16-byte alignment
}

/**
 * BigAlignedData - Large struct (>16 bytes) requiring 16-byte alignment.
 * This will be allocated from the arena, not inline storage.
 */
struct BigAlignedData @align(16) {
    char[32] data;
}

/**
 * DoubleWrapper - Simple wrapper for a double value.
 * Doubles require 8-byte alignment on most platforms.
 */
struct DoubleWrapper {
    double value;
}

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

/**
 * Check if a pointer is aligned to the specified boundary.
 */
fn bool is_aligned(void* ptr, usz alignment) {
    return (usz)ptr % alignment == 0;
}

/**
 * Get the alignment of a pointer.
 */
fn usz get_alignment(void* ptr) {
    usz addr = (usz)ptr;
    if (addr == 0) return 0;

    usz alignment = 1;
    while ((addr & alignment) == 0 && alignment < 4096) {
        alignment <<= 1;
    }
    return alignment;
}

// =============================================================================
// TEST: INLINE ALIGNMENT
// =============================================================================

/**
 * Test that inline storage is properly aligned for 16-byte aligned types.
 *
 * The inline_data field in PoolSlot stores small objects (<= 16 bytes).
 * When storing a 16-byte aligned type like AlignedStruct, the inline_data
 * must itself be 16-byte aligned.
 */
fn void test_inline_alignment() {
    io::printfn("\n=== TEST: Inline Storage Alignment ===");

    // Create a pool directly (simulating what Region does)
    Pool pool;

    // Create a 16-byte aligned struct (fits in inline storage)
    AlignedStruct s = { .data = {1.0, 2.0} };

    // Verify the source data is aligned
    io::printfn("Source AlignedStruct address: %p", &s);
    io::printfn("Source alignment: %d bytes", get_alignment(&s));
    assert(is_aligned(&s, 16), "Source struct not 16-byte aligned!");

    // Allocate in pool
    PoolId pid = pool.allocate(&s, AlignedStruct.sizeof, AlignedStruct.typeid);

    // Get the data pointer
    void* ptr = pool.get_data(pid);

    // Check alignment
    usz actual_alignment = get_alignment(ptr);
    io::printfn("Pool data address: %p", ptr);
    io::printfn("Pool data alignment: %d bytes", actual_alignment);

    // CRITICAL: This assertion will FAIL if inline_data is not properly aligned
    if (!is_aligned(ptr, 16)) {
        io::printfn("FAIL: Inline data not 16-byte aligned! (alignment: %d)", actual_alignment);
        io::printfn("  Expected: 16-byte alignment");
        io::printfn("  Actual: %d-byte alignment", actual_alignment);
        unreachable("Inline data not 16-byte aligned!");
    }

    // Verify data integrity (not corrupted by misalignment)
    AlignedStruct* result = (AlignedStruct*)ptr;
    io::printfn("Data integrity check: result.data[0] = %f, result.data[1] = %f",
                result.data[0], result.data[1]);

    assert(result.data[0] == 1.0, "Data corruption: data[0] != 1.0");
    assert(result.data[1] == 2.0, "Data corruption: data[1] != 2.0");

    io::printfn("PASS: Inline storage alignment test passed!");

    // Cleanup
    pool.destroy_all();
}

/**
 * Test that inline storage is properly aligned for double types.
 * Doubles require 8-byte alignment.
 */
fn void test_inline_double_alignment() {
    io::printfn("\n=== TEST: Inline Double Alignment ===");

    Pool pool;

    // Create a double wrapper (8 bytes, fits inline)
    DoubleWrapper dw = { .value = 3.14159265358979 };

    PoolId pid = pool.allocate(&dw, DoubleWrapper.sizeof, DoubleWrapper.typeid);
    void* ptr = pool.get_data(pid);

    usz actual_alignment = get_alignment(ptr);
    io::printfn("Double wrapper address: %p", ptr);
    io::printfn("Double wrapper alignment: %d bytes", actual_alignment);

    // Doubles require 8-byte alignment
    if (!is_aligned(ptr, 8)) {
        io::printfn("FAIL: Double not 8-byte aligned! (alignment: %d)", actual_alignment);
        unreachable("Double not 8-byte aligned!");
    }

    // Verify value integrity
    DoubleWrapper* result = (DoubleWrapper*)ptr;
    io::printfn("Data integrity check: value = %.15f", result.value);

    // Use epsilon comparison for floating point
    double diff = result.value - 3.14159265358979;
    if (diff < 0) diff = -diff;
    assert(diff < 0.0000001, "Data corruption: double value incorrect");

    io::printfn("PASS: Inline double alignment test passed!");

    pool.destroy_all();
}

// =============================================================================
// TEST: ARENA ALIGNMENT
// =============================================================================

/**
 * Test that arena allocation respects alignment requirements.
 *
 * Large objects (>16 bytes) are allocated from the arena.
 * The arena must respect the type's alignment requirements.
 */
fn void test_arena_alignment() {
    io::printfn("\n=== TEST: Arena Allocation Alignment ===");

    Pool pool;

    // Create a large aligned struct (>16 bytes, will use arena)
    BigAlignedData big = {};
    for (int i = 0; i < 32; i++) {
        big.data[i] = (char)(i + 65);  // Fill with 'A', 'B', 'C', ...
    }

    io::printfn("BigAlignedData size: %d bytes (threshold: %d)",
                BigAlignedData.sizeof, INLINE_THRESHOLD);
    assert(BigAlignedData.sizeof > INLINE_THRESHOLD, "Test struct should exceed inline threshold");

    PoolId pid = pool.allocate(&big, BigAlignedData.sizeof, BigAlignedData.typeid);
    void* ptr = pool.get_data(pid);

    usz actual_alignment = get_alignment(ptr);
    io::printfn("Arena data address: %p", ptr);
    io::printfn("Arena data alignment: %d bytes", actual_alignment);

    // CRITICAL: This assertion will FAIL if arena doesn't respect alignment
    if (!is_aligned(ptr, 16)) {
        io::printfn("FAIL: Arena data not 16-byte aligned! (alignment: %d)", actual_alignment);
        io::printfn("  Expected: 16-byte alignment");
        io::printfn("  Actual: %d-byte alignment", actual_alignment);
        unreachable("Arena data not 16-byte aligned!");
    }

    // Verify data integrity
    BigAlignedData* result = (BigAlignedData*)ptr;
    for (int i = 0; i < 32; i++) {
        if (result.data[i] != (char)(i + 65)) {
            io::printfn("FAIL: Data corruption at index %d", i);
            unreachable("Arena data corruption!");
        }
    }

    io::printfn("PASS: Arena allocation alignment test passed!");

    pool.destroy_all();
}

/**
 * Test multiple consecutive arena allocations maintain alignment.
 * This verifies that the bump pointer is properly aligned after each allocation.
 */
fn void test_arena_consecutive_allocations() {
    io::printfn("\n=== TEST: Consecutive Arena Allocations ===");

    Pool pool;

    // Allocate several objects with different sizes and alignments
    // to stress-test the bump allocator

    // First: 17-byte object (odd size, just over threshold)
    char[17] small_arena;
    for (int i = 0; i < 17; i++) small_arena[i] = (char)i;
    PoolId pid1 = pool.allocate(&small_arena, 17, char.typeid);
    void* ptr1 = pool.get_data(pid1);
    io::printfn("Allocation 1 (17 bytes): %p, alignment: %d", ptr1, get_alignment(ptr1));

    // Second: 32-byte aligned object
    BigAlignedData aligned1 = {};
    PoolId pid2 = pool.allocate(&aligned1, BigAlignedData.sizeof, BigAlignedData.typeid);
    void* ptr2 = pool.get_data(pid2);
    io::printfn("Allocation 2 (32 bytes @align(16)): %p, alignment: %d", ptr2, get_alignment(ptr2));

    // Third: Another odd-sized object
    char[23] medium_arena;
    PoolId pid3 = pool.allocate(&medium_arena, 23, char.typeid);
    void* ptr3 = pool.get_data(pid3);
    io::printfn("Allocation 3 (23 bytes): %p, alignment: %d", ptr3, get_alignment(ptr3));

    // Fourth: Another aligned object - this MUST be 16-byte aligned
    BigAlignedData aligned2 = {};
    PoolId pid4 = pool.allocate(&aligned2, BigAlignedData.sizeof, BigAlignedData.typeid);
    void* ptr4 = pool.get_data(pid4);
    io::printfn("Allocation 4 (32 bytes @align(16)): %p, alignment: %d", ptr4, get_alignment(ptr4));

    // Check that aligned objects are actually aligned
    if (!is_aligned(ptr2, 16)) {
        io::printfn("FAIL: Second allocation not 16-byte aligned!");
        unreachable("Arena consecutive allocation alignment failure!");
    }

    if (!is_aligned(ptr4, 16)) {
        io::printfn("FAIL: Fourth allocation not 16-byte aligned!");
        unreachable("Arena consecutive allocation alignment failure!");
    }

    io::printfn("PASS: Consecutive arena allocations maintain alignment!");

    pool.destroy_all();
}

// =============================================================================
// TEST: INTEGRATION WITH REGION
// =============================================================================

/**
 * Test alignment through the full Region allocation path.
 */
fn void test_region_aligned_allocation() {
    io::printfn("\n=== TEST: Region Aligned Allocation ===");

    RegionRegistry registry;
    registry.init();

    RegionHandle region_handle = registry.create_region(INVALID_REGION_HANDLE);
    Region* region = &registry.region_storage[(usz)(uint)region_handle.region_id];

    // Allocate a 16-byte aligned struct through the region
    AlignedStruct s = { .data = {42.0, 84.0} };
    ObjectHandle handle = region.allocate_typed(AlignedStruct, s);

    // Dereference and check alignment
    void* ptr = registry.dereference(handle);

    usz actual_alignment = get_alignment(ptr);
    io::printfn("Region-allocated data address: %p", ptr);
    io::printfn("Region-allocated data alignment: %d bytes", actual_alignment);

    if (!is_aligned(ptr, 16)) {
        io::printfn("FAIL: Region-allocated data not 16-byte aligned!");
        unreachable("Region allocation alignment failure!");
    }

    // Verify data
    AlignedStruct* result = (AlignedStruct*)ptr;
    assert(result.data[0] == 42.0, "Data corruption in region allocation");
    assert(result.data[1] == 84.0, "Data corruption in region allocation");

    io::printfn("PASS: Region aligned allocation test passed!");

    // Cleanup
    registry.release_region(region_handle);
}

// =============================================================================
// TEST RUNNER
// =============================================================================

/**
 * Run all alignment tests.
 */
fn void run_alignment_tests() {
    io::printfn("========================================");
    io::printfn("RUNNING ALIGNMENT TESTS");
    io::printfn("========================================");
    io::printfn("INLINE_THRESHOLD = %d bytes", INLINE_THRESHOLD);
    io::printfn("AlignedStruct.sizeof = %d bytes", AlignedStruct.sizeof);
    io::printfn("BigAlignedData.sizeof = %d bytes", BigAlignedData.sizeof);
    io::printfn("DoubleWrapper.sizeof = %d bytes", DoubleWrapper.sizeof);

    // Run inline storage tests
    test_inline_alignment();
    test_inline_double_alignment();

    // Run arena allocation tests
    test_arena_alignment();
    test_arena_consecutive_allocations();

    // Run integration tests
    test_region_aligned_allocation();

    io::printfn("\n========================================");
    io::printfn("ALL ALIGNMENT TESTS PASSED!");
    io::printfn("========================================");
}
