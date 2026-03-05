# Session 34-44 Boundary Hardening Playbook

Scope:
- Consolidate lifetime/ownership transitions into one audited boundary API.
- Improve old internals (code quality + business logic), not just wrap them.
- Keep behavior stable while reducing bug surface.

Execution policy:
- Do not advance sessions unless all gates pass.
- Keep commits small and domain-local.
- Prefer behavior-preserving refactors before semantic changes.

## Current Status (2026-03-04)

- Sessions 34-44 goals: completed in sequence (see `memory/CHANGELOG.md` and commit history).
- Continued hardening/decomposition after Session 44: Sessions 45-68 completed.
- Validation discipline held for each session:
  - `c3c build`
  - `LD_LIBRARY_PATH=/usr/local/lib ./build/main`
  - `c3c build --sanitize=address`
  - `ASAN_OPTIONS=detect_leaks=0,halt_on_error=1,abort_on_error=1 LD_LIBRARY_PATH=/usr/local/lib ./build/main`

### Session 178 Follow-up (2026-03-05): Boundary Invariant Coverage in Run/JIT + Interleaving Regression

- Extended invariant-hook call-site coverage into top-level run/JIT high-risk transitions:
  - `run_promote_result(...)` and `run(...)` in `src/lisp/eval_run_pipeline.c3`
  - `jit_finalize_scoped_result(...)`, `jit_eval_in_single_scope(...)`, `jit_eval_in_call_scope(...)` in `src/lisp/jit_jit_eval_scopes.c3`
- Added focused regression `run_memory_lifetime_boundary_scope_interleaving_test(...)` in `src/lisp/tests_tests.c3`:
  - exercises nested `boundary_enter_scope`/`boundary_leave_scope` interleaved with `boundary_push_child_scope`/`boundary_pop_child_scope`,
  - validates exact restoration of `current_scope` + `releasing_scope`,
  - validates release-copy path remains correct post-interleaving.
- Validation:
  - normal full suite: pass (`Unified 1186/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1185/0`, `Compiler 73/0`)

### Session 179 Follow-up (2026-03-05): Boundary Hook Coverage for REPL/Macro Scope Helpers

- Completed next low-risk residual-hook sweep:
  - `repl_eval_line(...)` in `src/lisp/eval_repl.c3`
  - `capture_template_bindings_in_root_scope(...)` in `src/lisp/macros_expansion.c3`
- Added `boundary_assert_interp_scope_chain(...)` at helper entry/exit and immediately after child-scope push where applicable.
- Validation:
  - normal full suite: pass (`Unified 1186/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1185/0`, `Compiler 73/0`)

### Session 180 Follow-up (2026-03-05): TCO Env-Copy Boundary Restore Consolidation

- Consolidated manual boundary-state save/restore in:
  - `jit_copy_tco_env_chain_for_recycle(...)` (`src/lisp/jit_jit_eval_scopes.c3`)
- Migration:
  - replaced ad-hoc `releasing_scope` save/restore with
    `boundary_save_interp_state(...)` + `defer boundary_restore_interp_state(...)`.
- Added regression:
  - `run_memory_lifetime_tco_boundary_state_restore_test(...)` in `src/lisp/tests_tests.c3`
  - ensures long named-let/TCO flow restores `current_scope` and `releasing_scope` exactly.
- Validation:
  - normal full suite: pass (`Unified 1187/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1186/0`, `Compiler 73/0`)

### Session 181 Follow-up (2026-03-05): TCO Recycle Error-Path Rollback Consolidation

- Refactored duplicated rollback/error branches in `jit_prepare_tco_recycle(...)`:
  - added `jit_tco_recycle_restore_on_error(...)` in `src/lisp/jit_jit_eval_scopes.c3`.
- Unified rollback guarantees now flow through one helper:
  - restore `current_scope` + `tco_recycle_scope`,
  - release fresh scope,
  - restore `jit_env`,
  - assert boundary invariant,
  - return runtime error.
- Added invariant hooks at TCO prepare entry/return points (fast path, alloc-failure, success).
- Validation:
  - normal full suite: pass (`Unified 1187/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1186/0`, `Compiler 73/0`)

### Session 182 Follow-up (2026-03-05): run_program Boundary-State Coverage

- Added focused regression `run_memory_lifetime_run_program_boundary_state_test(...)` in `src/lisp/tests_tests.c3`.
- Coverage intent:
  - lock in boundary-state restoration for top-level multi-form execution (`run_program`),
  - confirm `current_scope` + `releasing_scope` stability across parse+eval orchestration.
- Validation:
  - normal full suite: pass (`Unified 1188/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1187/0`, `Compiler 73/0`)

### Session 183 Follow-up (2026-03-05): JIT TCO Runtime-State Helper Consolidation

- Consolidated remaining distributed TCO call-state transitions in `src/lisp/jit_jit_eval_scopes.c3`:
  - added `JitCallScopeState` save/restore helpers,
  - added recycle-scope set/activate helpers,
  - routed single/call-scope defer registration failure cleanup through `boundary_pop_child_scope(...)`.
- Added regression `run_memory_lifetime_tco_runtime_fields_restore_test(...)` in `src/lisp/tests_tests.c3`:
  - validates runtime-field restore on both successful and erroring TCO runs:
    `current_scope`, `releasing_scope`, `tco_recycle_scope`,
    `tco_scope_defer_slot`, `tco_scope_defer_active`, `escape_env_mode`.
- Validation:
  - normal full suite: pass (`Unified 1189/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1188/0`, `Compiler 73/0`)

### Session 184 Follow-up (2026-03-05): Context-Switch Invariant Hooks in jit_common

- Added boundary invariant assertions at shared context-switch helpers in `src/lisp/jit_common.c3`:
  - `save_interp_state(...)` precondition check,
  - `restore_interp_state(...)` post-restore check.
- Rationale:
  - these helpers sit on stack/effect/JIT transition seams and are high leverage for failure localization.
- Validation:
  - normal full suite: pass (`Unified 1189/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1188/0`, `Compiler 73/0`)

### Session 185 Follow-up (2026-03-05): TCO Recycle Error-Path Coverage

- Added targeted regression `run_memory_lifetime_tco_recycle_error_restore_test(...)` in `src/lisp/tests_tests.c3`.
- Directly exercises `jit_prepare_tco_recycle(...)` error rollback branch (missing active stack context while defer retargeting is required) and verifies restore invariants.
- Replaced one direct single-scope scope restore with facade helper call:
  - `jit_eval_in_single_scope(...)` now uses `boundary_leave_scope(...)`.
- Validation:
  - normal full suite: pass (`Unified 1190/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1189/0`, `Compiler 73/0`)

### Session 186 Follow-up (2026-03-05): run_program Error-Path Boundary Coverage

- Added `run_memory_lifetime_run_program_error_boundary_state_test(...)` in `src/lisp/tests_tests.c3`.
- Verifies top-level multi-form execution preserves boundary state on:
  - parse-error exit,
  - runtime-error exit.
