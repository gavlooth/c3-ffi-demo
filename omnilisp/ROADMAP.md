# Omnilisp Implementation Roadmap

This roadmap outlines the steps to build the Omnilisp Go compiler that emits ANSI C99 + POSIX code and links against `omniruntime`. Prefer Go libraries (parser/CLI/collections) over bespoke implementations.

## Phase 0: Compiler + Runtime Core (Go + C99 target)
**Goal:** Establish the object model, ABI, and ASAP memory management.
*   [ ] Define `OmniValue` and core runtime types in C (`types.c`) and mirror them in Go.
*   [ ] Implement allocation helpers and `free_obj`; **no runtime GC**.
*   [ ] Integrate ASAP analysis in the Go compiler (CLEAN insertion, liveness, escape, capture tracking).
*   [ ] Validate arena/region allocators for non-escaping or cyclic data; ensure pthread usage for sync.

## Phase 1: Reader (Lexer & Parser)
**Goal:** Convert source text into AST in Go.
*   [ ] Tokenize brackets `()` `[]` `{}` `#{}` and `..`/`...` rest markers.
*   [ ] Preserve quote semantics (`'[...]` and `'#{...}` keep structure).
*   [ ] Parse reader macros: `#(...)`, `#?`, `#r/#raw/#b`, `#_`, `#| |#`, `#!`, `#uuid/#path`.
*   [ ] Parse `->` lambda shorthand, `|>` pipeline, and `&` named args/constructors.
*   [ ] Parse dot access (`obj.field`, `obj.(expr)`) and string interpolation.

## Phase 2: Analyzer + IR
**Goal:** Resolve bindings and prepare for codegen.
*   [ ] Implement `let` modifiers via metadata (`^:seq`, `^:rec`).
*   [ ] Enforce truthiness: only `false` and `nothing` are falsy.
*   [ ] Honor `^:mutable` metadata for local/field mutation checks.
*   [ ] Enforce/record hint metadata: `^:tailrec`, `^:borrowed`, `^:consumes`, `^:noescape`, `^:unchecked`, `^:deprecated`.
*   [ ] Build CFGs for liveness-driven free insertion.

## Phase 3: Object System
**Goal:** Types, structs, enums, and multiple dispatch.
*   [ ] Abstract types, concrete structs, and interfaces.
*   [ ] Parent types via `^:parent {Type}` metadata (defaults to `Any`).
*   [ ] Struct constructors: positional args plus `&` named fields; unknown/duplicate keys error.
*   [ ] Enum namespace rules: unqualified if unique, else `Type.Variant`.
*   [ ] Methods with explicit `self` parameter.

## Phase 4: Sequences & Iteration
*   [ ] Iterator protocol (`iterate`) - **Runtime support complete (generators)**
*   [ ] `for`/`foreach` macros (multiple bindings are nested; add `^:zip` later if needed).
*   [ ] Support `^:zip` metadata (with `:zip` sugar) if zip semantics are added.
*   [ ] Ranges and slicing.
*   [x] Generator infrastructure in C runtime (`make_gen`, `gen_next`, `generator_yield`)

## Phase 5: Standard Library & Macros
*   [ ] Hygienic macros and syntax objects.
*   [ ] Core library in Omnilisp (`map`, `filter`, `reduce`, etc.).
*   [ ] Module system (`module`, `import`).

## Phase 6: Concurrency - COMPLETE (Runtime)
**Status:** Runtime support complete, compiler integration pending.

### Two-Tier Concurrency Model
*   [x] **Tier 1 (OS Threads):** pthread-based channels, atoms, spawn_goroutine
*   [x] **Tier 2 (Green Threads):** Continuation-based scheduling

### Green Thread Features (Complete in Runtime)
*   [x] CEK-style continuation frames
*   [x] Delimited continuations (`prompt`/`control`)
*   [x] Green thread scheduler with task parking
*   [x] Continuation-based channels (`make_green_chan`, `green_send`, `green_recv`)
*   [x] Generators/iterators via continuations
*   [x] Promises with async/await pattern

### Compiler Integration (Pending)
*   [ ] Emit `green_scheduler_init()` in main
*   [ ] Transform `spawn` to `spawn_green_task` vs `spawn_goroutine`
*   [ ] Transform channel ops based on context
*   [ ] Generator syntax sugar (`yield` expression)
*   [ ] Async/await syntax

### Performance Characteristics
| Metric | OS Thread | Green Thread |
|--------|-----------|--------------|
| Creation | ~10μs | ~100ns |
| Context switch | ~1μs | ~50ns |
| Memory/task | ~8KB | ~100 bytes |
| Max tasks | ~10K | ~1M+ |
