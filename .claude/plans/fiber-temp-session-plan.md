# Fiber Temp Session Plan (Execution Cadence)

Date: 2026-03-05  
Source roadmap: `.claude/plans/fiber-temp-detailed-implementation-plan.md`  
Target cadence: 16 sessions (small, auditable commits)

## Current Checkpoint (2026-03-05)

Completed this session:
- `1)` Design freeze sign-off: **pass** (proposal/guardrail docs aligned and frozen for execution).
- `2)` Ownership/guardrail checkpoint: **conditional pass**.
  - Region-centric ownership and no per-type RC drift remain enforced.
  - Open exception to resolve in next session (`4`): `src/stack_engine.c3` still has legacy direct scope touchpoints (`stack_ctx_pin_scope`, `stack_ctx_unpin_scope`, `pinned_scope` field).
- `3)` Defer hot-path perf sign-off: **pass** with runtime counters and zero heap-overflow allocations in current suite run.
  - `STACK_DEFER_METRICS push=8 undefer=1 destroy_cb=9 clone_cb=1 update_arg=2 cloned_entries=2 peak_depth=3 heap_alloc=0`
- `4)` Deferred by request to next session.

Validation evidence:
- Normal: `c3c build` + `OMNI_TEST_QUIET=1 LD_LIBRARY_PATH=/usr/local/lib ./build/main`  
  Result: `Stack engine 14/0`, `Unified 1177/0`, `Compiler 73/0`.
- ASAN strict: `c3c clean` + `c3c build --sanitize=address` + `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 OMNI_TEST_QUIET=1 LD_LIBRARY_PATH=/usr/local/lib ./build/main`  
  Result: `Stack engine 14/0`, `Unified 1177/0`, `Compiler 73/0`.

## Item 4 Follow-up (2026-03-05)

Completed in subsequent session:
- Removed legacy stack-layer direct scope touchpoints from `src/stack_engine.c3`:
  - `pinned_scope` field removed
  - `stack_ctx_pin_scope(...)` removed
  - `stack_ctx_unpin_scope(...)` removed
- Migrated suspend sites to defer-backed boundary guard usage (`suspend_with_scope_guard(...)`) in:
  - `src/lisp/jit_jit_runtime_effects.c3`
  - `src/lisp/jit_jit_handle_signal.c3`
  - `src/lisp/jit_jit_reset_shift.c3`
  - `src/lisp/primitives_iter_coroutine.c3`

Validation for follow-up:
- `c3c build` + `OMNI_TEST_QUIET=1 LD_LIBRARY_PATH=/usr/local/lib ./build/main`: pass (`Stack engine 15/0`, `Unified 1178/0`, `Compiler 73/0`)
- `c3c clean && c3c build --sanitize=address` + strict ASAN run: pass (`Stack engine 14/0`, `Unified 1177/0`, `Compiler 73/0`)

## Fiber TEMP Phase 1 Progress (2026-03-05)

Completed:
- Added conservative `OMNI_FIBER_TEMP` scaffold in `scope_region`:
  - chunk-pool state and helpers,
  - TEMP-lane reclaim hook in destroy/reset/splice paths,
  - summary counters (`OMNI_TEST_SUMMARY suite=fiber_temp_pool ...`).
- Validation remains green in normal + ASAN with flag OFF and ON smoke run.

Next:
- Start eligibility routing (narrow whitelist) so selected TEMP allocations can use Fiber TEMP in active stack contexts.

## Fiber TEMP Phase 2 Progress (2026-03-05)

Completed:
- Introduced narrow eligibility gate:
  - scopes become Fiber TEMP eligible only when created in active stack contexts (`OMNI_FIBER_TEMP` enabled, stack context active, parent scope present).
  - TEMP lane only: ESCAPE lane remains raw alloc/free.
- Added stack-engine exercise test for in-context scope create/release to drive pool metrics under the flag.

Validation:
- Normal: `Stack engine 16/0`, `Unified 1178/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 15/0`, `Unified 1177/0`, `Compiler 73/0`.
- Flagged metrics now non-zero (`hits=3`, `misses=3`, `returns=6`, `eligible_slow=4`, `bypass_large=0`), confirming active path exercise.

