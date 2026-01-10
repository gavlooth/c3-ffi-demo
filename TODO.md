# OmniLisp TODO (Restarted)

This TODO list was **restarted on 2026-01-10** after archiving the previous historical backlog.

Backup of the previous `TODO.md` (full history):
- `TODO_ARCHIVE/TODO.md.backup.2026-01-10_120111`

## Review Directive

**All newly implemented features must be marked with `[DONE] (Review Needed)` until explicitly approved by the user.**

- When an agent completes implementing a feature, mark it `[DONE] (Review Needed)` (not `[DONE]`)
- `[DONE] (Review Needed)` means: code is written and working, but awaits user review/approval
- After user approval, change `[DONE] (Review Needed)` → `[DONE]`
- Workflow: `[TODO]` → implement → `[DONE] (Review Needed)` → user approves → `[DONE]`

---

## Transmigration Directive (Non-Negotiable)

**Correctness invariant (must always hold):** For every in-region heap object `src` reached during transmigration, `remap(src)` yields exactly one stable destination `dst`, and all pointer discovery/rewrites happen only via metadata-driven `clone/trace` (no ad-hoc shape walkers); external/non-region pointers are treated as roots and never rewritten.

**Do not bypass the metadata-driven transmigration machinery for “fast paths”.**

**Pinned terminology (required reading):**
- `runtime/docs/MEMORY_TERMINOLOGY.md` (Region = lifetime class; ArenaRegion/RCB = runtime container; enforcement requires region identity + store barrier)

Rationale:
- Fast paths that “special-case” one shape (e.g., linear lists) tend to reintroduce unsoundness by silently skipping necessary escape repair (Region Closure Property).
- CTRR’s guarantee requires a *single* authoritative escape repair mechanism (metadata-driven `clone/trace`), with optimizations implemented **inside** that machinery (remap/forwarding strategy, worklist layout/chunking, batch allocation, dispatch reductions, etc.), not around it.

Minimal “stay on the path” examples:

Allowed (optimize inside the existing machinery):
```c
// GOOD: still uses the same remap + metadata callbacks.
Obj *dst = omni_remap_get_or_clone(ctx, src, meta);   // may use dense tables / forwarding
meta->trace(ctx, dst, src);                           // discovers edges via metadata
// ...ctx pushes work items; loop processes them...
```

Forbidden (bypass/alternate implementations):
```c
// BAD: special-cases a shape and bypasses metadata-driven trace/clone.
if (omni_is_linear_list(src)) {
  return omni_fast_copy_list_without_metadata(src, dst_region);
}
```

Allowed:
- Optimization of the existing transmigration loop and remap/worklist internals.
- Type-specific micro-optimizations that are implemented via metadata callbacks and remain fully covered by the same correctness tests.
- Instrumentation/metrics that prove a change is a *true* win (e.g., worklist push/pop counts, visitor-call counts, forwarding hit rate), without changing the correctness contract.

Forbidden:
- Any separate “alternate transmigrate implementation” that bypasses metadata clone/trace for a subset of graphs unless it is proven equivalent and treated as part of the same contract (and reviewed as a high-risk change).

---

## Issue Authoring Directive (Agent-Proof)

This file uses **Issue N** enumeration (starting at 1). Do not renumber existing issues.

Rules (mandatory):
- **Append-only numbering:** When creating a new issue, add it as `## Issue N: ...` using the next available integer `N`. Never renumber existing issues.
- **No duplicates:** There must be exactly one header for each issue number. If an issue needs revision, append an “Amendment” subsection inside that issue instead of creating a second copy elsewhere.
- **Dependency order:** Issues must be ordered top-to-bottom by dependency.
- **Status required:** Every task line must be one of `[TODO]`, `[IN_PROGRESS]`, `[DONE]`, or `[N/A]` with a one-line reason for `[N/A]`. Never delete old tasks; mark them `[N/A]` instead.
- **Benchmark consistency clause (perf tasks):** Any performance-related issue MUST define a reproducible benchmark protocol (compiler + flags, rebuild steps, warmup/repeats, and what to report). If the protocol is not specified, the issue is incomplete and must be marked `[N/A]` until fixed.

