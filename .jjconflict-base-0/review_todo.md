# OmniLisp Memory System — Review Task List (CTRR + Region‑RC)
Date: 2026-01-10

This file is a **review backlog** for the OmniLisp memory system. It is intentionally exhaustive so you can pick/choose later.

Scope:
- **CTRR (Compile‑Time Region Reclamation)**: compile-time scheduling of region lifetimes and escape repair points.
- **Region‑RC (Per‑Region Reference Counting)**: runtime liveness is determined by **region-level** reference counts + scope liveness (no heap-wide collector).
- Building components: regions, per-region RC, tethering/borrowing, transmigration, concurrency + tooling.

Status legend (for this file):
- `TODO`: Candidate work item.
- `DEFER`: Only do if/when needed (explicitly justify).
- `N/A`: Not applicable (include a one-line reason).

Non‑negotiable constraints (global):
- No stop‑the‑world GC; no heap-wide scanning collectors.
- No language-visible “sharing primitive” (no `(share v)` or similar).
- Optimizations must remain **inside** the single authoritative CTRR/Region‑RC machinery (no bypass special-case walkers).
- Benchmark claims must use a reproducible protocol (see Issue 8).

Primary references (read before implementing *any* memory-model task):
- `docs/CTRR.md` (normative CTRR contract: “everything can escape”, Region Closure Property)
- `runtime/docs/ARCHITECTURE.md` (runtime architecture + model naming)
- `runtime/docs/CTRR_TRANSMIGRATION.md` (clone/trace contract, external-root rule, correctness invariants)
- `runtime/docs/CTRR_REMAP_FORWARDING_TABLE.md` (forwarding/remap identity)
- `docs/ADVANCED_REGION_ALGORITHMS.md` (long-term region/transmigration plans)

------------------------------------------------------------------------------

## Issue 1 — Formalize “Per‑Region RC” Semantics (Make the Model Explicit)

Goal: eliminate ambiguity about what Region‑RC means and what correctness guarantees it provides.

- [TODO] Label: R1-model-spec-core
  Objective: Write an explicit spec for the **per-region reference counting** model, including invariants, allowed pointer directions, and what CTRR must insert at compile time.
  Reference: `docs/CTRR.md`, `runtime/docs/ARCHITECTURE.md`
  Where:
    - New doc: `runtime/docs/REGION_RC_MODEL.md`
    - Update/align: `runtime/docs/ARCHITECTURE.md` (link to new doc)
  Why:
    “Per-region RC” only works robustly if we define:
    - what counts as an “external reference” to a region,
    - when a region may be reclaimed,
    - and how we prevent/handle region dependency cycles.
  What to write (minimum content checklist):
    1. **Region liveness rule:**
       - Region is alive iff `scope_alive == true || external_rc > 0`.
       - Region may free memory iff `scope_alive == false && external_rc == 0`.
    2. **Region Closure Property (runtime form):**
       - No pointers into dead regions.
    3. **External-root rule:**
       - Pointers outside the src region are treated as roots during transmigration and are not rewritten.
    4. **Region dependency DAG rule (see Issue 2):**
       - Define the “older-only store” rule or an equivalent rule that prevents region cycles.
    5. **Concurrency model hooks:**
       - define what operations may cross threads and what instrumentation/tooling is expected.
  Verification plan:
    - Add a short “spec conformance checklist” section to `runtime/docs/REGION_RC_MODEL.md`.
    - Ensure all existing docs link consistently (no contradictory naming).

- [TODO] Label: R1-define-external-ref-boundaries
  Objective: Define precisely which operations increment/decrement `external_rc` (or equivalent region-level refs).
  Reference: `runtime/docs/REGION_RC_MODEL.md` (to be written by R1-model-spec-core)
  Where:
    - New doc section: `runtime/docs/REGION_RC_MODEL.md` (“External references”)
    - Code touchpoints to list (do not implement here): `runtime/src/` modules for channel send/recv, closure capture, return, globals, etc.
  Why:
    Region‑RC is unsound if raw pointers can escape without RC updates; the runtime cannot observe arbitrary pointer copying.
  Implementation details (spec-level pseudocode):
    - On escape boundary (compile-time inserted):
      - `region_inc_external(root_region_of(value))` if value points into a region that outlives current scope.
      - `region_dec_external(...)` when the escape handle is dropped or transferred.
  Verification plan:
    - Spec must enumerate each escape boundary category:
      - return to parent/caller
      - closure capture
      - global store / module store
      - channel send/recv (cross-thread handoff)

