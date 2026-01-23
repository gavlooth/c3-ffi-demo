# Macro Architecture

This document catalogs core macros in OmniLisp and their generated code patterns for static analysis tools.

**Last Updated:** 2026-01-18

---

## Overview

OmniLisp uses macros extensively for:
1. Tagged pointer manipulation (type checking, conversion)
2. Reference counting (atomic/non-atomic)
3. Memory safety checks (IPGE generation validation)
4. Region operations

Static analysis tools may report unresolved edges for macro-generated calls. This document maps macros to their generated code.

---

## Tagged Pointer Macros

### Type Checking Macros

| Macro | Expansion | Purpose |
|-------|-----------|---------|
| `IS_IMMEDIATE(p)` | `(GET_IMM_TAG(p) != IMM_TAG_PTR)` | Check if value is immediate (not heap) |
| `IS_IMMEDIATE_INT(p)` | `(GET_IMM_TAG(p) == IMM_TAG_INT)` | Check for immediate integer |
| `IS_IMMEDIATE_CHAR(p)` | `(GET_IMM_TAG(p) == IMM_TAG_CHAR)` | Check for immediate character |
| `IS_IMMEDIATE_BOOL(p)` | `(GET_IMM_TAG(p) == IMM_TAG_BOOL)` | Check for immediate boolean |
| `IS_BOXED(p)` | `(GET_IMM_TAG(p) == IMM_TAG_PTR && (p) != NULL)` | Check for heap-allocated object |

### Tag Extraction

```c
#define IMM_TAG_MASK     0x7ULL
#define GET_IMM_TAG(p)   (((uintptr_t)(p)) & IMM_TAG_MASK)
```

### Value Construction

| Macro | Purpose | Example |
|-------|---------|---------|
| `MAKE_INT_IMM(n)` | Create immediate integer | `MAKE_INT_IMM(42)` → tagged pointer |
| `MAKE_CHAR_IMM(c)` | Create immediate character | `MAKE_CHAR_IMM('a')` → tagged pointer |
| `OMNI_TRUE` / `OMNI_FALSE` | Boolean constants | Pre-computed tagged values |

### Value Extraction

| Macro | Purpose |
|-------|---------|
| `INT_IMM_VALUE(p)` | Extract integer from immediate |
| `CHAR_IMM_VALUE(p)` | Extract char from immediate |
| `IS_TRUE(p)` / `IS_FALSE(p)` | Boolean comparison |

---

## Reference Counting Macros

### Thread-Safe RC (Default)

```c
#define ATOMIC_INC_REF(o) inc_ref(o)
#define ATOMIC_DEC_REF(o) dec_ref(o)
```

**Generated calls:** `inc_ref()`, `dec_ref()`

### Thread-Local Optimization

When `OMNI_SINGLE_THREADED` is defined:
```c
#define ATOMIC_INC_REF(o) do { if ((o) && IS_BOXED(o)) (o)->rc++; } while(0)
#define ATOMIC_DEC_REF(o) do { if ((o) && IS_BOXED(o)) (o)->rc--; } while(0)
```

**Generated calls:** Direct field access (no function call)

---

## IPGE Generation Macros

In-Place Generational Evolution for memory safety.

### Generation Check

```c
#define IPGE_CHECK_VALID(obj, stored_gen) \
    ((obj)->generation == (stored_gen))
```

### Generation Evolution

```c
#define IPGE_EVOLVE(gen) \
    (((gen) * IPGE_LCG_A + IPGE_LCG_C) & IPGE_LCG_MASK)
```

**Generated calls:** Inline arithmetic (no function calls)

---

## Region Operation Macros

### Inline Allocation Fast Path

```c
// In region_core.h - region_alloc() is static inline
static inline void* region_alloc(Region* r, size_t size) {
    // Fast path: inline buffer
    if (size <= REGION_INLINE_MAX_ALLOC) {
        // Bump allocation in inline buffer
    }
    // Slow path: arena allocation
    return arena_alloc(&r->arena, size);
}
```

**Generated calls:** `arena_alloc()`

### Splice Soundness Check

```c
static inline bool region_can_splice_arena_only(const Region* r) {
    return r && (r->inline_buf.offset == 0);
}
```

---

## CodeGraph Resolution Guide

For static analysis tools, map these macro expansions:

| Pattern in Source | Actual Call |
|-------------------|-------------|
| `IS_BOXED(x) && x->tag == ...` | Type check sequence |
| `ATOMIC_INC_REF(x)` | `inc_ref(x)` |
| `ATOMIC_DEC_REF(x)` | `dec_ref(x)` |
| `region_alloc(r, size)` | Inline → `arena_alloc()` |
| `MAKE_INT_IMM(n)` | Inline bitwise ops |

### Unresolved Edge Categories

1. **Immediate value macros** - No function calls, pure bitwise ops
2. **Inline static functions** - Calls to `arena_alloc`, etc.
3. **Conditional compilation** - `#ifdef OMNI_SINGLE_THREADED` variants

---

## Tag Constants

```c
typedef enum {
    TAG_INT = 0,
    TAG_FLOAT,
    TAG_SYM,
    TAG_CELL,
    TAG_CLOSURE,
    TAG_SPECIAL,
    TAG_PAIR,
    TAG_BOX,
    TAG_CHANNEL,
    TAG_ATOM,
    TAG_THREAD,
    TAG_ARRAY,
    TAG_DICT,
    TAG_STRING,
    TAG_KEYWORD,
    TAG_GENERIC,
    TAG_KIND,
    TAG_NOTHING,
    TAG_EFFECT_TYPE,
    TAG_RESUMPTION,
    TAG_HANDLER,
    TAG_CONDITION,
    TAG_GENERATOR
} ObjTag;

#define TAG_USER_BASE 1000  // Start of user-defined types
```

---

## Performance Implications

| Macro Category | Overhead | Notes |
|----------------|----------|-------|
| Type checks | 0 cycles | Compile-time constant folding |
| Immediate construction | ~1 cycle | Single bitwise OR |
| RC operations | ~5-20 cycles | Atomic vs non-atomic |
| Region alloc (inline) | ~3 cycles | Bump pointer |
| Region alloc (arena) | ~50 cycles | May trigger mmap |