Next:
- Expand allocation-shape whitelist beyond size-only checks (for example bypass pool routing once ESCAPE-lane activity appears in a scope).

## Fiber TEMP Phase 2b Progress (2026-03-05)

Completed:
- Extended allocation-shape whitelist to include ESCAPE activity:
  - Fiber TEMP slow-path routing now requires no ESCAPE chunks/dtors on the scope.
  - Added `bypass_escape` metric counter for explicit observability.
- Extended stack-engine in-context scope test to exercise both:
  - a TEMP-heavy eligible scope,
  - a mixed TEMP+ESCAPE scope that should bypass Fiber TEMP pool routing.

Validation:
- Normal: `Stack engine 16/0`, `Unified 1178/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 15/0`, `Unified 1177/0`, `Compiler 73/0`.
- Flagged metrics: `hits=1`, `misses=3`, `returns=6`, `eligible_slow=2`, `bypass_large=0`, `bypass_escape=2`.

Next:
- Add focused assertions around Fiber TEMP pool invariants (take/reclaim behavior and bypass counters) without coupling tests to suite order.

## Fiber TEMP Phase 2c Progress (2026-03-05)

Completed:
- Added focused Fiber TEMP pool invariant checks in `scope_region`:
  - reclaim path (return vs drop behavior),
  - take-hit path,
  - take-miss path,
  using local before/after deltas to avoid suite-order coupling.

Validation:
- Normal: `Stack engine 16/0`, `Scope region 51/0`, `Unified 1178/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 15/0`, `Scope region 51/0`, `Unified 1177/0`, `Compiler 73/0`.
- Flagged metrics include exercised pool counters:
  - `hits=2`, `misses=4`, `returns=8`, `eligible_slow=2`, `bypass_escape=2`.

Next:
- Begin per-fiber TEMP ownership plumbing behind the flag while preserving stack-layer genericity (no direct scope logic in stack engine).

## Fiber TEMP Enablement Substrate Progress (2026-03-05)

Completed:
- Added a generic `StackCtx` lifecycle callback channel in `stack_engine` that is independent of LIFO defer push/pop.
- Added coverage for:
  - lifecycle destroy isolation from `stack_ctx_undefer(...)`,
  - lifecycle clone hook behavior.

Why this step:
- A previous attempt to bind persistent resource state via dynamic defer registration exposed a real risk:
  non-LIFO registrations can violate call-site assumptions around `stack_ctx_undefer(...)`.
- Lifecycle channel provides the correct primitive for persistent context-owned resources.

Validation:
- Normal: `Stack engine 18/0`, `Scope region 51/0`, `Unified 1178/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 17/0`, `Scope region 51/0`, `Unified 1177/0`, `Compiler 73/0`.

Next:
- Re-attempt Fiber TEMP per-context ownership using lifecycle callbacks (not defer stack), keeping default behavior unchanged when flag is off.

## Fiber TEMP Phase 3 Progress (2026-03-05)

Completed:
- Wired per-`StackCtx` Fiber TEMP chunk caches through lifecycle hooks:
  - context-local take/reclaim path added,
  - clone-aware shared context state via lifecycle clone callback,
  - lifecycle destroy callback flushes residual chunks to global pool.
- Extended metrics and tests:
  - summary now includes `ctx_hits` and `ctx_returns`,
  - stack-context exercise test now asserts `ctx_returns` delta under the flag.

Validation:
- Normal: `Stack engine 18/0`, `Scope region 51/0`, `Unified 1178/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 17/0`, `Scope region 51/0`, `Unified 1177/0`, `Compiler 73/0`.
- Flagged: `ctx_hits=1`, `ctx_returns=6`, no assertion failures.

Next:
- Stress clone/discard permutations with Fiber TEMP enabled to verify shared-context cache behavior under multi-shot continuation patterns.

## Fiber TEMP Phase 4 Progress (2026-03-05)

Completed:
- Added clone/discard stress coverage in stack-engine tests for Fiber TEMP lifecycle paths.
- New stress test validates repeated:
  - suspend,
  - clone + discard clone,
  - resume original to completion,
  with scope create/alloc/release around suspend boundaries.
