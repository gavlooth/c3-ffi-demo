# Region Memory System v3 - TODO

A focused roadmap of improvements for the Region Memory System, filtered for items that align with the architecture.

---

## Design Principles (Context for TODO Filtering)

The following items were considered but **intentionally excluded** because they conflict with or are redundant given the architecture:

| Excluded Item | Reason |
|---------------|--------|
| Weak References | Handles are already "weak" - they don't extend object lifetime. Regions own objects. |
| Memory Leak Detection | Objects are promoted to parent on region death, not "leaked". The concept doesn't apply. |
| Handle Poisoning | Generation counters already detect use-after-free. Redundant safety mechanism. |
| Size Classes | The inline (≤16 bytes) + arena (>16 bytes) split is already effective and simple. |
| Slab Allocator | Inline storage already provides slab-like behavior for small objects. |
| Type-specific Pools | Adds complexity without clear benefit; type filtering can be done via typeid. |
| Arena Compaction | Fights the region model. Regions should be short-lived; promote objects if needed. |
| Free-list Coalescing | Micro-optimization. Arenas are freed together when region dies. |
| Builder Pattern | Over-engineering. `create_region(parent)` is already simple. |
| Result Error Types | Panics are correct for invariant violations (programmer errors, not runtime errors). |

---

## P0: Critical Bug Fixes

High-priority issues that could cause incorrect behavior.

### Arena Free-List Bug

- [ ] **P0** Fix arena free-list using wrong arena index
  - **Location**: `Pool.arena_free()` at `src/main.c3:532-547`
  - **Issue**: When freeing arena memory, the free-list entry stores offset but no arena index
  - **Current Code**: `ptr = self.arenas[0].data + self.free_list[i].offset` (always uses arena 0!)
  - **Impact**: Memory corruption when multiple arenas exist
  - **Fix**: Store `arena_index` in `FreeListEntry` alongside offset and size
  - **Dependencies**: None

### Memory Alignment Issues

- [ ] **P0** Add explicit alignment for inline storage
  - **Location**: `PoolSlot.inline_data` at `src/main.c3:345-355`
  - **Issue**: `char[16]` may not provide correct alignment for all types
  - **Fix**: Use C3 alignment attribute or ensure PoolSlot struct is properly aligned
  - **Dependencies**: None

- [ ] **P0** Ensure arena bump allocator respects alignment requirements
  - **Location**: `Pool.arena_alloc()` at `src/main.c3:479-518`
  - **Issue**: Bump pointer allocation doesn't account for type alignment
  - **Fix**: Add alignment parameter, round up `arena.used` before allocation
  - **Dependencies**: None

---

## P1: Performance Improvements

Optimizations that significantly improve runtime performance.

### O(1) Ghost Table Lookup

- [ ] **P1** Replace linear ghost table search with hash map
  - **Location**: `RegionRegistry.dereference_via_ghost()` at `src/main.c3:1728-1761`
  - **Current**: O(n) linear scan of `ghost_source_ids` array
  - **Target**: O(1) with hash map keyed by `(region_id, generation)`
  - **Implementation Notes**:
    - Simple open-addressing hash table
    - Key: `region_id << 32 | generation` (combined 64-bit key)
    - Value: index into host region's ghost table list
  - **Dependencies**: None

### O(1) Destructor Registry

- [ ] **P1** Replace linear destructor lookup with hash map
  - **Location**: `DestructorRegistry.find()` at `src/main.c3:1189-1196`
  - **Current**: O(n) linear scan of `registered_type_ids`
  - **Target**: O(1) with hash map `typeid -> DestructorFn`
  - **Note**: If typeids are sequential integers, direct array indexing even simpler
  - **Dependencies**: None

### Bulk Allocation API

- [ ] **P1** Add batch allocation methods for arrays
  - **Location**: New methods on `Region`
  - **API**:
    ```c3
    macro List{ObjectHandle} Region.allocate_array(&self, $Type, values)
    ```
  - **Benefits**:
    - Single slot table growth
    - Contiguous arena allocation
    - Reduced per-object overhead
  - **Dependencies**: None

---

## P2: New Features

Functionality additions that expand system capabilities.

### Memory Statistics

- [ ] **P2** Add memory statistics for debugging
  - **API**:
    ```c3
    struct MemoryStats {
        uint live_regions;
        uint live_objects;
        uint total_arenas;
        usz  arena_bytes_used;
        usz  arena_bytes_capacity;
        uint inline_objects;
        uint heap_objects;
    }
    fn MemoryStats RegionRegistry.get_stats(&self)
    ```
  - **Use Cases**: Debugging, performance tuning, capacity planning
  - **Dependencies**: None

### Region Scopes (RAII Helper)