------------------------------------------------------------------------------

## Issue 2 — Prevent Region Cycles (Region Dependency Graph Rule + Enforcement)

This is the largest “robustness unlock”. Per-region RC cannot reclaim cycles of regions, so we must prevent region dependency cycles by construction.

- [TODO] Label: R2-older-only-store-rule-spec
  Objective: Specify the **region dependency rule** that prevents region cycles (recommended: “older-only store”).
  Reference: `docs/CTRR.md` (Region Closure Property), `runtime/docs/REGION_RC_MODEL.md`
  Where:
    - New doc: `runtime/docs/REGION_DEPENDENCY_RULES.md`
  Why:
    Without an explicit rule, mutable Lisp updates can create:
    - older region objects pointing to younger region objects,
    - forming region cycles,
    - which per-region RC cannot collect.
  Spec content (minimum):
    - Define a region partial order (e.g., creation time / nesting depth).
    - Rule: a store into an object in region `R_old` may only reference values in `R_old` or an “older/equal” region (never a younger region).
    - If code attempts to store a younger pointer into an older object:
      - The runtime must **auto-repair** the store (no language changes, no user-visible `(share ...)`), using:
        - transmigration for “small” values/regions,
        - region coalescing/merge (“promote the region”) for “large” values/regions,
        - and a well-defined, reproducible size heuristic (see R2-auto-repair-policy below).
  Verification plan:
    - Doc includes a table of allowed/disallowed cases with examples.
    - Include at least 2 examples showing how this prevents region cycles.

- [TODO] Label: R2-runtime-write-barrier-plan
  Objective: Design a runtime **store barrier** strategy for cases the compiler cannot prove (dynamic stores), without changing language syntax.
  Reference: `runtime/docs/REGION_DEPENDENCY_RULES.md`
  Where (planned implementation points):
    - `runtime/src/runtime.c` (or wherever `set-car!`, `set-cdr!`, `dict-set!`, array set, etc. are implemented)
    - `runtime/src/memory/` (helpers to map pointer→region id/domain)
  Why:
    The language is mutable; if we don’t enforce the region rule at mutation points, cycles/leaks appear.
  Implementation plan (pseudocode; auto-repair, never abort):
    - `store_slot(obj_in_Rold, slot, value)`:
      1. Determine region of `obj` (R_obj).
      2. Determine region of `value` if pointer into a region (R_val).
      3. If `R_val` is younger than `R_obj`:
         - Choose repair action via the size heuristic (see R2-auto-repair-policy):
           - If “small”: `value2 = transmigrate(value, src=R_val, dst=R_obj_or_parent)` then store `value2`.
           - If “large”: `region_merge(R_val, into=R_obj_or_parent)` then store original `value` (now in unified lifetime).
  Verification plan:
    - Define tests that attempt illegal stores and confirm behavior is always safe and automatic (no abort):
      - illegal younger→older store triggers *either* transmigrate or merge, depending on configured heuristic thresholds.
    - Run under ASAN/TSAN once tooling exists (Issue 7/9).