- Under `OMNI_FIBER_TEMP=1`, test asserts per-context cache return activity (`ctx_return_count` delta).

Validation:
- Normal: `Stack engine 19/0`, `Scope region 51/0`, `Unified 1178/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 18/0`, `Scope region 51/0`, `Unified 1177/0`, `Compiler 73/0`.
- Flagged metrics show substantial ctx-path exercise (`ctx_hits=33`, `ctx_returns=70`).

Next:
- Add cross-thread/offload guard tests to ensure Fiber TEMP remains confined to stack-context owner thread and global fallback remains safe under worker interactions.

## Fiber TEMP Phase 5 Progress (2026-03-05)

Completed:
- Added scheduler/offload boundary test ensuring Fiber TEMP context-cache counters stay unchanged for repeated `thread-spawn`/`thread-join` operations (no stack context).
- Coverage is active only when `OMNI_FIBER_TEMP=1`; flag-off path remains stable.

Validation:
- Normal: `Stack engine 19/0`, `Scope region 51/0`, `Unified 1179/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 18/0`, `Scope region 51/0`, `Unified 1178/0`, `Compiler 73/0`.

Next:
- Add targeted scheduler wakeup/offload interleaving stress with Fiber TEMP enabled to widen boundary-race coverage.

## Fiber TEMP Phase 5c Progress (2026-03-05)

Completed:
- Added deterministic scheduler wakeup/offload interleaving stress:
  - async offload via spawn/await,
  - wakeup enqueue/drain in-loop,
  - thread offload spawn/join.
- Added end-of-test wakeup queue drain invariant (`head == tail`).

Validation:
- Normal: `Stack engine 19/0`, `Scope region 51/0`, `Unified 1180/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 18/0`, `Scope region 51/0`, `Unified 1179/0`, `Compiler 73/0`.

Next:
- Start focused audit of Fiber TEMP behavior at scheduler cancellation/timeouts boundaries (especially destroy-before-complete paths).

## Fiber TEMP Phase 6 Progress (2026-03-05)

Completed:
- Added targeted scheduler cancellation/timeout stress under Fiber TEMP:
  - timeout-immediate and cancel+join thread paths (with context-metric invariants),
  - timeout-success control path.
- Added repeated offload-fiber cancel stress (`spawn(offload ...)`, `fiber-cancel`, `run-fibers`) to cover destroy-before-complete behavior.

Validation:
- Normal: `Stack engine 19/0`, `Scope region 51/0`, `Unified 1182/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 18/0`, `Scope region 51/0`, `Unified 1181/0`, `Compiler 73/0`.

Next:
- Begin focused metric/telemetry hardening for Fiber TEMP clone-share lifecycle (explicit counters for lifecycle clone/destroy pool flush behavior).

## Fiber TEMP Phase 6b Progress (2026-03-05)

Completed:
- Added explicit lifecycle telemetry counters:
  - context-pool create count,
  - lifecycle clone callback count,
  - lifecycle destroy callback count,
  - deferred-destroy count (shared refcount path),
  - destroy-time chunk flush count.
- Extended summary output to include lifecycle telemetry fields.
- Strengthened clone/discard stress test assertions to require lifecycle telemetry deltas under flag.

Validation:
- Normal: `Stack engine 19/0`, `Scope region 51/0`, `Unified 1182/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 18/0`, `Scope region 51/0`, `Unified 1181/0`, `Compiler 73/0`.
- Flagged summary includes lifecycle fields (`ctx_pools`, `lc_clone`, `lc_destroy`, `lc_defer`, `lc_flush`).

Next:
- Add a focused leakage/retention guard test around repeated create/destroy cycles to verify lifecycle flush counters and pooled-count stability over long runs.

## Fiber TEMP Phase 6c Progress (2026-03-05)

Completed:
- Added long-run retention guard for clone/discard lifecycle:
  - 128 repeated create/suspend/clone-discard/resume/destroy cycles,
  - verifies lifecycle creation/flush activity,
  - asserts bounded pooled-chunk growth.
- Integrated into stack engine suite.

