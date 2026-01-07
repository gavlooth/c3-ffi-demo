# OmniLisp

**OmniLisp** is a high-performance, multi-paradigm Lisp dialect designed for systems programming and modern application development. It combines the minimalism of **Scheme**, the industrial power of **Common Lisp**, the modern type system of **Julia**, and the data-driven elegance of **Clojure**, all built on a foundation of **ASAP (As Static As Possible)** memory management.

## Key Design Pillars

*   **ASAP Memory Management:** Deterministic, compile-time memory management without a stop-the-world garbage collector.
*   **Two-Tier Concurrency:** High-performance OS threads (pthreads) combined with lightweight green threads (delimited continuations).
*   **Multiple Dispatch:** Full multiple dispatch on all arguments (Julia-style).
*   **Algebraic Effects:** Resumable exception handling and structured control flow.
*   **Hygienic Macros:** True syntax transformers with pattern matching and hygiene.
*   **Modern Syntax:** S-expressions with specialized brackets (`[]` for arrays/bindings, `{}` for types/FFI, `#{}` for dicts).

---

## Current Status (2026-01-07)

The project features a unified C99 + POSIX toolchain (parser, analysis, codegen) and runtime implementing:

### Memory Management (ASAP + RC-G)
| Optimization | Status | Description |
|---|---|---|
| **Liveness-Driven Free Insertion** | ✅ | CFG-based analysis for optimal `free()` placement. |
| **Iterative Transmigration** | ✅ | Worklist-based deep copy with O(1) **Bitmap Cycle Detection**. |
| **Region Splicing** | ✅ | O(1) block-level movement of physical memory between regions. |
| **Thread-Local Tethering** | ✅ | TLS-based caches to elide atomic operations on shared regions. |
| **Generational References** | ✅ | Sound generational validation via stable slot pools. |

### Language Features
*   **Special Forms:** `define`, `lambda`/`fn`, `let`, `if`, `do`/`begin`, `match`, `handle`/`perform`.
*   **Data Types:** Integers, Floats, Symbols, Lists, Arrays, Dicts, Strings, Characters.
*   **Concurrency:** Delimited continuations (`prompt`/`control`), Fibers (ucontext), CSP Channels.
*   **Modules:** Full module system with `export`, `import`, and namespace aliasing.
*   **FFI:** Handle-based Foreign Function Interface with ownership annotations.

---

## Memory Management: ASAP is NOT Garbage Collection

OmniLisp utilizes **ASAP (As Static As Possible)** memory management. Unlike traditional garbage collection, ASAP deallocates memory at **compile-time**.

### Core Strategy

```
┌─────────────────────────────────────────────────────────────────┐
│                    COMPILE-TIME ANALYSIS                         │
│  Shape Analysis ──► TREE / DAG / CYCLIC                         │
│  Escape Analysis ──► LOCAL / ESCAPING                           │
└─────────────────────────────────────────────────────────────────┘
                              │
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
       **TREE**                 **DAG**                  **CYCLIC**
   (Unique/Unshared)        (Shared/Acyclic)            (Back-Edges)
          │                       │                         │
          │            [Region-Aware RC Elision]    [Static Symmetric RC]
          │               ┌───────┴───────┐         ┌───────┴───────┐
          │               ▼               ▼         ▼               ▼
          │            **LOCAL**      **ESCAPING**   **BROKEN**    **UNBROKEN**
          │            (Elided)          (RC)      (Weak Refs)   (Strong Cycle)
          │               │               │             │             │
          │               │               │             │      [Region/Arena Strategy]
          │               │               │             │      ┌──────┴──────┐
          │               │               │             │      ▼             ▼
          │               │               │             │   **LOCAL**    **ESCAPING**
          ▼               ▼               ▼             ▼  (Scope-Bound) (Heap-Bound)
      Pure ASAP         No-Op         Standard      Standard  Arena Alloc   Region
     (free_tree)       (Borrow)       (dec_ref)     (dec_ref) (bulk_free)   Ext RC
```

1.  **Liveness Analysis:** Injects `free_obj()` calls at the earliest point a variable becomes dead.
2.  **Static Symmetric RC:** Reclaims cycles within the region hierarchy using bidirectional references, allowing for O(1) reclamation without a global heap scan.
3.  **Region Hierarchy:** Validates that outer scopes never point to inner scopes, providing the static backbone for symmetric reference tracking.
4.  **Generational References:** Provides safety for stable slot pooling and use-after-free detection.

---

## Concurrency & Effects

OmniLisp provides structured control flow through **Delimited Continuations** and **Algebraic Effects**.

```lisp
;; Define an effect
(define {effect ask} :one-shot (returns String))

;; Handle the effect
(handle
  (str "Hello, " (perform ask))
  (ask (_ resume) (resume "World")))  ; -> "Hello, World"
```

The concurrency model is two-tiered:
- **Tier 1:** OS Threads for true parallelism and blocking I/O.
- **Tier 2:** Lightweight green threads (Fibers) for massive concurrency using CSP channels.

---

## Syntax Showcase

```lisp
;; Vector bindings and dot notation
(let [person #{:name "Alice" :age 30}]
  (println person.name))

;; Multiple dispatch method
(define [method area Circle] [c]
  (* π c.radius c.radius))

;; Hygienic macros
(define [syntax unless]
  [(unless test body ...)
   (if test nothing (do body ...))])

;; FFI with ownership annotations
(define {extern malloc :from libc}
  [size {CSize}]
  -> {^:owned CPtr})
```

---

## References

1.  *ASAP: As Static As Possible memory management* (Proust, 2017).
2.  *Perceus: Garbage Free Reference Counting with Reuse* (Reinking et al., PLDI 2021).
3.  *Cyclic Reference Counting by Typed Reference Fields* (Sitaram, 2011).
4.  *Vale: Seamless, Fearless, Structured Concurrency* (Verdagon.dev).
5.  *Region-Based Memory Management* (Tofte & Talpin, 1997).
6.  *Abstracting Control* (Danvy & Filinski, 1990).
7.  *Collapsing Towers of Interpreters* (Amin & Rompf, POPL 2018).

---

## Building & Running

```bash
# Build the compiler and runtime
make

# Run a script
./omni examples/hello.lisp

# Run an expression
./omni -e "(+ 1 2)"
```

## Documentation

*   [SYNTAX.md](./SYNTAX.md) - Exhaustive syntax guide.
*   [DESIGN.md](./DESIGN.md) - Full technical specification.
*   [SUMMARY.md](./SUMMARY.md) - Feature implementation overview.
*   [FFI_PROPOSAL.md](./FFI_PROPOSAL.md) - Foreign Function Interface design.