Required “agent-proof” structure for new issues/tasks:
- **Objective:** 1–2 sentences describing the concrete outcome.
- **Reference (read first):** exact doc paths that explain the theory/contract.
- **Constraints:** restate “no stop-the-world GC”, “no language-visible share”, and issue-specific invariants.
- **Subtasks:** each subtask must include:
  - **Label:** short, unique, grep-able (e.g. `I1-rc-crosscheck`).
  - **Where:** exact file paths to modify.
  - **Why:** architectural reason; what breaks or is slow today.
  - **What to change:** numbered steps.
  - **Implementation details:** include pseudocode and key structs/functions/macros.
  - **Verification plan:** concrete tests + exact commands.

---

## Jujutsu Commit Directive (MANDATORY)

**Use Jujutsu (jj) for ALL version control operations.**

### Pre-Task Checklist (MANDATORY)

**Before beginning ANY implementation subtask, you MUST:**

1. **Run `jj describe -m "sample message here"`** to save the current working state
1. **Run `jj log`** to see the current working state
2. **Read the description** to understand what changes are in progress
3. **Confirm alignment** with the task you're about to implement
4. **If mismatch**: Either `jj squash` to consolidate or `jj new` to start fresh

```bash
# ALWAYS run this first
jj describe
```

### Commit Workflow

- **Use jj (not git)**: All commits must be made using `jj` commands
- **Squash workflow**: Use `jj squash` to combine related changes before committing
- **For every completed task:**
  - Create a dedicated jujutsu squash with a clear, imperative message.

---

## Issue 1: Adopt “RC external pointers” semantics as Region‑RC spec cross-check (Internet-Informed 11.1) [TODO]

**Objective:** Cross-check OmniLisp’s per-region reference counting model against the RC dialect’s definition of “external pointers”, then encode the applicable semantics as explicit OmniLisp docs and verification checklists (no language-visible API changes).

**Reference (read first):**
- `review_todo.md` (Issue 11.1 and Issue 1/2 model tasks)
- `docs/CTRR.md` (Region Closure Property; “everything can escape”)
- `runtime/docs/ARCHITECTURE.md` (model naming and boundaries)
- RC dialect overview: https://www.barnowl.org/research/rc/index.html

**Constraints (non-negotiable):**
- No stop-the-world GC; no heap-wide scanning collectors.
- No language-visible “share primitive” (`(share v)` is forbidden).
- No programmer-visible RC APIs (we use CTRR insertion + runtime barriers).
- The runtime contract must be “always safe”; debug aborts may exist as diagnostics only, not as the semantic contract.

### P0: Write the Region‑RC model spec (external pointers, liveness, invariants) [DONE] (Review Needed)

- [DONE] Label: I1-region-rc-model-doc (P0)
  Objective: Create a single authoritative doc defining Region‑RC and "external pointer" semantics in OmniLisp terms.
  Where:
    - Add: `runtime/docs/REGION_RC_MODEL.md` ✅
    - Update: `runtime/docs/ARCHITECTURE.md` (link + terminology alignment) ✅
  Why:
    "Per-region RC" is ambiguous unless we explicitly define what increments/decrements region external refs and what it means for safe reclamation.
  What to write:
    1. Region liveness rule:
       - Alive iff `scope_alive == true || external_rc > 0`.
       - Reclaimable iff `scope_alive == false && external_rc == 0`.
    2. Definition of "external pointer / external reference":
       - in OmniLisp: pointers into region `R` stored *outside `R`* in a way that can outlive `R` (older region, global, other thread, etc.).
    3. Relationship to transmigration:
       - transmigrate repairs escape edges so Region Closure holds (no pointers into dead regions).
    4. Relationship to mutation auto-repair (Issue 2 tasks are in `review_todo.md`):
       - illegal younger→older stores must be repaired (copy or merge) so the model stays sound without GC.
  Verification plan:
    - Doc includes a "Conformance Checklist" referencing:
      - "external-root non-rewrite rule" (transmigration)
      - "mutation barrier inventory complete" (review_todo Issue 2)

- [DONE] Label: I1-external-ref-boundary-inventory (P0)
  Objective: Enumerate all runtime/compile-time "escape boundaries" that can create external references to a region.
  Where:
    - Add section: `runtime/docs/REGION_RC_MODEL.md` ("External reference boundaries") ✅
  Why:
    If even one boundary is missed, region external_rc becomes meaningless and reclaim can be unsafe.
  What to list (minimum categories):
    - return to caller/outliving scope ✅
    - closure capture ✅
    - global/module store ✅
    - channel send/recv (cross-thread) ✅
    - mutation store into older region containers (must auto-repair; see Issue 2 in review_todo.md) ✅
  Verification plan:
    - Provide at least one example per boundary in the doc (source + expected runtime operation). ✅