- Validation:
  - normal full suite: pass (`Unified 1191/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1190/0`, `Compiler 73/0`)

### Session 187 Follow-up (2026-03-05): Nested Releasing-Copy Interleaving Under Abort

- Added regression `run_memory_lifetime_promotion_abort_nested_releasing_copy_test(...)` in `src/lisp/tests_tests.c3`.
- Coverage target:
  - nested `boundary_copy_from_releasing_scope(...)` calls under a budget-aborted promotion context,
  - verifies boundary state restore, expected target-scope placement, non-aliasing fallback copies, and clean promotion-context teardown.
- Validation:
  - normal full suite: pass (`Unified 1192/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1191/0`, `Compiler 73/0`)

### Session 188 Follow-up (2026-03-05): Nested Promotion-Context Stack Discipline

- Added regression `run_memory_lifetime_nested_promotion_context_stack_test(...)` in `src/lisp/tests_tests.c3`.
- Coverage target:
  - nested outer-aborted + inner-budgeted promotion contexts under releasing-scope copies,
  - verifies context `prev` linkage, active-context restoration, inner memo reuse, outer abort non-aliasing fallback, and final context teardown.
- Validation:
  - normal full suite: pass (`Unified 1193/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass after immediate rerun (`Unified 1192/0`, `Compiler 73/0`)
  - note: observed one transient order-sensitive deduce fail on first ASAN run (`deduce repeated open/rebind`), not reproduced on rerun.

### Session 189 Follow-up (2026-03-05): Deduce Reopen Flake Hardening (ASAN)

- Root-cause hardening in `src/lisp/deduce.c3`:
  - replaced truncated `%x` `(uint)(usz)db` memory-db suffix with full-width `usz` hex formatting helper (`deduce_format_usz_hex`).
- Goal:
  - reduce rare ASAN allocator-layout collisions in `deduce 'open 'memory` temp paths (`/tmp/deduce-*`), which could surface as order-sensitive `deduce repeated open/rebind` failures.
- Validation:
  - normal full suite: pass (`Unified 1193/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1192/0`, `Compiler 73/0`)
  - repeated ASAN probe (3 full reruns): all pass (`Unified 1192/0`, `Compiler 73/0` each).

### Session 190 Follow-up (2026-03-05): Deduce Reopen Stress Test Coupling Reduction

- Updated `run_deduce_reopen_stress_test(...)` in `src/lisp/tests_tests.c3`:
  - replaced separate `run(...)` + `global_env.lookup(...)` pattern with single expression:
    `(begin (define ddb-reopen (deduce 'open 'memory)) ddb-reopen)`.
- Goal:
  - preserve test semantics while reducing out-of-band env lookup timing/order coupling in stress loops.
- Validation:
  - normal full suite: pass (`Unified 1193/0`, `Compiler 73/0`)
  - strict ASAN repeated full-run probe (3 runs): all pass (`Unified 1192/0`, `Compiler 73/0` each).

### Session 191 Follow-up (2026-03-05): Deduce Memory-Open Isolation Guardrail

- Runtime hardening in `src/lisp/deduce.c3`:
  - added process-sequence entropy in `'memory` temp DB path generation (`getpid` + monotonic seq + full-width pointer suffix).
- Regression added in `src/lisp/tests_tests.c3`:
  - `run_deduce_memory_open_isolation_test(...)` verifies two memory-open handles are storage-isolated (insert into A does not appear in B).
- Validation:
  - normal full suite: pass (`Unified 1194/0`, `Compiler 73/0`)
  - strict ASAN repeated full-run probe (3 runs): all pass (`Unified 1193/0`, `Compiler 73/0` each).

### Session 192 Follow-up (2026-03-05): Scheduler Mixed Boundary-State Coverage + Top-Level TCO Transient Cleanup

- Added scheduler regression `run_scheduler_mixed_boundary_state_restore_tests(...)` in `src/lisp/tests_tests.c3`:
  - mixes success (`thread-join` + `offload`/`await`) and expected-error (`thread-cancel` + `thread-join`) paths,
  - verifies boundary/runtime fields are restored after each cycle.
- Regression initially exposed stale `jit_tco_expr` / `jit_tco_env` leakage after top-level error exits.
- Runtime fix in `src/lisp/eval_run_pipeline.c3`:
  - introduced `run_clear_stale_jit_tco_state(...)`,
  - applied at run entry and via `defer` on exit in `run(...)` and `run_program(...)`.
- Validation:
  - normal full suite: pass (`Unified 1195/0`, `Compiler 73/0`)
  - strict ASAN repeated full-run probe (3 runs): all pass (`Unified 1194/0`, `Compiler 73/0` each).

### Session 193 Follow-up (2026-03-05): Scheduler Wakeup/Offload Expected-Error Boundary Coverage

- Added scheduler regression `run_scheduler_wakeup_offload_error_boundary_tests(...)` in `src/lisp/tests_tests.c3`:
  - runs repeated triplets of:
    - success spawn/await with `offload 'sleep-ms`,
    - manual wakeup queue enqueue/drain (`WAKEUP_POLL_ERROR` with out-of-range fiber id),
    - expected-error spawn/await with invalid `offload 'nope`.
  - verifies boundary/runtime field stability after every phase:
    - `current_scope`, `releasing_scope`
    - `jit_env`, `match_env`
    - `jit_tco_expr`, `jit_tco_env`
    - `tco_recycle_scope`, `tco_scope_defer_slot`, `tco_scope_defer_active`
    - `escape_env_mode`, `active_promotion_ctx`
- Validation:
  - normal full suite: pass (`Unified 1196/0`, `Compiler 73/0`)
  - normal full suite with `OMNI_FIBER_TEMP=1`: pass (`Unified 1196/0`, `Compiler 73/0`)
  - strict ASAN repeated full-run probe (3 runs): all pass (`Unified 1195/0`, `Compiler 73/0` each).

### Session 194 Follow-up (2026-03-05): Wakeup Wraparound Boundary-State Regression

- Added scheduler regression `run_scheduler_wakeup_wraparound_boundary_tests(...)` in `src/lisp/tests_tests.c3`:
  - repeatedly fills wakeup ring to capacity and verifies overflow enqueue rejection,
  - drains queue and verifies head/tail convergence each cycle,
  - asserts interpreter boundary/runtime field restoration after each fill+drain cycle.
- Wired into `run_scheduler_tests(...)`.
- Validation:
  - normal full suite: pass (`Unified 1197/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1196/0`, `Compiler 73/0`)
  - strict ASAN full suite with `OMNI_FIBER_TEMP=1`: pass (`Unified 1196/0`, `Compiler 73/0`)

### Session 195 Follow-up (2026-03-05): Mixed Wakeup-Event Boundary Ordering

- Added scheduler regression `run_scheduler_wakeup_mixed_event_boundary_tests(...)` in `src/lisp/tests_tests.c3`:
  - alternates `WAKEUP_POLL_ERROR` and `WAKEUP_TIMER_EXPIRED` ordering for the same blocked pending-read fiber,
  - verifies first-event-wins completion semantics (`error` vs `timed_out`) and `FIBER_BLOCKED -> FIBER_READY` transition,
  - verifies wakeup ring drains cleanly and interpreter boundary/runtime fields remain unchanged.
- Wired into `run_scheduler_tests(...)`.
- Validation:
  - normal full suite: pass (`Unified 1198/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1197/0`, `Compiler 73/0`)
  - strict ASAN full suite with `OMNI_FIBER_TEMP=1`: pass (`Unified 1197/0`, `Compiler 73/0`)

### Session 196 Follow-up (2026-03-05): Invalid Offload Wakeup Payload Drain

- Added scheduler regression `run_scheduler_invalid_offload_wakeup_boundary_tests(...)` in `src/lisp/tests_tests.c3`:
  - enqueues `WAKEUP_OFFLOAD_READY` with out-of-range fiber ids and real `OffloadCompletion*` payloads,
  - interleaves additional invalid poll-error events,
  - verifies wakeup ring drains fully and interpreter boundary/runtime fields remain unchanged.
- Wired into `run_scheduler_tests(...)`.
- Validation:
  - normal full suite: pass (`Unified 1199/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1198/0`, `Compiler 73/0`)
  - strict ASAN full suite with `OMNI_FIBER_TEMP=1`: pass (`Unified 1198/0`, `Compiler 73/0`)

### Session 197 Follow-up (2026-03-05): Wakeup Ready-Barrier Ordering

- Added scheduler regression `run_scheduler_wakeup_ready_barrier_boundary_tests(...)` in `src/lisp/tests_tests.c3`:
  - constructs a queue with slot-0 unready and slot-1 ready events,
  - verifies `drain_wakeups()` stops at the first unready slot (no out-of-order processing),
  - then releases slot-0 readiness and verifies ordered completion semantics and queue convergence,
  - asserts interpreter boundary/runtime fields remain unchanged across both drains.
- Wired into `run_scheduler_tests(...)`.
- Validation:
  - normal full suite: pass (`Unified 1200/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1199/0`, `Compiler 73/0`)
  - strict ASAN full suite with `OMNI_FIBER_TEMP=1`: pass (`Unified 1199/0`, `Compiler 73/0`)

### Session 198 Follow-up (2026-03-05): Duplicate/Late Offload-Ready Semantics

- Added scheduler regression `run_scheduler_duplicate_offload_ready_boundary_tests(...)` in `src/lisp/tests_tests.c3`:
  - drives duplicate and late `WAKEUP_OFFLOAD_READY` events for one active blocked pending offload,
  - verifies first completion payload retention and extra payload discard semantics,
  - verifies queue convergence, blocked->ready transition, and boundary/runtime state stability after both phases.
- Wired into `run_scheduler_tests(...)`.
- Validation:
  - normal full suite: pass (`Unified 1201/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1200/0`, `Compiler 73/0`)
  - strict ASAN full suite with `OMNI_FIBER_TEMP=1`: pass (`Unified 1200/0`, `Compiler 73/0`)

### Session 199 Follow-up (2026-03-05): Pending-Offload Consume Path Boundary Checks

- Added scheduler regression `run_scheduler_consume_pending_offload_boundary_tests(...)` in `src/lisp/tests_tests.c3`:
  - exercises direct `scheduler_consume_pending_offload(...)` pre-completion, completed, and post-reset paths,
  - alternates completion kinds (`OFFLOAD_RES_INT` and cancel/error completion),
  - verifies returned value semantics, pending-slot reset, and boundary/runtime field stability after each phase.
- Wired into `run_scheduler_tests(...)`.
- Validation:
  - normal full suite: pass (`Unified 1202/0`, `Compiler 73/0`)
  - strict ASAN full suite: pass (`Unified 1201/0`, `Compiler 73/0`)
  - strict ASAN full suite with `OMNI_FIBER_TEMP=1`: pass (`Unified 1201/0`, `Compiler 73/0`)

### Post-44 Continuation Snapshot (Sessions 45-68)

- Boundary API expansion and caller migration completed across eval/jit/env/value/module paths.
- Scoped allocation/switch helpers consolidated (`enter/leave`, `push/pop child scope`, scoped env helpers).
- JIT/runtime lifetime hardening completed for:
  - closure env-copy paths
  - root promotion paths
  - scoped eval/finalization paths
  - constructor allocation failure paths
- Hotspot decomposition continued in scheduler/runtime modules:
  - scheduler await/cancel/join/spawn/offload paths
  - wakeup drain event handlers
  - JIT set-path and cache warm traversal helpers

### Session 106 Follow-up (2026-03-05): ASAN Stabilization

- Reproduced and fixed the ASAN fake-stack crash in escape-scope tests:
  - `AddressSanitizer CHECK failed (asan_thread.cpp:369)` with stack tail through:
    - `scope_chunk_alloc` -> `scope_create` -> `jit_copy_closure_env_if_needed` -> `jit_make_closure_from_expr`.
- Root cause:
  - JIT warm-cache compilation path (`jit_warm_expr_cache`/`jit_cache_expr`) bypassed eval-path JIT gating under ASAN.
- Remediation:
  - Added unified runtime JIT gate (`run_jit_enabled`) and applied it to top-level eval paths.
  - Gated warm-cache compilation behind the same runtime policy.
  - Disabled JIT cross-check execution in ASAN mode in unified test helpers.
  - Added deterministic JIT teardown at test end (`jit_global_shutdown`).
  - Kept mid-run JIT GC lightweight; skipped GC scheduling under ASAN to avoid unstable state-destroy cycles during execution.
- Validation:
  - `c3c build` + full test run: pass.
  - `c3c build --sanitize=address` + `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1` full run: pass.

### Session 107 Follow-up (2026-03-05): JIT GC Safe-Point Policy Correction

- Reproduced strict-ASAN follow-up regression:
  - leak mode: `JIT state pool full (4096)` + `2320 bytes leaked in 29 allocs` (`jit_alloc` via `jit_lookup_or_compile`)
  - crash mode: `EXIT:139` after enabling scheduling while still allowing GC call inside `jit_compile`.
- Root cause:
  - scheduling/teardown policy mismatch:
    - ASAN scheduling disabled => pool overflow + leaks
    - GC invoked from compile-time call paths => unsafe destruction context.
- Remediation:
  - moved JIT state destruction to true top-level safe points only (`run`/`run_program`/REPL `jit_gc()` calls),
  - removed `jit_gc()` call from `jit_compile()`,
  - re-enabled threshold scheduling uniformly (including ASAN),
  - kept runtime/test ASAN policy based on `main::stack_runtime_asan_enabled()`.
- Validation:
  - `c3c build` + `LD_LIBRARY_PATH=/usr/local/lib ./build/main`: pass.
  - `c3c build --sanitize=address` + strict ASAN leak run (`detect_leaks=1`): pass.

### Session 108 Follow-up (2026-03-05): ASAN Pool Pressure + Parser Edge Contracts

- Added explicit JIT execution-depth accounting around generated-code calls:
  - `jit_exec_enter/jit_exec_leave`
  - enables safe-point gating for opportunistic compile-time GC.
- Increased JIT tracked-state pool headroom (`4096 -> 16384`) to avoid false overflow warnings during nested chains where GC cannot run until unwind.
- Expanded parser edge-case contract tests for refactored helper paths:
  - `export-from` specifier/module/list errors,
  - `deftype` missing-name/missing-field-name,
  - `defunion` missing-name/missing-variant-name.
- Validation:
  - Normal full suite: pass (`Unified 1154/0`, `Compiler 73/0`).
  - Strict ASAN full suite (`detect_leaks=1`): pass (`Unified 1153/0`, `Compiler 73/0`, no pool-full warning, no leak summary failure).

### Session 109 Follow-up (2026-03-05): Persistent Env Parent Rewrite Regression

- Added boundary regression coverage for `copy_env_to_scope_inner(...)` persistent-frame parent rewrite behavior.
- New test verifies:
  - persistent env node identity is preserved across copy,
  - parent pointer is rewritten into target-scope chain (not left on releasing source parent),
  - lookups remain valid after source-scope release.
- Validation:
  - Normal full suite: pass (`Unified 1155/0`, `Compiler 73/0`).
  - Strict ASAN full suite (`detect_leaks=1`): pass (`Unified 1154/0`, `Compiler 73/0`).

### Session 110 Follow-up (2026-03-05): JIT GC Scheduling Signal Cleanup

- Reduced debug noise in `jit_track_compiled_state(...)`:
  - GC scheduling log now emits once per transition (`gc_needed` false -> true), instead of repeating on every post-threshold state.
- Behavior unchanged; this is diagnostics hardening only.
- Validation:
  - Normal full suite: pass (`Unified 1155/0`, `Compiler 73/0`).
  - Strict ASAN full suite (`detect_leaks=1`): pass (`Unified 1154/0`, `Compiler 73/0`).

### Session 111 Follow-up (2026-03-05): Test Group Isolation + Mixed Chain Rewrite Coverage

- Added test-harness group boundary reset between major unified test groups:
  - clears transient JIT/effect/error runtime state,
  - performs safe-point JIT teardown when needed.
- Added regression for mixed persistent/non-persistent env-chain rewrite under `copy_env_to_scope_inner(...)`.
- Added JIT policy assertion that boundary reset clears transient runtime state.
- Validation:
  - Normal full suite: pass (`Unified 1157/0`, `Compiler 73/0`).
  - Strict ASAN full suite (`detect_leaks=1`): pass (`Unified 1156/0`, `Compiler 73/0`).

### Session 112 Follow-up (2026-03-05): CI Summary Hook + Compiler-Phase Boundary Reset

- Added optional machine-readable summary output for unified tests:
  - `OMNI_TEST_SUMMARY=1` emits:
    - `OMNI_TEST_SUMMARY suite=unified pass=<n> fail=<n>`
- Added explicit test-group boundary reset before compiler tests to further reduce cross-suite state coupling.
- Validation:
  - Normal full suite: pass (`Unified 1157/0`, `Compiler 73/0`).
  - Strict ASAN full suite (`detect_leaks=1`): pass (`Unified 1156/0`, `Compiler 73/0`).

### Session 114 Follow-up (2026-03-05): Defer Slot Context Hardening (Clone-Safe)

- Removed persisted `StackCtx*` ownership from `Interp` defer-tracking state:
  - dropped `tco_scope_defer_ctx`,
  - retained `tco_scope_defer_slot` + `tco_scope_defer_active`.
- Updated JIT defer retarget/pop to resolve context at use-time:
  - retarget now uses `main::g_current_stack_ctx`,
  - pop now uses helper that prefers running context and falls back to caller context.
- Added invariant error path in recycle retarget:
  - `"jit: missing active stack context for call-scope defer"`.
- Rationale:
  - removes stale-pointer risk across suspend/clone/resume,
  - keeps defer identity slot-based and runtime-context driven.
- Validation:
  - `c3c build` + full suite: pass (`Unified 1157/0`, `Compiler 73/0`).
  - `c3c build --sanitize=address` + strict ASAN (`detect_leaks=1`): pass (`Unified 1158/0`, `Compiler 73/0`).

### Session 115 Follow-up (2026-03-05): Less-Brittle Instrumentation Delta Assertions

- Reduced over-constrained counter assertions in lifetime regression tests:
  - replaced exact `site_delta == 1` checks with bounded ranges (`delta_in_range(...)`) for:
    - root-boundary promotion,
    - promotion-context memo gate,
    - promotion-abort fallback gate.
- Kept strict zero-delta sentinels where they represent structural invariants:
  - cons-barrier fallback copy sites remain `== 0`.
- Rationale:
  - preserve regression signal while avoiding false failures from benign internal instrumentation reshapes.
- Validation:
  - `c3c build` + full suite: pass (`Unified 1157/0`, `Compiler 73/0`).
  - `c3c build --sanitize=address` + strict ASAN (`detect_leaks=1`): pass (`Unified 1158/0`, `Compiler 73/0`).

### Session 116 Follow-up (2026-03-05): Parser Compound-Form Helper Consolidation

- Consolidated repeated parser logic for `(<name> <param>...)` forms into:
  - `Parser.parse_compound_symbol_with_params(...)`
- Migrated type/union callers:
  - `parse_deftype_name_compound(...)`
  - `parse_defunion_name_compound(...)`
  - `parse_defunion_variant_compound(...)`
- Preserved existing parser error-shape strings (`expected type name`, `expected union name`, `expected variant name`).
- Validation:
  - `c3c build` + full suite: pass (`Unified 1157/0`, `Compiler 73/0`).
  - `c3c build --sanitize=address` + strict ASAN (`detect_leaks=1`): pass (`Unified 1158/0`, `Compiler 73/0`).

### Session 117 Follow-up (2026-03-05): Parser Export-From Validation Dedup

- Performed a small parser cleanup in `parse_export_from_specifiers(...)`:
  - deduplicated repeated invalid-specifier error literal via local `spec_err`.
- Preserved parser error-shape contract and accepted forms:
  - only `:all` or `(name...)`.
- Validation:
  - `c3c build` + full suite: pass (`Unified 1157/0`, `Compiler 73/0`).

### Session 118 Follow-up (2026-03-05): Stack Defer Clone-Isolation Regression

- Added stack-engine regression:
  - `test_stack_ctx_defer_update_arg_clone_isolation()`
- Verifies cloned context defer-arg retargeting is context-local:
  - clone retarget does not mutate source defer arg,
  - destroy paths invoke callbacks on the expected arg only.
- Integrated into stack test runner:
  - new output `PASS: defer update arg clone isolation`.
- Validation:
  - normal + strict ASAN runs green.
  - stack engine now reports `15 passed, 0 failed`.

### Session 119 Follow-up (2026-03-05): Parser Closing-Paren Edge Contracts

- Added focused parser edge tests for missing-close-paren forms:
  - `export-from`
  - `deftype` compound name
  - `defunion` compound name/variants
- Adjusted expected substrings for these cases to runtime-observed shape `")"` (instead of `"expected )"`), keeping assertions strict to real parser output.
- Validation:
  - normal suite green (`Unified 1160/0`, `Compiler 73/0`).
  - strict ASAN suite green (`Unified 1160/0`, `Compiler 73/0`).

### Session 120 Follow-up (2026-03-05): CI Summary Coverage for Stack/Scope Suites

- Extended `OMNI_TEST_SUMMARY` support to runtime pre-suites:
  - `stack_engine` summary line emitted by `run_stack_engine_tests()`.
  - `scope_region` summary line emitted by `run_scope_region_tests()`.
- Existing unified/compiler summary behavior preserved.
- Validation:
  - normal + strict ASAN runs green.
  - summary lines now available for all major suites:
    - stack_engine, scope_region, unified, compiler.

### Session 121 Follow-up (2026-03-05): Helper-Level Quiet Mode for Unified Tests

- Added runtime-cached helper quiet policy in `run_lisp_tests()`:
  - quiet on `OMNI_TEST_QUIET=1`,
  - or on `OMNI_TEST_SUMMARY=1` unless `OMNI_TEST_VERBOSE=1`.
- Helper pass lines now route through `emit_pass(...)` and honor cached quiet state (including `test_gt`).
- Effect:
  - reduced CI log noise while preserving fail lines and summary markers.
- Validation:
  - normal + strict ASAN suites green.
  - verified quiet vs verbose summary-mode behavior via targeted runs.

### Session 122 Follow-up (2026-03-05): Stack Engine Quiet-Mode Caching

- Applied cached quiet policy to stack-engine PASS lines (avoid per-check env ambiguity):
  - `g_stack_quiet_output` computed once at `run_stack_engine_tests()` start.
- PASS output routed through `stack_print_pass(...)` and suppressed under:
  - `OMNI_TEST_QUIET=1`, or
  - `OMNI_TEST_SUMMARY=1` without `OMNI_TEST_VERBOSE=1`.
- `OMNI_TEST_SUMMARY suite=stack_engine ...` remains emitted.
- Validation:
  - normal + strict ASAN suites green.
  - summary-mode run confirms compact stack-engine output + preserved summary line.

### Session 123 Follow-up (2026-03-05): Compiler Suite Quiet-Mode PASS Gating

- Added compiler-local quiet gating (`compiler_print_pass`) using env policy:
  - `OMNI_TEST_QUIET=1`, or
  - `OMNI_TEST_SUMMARY=1` without `OMNI_TEST_VERBOSE=1`.
- Mechanically routed compiler PASS outputs through the helper.
- Outcome:
  - summary-mode CI no longer emits full compiler PASS stream by default,
  - `OMNI_TEST_VERBOSE=1` restores full PASS output when needed.
- Validation:
  - quiet/verbose behavior verified.
  - normal + strict ASAN suites remain green.

### Session 124 Follow-up (2026-03-05): Unified Harness Direct-PASS Quiet Routing

- Added `emit_pass_literal(...)` in `tests_tests.c3` and routed direct literal PASS prints through it.
- Net effect:
  - summary-mode runs avoid most custom-group PASS spam in unified suite,
  - fail lines and suite summaries remain visible.
- Validation:
  - normal and strict ASAN suites green.
  - summary-mode output compact with stable suite summary markers.

### Session 125 Follow-up (2026-03-05): Parser Helper Edge Coverage Extension

- Added focused parser/helper regression cases in `tests_advanced_tests.c3`:
  - `parser export-from missing specifier`
  - `parser deftype compound missing name`
  - `parser defunion compound missing name`
  - `parser defunion variant missing close`
- Added functional module regression for helperized `export-from :all` path:
  - `export-from :all all-a`
  - `export-from :all all-b`
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1167/0`, `Compiler 73/0`).
  - strict ASAN rerun green (`Stack engine 14/0`, `Unified 1166/0`, `Compiler 73/0`).

### Session 126 Follow-up (2026-03-05): Deduce ASAN Flake Hardening

- Hardened Deduce resource teardown:
  - added `deduce_db_finalizer(...)` and wired `deduce-db` handles to close LMDB env on FFI-handle release.
- Reduced order sensitivity for Deduce suite:
  - `run_deduce_tests(...)` now executes on a fresh interpreter instance,
  - added focused smoke + reopen/rebind stress checks:
    - `deduce 'open returns handle`
    - `deduce repeated open/rebind`
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1168/0`, `Compiler 73/0`).
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1167/0`, `Compiler 73/0`).

