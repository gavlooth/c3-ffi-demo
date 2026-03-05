# Fiber Temp Detailed Implementation Plan

Date: 2026-03-05  
Status: Complete for current architecture target (fiber-temp optional/guarded backing, region-centric ownership unchanged)

## 1. Objective

Build a safe path from current teardown/clone lifetime bugs to a production-grade fiber-local TEMP backing model, without drifting from Omni's region-centric ownership model.

Target outcome:
- Immediate correctness for suspended-context destroy and clone semantics.
- Stable boundary invariants across JIT/eval/scheduler edges.
- Optional fiber-temp backing for eligible TEMP allocations, under strict gates.

## 2. Hard Constraints

1. Ownership authority remains region/boundary-centric (`TEMP`/`ESCAPE`), not per-type RC for language values.
2. Stack engine remains generic and opaque. No direct `ScopeRegion` operations in stack core.
3. Finalizer-bearing resources stay on deterministic ESCAPE/lifetime paths.
4. No root-pinning expansion as correctness fallback.
5. Every phase must pass build + full suite + ASAN gates before moving forward.

## 3. Global Validation Gates (run after each phase)

```bash
c3c build
LD_LIBRARY_PATH=/usr/local/lib ./build/main
c3c build --sanitize=address
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 LD_LIBRARY_PATH=/usr/local/lib ./build/main
```

Targeted gates for lifetime/concurrency work:

```bash
LD_LIBRARY_PATH=/usr/local/lib ./build/main --filter continuation
LD_LIBRARY_PATH=/usr/local/lib ./build/main --filter scheduler
```

## 3.1 Execution Checkpoint (2026-03-05)

Requested scope for this session:
- Complete `1/2/3`, defer `4`.

Checkpoint outcome:
1. Design freeze sign-off: **pass**.
2. Ownership/guardrail checkpoint: **conditional pass**.
3. Defer hot-path perf sign-off: **pass**.
4. Deferred to next session by request.

Evidence:
- Normal validation:
  - `c3c build`
  - `OMNI_TEST_QUIET=1 LD_LIBRARY_PATH=/usr/local/lib ./build/main`
  - Result: `Stack engine 14/0`, `Unified 1177/0`, `Compiler 73/0`.
- ASAN strict validation:
  - `c3c clean`
  - `c3c build --sanitize=address`
  - `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 OMNI_TEST_QUIET=1 LD_LIBRARY_PATH=/usr/local/lib ./build/main`
  - Result: `Stack engine 14/0`, `Unified 1177/0`, `Compiler 73/0`.
- Defer hot-path counters:
  - `OMNI_STACK_DEFER_METRICS=1 OMNI_TEST_SUMMARY=1 OMNI_TEST_QUIET=1 LD_LIBRARY_PATH=/usr/local/lib ./build/main`
  - `STACK_DEFER_METRICS push=8 undefer=1 destroy_cb=9 clone_cb=1 update_arg=2 cloned_entries=2 peak_depth=3 heap_alloc=0`

Item `4` follow-up status:
- Completed: legacy stack-layer scope coupling removed (`stack_ctx_pin_scope`, `stack_ctx_unpin_scope`, `pinned_scope`).
- Suspend-lifetime scope retention is now owned by runtime boundary code through generic defer-backed guards.

Phase 1 scaffold status:
- Completed conservative Fiber TEMP skeleton in `scope_region` under `OMNI_FIBER_TEMP`:
  - chunk-pool helpers + counters,
  - TEMP-lane chunk reclaim hook in destroy/reset/splice paths,
  - no default behavior change with flag OFF.

Phase 2 initial gate status:
- Completed context-level eligibility routing:
  - `ScopeRegion.fiber_temp_eligible` set only for scopes created inside active stack contexts.
  - TEMP lane routing is eligibility-aware; ESCAPE lane remains raw.
  - Added stack-engine in-context scope create/release test to exercise flagged pool metrics.
- Extended allocation-shape whitelist:
  - Pool routing remains size-gated (`<= 4096` slow-path request).
  - Eligible scopes now bypass pool routing after ESCAPE-lane activity appears (`escape_chunks`/`escape_dtors`), further constraining Fiber TEMP to ephemeral TEMP-heavy scope shapes.
  - Flagged summary confirms bypass path exercise (`bypass_escape=2`).