- [TODO] Label: R2-auto-repair-policy
  Objective: Specify the **auto-repair** policy for illegal younger→older stores, including the “size heuristic” you want: small ⇒ transmigrate, large ⇒ coalesce/merge.
  Reference: `runtime/docs/REGION_DEPENDENCY_RULES.md`, `runtime/docs/REGION_RC_MODEL.md`
  Where:
    - Add section: `runtime/docs/REGION_DEPENDENCY_RULES.md` (“Auto-Repair Policy: Transmigrate vs Merge”)
    - Add section: `runtime/docs/REGION_RC_MODEL.md` (“Why region cycles must be prevented; why auto-repair is mandatory in a mutable Lisp”)
  Why:
    In a mutable Lisp, the program can create new lifetime dependencies at runtime via mutation. If the runtime doesn’t auto-repair these, per-region RC is either:
    - unsound (dangling pointers when younger regions die), or
    - incomplete (region cycles retained forever if cross-region refs keep regions alive).
    Auto-repair turns mutation-time lifetime changes into a guaranteed-safe operation.
  Policy requirements (must be explicit in the doc):
    1. **No user-visible syntax:** no `(share v)` or opt-in primitives.
    2. **No debug abort as the contract:** aborts may exist as optional instrumentation, but the language/runtime contract is “always repairs automatically”.
    3. **Deterministic decision:** given the same configuration and same allocation sizes, the repair choice should be predictable (for reproducible benchmarks).
    4. **No STW GC:** repair must be local (transmigrate) or structural (merge), never heap scanning.
  Size heuristic (recommended to document and implement in this order, from simplest to more precise):
    - Heuristic H0 (default): compare **region allocated bytes**:
      - if `bytes_allocated(R_val) >= MERGE_THRESHOLD_BYTES` ⇒ merge `R_val` into `R_obj`
      - else ⇒ transmigrate the stored value into `R_obj`
    - Heuristic H1 (optional): compare **estimated reachable size** from the stored root, with a bounded traversal budget:
      - stop after N nodes/bytes and treat as “large” if budget exceeded
      - NOTE: this is still not a GC (it only traverses the value graph being stored), but it is more complex and must be justified by perf wins.
  Merge safety constraints (must be explicit):
    - If merge cannot be performed safely (e.g., region ownership/threading constraints, or region is already shared in an incompatible way), then the runtime must fall back to transmigration (correctness over performance).
  Verification plan:
    - Provide 3 concrete behavioral examples in the doc:
      1. small young list stored into older dict ⇒ transmigrate
      2. large young region stored into older structure ⇒ merge
      3. merge not permitted ⇒ transmigrate fallback (still safe)

- [TODO] Label: R2-mutation-site-inventory
  Objective: Inventory every runtime mutation primitive that can create cross-region edges and therefore must use the store barrier / auto-repair policy.
  Reference: `runtime/docs/REGION_DEPENDENCY_RULES.md` (auto-repair contract)
  Where:
    - List and link to the exact functions implementing:
      - `set-car!`, `set-cdr!`
      - array/vector set
      - dict/map set
      - any internal structure mutations (closure/env updates, object field sets)
  Why:
    Region-cycle prevention fails if even one mutation path bypasses the barrier.
  Done means:
    - The inventory is complete and includes “how to test” each mutation site.
    - The inventory explicitly says “all must call the same store helper”.

- [TODO] Label: R2-auto-repair-tests
  Objective: Define a regression test plan proving auto-repair prevents region cycles without changing language syntax.
  Reference: `runtime/docs/REGION_DEPENDENCY_RULES.md`
  Where:
    - Planned tests: `runtime/tests/` (new test file, e.g. `test_region_cycle_prevention.c`)
  Test plan (minimum):
    1. **Younger→older store repairs safely (no dangling pointers):**
       - Create `R_old` and `R_young`.
       - Allocate container in `R_old`, value in `R_young`.
       - Perform mutation store.
       - Exit `R_young` scope.
       - Assert container still points to a valid object/value (either migrated or merged).
    2. **Heuristic branch coverage:**
       - Configure threshold low to force merge; verify merge path taken.
       - Configure threshold high to force transmigrate; verify transmigrate path taken.
    3. **No region cycles retained:**
       - Construct scenario that would create `R_old ↔ R_young` cycle without repair.
       - Verify that after repair the graph does not contain any pointer into a dead region and region liveness counters drop as expected.
  Verification commands:
    - `make -C runtime/tests test`
    - Add ASAN/TSAN commands once sanitizer gates exist (Issue 9).

