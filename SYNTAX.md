# OmniLisp Comprehensive Syntax Specification

This document is the authoritative reference for OmniLisp syntax, merging the core language reference, standardized bracket semantics, and the Pika Grammar DSL.

---

## 1. The Character Calculus (Delimiters)

To ensure consistency, every delimiter has a fixed semantic "Charge".

| Character | Domain | Purpose |
| :--- | :--- | :--- |
| **`()`** | **Flow** | Execution of logic, function calls, special forms, and **Persistent Linked Lists**. |
| **`[]`** | **Slot** | Argument vectors, `let` bindings, contiguous **Realized Arrays**, and Path indexing. |
| **`{}`** | **Kind** | Type system names, annotations, and parametric type construction. |
| **`.`**  | **Path** | The access operator used to reach or locate data. Bridges Flow and Slot. |
| **`^`**  | **Tag**  | Out-of-band metadata, visibility hints, and compiler instructions. |
| **`#`**  | **Sign** | Reader tags for literals (Dicts, Formats). |

---

## 2. Bindings & Functions

### 2.1 Function Definitions
Functions use the **Argument Vector `[]`** to clearly separate names from logic.

```lisp
;; Standard definition
(define add [x y] (+ x y))

;; With type annotations (Kinds in {})
(define add [x {Int} y {Int}] {Int}
  (+ x y))

;; Lambda shorthand
(lambda [x] (* x x))
(fn [x] (* x x))
(-> [x y] (+ x y))
```

### 2.2 Local Bindings (`let`)
`let` uses flat, even-numbered forms inside the **Slot Bracket `[]`**.

```lisp
(let [x 10
      y 20]
  (+ x y))
```

### 2.3 Destructuring
```lisp
;; Basic destructuring in define or let
(define [a b c] [1 2 3])
(define [first .. rest] [1 2 3 4])

(let [[x y] [10 20]]
  (+ x y))
```

---

## 3. The Trinity of Sequences

| Type | Syntax | Nature |
| :--- | :--- | :--- |
| **List** | `'(1 2 3)` | Persistent, recursive, uses `()`. |
| **Array** | `[1 2 3]` | Contiguous, realized, indexed, uses `[]`. |
| **Iterator**| `(range 10)`| Transient, one-shot process, returns `{Iter T}`. |

### 3.1 Lazy Pipelines
Operations like `map` and `filter` return objects of type `{Iter T}`. They do not allocate storage unless explicitly "Materialized".

```lisp
(define lazy-flow (map inc (range 100)))  ; No allocation
(define realized  (array lazy-flow))      ; Materialized to Array []
(define linked    (list lazy-flow))       ; Materialized to List ()
```

---

## 4. Access & Mutation

### 4.1 Path-as-Locator
The dot `.` identifies a location in memory.

*   **Static**: `person.name` -> `(get person :name)`
*   **Dynamic**: `arr.[idx]` -> `(get arr idx)`
*   **Symbolic**: `obj.[:key]` -> `(get obj :key)`
*   **Functional**: `.name` -> `(lambda [x] x.name)`

### 4.2 Mutation Operators
*   **`set!`**: Modifies a **Name** (Binding). Returns the **Value**.
    *   `(set! x 10)`
*   **`put!`**: Modifies a **Slot** (Path). Returns the **Root Object**.
    *   `(put! person.age 30)`
*   **`update!`**: Transforms a **Slot** in-place. Returns the **Root Object**.
    *   `(update! player.hp dec)`
*   **`update`**: Transforms a **Slot** functionally. Returns a **New Object**.
    *   `(define p2 (update p1.x inc))`

---

## 5. Type System

### 5.1 Type Annotations vs. Construction
*   **Annotation**: `{Type}` marks a binding's type.
*   **Construction**: `(Ctor args)` builds a type at the value level.

```lisp
[x {Int}]                    ; simple annotation
{Option Int}                 ; Apply Option to Int
{Dict String Int}            ; Apply Dict to String and Int
{Option (List Int)}          ; Nested application
```

### 5.2 Higher-Kinded Types
Use nested `{}` when passing a type constructor (not a fully applied type).
```lisp
{Functor {List}}             ; Functor applied to List constructor
```

### 5.3 Explicit Type Arguments
Optional `[]` syntax for disambiguation.
```lisp
{Array [Float] 2}            ; Type param + dimension param
(None [Int])                 ; Explicit value-level type application
```

---

## 6. Pattern Matching

`match` uses an even number of forms for branches (pattern/result pairs).

```lisp
(match opt
  (Some v) (process v)
  None     default
  1        "one")

;; Guards
(match x
  (Some v :when (> v 10)) (big-process v)
  _                       "default")
```

---

## 7. Pika Grammar DSL

Define high-performance, left-recursive grammars directly in code.

### 7.1 Definition
```lisp
(define [grammar name]
  [rule-name clause]
  ...)
```

### 7.2 Clause Forms
| Clause | Description | Example |
| :--- | :--- | :--- |
| `"str"` | Literal string | `"hello"` |
| `(char c)` | Single character | `(char #\a)` |
| `(seq a b)` | Sequence | `(seq "a" "b")` |
| `(first a b)`| Ordered Choice | `(first "x" "y")` |
| `(ref rule)` | Rule reference | `(ref expr)` |
| `(* x)` | Zero-or-more | `(* (charset "0-9"))` |
| `(+ x)` | One-or-more | `(+ (charset "a-z"))` |
| `(? x)` | Optional | `(? "opt")` |
| `(charset p)`| Char range | `(charset "a-z")` |
| `(& x)` | Positive Lookahead | `(& "prefix")` |
| `(! x)` | Negative Lookahead | `(! "forbidden")` |
| `(label :L x)`| AST Node Label | `(label :val (ref factor))` |

### 7.3 Runtime API
*   `(pika-match grammar rule input)`: Returns matched string or nil.
*   `(pika-find-all grammar rule input)`: Returns list of non-overlapping matches.

---

## 8. Metadata (`^`)

The `^` tag is for "Extra-Logic" metadata (hints, visibility).
```lisp
(define ^:private ^:hot [version {Float} 1.0])
```