### Amendment A (2026-01-10): Doc→Code conformance + “make Region‑RC real”

**Problem statement (constructive criticism):** Right now, Region‑RC is mostly a *documented intent*, not a complete runtime/CTRR guarantee, because neither the runtime nor the compiler has a reliable, cheap way to answer “which region owns this `Obj*`?”. Without that, we cannot soundly update `external_rc` at real escape boundaries, nor implement the mutation-time auto-repair barrier (Issue 2).

**Reality check (what exists in code today):**
- ✅ Region control block and liveness fields exist: `runtime/src/memory/region_core.h` (`external_rc`, `tether_count`, `scope_alive`, `owner_thread`, `region_id`).
- ✅ Region liveness ops exist: `runtime/src/memory/region_core.c` (`region_exit`, `region_destroy_if_dead`, `region_retain_internal`, `region_release_internal`, `region_tether_start/end`).
- ✅ Compiler emits region lifecycle + some tethering: `csrc/codegen/region_codegen.c`, `csrc/codegen/codegen.c`.
- ❌ No stable “owning region” metadata on `Obj` (or fully-integrated pointer-encoding) to implement `region_of(obj)` cheaply.
- ❌ No compile-time insertion of `region_retain/region_release` at return/capture/global/channel boundaries (search in `csrc/` finds none).
- ❌ Runtime primitives store pointers freely (channels/atoms/arrays/dicts) without a store barrier (Issue 2).

#### P1: Implement `region_of(obj)` (foundation for RC + store barrier) [DONE] (Review Needed)

- [DONE] (Review Needed) Label: I1-region-of-obj-mechanism (P1)
  Objective: Add a **single, audited** mechanism to map any boxed `Obj*` to its owning `Region*` in O(1), so that `external_rc` updates and mutation auto-repair are implementable without heap scanning.
  Reference (read first):
    - `runtime/docs/REGION_RC_MODEL.md` (Sections 2–3 “External references”)
    - `runtime/docs/CTRR_TRANSMIGRATION.md` (external-root rule + remap identity)
    - `runtime/docs/ARCHITECTURE.md` (“Region‑RC = when regions escape”)
  Constraints:
    - Tooling matters: the default approach must work with ASAN/TSAN builds.
    - No heap-wide scanning to find an owning region.
  Where (expected touchpoints):
    - `runtime/include/omni.h` (define/gate the chosen mechanism; add `omni_obj_region()` API)
    - `runtime/src/memory/region_value.c` (set owner metadata on allocation)
    - `runtime/src/memory/transmigrate.c` (use `region_of()` when correctness requires, not only “range checks”)
    - Optional: `runtime/src/memory/region_pointer.h` (only if pointer-masking is selected)
  Why:
    Without `region_of(obj)`, the runtime cannot:
      - retain/release the correct region at external boundaries,
      - implement store-barrier auto-repair,
      - make channel/atom safe without transmigrating everything into global region.
  What to change (choose ONE approach and document the decision in `runtime/docs/REGION_RC_MODEL.md`):
    1. **Option A (tooling-first, simplest): per-object owner pointer**
       - Add `struct Region* owner_region;` to `struct Obj` (boxed only).
       - Set it in `alloc_obj_typed()` and all region constructors.
       - Provide `static inline Region* omni_obj_region(Obj* o)` that returns NULL for immediates.
    2. **Option B (perf-first, risky): pointer masking using `Region.region_id`**
       - Encode region_id into pointer high bits (see `runtime/src/memory/region_pointer.h`).
       - Requires strict masking/unmasking rules at every API boundary; known to fight ASAN/UBSAN and non-canonical pointers.
  Implementation details (pseudocode):
    ```c
    static inline Region* omni_obj_region(Obj* o) {
      if (!o || IS_IMMEDIATE(o)) return NULL;
      return o->owner_region;  // Option A
    }
    ```
  Verification plan (tests must fail before fix, pass after):
    - Add `runtime/tests/test_region_of_obj.c`:
      1. allocate in `R1`, assert `omni_obj_region(obj) == R1`
      2. allocate in `R2`, assert `omni_obj_region(obj) == R2`
      3. immediates return NULL
    - Commands:
      - `make -C runtime/tests test`
      - `make -C runtime/tests asan`

#### P2: Make Region‑RC escape boundaries compile-time actionable (retain/release insertion plan) [N/A]