------------------------------------------------------------------------------

## Issue 3 — Region Ownership + Cross‑Thread Handoff (Concurrency Robustness)

Pick a threading contract that matches “robustness + tooling”.

- [TODO] Label: R3-threading-contract-spec
  Objective: Specify the threading/ownership model for regions and objects (recommended: single-owner regions; cross-thread requires handoff/transmigrate).
  Reference: `runtime/docs/REGION_RC_MODEL.md`, `runtime/docs/ARCHITECTURE.md`
  Where:
    - New doc: `runtime/docs/REGION_THREADING_MODEL.md`
    - Update: `runtime/docs/ARCHITECTURE.md` (link)
  Why:
    TSAN usefulness and overall robustness depend on having a clear contract:
    - either transmigrate is called under a runtime lock,
    - or regions/allocations are thread-safe (harder).
  Spec requirements:
    - Define “region owner” (thread id) conceptually (even if not implemented yet).
    - Define allowed cross-thread operations:
      - channel send/recv of values with region transfer or region inc/dec.
    - Define how Region‑RC interacts with handoff (who decrements external refs).
  Verification plan:
    - Add doc examples showing safe handoff vs unsafe concurrent mutation.

- [TODO] Label: R3-channel-handoff-mechanics
  Objective: Specify how channels transfer values without STW GC and without language-visible share constructs.
  Reference: `runtime/docs/REGION_THREADING_MODEL.md`
  Where:
    - New doc section: `runtime/docs/REGION_THREADING_MODEL.md` (“Channels”)
  Why:
    Channel handoff is a primary real-world escape boundary. If it’s ambiguous, concurrency bugs will dominate.
  Plan options (document both; pick one):
    - Option A: **Ownership transfer** of an entire region (fastest, most robust).
    - Option B: **Transmigrate on send** into receiver’s long-lived region (safe, more copying).
    - Option C: **Shared region** with locks (risky; only if needed).
  Verification plan:
    - Provide at least one test case per option in the spec (source + expected behavior).

------------------------------------------------------------------------------

## Issue 4 — Atomic Policy Layer (Robust Concurrency + Tooling)

Even if we remain C99, concurrency robustness improves dramatically if all atomics go through one auditable layer.

- [TODO] Label: R4-omni-atomic-wrapper-design
  Objective: Design an `omni_atomic.h` abstraction so the runtime can use C11 atomics when available, or `__atomic_*` intrinsics otherwise.
  Reference: `runtime/docs/REGION_THREADING_MODEL.md`
  Where:
    - New header: `runtime/include/omni_atomic.h`
    - New doc: `runtime/docs/ATOMIC_POLICY.md`
  Why:
    Tooling (TSAN) and correctness review require explicit ordering semantics; ad-hoc intrinsics are easy to misuse.
  API sketch (example):
    ```c
    typedef struct { /* impl */ } omni_atomic_u32;
    uint32_t omni_atomic_load_acquire(const omni_atomic_u32*);
    void omni_atomic_store_release(omni_atomic_u32*, uint32_t);
    uint32_t omni_atomic_fetch_add_relaxed(omni_atomic_u32*, uint32_t);
    uint32_t omni_atomic_fetch_sub_acqrel(omni_atomic_u32*, uint32_t);
    ```
  Memory-order policy doc (must include):
    - which counters can be relaxed (most refcount increments)
    - which transitions require acquire/release (dec-to-zero path)
  Verification plan:
    - Add a small unit test (if tests infra permits) for atomic wrapper correctness (single-thread sanity).
    - Add TSAN gate tests later (Issue 9).

------------------------------------------------------------------------------

## Issue 5 — Tethering / Borrowing Hardening (Epoch + Tripwires)

Tethering exists to keep borrowing safe without GC. Robustness depends on making violations loud.

