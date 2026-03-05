# Boundary Runtime Audit (2026-03-05)

Status: post-hardening audit snapshot  
Scope: boundary ownership/lifetime transitions, stack defer substrate, scheduler wakeup/thread-task interleavings

## 1) What Is Enforced Now

- Facade policy guard:
  - `scripts/check_boundary_facade_usage.sh`
  - policy source: `scripts/boundary_facade_policy.txt`
- Boundary-sensitive change policy:
  - `scripts/check_boundary_change_policy.sh`
  - sensitive list: `scripts/boundary_sensitive_files.txt`
  - requires normal + ASAN summary evidence when sensitive files are touched
- CI guard workflow:
  - `.github/workflows/boundary-policy-guard.yml`
- Runtime/test coverage:
  - scheduler wakeup overflow/drop accounting
  - full-queue payload ownership semantics
  - inactive-slot payload cleanup semantics
  - worker retry while ring is full
  - worker-cancel and timeout-then-join thread-task interleavings
  - ready-barrier ordering for payload events
  - all with boundary snapshot assertions

## 2) Stack/Clone Boundary Posture

- `stack_ctx_defer`/`stack_ctx_undefer`/clone hooks are implemented and used.
- Defer clone/update semantics are covered by stack-engine tests.
- Active runtime usage in JIT call-scope paths is present and validated in regression runs.

## 3) Residual Risks (Still Open)

- Test topology:
  - scheduler boundary tests remain large overall; one additional split landed, more decomposition is still warranted.
- CI blast radius:
  - heavy profile (`scripts/run_boundary_hardening.sh`) still depends on self-hosted toolchain/runtime image quality.
- Fiber-temp track:
  - still correctly treated as R&D lane; not a replacement ownership authority.

## 4) No-Drift Contract (Current)

- Stack layer stays generic and callback-driven.
- Ownership authority remains region-centric (`ScopeRegion` + boundary policy).
- No per-type RC ownership model for language graph values.
- Finalizer-bearing resources stay on explicit deterministic paths.

## 5) Closure Criteria for This Hardening Track

The track is considered closed when all are true:

1. Facade policy guard has zero violations in strict mode.
2. Boundary surface audit reports zero violations.
3. Normal + ASAN + ASAN+fiber-temp full suites stay green.
4. Repeated ASAN+fiber-temp soak runs are green (no leak regressions).
5. No new direct legacy boundary callsites appear outside policy-allowed modules.