Validation:
- Normal: `Stack engine 20/0`, `Scope region 51/0`, `Unified 1182/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 19/0`, `Scope region 51/0`, `Unified 1181/0`, `Compiler 73/0`.
- Flagged telemetry confirms heavy exercised lifecycle paths with stable pooled count.

Next:
- Begin a small rollout hygiene pass: tighten docs around Fiber TEMP enablement policy (explicitly experimental/flagged) and enumerate remaining production gates.

## Fiber TEMP Phase 6d Progress (2026-03-05)

Completed:
- Hardened stack engine thread-affinity boundaries so `StackPool`/`StackCtx` ownership is explicit and enforced:
  - added owner thread tokens on pool and context structs,
  - added runtime guards for create/destroy/init/switch/suspend/resume/clone and pool shutdown paths.
- Added targeted stack-engine ownership-state test:
  - `test_stack_ctx_thread_affinity_state()` verifies pool/context ownership tokens are initialized to the current thread.

Validation:
- Normal: `Stack engine 21/0`, `Scope region 51/0`, `Unified 1182/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 20/0`, `Scope region 51/0`, `Unified 1181/0`, `Compiler 73/0`.
- Flagged (`OMNI_FIBER_TEMP=1` + summary): pass, with stable lifecycle telemetry counters.

Next:
- Expand thread-boundary stress to explicitly cover reject/fail-fast behavior for cross-thread stack-engine misuse in a non-production test harness.

## Fiber TEMP Phase 6e Progress (2026-03-05)

Completed:
- Extended thread-affinity guards to all stack defer/lifecycle API entry points (not only top-level context lifecycle calls).
- Added owner checks for defer register/pop/update and lifecycle attach/find/clone/destroy/clear paths, closing an internal safety gap for stack-owned teardown metadata.

Validation:
- Normal: `Stack engine 21/0`, `Scope region 51/0`, `Unified 1182/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 20/0`, `Scope region 51/0`, `Unified 1181/0`, `Compiler 73/0`.
- Flagged (`OMNI_FIBER_TEMP=1` + summary): pass with stable Fiber TEMP telemetry.

Next:
- Add an opt-in misuse harness to exercise fail-fast cross-thread stack API violations outside the default CI suite.

## Fiber TEMP Phase 6f Progress (2026-03-05)

Completed:
- Added an explicit opt-in misuse probe for stack-affinity fail-fast verification:
  - new CLI mode: `--stack-affinity-probe`,
  - intentionally corrupts stack-context owner token and calls guarded destroy path,
  - expected outcome: deterministic non-zero process termination via ownership violation.
- This keeps default tests stable while giving a concrete harness for boundary-misuse verification.

Validation:
- Normal: `Stack engine 21/0`, `Scope region 51/0`, `Unified 1182/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 20/0`, `Scope region 51/0`, `Unified 1181/0`, `Compiler 73/0`.
- Flagged (`OMNI_FIBER_TEMP=1` + summary): pass with stable telemetry.
- Probe run: `./build/main --stack-affinity-probe` exits non-zero (`132`) with expected fail-fast backtrace.

Next:
- Decide whether to add an opt-in harness wrapper (`OMNI_STACK_AFFINITY_HARNESS=1`) that auto-runs the probe subprocess and summarizes pass/fail in test mode.

## Fiber TEMP Phase 6g Progress (2026-03-05)

Completed:
- Added `OMNI_STACK_AFFINITY_HARNESS=1` wrapper in default test mode:
  - runs full suite as before,
  - then executes `--stack-affinity-probe` as a subprocess,
  - validates non-zero exit + expected fail-fast marker in probe output,
  - emits `OMNI_TEST_SUMMARY suite=stack_affinity_harness ...`.
- Default path remains unchanged when the env flag is unset.

Validation:
- Normal default: pass (`Stack engine 21/0`, `Scope region 51/0`, `Unified 1182/0`, `Compiler 73/0`).
- Normal + harness: pass + `stack_affinity_harness pass=1 fail=0`.
- ASAN default: pass (`Stack engine 20/0`, `Scope region 51/0`, `Unified 1181/0`, `Compiler 73/0`).
- ASAN + harness: pass + `stack_affinity_harness pass=1 fail=0`.

