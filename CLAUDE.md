# Omni Lisp — Claude Code Instructions

## Onboarding — Read Before Any Work

Before writing or modifying code, understand the project's intent and paradigms:

1. **Read `memory/MEMORY.md`** — project overview, key files, naming conventions, architecture, what's implemented
2. **Read `docs/LANGUAGE_SPEC.md`** — the language Omni Lisp aims to be: syntax, data types, special forms, type system, dispatch, effects
3. **For type/dispatch/effects work**, also read `memory/type-system-design.md`

### Design Intent
Omni is a **Lisp with modern semantics** — not Scheme, not Clojure, not Common Lisp. Key paradigm choices:
- **Three collection types**: list (linked, `'(1 2 3)`), array (contiguous, `[1 2 3]`), dict (hash, `{'a 1}`)
- **No "vector"** — arrays are arrays. No Scheme/C++ naming.
- **Generic operations over type-prefixed names**: `(ref coll key)` not `(array-ref arr idx)` / `(dict-ref d k)`
- **Multiple dispatch as the extension mechanism** — no traits, no interfaces, dispatch table IS the protocol
- **Algebraic effects for I/O** — `print`/`println` go through `perform` with fast path when unhandled
- **Auto-curried lambdas** — `(lambda (x y) body)` desugars to nested single-param lambdas, except typed multi-param (preserved for dispatch)
- **Region-based memory** — no GC, deterministic cleanup
- **Truthiness**: only `nil` and `false` are falsy — `0`, `""`, `'()` are truthy

When proposing new features or changes, check whether they align with these paradigms. Prefer generic dispatch over adding new type-prefixed primitives. Prefer simplicity over configurability.

## Build & Test
- Build: `c3c build`
- Run: `LD_LIBRARY_PATH=/usr/local/lib ./build/main` (needs GNU Lightning)
- All tests must pass before committing

## Conventions
- Single-param lambdas with currying (multi-param desugars to nested lambdas)
- `_` is a wildcard token (T_UNDERSCORE), NOT a symbol — never use as lambda param name
- C3 slices are INCLUSIVE: `buffer[0..n]` = n+1 elements, use `buffer[:n]` for n elements
- Stdlib functions defined in `register_stdlib()` as Omni code via `run()`
- Tests go in `run_advanced_tests()` or appropriate test function in eval.c3

## Audit Mode
When auditing for naive implementations or production readiness:
- **DO NOT** create pull requests or make fixes automatically
- **ONLY** report findings as a prioritized list with file:line references
- Let the human decide what to fix and when

## Post-Implementation (MANDATORY)
After every implementation session, ALWAYS do all three:
1. **Update tasks**: Mark completed tasks as done, create new ones for follow-up work
2. **Update `memory/CHANGELOG.md`**: Date, summary of changes, files modified, test count before/after
3. **Update `memory/MEMORY.md`**: Reflect new test counts, architecture changes, new features, and updated TODOs
