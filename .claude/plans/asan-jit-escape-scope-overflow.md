# ASAN Triage: JIT Multi-Arg + Escape-Scope Overflow

Date: 2026-03-04
Owner: runtime triage
Status: open

## Problem Statement

Under ASAN builds, the unified test suite fails deterministically during:

- `escape-scope: nested map+reverse`

Normal (non-ASAN) runs pass this path.

## Reproduction

```bash
c3c clean
c3c build --sanitize=address
LD_LIBRARY_PATH=/usr/local/lib ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=0 ./build/main
```

Failure appears in `run_escape_scope_tests` after:

- `escape-scope: foldl non-list result` (PASS)
- `escape-scope: nested map+reverse` (then ASAN failure)

Relevant test location:

- `src/lisp/tests_compiler_tests.c3` (`run_escape_scope_tests`, nested map+reverse case)

## Stable Failing Call Chain

The same stack chain repeats across runs:

1. `lisp.jit_apply_multi_args` (`src/lisp/jit_jit_helper_functions.c3`, around 2490/2525)
2. `lisp.jit_eval_in_single_scope` (`src/lisp/jit_jit_helper_functions.c3`, around 507)
3. `main.scope_create` / `scope_chunk_alloc` (`src/scope_region.c3`, around 256/209)
4. `std.core.mem.malloc` (where corruption becomes visible)

Interpretation: allocator is likely the first visible crash site, not necessarily the origin.

## What Was Tried

### 1) ASAN-only scope freelist bypass

Change:

- `src/scope_region.c3`: disable `ScopeRegion` freelist recycle under ASAN.

Result:

- No change to deterministic failure site.

Conclusion:

- Not a freelist stale-pointer reuse artifact.

### 2) Runtime ASAN detection + sanitizer fiber hook integration

Changes:

- `csrc/stack_helpers.c`
  - runtime ASAN detection via weak `__asan_init`
  - weak-symbol calls to `__sanitizer_start_switch_fiber` / `__sanitizer_finish_switch_fiber`
- `src/stack_engine.c3`
  - `stack_runtime_asan_enabled()` and use in stack sizing + overflow-test skip gate.

Result:

- Failure still reproduces at same chain.
- ASAN additionally reports internal stack frame consistency checks at the same location in some runs.

Conclusion:

- Improves instrumentation correctness, but does not resolve root corruption.

### 3) Diagnostic forced reroute of 2+ arg call compilation

Attempt:

- Temporarily routed 2+ arg call compilation through a helper runtime path.

Result:

- Introduced separate UAF/lifetime hazards due nested `jit_eval` + scope churn.
- Diagnostic branch was reverted.

Conclusion:

- Not safe as a fix path; keep mainline semantics intact.

## Current Working Hypothesis

The fault is in JIT/runtime interaction around multi-arg apply and scope transitions, likely before `scope_create` executes.

Strong candidates:

- corrupted function/arg state entering `jit_apply_multi_args`
- invalid scope state/metadata during recursive apply chain
- stack/context mismatch during deep JIT call chains interacting with scope lifetime paths

## Next Steps (Concrete)

1. Add ASAN/debug-gated invariants at entry/exit of:
   - `jit_apply_multi_args`
   - `jit_eval_in_single_scope`
   - `scope_create`
   Capture:
   - current scope pointer
   - scope generation / escape generation
   - owner-thread token match
   - refcount sanity
   - call depth and arg_count

2. Add fast-fail guard before allocation in `scope_create`:
   - verify `parent` shape and thread ownership
   - verify generation counters monotonic and non-zero

3. Build minimal reproducer expression (single eval) from failing test case:
   - `(car (reverse (map (lambda (x) (* x 10)) (quote (1 2 3)))))`
   Run under ASAN in isolation to shorten iteration cycle.

4. Compare interpreter-only vs JIT-enabled execution for that exact expression.
   - If interpreter-only passes under ASAN consistently, focus on JIT call/apply boundary.

5. Only after invariants locate first bad state:
   - patch root cause in apply/scope transition logic
   - keep ASAN-specific triage gates minimal and removable

## Notes

- This issue is currently triage-only documented; no API/user syntax changes required.
- Preserve region-centric ownership model; avoid introducing per-type lifetime drift as a workaround.
