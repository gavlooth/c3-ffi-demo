# Region Memory System v3 - TODO

A comprehensive roadmap of planned improvements, bug fixes, and enhancements for the Region Memory System.

---

## P0: Critical Bug Fixes

High-priority issues that could cause incorrect behavior.

### Arena Free-List Bug

- [ ] **P0** Fix arena free-list using wrong arena index
  - **Location**: `Pool.arena_free()` at `src/main.c3:532-547`
  - **Issue**: When freeing arena memory, the arena index lookup may return incorrect index if arenas were compacted
  - **Impact**: Memory corruption, use-after-free potential
  - **Fix**: Store arena index in PoolSlot or use stable arena identifiers
  - **Dependencies**: None

### Memory Alignment Issues

- [ ] **P0** Add explicit alignment for inline storage
  - **Location**: `PoolSlot.inline_data` at `src/main.c3:345-355`
  - **Issue**: `char[16]` may not provide correct alignment for all types (e.g., `double`, SIMD types)
  - **Impact**: Potential alignment faults on strict architectures, performance degradation on x86
  - **Fix**: Use `alignas(16)` or platform max alignment, or align to `max_align_t`
  - **Dependencies**: None

- [ ] **P0** Ensure arena bump allocator respects alignment requirements
  - **Location**: `Pool.arena_allocate()` at `src/main.c3:479-518`
  - **Issue**: Bump pointer allocation doesn't account for type alignment
  - **Impact**: Misaligned access for types requiring > 1-byte alignment
  - **Fix**: Round up `arena.used` to required alignment before allocation
  - **Dependencies**: None

---

## P1: Performance Improvements

Optimizations that significantly improve runtime performance.

### O(1) Ghost Table Lookup

- [ ] **P1** Replace linear ghost table search with hash map
  - **Location**: `RegionRegistry.dereference_via_ghost()` at `src/main.c3:1728-1761`
  - **Current**: O(n) linear scan of `ghost_source_ids`
  - **Target**: O(1) average case with hash map `RegionId -> GhostTableIndex`
  - **Implementation Notes**:
    - Use open addressing with linear probing for cache locality
    - Key: `(source_region_id, source_generation)` tuple
    - Rehash when load factor > 0.75
  - **Dependencies**: None

### O(1) Destructor Registry

- [ ] **P1** Replace linear destructor lookup with hash map or type indexing
  - **Location**: `DestructorRegistry.find()` at `src/main.c3:1189`
  - **Current**: O(n) linear scan of `registered_type_ids`
  - **Target**: O(1) lookup
  - **Implementation Options**:
    1. Hash map: `typeid -> DestructorFn`
    2. Type indexing: If typeids are dense integers, use direct array indexing
  - **Dependencies**: None

### Size Classes for Cache Locality

- [ ] **P1** Implement size-class segregated allocation
  - **Location**: `Pool` struct at `src/main.c3:428-444`
  - **Concept**: Group objects by size class (8, 16, 32, 64, 128, 256, 512, 1024+ bytes)
  - **Benefits**:
    - Same-size objects are contiguous in memory
    - Eliminates external fragmentation within size class
    - Better cache utilization during iteration
  - **Implementation Notes**:
    - Add `List{ArenaBlock}` per size class
    - Inline threshold becomes first size class boundary
  - **Dependencies**: None

### Bulk Allocation API

- [ ] **P1** Add batch allocation methods
  - **Location**: New methods on `Region`
  - **API**:
    ```c3
    fn List{ObjectHandle} Region.allocate_bulk(&self, $Type, $count)
    fn List{ObjectHandle} Region.allocate_array(&self, $Type, slice)
    ```
  - **Benefits**:
    - Single lock acquisition (when threading added)
    - Contiguous arena allocation
    - Reduced per-object overhead
  - **Dependencies**: None

### Arena Compaction / Defragmentation