- [N/A] Label: I1-ctrr-external-rc-insertion (P2)
  Reason: Partial implementation completed - enum and function stubs added, but full retain/release insertion requires major compiler refactoring (last-use analysis, exit path tracking, size heuristics). Enum and function declarations provide foundation for future completion.
  Objective: Extend CTRR so that when it chooses “**retain region**” (instead of transmigrate), it also emits **matching** `region_retain_internal()` and `region_release_internal()` at compile time based on last-use, so regions can outlive scope safely without leaks.
  Reference (read first):
    - `runtime/docs/REGION_RC_MODEL.md` (Section 3.3 external boundaries)
    - `docs/CTRR.md` (“everything can escape”; last-use insertion is the whole point)
    - `csrc/analysis/analysis.h` (`RegionInfo`, escape/capture tracking; liveness infra)
  Where:
    - `csrc/analysis/analysis.c` (decide retain vs transmigrate per escape site)
    - `csrc/codegen/region_codegen.c` (emit retain/release sites)
    - `csrc/codegen/codegen.c` (ensure all exit paths release)
  Why:
    The spec says “caller releases when done”, but OmniLisp has no explicit “drop”.
    The only sound path is: **compiler inserts releases at last use**, like ASAP/CTRR inserts `free()`.
  What to change:
    1. Define an analysis-level escape-repair tag per escaping binding:
       - `ESCAPE_REPAIR_TRANSMIGRATE` vs `ESCAPE_REPAIR_RETAIN_REGION`.
    2. When `RETAIN_REGION` is chosen:
       - Emit `region_retain_internal(omni_obj_region(x))` at escape boundary.
       - Emit `region_release_internal(omni_obj_region(x))` at the *last use* of `x` in the outliving scope.
  Implementation details (pseudocode):
    ```c
    if (plan == RETAIN_REGION) {
      region_retain_internal(omni_obj_region(x));
      return x;
    } else {
      return transmigrate(x, _local_region, _caller_region);
    }
    // ...later, at last use of x...
    region_release_internal(omni_obj_region(x));
    ```
  Verification plan:
    - Add `csrc/tests/test_codegen_region_retain_release.c`: compile a small program; assert generated C contains retain/release at the expected points.
    - Add `runtime/tests/test_region_rc_liveness.c`:
      - Build value in region `R`, `region_exit(R)`, retain it, verify region isn’t reclaimed until release happens.
    - Commands:
      - `make -C csrc/tests test`
      - `make -C runtime/tests test`

---

## Issue 2: Pool/arena practice + region accounting + auto-repair threshold tuning (Internet-Informed 11.3) [TODO]

**Objective:** Adopt “shortest-lived pool” lessons from pool-based allocators by adding region accounting and diagnostics that (a) detect retention cliffs and (b) provide deterministic inputs for the size heuristic used by auto-repair (transmigrate vs merge).

**Reference (read first):**
- `review_todo.md` Issue 6 (accounting), Issue 2 (auto-repair policy), Issue 11.3 (pool practice)
- APR pools usage guidance: https://perl.apache.org/docs/2.0/user/performance/prevent.html
- ATS MemArena (freeze/thaw style): https://docs.trafficserver.apache.org/en/10.1.x/developer-guide/internal-libraries/MemArena.en.html

**Constraints (non-negotiable):**
- No stop-the-world GC; no heap-wide scanning collectors.
- No language-visible share primitive.
- Diagnostics must be deterministic and cheap enough for debug builds.

### P0: Region accounting doc + required counters [DONE] (Review Needed)

- [DONE] Label: I2-region-accounting-doc (P0)
  Objective: Specify which counters are required per region (bytes, chunks, inline usage, peak) and how they power the size heuristic for store auto-repair.
  Where:
    - Add: `runtime/docs/REGION_ACCOUNTING.md` ✅
  Why:
    The "size heuristic" must be tunable and reproducible; region bytes/chunks are the best low-cost proxy.
  What to define:
    - `bytes_allocated_total`, `bytes_allocated_peak` ✅
    - `chunk_count`, `inline_buf_used_bytes` ✅
    - optional: `escape_repair_count` (how often this region forced repair) ✅
  Verification plan:
    - Doc includes example output format and thresholds. ✅

### P1: Retention diagnostics plan (shortest-lived pool enforcement without language changes) [DONE] (Review Needed)

