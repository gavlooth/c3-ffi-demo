# Type Specialization and Typed Arrays Design
## Julia-Level Performance for OmniLisp

**Status:** Design Document
**Created:** 2025-01-08
**Author:** AI Agent (with user guidance)

---

## Executive Summary

This document describes the implementation of **Julia-style type specialization** for OmniLisp. The goal is to eliminate boxing overhead for primitive operations by generating specialized native code for each type combination, similar to how Julia achieves high performance.

### The Problem

Currently, OmniLisp uses tagged pointers for primitive values:
- **Integers**: Unboxed (61-bit immediate values)
- **Floats**: Boxed (heap-allocated Obj with TAG_FLOAT)
- **Characters**: Unboxed (21-bit immediate values)
- **Booleans**: Unboxed (1-bit immediate values)

Float boxing costs ~18 ns per operation due to heap allocation and indirection.

### The Solution: Type Specialization

**"OmniLisp is not fast because of CTRR alone; it's fast because of function specialization and type inference."**

Instead of runtime tagged value operations:
1. **Infer types at compile time** via type inference
2. **Generate specialized machine code** for each type combination
3. **Keep values unboxed in registers** during computation
4. **Box only when necessary** (storing in heterogeneous containers)

For numeric-heavy code, this means operations like `(+)` become:
- `add_Int_Int(int64_t, int64_t) → int64_t`
- `add_Float_Float(double, double) → double`
- `add_Int_Float(int64_t, double) → double`

All using native CPU registers, no heap allocation.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                         OmniLisp Compiler                           │
├─────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  ┌──────────────────┐      ┌──────────────────┐                    │
│  │  Parser          │      │  Type Inference  │                    │
│  │  (AST)           │ ───▶ │  (TypeEnv)       │                    │
│  └──────────────────┘      └────────┬─────────┘                    │
│                                      │                               │
│                                      ▼                               │
│                           ┌─────────────────────┐                    │
│                           │ Specialization     │                     │
│                           │ Decision Engine    │                     │
│                           └────────┬───────────┘                    │
│                                    │                                │
│                    ┌───────────────┼───────────────┐                 │
│                    ▼               ▼               ▼                 │
│           ┌─────────────┐ ┌─────────────┐ ┌─────────────┐           │
│           │ Specialized │ │  Generic    │ │  Typed     │           │
│           │ Codegen     │ │  Fallback   │ │  Arrays    │           │
│           └─────────────┘ └─────────────┘ └─────────────┘           │
│                    │               │               │                 │
│                    └───────────────┼───────────────┘                 │
│                                   ▼                                 │
│                        ┌─────────────────────┐                       │
│                        │  C99 Emitter        │                       │
│                        └─────────────────────┘                       │
└─────────────────────────────────────────────────────────────────────┘
                                       │
                                       ▼
                              ┌─────────────────┐
                              │  Native Code    │
                              │  (unboxed)      │
                              └─────────────────┘
```

---

## Phase 1: Type Inference Enhancement

### Objective

Build a richer type inference system that can infer concrete types for expressions, not just TypeID enums.

### Current State

The existing `type_id` system provides coarse-grained types:
- `TYPE_ID_INT`, `TYPE_ID_FLOAT`, `TYPE_ID_PAIR`, etc.

### Enhancement: Concrete Types

```c
/* Enhanced type representation for specialization */

typedef enum {
    TYPE_KIND_PRIMITIVE,     /* Int, Float, Char, Bool */
    TYPE_KIND_ARRAY,         /* Typed array */
    TYPE_KIND_CLOSURE,       /* Function */
    TYPE_KIND_ANY,           /* Unknown/generic */
} TypeKind;

typedef enum {
    PRIMITIVE_INT64,
    PRIMITIVE_FLOAT64,
    PRIMITIVE_CHAR,
    PRIMITIVE_BOOL,
} PrimitiveType;

typedef struct ConcreteType {
    TypeKind kind;

    union {
        struct {
            PrimitiveType prim;
            int bit_width;      /* 32, 64, etc. */
        } primitive;

        struct {
            struct ConcreteType* element_type;
            int rank;           /* 1 for vector, 2 for matrix, etc. */
            bool is_mutable;    /* Array vs Vector */
        } array;

        struct {
            struct ConcreteType** param_types;
            int param_count;
            struct ConcreteType* return_type;
        } closure;
    };
} ConcreteType;
```

### Type Inference Rules

```lisp
;; Constant literals
(infer-type 42)     → ConcreteType(PRIMITIVE_INT64, 64)
(infer-type 3.14)   → ConcreteType(PRIMITIVE_FLOAT64, 64)
(infer-type \a)     → ConcreteType(PRIMITIVE_CHAR, 32)

