# Omnilisp Decision Log

This file is a compact index of resolved design decisions. The canonical spec and
examples live in `DESIGN.md` and `SYNTAX.md`.

## Update Checklist
- [ ] [change] Switch to `#r/#raw/#b` string prefixes; reserve `r/raw/b` forms.
- [ ] [change] Add reader macros `#(...)`, `#?`, `#_`, `#| |#`, `#!`, `#uuid/#path`.
- [ ] [change] Prefer `^:mutable` metadata for mutability; keep `:mutable` as sugar.
- [ ] [change] Remove `:seq` / `:rec`; use `^:seq` / `^:rec` metadata only.
- [ ] [change] Prefer `^:zip` metadata for zip semantics in `for` / `foreach` (if added).
- [ ] [change] Replace `^:extends` with `^:parent {Type}` metadata before `{struct ...}`.
- [ ] [change] Add hint metadata: `^:tailrec`, `^:borrowed`, `^:consumes`, `^:noescape`, `^:unchecked`, `^:deprecated`.
- [ ] [implementation] Reader support for `#(...)`, `#?`, `#_`, `#| |#`, `#!`, `#r/#raw/#b`, `#uuid/#path`.
- [ ] [implementation] Metadata handling for `^:mutable`, `^:tailrec`, `^:borrowed`, `^:consumes`, `^:noescape`, `^:unchecked`, `^:deprecated`.
- [ ] [implementation] Metadata handling for `^:seq` / `^:rec` on `let`.
- [ ] [refactor] Migrate code/tests to new string prefixes and `^:mutable` style.

## Resolved
*   Parameter syntax: `[x {Type}]` and defaults `[x {Type} default]`.
*   Let type annotations: triplets `[x {Type} value]`.
*   Truthiness (design target): only `false` and `nothing` are falsy.
*   Implementation note (current C compiler/runtime): only `false` and `nothing` are falsy; numeric `0` and empty lists are truthy.
*   Named let + modifiers: metadata form only (`(let ^:seq loop [...] ...)`).
*   Struct construction: positional args by default; named fields after `&`; mixed allowed; unknown/duplicate keys error.
*   Parametric struct + parent: `^:parent {Type}` metadata before `{struct ...}` (parent defaults to `Any`).
*   Struct mutability: per-field `^:mutable` (or `:mutable` sugar); `{mutable ...}` sugar for all fields mutable.
*   Enum namespace: unqualified when unique; otherwise `Type.Variant`.
*   `satisfies` patterns accept any predicate expression, evaluated per branch.
*   `match` guards accept any predicate expression; if it evaluates to a function (including a lambda literal), it is invoked with the pattern-bound variables.
*   Quote preserves structure: `'(...)` list, `'[...]` array, `'#{...}` dict.
*   Channel closed semantics: `(send ch v)` returns false if closed; `(recv ch)` returns `nothing` on closed.
*   Lambda shorthand: `(-> x ...)` and `(-> (x y) ...)` (`|>` reserved for pipelines).
*   Reader macros: `#(...)`, `#?`, `#_`, `#| |#`, `#!`, `#uuid/#path`.
*   `for`/`foreach` multiple bindings are nested by default.
*   Methods use explicit `self` parameter.
*   No `nil`; empty list literal is `()`.
*   Error handling: `error` + single `restart-case` in phase 1; full condition system deferred.
*   Named args: `&` separates positional from named.
*   String prefixes use `#` forms: `#r` (regex), `#raw` (raw), `#b` (bytes).
*   Hint metadata: `^:tailrec`, `^:borrowed`, `^:consumes`, `^:noescape`, `^:unchecked`, `^:deprecated`.

## Open Decisions
*   None tracked here. Add new questions as issues or expand `DESIGN.md`.