- [DONE] Label: I2-retention-diagnostics-plan (P1)
  Objective: Define the diagnostics that identify "allocating into too-long-lived region" retention smells, and how they should be reported.
  Where:
    - Add section: `runtime/docs/REGION_ACCOUNTING.md` ("Retention cliffs") ✅
    - Optionally add doc: `runtime/docs/REGION_DIAGNOSTICS.md` (N/A - covered in REGION_ACCOUNTING.md)
  Why:
    Pool practice shows the #1 failure mode: memory "leaks" are often retention due to lifetime mismatch. We want the runtime to make this visible.
  What to report (examples):
    - "Region R is long-lived and received X allocations but only Y escapes; consider allocating in shorter-lived region" ✅
    - "Auto-repair triggered N times (M transmigrates, K merges); threshold tuning suggested" ✅
  Verification plan:
    - Define a toy workload and expected diagnostics output. ✅

### P2: Optional "freeze/thaw" coalesce-at-safe-point evaluation [N/A - Fragmentation not yet measured]

- [N/A] Label: I2-freeze-thaw-eval (P2)
  Objective: Evaluate whether a safe-point "coalesce/compact" step for long-lived regions (freeze/thaw style) is beneficial, without becoming a GC.
  Where:
    - Add: `runtime/docs/REGION_COALESCE_POLICY.md` (evaluation + decision)
  Why:
    It may reduce fragmentation for long-lived regions, but it increases complexity and must be justified by benchmarks.
  Constraints:
    - Only at explicit safe points (e.g., end of init), not background.
    - Only touches explicitly selected regions; no heap scanning beyond known live roots for that safe point.
  Verification plan:
    - Define a benchmark scenario and what would constitute a "win".
  **Reason for N/A:** Fragmentation impact not yet measured; defer until region accounting shows fragmentation is problematic.

### Amendment A (2026-01-10): Region accounting + mutation auto-repair are not implemented yet

**Reality check (constructive criticism):** `runtime/docs/REGION_ACCOUNTING.md` is normative, but `struct Region` currently has none of the required accounting fields, and the runtime has multiple pointer-storing primitives (arrays/dicts/atoms/channels) that bypass any “auto-repair” barrier. That means the *contract* is not currently enforced.

#### P3: Implement per-region accounting counters (minimal, deterministic, low overhead) [TODO]

- [TODO] Label: I2-impl-region-accounting-counters (P3)
  Objective: Implement the **required** per-region accounting counters described in `runtime/docs/REGION_ACCOUNTING.md`, including a way to count arena chunk growth without heap scanning.
  Reference (read first):
    - `runtime/docs/REGION_ACCOUNTING.md` (Sections 2–4)
    - `runtime/src/memory/region_core.h` (`region_alloc` fast path + arena slow path)
    - `third_party/arena/arena.h` (ArenaChunk list structure)
  Where:
    - `runtime/src/memory/region_core.h` (add fields to `struct Region`; update inline alloc)
    - `runtime/src/memory/region_core.c` (initialize/reset counters; include region-pool reset path)
  What to change (required `struct Region` fields; names may be adjusted but MUST exist):
    ```c
    size_t bytes_allocated_total;
    size_t bytes_allocated_peak;
    size_t inline_buf_used_bytes;
    size_t escape_repair_count;
    size_t chunk_count;
    ArenaChunk* last_arena_end;  // internal: detect chunk growth without touching third_party
    ```
  Implementation details (chunk_count without modifying `third_party/arena`):
    - In `region_alloc` slow-path:
      - `ArenaChunk* before = r->arena.end;`
      - `ptr = arena_alloc(&r->arena, size);`
      - If `r->arena.end != before` then `r->chunk_count++`
  Verification plan:
    - Add `runtime/tests/test_region_accounting.c`:
      1. Allocate known sizes; assert `bytes_allocated_total` matches sum of requested sizes (or aligned sizes if documented).
      2. Force arena growth; assert `chunk_count` increments.
      3. Force inline allocations; assert `inline_buf_used_bytes` tracks peak offset.
    - Commands:
      - `make -C runtime/tests test`

#### P4: Define + implement the mutation store barrier choke point (auto-repair at runtime) [TODO]

