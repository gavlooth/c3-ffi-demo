# Region Accounting Specification

**Status:** Normative specification for per-region accounting counters  
**Applies to:** `runtime/src/memory/region_core.h`, `runtime/src/memory/region_core.c`  
**Last Updated:** 2026-01-10  
**Related:** `runtime/docs/REGION_RC_MODEL.md` (Region-RC model), `runtime/docs/ARCHITECTURE.md` (architecture)

**Terminology:** This document counts metrics on the runtime **ArenaRegion/RCB** (currently `struct Region`).
Canonical “Region” terminology is pinned in `runtime/docs/MEMORY_TERMINOLOGY.md`.

---

## 1) Purpose

Region accounting provides **deterministic, low-cost metrics** per region that enable:

1. **Retention diagnostics** - Detect "retention cliffs" (allocating into too-long-lived regions)
2. **Auto-repair decision heuristic** - Choose between transmigrate vs. retain for younger→older stores
3. **Performance tuning** - Identify regions with unexpected allocation patterns
4. **Debug diagnostics** - Verify compiler escape analysis correctness

---

## 2) Required Counters

### 2.1 Allocation Counters (Mandatory)

Every `Region` struct MUST track the following counters:

```c
typedef struct Region {
    // ... existing fields ...
    
    // Allocation counters (all reset to 0 on region_create)
    size_t bytes_allocated_total;  // Cumulative bytes allocated in this region
    size_t bytes_allocated_peak;   // Peak bytes simultaneously allocated
    size_t chunk_count;             // Number of arena chunks allocated
    size_t inline_buf_used_bytes;   // Peak usage of inline buffer (0-512 bytes)
    
    // Repair counters (optional but recommended)
    size_t escape_repair_count;    // Number of times this region triggered auto-repair
} Region;
```

#### Counter Definitions

| Counter | Type | Reset On | Update Trigger | Purpose |
|---------|------|----------|----------------|---------|
| `bytes_allocated_total` | `size_t` | `region_create()` | Every `region_alloc()` | Track total memory pressure from this region |
| `bytes_allocated_peak` | `size_t` | `region_create()` | On every allocation: `current = bytes_allocated_total - bytes_freed; peak = max(peak, current)` | Detect maximum simultaneous memory usage |
| `chunk_count` | `size_t` | `region_create()` | Every time a new 4KB arena chunk is allocated | Track fragmentation/overhead |
| `inline_buf_used_bytes` | `size_t` | `region_create()` | On inline buffer allocation: `used_bytes = current_offset` | Measure inline buffer utilization |
| `escape_repair_count` | `size_t` | `region_create()` | Every time auto-repair (transmigrate or retain) is triggered for this region | Detect problematic escape patterns |

### 2.2 Optional Counters (For Enhanced Diagnostics)

The following counters are **optional** but recommended for debug builds:

```c
typedef struct Region {
    // ... required counters ...
    
    // Optional diagnostic counters (debug-only)
#ifdef OMNI_DEBUG
    size_t alloc_count;           // Total number of allocations
    size_t free_count;            // Total number of frees (for transmigration/etc.)
    size_t retain_count;          // Number of region_retain() calls
    size_t release_count;         // Number of region_release() calls
    size_t tether_start_count;    // Number of tether_start() calls
    size_t tether_end_count;      // Number of tether_end() calls
#endif
} Region;
```

---

## 3) Accounting Implementation

### 3.1 Region Initialization

```c
Region* region_create(void) {
    Region* r = (Region*)malloc(sizeof(Region));
    if (!r) return NULL;
    
    // ... existing initialization ...
    
    // Initialize accounting counters
    r->bytes_allocated_total = 0;
    r->bytes_allocated_peak = 0;
    r->chunk_count = 0;
    r->inline_buf_used_bytes = 0;
    r->escape_repair_count = 0;
    
#ifdef OMNI_DEBUG
    r->alloc_count = 0;
    r->free_count = 0;
    r->retain_count = 0;
    r->release_count = 0;
    r->tether_start_count = 0;
    r->tether_end_count = 0;
#endif
    
    return r;
}
```

### 3.2 Allocation Accounting