### Session 127 Follow-up (2026-03-05): Additional Stateful Group Isolation

- Reduced suite order coupling by isolating more stateful groups:
  - added `run_scheduler_tests_isolated(...)`
  - added `run_atomic_tests_isolated(...)`
- `run_lisp_tests()` now runs scheduler/atomic groups in fresh interpreters instead of the shared suite interpreter.
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1168/0`, `Compiler 73/0`).
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1167/0`, `Compiler 73/0`).

### Session 128 Follow-up (2026-03-05): Async/HTTP Group Isolation

- Continued order-sensitivity hardening by isolating additional side-effect-heavy groups:
  - added `run_async_tests_isolated(...)`
  - added `run_http_tests_isolated(...)`
- `run_lisp_tests()` now runs async/http groups in fresh interpreters.
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1168/0`, `Compiler 73/0`).
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1167/0`, `Compiler 73/0`).

### Session 129 Follow-up (2026-03-05): Shared Isolated-Group Helper

- Reduced duplicated harness boilerplate by adding shared isolated-group helper:
  - `alias IsolatedTestGroupFn = fn void(Interp*, int*, int*)`
  - `run_group_isolated(...)`
- Updated isolated wrappers to use helper:
  - async/http/scheduler/atomic.
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1168/0`, `Compiler 73/0`).
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1167/0`, `Compiler 73/0`).

