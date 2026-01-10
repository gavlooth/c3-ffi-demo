# Ablative Fat Pointer Prototype Implementation Plan

**Date:** 2026-01-08
**Status:** Planning Phase
**Estimated Duration:** 8 weeks
**Risk Level:** Medium (with fallback strategy)

## Overview

This document describes a concrete implementation plan for an **ablative fat pointer system** for OmniLisp. The key innovation is starting with all objects as fat pointers and adding thinning fallbacks gradually, based on actual measurements and runtime behavior.

**Core Philosophy:** Start fat, thin when necessary, measure everything, can abort anytime.

---

## Table of Contents

1. [Architecture](#architecture)
2. [Phase 1: Always Fat Baseline](#phase-1-always-fat-baseline)
3. [Phase 2: Gradual Thinning](#phase-2-gradual-thinning)
4. [Phase 3: Compiler Integration](#phase-3-compiler-integration)
5. [Phase 4: Evaluation](#phase-4-evaluation)
6. [File Structure](#file-structure)
7. [Testing Strategy](#testing-strategy)
8. [Success Criteria](#success-criteria)

---

## Architecture

### Core Data Structures

```c
// File: src/runtime/obj_fat.h

#ifndef OBJ_FAT_H
#define OBJ_FAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Maximum size for inline storage (64 bytes)
#define MAX_INLINE_SIZE 56
#define TOTAL_FAT_SIZE 64  // Including header

// Fat pointer discriminator (low bit)
#define FAT_BIT 0x1
#define IS_FAT(obj) (((uintptr_t)(obj) & FAT_BIT) == FAT_BIT)
#define TO_FAT(obj)  ((ObjFat*)((uintptr_t)(obj) & ~FAT_BIT))
#define TO_THIN(obj) ((ObjThin*)((uintptr_t)(obj) | FAT_BIT))

// Object flags
typedef enum {
    FLAG_FAT      = 0x01,  // This is a fat pointer
    FLAG_THIN     = 0x02,  // This is a thin pointer
    FLAG_IMMUTABLE = 0x04,  // Object is immutable
    FLAG_CONST    = 0x08,  // Object is const
    FLAG_ESCAPED  = 0x10,  // Object has escaped current scope
} ObjFlags;

// Forward declarations
typedef struct ObjFat ObjFat;
typedef struct ObjThin ObjThin;

// Fat pointer: Inline storage for small objects
typedef struct ObjFat {
    uint64_t generation;      // IPGE generation
    uint32_t tag;            // Object type tag
    uint32_t flags;          // ObjFlags
    uint32_t size;           // Actual data size
    uint32_t _pad;           // Alignment
    uint8_t  data[MAX_INLINE_SIZE];  // Inline data
} ObjFat;

// Thin pointer: Regular heap allocation
typedef struct ObjThin {
    uint64_t generation;      // IPGE generation
    uint32_t tag;            // Object type tag
    uint32_t flags;          // ObjFlags
    size_t    size;          // Total object size
    void*     data;          // Pointer to actual data
} ObjThin;

// Unified object type (discriminated union)
typedef union ObjUnified {
    ObjFat*  fat;
    ObjThin* thin;
} ObjUnified;

// Runtime statistics
typedef struct {
    // Allocation counts
    long alloc_fat;
    long alloc_thin;
    long alloc_failed_size;

    // Thinning operations
    long fat_to_thin_size;
    long fat_to_thin_escape;
    long fat_to_thin_capture;
    long fat_to_thin_address;

    // Access patterns
    long fat_access;
    long thin_access;
    long fat_field_access;
    long thin_field_access;

    // Memory usage
    size_t bytes_inline;
    size_t bytes_heap;
    size_t bytes_saved;

    // Performance
    long cache_hits;
    long cache_misses;
} FatPointerStats;

// Global statistics (one per thread)
extern _thread_local FatPointerStats g_fat_stats;

#endif // OBJ_FAT_H
```

### Core API

```c
// File: src/runtime/obj_fat.c

#include "obj_fat.h"
#include "region_core.h"
#include <string.h>
#include <stdio.h>

// Thread-local statistics
_thread_local FatPointerStats g_fat_stats = {0};

// Initialize statistics
void fat_stats_init() {
    memset(&g_fat_stats, 0, sizeof(FatPointerStats));
}

// Print statistics
void fat_stats_print() {
    printf("\n=== Fat Pointer Statistics ===\n");
    printf("Allocations:       fat=%ld thin=%ld failed=%ld\n",
           g_fat_stats.alloc_fat,
           g_fat_stats.alloc_thin,
           g_fat_stats.alloc_failed_size);
    printf("Thinning:         size=%ld escape=%ld capture=%ld address=%ld\n",
           g_fat_stats.fat_to_thin_size,
           g_fat_stats.fat_to_thin_escape,
           g_fat_stats.fat_to_thin_capture,
           g_fat_stats.fat_to_thin_address);
    printf("Access:           fat=%ld thin=%ld\n",
           g_fat_stats.fat_access,
           g_fat_stats.thin_access);
    printf("Field Access:     fat=%ld thin=%ld\n",
           g_fat_stats.fat_field_access,
           g_fat_stats.thin_field_access);
    printf("Memory:           inline=%zu heap=%zu saved=%zu\n",
           g_fat_stats.bytes_inline,
           g_fat_stats.bytes_heap,
           g_fat_stats.bytes_saved);
    printf("Cache:            hits=%ld misses=%ld\n",
           g_fat_stats.cache_hits,
           g_fat_stats.cache_misses);
    printf("==============================\n\n");
}

// Allocate object (tries fat first)
ObjUnified alloc_obj_fat_first(Region* r, size_t size, uint32_t tag) {
    ObjUnified result;

    if (size <= MAX_INLINE_SIZE) {
        // Allocate as fat
        ObjFat* fat = (ObjFat*)region_alloc(r, sizeof(ObjFat));
        fat->generation = 0;
        fat->tag = tag;
        fat->flags = FLAG_FAT;
        fat->size = size;
        memset(fat->data, 0, MAX_INLINE_SIZE);

        result.fat = TO_FAT(fat);
        g_fat_stats.alloc_fat++;
        g_fat_stats.bytes_inline += sizeof(ObjFat);
        g_fat_stats.bytes_saved += (sizeof(ObjThin) - sizeof(ObjFat));

        return result;
    } else {
        // Too big, allocate as thin
        ObjThin* thin = (ObjThin*)region_alloc(r, sizeof(ObjThin));
        thin->generation = 0;
        thin->tag = tag;
        thin->flags = FLAG_THIN;
        thin->size = size;
        thin->data = region_alloc(r, size);
        memset(thin->data, 0, size);

        result.thin = thin;
        g_fat_stats.alloc_thin++;
        g_fat_stats.alloc_failed_size++;
        g_fat_stats.bytes_heap += sizeof(ObjThin) + size;

        return result;
    }
}

// Check if object is fat
bool obj_is_fat(ObjUnified obj) {
    return IS_FAT(obj.fat);  // Bit test
}

// Get object size
size_t obj_size(ObjUnified obj) {
    if (obj_is_fat(obj)) {
        return TO_FAT(obj.fat)->size;
    } else {
        return obj.thin->size;
    }
}

// Get object tag
uint32_t obj_tag(ObjUnified obj) {
    if (obj_is_fat(obj)) {
        return TO_FAT(obj.fat)->tag;
    } else {
        return obj.thin->tag;
    }
}

// Read field from object (generic)
void* obj_get_field(ObjUnified obj, size_t offset, size_t size) {
    if (obj_is_fat(obj)) {
        ObjFat* fat = TO_FAT(obj.fat);
        if (offset + size > MAX_INLINE_SIZE) {
            // Field access out of bounds - should have been thinned
            fprintf(stderr, "Error: Field access beyond inline storage\n");
            return NULL;
        }
        g_fat_stats.fat_field_access++;
        g_fat_stats.fat_access++;
        return &fat->data[offset];
    } else {
        g_fat_stats.thin_field_access++;
        g_fat_stats.thin_access++;
        return (char*)obj.thin->data + offset;
    }
}

// Write field to object (generic)
bool obj_set_field(ObjUnified obj, size_t offset, void* value, size_t size) {
    if (obj_is_fat(obj)) {
        ObjFat* fat = TO_FAT(obj.fat);
        if (offset + size > MAX_INLINE_SIZE) {
            return false;  // Should have been thinned
        }
        memcpy(&fat->data[offset], value, size);
        return true;
    } else {
        memcpy((char*)obj.thin->data + offset, value, size);
        return true;
    }
}
```

---

## Phase 1: Always Fat Baseline

**Duration:** 2 weeks
**Goal:** Make all allocations fat pointers, ensure tests pass
**Success Criteria:** All existing tests pass with fat pointers

### Week 1: Core Implementation

**Task 1.1: Create fat pointer infrastructure**
- [ ] Create `src/runtime/obj_fat.h` with data structures
- [ ] Create `src/runtime/obj_fat.c` with core functions
- [ ] Add `obj_fat.o` to Makefile
- [ ] Compile and run basic smoke test

**Task 1.2: Update allocation path**
```c
// In src/runtime/runtime.c or allocation.c

// OLD:
Obj* alloc_object(Region* r, size_t size, Type* type) {
    Obj* obj = region_alloc(r, size);
    obj->tag = type->tag;
    return obj;
}

// NEW (Phase 1):
ObjUnified alloc_object(Region* r, size_t size, Type* type) {
    // Force all allocations to be fat (for now)
    if (size > MAX_INLINE_SIZE) {
        fprintf(stderr, "Error: Object too large for inline storage\n");
        fprintf(stderr, "       Size: %zu, Max: %d\n", size, MAX_INLINE_SIZE);
        exit(1);  // Will be relaxed in Phase 2
    }

    ObjUnified obj = alloc_obj_fat_first(r, size, type->tag);
    return obj;
}
```

**Task 1.3: Update access paths**
```c
// OLD:
Obj* get_pair_car(Obj* pair) {
    return pair->fields[0];
}

// NEW:
ObjUnified get_pair_car(ObjUnified pair) {
    void* field = obj_get_field(pair, 0, sizeof(ObjUnified));
    return *(ObjUnified*)field;
}
```

### Week 2: Integration and Testing

**Task 2.1: Update all object operations**
- [ ] `obj_car()`, `obj_cdr()` for pairs
- [ ] `mk_pair()`, `mk_list()` for constructors
- [ ] `obj_to_int()`, `obj_to_float()` for primitives
- [ ] `inc_ref()`, `dec_ref()` for RC operations

**Task 2.2: Run existing test suite**
```bash
# Run all tests with fat pointers
cd /home/heefoo/Documents/code/OmniLisp/runtime
make clean
make test-fat  # New target
./tests.sh
```

**Task 2.3: Benchmark baseline**
```c
// Create bench/bench_fat_baseline.c
void benchmark_fat_baseline() {
    fat_stats_init();

    // Run standard workloads
    benchmark_list_operations();
    benchmark_tree_operations();
    benchmark_gc_pressure();

    fat_stats_print();
}
```

**Deliverable:** Working fat pointer baseline with performance numbers

---

## Phase 2: Gradual Thinning

**Duration:** 2-3 weeks
**Goal:** Add thinning fallbacks based on specific criteria
**Success Criteria:** Large/escaping objects automatically thin

### Week 3: Size-Based Thinning

**Task 3.1: Implement automatic size-based thinning**
```c
// In obj_fat.c

// Relaxed allocation (allows large objects)
ObjUnified alloc_obj_with_thinning(Region* r, size_t size, uint32_t tag) {
    if (size <= MAX_INLINE_SIZE) {
        // Try fat first
        return alloc_obj_fat_first(r, size, tag);
    } else {
        // Automatically thin for large objects
        ObjThin* thin = (ObjThin*)region_alloc(r, sizeof(ObjThin));
        thin->generation = 0;
        thin->tag = tag;
        thin->flags = FLAG_THIN;
        thin->size = size;
        thin->data = region_alloc(r, size);

        g_fat_stats.fat_to_thin_size++;
        g_fat_stats.alloc_thin++;

        ObjUnified result;
        result.thin = thin;
        return result;
    }
}
```

**Task 3.2: Update allocation to use new path**
```c
// Replace alloc_object() with:
ObjUnified alloc_object(Region* r, size_t size, Type* type) {
    return alloc_obj_with_thinning(r, size, type->tag);
}
```

**Task 3.3: Test with large objects**
```lisp
;; Test large list
(define large-list (range 10000))  ;; Should auto-thin

;; Test nested structures
(define deep-tree (make-tree 100))  ;; Should stay fat if small enough
```

### Week 4: Escape-Based Thinning (Compile-Time)

**Task 4.1: Add escape detection to compiler**
```c
// In csrc/analysis/analysis.h

typedef enum {
    ESCAPE_NONE,      // Stays local
    ESCAPE_LOCAL,     // Escapes to local variable
    ESCAPE_REGION,    // Escapes region boundary
    ESCAPE_GLOBAL,    // Escapes to global
    ESCAPE_CLOSURE,   // Captured by closure
    ESCAPE_ADDRESS    // Address taken
} EscapeLevel;

typedef struct {
    EscapeLevel level;
    bool address_taken;
    bool captured;
    bool returned;
} EscapeInfo;

EscapeInfo analyze_escape(ASTNode* expr);
```

**Task 4.2: Generate thinning code**
```c
// In csrc/codegen/codegen.c

void generate_allocation(ASTNode* alloc_expr) {
    EscapeInfo escape = analyze_escape(alloc_expr);

    if (escape.level == ESCAPE_NONE) {
        // Safe for fat pointers
        emit_call("alloc_obj_fat_first");
    } else {
        // Force thin for escaping objects
        emit_call("alloc_obj_thin_forced");
    }
}
```

**Task 4.3: Test escape-based thinning**
```lisp
;; Should stay fat (no escape)
(define local-obj (make-point 1 2))

;; Should thin (escapes region)
(define global-obj (make-large-structure))

;; Should thin (captured)
(define captured (lambda [] (return local-obj)))
```

### Week 5: Runtime Thinning (Promotion)

**Task 5.1: Implement fat-to-thin promotion**
```c
// In obj_fat.c

ObjUnified promote_fat_to_thin(Region* r, ObjUnified fat_obj) {
    if (!obj_is_fat(fat_obj)) {
        return fat_obj;  // Already thin
    }

    ObjFat* fat = TO_FAT(fat_obj.fat);

    // Allocate thin version
    ObjThin* thin = (ObjThin*)region_alloc(r, sizeof(ObjThin));
    thin->generation = fat->generation;
    thin->tag = fat->tag;
    thin->flags = FLAG_THIN;
    thin->size = fat->size;
    thin->data = region_alloc(r, fat->size);
    memcpy(thin->data, fat->data, fat->size);

    g_fat_stats.fat_to_thin_escape++;

    ObjUnified result;
    result.thin = thin;
    return result;
}

// Called when address is taken
void* obj_get_address(Region* r, ObjUnified* obj_ptr) {
    ObjUnified obj = *obj_ptr;

    if (obj_is_fat(obj)) {
        // Promote to thin
        *obj_ptr = promote_fat_to_thin(r, obj);
        return (*obj_ptr).thin->data;
    } else {
        return obj.thin->data;
    }
}
```

**Task 5.2: Add address-taking detection**
```c
// In codegen:
void generate_address_of(ASTNode* addr_expr) {
    // If &obj is taken, force thinning
    emit_call("obj_get_address");
}
```

**Deliverable:** Working thinning system with statistics

---

## Phase 3: Compiler Integration

**Duration:** 2 weeks
**Goal:** Automatic fat/thin decisions based on escape analysis
**Success Criteria:** Compiler makes correct decisions without annotations

### Week 6: Escape Analysis Enhancement

**Task 6.1: Extend escape analysis**
```c
// In csrc/analysis/analysis.c

EscapeInfo analyze_allocation(ASTNode* alloc) {
    EscapeInfo info;
    info.level = ESCAPE_NONE;
    info.address_taken = false;
    info.captured = false;
    info.returned = false;

    // Analyze all uses of this allocation
    for (each use of alloc) {
        if (is_return(use)) {
            info.returned = true;
            info.level = ESCAPE_GLOBAL;
        }
        if (is_address_taken(use)) {
            info.address_taken = true;
            info.level = ESCAPE_ADDRESS;
        }
        if (is_closure_capture(use)) {
            info.captured = true;
            info.level = ESCAPE_CLOSURE;
        }
        if (is_region_cross(use)) {
            info.level = ESCAPE_REGION;
        }
    }

    return info;
}
```

**Task 6.2: Integrate with VarUsage**
```c
// Extend VarUsage struct
typedef struct {
    // ... existing fields ...
    EscapeInfo escape;
    bool prefer_fat;  // Compiler hint
    bool prefer_thin;
} VarUsage;
```

### Week 7: Code Generation

**Task 7.1: Generate optimized allocation paths**
```c
// In csrc/codegen/codegen.c

void generate_obj_allocation(ASTNode* node) {
    Type* type = node->alloc.type;
    size_t size = type_size(type);
    VarUsage* vu = get_var_usage(node);

    if (vu->escape.level == ESCAPE_NONE &&
        size <= MAX_INLINE_SIZE &&
        !vu->escape.address_taken) {
        // Safe for fat
        emit_alloc_fat(node);
    } else {
        // Force thin
        emit_alloc_thin(node);
    }
}
```

**Task 7.2: Generate optimized access paths**
```c
void generate_field_access(ASTNode* field) {
    VarUsage* vu = get_var_usage(field->obj);

    if (vu->prefer_fat) {
        // Direct inline access
        emit_inline_load(field);
    } else {
        // Indirect heap access
        emit_heap_load(field);
    }
}
```

**Deliverable:** Compiler making automatic fat/thin decisions

---

## Phase 4: Evaluation

**Duration:** 1 week
**Goal:** Measure impact and decide on full implementation
**Success Criteria:** Data-driven decision on fat pointers

### Week 8: Benchmarking and Decision

**Task 8.1: Comprehensive benchmark suite**
```c
// File: bench/bench_fat_evaluation.c

typedef struct {
    const char* name;
    void (*func)(void);
    const char* description;
} FatBenchmark;

FatBenchmark fat_benchmarks[] = {
    {"list_ops", benchmark_list_ops, "List creation and manipulation"},
    {"tree_ops", benchmark_tree_ops, "Tree creation and traversal"},
    {"gc_pressure", benchmark_gc_pressure, "Memory allocation pressure"},
    {"cache_behavior", benchmark_cache_behavior, "Cache hit rates"},
    {"real_workload", benchmark_real_workload, "Actual application code"},
    {NULL, NULL, NULL}
};

void run_fat_benchmarks() {
    printf("Running fat pointer evaluation benchmarks...\n\n");

    for (int i = 0; fat_benchmarks[i].name; i++) {
        printf("Benchmark: %s\n", fat_benchmarks[i].name);
        printf("Description: %s\n", fat_benchmarks[i].description);

        fat_stats_init();
        fat_benchmarks[i].func();
        fat_stats_print();

        printf("\n");
    }
}
```

**Task 8.2: Comparison with baseline**
```bash
# Compare fat pointer performance vs baseline
cd bench

# Baseline (no fat pointers)
./benchmark_baseline > baseline.txt

# Fat pointers (all fat)
./benchmark_fat_all > fat_all.txt

# Fat pointers (with thinning)
./benchmark_fat_thin > fat_thin.txt

# Generate comparison report
python3 compare_results.py baseline.txt fat_all.txt fat_thin.txt
```

**Task 8.3: Decision matrix**
```c
// Criteria for continuing with fat pointers

typedef struct {
    bool speedup_positive;      // At least one benchmark shows speedup
    bool no_major_slowdown;     // No benchmark >2x slower
    bool memory_reduction;      // Memory usage reduced
    bool cache_improvement;     // Cache hit rate improved
    bool tests_pass;            // All tests still pass
    bool maintainable;          // Code complexity acceptable
} DecisionCriteria;

DecisionCriteria evaluate_fat_pointers() {
    DecisionCriteria dc;

    // Analyze benchmark results
    dc.speedup_positive = check_speedup_positive();
    dc.no_major_slowdown = check_no_slowdown(2.0);  // Max 2x slowdown
    dc.memory_reduction = check_memory_reduction();
    dc.cache_improvement = check_cache_improvement();
    dc.tests_pass = check_all_tests_pass();
    dc.maintainable = check_code_complexity();

    return dc;
}

bool should_continue_fat_pointers() {
    DecisionCriteria dc = evaluate_fat_pointers();

    // Require: positive speedup, no major slowdowns, tests pass
    return dc.speedup_positive &&
           dc.no_major_slowdown &&
           dc.tests_pass;
}
```

**Task 8.4: Go/No-Go Decision**
```bash
# After benchmarks complete:
if should_continue_fat_pointers(); then
    echo "Fat pointers approved for production"
    echo "Proceeding with optimization..."
    # Continue with production implementation
else
    echo "Fat pointers not recommended"
    echo "Falling back to baseline implementation"
    # Revert to baseline
fi
```

**Deliverable:** Go/No-Go decision with data

---

## File Structure

### New Files

```
runtime/
├── src/runtime/
│   ├── obj_fat.h          # Fat pointer data structures
│   ├── obj_fat.c          # Fat pointer implementation
│   └── fat_stats.h        # Statistics tracking
├── csrc/analysis/
│   ├── escape_analysis.h  # Escape analysis for fat pointers
│   └── escape_analysis.c  # Escape analysis implementation
├── csrc/codegen/
│   ├── fat_codegen.h      # Fat pointer code generation
│   └── fat_codegen.c      # Fat pointer code generation
└── bench/
    ├── bench_fat_baseline.c    # Baseline benchmarks
    ├── bench_fat_thin.c        # Thinning benchmarks
    └── bench_fat_evaluation.c  # Final evaluation
```

### Modified Files

```
runtime/
├── src/runtime/
│   ├── runtime.c         # Update allocation path
│   ├── types.c           # Update type handling
│   └── memory.c          # Update memory operations
├── csrc/analysis/
│   └── analysis.c        # Add escape analysis
└── csrc/codegen/
    └── codegen.c         # Add fat/thin codegen
```

---

## Testing Strategy

### Unit Tests

```c
// File: tests/test_fat_pointers.c

void test_fat_allocation() {
    Region* r = region_create();

    // Allocate small object (should be fat)
    ObjUnified obj1 = alloc_object(r, 32, TAG_PAIR);
    assert(obj_is_fat(obj1));
    assert(obj_size(obj1) == 32);

    region_destroy(r);
}

void test_thin_allocation() {
    Region* r = region_create();

    // Allocate large object (should auto-thin)
    ObjUnified obj2 = alloc_object(r, 1000, TAG_ARRAY);
    assert(!obj_is_fat(obj2));
    assert(obj_size(obj2) == 1000);

    region_destroy(r);
}

void test_fat_field_access() {
    Region* r = region_create();

    ObjUnified obj = alloc_object(r, 16, TAG_PAIR);
    int value = 42;
    obj_set_field(obj, 0, &value, sizeof(int));

    int* result = obj_get_field(obj, 0, sizeof(int));
    assert(*result == 42);

    region_destroy(r);
}

void test_fat_to_thin_promotion() {
    Region* r = region_create();

    // Allocate as fat
    ObjUnified obj = alloc_object(r, 32, TAG_PAIR);
    assert(obj_is_fat(obj));

    // Promote to thin
    ObjUnified thin = promote_fat_to_thin(r, obj);
    assert(!obj_is_fat(thin));
    assert(obj_size(thin) == 32);

    region_destroy(r);
}
```

### Integration Tests

```lisp
;; File: tests/fat_pointers.omni

;; Test list operations
(define list-test
  (let [lst (list 1 2 3 4 5)]
    (assert (= (length lst) 5))
    lst))

;; Test nested structures
(define tree-test
  (let [tree (make-tree 10)]
    (assert (tree-valid? tree))
    tree))

;; Test large objects
(define large-test
  (let [large (make-array 1000)]
    (assert (= (length large) 1000))
    large))

;; Test escaping objects
(define escape-test
  (let [obj (make-point 1 2)]
    (return obj)  ;; Should force thinning
    ))
```

### Performance Tests

```c
// File: bench/bench_fat_performance.c

void benchmark_list_creation() {
    const int N = 100000;

    fat_stats_init();

    clock_t start = clock();

    ObjUnified list = NULL;
    for (int i = 0; i < N; i++) {
        list = cons(make_int(i), list);
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    printf("List creation: %.3f seconds\n", elapsed);
    printf("Objects created: %d\n", N);
    printf("Fat allocations: %ld\n", g_fat_stats.alloc_fat);
    printf("Thin allocations: %ld\n", g_fat_stats.alloc_thin);

    fat_stats_print();
}
```

---

## Success Criteria

### Phase 1 Success (Week 2)

- [ ] All existing tests pass with fat pointers
- [ ] No regressions in functionality
- [ ] Baseline performance numbers collected
- [ ] Code compiles without warnings

### Phase 2 Success (Week 5)

- [ ] Large objects automatically thin
- [ ] Escaping objects automatically thin
- [ ] Address-taking triggers thinning
- [ ] Statistics show thinning decisions
- [ ] Performance within 2x of baseline

### Phase 3 Success (Week 7)

- [ ] Compiler makes correct fat/thin decisions
- [ ] Escape analysis working accurately
- [ ] Code generation produces correct code
- [ ] No manual annotations needed

### Phase 4 Success (Week 8)

- [ ] Comprehensive benchmark suite complete
- [ ] Go/No-Go decision made with data
- [ ] If Go: Production implementation plan ready
- [ ] If No-Go: Clean rollback to baseline

### Overall Success

**Minimum Viable Success:**
- At least one benchmark shows >1.5x speedup
- No benchmark shows >2x slowdown
- All tests pass
- Code complexity remains manageable

**Optimal Success:**
- Multiple benchmarks show >2x speedup
- Memory usage reduced by >30%
- Cache hit rate improved by >20%
- Zero test failures
- Code is clean and maintainable

---

## Fallback Strategy

If at any point the fat pointer approach fails:

1. **Compile-time disable:**
   ```bash
   make NO_FAT_POINTERS=1
   ```

2. **Runtime disable:**
   ```c
   // In obj_fat.c
   ObjUnified alloc_object(...) {
       #ifdef DISABLE_FAT_POINTERS
           return alloc_thin_forced(...);
       #else
           return alloc_with_thinning(...);
       #endif
   }
   ```

3. **Clean rollback:**
   ```bash
   git revert <fat-pointer-commits>
   make clean
   make
   ```

**Key Point:** We can abort at ANY time without breaking existing code.

---

## Timeline Summary

| Week | Phase | Deliverable | Decision Point |
|------|-------|-------------|----------------|
| 1-2 | Phase 1 | Fat pointer baseline | Continue if tests pass |
| 3-5 | Phase 2 | Thinning system | Continue if performance OK |
| 6-7 | Phase 3 | Compiler integration | Continue if analysis works |
| 8 | Phase 4 | Evaluation | **GO/NO-GO DECISION** |

---

## Next Steps

1. **Review this plan** with stakeholders
2. **Create feature branch:** `git checkout -b feature/ablative-fat-pointers`
3. **Start Week 1 tasks** (Core implementation)
4. **Track progress** with TODO system
5. **Evaluate at each decision point**

---

## References

- Virgil ADT Unboxing: https://arxiv.org/html/2410.11094
- Selective Pointer Metadata Inlining: https://security.csl.toronto.edu/wp-content/uploads/2025/04/sxu_phdthesis_2025.pdf
- OmniLisp Performance Opportunities: `docs/PERFORMANCE_OPTIMIZATION_OPPORTUNITIES.md`
- OmniLisp Memory Safety Research: `docs/MEMORY_SAFETY_RESEARCH_UPDATE_2024_2025.md`

---

**Author:** Claude (AI Assistant)
**Date:** 2026-01-08
**Status:** Ready for Implementation
