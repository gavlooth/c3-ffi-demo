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

**Status:** Implemented in `src/runtime/eval/omni_eval.c`.

OmniLisp supports hygienic macros using `syntax-rules` style pattern matching.

### Syntax Transformers

```lisp
(define-syntax name
  (syntax-rules (literal1 literal2 ...)
    ((name pattern1) template1)
    ((name pattern2) template2)))
```

**Example:**
```lisp
(define-syntax when
  (syntax-rules ()
    ((when test stmt1 stmt2 ...)
     (if test
         (begin stmt1 stmt2 ...)
         nothing))))
```

### `define [syntax ...]` (Alternative)

There is also a `define` format for syntax transformers:

```lisp
(define [syntax my-macro]
  [literals (else)]
  [(my-macro pattern) template])
```

---

## 3. Type System Extensions

**Status:** Partially implemented in compiler analysis (`csrc/analysis/`) and runtime registry.

### `deftype` and `defstruct`

Used to define new data types and structures.

```lisp
;; Define a product type (struct)
(defstruct Point (x Int) (y Int))

;; Define a sum type (tagged union)
(deftype Option
  (Some value)
  (None))
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