- [ ] **P2** Add RAII-style region scope macro
  - **API**:
    ```c3
    macro with_region(registry, parent, $body) {
        RegionHandle r = registry.create_region(parent);
        defer registry.release_region(r);
        $body
    }
    ```
  - **Benefits**: Automatic region cleanup, prevents forgetting release
  - **Dependencies**: None

### Iteration API for Live Objects

- [ ] **P2** Add iteration over region objects
  - **API**:
    ```c3
    fn uint[] Region.get_live_slot_ids(&self)
    macro Region.foreach_object(&self, $body)
    ```
  - **Implementation**: Leverage existing `SlotTable.live_slot_tracker` sparse set
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
    - Serialize object data with type info
    - Handle internal references via slot ID remapping
  - **Use Cases**: Persistence, checkpointing
  - **Dependencies**: Iteration API

---

## P2: Safety & Robustness

Defensive programming and error detection.

### Bounds Checking in Debug Mode

- [ ] **P2** Add debug-mode bounds checking
  - **Checks to Add**:
    - SlotId bounds in slot table access
    - PoolId bounds in pool access
    - Arena pointer bounds validation
  - **Implementation**: Use `$if @defined(DEBUG)` conditional compilation
  - **Dependencies**: None

### Overflow Protection

- [ ] **P3** Add integer overflow checks for size calculations
  - **Locations**:
    - Arena size calculations in `arena_alloc`
    - Bulk allocation count * size
  - **Implementation**: Checked arithmetic with explicit overflow handling
  - **Dependencies**: None

---

## P2: API Ergonomics

Developer experience improvements.

### Simplified Allocation Macros

- [ ] **P3** Add convenience allocation macros
  - **API**:
    ```c3
    // Infer type from value
    macro alloc(region, value) => region.allocate_typed(typeof(value), value)

    // Default-initialize
    macro alloc_default(region, $Type) => region.allocate_typed($Type, {})
    ```
  - **Dependencies**: None

### Better Debug Messages

- [ ] **P3** Improve panic messages with context
  - **Current**: `unreachable("DEAD slot should not exist")`
  - **Proposed**: Include handle values, region ID, generation in message
  - **Example**: `unreachable("DEAD slot: region=%d, slot=%d, gen=%d", ...)`
  - **Dependencies**: None

---

## P2: Testing & Validation

Quality assurance and reliability.

### Unit Tests

- [ ] **P2** Unit tests for SparseSet
  - **Coverage**: insert, remove, contains, iteration, edge cases
  - **Dependencies**: Test framework

- [ ] **P2** Unit tests for Pool
  - **Coverage**: inline vs arena threshold, removal compaction, ID recycling
  - **Dependencies**: Test framework

- [ ] **P2** Unit tests for SlotTable
  - **Coverage**: generation counters, forwarding, slot recycling
  - **Dependencies**: Test framework

- [ ] **P2** Unit tests for Region lifecycle
  - **Coverage**: allocation, deallocation, promotion on death
  - **Dependencies**: Test framework

- [ ] **P2** Unit tests for RegionRegistry
  - **Coverage**: region tree, ghost tables, handle validation
  - **Dependencies**: Test framework

### Stress Tests

- [ ] **P2** Memory pressure stress test
  - **Scenarios**:
    - Many small allocations (inline path)
    - Many large allocations (arena path)
    - Deep region nesting (10+ levels)
    - Rapid create/destroy cycles
  - **Dependencies**: Unit tests

- [ ] **P2** Forwarding chain stress test
  - **Scenarios**:
    - Deep promotion chains
    - Verify chain compression works
  - **Dependencies**: Unit tests

### Fuzzing

- [ ] **P3** Fuzz testing for edge cases
  - **Targets**:
    - Random allocation sizes (0 to 1MB)
    - Random operation sequences
    - Random region tree structures
  - **Dependencies**: Unit tests, stress tests

---

## P3: Documentation

### Usage Examples

- [ ] **P3** Add example: Basic region usage
- [ ] **P3** Add example: Hierarchical regions for game entities
- [ ] **P3** Add example: Using destructors for resource cleanup
- [ ] **P3** Add example: Manual promotion for performance

---

## Summary by Priority

| Priority | Count | Description |
|----------|-------|-------------|
| P0 | 3 | Critical bugs (arena indexing, alignment) |
| P1 | 3 | Performance (O(1) lookups, bulk allocation) |
| P2 | 11 | Features, safety, testing |
| P3 | 8 | Ergonomics, documentation, fuzzing |

**Total: 25 items** (reduced from 41 by removing architecture-incompatible items)

---

## Dependency Graph

```
P0: Arena Free-List Fix
    (no dependencies)

P0: Alignment Fixes
    (no dependencies)

P1: O(1) Lookups
    (no dependencies)

P2: Iteration API
    └── P3: Serialization

P2: Unit Tests
    └── P2: Stress Tests
        └── P3: Fuzzing
```

---

*Last updated: 2026-02-08*
