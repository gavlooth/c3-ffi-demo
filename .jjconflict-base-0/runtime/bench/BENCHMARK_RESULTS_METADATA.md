# Region-Level Metadata Benchmark Results

**Date:** 2025-01-08
**Optimization:** T-opt-region-metadata (Region-Level Type Metadata)

## Summary

The region-level metadata optimization shows **significant performance improvements** with minimal memory overhead.

## Key Findings

### 1. Allocation Performance: 3.94x Speedup

```
alloc_tag_int (baseline):    13.30 ns/op (75,182,995 ops/sec)
alloc_typed_int (new):        3.37 ns/op (296,550,231 ops/sec)
Speedup: 3.94x faster
```

**Analysis:**
- The new `alloc_obj_typed()` is significantly faster than tag-based allocation
- This is likely due to:
  - Better compiler optimization of compile-time type_id constants
  - More efficient inline buffer utilization
  - O(1) metadata lookup via array indexing

### 2. Metadata Lookup: Sub-Nanosecond

```
metadata_lookup_int:     0.78 ns/op (1,276,373,058 ops/sec)
metadata_lookup_pair:    0.61 ns/op (1,628,205,734 ops/sec)
metadata_lookup_array:   0.60 ns/op (1,659,450,821 ops/sec)
```

**Analysis:**
- Metadata lookup is essentially an array access
- Consistent ~0.6-0.8 ns regardless of type
- L1 cache hit: ~4 cycles at 3GHz

### 3. Memory Overhead: Minimal

```
Per-region overhead:  19 types × 96 bytes = 1,824 bytes (1.8 KB)
Per-object:           4 bytes (tag field, kept for compatibility)
```

**Analysis:**
- One-time cost per region: ~2 KB
- Amortized over thousands of objects: negligible
- Tag field still present (4 bytes) for compatibility
- **Future optimization:** Can eliminate tag field entirely once all code uses type_id

### 4. Correctness Verification

All tests passed:
- ✓ TypeID-to-tag mapping produces correct tags
- ✓ Metadata lookup returns correct information
- ✓ Size, pointer fields, and inline thresholds accurate

## Comparison with Other Optimizations

| Optimization | Speedup | Memory Cost | Complexity |
|-------------|---------|-------------|------------|
| Region Metadata | **3.94x** | 1.8 KB/region | Low |
| Inline Allocation | 7.74x | 512 bytes/region | Low |
| Specialized Constructors | 3.98-5.21x | None | Medium |

## Architectural Impact

### Enabled by This Change

1. **Compile-Time Type Constants:** TypeID is a compile-time constant (enum value)
2. **Inline Allocation:** Metadata includes `inline_threshold` for each type
3. **Type-Specific Operations:** Metadata includes function pointers for trace, destroy, equals, hash
4. **Future Tag Elimination:** Can remove 4-byte tag field from every object

### Code Generation Impact

**Before:**
```c
Obj* obj = alloc_obj_region(region, TAG_INT);
```

**After:**
```c
Obj* obj = alloc_obj_typed(region, TYPE_ID_INT);
```

The compiler can now:
- Emit compile-time type_id constants
- Enable inline allocation for small objects
- Eliminate runtime type checks
- Enable better optimization

## Recommendations

### Immediate
- ✅ **ADOPT:** Use `alloc_obj_typed()` in all new code
- ✅ **MIGRATE:** Gradually convert existing tag-based code
- ✅ **EXTEND:** Add metadata for user-defined types

### Future Work
- **Tag Field Elimination:** Once all code uses type_id, remove tag field
- **Inline Allocation:** Use `inline_threshold` from metadata
- **Cross-Region Masking:** Implement pointer masking for inter-region references
- **Compiler Integration:** Extend type checker to emit type_id constants

## Benchmark Methodology

**Platform:** Linux x86_64, gcc -O2
**Iterations:** 1,000,000 allocations per test
**Timing:** clock_gettime(CLOCK_MONOTONIC)
**Region:** Single region with metadata initialized

**Warmup:** 1,000 allocations before timing
**Verification:** Tag correctness checked after each test

## Conclusion

The region-level metadata optimization provides:
- ✅ **3.94x allocation speedup**
- ✅ **Sub-nanosecond metadata lookup**
- ✅ **Minimal memory overhead (1.8 KB/region)**
- ✅ **Foundation for future optimizations**

**Verdict:** Strongly recommend adoption.