Next:
- Fold this harness summary into any CI profile that already sets `OMNI_TEST_SUMMARY=1` for boundary-hardening runs.

## Fiber TEMP Phase 6h Progress (2026-03-05)

Completed:
- Added a one-command boundary-hardening runner:
  - `scripts/run_boundary_hardening.sh`
  - runs normal + ASAN matrix with Fiber TEMP and stack-affinity harness enabled by default.
- Added profile docs in `docs/PROJECT_TOOLING.md` with toggle env vars.

Validation:
- `scripts/run_boundary_hardening.sh` passed end-to-end.
- Normal stage: `stack_engine 21/0`, `scope_region 51/0`, `unified 1182/0`, `compiler 73/0`.
- ASAN stage: `stack_engine 20/0`, `scope_region 51/0`, `unified 1181/0`, `compiler 73/0`.
- Harness summary present and passing in both stages.

Next:
- Wire this script (or equivalent env profile) into external CI where available.

## Fiber TEMP Phase 6i Progress (2026-03-05)

Completed:
- Added machine-checkable summary assertions to `scripts/run_boundary_hardening.sh`.
- The runner now captures normal/ASAN logs and enforces:
  - `stack_engine/scope_region/unified/compiler fail=0`
  - `stack_affinity_harness fail=0` when harness is enabled
  - `fiber_temp_pool enabled=1` when Fiber TEMP is enabled
- Added assertion toggle:
  - `OMNI_BOUNDARY_ASSERT_SUMMARY=0` to disable assertion checks.

Validation:
- `scripts/run_boundary_hardening.sh` passed with Stage 5 assertions.
- Produced log artifacts:
  - `build/boundary_hardening_normal.log`
  - `build/boundary_hardening_asan.log`

Next:
- Reuse these logs in external CI to publish concise summary artifacts.

## Fiber TEMP Phase 6j Progress (2026-03-05)

Completed:
- Added machine-readable boundary summary emission:
  - new parser script `scripts/parse_boundary_summary.sh`,
  - runner Stage 6 emits `build/boundary_hardening_summary.json`.
- Added runner controls:
  - `OMNI_BOUNDARY_EMIT_JSON`
  - `OMNI_BOUNDARY_SUMMARY_JSON`

Validation:
- `scripts/run_boundary_hardening.sh` passed:
  - Stage 5 assertions pass,
  - Stage 6 JSON artifact generated and verified for both normal/ASAN sections.

Next:
- Hook `build/boundary_hardening_summary.json` as a CI artifact in external workflow.

## Fiber TEMP Phase 6k Progress (2026-03-05)

Completed:
- Added external CI wiring for boundary profile:
  - `.github/workflows/boundary-hardening.yml`
  - manual trigger (`workflow_dispatch`)
  - runs `scripts/run_boundary_hardening.sh`
  - uploads normal/ASAN logs + JSON summary artifact

Validation:
- Local boundary profile remains green and emits expected artifacts.
- Workflow is non-disruptive (manual trigger only).

Next:
- Optionally add a PR/comment bot step to surface key summary fields from `boundary_hardening_summary.json`.

## Fiber TEMP Phase 6l Progress (2026-03-05)

Completed:
- Added CI job-summary renderer:
  - `scripts/emit_boundary_job_summary.sh`
  - workflow now publishes compact boundary result table to `$GITHUB_STEP_SUMMARY`.
- This complements artifact upload with immediate, in-page diagnostics.

Validation:
- Local summary-renderer dry-run showed expected PASS rows and fail-field values.
- `scripts/run_boundary_hardening.sh` remains green end-to-end.

Next:
- Optional: add a lightweight PR comment bridge consuming the same renderer output.

## Fiber TEMP Phase 6m Progress (2026-03-05)

Completed:
- Added optional PR-comment bridge in boundary workflow:
  - new `workflow_dispatch` input `pr_number`,
  - workflow posts boundary summary markdown to the specified PR when set.
- Added workflow-level permissions required for PR comments and included markdown summary artifact upload.

Validation:
- Shell scripts syntax-check clean (`bash -n` across boundary tooling scripts).
- Summary renderer output remains correct on existing normal/ASAN logs.

