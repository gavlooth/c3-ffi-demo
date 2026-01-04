# Omnilisp: Summary of Design

Omnilisp is a modern Lisp dialect that synthesizes the best features of Scheme, Common Lisp, Julia, and Clojure. This repo currently ships a small C compiler/runtime subset; the rest is the target design.

See also:
- `DESIGN.md` for the full specification.
- `SYNTAX.md` for exhaustive examples.
- `DESIGN_DECISIONS.md` for the decision log.

## Implemented (C Compiler)
*   **Core syntax:** lists `(...)`, quote `'x`, comments `; ...`
*   **Special forms:** `define`, `lambda`/`fn`, `let`, `let*`, `if`, `do`/`begin`
*   **Bindings:** list-style `(let ((x 1) (y 2)) ...)` and array-style `(let [x 1 y 2] ...)`
*   **Primitives:** `+ - * / %`, `< > <= >= =`, `cons car cdr null?`, `display print newline`
*   **Truthiness (current runtime):** empty list and numeric zero are false; everything else is truthy

## Planned Design (Not Yet Implemented)
### Syntax & Aesthetics (planned)
*   **Bracket roles:** `()` forms, `[]` bindings/arrays/patterns, `{}` types, `#{}` dicts; quote preserves bracketed literals.
*   **Access:** `obj.field.subfield` reader macro with UFCS-style calls.
*   **Symbols only:** no keyword type; `:key` is sugar for `'key`.
*   **Reader macros:** `#(...)` lambda shorthand, `#?` conditionals, `#r/#raw/#b` strings, `#_` discard, `#| |#` block comments, `#!` shebang, `#uuid/#path` typed literals.
*   **Lambdas:** `->` shorthand for explicit-arg lambdas; `|>` for pipelines.
*   **No `nil`:** `nothing` is unit/void, empty collections are distinct values.

### Type System (planned, Julia-inspired)
*   **Multiple dispatch:** generic functions dispatched on all arguments with Julia-style specificity.
*   **Hierarchy:** abstract types, structs, and enums; parent type via `^:parent {Type}` metadata before `{struct ...}`.
*   **Mutability:** structs immutable by default; per-field `^:mutable` (`:mutable` sugar) with `{mutable ...}` sugar.
*   **Ownership hints:** `^:borrowed`, `^:consumes`, `^:noescape`; plus `^:tailrec`, `^:unchecked`, `^:deprecated`.
*   **Named args:** `&` separates positional from named args (positional before `&`); constructors use the same rules.
*   **Enums:** variants unqualified when unique, otherwise `Type.Variant`.
*   **Defaults:** `[name default]` or `[name Type default]` for parameters.

### Control & Error Handling (planned)
*   **Truthiness:** only `false` and `nothing` are falsy.
*   **Let modifiers:** `^:seq`/`^:rec` metadata.
*   **Continuations:** native `prompt`/`control` plus TCO via trampolining.
*   **Errors:** `error` + single `restart-case` in phase 1; full condition system deferred.
*   **Concurrency:** cooperative green threads and channels.

### Power Tools (planned)
*   **Hygienic macros:** Racket-style `syntax-case`.
*   **Pattern matching:** guards, `satisfies` predicates, and constructor patterns.
*   **Iterators & loops:** `iterate` protocol with `for`/`foreach` (multiple bindings are nested).
*   **Sequences:** arrays are the primary mutable sequence; sequence ops work on any iterator.

### Module System (planned)
*   **Explicit:** one-file-per-module, no implicit `include`.
*   **Controlled:** `(import [Mod :as M :refer (f)])` for namespace sanity.