Enablement substrate update:
- Added a defer-independent `StackCtx` lifecycle callback channel (attach/find/clone/destroy) so persistent per-context resources can be owned safely without interfering with LIFO `stack_ctx_undefer(...)` behavior.
- Fiber TEMP now uses lifecycle-backed per-context chunk caches (with clone-share refcount) before global fallback.
- Added stack-engine clone/discard stress coverage for Fiber TEMP-enabled contexts.
- Added scheduler/offload boundary test coverage asserting Fiber TEMP context-cache counters remain unchanged for worker-thread operations without stack contexts.
- Extended scheduler suite coverage to keep this boundary invariant explicit while avoiding false constraints on mixed stack-context stress paths.
- Added deterministic wakeup/offload interleaving stress to widen scheduler boundary-race coverage.
- Added targeted cancellation/timeout + offload-cancel stress coverage for destroy-before-complete scheduler boundaries.
- Added explicit Fiber TEMP lifecycle telemetry counters and clone/discard delta assertions (context-pool creation, lifecycle clone/destroy/deferred/flush events).
- Added long-run retention guard test for repeated clone/discard lifecycle cycles with bounded pooled-count invariant.
- Added stack-engine thread-affinity hardening:
  - `StackPool` and `StackCtx` now carry owner-thread tokens,
  - create/destroy/init/switch/suspend/resume/clone/shutdown paths enforce ownership,
  - targeted ownership-state test added to stack-engine suite.
- Extended affinity enforcement into stack defer/lifecycle APIs:
  - defer register/pop/update and lifecycle attach/find/clone/destroy/clear now assert owner-thread access,
  - closes API-level misuse gap for stack-owned teardown metadata.
- Added an opt-in stack-affinity misuse probe (`--stack-affinity-probe`) to exercise fail-fast ownership violations in a controlled, non-default path.
- Added optional harness wrapper (`OMNI_STACK_AFFINITY_HARNESS=1`) in default test mode to execute probe as a subprocess and emit machine-readable pass/fail summary.
- Added reusable boundary-hardening runner script (`scripts/run_boundary_hardening.sh`) to execute normal + ASAN profile with Fiber TEMP and affinity harness defaults.
- Added summary assertion pass in boundary runner to enforce required suite signals (`fail=0`) and feature flags (`fiber_temp_pool enabled=1` when enabled).
- Added JSON summary artifact emission (`scripts/parse_boundary_summary.sh`) for normal/ASAN boundary profile logs.
- Added external CI entrypoint (`.github/workflows/boundary-hardening.yml`) to run boundary profile and upload logs/JSON artifacts.
- Added CI job-summary rendering step (`scripts/emit_boundary_job_summary.sh`) for quick boundary signal visibility.
- Added optional workflow-dispatch PR comment bridge for boundary summary publication (`pr_number` input).
- Updated PR comment bridge to upsert (marker-based update) to avoid repeated comment spam.
- Hardened upsert lookup with paginated PR comment scan for long threads.
- Tightened upsert target identity to `github-actions[bot]` for deterministic bot-comment updates.
- Added configurable bot-login workflow input for upsert targeting, preserving secure default.

## 4. Phase Plan

### Phase 0: Baseline and Invariant Freeze

Goal:
- Freeze baseline behavior and explicit invariants before new substrate work.

Likely files:
- `memory/CHANGELOG.md`
- `.claude/plans/fiber-temp-teardown-revision-summary.md`
- `.claude/plans/session-34-44-boundary-hardening.md`

Changes:
- Document current failing scenarios and ownership invariants as explicit acceptance gates.
- Record baseline run outputs and known ASAN findings.

Risk:
- Low.

Exit criteria:
- Invariant list is explicit and reviewable.
- Baseline test/ASAN outputs recorded.

### Phase 1: Generic Defer Substrate in Stack Engine

Goal:
- Introduce a generic defer API that can run cleanup on destroy and duplicate semantic ownership on clone.

Likely files:
- `src/stack_engine.c3`
- `src/lisp/value_core_types.c3` (opaque callback type exposure only if needed)