Next:
- Optional: deduplicate/replace prior boundary bot comments (update-last-comment policy) to reduce PR noise.

## Fiber TEMP Phase 6n Progress (2026-03-05)

Completed:
- Implemented boundary PR comment upsert policy in workflow:
  - marker-based detection,
  - update existing bot comment when present,
  - create only when absent.

Validation:
- Workflow script path remains deterministic and non-invasive (manual dispatch + optional input).

Next:
- Optional: switch from list-first-page to paginated lookup if comment volume grows beyond 100 comments.

## Fiber TEMP Phase 6o Progress (2026-03-05)

Completed:
- Implemented paginated PR comment lookup in boundary workflow upsert path (up to 10 pages).
- Marker-based create-or-update behavior preserved.

Validation:
- CI workflow script remains valid and non-invasive (manual dispatch + optional `pr_number`).

Next:
- Optional: tighten target identity from generic `Bot` type to GitHub Actions bot account if needed.

## Fiber TEMP Phase 5b Progress (2026-03-05)

Completed:
- Added scheduler thread-boundary coverage for Fiber TEMP context metrics:
  - repeated `thread-spawn`/`thread-join` cycles under flag,
  - verifies `ctx_take_hits`/`ctx_return_count` remain unchanged when no stack context is active.
- Preserved mixed scheduler boundary stress semantics without introducing invalid invariants for stack-context-involving paths.

Validation:
- Normal: `Stack engine 19/0`, `Scope region 51/0`, `Unified 1179/0`, `Compiler 73/0`.
- ASAN strict: `Stack engine 18/0`, `Scope region 51/0`, `Unified 1178/0`, `Compiler 73/0`.

Next:
- Add targeted scheduler wakeup/offload interleaving stress with Fiber TEMP enabled to widen boundary-race coverage.

## Session Rules

Global rule for every session:

```bash
c3c build
LD_LIBRARY_PATH=/usr/local/lib ./build/main
```

Memory/lifetime touching sessions additionally require:

```bash
c3c build --sanitize=address
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 LD_LIBRARY_PATH=/usr/local/lib ./build/main
```

Commit discipline:
- One session can produce 1-3 commits.
- Keep commits phase-local and revertable.
- Update `memory/CHANGELOG.md` for behavior-impacting lifetime changes.

Hard stop conditions:
1. Stack engine needs direct `ScopeRegion` operations to proceed.
2. Any per-type RC lifetime appears for language graph values.
3. New ASAN UAF/double-free/leak appears after integration.
4. Clone/discard behavior becomes non-deterministic.

---

## Wave A: Baseline + Defer Substrate (Sessions 1-6)

### Session 1: Baseline Freeze + Acceptance Contract

Goal:
- Lock baseline behavior and invariant contract.

Work:
- Record current teardown/clone failure surfaces and expected invariants.
- Update:
  - `.claude/plans/fiber-temp-teardown-revision-summary.md`
  - `.claude/plans/session-34-44-boundary-hardening.md`
  - `memory/CHANGELOG.md` (baseline snapshot)

Commits:
1. `docs: freeze teardown+clone invariants and baseline gates`

Validation:
- Global rule only.

### Session 2: Stack Engine API Skeleton

Goal:
- Introduce generic defer API surface without runtime wiring.

Likely files:
- `src/stack_engine.c3`

Work:
- Add `stack_ctx_defer` and `stack_ctx_undefer` API.
- Add generic `DeferOps` callback contract (`destroy`, `clone`).
- Add basic stack-engine unit tests for API shape.

Commits:
1. `stack: add generic defer API and DeferOps contract`

Validation:
- Global rule + ASAN.

### Session 3: Destroy-Path Defer Execution

Goal:
- Ensure stack destroy path runs remaining defers deterministically.

Likely files:
- `src/stack_engine.c3`

Work:
- Implement LIFO defer execution on destroy.
- Add tests for:
  - normal return (no destroy callback)
  - destroy with multiple defers (ordering)

Commits:
1. `stack: execute defer callbacks on destroy in deterministic order`

Validation:
- Global rule + ASAN.

### Session 4: Clone-Path Defer Replay

