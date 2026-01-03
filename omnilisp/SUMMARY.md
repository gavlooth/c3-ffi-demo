# Omnilisp: Summary of Design

Omnilisp is a modern Lisp dialect that synthesizes the best features of Scheme, Common Lisp, Julia, and Clojure, implemented on a "Collapsing Tower of Interpreters" (Purple).

## 1. Syntax & Aesthetics
*   **Pure S-Expressions:** Uses `()` for logic, `[]` for bindings/vectors, and `{}` for types/dicts.
*   **Dot Access:** `obj.field.subfield` reader macro for clean, nested property access.
*   **No Redundancy:** Only Symbols (no separate Keyword type). `:key` is sugar for `'key`.

## 2. Type System (Julia-inspired)
*   **Multiple Dispatch:** Functions are generic by default. Methods are dispatched on all arguments.
*   **Specificity Rules:** Julia-style method specificity with explicit ambiguity errors.
*   **Hierarchy:** Abstract types for organization, Concrete structs for data.
*   **Parametricity:** `{Vector Int}` for clear, typed containers.
*   **Immutability:** Structs are immutable by default; `{mutable ...}` for imperative needs.
*   **Named Args:** Passed as immutable named tuples; `&` separates positional from named `:key value` pairs and supports defaults.
*   **Default Params:** Positional defaults supported via `[name default]` or `[name Type default]`.

## 3. Control & Error Handling (Purple/Common Lisp)
*   **Delimited Continuations:** Native `prompt` and `control` primitives.
*   **Condition System:** Resumable errors (Restarts) built on continuations. No stack-unwinding `try/catch`.
*   **TCO:** Fully supported via trampolining.
*   **Green Threads:** Cooperative processes and channels built on continuations.

## 4. Power Tools
*   **Hygienic Macros:** Racket-style `syntax-case` for safe, powerful code generation.
*   **Pattern Matching:** Optima-style, extensible, and compiled for performance.
*   **Iterators & Loops:** Explicit `iterate(iter, state)` protocol with `for`/`foreach` macros.
*   **Arrays & Sequences:** Arrays are the primary mutable sequence; vectors are immutable by default. Tuples/lists are in Phase‑1, with other collections later. Sequence ops work on any iterator and `collect` builds vectors (`iter->list`/`iter->vector` conversions).
*   **Piping:** `|>` operator for readable data flows.

## 5. Module System
*   **Explicit:** One-file-per-module. No implicit `include`.
*   **Controlled:** `(import [Mod :as M :refer (f)])` for namespace sanity.