- [TODO] Label: I2-store-barrier-choke-point (P4)
  Objective: Add a single runtime helper that *all* pointer-storing primitives must use, enforcing Region Closure by **automatic** repair of illegal lifetime edges at mutation time (no “abort as semantics”).
  Reference (read first):
    - `docs/CTRR.md` (Region Closure Property; mutation is a new escape boundary)
    - `runtime/docs/REGION_RC_MODEL.md` (mutation-time repair contract)
    - `runtime/docs/REGION_ACCOUNTING.md` (size heuristic inputs)
  Constraints:
    - No stop-the-world GC.
    - No new language-visible sharing primitive.
    - Debug-only aborts permitted as diagnostics, not as the semantic contract.
  Where (minimum call sites; inventory must be completed as part of this task):
    - `runtime/src/runtime.c`: `array_set`, `dict_set`, atom writes (`atom_reset`, `atom_swap`, `atom_cas`), channel enqueue (`channel_send`), box setters
    - `runtime/src/typed_array.c`: `omni_typed_array_set` (when storing boxed values)
  What to change:
    1. Introduce a single helper (name is illustrative; enforce usage everywhere):
       ```c
       Obj* omni_store_repair(Obj* container, Obj** slot, Obj* new_value);
       ```
    2. Required logic (high-level):
       - If `new_value` is immediate/NULL → store directly.
       - `Region* src = omni_obj_region(new_value)`, `Region* dst = omni_obj_region(container)` (requires Issue 1 P1).
       - If `src == NULL || dst == NULL || src == dst` → store directly.
       - If the store would create a younger→older (or shorter-lived→longer-lived) edge:
         - Choose repair strategy via accounting:
           - small ⇒ `transmigrate(new_value, src, dst)` and store moved pointer
           - large ⇒ merge/coalesce/promote if permitted, else fallback to transmigrate
       - Update `escape_repair_count`.
  Verification plan (must include ASAN):
    - Add `runtime/tests/test_store_barrier_autorepair.c`:
      1. Create `dst` region and a container allocated in `dst`.
      2. Create `src` region and allocate a nested value graph in `src`.
      3. Store the value into the container via the public API.
      4. `region_exit(src); region_destroy_if_dead(src);`
      5. Read through the container; must still be valid (no UAF).
    - Commands:
      - `make -C runtime/tests test`
      - `make -C runtime/tests asan`

#### P5: Define “merge/coalesce/promote” behavior (tests first; avoid silent dangling pointers) [TODO]

- [TODO] Label: I2-region-merge-policy (P5)
  Objective: Specify and implement the “large ⇒ merge/coalesce/promote” half of auto-repair in a way compatible with the allocator constraints (inline buffer + arena chunks) and threading/ownership rules.
  Reference (read first):
    - `runtime/docs/REGION_RC_MODEL.md` (auto-repair contract)
    - `runtime/src/memory/region_core.h` (`region_can_splice_arena_only` and inline buffer constraints)
    - `runtime/src/memory/region_core.c` (`region_splice`)
  Why:
    `region_splice()` exists but only transfers arena chunks; it does NOT move inline-buffer allocations.
    A naïve “merge region” will silently create dangling pointers.
  What to change (step-by-step):
    1. Define an explicit “merge permitted” predicate:
       - merge permitted only when `region_can_splice_arena_only(src)` is true OR src has no inline allocations.
    2. Minimal safe merge path:
       - splice arena chunks from `src` into `dst` and mark `src` drained (cannot allocate further).
       - otherwise fallback to transmigrate for the specific value graph.
    3. Threading gates:
       - If `src.owner_thread != dst.owner_thread`, merge forbidden → transmigrate fallback.
  Verification plan:
    - Extend `runtime/tests/test_store_barrier_autorepair.c` with a forced-merge case:
      - Ensure src is arena-only (no inline allocations)
      - Set threshold low to force merge
      - Verify values remain valid after `region_exit(src)` and `region_destroy_if_dead(src)`

---

## Issue 3: Non-lexical regions + splitting ideas as CTRR roadmap (Internet-Informed 11.2) [TODO]

**Objective:** Incorporate non-lexical region analysis and region splitting ideas as a CTRR roadmap to reduce the frequency and cost of runtime repairs (transmigrate/merge), without changing language syntax.

**Reference (read first):**
- `docs/CTRR.md`
- Better Static Memory Management (Aiken/Fähndrich/Levien, 1995): https://digicoll.lib.berkeley.edu/record/139069
- Region-Based Memory Management (Tofte/Talpin, 1997): https://www.sciencedirect.com/science/article/pii/S0890540196926139
- Spegion (ECOOP 2025): https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2025.15