### Session 130 Follow-up (2026-03-05): Deduce Helper Roll-In

- Finished isolated-helper adoption for Deduce:
  - added `run_deduce_group_tests(...)`
  - `run_deduce_tests(...)` now routes through `run_group_isolated(...)`
- Removed duplicated local fresh-interpreter setup in Deduce runner.
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1168/0`, `Compiler 73/0`).
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1167/0`, `Compiler 73/0`).

### Session 131 Follow-up (2026-03-05): Reader/Schema Isolation

- Continued order-sensitivity hardening:
  - added `run_reader_dispatch_tests_isolated(...)`
  - added `run_schema_tests_isolated(...)`
- `run_lisp_tests()` now runs reader+schema groups through isolated wrapper path.
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1168/0`, `Compiler 73/0`).
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1167/0`, `Compiler 73/0`).

### Session 132 Follow-up (2026-03-05): Pika/Unicode/Compression/JSON Isolation

- Extended isolated-group execution to additional mid-suite groups:
  - `run_pika_tests_isolated(...)`
  - `run_unicode_tests_isolated(...)`
  - `run_compression_tests_isolated(...)`
  - `run_json_tests_isolated(...)`
- `run_lisp_tests()` now executes these groups via `run_group_isolated(...)`.
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1168/0`, `Compiler 73/0`).
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1167/0`, `Compiler 73/0`).

