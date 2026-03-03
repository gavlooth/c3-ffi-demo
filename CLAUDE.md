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
- **Strict arity lambdas** — `(lambda (x y) body)` requires exactly 2 args; use `_` placeholder `(+ 1 _)`, `|>` pipe, or `partial` for partial application
- **Region-based memory** — no GC, deterministic cleanup
- **Truthiness**: only `nil` and `false` are falsy — `0`, `""`, `'()` are truthy

When proposing new features or changes, check whether they align with these paradigms. Prefer generic dispatch over adding new type-prefixed primitives. Prefer simplicity over configurability.

## Build & Test
- Build: `c3c build`
- Run: `LD_LIBRARY_PATH=/usr/local/lib ./build/main` (needs GNU Lightning)
- All tests must pass before committing

## C3 Style Guide (MANDATORY)
Read `docs/C3_STYLE.md` before writing any C3 code. Key rules:
- `docs/C3_STYLE.md` is the authoritative C3 standard for this repo and is
  derived from official C3 docs (starting point:
  `https://c3-lang.org/language-overview/examples/`).
- **defer**: acquire → defer release → use. Every `mem::malloc` → `defer mem::free`. Every LMDB txn → `defer mdb_txn_abort`. Every scope save → `defer restore`.
- **distinct**: Never cast unrelated structs to `FfiHandle*`. Use distinct types or separate ValueTags.
- **contracts**: Add `@require` to functions with non-obvious preconditions (scope state, non-null, valid tags).
- **optionals**: Prefer `Type!` returns over null + raise_error for functions that can fail.
- **$assert**: Verify array sizes, struct sizes, alignment at compile time.
- **foreach**: Use foreach over manual index loops.
- **switch**: Exhaustive — no default case for ValueTag switches (compiler catches missing tags).
- **ASAN**: Run `c3c build --sanitize=address` when debugging memory issues.

## Scope-Boundary Ownership Rules (MANDATORY — NEVER VIOLATE)

When a heap-allocated struct (Instance, Closure, FfiHandle, etc.) can be shared across scope boundaries, it MUST follow one of these two models. There is no third option.

### Model A: Refcounted sharing (Instance, FfiHandle, Closure)
- The struct is `malloc`'d with a `refcount` field, initialized to 1.
- `copy_to_parent` / `promote_to_escape` create a **new Value wrapper** and call `retain()`. O(1).
- Scope dtor calls `release()` — frees when refcount hits 0.
- **The struct MUST own its data by value** (not pointers into scopes).
  - Instance: `Value[N] fields` (by value), NOT `Value*[N] fields` (pointers).
  - FfiHandle: `lib_handle` is a raw C pointer (scope-independent).
  - Closure: `env` points to env_scope (refcounted separately).

### Model B: Root-scope pinning (Primitive, MethodTable, TypeInfo)
- Allocated once in `root_scope`, lives forever.
- `copy_to_parent` returns as-is (already in root).
- Only for truly global, singleton values.

### NEVER do any of these:
- **NEVER** deep-copy structs on every boundary crossing — this is O(fields) per function return.
- **NEVER** store scope-allocated `Value*` pointers inside a shared struct — they dangle when the scope is released.
- **NEVER** assume a struct "lives in root_scope" without verifying — user code creates instances in call scopes.
- **NEVER** add a `promote_to_root` call as a "fix" for boundary bugs — find the real ownership issue.

## Conventions
- Multi-param lambdas with strict arity (NO auto-curry); use `_` placeholder, `|>` pipe, or `partial` for partial application
- `_` is T_UNDERSCORE — wildcard in match patterns, placeholder in call args `(+ 1 _)` → lambda
- C3 slices are INCLUSIVE: `buffer[0..n]` = n+1 elements, use `buffer[:n]` for n elements
- Stdlib functions defined in `register_stdlib()` as Omni code via `run()`
- Tests go in `run_advanced_tests()` or appropriate test function in eval.c3

## Anti-Skip Rule
When a migration or refactoring step feels "too big" or "too risky":
- **Split it** until each sub-step touches ≤1 file and ≤30 lines
- **Never skip** — if a step can't be done, split it smaller, don't jump to an easier one
- **Commit after every sub-step** — build + test + commit. No batching.
- **Start with the smallest instance** — don't begin with the file that has 50 call sites. Begin with the file that has 3.

## Deferred Work Policy
When deferring work (skipping something now with intent to do it later):
- **NEVER** just mention it in a comment or conversation — always add it to the active plan file (`.claude/plans/`)
- Each deferred item must have: **What**, **Why deferred**, **Risk if not done**, **When** (which phase/milestone), and **How** (concrete approach)
- Reference the plan item from inline code comments (e.g., `// See plan item D1 in .claude/plans/...`)
- If no plan file exists yet, create one

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