**Constraints (non-negotiable):**
- No STW GC; no runtime heap scanning.
- No new language surface constructs required for users.

### P0: CTRR inference roadmap doc [DONE] (Review Needed)

- [DONE] Label: I3-ctrr-roadmap-doc (P0)
  Objective: Write a roadmap doc that maps research ideas into concrete CTRR phases OmniLisp could implement.
  Where:
    - Add: `docs/CTRR_REGION_INFERENCE_ROADMAP.md` ✅
  Why:
    Compiler improvements reduce runtime repair pressure and improve robustness (fewer opportunities for dynamic lifetime changes).
  What to include:
    - Non-lexical region ends (liveness-driven end-of-region insertion) ✅
    - Allocate-into-outliving-region when escape is provable (avoid transmigrate) ✅
    - Internal "splittable regions" representation (to support later merges without copying) ✅
    - Interaction with mutation auto-repair policy (Issue 2 in review_todo.md) ✅
  Verification plan:
    - Provide 3 pseudo-programs with "expected region plan" (where regions start/end; where transmigrate would be inserted). ✅

### Amendment A (2026-01-10): Turn roadmap into implementable compiler milestones

#### P1: Inventory what codegen already emits (region lifecycle, tether, transmigrate) [TODO]

- [TODO] Label: I3-ctrr-emission-inventory (P1)
  Objective: Produce an explicit “what codegen currently emits” inventory so later phases don’t assume features that aren’t present (retain/release insertion, precise tethering, etc.).
  Reference (read first):
    - `docs/CTRR_REGION_INFERENCE_ROADMAP.md`
    - `csrc/codegen/region_codegen.c`
    - `csrc/codegen/codegen.c`
  Where:
    - Update: `docs/CTRR_REGION_INFERENCE_ROADMAP.md` (append “Current Implementation Status” section)
  What to include (minimum checklist):
    1. Where `_local_region` is created and destroyed.
    2. Where `transmigrate()` is emitted (return, args, closure captures, globals?) and what decides it.
    3. Where tethering is emitted and whether it is precise or conservative.
    4. Confirm that no `region_retain/region_release` insertion exists yet (Issue 1 P2 will add it).
  Verification plan:
    - Add `csrc/tests/test_codegen_region_emission_inventory.c`:
      - Compile a small OmniLisp snippet and assert specific emitted lines exist in generated C.

#### P2: Implement “non-lexical region end” for straight-line liveness (no branches yet) [TODO]

- [TODO] Label: I3-nonlexical-region-end-straightline (P2)
  Objective: Implement the simplest non-lexical region end insertion: in straight-line code, if all values allocated in `_local_region` are dead before function end, emit `region_exit/_destroy_if_dead` at the last-use point.
  Reference (read first):
    - `docs/CTRR_REGION_INFERENCE_ROADMAP.md` (non-lexical end section)
    - `csrc/analysis/analysis.h` (liveness tracking)
  Where:
    - `csrc/analysis/analysis.c` (compute last-use for region-owned locals)
    - `csrc/codegen/codegen.c` (emit early region exit at that last-use position)
  Implementation details (pseudocode):
    ```c
    // After analysis: last_use_pos for region R is computed.
    // Codegen: when emitting node for last_use_pos:
    emit("region_exit(_local_region);");
    emit("region_destroy_if_dead(_local_region);");
    ```
  Verification plan:
    - Add `csrc/tests/test_nonlexical_region_end_straightline.c` with:
      - Input: `(let ((x (pair 1 2))) 0)`
      - Expected: generated C includes `region_exit(_local_region)` before the final `return`.

---

## Issue 4: Concurrency SMR techniques for internal runtime DS (QSBR first; alternatives documented) (Internet-Informed 11.4) [TODO]

**Objective:** Evaluate safe memory reclamation (SMR) techniques (QSBR/Userspace RCU first) for *internal runtime data structures* (metadata registries, intern tables), improving concurrency and tooling robustness without turning the heap into a GC-managed space.

**Reference (read first):**
- `review_todo.md` Issue 11.4 tasks
- liburcu (Userspace RCU): https://liburcu.org/
- QSBR overview: https://lwn.net/Articles/573424/
- `runtime/docs/REGION_THREADING_MODEL.md` (to be written; threading contract)

**Constraints (non-negotiable):**
- Not a heap GC: SMR applies only to internal runtime DS nodes.
- No STW “scan and collect”.
- Must align with the threading/ownership contract (documented first).