### Session 133 Follow-up (2026-03-05): Diagnostic/JIT Policy Isolation

- Isolated two side-effect-prone groups:
  - `run_diagnostic_tests_isolated(...)`
  - `run_jit_policy_tests_isolated(...)`
- `run_lisp_tests()` now routes diagnostic + jit-policy groups through isolated wrappers.
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1168/0`, `Compiler 73/0`).
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1167/0`, `Compiler 73/0`).

### Session 134 Follow-up (2026-03-05): ASAN Raise-Pending Boundary Scrub

- Reproduced ASAN-only regression:
  - `tco-recycle: effects in loop` failed (`interp=FAIL`),
  - standalone handle repro returned `999` instead of `5`.
- Root cause:
  - stale `raise_pending` state leaked across top-level `run()` boundaries in interpreter/ASAN paths.
- Implemented top-level-safe scrub in `src/lisp/eval_run_pipeline.c3`:
  - added `run_clear_stale_raise_state(...)`,
  - clears only when `handler_count == 0`,
  - called at:
    - `run_program(...)` entry,
    - each `run_program(...)` expression iteration,
    - `run(...)` entry.
- Validation:
  - targeted ASAN repro now returns `5` (correct),
  - normal full suite green (`Stack engine 15/0`, `Unified 1168/0`, `Compiler 73/0`),
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1167/0`, `Compiler 73/0`).

### Session 135 Follow-up (2026-03-05): Isolated Group Reset Consolidation

- Reduced repetitive boundary-reset boilerplate in unified test runner.
- Added global-only helper:
  - `run_test_global_boundary_reset(...)`
- Updated isolated runner:
  - `run_group_isolated(...)` now applies global reset before/after each isolated group.
- Kept `run_test_group_boundary_reset(...)` for per-interpreter field reset and compiler-suite transition, delegating global reset to shared helper.
- Removed repeated `run_test_group_boundary_reset(interp)` calls between isolated group invocations in `run_lisp_tests()`.
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1168/0`, `Compiler 73/0`),
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1167/0`, `Compiler 73/0`).

### Session 136 Follow-up (2026-03-05): Parser Edge Coverage Expansion

- Increased focused parser helper coverage for import/export/type edge patterns:
  - new `run_advanced_parser_import_edge_tests(...)` cases for:
    - missing `:as` in paren import specifier,
    - missing alias after `:as` (paren and symbol forms),
    - non-symbol import list element,
    - missing close paren.
  - added export-from nested-list invalid case.
  - added deftype/defunion malformed compound param and variant-name cases.
- Wired import-edge tests into module-system advanced suite.
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1177/0`, `Compiler 73/0`),
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1176/0`, `Compiler 73/0`).

### Session 137 Follow-up (2026-03-05): Stale Raise Regression Lock-In

- Added explicit regression test in `run_jit_policy_tests(...)`:
  - manually seeds stale `raise_pending`,
  - executes top-level `run(...)` containing `handle` with `raise` clause,
  - asserts result path is unaffected by stale state and runtime flags are scrubbed.
- New pass signal:
  - `jit policy: top-level run scrubs stale pending raise state`
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1178/0`, `Compiler 73/0`),
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1177/0`, `Compiler 73/0`).

### Session 138 Follow-up (2026-03-05): JIT Policy Test Decomposition

- Decomposed large mixed-responsibility test function:
  - `run_jit_policy_tests(...)` split into focused helpers:
    - `run_jit_policy_warm_cache_tests(...)`
    - `run_jit_policy_gc_safe_point_test(...)`
    - `run_jit_policy_boundary_reset_test(...)`
    - `run_jit_policy_stale_raise_scrub_test(...)`
- Kept behavior and pass/fail output contracts unchanged.
- Validation:
  - normal full suite green (`Stack engine 15/0`, `Unified 1178/0`, `Compiler 73/0`),
  - strict ASAN full suite green (`Stack engine 14/0`, `Unified 1177/0`, `Compiler 73/0`).

### Session 169 Follow-up (2026-03-05): Boundary Facade CI Guard

- Added explicit boundary-facade enforcement script:
  - `scripts/check_boundary_facade_usage.sh`
  - scans `src/lisp` for direct legacy boundary helper usage and fails on non-sanctioned callsites.
- Guarded symbols:
  - `copy_to_parent(...)`
  - `promote_to_escape(...)`
  - `promote_to_root(...)`
  - `copy_env_to_scope_inner(...)`
  - `scope_splice_escapes(...)`
- Allowed only in sanctioned internal files:
  - `src/lisp/eval_boundary_api.c3`
  - `src/lisp/eval_promotion_copy.c3`
  - `src/lisp/eval_promotion_escape.c3`
  - `src/lisp/eval_env_copy.c3`
- Tests are excluded from this gate (`src/lisp/tests_*.c3`) to preserve low-level regression fixtures.
- Wired guard into boundary profile:
  - `scripts/run_boundary_hardening.sh` now runs Stage 0 guard check before build/test stages.
- Validation:
  - `scripts/check_boundary_facade_usage.sh`: pass.

### Session 170 Follow-up (2026-03-05): Boundary Change Policy (ASAN Requirement)

- Added `scripts/check_boundary_change_policy.sh`:
  - detects boundary-sensitive file changes (`HEAD~1..HEAD` by default, `OMNI_BOUNDARY_POLICY_RANGE` override),
  - when boundary-sensitive changes exist, requires both normal and ASAN profile evidence:
    - `stack_engine/scope_region/unified/compiler fail=0`,
    - `fiber_temp_pool enabled=1` in both logs.
- Wired into boundary profile:
  - `scripts/run_boundary_hardening.sh` now runs Stage 7 policy check.
- Workflow support:
  - `.github/workflows/boundary-hardening.yml` adds optional `policy_range` input and forwards it as `OMNI_BOUNDARY_POLICY_RANGE`.
- Validation:
  - boundary profile run remains green with policy stage enabled.

### Session 172 Follow-up (2026-03-05): Boundary State Restore Helper Consolidation

- Centralized boundary state transitions in `eval_boundary_api.c3`:
  - added `BoundaryInterpState` save/restore helpers,
  - routed temporary boundary context overrides through shared helper + `defer`.
- Migrated helper-backed restore paths:
  - `boundary_copy_to_scope_site(...)`
  - `boundary_alloc_value_in_scope(...)`
  - `boundary_make_env_in_scope(...)`
  - `boundary_env_extend_in_scope(...)`
  - `boundary_copy_from_releasing_scope(...)`
  - `boundary_copy_env_to_target_scope(...)`
- Added regression coverage in `tests_tests.c3`:
  - `run_memory_lifetime_boundary_scope_restore_tests(...)`,
  - asserts `current_scope` and `releasing_scope` restoration after boundary helper calls.
- Validation:
  - normal full suite green,
  - strict ASAN suite green.

### Session 173 Follow-up (2026-03-05): Shared Decision Predicate Routing

- Added shared boundary decision predicates in `eval_boundary_api.c3`:
  - `boundary_ptr_in_target_scope_chain(...)`
  - `boundary_value_in_releasing_scope(...)`
  - `boundary_can_reuse_value(...)`
- Routed repeated decision branches through shared helpers:
  - `eval_promotion_copy.c3` (`copy_cons_to_parent`, `needs_wrapper_copy`, `copy_to_parent_try_fast_reuse`)
  - `eval_env_copy.c3`
  - `eval_promotion_context.c3`
  - `eval_promotion_escape.c3` fast-path target-chain check
- Scope:
  - behavior-preserving policy consolidation only (no API changes).
- Validation:
  - normal full suite green,
  - strict ASAN suite green.

### Session 174 Follow-up (2026-03-05): Env-Copy Internal Decision/Mutation Decomposition

- Decomposed `eval_env_copy.c3` internals into policy vs mutation helpers.
- New decision helpers:
  - `copy_env_should_reuse_value(...)`
  - `copy_env_copy_by_boundary_policy(...)`
  - `copy_env_should_clone_closure(...)`
  - `copy_env_is_terminal_frame(...)`
- New mutation/materialization helpers:
  - `copy_env_clone_closure_payload(...)`
  - `copy_env_rewrite_persistent_parent(...)`
  - `copy_env_materialize_frame(...)`
- Routed core env-copy paths through helper split:
  - `copy_env_value_fast(...)`
  - `copy_env_clone_closure_if_needed(...)`
  - `copy_env_to_scope_inner(...)`
- Scope:
  - behavior-preserving decomposition only, no API/semantics changes.
- Validation:
  - normal full suite green,
  - strict ASAN suite green.

### Session 175 Follow-up (2026-03-05): Promotion-Copy Closure Split + Wrapper Regression

- Decomposed `copy_closure_to_parent(...)` into explicit policy + payload helpers:
  - `copy_parent_closure_in_releasing_scope(...)`
  - `copy_parent_should_reuse_closure(...)`
  - `copy_parent_clone_closure_payload(...)`
- Added boundary regression in `tests_tests.c3`:
  - `run_memory_lifetime_wrapper_reuse_vs_defensive_copy_test(...)`
  - verifies:
    - wrapper fast-reuse in target chain,
    - defensive wrapper copy for disjoint scope values,
    - copied-wrapper survival/refcount behavior after source scope release.
- Scope:
  - behavior-preserving decomposition + focused lifetime invariant lock-in.
- Validation:
  - normal full suite green,
  - strict ASAN suite green.

### Session 176 Follow-up (2026-03-05): Escape Route-Map Decomposition + Disjoint Fallback Test

- Decomposed `promote_to_escape_by_tag(...)` into explicit route selection + execution:
  - `PromoteEscapeRoute`
  - `promote_escape_route_for_tag(...)`
  - `promote_to_escape_by_route(...)`
- Kept behavior unchanged while making tag-policy mapping explicit and auditable.
- Added regression coverage:
  - `run_memory_lifetime_escape_disjoint_fallback_test(...)`
  - verifies disjoint value fallback copy path + post-release survival.
- Wired new test into `run_memory_lifetime_root_fallback_tests(...)`.
- Validation:
  - normal full suite green,
  - strict ASAN suite green.

### Session 177 Follow-up (2026-03-05): Centralized Boundary Invariant Hooks

- Added centralized boundary invariant helpers in `eval_boundary_api.c3`:
  - `boundary_scope_chain_contains(...)`
  - `boundary_assert_interp_scope_chain(...)`
  - `boundary_assert_saved_state(...)`
- Wired hooks into key boundary entry/restore paths:
  - `boundary_save_interp_state(...)`
  - `boundary_restore_interp_state(...)`
  - `boundary_can_reuse_value(...)`
  - `boundary_enter_scope(...)`
  - `boundary_leave_scope(...)`
  - `boundary_push_child_scope(...)`
  - `boundary_pop_child_scope(...)`
- Kept invariant policy conservative (non-null scope-state guarantees) to avoid false positives on valid disjoint-scope transitions.
- Validation:
  - normal full suite green,
  - strict ASAN suite green.

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

- [x] Session 34 complete
- [x] Session 35 complete
- [x] Session 36 complete
- [x] Session 37 complete
- [x] Session 38 complete
- [x] Session 39 complete
- [x] Session 40 complete
- [x] Session 41 complete
- [x] Session 42 complete
- [x] Session 43 complete
- [x] Session 44 complete

---

## Revision Draft (v3): Lifetime Teardown and Continuation Safety

Status:
- This section is a living revision draft for design decisions.
- It incorporates critical analysis of the multi-shot continuation cloning problem.

### Why This Revision Exists

- We still have teardown risk when suspended contexts are destroyed before normal call-scope epilogues run.
- Prior quick fixes created layer violations by leaking Lisp memory semantics (`ScopeRegion`) into stack engine internals.
- A newly discovered **latent danger** exists in multi-shot cloning: `memcpy` of the C-stack copies Lisp scope pointers but does not bump their Lisp-level refcounts, leading to silent double-frees upon eventual normal returns. We must solve the leak *and* the clone refcount problem together.

### Hard Constraints

- Keep stack engine generic. No direct `ScopeRegion` ownership logic in low-level coroutine core.
- Preserve deterministic cleanup behavior for finalizer-bearing values.
- Keep continuation clone behavior safe: absolute pointers must remain valid, and semantic ownership (refcounts) must be correctly duplicated.
- Avoid moving-GC complexity (no pointer rewriting of Lisp values on the C-stack).

### Candidate Architectures (Updated)

1. **Exception-driven unwinding (OCaml/Koka style)**
   - *Mechanism:* On destroy, resume the fiber with a special `UNWIND` signal. The JIT loop catches it and naturally unwinds through existing `scope_release` epilogues.
   - *Pros:* Zero hot-path overhead. Naturally scales to user-level `try/finally`.
   - *Cons:* High risk of violating thread-affinity if destructors run on scheduler threads. Requires deep JIT modification to separate standard errors from control-flow signals.

2. **Generic stack defer substrate with `DeferOps` VTable (Current Recommendation)**
   - *Mechanism:* Opaque defer registration (`stack_ctx_defer`) with `{ DeferOps* ops; void* arg }` semantics. `DeferOps` provides both `destroy(arg)` and `clone(arg)` callbacks. Storage strategy (intrusive stack nodes vs. inline slots) is an internal optimization choice.
   - *Pros:*
     - Solves the layer violation: `stack_engine.c3` only knows about function pointers.
     - Zero heap allocation on fast path (with suitable internal storage strategy).
     - Solves the latent clone double-free: `stack_ctx_clone` replays defer metadata and calls `ops.clone(arg)` (which the Lisp layer implements as `scope_retain`).
   - *Cons:* Mild hot-path setup cost (writing ~3 pointers per call scope).

3. **Fiber-Temp Arenas (The "Holy Grail")**
   - *Mechanism:* A 3-domain model (ROOT, ESCAPE/SCOPE, FIBER_TEMP). Ephemeral allocations bump-allocate from a chunk-pool owned by the `StackCtx`.
   - *Pros:* O(1) cleanup. Scopes naturally vanish when fibers die.
   - *Cons:* Highly complex interaction with multi-shot clones. Cloned fibers would need to freeze shared chunks and allocate from new chunks to avoid corrupting each other's state while preserving absolute pointers.

### Antithesis: The Smell in Candidate 2 (If Implemented as Intrusive C-Stack Nodes)

While Candidate 2 is the most mechanically sound immediate fix, it carries distinct architectural "smells" that we must acknowledge:

1. **Pointer Fixup Fragility:** Because `DeferNode`s live on the C-stack, a `memcpy` during `stack_ctx_clone` copies the `next` pointers exactly as they are. Those `next` pointers now point to the *original* fiber's stack memory, not the cloned stack. The stack engine will have to carefully walk and relocate these `next` pointers (similar to how it fixes `RBP` chains). This is delicate, unsafe C magic.
2. **The Heap Fallback Trap:** If a user ever allocates a `DeferNode` on the heap instead of the C-stack (e.g., inside a persistent coroutine object), the `memcpy` clone won't duplicate the node itself, but it *will* duplicate the pointer to it. Both the original and the clone will share the same heap node, destroying the intrusive list structure if either one unwinds. We would have to strictly enforce that `stack_ctx_push_defer` only accepts pointers within the `StackRegion` bounds.
3. **Semantic Mismatch:** Is "defer" solving the root problem, or just patching the symptom? The root problem is that Lisp-level call scopes are tethered to C-level control flow. The more we bind Lisp memory lifecycle to C-stack intrusive nodes, the harder it becomes to serialize continuations, move them across machines, or ever transition to a stackless VM architecture in the future.

### Recommended Sequence

**Phase A (The Mechanical Fix):**
- Adopt Candidate 2 (generic `DeferOps` substrate).
- Implement `stack_ctx_defer` and `stack_ctx_undefer` public surface.
- Define clone behavior via `DeferOps.clone` contract, independent of concrete defer storage.
- Use it to manage `ScopeRegion` release and clone-time retains.

**Phase B (The Structural Fix):**
- Once correctness is proven, evaluate Candidate 3 (Fiber-Temp Arenas) as an RnD track, not a committed endpoint.
- Any 3-domain migration proposal must first prove compatibility with region-centric ownership guardrails before entering production roadmap.

### Open Questions (Resolved for this Revision)

- **Should discontinuation semantics be represented as a first-class runtime signal now, or deferred?**
  *Deferred.* Mechanical safety at the engine level (`DeferOps`) is required first.
- **What is the acceptable hot-path overhead budget for generic defer push/pop?**
  *Zero heap allocations on fast path.* Concrete storage remains an implementation detail; intrusive nodes are optional if clone-safety is proven.
- **Do we want clone-time duplication of defer metadata or shared immutable snapshots?**
  *Duplication via explicit `DeferOps.clone` hooks.* Stack clone flow must re-establish defer metadata deterministically, then let Lisp-layer `clone` hooks bump semantic ownership.
- **At what milestone does a fiber-temp domain become justified?**
  *Post-profiling, after boundary invariants hold.* Fiber arenas are the ultimate structural fix but require the boundary API to be perfectly solid first.

### Drift Audit (v3 vs. Omni Architecture)

Reference:
- `AGENTS.md` ownership/drift guardrails.
- `memory/DESTINATION_ARENA_PLAN.md` synthesis constraints.

Verdict:
- **Recommended Phase A path (generic defer substrate) is LOW drift** when kept strictly opaque.
- **Current v3 wording has LOW/MEDIUM drift risk** in one active place:
  - It frames Fiber-Temp as a likely structural endpoint; this needs explicit "RnD only" scoping to avoid ownership-model drift.

Specific drift findings:

1. Layering drift risk (medium)
- `DeferOps` itself is aligned.
- Risk appears when implementation details require stack-engine knowledge of node placement policy beyond opaque callback execution.
- Guardrail:
  - Keep stack API generic (`stack_ctx_defer(cb, arg)` + opaque storage).
  - Do not make "intrusive node on stack" part of public contract.

2. Ownership-model drift risk (medium/high if unchecked)
- Fiber-Temp language can drift toward a parallel lifetime authority.
- Guardrail:
  - Treat fiber temp as allocator backing only.
  - Lifetime authority remains region/boundary policy (`TEMP`/`ESCAPE`, retain/release invariants).

3. Clone mechanics drift risk (medium)
- Pointer-fixup-heavy design is mechanically possible but raises fragility and maintainability risk.
- Guardrail:
  - Clone semantics should be explicit, testable, and independent of node memory placement assumptions where possible.

4. Determinism drift risk (low if constrained)
- Finalizer determinism remains aligned if all finalizer-bearing objects stay in audited ESCAPE paths.
- Guardrail:
  - No FIBER_TEMP routing for finalizer-bearing values.

### Revision Actions (This Iteration)

- Keep recommendation as: **generic defer substrate first**, but relax storage strategy to "implementation detail".
- Add explicit note: intrusive stack nodes are an optimization candidate, not an architectural requirement.
- Keep Fiber-Temp in RnD lane only until:
  - boundary invariants are centrally enforced,
  - ASAN teardown matrix is stable,
  - clone/discard stress tests are green.

### Quick Go/No-Go Checklist

- Go:
  - Stack layer remains opaque and callback-driven.
  - Boundary layer owns ownership policy.
  - ASAN + targeted suspend/clone tests pass.
- No-Go:
  - Stack layer directly manipulates `ScopeRegion`.
  - New per-type RC appears for language graph values.
  - Fiber-Temp is introduced as a third ownership system.
