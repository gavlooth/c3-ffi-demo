# Omnilisp

**Omnilisp** is a multi-paradigm Lisp dialect designed to be the ultimate developer's tool. It combines the minimalism of **Scheme**, the industrial power of **Common Lisp**, the modern type system of **Julia**, and the data-driven elegance of **Clojure**.

The repo currently ships a small C compiler/runtime subset. The rest of the language features are the intended design.

## Current Implementation (C Compiler)
*   **Core syntax:** lists `(...)`, quote `'x`, comments `; ...`, arrays `[...]`
*   **Special forms:** `define`, `lambda`/`fn`, `let`, `let*`, `if`, `do`/`begin`, `handle`/`perform`, `with-open-file`
*   **Bindings:** list-style `(let ((x 1) (y 2)) ...)`, array-style `(let [x 1 y 2] ...)`, destructuring `(define [a b c] xs)`
*   **Default params:** `(define (f [x default]) ...)` with named arguments `(f :x value)`
*   **Primitives:** `+ - * / %`, `< > <= >= =`, `cons car cdr empty?`, `print println`, `str`, `map filter reduce partial compose`
*   **Data types:** lists, arrays `[1 2 3]`, dicts `#{:a 1}` - dot notation `obj.field`
*   **Control flow:** algebraic effects (`handle`/`perform`/`resume`) for all error handling, `with-open-file` auto-close
*   **Truthiness:** only `false` and `nothing` are falsy; everything else is truthy (including numeric `0` and empty lists)

## Key Design Pillars (Planned)

*   **Syntax:** S-expressions with specialized brackets (`[]` for arrays, `{}` for types, `#{}` for dicts).
*   **Dispatch:** Full **Multiple Dispatch** on all arguments (Julia style).
*   **Types:** Abstract hierarchy with parametric types like `{Vector Int}`.
*   **Access:** Native **Dot Notation** (`obj.field.subfield`) for clean nested access.
*   **Control:** **Delimited Continuations** (`prompt`/`control`) and a resumable **Condition System**.
*   **Hygiene:** Fully **Hygienic Macros** using syntax objects.
*   **Matching:** **Optima-style** extensible pattern matching.
*   **Symbols:** No separate keyword type; `:key` is sugar for `'key`.

## Documentation

*   [DESIGN.md](./DESIGN.md) - Full technical specification.
*   [SUMMARY.md](./SUMMARY.md) - High-level feature overview.
*   [ROADMAP.md](./ROADMAP.md) - Implementation plan for OmniLisp.
