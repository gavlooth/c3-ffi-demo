# OmniLisp

**OmniLisp** is a high-performance, multi-paradigm Lisp dialect designed for systems programming and modern application development. It combines the minimalism of **Scheme**, the industrial power of **Common Lisp**, the modern type system of **Julia**, and the data-driven elegance of **Clojure**, all built on a foundation of **CTRR (Compile-Time Region Reclamation)**.

## Key Design Pillars

*   **CTRR Memory Model:** Deterministic region-based reclamation scheduled at compile time (no stop-the-world GC).
*   **Two-Tier Concurrency:** High-performance OS threads (pthreads) combined with lightweight green threads (delimited continuations).
*   **Multiple Dispatch:** Full multiple dispatch on all arguments (Julia-style).
*   **Algebraic Effects:** Resumable exception handling and structured control flow.
*   **Hygienic Macros:** True syntax transformers with pattern matching and hygiene.
*   **Modern Syntax:** S-expressions with specialized brackets (`[]` for arrays/bindings, `{}` for types/FFI, `#{}` for dicts).

---

## Current Status (2026-01-07)

The project features a unified C99 + POSIX toolchain (parser, analysis, codegen) and runtime implementing:

### Memory Management (CTRR)
| Optimization | Status | Description |
|---|---|---|
| **Region Lifetime Scheduling** | ✅ | Compiler schedules `region_create` / `region_exit` at scope boundaries. |
| **Transmigration on Escape** | ✅ | Explicit escape repair: copy/move graphs between regions. |
| **Thread-Safe Tethering** | ✅ | Borrow-window pinning to prevent region reclamation during calls. |
| **Metadata-Driven Transmigration** | 🚧 | Required by the CTRR contract; spec is written, implementation is pending. |

### Language Features
*   **Special Forms:** `define`, `lambda`/`fn`, `let`, `if`, `do`/`begin`, `match`, `handle`/`perform`.
*   **Data Types:** Integers, Floats, Symbols, Lists, Arrays, Dicts, Strings, Characters.
*   **Concurrency:** Delimited continuations (`prompt`/`control`), Fibers (ucontext), CSP Channels.
*   **Modules:** Full module system with `export`, `import`, and namespace aliasing.
*   **FFI:** Handle-based Foreign Function Interface with ownership annotations.

---

## Memory Management: CTRR is NOT Garbage Collection

OmniLisp uses **CTRR (Compile-Time Region Reclamation)**: the compiler schedules
region lifetimes and inserts explicit runtime operations for **escapes**
(transmigration) and **borrows** (tethering). There is **no stop-the-world
tracing collector**.

Canonical references:

- `docs/CTRR.md` (short, normative contract)
- `runtime/docs/CTRR_TRANSMIGRATION.md` (detailed transmigration contract)

---

## Concurrency & Effects

OmniLisp provides structured control flow through **Delimited Continuations** and **Algebraic Effects**.

```lisp
;; Define an effect
; REVIEWED:SYNTAX
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

1.  *ASAP (paper terminology): As Static As Possible memory management* (Proust, 2017).
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

## Build and Test

See [docs/BUILD_AND_TEST.md](docs/BUILD_AND_TEST.md) for canonical build and test commands.