Changes:
- Add generic `stack_ctx_defer(...)` and `stack_ctx_undefer(...)`.
- Add `DeferOps` contract with at least:
  - `destroy(void* arg)`
  - `clone(void* arg)`
- Ensure destroy path drains defers in deterministic order.
- Ensure clone path re-establishes defer metadata and invokes `clone` hooks.
- Keep defer storage strategy internal and swappable.

Risk:
- Medium.

Exit criteria:
- Stack tests cover normal return, destroy, clone, and clone+destroy permutations.
- No stack layer dependency on Lisp memory structs.

### Phase 2: Wire Defer Substrate into Runtime Boundaries

Goal:
- Replace ad-hoc scope teardown assumptions with explicit boundary cleanup registration.

Likely files:
- `src/lisp/jit_jit_eval_scopes.c3`
- `src/lisp/eval_boundary_api.c3`
- `src/lisp/eval_env_copy.c3`
- `src/lisp/eval_promotion_context.c3`

Changes:
- Register boundary cleanup actions through generic defer API.
- Remove direct assumptions that epilogues always run naturally.
- Ensure clone hooks perform semantic ownership duplication (`scope_retain` style behavior where required).

Risk:
- Medium.

Exit criteria:
- Suspended destroy and clone paths do not leak or double-release.
- Existing behavior remains unchanged in normal execution paths.

### Phase 3: Regression Expansion for Teardown and Clone

Goal:
- Lock down the bug class with targeted tests.

Likely files:
- `src/lisp/tests_tests.c3`
- `src/lisp/tests_escape_scope_tests.c3`
- `src/stack_engine.c3` (engine-level tests if present here)

Changes:
- Add focused tests for:
  - suspend then destroy before natural return,
  - multi-shot clone/discard ordering,
  - mixed JIT/eval boundary transitions,
  - effect/continuation interleavings across boundaries.

Risk:
- Low.

Exit criteria:
- New tests fail on pre-fix behavior and pass on current branch.
- ASAN passes with leak detection enabled.

### Phase 4: Defer Substrate Performance Stabilization

Goal:
- Keep correctness while reducing hot-path overhead.

Likely files:
- `src/stack_engine.c3`
- `src/lisp/jit_jit_compiler.c3`
- `src/lisp/jit_jit_eval_scopes.c3`

Changes:
- Profile defer registration overhead.
- Optimize internal storage strategy without API changes.
- Add micro-bench or runtime counters for defer push/pop frequency and cost.

Risk:
- Medium.

Exit criteria:
- No statistically significant regression in current hot paths.
- Correctness suite unchanged.

### Phase 5: Fiber-Temp Design Freeze (R&D to design-ready)

Goal:
- Produce an implementation-spec for fiber-temp that cannot violate ownership guardrails.

Likely files:
- `memory/DESTINATION_ARENA_PLAN.md`
- `.claude/plans/fiber-temp-teardown-revision-summary.md`
- New design appendix (if needed)

Changes:
- Define eligibility rules for TEMP allocation routing.
- Define non-eligible classes (finalizer-bearing, boundary-crossing unsafe classes).
- Define clone/suspend semantics for temp backing chunks.
- Define rollback strategy and feature flag boundaries.

Risk:
- Medium.

Exit criteria:
- Design reviewed and accepted as compatible with region-centric ownership.
- No unresolved ownership-authority ambiguity.

### Phase 6: Fiber-Temp Chunk Pool and Arena Plumbing (behind flag)

Goal:
- Introduce runtime plumbing for fiber-local TEMP backing under a guarded feature flag.

Likely files:
- `src/stack_engine.c3`
- `src/scope_region.c3`
- `src/lisp/value_interp_state.c3`
- `src/lisp/scheduler_state_offload.c3` (if stack context lifecycle crosses scheduler paths)
- New helper module if needed (for chunk pool internals)

Changes:
- Add chunk metadata and lifecycle operations.
- Bind temp backing context to stack/fiber lifecycle.
- Keep default behavior unchanged when flag is disabled.

Risk:
- High.

Exit criteria:
- Feature flag OFF matches baseline behavior.
- Feature flag ON passes engine-level and runtime-level invariants for basic cases.

### Phase 7: Eligibility-Based TEMP Routing