- [TODO] Label: R5-region-epoch-borrow-tripwires
  Objective: Specify a region “epoch” mechanism and BorrowedRef validation in debug builds.
  Reference: `runtime/docs/REGION_RC_MODEL.md`, `runtime/docs/REGION_THREADING_MODEL.md`
  Where:
    - New doc section: `runtime/docs/REGION_TETHERING.md`
    - Planned code: `runtime/src/memory/region_core.{c,h}`, borrow/tether APIs
  Why:
    Without epoch-style validation, stale borrows become silent UAFs; tooling catches them only sometimes.
  Spec plan:
    - Each region has `uint64_t epoch`.
    - On region destroy: `epoch++` and (debug) poison memory.
    - BorrowedRef captures `(region_id, epoch_snapshot, tether_depth/handle)`.
    - On dereference (debug): assert region still alive and epoch matches.
  Verification plan:
    - Tests:
      - borrowing across tether end triggers abort in debug
      - borrow after region exit triggers abort in debug

------------------------------------------------------------------------------

## Issue 6 — Regions (Allocator Policy, Inline Buffer, Accounting)

This is where you can win both robustness and performance without touching user code.

- [TODO] Label: R6-region-accounting-quotas
  Objective: Add a plan for per-region and global accounting (bytes, allocations, peak), and optional debug quotas.
  Reference: `runtime/docs/REGION_RC_MODEL.md`
  Where:
    - New doc: `runtime/docs/REGION_ACCOUNTING.md`
    - Planned code: `runtime/src/memory/region_core.c`, `runtime/include/omni.h`
  Why:
    Regions can “silently become long-lived” (retention cliff). Accounting makes issues visible.
  Plan details:
    - Track:
      - bytes allocated in region
      - chunk count
      - inline_buf usage
      - peak bytes
    - Optional debug quota:
      - abort with message “region exceeded quota; likely escape/retention”.
  Verification plan:
    - Provide a sample program and expected diagnostic output.

- [TODO] Label: R6-inline-buf-policy-and-domain
  Objective: Clarify inline_buf policy and ensure “in-region domain” includes inline allocations consistently (for bitmap, forwarding, and external-root classification).
  Reference: Phase 31 inline_buf coverage notes in `TODO.md`, `runtime/docs/CTRR_TRANSMIGRATION.md`
  Where:
    - `runtime/docs/REGION_DEPENDENCY_RULES.md` (domain note)
    - `runtime/docs/REGION_RC_MODEL.md` (domain note)
  Verification plan:
    - Tests must include objects allocated in inline_buf and ensure transmigration and domain checks behave correctly.

------------------------------------------------------------------------------

## Issue 7 — Transmigration: Structural Work (Correctness + Performance)

These refer to phases already in `TODO.md` (40–47). This file records the “why” and the dependency structure so it’s visible in one place.

- [TODO] Label: R7-transmigrate-edge-choke-point-priority
  Objective: Treat “single edge rewrite choke point” (Phase 42) as the prerequisite for all future perf work to prevent regression churn.
  Reference: `TODO.md` Phase 42
  Verification plan:
    - When Phase 42 is implemented, require a check that no edge path allocates WorkItem nodes in hot path.

- [TODO] Label: R7-forwarding-as-visited-priority
  Objective: Prioritize “forwarding table as visited+remap” (Phase 40) to reduce per-edge bookkeeping.
  Reference: `TODO.md` Phase 40
  Verification plan:
    - Must run under forced forwarding mode and demonstrate (via counters) that bitmap/remap fallback is not exercised in the hot path.

- [TODO] Label: R7-pair-microkernel-priority
  Objective: Prioritize `TAG_PAIR` microkernel (Phase 41) after Phase 42, to remove both clone and trace indirection for cons-heavy graphs.
  Reference: `TODO.md` Phase 41
  Verification plan:
    - Must include header-invariant tripwires and list/array regression benchmarks.

------------------------------------------------------------------------------

## Issue 8 — Benchmark Protocol (Reproducibility and Reportability)

Tooling matters: without reproducible benchmarks, performance work becomes narrative.