;; Arithmetic operations
(infer-type (+ x y))
  where x: Int, y: Int → Int
  where x: Float, y: Float → Float
  where x: Int, y: Float → Float  (promotion)

;; Comparison operations
(infer-type (< x y)) → Bool

;; Array construction
(infer-type (array 10 {Float}))
  → ConcreteType(ARRAY, element=Float, rank=1)

;; Function definition
(infer-type (define (f [x {Int}]) {Int} body))
  → Closure(params=[Int], return=Int)
```

### Implementation Tasks

#### Task 1.1: Type Environment

**File:** `csrc/analysis/type_env.h`, `csrc/analysis/type_env.c`

Add a type environment that tracks concrete types for variables:

```c
typedef struct TypeBinding {
    char* var_name;
    ConcreteType* type;
    struct TypeBinding* next;
} TypeBinding;

typedef struct TypeEnv {
    TypeBinding* bindings;
    struct TypeEnv* parent;  /* For nested scopes */
} TypeEnv;

/* Create a new type environment */
TypeEnv* type_env_new(TypeEnv* parent);

/* Bind a variable to a type */
void type_env_bind(TypeEnv* env, const char* name, ConcreteType* type);

/* Lookup a variable's type */
ConcreteType* type_env_lookup(TypeEnv* env, const char* name);

/* Enter/exit nested scope */
TypeEnv* type_env_push(TypeEnv* env);
void type_env_pop(TypeEnv* env);
```

#### Task 1.2: Enhanced Type Inference

**File:** `csrc/analysis/type_infer.c`, `csrc/analysis/type_infer.h`

Implement type inference for expressions:

```c
/* Infer the type of an expression */
ConcreteType* infer_expr(AnalysisContext* ctx, TypeEnv* env, OmniValue* expr);

/* Infer the type of a binary operation */
ConcreteType* infer_binop(AnalysisContext* ctx, TypeEnv* env,
                         const char* op,
                         ConcreteType* left,
                         ConcreteType* right);

/* Check if two types are compatible */
bool type_is_compatible(ConcreteType* a, ConcreteType* b);

/* Compute the result type of a binary operation */
ConcreteType* compute_binop_result(const char* op,
                                   ConcreteType* left,
                                   ConcreteType* right);
```

---

## Phase 2: Specialization Decision Engine

### Objective

Decide which functions should be specialized and for which type signatures.

### Specialization Criteria

A function should be specialized when:
1. **All parameter types are known** (no generic parameters)
2. **The function is a primitive** (arithmetic, comparison, etc.)
3. **The function is "hot"** (called frequently) - for user functions
4. **Performance benefit > code size cost**

### Specialization Matrix

```lisp
;; Arithmetic primitives
(+ [Int Int] → Int)
(+ [Float Float] → Float)
(+ [Int Float] → Float)

;; Comparison primitives
(< [Int Int] → Bool)
(< [Float Float] → Bool)
(< [Int Float] → Bool)

;; User functions
(define (square [x {Int}]) {Int} (* x x))
  → Specialize: square_Int(Int64) → Int64
```

### Implementation Tasks

#### Task 2.1: Specialization Database

**File:** `csrc/codegen/spec_db.h`, `csrc/codegen/spec_db.c`

Track which specializations exist and are needed:

```c
typedef struct SpecSignature {
    char* func_name;           /* "add", "square", etc. */
    ConcreteType** param_types; /* Array of parameter types */
    int param_count;
    ConcreteType* return_type;
    char* mangled_name;        /* "add_Int_Int", "square_Float", etc. */
    bool is_generated;         /* Has code been emitted? */
    struct SpecSignature* next;
} SpecSignature;

typedef struct SpecDB {
    SpecSignature* signatures;
    /* Hash map for O(1) lookup */
    SpecSignature** sig_table;
    int table_size;
} SpecDB;

/* Create specialization database */
SpecDB* spec_db_new(void);

/* Register a specialization */
void spec_db_register(SpecDB* db,
                      const char* func_name,
                      ConcreteType** param_types,
                      int param_count,
                      ConcreteType* return_type);

/* Lookup existing specialization */
SpecSignature* spec_db_lookup(SpecDB* db,
                              const char* func_name,
                              ConcreteType** param_types,
                              int param_count);

/* Generate mangled name for specialized function */
char* spec_mangle_name(const char* base,
                       ConcreteType** types,
                       int count);

/* Check if a function should be specialized */
bool should_specialize(AnalysisContext* ctx,
                       const char* func_name,
                       ConcreteType** param_types,
                       int param_count);
