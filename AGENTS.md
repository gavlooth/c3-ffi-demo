# Omni Lisp - Agent Guide

This file is the working guide for coding agents in this repository.
Keep changes aligned with Omni's design and current codebase reality.

## Read First

Before modifying code, read:

1. `docs/LANGUAGE_SPEC.md`
2. `docs/C3_STYLE.md` (mandatory for C3 changes)

`docs/C3_STYLE.md` is the authoritative C3 coding standard for this repo.
It is derived from official C3 language documentation (starting from
`https://c3-lang.org/language-overview/examples/`), and agents should treat it
as the source of truth for feature usage and coding conventions.

For specialized work, also read:

- Type and dispatch work: `docs/type-system-syntax.md`
- Effects and handlers: `docs/EFFECTS_GUIDE.md`
- Major refactors in progress: `docs/C3_MIGRATION.md`, `docs/REFACTOR_PLAN.md`
- Memory architecture and status:
  - `memory/DESTINATION_ARENA_PLAN.md` (authoritative architecture + current implementation status)
  - `memory/CHANGELOG.md` (chronological implementation log)

## Core Language/Runtime Invariants

When implementing or reviewing features, preserve these constraints:

- Three collection types: list, array, dict
- Do not introduce "vector" terminology or type-specific naming when a generic operation is intended
- Prefer generic operations and dispatch-based extension over adding many type-prefixed primitives
- Multiple dispatch is a core extension mechanism
- Effects are first-class and used for structured control flow and I/O paths
- Lambda arity is strict; partial application should use `_`, `|>`, or explicit `partial`
- Truthiness rule: only `nil` and `false` are falsy
- Memory model is deterministic (scope/region based), so lifetime boundaries must stay explicit
- Runtime memory architecture is dual-lane (`TEMP`/`ESCAPE`) with no stop-the-world GC
- `scope_adopt` is retired from normal return flow; do not reintroduce it in runtime paths
- Boundary promotion/fallback logic must preserve shared promotion-context semantics
- Hardening priority: stabilize JIT/eval boundary paths (`jit_*`, effect dispatch boundaries, scope handoff points) before adding new runtime wiring.
- Repeated runtime invariants (ownership/lifetime/state guards) should be enforced through shared macros/helpers, not ad-hoc one-off checks.
- Ownership guardrail: default to scope/region ownership (`scope_retain`/`scope_release`) for runtime values.
- Do not introduce per-type refcount lifetimes for language values (`Instance`, `Closure`, etc.).
- Sound exception policy: only rare external-resource wrappers (for example `FFI_HANDLE` boxes over foreign pointers) may use local RC, and only with explicit finalizer policy + boundary tests.
- Any ownership-model exception must include regression tests for:
  - scope return boundary (`let`/function return),
  - closure capture/env copy boundary,
  - destruction path (no UAF/double-free).

## Ownership Drift Guardrails (Required)

Use this as a hard gate before merging memory/lifetime changes:

- Keep Omni value lifetimes region-centric: `ScopeRegion` is the owner of language values; wrappers may cross boundaries, but ownership authority stays with region retain/release.
- Do not add per-type lifetime systems for language objects. If a type stores or embeds Omni `Value`, it must not introduce its own independent RC/GC path.
- Do not use root pinning as a general escape hatch for correctness. Root pinning is allowed only for explicit process-lifetime singletons.
- Treat boundary logic as the source of truth: return-copy, env-copy, mutation copy, and promotion paths must agree on the same ownership model.
- If an exception is unavoidable, it must be rare, local, and explicit:
  - applies only to opaque foreign resources that do not own Omni `Value` graphs,
  - has one clear finalizer authority,
  - includes tests for return/env/destruction boundaries,
  - is recorded in `memory/CHANGELOG.md` with rationale and rollback note.

## Build and Test

Use the commands that match the current repo:

- Build: `c3c build`
- Run main binary: `LD_LIBRARY_PATH=/usr/local/lib ./build/main`
- Run end-to-end compiler checks: `scripts/run_e2e.sh`

Before finishing significant code changes:

- Run at least targeted tests for touched areas
- Run `c3c build` for integration safety
- When touching memory/lifetime logic, strongly prefer an ASAN pass: `c3c build --sanitize=address`
- When touching memory/lifetime logic, also run full suite:
  - `LD_LIBRARY_PATH=/usr/local/lib ./build/main`

## C3 Implementation Rules

Follow `docs/C3_STYLE.md`. Especially:

- Pair resource acquisition with `defer` cleanup
- Use clear preconditions (`@require`) where state assumptions are non-obvious
- Prefer exhaustive `switch` on tagged unions (for example `ValueTag`)
- Prefer `foreach` over manual index loops when practical
- Avoid unsafe type-punning between unrelated handle/value structs

## Change Discipline

- Keep edits small and locally coherent
- Do not silently defer known work; record it in `.claude/plans/` with concrete next steps
- If behavior changes, update relevant docs and `memory/CHANGELOG.md`

## Audit Mode (Important)

If asked to audit:

- Do not apply fixes unless explicitly requested
- Report prioritized findings with `file:line` references
- Focus on correctness, regressions, memory/lifetime risks, and missing tests
- Keep summary short; findings come first