- [TODO] Label: R8-bench-protocol-spec
  Objective: Codify the benchmark protocol (compiler + flags + rebuild steps + reporting) as a doc and ensure every perf phase references it.
  Reference: Phase 39 protocol in `runtime/tests/Makefile`
  Where:
    - New doc: `runtime/docs/BENCH_PROTOCOL.md`
    - Update: `runtime/tests/bench_transmigrate_vs_c.c` (ensure it prints correct compiler/flags; avoid misleading “GCC 4.2.1” output if using clang)
  Why:
    “bench-rel” must be apples-to-apples: benchmark binary and runtime library must be built with consistent flags, or results are not comparable.
  Verification plan:
    - `bench-rel` output must print:
      - actual compiler ID and version
      - actual CFLAGS used to build *both* runtime and bench (or explicitly state if runtime was built differently)
      - mode name

------------------------------------------------------------------------------

## Issue 9 — Tooling Gates: ASAN/TSAN/UBSAN Focused on Memory Model

This is where “tooling matters” becomes concrete.

- [TODO] Label: R9-sanitizer-contract-tests
  Objective: Add focused sanitizer stress tests specifically for region lifecycle + tethering + transmigration invariants.
  Reference: `runtime/tests/Makefile` sanitizer targets, `runtime/docs/REGION_THREADING_MODEL.md`
  Where:
    - `runtime/tests/` (new focused tests)
  Why:
    Sanitizers are most useful when they target well-defined invariants rather than random workloads.
  Plan:
    - ASAN: stress alloc/exit/transmigrate loops; ensure no UAF/leaks.
    - UBSAN: ensure no UB in bitmap/index math.
    - TSAN: only if threading contract is explicit; otherwise TSAN will produce false positives.
  Verification plan:
    - Provide exact commands to run and what “pass” means (no sanitizer reports within the scope of the focused tests).

------------------------------------------------------------------------------

## Issue 10 — Repo Hygiene (Build Artifacts Policy)

This is not “runtime code” but it materially affects robustness of the workflow.

- [TODO] Label: R10-build-artifact-policy
  Objective: Stop tracked build artifacts from polluting diffs and accidental commits, or document/enforce a strict policy if artifacts must remain tracked.
  Reference: `TODO.md` Phase 48
  Where:
    - Decide policy in `runtime/docs/DEV_HYGIENE.md` (new) and align with JJ workflow.
  Verification plan:
    - After running tests/bench, `jj status` should not show modified tracked binaries/objects under normal workflows.

------------------------------------------------------------------------------

## Issue 11 — Internet‑Informed Techniques (Adopt What Helps, Without Violating CTRR/Region‑RC)

This issue captures external techniques worth incorporating **only if** they align with:
- CTRR (compile-time scheduling of region lifetimes and escape repair points),
- Region‑RC (per-region external reference counting, no heap-wide tracing),
- auto-repair on mutation stores (transmigrate for small, merge/coalesce for large),
- no stop-the-world GC,
- no language-visible “share” construct.

### Why this issue exists
We want robustness and performance improvements that have precedent in real systems and the literature, but we must:
1) avoid importing techniques that are secretly “GC in disguise” (heap scanning),
2) avoid techniques that require language surface changes,
3) keep concurrency/tooling explicit and testable.

---

### 11.1 Region‑RC precedent: “RC” safe C dialect (external pointer counting)

- [TODO] Label: R11-rc-model-crosscheck
  Objective: Cross-check OmniLisp’s Region‑RC model against the “RC” safe C dialect’s definition of “external pointers”, and extract what applies to OmniLisp.
  Reference:
    - RC overview: https://www.barnowl.org/research/rc/index.html
    - `runtime/docs/REGION_RC_MODEL.md` (to be written by Issue 1)
  Where:
    - Add appendix: `runtime/docs/REGION_RC_MODEL.md` (“Related work: RC dialect; differences in a mutable Lisp”)
  Why:
    RC’s core claim is: “per-region refcount of pointers not stored within the region” prevents premature frees. That maps directly to our external_rc concept, but OmniLisp needs mutation auto-repair, so the store barrier is non-optional.
  What to write:
    - Define “external pointer” in OmniLisp terms (stored outside region, or crossing region ownership boundary).
    - Explain why OmniLisp cannot rely on programmer-visible `alias_refptr/drop_refptr` style APIs (no surface changes).
    - Explain why mutation barrier + auto-repair replaces RC’s “abort on delete with non-zero refcount”.
  Verification plan:
    - Doc section includes at least 2 concrete examples (illegal younger→older store; channel handoff) showing how OmniLisp handles cases RC handles via explicit operations.

