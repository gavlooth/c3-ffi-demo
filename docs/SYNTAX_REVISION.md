# OmniLisp Syntax Revision (Strict Character Calculus)

This document defines the definitive syntax for OmniLisp, adhering strictly to the **Character Calculus** rules and the **Uniform Definition** principle.

## 1. The Character Calculus Rules

| Character | Domain | Purpose |
| :--- | :--- | :--- |
| **`{}`** | **Kind** | Type annotations, blueprints, and the domain of static blueprints. |
| **`[]`** | **Slot** | Arguments, parameters, field lists, and data-carrying tuples (Data Domain). |
| **`()`** | **Flow** | Execution, value construction, and type-generating calculations (Execution Domain). |
| **`^`**  | **Metadata** | Out-of-band instructions, relationships, and constraints. |

---

## 2. Bindings & Function Definitions

Functions follow a strict Flow template. Parameters are Slots `[]` containing typed/untyped names.

### 2.1 Basic Definition
```lisp
;; (define [parameters] {return-type} (body))
(define [x {Int} y {Int}] {Int}
  (+ x y))
```

### 2.2 Diagonal Dispatch & Constraints (`^:where`)
To enforce that multiple arguments share the same type or to restrict generics.

```lisp
;; x and y MUST be the same T, where T is a Number
(define ^:where [T {Number}] 
  [x {T} y {T}] {T}
  (+ x y))
```

---

## 3. The Type System (Julia-Style)

All types are defined using the `(define ...)` Flow form.

### 3.1 Abstract Types
```lisp
(define ^:parent {Any} {abstract Number} [])
(define ^:parent {Number} {abstract Real} [])
```

### 3.2 Primitive Types (Bit-Width)
The bit-width is passed as Slot data `[]` to the constructor.

```lisp
(define ^:parent {Real} {primitive Float64} [64])
(define ^:parent {Integer} {primitive Int32} [32])
```

### 3.3 Composite Types (Structs)
Fields are defined in a Slot `[]`.

```lisp
;; Immutable Struct
(define ^:parent {Any} {struct Point}
  [x {Float64} y {Float64}])

;; Mutable Struct (Field-level)
(define {struct Player}
  [^:mutable hp {Int32} 
   name {String}])

;; Global Mutability Sugar
(define ^:mutable {struct AtomicState}
  [value {Any}])
```

### 3.4 Parametric Types & Variance
Parameters are Slots. Variance is metadata on the parameter.

```lisp
;; Parametric Struct with Covariant (+) T
(define {struct [^:covar T]} {List}
  [head {T} tail {List T}])

;; Contravariant (-) Consumer
(define {abstract [^:contra T]} {Consumer}
  (consume [self {Self} val {T}]))
```

---

## 4. Type Algebra (Flow Constructors)

Unions and Function Signatures are **Flow** calculations `()` that return a **Kind** `{}`.

### 4.1 Structural Unions
```lisp
;; constructor: (union [types...])
(union [{Int32} {String}])

;; Type Alias: naming the result of a Flow
(define {IntOrString} 
  (union [{Int32} {String}]))

;; Inline Usage
(define [val {(union [{Int32} {String}])}] {Nothing}
  (display val))
```

### 4.2 Function Kinds (First-Class Signatures)
```lisp
;; constructor: (fn [[params...] {return}])
(fn [[{Int32} {Int32}] {Int32}])

;; Usage in Higher-Order Functions
(define [f {(fn [[{Int32}] {Int32}])} x {Int32}] {Int32}
  (f (f x)))
```

---

## 5. Metadata (`^`)

Metadata provides instructions that do not affect the identity of the value/type.

*   `^:parent {Type}` - Inheritance relationship.
*   `^:where [Constraints]` - Type variable scoping/diagonal dispatch.
*   `^:mutable` - Mutability marker.
*   `^:covar` / `^:contra` - Variance markers on type parameters.
*   `^:seq` / `^:rec` - Binding behavior for `let`.

---

## 6. Control Structures

### 6.1 `match` (Special Form)
Branches are Slot pairs `[]` within the Flow.

```lisp
(match val
  [pattern result]
  [pattern :when guard result]
  [else default])
```

### 6.2 `let` (Local Bindings)
```lisp
(let [x 10 y {Int} 20]
  (+ x y))

;; Sequential let
(let ^:seq [x 1 y (+ x 1)]
  y)
```

---

## 7. The Bottom Type
The Kind representing a computation that never returns.

```lisp
---

# Implementation Plan

This plan outlines the steps to migrate the C compiler and runtime to the **Strict Character Calculus** and the **Julia-aligned Type System**.

## Phase 1: Core Uniform Definition (TOP PRIORITY)
**Goal:** Restore the "lost" syntax and align with the new Calculus rules in the analyzer.

1.  **Refactor `analyze_define` (`csrc/analysis/analysis.c`):**
    *   Handle `OMNI_TYPE_LIT` as the first argument to support `(define {struct Name} ...)` and `(define {abstract Name} ...)`.
    *   Implement the new parameter layout: `(define [params] {ret} (body))`.
    *   Extract metadata `^` attached to the `define` form or its components.
2.  **Update Type Registry:**
    *   Update `TypeDef` in `analysis.h` to store `parent` pointers and `bit_width`.
    *   Implement `omni_type_is_subtype(child, parent)` logic.
3.  **Codegen Alignment:**
    *   Update `csrc/codegen/codegen.c` to emit C code matching the new `define` signatures.
    *   Generate C `struct` or `typedef` based on `{primitive}` or `{struct}` definitions.

## Phase 2: The Type System Logic
**Goal:** Implement the semantics of the Julia-style type hierarchy.

1.  **Bit-Width Primitives:**
    *   Support `{primitive}` definitions by mapping them to C machine types (e.g., `[64]` -> `int64_t`).
2.  **Flow Constructors (Dynamic Types):**
    *   Implement `(union [types])` and `(fn [params] {ret})` as Flow functions that return Kind objects.
    *   Add Kind-equivalence checks in the analyzer.
3.  **Metadata-Driven Inheritance:**
    *   Process `^:parent {Type}` to populate the type graph.
    *   Detect and reject circular inheritance.

## Phase 3: Bindings & Control Structures
**Goal:** Align `let` and `match` with the new Slot `[]` semantics.

1.  **Enhanced `let` (`analyze_let`):**
    *   Support `(let [x {Type} val] ...)` triplets.
    *   Handle `^:seq` and `^:rec` metadata to switch between `let`, `let*`, and `letrec` behavior.
2.  **`match` Special Form:**
    *   Implement structural destructuring for `[]` (arrays) and `()` (constructors).
    *   Implement `:when` guard handling in the CFG builder.

## Phase 4: Advanced Type Features
**Goal:** Close the final gap with Julia.

1.  **`^:where` Constraints:**
    *   Implement diagonal dispatch (type-variable unification) in `omni_apply` analysis.
    *   Support Upper Bound checks: `[T {Number}]`.
2.  **Variance Analysis:**
    *   Use `^:covar` and `^:contra` to validate subtyping of parametric types.
3.  **Bottom Type:**
    *   Wire `{bottom}` to the analyzer's dead-code detection (post-divergence).

---

# Verification Plan

Each phase must pass the following regression tests:
1.  `tests/test_kinds.omni`: Basic annotation and `type?` checks.
2.  `tests/test_julia_flex.omni`: Subtyping and dispatch checks.
3.  New `tests/test_calculus.omni`: Verifying strict `{}`, `[]`, `()` boundaries.
```