- [ ] **P2** Implement arena compaction for long-lived regions
  - **Location**: New method on `Pool`
  - **Concept**: Move live objects to eliminate gaps, release unused arenas
  - **Implementation Notes**:
    - Requires updating all SlotTable entries pointing to moved objects
    - Best run during low-activity periods
    - Optional: Incremental compaction (N objects per call)
  - **Dependencies**: P0 alignment fixes

---

## P2: Memory Efficiency

Reduce memory waste and improve utilization.

### Free-List Coalescing

- [ ] **P2** Coalesce adjacent free chunks in arena free-list
  - **Location**: `Pool.arena_free()` at `src/main.c3:532-547`
  - **Current**: Free chunks are not merged; fragmentation accumulates
  - **Issue**: Documented at line 526 comment
  - **Implementation Notes**:
    - Sort free-list by offset (or maintain sorted)
    - On free: check if new chunk is adjacent to existing entries
    - Merge adjacent entries into single larger chunk
  - **Trade-off**: O(n) merge vs O(1) simple append
  - **Dependencies**: None

### Slab Allocator for Fixed-Size Objects

- [ ] **P2** Add slab allocator for homogeneous object pools
  - **Concept**: Pre-allocate fixed-size slots for frequently used types
  - **Benefits**:
    - Zero fragmentation for fixed-size objects
    - O(1) allocation from free-list
    - Excellent cache locality
  - **Use Cases**: Nodes in tree/graph structures, small fixed-size records
  - **Dependencies**: Size classes (P1)

### Memory Pooling per Type

- [ ] **P2** Type-specific memory pools
  - **Concept**: Separate pools for each registered type
  - **Benefits**:
    - Type-homogeneous memory regions
    - Simplified destructor dispatch
    - Better iteration performance
  - **Trade-off**: More complex Pool management, increased metadata
  - **Dependencies**: P1 destructor registry improvements

### Reduce PoolSlot Overhead

- [ ] **P3** Optimize PoolSlot memory layout
  - **Location**: `PoolSlot` at `src/main.c3:345-355`
  - **Current Size**: ~40 bytes per slot (size, typeid, owner_id, flags, ptr, inline_data)
  - **Optimizations**:
    - Pack flags into size/typeid unused bits
    - Consider separate inline/heap slot types
  - **Dependencies**: None

---

## P2: New Features

Functionality additions that expand system capabilities.

### Weak References

- [ ] **P2** Implement weak reference handles
  - **Concept**: References that don't prevent object destruction
  - **API**:
    ```c3
    struct WeakHandle { ... }
    fn WeakHandle ObjectHandle.downgrade(self)
    fn ObjectHandle? WeakHandle.upgrade(self)
    ```
  - **Implementation Notes**:
    - Weak handles don't increment refcount
    - `upgrade()` returns null if object was destroyed
    - Requires tracking weak handle count per object
  - **Use Cases**: Caches, parent references in trees, observer patterns
  - **Dependencies**: None

### Memory Statistics & Debugging

- [ ] **P2** Add comprehensive memory statistics
  - **API**:
    ```c3
    struct MemoryStats {
        usz total_allocated;
        usz total_freed;
        usz live_objects;
        usz arena_count;
        usz fragmentation_ratio;
        usz inline_object_count;
        usz heap_object_count;
    }
    fn MemoryStats Region.get_stats(&self)
    fn MemoryStats RegionRegistry.get_global_stats(&self)
    ```
  - **Benefits**: Debugging memory leaks, tuning allocation strategies
  - **Dependencies**: None

- [ ] **P2** Add memory leak detection in debug builds
  - **Concept**: Track allocation stack traces, report unreleased objects
  - **Implementation Notes**:
    - Only in debug builds (conditional compilation)
    - Store allocation location (file, line) per object
    - Report on region destruction if live objects remain
  - **Dependencies**: Memory statistics

### Region Scopes (RAII Helper)

- [ ] **P2** Add RAII-style region scope macro
  - **API**:
    ```c3
    macro with_region(parent, $body) {
        RegionHandle r = g_registry.create_region(parent);
        defer g_registry.release_region(r);
        $body
    }
    ```
  - **Benefits**: Automatic region cleanup, exception-safe (if C3 adds exceptions)
  - **Dependencies**: None