---

### 11.2 Non‑lexical regions, splitting, and sized allocations (compile-time inspiration)

- [TODO] Label: R11-nonlexical-region-inference-notes
  Objective: Extract actionable ideas from “Better Static Memory Management” and Spegion (non-lexical regions, splitting, sized allocations) and map them to CTRR roadmap items.
  Reference:
    - Aiken/Fähndrich/Levien 1995 tech report: https://digicoll.lib.berkeley.edu/record/139069
    - Tofte/Talpin 1997: https://www.sciencedirect.com/science/article/pii/S0890540196926139
    - Spegion (ECOOP 2025 LIPIcs): https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2025.15
    - `docs/CTRR.md`
  Where:
    - New doc: `docs/CTRR_REGION_INFERENCE_ROADMAP.md`
  Why:
    Even with runtime repair (transmigrate/merge), compile-time improvements reduce the rate of repairs and shrink region lifetimes (robustness + performance).
  What to write (concrete CTRR-aligned ideas; no new language syntax):
    - Non-lexical region end insertion driven by liveness (CTRR already does liveness; document explicit goals).
    - “Splittable regions” as an internal representation trick: allow the compiler/runtime to create subregions under one logical region, enabling later coalescence decisions (ties to your size heuristic).
    - Sized allocations: use runtime accounting to inform compile-time heuristics (e.g., predict big allocations; allocate directly into parent/global to avoid later repair).
  Verification plan:
    - Include 3 “before/after” pseudo examples: (1) early region end, (2) avoiding transmigration by allocating in outliving region, (3) how split regions could reduce copying during coalescence.

---

### 11.3 Pool/arena practice: shortest-lived pool guidance + “freeze/thaw” compaction analogue

- [TODO] Label: R11-pool-practice-guidance
  Objective: Incorporate “shortest-lived pool” guidance (from APR pools) into OmniLisp docs and diagnostic tooling, and evaluate whether a safe compaction/coalescence step (freeze/thaw style) applies to long-lived regions.
  Reference:
    - APR pools guidance: https://perl.apache.org/docs/2.0/user/performance/prevent.html
    - APR::Pool overview: https://perl.apache.org/docs/2.0/api/APR/Pool.html
    - MemArena freeze/thaw coalescence: https://docs.trafficserver.apache.org/en/10.1.x/developer-guide/internal-libraries/MemArena.en.html
    - `runtime/docs/REGION_ACCOUNTING.md` (Issue 6)
  Where:
    - Update: `runtime/docs/REGION_ACCOUNTING.md` (“Retention cliffs and pool lifetime mismatches”)
    - New doc: `runtime/docs/REGION_COALESCE_POLICY.md` (if warranted)
  Why:
    Pool systems show the main failure mode of region allocators in real code: allocating into too-long-lived pools causes “leaks by retention”. Your auto-repair heuristic is exactly a runtime enforcement of “pick shortest lived pool”.
  What to do (tasks, not implementation here):
    1. Define diagnostics:
       - warn if a region becomes long-lived and is receiving many allocations that never escape (retention smell)
    2. Evaluate a “coalesce after init” operation for long-lived regions:
       - Not a GC: only done at explicit safe points (end of init phase).
       - Coalesce by copying known-live data into one chunk and releasing old chunks (like MemArena freeze/thaw).
       - Must be opt-in and benchmarked; may be DEFER if complexity outweighs gains.
  Verification plan:
    - Provide a toy “server init then steady-state” scenario and expected accounting output showing reduced fragmentation after coalesce.

---

### 11.4 Concurrency: Safe Memory Reclamation (SMR) for internal runtime structures (not heap GC)