Goal:
- Ensure clone path re-establishes semantic ownership via `clone` callbacks.

Likely files:
- `src/stack_engine.c3`

Work:
- Implement clone-time defer metadata replay.
- Invoke `ops.clone(arg)` during stack clone flow.
- Add clone-specific tests:
  - clone without destroy
  - clone then destroy both original and clone

Commits:
1. `stack: add defer clone replay hooks for semantic ownership duplication`

Validation:
- Global rule + ASAN.

### Session 5: Runtime Boundary Wiring (JIT/Boundary Core)

Goal:
- Use defer substrate for scope teardown safety in core boundary paths.

Likely files:
- `src/lisp/jit_jit_eval_scopes.c3`
- `src/lisp/eval_boundary_api.c3`

Work:
- Register cleanup at boundary entry.
- Remove assumptions that epilogue always executes naturally.

Commits:
1. `jit/boundary: register scope cleanup via generic defer substrate`

Validation:
- Global rule + ASAN + targeted:
  - `--filter continuation`

### Session 6: Runtime Boundary Wiring (Env/Promotion Context)

Goal:
- Extend defer-backed cleanup to env/promotion-sensitive paths.

Likely files:
- `src/lisp/eval_env_copy.c3`
- `src/lisp/eval_promotion_context.c3`

Work:
- Wire boundary cleanup for env/promotion transitions that can cross suspend/clone paths.

Commits:
1. `lifetime: harden env/promotion boundaries with defer-backed cleanup`

Validation:
- Global rule + ASAN + targeted:
  - `--filter continuation`
  - `--filter scheduler`

---

## Wave B: Regression Lock + Performance Stabilization (Sessions 7-9)

### Session 7: Teardown Regression Pack

Goal:
- Lock bug class with deterministic regression cases.

Likely files:
- `src/lisp/tests_tests.c3`
- `src/lisp/tests_escape_scope_tests.c3`
- `src/stack_engine.c3` tests

Work:
- Add tests for:
  - suspend then destroy before natural return
  - clone/discard ordering permutations
  - mixed JIT/eval transitions

Commits:
1. `tests: add teardown and clone boundary regression pack`

Validation:
- Global rule + ASAN.

### Session 8: Stress + Flake Check

Goal:
- Ensure tests are stable across repetition and order variation.

Likely files:
- test files only (plus minor harness hooks if needed)

Work:
- Run repeated targeted tests and tighten nondeterministic assertions.

Commits:
1. `tests: stabilize continuation/scheduler stress assertions`

Validation:
- Global rule + ASAN + repeated targeted runs.

### Session 9: Defer Overhead Stabilization

Goal:
- Reduce hot-path cost without changing API/semantics.

Likely files:
- `src/stack_engine.c3`
- `src/lisp/jit_jit_compiler.c3`
- `src/lisp/jit_jit_eval_scopes.c3`

Work:
- Add counters/bench hooks.
- Optimize internal defer storage strategy (implementation detail only).

Commits:
1. `perf: optimize defer substrate internals with no API change`
2. `perf/tests: add lightweight no-regression checks`

Validation:
- Global rule + ASAN.

---

## Wave C: Fiber-Temp Design + Flagged Plumbing (Sessions 10-13)

### Session 10: Fiber-Temp Design Freeze

Goal:
- Convert RnD concept into implementation-spec with explicit guardrails.

Likely files:
- `memory/DESTINATION_ARENA_PLAN.md`
- `.claude/plans/fiber-temp-teardown-revision-summary.md`
- `memory/CHANGELOG.md`

Work:
- Define:
  - eligibility for TEMP routing
  - non-eligible classes
  - clone/suspend constraints
  - rollback strategy

Commits:
1. `docs: freeze fiber-temp design constraints and proof obligations`

Validation:
- Global rule only.

### Session 11: Chunk Pool + Feature Flag Skeleton

Goal:
- Introduce fiber-temp plumbing behind flag, default OFF.

Likely files:
- `src/stack_engine.c3`
- `src/scope_region.c3`
- `src/lisp/value_interp_state.c3`

Work:
- Add chunk metadata/lifecycle primitives.
- Add feature flag path with no behavior change when OFF.