### Iteration API for Live Objects

- [ ] **P2** Add typed iteration over region objects
  - **API**:
    ```c3
    fn ObjectIterator Region.iterate(&self)
    fn ObjectIterator Region.iterate_type(&self, typeid)

    macro Region.foreach(&self, $Type, $var, $body) { ... }
    ```
  - **Implementation Notes**:
    - Leverage `SlotTable.live_slot_tracker` sparse set
    - Filter by typeid for type-specific iteration
  - **Use Cases**: Serialization, debugging, batch operations
  - **Dependencies**: None

### Serialization Support

- [ ] **P3** Add region serialization/deserialization
  - **API**:
    ```c3
    fn []char Region.serialize(&self)
    fn Region Region.deserialize([]char data)
    ```
  - **Implementation Notes**:
    - Serialize object data, types, and relationships
    - Handle cross-references between objects
    - Support custom serializers per type
  - **Use Cases**: Persistence, network transfer, checkpointing
  - **Dependencies**: Iteration API (P2)

---

## P2: Safety & Robustness

Defensive programming and error detection.

### Thread Safety (Optional)

- [ ] **P2** Add optional thread-safe mode
  - **Scope**:
    - `RegionRegistry`: Global lock or read-write lock
    - `Region`: Per-region lock for modifications
    - `Pool`: Lock or lock-free arena allocation
  - **Implementation Options**:
    1. Coarse-grained: Single global mutex
    2. Fine-grained: Per-region locks
    3. Lock-free: Atomic operations for hot paths
  - **Configuration**: Compile-time flag to enable/disable
  - **Dependencies**: None

### Bounds Checking in Debug Mode

- [ ] **P2** Add debug-mode bounds checking
  - **Checks to Add**:
    - Array index bounds in sparse set
    - Slot ID bounds in slot table
    - Pool ID bounds in pool
    - Arena pointer bounds
  - **Implementation**: Conditional compilation with `$if DEBUG`
  - **Dependencies**: None

### Handle Poisoning for Use-After-Free

- [ ] **P2** Poison freed handles in debug mode
  - **Concept**: Set freed memory to recognizable pattern (e.g., `0xDEADBEEF`)
  - **Implementation Notes**:
    - Poison inline_data on slot release
    - Poison arena memory on free
    - Check for poison pattern on dereference (detect use-after-free)
  - **Dependencies**: Debug bounds checking

### Overflow Protection

- [ ] **P3** Add integer overflow checks for size calculations
  - **Locations**:
    - Arena size calculations
    - Bulk allocation count * size
    - Index arithmetic
  - **Implementation**: Safe arithmetic macros with overflow detection
  - **Dependencies**: None

---

## P3: API Ergonomics

Developer experience improvements.

### Simplified Allocation Macros

- [ ] **P3** Add convenience allocation macros
  - **Current**: `region.allocate_typed(MyType, value)`
  - **Proposed**:
    ```c3
    macro alloc(region, value) => region.allocate_typed(typeof(value), value)
    macro alloc_default(region, $Type) => region.allocate_typed($Type, {})
    macro alloc_array(region, $Type, count) => ...
    ```
  - **Dependencies**: None

### Builder Pattern for Region Configuration

- [ ] **P3** Add builder pattern for region creation
  - **API**:
    ```c3
    fn RegionBuilder RegionRegistry.build_region(&self)
    fn RegionBuilder RegionBuilder.with_parent(RegionHandle)
    fn RegionBuilder RegionBuilder.with_initial_capacity(usz)
    fn RegionBuilder RegionBuilder.with_arena_size(usz)
    fn RegionHandle RegionBuilder.create()
    ```
  - **Benefits**: Clearer configuration, extensible without API breaks
  - **Dependencies**: None

### Error Handling Improvements