These techniques are for **internal concurrent data structures** (e.g., lock-free queues/maps used by the runtime). They do not replace CTRR/Region‑RC, but can reduce locking and improve robustness.

- [TODO] Label: R11-rcu-qsbr-evaluation
  Objective: Evaluate whether QSBR/Userspace RCU-style quiescent states can be mapped onto OmniLisp’s tethering/borrow windows to protect internal read-mostly structures (metadata tables, interned symbol tables) without STW GC.
  Reference:
    - Userspace RCU docs: https://liburcu.org/
    - QSBR overview (LWN): https://lwn.net/Articles/573424/
    - `runtime/docs/REGION_THREADING_MODEL.md` (Issue 3)
  Where:
    - New doc: `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md`
  Why:
    If concurrency matters, we need a plan for safely reclaiming nodes in internal structures without global locks. QSBR is fast but requires explicit quiescent state reporting — conceptually similar to “no borrows held / tether windows ended”.
  What to define:
    - Which runtime structures would benefit (list them explicitly).
    - Where quiescent states would be reported (end of bytecode instruction? end of GC-free safe point? end of tether?).
    - Why this is not heap GC: only internal DS nodes are reclaimed.
  Verification plan:
    - Provide at least one test scenario in the doc: concurrent reads, writer retires nodes, reclaim after quiescent period.
    - Tie to TSAN expectations (Issue 9).

- [TODO] Label: R11-hazard-pointers-and-variants-review
  Objective: Review hazard pointers and newer variants (e.g., publish-on-ping) as alternatives to QSBR for internal runtime structures, focusing on overhead vs robustness tradeoffs.
  Reference:
    - Publish on Ping (2025): https://arxiv.org/abs/2501.04250
    - Hyaline (2019): https://arxiv.org/abs/1905.07903
    - `runtime/docs/ATOMIC_POLICY.md` (Issue 4)
  Where:
    - `runtime/docs/SMR_FOR_RUNTIME_STRUCTURES.md` (appendix “Alternatives”)
  Why:
    QSBR is fast but can retain memory if threads don’t report quiescent states. Hazard-pointer families are more robust but can impose per-read overhead. We need an explicit choice per structure.
  What to produce:
    - A decision matrix for:
      - read-mostly global tables (prefer QSBR/RCU)
      - lock-free linked structures with heavy traversal (consider variants that reduce per-read fences)
  Verification plan:
    - Include a minimal microbenchmark plan for one internal DS (not the heap), gated by Issue 8 benchmark protocol.

------------------------------------------------------------------------------

## External References (Internet / Literature)

These are the external sources referenced in Issue 11 tasks above. Prefer primary sources (papers, official docs) when implementing.

1. RC safe C dialect (region external pointer counting): https://www.barnowl.org/research/rc/index.html
2. Cyclone manual section on regions: https://www.cs.cornell.edu/Projects/cyclone/online-manual/main-screen008.html
3. Userspace RCU (liburcu) official site/docs: https://liburcu.org/
4. LWN overview of Userspace RCU flavors (QSBR): https://lwn.net/Articles/573424/
5. Hyaline SMR paper (2019): https://arxiv.org/abs/1905.07903
6. Publish on Ping SMR paper (2025): https://arxiv.org/abs/2501.04250
7. Better Static Memory Management (Aiken/Fähndrich/Levien, 1995): https://digicoll.lib.berkeley.edu/record/139069
8. Region-Based Memory Management (Tofte/Talpin, 1997): https://www.sciencedirect.com/science/article/pii/S0890540196926139
9. Spegion (ECOOP 2025, LIPIcs): https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2025.15
10. mod_perl: Proper Memory Pools Usage: https://perl.apache.org/docs/2.0/user/performance/prevent.html
11. mod_perl: APR::Pool API: https://perl.apache.org/docs/2.0/api/APR/Pool.html
12. Apache Traffic Server MemArena (freeze/thaw coalescence): https://docs.trafficserver.apache.org/en/10.1.x/developer-guide/internal-libraries/MemArena.en.html