```c
void* region_alloc(Region* r, size_t size) {
    void* ptr = allocate_from_arena_or_inline(r, size);
    
    // Update allocation counters
    r->bytes_allocated_total += size;
    r->bytes_allocated_peak = max(r->bytes_allocated_peak, current_usage(r));
    r->inline_buf_used_bytes = max(r->inline_buf_used_bytes, inline_offset(r));
    
#ifdef OMNI_DEBUG
    r->alloc_count++;
#endif
    
    return ptr;
}
```

### 3.3 Chunk Allocation Accounting

```c
void* alloc_new_chunk(Region* r, size_t chunk_size) {
    void* chunk = malloc(chunk_size);
    
    if (chunk) {
        r->chunk_count++;
    }
    
    return chunk;
}
```

### 3.4 Repair Counter Update

```c
// Called when auto-repair is triggered for this region
void region_record_escape_repair(Region* r) {
    r->escape_repair_count++;
}
```

---

## 4) Size Heuristic for Auto-Repair

### 4.1 Problem Statement

When a **younger region** stores a value into an **older region**, the compiler must choose:

1. **Transmigrate:** Copy value into older region (adds copy cost, immediate reclamation of younger)
2. **Retain:** Keep younger region alive (adds retention cost, deferred reclamation)

The **size heuristic** uses region accounting to make this decision deterministically.

### 4.2 Heuristic Algorithm

```c
typedef enum RepairStrategy {
    REPAIR_TRANSMIGRATE,  // Copy value into destination region
    REPAIR_RETAIN         // Keep source region alive via RC
} RepairStrategy;

RepairStrategy choose_repair_strategy(
    Region* src_region,    // Younger region
    Region* dst_region    // Older region
) {
    size_t src_bytes = src_region->bytes_allocated_total;
    size_t dst_bytes = dst_region->bytes_allocated_total;
    
    // HEURISTIC 1: Size-based threshold
    // If source region is small (≤ 4 KB), prefer transmigrate
    // If source region is large (> 4 KB), prefer retain
    if (src_bytes <= 4096) {
        return REPAIR_TRANSMIGRATE;
    } else {
        return REPAIR_RETAIN;
    }
    
    // HEURISTIC 2: Relative size (alternative)
    // Prefer retain if source is > 50% of destination
    // if (src_bytes > (dst_bytes / 2)) {
    //     return REPAIR_RETAIN;
    // } else {
    //     return REPAIR_TRANSMIGRATE;
    // }
    
    // HEURISTIC 3: Retention risk (future)
    // Check if dst_region has external_rc > 0 or long lifetime
    // If yes, prefer transmigrate to avoid retention cliff
}
```

### 4.3 Tunable Thresholds

The size heuristic should be **configurable** via compile-time constants:

```c
// region_core.h

// Auto-repair strategy thresholds
#define OMNI_REPAIR_TRANSMIGRATE_THRESHOLD_BYTES  4096  // 4 KB
#define OMNI_REPAIR_RETAIN_THRESHOLD_BYTES        (OMNI_REPAIR_TRANSMIGRATE_THRESHOLD_BYTES + 1)

// Alternative: Relative size threshold
#define OMNI_REPAIR_RETAIN_RELATIVE_THRESHOLD     0.5   // 50%
```

### 4.4 Accounting Impact on Heuristic

| Counter | Impact on Heuristic |
|---------|-------------------|
| `bytes_allocated_total` | Primary input: size of source/destination regions |
| `chunk_count` | Secondary input: detect highly fragmented regions (prefer transmigrate) |
| `escape_repair_count` | Diagnostic: if > 10, warn about problematic escape pattern |
| `inline_buf_used_bytes` | Secondary input: small inline-only regions always prefer transmigrate |

---

## 5) Retention Diagnostics

### 5.1 What is a "Retention Cliff"?

A **retention cliff** occurs when:

1. A **short-lived region** (young, expected to close quickly) allocates data
2. That data escapes into a **long-lived region** (older, outliving scope)
3. Via **retain** (not transmigrate), the young region is kept alive
4. Result: The young region is **kept alive longer than intended**, causing "leak-like" behavior

### 5.2 Diagnostic Detection

When a region exits (`region_exit()`), emit diagnostics if:

```c
void region_exit(Region* r) {
    r->scope_alive = false;
    
    // Check for retention cliff warning
    if (r->external_rc > 0) {
        time_t exit_time = time(NULL);
        time_t lifetime = exit_time - r->create_time;
        
        // Warn if region kept alive > 2x expected lifetime
        // (Expected lifetime estimated from allocation pattern)
        if (lifetime > EXPECTED_LIFETIME_MS * 2) {
            fprintf(stderr, "RETENTION CLIFF WARNING:\n");
            fprintf(stderr, "  Region %u kept alive for %ld ms (expected ~%d ms)\n",
                    r->id, lifetime, EXPECTED_LIFETIME_MS);
            fprintf(stderr, "  External RC: %u\n", r->external_rc);
            fprintf(stderr, "  Allocations: %zu bytes, %zu chunks\n",
                    r->bytes_allocated_total, r->chunk_count);
            fprintf(stderr, "  Escape repairs triggered: %zu\n", r->escape_repair_count);
        }
    }
}
```

### 5.3 Diagnostic Output Format

```
RETENTION CLIFF WARNING:
  Region 42 kept alive for 1500 ms (expected ~50 ms)
  External RC: 3
  Allocations: 8192 bytes, 2 chunks
  Escape repairs triggered: 15
  Inline buffer peak usage: 512 bytes

Recommended action:
  Consider using transmigrate instead of retain for this region,
  or allocate directly into the older region.
```

---

## 6) Reporting Interface

### 6.1 Per-Region Accounting Dump

```c
void region_dump_accounting(const Region* r) {
    printf("Region %u Accounting:\n", r->id);
    printf("  Total allocated: %zu bytes\n", r->bytes_allocated_total);
    printf("  Peak allocated:  %zu bytes\n", r->bytes_allocated_peak);
    printf("  Chunks:          %zu\n", r->chunk_count);
    printf("  Inline usage:    %zu / 512 bytes\n", r->inline_buf_used_bytes);
    printf("  Escape repairs:   %zu\n", r->escape_repair_count);
    printf("  External RC:      %u\n", r->external_rc);
    printf("  Scope alive:     %s\n", r->scope_alive ? "yes" : "no");
    
#ifdef OMNI_DEBUG
    printf("  Allocs:          %zu\n", r->alloc_count);
    printf("  Retains:         %zu\n", r->retain_count);
    printf("  Releases:        %zu\n", r->release_count);
#endif
}
```

### 6.2 Global Region Accounting Summary

```c
void region_dump_global_accounting(void) {
    printf("Global Region Accounting Summary:\n");
    printf("  Active regions:   %zu\n", active_region_count);
    printf("  Total allocated: %zu bytes\n", global_bytes_allocated);
    printf("  Total peak:      %zu bytes\n", global_bytes_peak);
    printf("  Total chunks:    %zu\n", global_chunk_count);
    printf("  Total repairs:   %zu\n", global_repair_count);
}
```

---

## 7) Implementation Verification

### 7.1 Unit Tests

The following unit tests MUST pass:

```lisp
;; Test 1: Basic allocation accounting
(define (test-alloc-accounting)
  (let ((region (region-create))
        (x (alloc region 100))
        (y (alloc region 200)))
    (assert (= (region-bytes-allocated-total region) 300))
    (assert (= (region-bytes-allocated-peak region) 300))
    (region-exit region)))

;; Test 2: Peak tracking
(define (test-peak-tracking)
  (let ((region (region-create))
        (x (alloc region 1000)))
    (alloc region 500)  ; 1500 total
    (free x)            ; back to 500
    ;; Peak should still be 1500
    (assert (= (region-bytes-allocated-peak region) 1500))
    (region-exit region)))

;; Test 3: Chunk counting
(define (test-chunk-counting)
  (let ((region (region-create)))
    (alloc region 8192)  ; Force 2 chunks (4KB each)
    (assert (>= (region-chunk-count region) 2))
    (region-exit region)))

;; Test 4: Repair counter
(define (test-repair-counter)
  (let ((older (region-create))
        (younger (region-create))
        (x (alloc younger 100)))
    (set-in-older older x)  ; Triggers auto-repair
    (assert (> (region-escape-repair-count younger) 0))
    (region-exit older)
    (region-exit younger)))
```

### 7.2 Integration Test: Retention Cliff Detection

```lisp
;; Test: Retention cliff warning emitted
(define (test-retention-cliff-detection)
  (let ((older (region-create))  ; Long-lived
        (short-lived (region-create)))  ; Short-lived
    (region-exit short-lived)
    ;; Trigger retain (simulating escape to older)
    (retain-region short-lived)
    ;; After some time, check warning emitted
    (sleep 1000)
    ;; Expected: "RETENTION CLIFF WARNING" printed to stderr
    (region-exit older)))
```