Commits:
1. `runtime: add fiber-temp chunk pool skeleton behind feature flag`

Validation:
- Global rule + ASAN.

### Session 12: TEMP Backing Integration (Flag OFF/ON parity basics)

Goal:
- Connect temp backing context to stack/fiber lifecycle with safe default behavior.

Likely files:
- `src/stack_engine.c3`
- `src/scope_region.c3`
- `src/lisp/value_interp_state.c3`

Work:
- Wire plumbing paths and ensure OFF mode parity.
- Add basic ON-mode sanity tests.

Commits:
1. `runtime: wire fiber-temp backing lifecycle under guarded mode`

Validation:
- Global rule + ASAN.

### Session 13: Eligibility-Based Routing

Goal:
- Route only safe TEMP allocations to fiber-temp-backed path.

Likely files:
- `src/scope_region.c3`
- `src/lisp/value_constructors.c3`
- `src/lisp/eval_promotion_copy.c3`
- `src/lisp/eval_promotion_escape.c3`

Work:
- Add routing eligibility checks.
- Keep non-eligible allocations on deterministic ESCAPE path.

Commits:
1. `runtime: add eligibility-based TEMP routing for fiber-temp mode`

Validation:
- Global rule + ASAN + targeted continuation tests.

---

## Wave D: Clone/Scheduler Safety + Rollout Decision (Sessions 14-16)

### Session 14: Suspend/Clone Safety for Fiber-Temp

Goal:
- Ensure clone/suspend correctness in fiber-temp mode.

Likely files:
- `src/stack_engine.c3`
- `src/scope_region.c3`
- `src/lisp/scheduler_io_fiber_core.c3`
- `src/lisp/scheduler_wakeup_io.c3`

Work:
- Implement safe freeze/share (or equivalent) semantics for temp backing during clone/suspend.
- Ensure destroy reclaims correctly.

Commits:
1. `runtime: harden fiber-temp clone/suspend semantics`

Validation:
- Global rule + ASAN + targeted continuation/scheduler stress.

### Session 15: Scheduler/Thread Boundary Audit

Goal:
- Verify thread-boundary correctness with fiber-temp and defer substrate.

Likely files:
- `src/lisp/scheduler_thread_tasks.c3`
- `src/lisp/scheduler_offload_worker.c3`
- `src/lisp/scheduler_tcp_async_bridge.c3`
- `src/lisp/scheduler_primitives.c3`
- test files

Work:
- Audit callback mutation boundaries.
- Add/expand stress tests for offload wakeups and teardown races.

Commits:
1. `scheduler: enforce boundary-safe teardown and wakeup invariants`
2. `tests: add scheduler-thread teardown stress coverage`

Validation:
- Global rule + ASAN + repeated scheduler stress runs.

### Session 16: Rollout Decision + Cleanup

Goal:
- Decide production posture and clean dead paths only if gates are satisfied.

Likely files:
- `memory/CHANGELOG.md`
- `memory/DESTINATION_ARENA_PLAN.md`
- runtime files touched in prior sessions

Work:
- Decide default flag policy (likely keep OFF initially).
- Remove dead fallback code only where parity is proven.
- Publish final operations/invariant note.

Commits:
1. `docs/runtime: publish fiber-temp readiness decision and invariant status`
2. `cleanup: remove proven-dead fallback paths (if any)`

Validation:
- Full global + ASAN + targeted stress matrix.

---

## Deliverable Checkpoints

Checkpoint A (after Session 6):
- Defer substrate integrated into boundary paths; no known teardown leak path unresolved.

Checkpoint B (after Session 9):
- Regressions locked; defer overhead acceptable.

Checkpoint C (after Session 13):
- Fiber-temp plumbing and eligibility routing functional behind feature flag.

Checkpoint D (after Session 16):
- Production rollout decision with evidence and rollback plan.

## Notes on Throughput

- If sessions are run in parallel branches, merge order must still follow dependency chain:
  - Sessions 1-6 serial required.
  - Sessions 7-9 can partially overlap.
  - Sessions 10-16 should be mostly serial due to shared core files.
- Prefer one active high-risk runtime session at a time to minimize conflict and hidden regressions.
