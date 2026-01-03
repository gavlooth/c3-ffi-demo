# Omnilisp

**Omnilisp** is a multi-paradigm Lisp dialect designed to be the ultimate developer's tool. It combines the minimalism of **Scheme**, the industrial power of **Common Lisp**, the modern type system of **Julia**, and the data-driven elegance of **Clojure**.

Implemented via a **Collapsing Tower of Interpreters** (Purple), it provides first-class delimited continuations and high-level reflection without sacrificing performance.

## Key Design Pillars

*   **Syntax:** S-expressions with specialized brackets (`[]` for vectors, `{}` for types/dicts).
*   **Dispatch:** Full **Multiple Dispatch** on all arguments (Julia style).
*   **Types:** Abstract hierarchy with parametric types like `{Vector Int}`.
*   **Access:** Native **Dot Notation** (`obj.field.subfield`) for clean nested access.
*   **Control:** **Delimited Continuations** (`prompt`/`control`) and a resumable **Condition System**.
*   **Hygiene:** Fully **Hygienic Macros** using syntax objects.
*   **Matching:** **Optima-style** extensible pattern matching.

## Documentation

*   [DESIGN.md](./DESIGN.md) - Full technical specification.
*   [SUMMARY.md](./SUMMARY.md) - High-level feature overview.
*   [ROADMAP.md](./ROADMAP.md) - Implementation plan for `purple_go`.