# Session 34-44 Boundary Hardening Playbook

Scope:
- Consolidate lifetime/ownership transitions into one audited boundary API.
- Improve old internals (code quality + business logic), not just wrap them.
- Keep behavior stable while reducing bug surface.

Execution policy:
- Do not advance sessions unless all gates pass.
- Keep commits small and domain-local.
- Prefer behavior-preserving refactors before semantic changes.

## Global Gates (run after every commit)

```bash
c3c build
LD_LIBRARY_PATH=/usr/local/lib ./build/main
c3c build --sanitize=address
ASAN_OPTIONS=detect_leaks=0,halt_on_error=1,abort_on_error=1 LD_LIBRARY_PATH=/usr/local/lib ./build/main
```

## Pre-commit Safety Check (run before every commit)

```bash
git status --short
rg -n "TODO|FIXME|HACK" src/lisp src | head
```

## Session 34: Boundary Facade + Baseline Contracts

- [ ] Commit A: `lifetime: introduce boundary facade with explicit contracts`
- [ ] Implement audited boundary module entry points only.
- [ ] Add `@require/@ensure` contracts to public boundary functions.
- [ ] Commit B: `lifetime: route low-risk callsites through boundary facade`
- [ ] Migrate only low-risk callsites.
- [ ] Run Global Gates.
- [ ] Record results in `memory/CHANGELOG.md`.

## Session 35: Internal Decomposition (Quality Pass A)

- [ ] Commit A: `lifetime: split boundary helpers into decision and mutation units`
- [ ] Break large boundary internals into small helper functions.
- [ ] Separate decision logic from mutation logic.
- [ ] Commit B: `lifetime: normalize naming and remove duplicated boundary branches`
- [ ] Remove duplicate condition trees and normalize helper naming.
- [ ] Run Global Gates.
- [ ] Update changelog.

## Session 36: Business Logic Unification

- [ ] Commit A: `lifetime: add shared ownership policy helpers for promote/copy/splice`
- [ ] Add shared policy helpers (`should_promote`, `should_copy_env`, `is_scope_transfer_legal`).
- [ ] Commit B: `lifetime: migrate boundary paths to shared policy decisions`
- [ ] Replace ad-hoc branching in return/env/splice paths.
- [ ] Run Global Gates.
- [ ] Update changelog with edge-case decision matrix.

## Session 37: High-Risk Caller Migration

- [ ] Commit A: `jit/eval: route return-boundary transitions via boundary API`
- [ ] Move JIT/eval return and resume boundaries through facade.
- [ ] Commit B: `lifetime: route env-copy and splice callers via boundary API`
- [ ] Migrate closure/env copy/splice callsites.
- [ ] Run Global Gates.
- [ ] Update changelog with migrated callsite summary.

## Session 38: Error Model Cleanup

- [ ] Commit A: `lifetime: convert boundary failure paths to typed faults/optionals`
- [ ] Replace ambiguous nil/error boundary failure paths.
- [ ] Commit B: `lifetime: remove silent boundary fallbacks and improve diagnostics`
- [ ] Make failure modes explicit and deterministic.
- [ ] Run Global Gates.
- [ ] Update changelog with failure model notes.

## Session 39: Invariant Framework

- [ ] Commit A: `lifetime: add centralized invariant hooks for ownership transitions`
- [ ] Add centralized invariant macros/helpers in boundary layer.
- [ ] Commit B: `tests: enforce invariant checks in sanitizer and test modes`
- [ ] Enable boundary invariant checks by default in test/ASAN runs.
- [ ] Run Global Gates.
- [ ] Update changelog with enabled invariant set.

## Session 40: Boundary Regression Pack

- [ ] Commit A: `tests: add boundary regression cases for return/env/splice transitions`
- [ ] Add deterministic regression tests for boundary transitions.
- [ ] Commit B: `tests: add stress cases for nested scopes and mixed jit/interp transitions`
- [ ] Add stress tests for nested scope/mode boundary behavior.
- [ ] Run Global Gates.
- [ ] Update changelog with new test groups.

## Session 41: Ownership-Domain Module Cleanup

- [ ] Commit A: `refactor: split lifetime boundary code by ownership domain`
- [ ] Split modules by policy/transition/invariant/diagnostics domains.
- [ ] Commit B: `refactor: remove dead boundary code and tighten internal visibility`
- [ ] Delete dead paths and reduce public/internal exposure.
- [ ] Run Global Gates.
- [ ] Update changelog with module ownership map.

## Session 42: Enforcement Gates

- [ ] Commit A: `ci: add guard to block direct legacy boundary calls outside facade`
- [ ] Add grep/script gate for forbidden direct calls.
- [ ] Commit B: `ci: add boundary-change policy checks with sanitizer requirement`
- [ ] Require ASAN + boundary tests for boundary-touched changes.
- [ ] Run Global Gates.
- [ ] Update changelog with enforcement rules.

## Session 43: Performance Stabilization

- [ ] Commit A: `perf: reduce redundant promotions/copies in boundary hot paths`
- [ ] Remove unnecessary boundary work introduced during cleanup.
- [ ] Commit B: `perf/tests: add boundary micro-bench and no-regression assertions`
- [ ] Add no-regression assertions and micro-bench coverage.
- [ ] Run Global Gates.
- [ ] Update changelog with perf notes.

## Session 44: Final Audit + Legacy Deletion Sweep

- [ ] Commit A: `audit: finalize boundary consolidation and remove deprecated entrypoints`
- [ ] Remove fully replaced legacy entrypoints.
- [ ] Commit B: `docs: publish boundary architecture audit and invariants contract`
- [ ] Write final architecture note + residual risk list.
- [ ] Run Global Gates.
- [ ] Confirm all sessions complete and changelog updated.

## Utility Commands

```bash
# Track remaining direct legacy callsites
rg -n "copy_to_parent|copy_env|splice_escapes|promote_to_escape" src | sort

# Confirm sanctioned boundary API entry points
rg -n "fn .*boundary_" src/lisp
```

## Progress Ledger

- [ ] Session 34 complete
- [ ] Session 35 complete
- [ ] Session 36 complete
- [ ] Session 37 complete
- [ ] Session 38 complete
- [ ] Session 39 complete
- [ ] Session 40 complete
- [ ] Session 41 complete
- [ ] Session 42 complete
- [ ] Session 43 complete
- [ ] Session 44 complete
