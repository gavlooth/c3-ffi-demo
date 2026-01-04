# Component-Level Scope Tethering Implementation Plan

## Overview
Evolve the current Symmetric RC (SRC) system into a "Component-Level" tethering system. This treats whole cyclic islands (SCCs) as units, allowing zero-cost access inside tethered scopes.

## Track A: Runtime Implementation (`runtime/src/memory/component.c`)

### Phase 1: Core Definitions (P0)
- [x] Define `SymComponent` struct (handle_count, tether_count, member_list).
- [x] Evolve `SymObj` to link to a `SymComponent`.
- [x] Implement `sym_component_new()` and `sym_component_add_member()`.

### Phase 2: Boundary & Tethering (P0)
- [x] Implement `sym_acquire_handle(Component*)` and `sym_release_handle(Component*)`.
- [x] Implement `sym_tether_begin(Component*)` and `sym_tether_end(Token)`.
- [x] Integrate with `maybe_schedule_dismantle()`.

### Phase 3: Dismantling Logic (P1)
- [x] Implement `sym_dismantle_component()`.
- [x] Implement symmetric edge cancellation (removing internal links).
- [ ] Verification: Test with complex nested cycles.

### Phase 4: Thread-Local Management (P1)
- [x] Add `__thread SymComponentPool` for fast allocation of component headers.
- [x] Ensure `sym_cleanup()` clears active components.

---

## Track B: Compiler Integration (`csrc/analysis/` & `csrc/codegen/`)

### Phase 5: Component Inference (P1)
- [x] Update `omni_analyze_static_symmetric` to group SCCs into `ComponentInfo`.
- [x] Identify boundary handles vs internal cyclic edges.

### Phase 6: Tether Injection (P2)
- [x] Inject `sym_tether_begin/end` around scopes where ASAP cannot prove SCC safety.
- [x] Inject `sym_release_handle` at the dominator's last use.

---

## Track C: Verification

### Phase 7: Verification (P2)
- [x] Micro-benchmark: Tethered Access vs. RC Access.
- [x] Stress-test: 1M components with cross-linking.
- [x] Functional Verification: Passed 454 runtime tests.

---

## Status Tracking
- [x] Phase 1: Core Definitions
- [x] Phase 2: Boundary & Tethering
- [x] Phase 3: Dismantling Logic
- [x] Phase 4: TLS Management
- [x] Phase 5: Component Inference
- [x] Phase 6: Tether Injection
- [x] Phase 7: Verification
