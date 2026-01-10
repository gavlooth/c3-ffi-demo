# OmniLisp TODO (Restarted)

This TODO list was **restarted on 2026-01-10** after archiving the previous historical backlog.

Backup of the previous `TODO.md` (full history):
- `TODO_ARCHIVE/TODO.md.backup.2026-01-10_120111`

## Review Directive

**All newly implemented features must be marked with `[DONE]` (Review Needed) until explicitly approved by the user.**

- When an agent completes implementing a feature, mark it `[DONE]` (not `[DONE]`)
- `[DONE]` means: code is written and working, but awaits user review/approval
- After user approval, change `[DONE]` to `[DONE]`
- Workflow: `[TODO]` → implement → `[DONE]` → user approves → `[DONE]`

---

## Transmigration Directive (Non-Negotiable)

**Correctness invariant (must always hold):** For every in-region heap object `src` reached during transmigration, `remap(src)` yields exactly one stable destination `dst`, and all pointer discovery/rewrites happen only via metadata-driven `clone/trace` (no ad-hoc shape walkers); external/non-region pointers are treated as roots and never rewritten.

**Do not bypass the metadata-driven transmigration machinery for “fast paths”.**

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