### P0: SMR target inventory + decision matrix [DONE] (Review Needed)

- [DONE] Label: I4-smr-target-inventory (P0)
  Objective: Identify which internal runtime structures would benefit from SMR and create a decision matrix (QSBR vs lock vs hazard-pointer family).
  Where:
    - Add: `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` ✅
  Why:
    Adopting SMR without a clear target is complexity without payoff. We must start with concrete structures.
  What to include:
    - List candidate structures (e.g., metadata registry, intern table, global module map). ✅
    - For each, note read/write ratio, expected contention, and preferred approach. ✅
  Verification plan:
    - Include a microbenchmark plan for one structure (not the heap) following the benchmark protocol clause. ✅

### P1: QSBR mapping to OmniLisp "quiescent points" [DONE] (Review Needed)

- [DONE] Label: I4-qsbr-quiescent-points (P1)
  Objective: Define where quiescent states would be reported in OmniLisp (end of bytecode step, end of tether window, safe points), and how that interacts with region ownership.
  Where:
    - `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` (QSBR design) ✅
    - `runtime/docs/REGION_THREADING_MODEL.md` (tie-in) ✅
  Verification plan:
    - Provide at least one concurrency scenario (reader/writer) and explain when reclamation is permitted. ✅

### P2: Alternatives review (hazard pointers / Hyaline / publish-on-ping) [DONE] (Review Needed)

- [DONE] Label: I4-alternatives-review (P2)
  Objective: Document alternatives to QSBR and when they should be chosen (only if QSBR is unsuitable due to stalled threads or missing quiescent points).
  Reference:
    - Publish on Ping (2025): https://arxiv.org/abs/2501.04250
    - Hyaline (2019): https://arxiv.org/abs/1905.07903
  Where:
    - `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` (appendix) ✅
  Verification plan:
    - Decision matrix includes "how to test" and "what to measure" for each alternative. ✅
    - Decision matrix includes “how to test” and “what to measure” for each alternative.

### Amendment A (2026-01-10): Implementation gating + pick a first SMR target

**Constructive criticism:** SMR is only worth implementing if we pick a concrete structure with a measured contention profile. Otherwise we’ll add complexity and still pay locks. Also: do not let SMR work distract from the immediate transmigration + store-barrier correctness work (Issues 1–2).

#### P3: Add a minimal atomic policy layer (C99 + compiler builtins) [TODO]

- [TODO] Label: I4-atomic-policy-wrapper (P3)
  Objective: Route all atomic ops used by Region‑RC/tethering (and future SMR) through one header so we can audit memory orders, add TSAN annotations, and keep C99+extensions consistent.
  Reference (read first):
    - `runtime/docs/REGION_THREADING_MODEL.md` (atomic expectations)
  Where:
    - Add: `runtime/include/omni_atomic.h` (preferred; used by both runtime + tests)
    - Update: `runtime/src/memory/region_core.c` (replace direct `__atomic_*` calls)
  Why:
    Today we directly call `__atomic_*` builtins in multiple places. That makes it hard to:
      - reason about memory ordering,
      - add consistent debug assertions,
      - and evolve toward “C99 + extensions” vs “C11 atomics” intentionally.
  Verification plan:
    - Build must be warning-clean under `-std=c99 -Wall -Wextra -pthread`.
    - `make -C runtime/tests tsan` must still run (do not hide races by weakening the build).

#### P4: Select ONE DS and write the first QSBR implementation plan (tests + microbench protocol) [TODO]

- [TODO] Label: I4-qsbr-first-target-plan (P4)
  Objective: Select exactly one internal DS (metadata registry OR intern table) and write an implementable plan to retrofit it with QSBR, including a microbenchmark and correctness tests.
  Reference (read first):
    - `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` (target inventory + QSBR sketch)
  Where:
    - Update: `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` (add “Phase 1 target” section with exact file paths + APIs)
    - Add: `runtime/tests/bench_smr_<target>.c` (microbench harness; or extend `runtime/tests/Makefile`)
  Benchmark protocol (required by benchmark-consistency clause):
    - `make -C runtime/tests clean`
    - `make -C runtime/tests bench CC=gcc CFLAGS='-O3 -std=c99 -pthread'`
    - Run 10 warmup iterations + 30 measured; report median + p95; record machine info (CPU model, governor if known).
  Verification plan:
    - Correctness test: concurrent readers + writer that replaces structure repeatedly; must not UAF under ASAN.