Goal:
- Route only safe TEMP allocations to fiber-temp backing.

Likely files:
- `src/scope_region.c3`
- `src/lisp/value_constructors.c3`
- `src/lisp/eval_promotion_copy.c3`
- `src/lisp/eval_promotion_escape.c3`

Changes:
- Add eligibility checks for routing into fiber-temp-backed TEMP.
- Keep non-eligible allocations on existing deterministic ESCAPE/lifetime paths.
- Preserve promotion/boundary semantics.

Risk:
- High.

Exit criteria:
- No new UAF/double-free in ASAN.
- No drift in boundary correctness tests.

### Phase 8: Suspend/Clone Semantics for Fiber-Temp

Goal:
- Make clone/suspend behavior safe under fiber-temp backing.

Likely files:
- `src/stack_engine.c3`
- `src/scope_region.c3`
- `src/lisp/scheduler_io_fiber_core.c3`
- `src/lisp/scheduler_wakeup_io.c3`

Changes:
- Implement freeze/share or equivalent safe policy for cloned/suspended temp chunks.
- Ensure resumed execution allocates into valid writable backing.
- Ensure destroy decrements/reclaims resources safely.

Risk:
- High.

Exit criteria:
- Clone/discard permutation tests pass repeatedly.
- ASAN remains clean under stress runs.

### Phase 9: Scheduler/Thread Boundary Safety Audit

Goal:
- Ensure fiber-temp and defer teardown semantics are safe across scheduler/offload boundaries.

Likely files:
- `src/lisp/scheduler_thread_tasks.c3`
- `src/lisp/scheduler_offload_worker.c3`
- `src/lisp/scheduler_tcp_async_bridge.c3`
- `src/lisp/scheduler_primitives.c3`

Changes:
- Verify callbacks do not mutate region-owned state outside approved consumer paths.
- Keep cross-thread communications event-based and ownership-safe.
- Add stress tests for multi-producer wakeups and teardown timing races.

Risk:
- High.

Exit criteria:
- No thread-affinity violations.
- Stress tests stable across repeated runs.

### Phase 10: Production Rollout and Cleanup

Goal:
- Graduate fiber-temp from experiment to controlled production feature (if all gates pass).

Likely files:
- `memory/CHANGELOG.md`
- `memory/DESTINATION_ARENA_PLAN.md`
- Runtime files touched in earlier phases

Changes:
- Promote feature flag policy (default off to default on only after confidence window).
- Remove dead fallback code only after measured parity.
- Publish final invariant and operational guide.

Risk:
- Medium to high.

Exit criteria:
- Sustained green CI with ASAN.
- Performance and memory behavior meet acceptance targets.
- Rollback path remains available for one release window.

## 5. Testing Strategy by Layer

Layer A: Stack engine unit tests
- Defer order, destroy behavior, clone behavior, clone+destroy permutations.

Layer B: Runtime boundary tests
- Return/env/promotion/copy boundary correctness under suspend/resume.

Layer C: Scheduler stress tests
- Event-loop wakeups, offload interactions, teardown under load.

Layer D: ASAN/stability tests
- Full suite with leak detection enabled.
- Repeat stress runs to detect order-sensitive regressions.

## 6. Rollback and Stop Conditions

Stop immediately and rollback phase changes if any of these occur:

1. Stack engine requires direct `ScopeRegion` access to make progress.
2. Any proposal introduces per-type RC ownership for language graph values.
3. ASAN reports new UAF/double-free/leak regressions after phase integration.
4. Clone/discard tests show non-deterministic failures across repeated runs.

Rollback method:
- Keep each phase in separate commits.
- Revert phase-local commits only.
- Preserve test additions and diagnostics docs while reverting risky runtime wiring.

## 7. Practical Milestones

Milestone M1:
- Phases 0-3 complete.
- Teardown and clone correctness stabilized under current ownership model.

Milestone M2:
- Phase 4 complete.
- Defer substrate overhead acceptable and measured.

Milestone M3:
- Phases 5-8 complete behind feature flag.
- Fiber-temp technically functional with passing invariants.

Milestone M4:
- Phases 9-10 complete.
- Fiber-temp rollout decision made with measurable confidence.
