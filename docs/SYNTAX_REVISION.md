# OmniLisp Syntax Revision (Recovered from Legacy Design)

This document collects syntax patterns and design decisions that were present in previous iterations (specifically commit `5f132fc`) but have since been lost or deprecated in the current codebase.

**Purpose:** To serve as a reference for revising the current implementation and documentation to align with the intended design.

---

## 1. Type System (Julia-Style)

**Core Principle:** Types are optional, Julia-style, and use `{}` braces.

### 1.1 Uniform Definition Syntax

The `(define ...)` form unifies all top-level definitions.

#### Abstract Types
```lisp
(define {abstract Animal} Any)
(define {abstract Mammal} Animal)
```

#### Concrete Structs (Immutable Default)
```lisp
(define {struct Point}
  [x {Float}]
  [y {Float}])

;; With Parent Type (Metadata Syntax)
(define ^:parent {Shape} {struct Circle}
  [center {Point}]
  [radius {Float}])
```

#### Parametric Types
```lisp
(define {struct [Pair T]}
  [first {T}]
  [second {T}])

;; Parametric with Parent
(define ^:parent {Any} {struct [Entry K V]}
  [key {K}]
  [value {V}])
```

#### Mutable Structs
```lisp
(define {struct Player}
  [^:mutable hp {Int}]  ; Field-level mutability
  [name {String}])

;; Whole struct mutable sugar
(define ^:mutable {struct Player} ...)
```

#### Enums (Sum Types)
```lisp
(define {enum Color} Red Green Blue)

(define {enum Option T}
  (Some [value {T}])
  None)
```

---

## 2. Function Definitions & Signatures

### 2.1 Parameter Forms
| Syntax | Meaning |
|--------|---------|
| `x` | Untyped parameter |
| `[x {Int}]` | Typed parameter |
| `[x 10]` | Parameter with default value |
| `[x {Int} 10]` | Typed parameter with default |

### 2.2 Return Type Annotation
Placed after the parameter vector.

```lisp
(define (square [x {Float}]) {Float}
  (* x x))
```

### 2.3 Named Arguments (`&`)
Named arguments follow `&` and do not affect dispatch.

```lisp
(define (draw shape & [color :black] [stroke {Int} 1]) ...)

;; Call
(draw circle & :color :red)
```

### 2.4 Variadic Functions (`..`)
```lisp
(define (sum .. nums) ...)
```

---

## 3. Bindings (`let`)

Unified `let` using vector `[]` for bindings. Modifiers via metadata.

### 3.1 Parallel (Default)
```lisp
(let [x 1 y 2] ...)
```

### 3.2 Sequential (`let*` behavior)
```lisp
(let ^:seq [x 1 y (+ x 1)] ...)
```

### 3.3 Recursive (`letrec` behavior)
```lisp
(let ^:rec [even? (lambda ...) odd? (lambda ...)] ...)
```

### 3.4 Loop (`named let`)
```lisp
(let ^:seq loop [i 0] (recur ...))
```

---

## 4. Control Structures

### 4.1 `match` (Special Form)
Uses `[]` for branches.

```lisp
(match val
  [pattern result]
  [pattern :when guard result]
  [else default])
```

### 4.2 `cond` (Macro)
Expands to `match`.

```lisp
(cond
  [condition result]
  [else default])
```

---

## 5. Metadata (`^`)

Metadata is attached via `^` or `^:` syntax.

*   `^:mutable` - Mutable fields/bindings.
*   `^:parent {Type}` - Type inheritance.
*   `^:seq`, `^:rec` - Let binding behavior.
*   `^:private` - Visibility.

---

## 6. Reader Macros (Planned)

*   `#(...)` - Anonymous function: `#(+ % 1)`
*   `#?` - Reader conditional: `#? (:linux (use-linux) :else (use-generic))`
*   `#r"..."` - Regex
*   `#raw"..."` - Raw string
*   `#b"..."` - Byte array

---

## 7. Tower Semantics (Staging)

*   `(lift val)` - Value to code.
*   `(run code)` - Execute code.
*   `(EM expr)` - Escape to Meta (eval at parent level).
*   `(clambda args body)` - Compile lambda.

---

## 8. Concurrency (Green Threads)

*   `(spawn thunk)` - Create green thread.
*   `(yield)` - Cooperative yield.
*   `(channel n)` / `(send c v)` / `(recv c)` - CSP.

---

## 9. Modules

```lisp
(module Name
  (export f1 Type)
  (define (f1) ...)
  (define {struct Type} ...))

(import Name)
(import [Name :as N])
```

 │ 226 +                                                                                                                                                                                                          │
│ 227 + ### 10.3 Ownership Metadata                                                                                                                                                                              │
│ 228 + *   `^:consumed` - Parameter ownership transferred to callee.                                                                                                                                            │
│ 229 + *   `^:borrowed` - Parameter is borrowed (default).               