```

#### Task 2.2: Specialization Decision Algorithm

**File:** `csrc/codegen/spec_decision.c`

```c
typedef enum {
    SPEC_ALWAYS,       /* Always specialize (arithmetic primitives) */
    SPEC_HOT,          /* Specialize if called frequently */
    SPEC_NEVER,        /* Never specialize (generic functions) */
    SPEC_MAYBE,        /* Decide based on heuristics */
} SpecPolicy;

SpecPolicy get_specialization_policy(AnalysisContext* ctx,
                                      const char* func_name);

/* Check if specialization is worth it */
bool is_worth_specializing(AnalysisContext* ctx,
                           const char* func_name,
                           ConcreteType** param_types,
                           int param_count);

/* Estimate performance benefit */
int estimate_speedup(ConcreteType** from_types,
                     ConcreteType** to_types,
                     int param_count);
```

---

## Phase 3: Specialized Code Generation

### Objective

Generate specialized C code for functions that use unboxed primitive values.

### Unboxed Representation

For specialized code, we use native C types directly:

```c
/* Instead of: Obj* add(Obj* a, Obj* b) { ... } */

/* Specialized version: */
int64_t add_Int_Int(int64_t a, int64_t b) {
    return a + b;
}

double add_Float_Float(double a, double b) {
    return a + b;
}

double add_Int_Float(int64_t a, double b) {
    return (double)a + b;
}
```

### Box/Unbox Primitives

At specialization boundaries, we need conversion functions:

```c
/* Box conversion (for returning to generic code) */
Obj* box_int(int64_t value) {
    /* Return immediate int (no allocation) */
    return MAKE_INT_IMM(value);
}

Obj* box_float(double value) {
    /* Allocate heap object for float */
    return mk_float(value);
}

/* Unbox conversion (for entering specialized code) */
int64_t unbox_int(Obj* obj) {
    if (IS_IMMEDIATE_INT(obj)) {
        return INT_IMM_VALUE(obj);
    }
    if (obj->tag == TAG_INT) {
        return obj->i;
    }
    /* Type error or runtime check */
    fprintf(stderr, "Type error: expected Int\n");
    exit(1);
}

double unbox_float(Obj* obj) {
    if (obj->tag == TAG_FLOAT) {
        return obj->f;
    }
    /* Try to convert int to float */
    if (IS_IMMEDIATE_INT(obj) || obj->tag == TAG_INT) {
        return (double)obj_to_int(obj);
    }
    fprintf(stderr, "Type error: expected Float\n");
    exit(1);
}
```

### Implementation Tasks

#### Task 3.1: Specialized Code Generator

**File:** `csrc/codegen/spec_codegen.c`, `csrc/codegen/spec_codegen.h`

```c
/* Generate specialized function */
void generate_specialized_function(CodeGenContext* ctx,
                                   SpecSignature* sig,
                                   OmniValue* func_body);

/* Generate function prologue with unboxed parameters */
void generate_spec_prologue(CodeGenContext* ctx,
                            SpecSignature* sig);

/* Generate function epilogue with boxed return value */
void generate_spec_epilogue(CodeGenContext* ctx,
                            SpecSignature* sig);

/* Generate unboxed operation */
void generate_unboxed_binop(CodeGenContext* ctx,
                            const char* op,
                            const char* left,
                            const char* right,
                            ConcreteType* result_type,
                            const char* result_var);

/* Generate box/unbox calls */
void generate_unbox(CodeGenContext* ctx,
                    const char* obj_var,
                    ConcreteType* type,
                    const char* unboxed_var);

void generate_box(CodeGenContext* ctx,
                  const char* unboxed_var,
                  ConcreteType* type,
                  const char* obj_var);
```

#### Task 3.2: Primitive Specializations

**File:** `runtime/src/primitives_specialized.c`

Generate specialized versions of all primitives:

```c
/* Arithmetic */
int64_t prim_add_Int_Int(int64_t a, int64_t b);
double prim_add_Float_Float(double a, double b);
double prim_add_Int_Float(int64_t a, double b);
double prim_add_Float_Int(double a, int64_t b);

int64_t prim_sub_Int_Int(int64_t a, int64_t b);
double prim_sub_Float_Float(double a, double b);
/* ... */

/* Comparison */
bool prim_lt_Int_Int(int64_t a, int64_t b);
bool prim_lt_Float_Float(double a, double b);
bool prim_lt_Int_Float(int64_t a, double b);
/* ... */