- [ ] **P3** Replace panics with Result types for recoverable errors
  - **Current**: `unreachable` panics for invalid operations
  - **Proposed**:
    ```c3
    enum RegionError { INVALID_HANDLE, INVALID_REGION, ... }
    alias Result{T} = T | RegionError;
    fn Result{void*} RegionRegistry.try_dereference(&self, ObjectHandle)
    ```
  - **Trade-off**: Adds error handling overhead, more verbose API
  - **Dependencies**: None

### Better Debug Messages

- [ ] **P3** Improve panic messages with context
  - **Current**: `unreachable("DEAD slot should not exist")`
  - **Proposed**: Include handle values, region state, operation context
  - **Dependencies**: None

---

## P2: Testing & Validation

Quality assurance and reliability.

### Unit Tests for Each Component

- [ ] **P2** Unit tests for SparseSet
  - **Coverage**:
    - Insert/remove/contains correctness
    - Boundary conditions (empty set, single element)
    - Stress test with many elements
  - **Dependencies**: Test framework setup

- [ ] **P2** Unit tests for Pool
  - **Coverage**:
    - Inline vs arena allocation threshold
    - Free-list reuse
    - Slot compaction correctness
  - **Dependencies**: Test framework setup

- [ ] **P2** Unit tests for SlotTable
  - **Coverage**:
    - Generation counter increments
    - Forwarding chain resolution
    - Slot recycling
  - **Dependencies**: Test framework setup

- [ ] **P2** Unit tests for Region
  - **Coverage**:
    - Object allocation and deallocation
    - Reference counting
    - Parent-child relationships
  - **Dependencies**: Test framework setup

- [ ] **P2** Unit tests for RegionRegistry
  - **Coverage**:
    - Region creation and destruction
    - Object promotion on region death
    - Ghost table resolution
    - Handle validation
  - **Dependencies**: Test framework setup

### Stress Tests

- [ ] **P2** Memory pressure stress test
  - **Scenarios**:
    - Allocate until OOM, verify graceful handling
    - Rapid alloc/free cycles
    - Deep region nesting (100+ levels)
  - **Dependencies**: Unit tests

- [ ] **P2** Forwarding chain stress test
  - **Scenarios**:
    - Create deep chains (10+ promotions)
    - Verify chain compression works
    - Measure access time degradation
  - **Dependencies**: Unit tests

### Fuzzing

- [ ] **P3** Fuzz testing for edge cases
  - **Targets**:
    - Random allocation sizes
    - Random operation sequences (alloc, free, promote)
    - Random region tree structures
  - **Tools**: Consider libFuzzer integration or custom fuzzer
  - **Dependencies**: Unit tests, stress tests

### Property-Based Testing

- [ ] **P3** Add property-based tests
  - **Properties**:
    - All allocated objects are dereferenceable
    - No double-free possible
    - Handle validation is consistent
    - Refcount invariants hold
  - **Dependencies**: Unit tests

---

## P3: Documentation & Examples

Improve documentation and provide usage examples.

### Usage Examples

- [ ] **P3** Add example: Basic region usage
- [ ] **P3** Add example: Hierarchical regions for game entities
- [ ] **P3** Add example: Using destructors for resource cleanup
- [ ] **P3** Add example: Manual promotion for performance

### API Documentation

- [ ] **P3** Generate API reference documentation
- [ ] **P3** Add architecture diagram in markdown/mermaid

---

## Summary by Priority

| Priority | Count | Description |
|----------|-------|-------------|
| P0 | 3 | Critical bugs that must be fixed |
| P1 | 5 | High-impact performance improvements |
| P2 | 20 | Important features and improvements |
| P3 | 13 | Nice-to-have enhancements |

---

## Dependency Graph

```
P0: Alignment Fixes
    └── P2: Arena Compaction

P1: Size Classes
    └── P2: Slab Allocator

P2: Memory Statistics
    └── P2: Leak Detection

P2: Iteration API
    └── P3: Serialization

P2: Unit Tests
    └── P2: Stress Tests
        └── P3: Fuzzing
```

---

*Last updated: 2026-02-08*
