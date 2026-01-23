# Undocumented & "Buried" Features

This document collects language features that are implemented in the runtime or compiler but are missing from the main `LANGUAGE_REFERENCE.md`. These features were found by auditing the codebase, git history, and test suites.

## 1. Module System

**Status:** Implemented in `src/runtime/eval/omni_eval.c`.

The module system provides namespace isolation with explicit exports.

### Syntax

```lisp
(module Name
  (export symbol1 symbol2 ...)
  
  (define (symbol1 x) ...)
  (define (private-func y) ...)
)
```

### Importing

```lisp
;; Import all exported symbols
(import Name)

;; Import with alias (creates a dict-like namespace object)
(import Name :as Alias)
(Alias.symbol1 arg)

;; Selective import
(import Name :only (symbol1))
```

---

## 2. Macro System

**Status:** Not yet implemented. Design documented here.

OmniLisp will support Scheme-style hygienic macros with pattern-based transformation rules.

### Syntax Transformers

Using OmniLisp's unified `define` with slot syntax:

```lisp
(define [syntax name]
  [literals symbol1 symbol2 ...]    ;; symbols that match literally (not as pattern vars)
  [(name pattern1) template1]
  [(name pattern2) template2])
```

**Note:** Keywords (`:foo`) desugar to quoted symbols (`'foo`). In OmniLisp, `:else` and `'else` are equivalent—both are just the symbol `else`. Use whichever is clearer in context.

### Examples

```lisp
;; Simple macro - no literals needed
(define [syntax when]
  [(when test body ...)
   (if test (do body ...) nil)])

;; Macro with literal keywords
;; 'else' matches the symbol literally, not as a pattern variable
(define [syntax cond]
  [literals else]
  [(cond (else result))
   result]
  [(cond (test result) clause ...)
   (if test result (cond clause ...))])

;; Multiple literals
(define [syntax case]
  [literals else =>]
  [(case val (else result))
   result]
  [(case val (datum => proc) clause ...)
   (if (eqv? val 'datum) (proc val) (case val clause ...))]
  [(case val (datum result) clause ...)
   (if (eqv? val 'datum) result (case val clause ...))])
```

### Pattern Syntax

| Pattern | Matches |
|---------|---------|
| `symbol` | Binds any expression to `symbol` |
| `literal` | Matches the literal symbol exactly (if in `[literals ...]`) |
| `(p1 p2 ...)` | Matches a list with elements matching p1, p2, etc. |
| `(p1 ... pN)` | `...` matches zero or more of the preceding pattern |
| `[p1 p2 ...]` | Matches an array/vector |

### Hygiene

Macros are hygienic by default—identifiers introduced by the macro cannot capture or be captured by identifiers in the macro use site. This prevents accidental variable shadowing without requiring manual `gensym`.

---

## 3. Type System Extensions

**Status:** Partially implemented in compiler analysis (`csrc/analysis/`) and runtime registry.

### Type Definitions (Julia-style via `define`)

Types are defined using the unified `define` form with Kind `{}` syntax, following Julia's type system conventions.

```lisp
;; Abstract type (cannot be instantiated)
(define {abstract Number})
(define ^:parent {Number} {abstract Integer})

;; Struct (product type) - immutable by default
(define {struct Point}
  [x {Int}]
  [y {Int}])

;; Struct with parent type
(define ^:parent {Shape} {struct Circle}
  [center {Point}]
  [radius {Float}])

;; Parametric struct (generics)
(define {struct [Pair T]}
  [first {T}]
  [second {T}])

;; Mutable struct
(define ^:mutable {struct Player}
  [hp {Int}]
  [name {String}])

;; Enum (sum type)
(define {enum Color} Red Green Blue)

;; Parametric enum
(define {enum [Option T]}
  (Some [value {T}])
  None)

;; Union type
(define {IntOrString} (union [{Int} {String}]))
```

### `define [grammar ...]` (Pika Parser)

Defines a PEG grammar for the Pika parser.

```lisp
(define [grammar arithmetic]
  [expr  (seq term (star (seq (or "+" "-") term)))]
  [term  (seq factor (star (seq (or "*" "/") factor)))]
  [factor (or number (seq "(" expr ")"))])
```

---

## 4. Meta-Programming (The Tower)

**Status:** Implemented in `src/runtime/tower/`.

OmniLisp is a multi-stage language. These primitives allow moving between evaluation stages.

| Primitive | Description |
| :--- | :--- |
| `(lift val)` | Lifts a value from the current stage to the next (e.g., value -> code). |
| `(run code)` | Compiles and executes code at the base level. |
| `(EM expr)` | "Escape to Meta" - Evaluates `expr` at the meta-level during compilation. |

**Example:**
```lisp
(define (power n x)
  (if (= n 0)
      (lift 1)
      (lift (* (unlift x) (unlift (power (- n 1) x))))))
```

---

## 5. Extended Mutation Primitives

**Status:** Implemented in `src/runtime/eval/omni_eval.c`.

Beyond simple `set!`, the runtime supports path-based mutation.

| Primitive | Syntax | Description |
| :--- | :--- | :--- |
| `put!` | `(put! obj.field val)` | Mutates a nested field/index. |
| `update!` | `(update! obj.field fn)` | Applies `fn` to the field in-place. |
| `update` | `(update obj.field fn)` | Functional update (returns new object). |
| `path` | `(path obj key1 key2)` | Manually constructs a path object. |

---

## 6. Scientific Computing (Planned/Unwired)

**Status:** Reserved syntax found in `SCIENTIFIC_COMPUTING_PLAN.md` and `examples/`.

| Feature | Syntax |
| :--- | :--- |
| Tensor | `(torch/tensor [1 2 3])` |
| Matrix Context | `(with-mat ...)` |
| Vector Context | `(with-vec ...)` |
| No Grad | `(with-no-grad ...)` |

---

## 7. Pattern Matching Extensions

**Status:** Implemented in `eval_match`.

The `match` form supports guards and array destructuring.

### Guards (`:when`)

```lisp
(match x
  [val :when (> val 10) "High"]
  [_ "Low"])
```

### Array Destructuring

```lisp
(match arr
  [[x y] (+ x y)]        ; Matches array of length 2
  [[x .. rest] x])       ; Matches head/tail
```