### 7.3 Heuristic Test: Transmigrate vs. Retain

```lisp
;; Test: Size heuristic chooses correctly
(define (test-repair-heuristic)
  (let ((small-region (region-create))
        (large-region (region-create))
        (older (region-create)))
    ;; Small region (≤ 4 KB) should transmigrate
    (alloc small-region 100)
    (assert (= (choose-repair-strategy small-region older) 'transmigrate))
    
    ;; Large region (> 4 KB) should retain
    (alloc large-region 8192)
    (assert (= (choose-repair-strategy large-region older) 'retain))
    
    (region-exit older)
    (region-exit small-region)
    (region-exit large-region)))
```

---

## 8) Performance Considerations

### 8.1 Overhead of Accounting

| Operation | Counter Updates | Overhead |
|-----------|----------------|----------|
| `region_alloc()` | 3 counters (`total`, `peak`, `inline_used`) | ~10-20 CPU cycles |
| `region_create()` | 5 counters initialization | Negligible |
| `chunk allocation` | 1 counter (`chunk_count`) | Negligible |
| `escape_repair` | 1 counter (`repair_count`) | Negligible |

**Total overhead:** ~2-3% of allocation time (acceptable for debug/profile builds)

### 8.2 Debug vs. Release Builds

**Release builds:** Required counters only (`bytes_*`, `chunk_count`, `repair_count`)

**Debug builds:** All counters including optional diagnostics (`alloc_count`, `retain_count`, etc.)

### 8.3 Zero-Cost in Production

In production builds without diagnostics enabled:
- Counters are maintained (required for auto-repair heuristic)
- But diagnostic logging is compiled out (`#ifdef OMNI_DEBUG`)

---

## 9) Usage Guidelines

### 9.1 When to Enable Diagnostics

- **Development builds:** Always enable (catch retention cliffs early)
- **CI/CD builds:** Enable to detect regressions
- **Production builds:** Disable (optional counters compiled out)

### 9.2 Threshold Tuning

Default thresholds (`4096` bytes for transmigrate) are a starting point. Tune based on:

1. **Benchmark results:** Measure transmigrate vs. retain performance for your workload
2. **Memory pressure:** If fragmentation is high, increase transmigrate threshold
3. **CPU cost:** If copying is too expensive, decrease transmigrate threshold

### 9.3 Retention Cliff Response

When a retention cliff is detected:

1. **Short term:** Manually fix the code (allocate in older region or force transmigrate)
2. **Long term:** Improve compiler escape analysis to detect this pattern automatically

---

## 10) "Done Means" for Region Accounting

Region accounting is complete only when:

1. **All required counters implemented:** `bytes_allocated_total`, `bytes_allocated_peak`, `chunk_count`, `inline_buf_used_bytes`, `escape_repair_count`
2. **Auto-repair heuristic works:** `choose_repair_strategy()` uses counters correctly
3. **Retention diagnostics work:** Warnings emitted for regions kept alive > 2x expected lifetime
4. **All unit tests pass:** Basic allocation, peak tracking, chunk counting, repair counter
5. **All integration tests pass:** Retention cliff detection, heuristic correctness
6. **Performance impact measured:** < 3% overhead for accounting operations

---

## 11) Future Work

### 11.1 Adaptive Thresholds

Future compiler passes could **learn** optimal thresholds dynamically based on:
- Historical repair performance
- CPU vs. memory cost trade-offs
- Region lifetime predictions

### 11.2 Compiler-Wide Retention Analysis

Instead of runtime-only diagnostics, the compiler could:
- Build a region dependency graph
- Detect "older holds reference to younger" patterns statically
- Warn at compile time about potential retention cliffs

### 11.3 Region Pool Reuse

Accounting could be extended to track **region pool reuse** (similar to Apache APR):
- Track how many times a region pool is reused
- Detect "pool leak" (pool never reused, accumulating allocations)

---

## 12) References

- `runtime/docs/REGION_RC_MODEL.md` - Region-RC model (external pointers, auto-repair)
- `runtime/docs/ARCHITECTURE.md` - Overall architecture
- Apache APR pools: https://perl.apache.org/docs/2.0/user/performance/prevent.html
- ATS MemArena (freeze/thaw): https://docs.trafficserver.apache.org/en/10.1.x/developer-guide/internal-libraries/MemArena.en.html