/* Math functions */
double prim_sin_Double(double x);
double prim_cos_Double(double x);
double prim_sqrt_Double(double x);
/* ... */
```

---

## Phase 4: Typed Arrays

### Objective

Implement typed arrays that store unboxed primitive values directly in contiguous memory.

### Motivation

Even with function specialization, storing values in generic arrays requires boxing:

```lisp
;; Current: Generic array boxes each element
(let ((arr (array 100)))
  (array-set arr 0 42)    ;; Boxes 42 into Obj*
  (array-set arr 1 3.14)) ;; Boxes 3.14 into Obj*

;; Desired: Typed array stores raw values
(let ((arr (typed-array 100 {Float})))
  (array-set arr 0 3.14)) ;; Stores double directly
```

### Design

```c
/* Typed array representation */

typedef enum {
    ARRAY_TYPE_INT64,
    ARRAY_TYPE_FLOAT64,
    ARRAY_TYPE_CHAR,
    ARRAY_TYPE_BOOL,
} ArrayElementType;

typedef struct TypedArray {
    ArrayElementType element_type;
    int rank;           /* 1 for vector, 2 for matrix, etc. */
    int* dimensions;    /* Size of each dimension */
    int total_size;
    void* data;         /* Raw data: int64_t[], double[], etc. */
    Region* region;     /* Owning region */
} TypedArray;

/* Creation */
TypedArray* typed_array_new(Region* r,
                            ArrayElementType elem_type,
                            int rank,
                            int* dimensions);

/* Access - unboxed operations */
int64_t typed_array_get_int(TypedArray* arr, int* indices);
double typed_array_get_float(TypedArray* arr, int* indices);

void typed_array_set_int(TypedArray* arr, int* indices, int64_t value);
void typed_array_set_float(TypedArray* arr, int* indices, double value);

/* Conversion */
TypedArray* typed_array_from_list(Region* r,
                                  ArrayElementType elem_type,
                                  Obj* list);
```

### OmniLisp Syntax

```lisp
;; Typed array construction
(typed-array 100 {Float64})     ;; 100-element float array
(typed-array [10 20] {Int64})   ;; 10x20 integer matrix

;; Array comprehension
(for [x (range 100)]
  (collect x (typed-array 100 {Int64})))

;; Operations on typed arrays
(map (fn [x {Float}] {Float} (* x 2.0))
     (typed-array 100 {Float}))
```

### Implementation Tasks

#### Task 4.1: Typed Array Runtime

**File:** `runtime/src/typed_array.h`, `runtime/src/typed_array.c`

```c
/* Create typed array */
TypedArray* omni_typed_array_create(Region* r,
                                    const char* type_name,
                                    int rank,
                                    int* dimensions);

/* Element access */
Obj* omni_typed_array_ref(TypedArray* arr, int* indices);
void omni_typed_array_set(TypedArray* arr, int* indices, Obj* value);

/* Typed array operations */
TypedArray* omni_typed_array_map(Obj* func, TypedArray* arr);
TypedArray* omni_typed_array_filter(Obj* pred, TypedArray* arr);
Obj* omni_typed_array_reduce(Obj* func, Obj* init, TypedArray* arr);

/* Conversion to/from generic arrays */
Obj* omni_typed_array_to_list(TypedArray* arr);
TypedArray* omni_list_to_typed_array(Region* r,
                                     const char* type_name,
                                     Obj* list);
```

#### Task 4.2: Typed Array Codegen

**File:** `csrc/codegen/typed_array_codegen.c`

```c
/* Generate typed array construction */
void generate_typed_array_alloc(CodeGenContext* ctx,
                                const char* var_name,
                                const char* type_name,
                                int rank,
                                int* dimensions);

/* Generate typed array access */
void generate_typed_array_get(CodeGenContext* ctx,
                              const char* result_var,
                              const char* array_var,
                              const char** indices,
                              ConcreteType* elem_type);

void generate_typed_array_set(CodeGenContext* ctx,
                              const char* array_var,
                              const char** indices,
                              const char* value_var,
                              ConcreteType* elem_type);
```

---

## Phase 5: Gradual Typing Integration

### Objective

Allow specialized and generic code to coexist, with fallback to generic operations when types are unknown.

### Design Principles

1. **Specialize when possible** - Use specialized functions for known types
2. **Fall back gracefully** - Use generic operations for unknown types
3. **No runtime penalty** - Type checks are compile-time only
4. **User control** - Allow annotations to force or prevent specialization

### Syntax

```lisp
;; Explicit type annotation - force specialization
(define (compute [x {Float64}] {Float64})
  (* x 2.0))

;; No annotation - generic (no specialization)
(define (compute x)
  (* x 2.0))

;; Mixed - specialize when types known, generic otherwise
(define (process x)
  (if (type? x Float64)
      (compute x)        ;; Specialized call
      (generic-mul x 2))) ;; Generic call
```

### Implementation

```c
/* Generate function call with type-based dispatch */
void generate_dispatch_call(CodeGenContext* ctx,
                            const char* func_name,
                            const char** args,
                            ConcreteType** arg_types,
                            int arg_count);

/* Generate specialized call (when types known) */
void generate_specialized_call(CodeGenContext* ctx,
                               SpecSignature* sig,
                               const char** args);

/* Generate generic call (when types unknown) */
void generate_generic_call(CodeGenContext* ctx,
                           const char* func_name,
                           const char** args);
```

---

## Phase 6: Integration with Existing Systems

### CTRR Memory Management

Specialized functions must still respect the CTRR memory management contract:

1. **No allocation in hot paths** - Specialized code shouldn't allocate
2. **Region-aware boxing** - When boxing, use the current region
3. **Cleanup at boundaries** - Unbox and cleanup when leaving specialized code

### Multiple Dispatch

Specialization integrates with the existing multiple dispatch system:

```lisp
;; Multiple definitions with different type signatures
(define process [x {Int}] {String}
  "int specialized")

(define process [x {Float}] {String}
  "float specialized")

(define process [x] {String}  ;; Fallback
  "generic")
```

The compiler:
1. Collects all definitions
2. Specializes the typed versions
3. Keeps the generic version as fallback

---

## Performance Expectations

### Benchmarks

Expected speedup for numeric operations:

| Operation | Current (boxed) | Specialized | Speedup |
|-----------|----------------|-------------|---------|
| Float add | ~50 ns | ~2 ns | 25x |
| Float mul | ~50 ns | ~2 ns | 25x |
| Sin/cos | ~100 ns | ~20 ns | 5x |
| Array access | ~30 ns | ~1 ns | 30x |
| Array store | ~50 ns | ~1 ns | 50x |

### Code Size Impact

- **Specialized primitives**: ~2-3KB per primitive type combination
- **100 primitive specializations**: ~200-300KB
- **Trade-off**: Small code size increase for massive performance gain

---

## Implementation Order

### Priority 1: Core Infrastructure
1. Type environment and enhanced type inference
2. Specialization database and decision engine
3. Basic arithmetic primitive specializations (Int, Float)

### Priority 2: Full Primitive Coverage
4. All arithmetic primitives (+, -, *, /, mod)
5. Comparison primitives (<, >, <=, >=, =)
6. Math library (sin, cos, sqrt, etc.)

### Priority 3: Typed Arrays
7. Typed array runtime implementation
8. Typed array codegen
9. Array operations (map, filter, reduce)

### Priority 4: User Function Specialization
10. User function specialization
11. Gradual typing integration
12. Performance heuristics and hot path detection

---

## Testing Strategy

### Unit Tests

For each specialization:
```lisp
;; Test that specialized version produces same result as generic
(test-equal (add-specialized 2.0 3.0)
           (add-generic 2.0 3.0))
```

### Performance Tests

Benchmark suite to verify speedup:
```lisp
;; Numeric computation benchmark
(define (benchmark-float-ops n)
  (let ((result 0.0))
    (for [i (range n)]
      (set! result (+ result (* i 1.5))))
    result))

;; Should be ~25x faster with specialization
```

### Regression Tests

Ensure existing functionality isn't broken:
```lisp
;; Dynamic code should still work
(define (dynamic-add x y)
  (+ x y))  ;; No types - should use generic version
```

---

## Documentation

### For Users

- How to write type-annotated functions
- When to use typed arrays
- Performance best practices

### For Developers

- Type inference algorithm
- Specialization decision process
- Code generation patterns

---

## Open Questions

1. **How aggressive should specialization be?**
   - Only primitives? (Phase 1)
   - User functions too? (Phase 4)
   - Automatically detect hot paths? (Future)

2. **How to handle parametric types?**
   - `(Array {Int})` vs `(Array {Float})`
   - Can we specialize based on type parameters?

3. **Inline specialization?**
   - Should we inline small specialized functions?
   - Integration with existing optimization passes

4. **JIT vs AOT?**
   - Current design is AOT (compile-time)
   - Could we add JIT for truly dynamic code?

---

## References

- Julia Type System: [A Type System for Julia](https://arxiv.org/abs/2009.07583)
- Julia Internals: https://docs.julialang.org/en/v1/devdocs/
- LuaJIT FFI: Trace compiler for dynamic languages
- Cranelift: Codegen framework with type specialization

---

*Document Status: Ready for Implementation*
*Next Step: Begin Phase 1 (Type Inference Enhancement)*
