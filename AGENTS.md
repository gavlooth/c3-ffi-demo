 # C Project Rules
- **Tooling:** Use the `c-lang-tools` MCP for all symbol lookups.
- **Memory Safety:** Always check for `NULL` after `malloc`. 
- **Style:** Follow the Linux Kernel style guide (Tabs, 80-char limit).
- **Thinking Mode:** Keep `preserved_thinking: true` to remember architectural decisions between debugging steps.
- **Break down tasks into steps using the sequential thinking tool.
- Do not provide a response until you have verified the logic against potential edge cases.

# OmniLisp C Scratch - CTRR Memory Model

## CRITICAL: CTRR is NOT Garbage Collection

**CTRR (Compile-Time Region Reclamation)** is a **compile-time scheduled, region-based**
memory model.
It does **NOT** use runtime garbage collection.
### Core Principle

The compiler analyzes the program and **statically schedules region lifetimes**
and explicit runtime operations:

- `region_create(...)` / `region_exit(...)` for scope lifetimes
- `transmigrate(...)` at escape boundaries
- `region_tether_start/end(...)` for borrow windows

All *deallocation decisions* are made at compile time; the runtime does not run a
heap-wide collector.

### Target: C99 + POSIX (+ common extensions)

The goal is to emit **C99 + POSIX** code while allowing practical low-level
extensions commonly supported by GCC/Clang:
- **C99** for the core language (no C11 features like `<stdatomic.h>`)
- **POSIX pthreads** for thread synchronization (`pthread_mutex_t`, `pthread_rwlock_t`)
- **Compiler extensions** where they are performance-critical and ubiquitous (e.g., TLS via `__thread`, atomics via `__atomic_*`)
- Compile with: `gcc -std=c99 -pthread` or `clang -std=c99 -pthread`

You can use other algorithms alongside CTRR as long as they don't do "stop the world". So mark/sweep and traditional garbage collection is out of the question
as well as "cyclic collection" algorithms that stop the world 
```
WRONG: Runtime GC that scans heap and collects garbage
RIGHT: Compiler injects region_exit/transmigrate/tether operations at compile time based on static analysis
```

### Terminology note: ASAP is a paper term

The literature uses **ASAP (As Static As Possible)** for a specific approach.
In OmniLisp, **CTRR** is the project’s term for the region-based contract.

### What CTRR Does

1. **CLEAN Phase** (compile-time)
   - Analyzes variable lifetimes statically
   - Schedules `region_exit()` at scope exit (or earlier based on liveness)
   - Inserts `transmigrate(...)` at escape boundaries (return/capture/global store)
   - Variables captured by closures are NOT freed in the parent scope (they escape)

2. **Liveness Analysis** (compile-time)
   - Tracks last use of each variable
   - Can end region lifetimes earlier than scope exit if a region becomes dead

3. **Escape Analysis** (compile-time)
   - `ESCAPE_TARGET_NONE`: Value stays local → can stack-allocate or allocate in a scratch region
   - `ESCAPE_TARGET_PARENT`: Escapes to parent scope → allocate in parent region or transmigrate
   - `ESCAPE_TARGET_RETURN`: Escapes via return → transmigrate into caller/outliving region
   - `ESCAPE_TARGET_GLOBAL`: Escapes to global/module scope → allocate in a global/outliving region (or transmigrate)

4. **Capture Tracking** (compile-time)
   - Identifies variables captured by lambdas/closures
   - These variables must NOT be freed/exited in the parent scope

### What Scanners Are For

The `scan_List()` function is a **traversal utility**, NOT a garbage collector:
- Debugging (checking what's reachable)
- Manual reference counting updates
- Runtime verification in debug builds
- Runtime verification of the Region Closure Property (no pointers into dead regions)
- Marking for other static analyses

### Deferred Free List

The `FREE_LIST` is an optimization for **batching frees**, not a GC mechanism:
- Prevents issues during complex traversals
- Allows flushing at safe points
- NOT for mark-sweep collection

## Implementation Status

### Completed Optimizations

| Phase | Feature | Description |
|-------|---------|-------------|
| 1 | VarUsage Infrastructure | Track variable usage, escape, capture status |
| 2 | Liveness Analysis | Free at last-use, not just scope-end |
| 3 | Escape Analysis | Stack-allocate non-escaping values |
| 4 | Capture Tracking | Don't free lambda-captured variables |
| 5 | Dynamic Free List | Linked list instead of fixed array |
| 6 | Type-Aware Scanners | Traversal utilities (NOT GC) |
| 7 | Multi-Binding Let | Support `(let ((x 1) (y 2)) body)` |
| 8 | Field-Aware Scanners | Skip non-pointer fields in traversal |

## TODO/Task Flow (Directive)

All agents must use this flow for TODOs and tasks, including **N/A** status.
Tasks must be written for developers who **do not know this codebase** but are **adept builders**.
That means each task must be explicit about *where*, *what*, *how*, and *done means*.
use SYNTAX.md so you understand OmniLisp syntax
TODO.md must be updated when task are complete

### High-Granularity Task Directive (MANDATORY)

Every task added to `TODO.md` MUST be written with sufficient detail that a developer with **zero context** could implement it immediately without asking clarifying questions.

### Issue / Phase Authoring Directive (Agent-Proof)

**Do not insert new “issues” (phases) near the top of `TODO.md`.** Phases are numbered and referenced by number in discussions, commits, and docs, so the file must remain stable and append-oriented.

Rules (mandatory):
1. **Append-only numbering:** When creating a new phase, add it as `## Phase NN: ...` using the next available integer `NN`. Never renumber existing phases.
2. **No duplicates:** There must be exactly one header for each phase number. If a phase needs revision, append an “Amendment” subsection inside that phase instead of creating a second copy elsewhere.
3. **Dependency order:** Phases must be ordered top-to-bottom by dependency (earlier phases provide prerequisites/invariants for later phases).
4. **Status required:** Every task line must be marked as one of: `TODO`, `IN_PROGRESS`, `DONE`, or `N/A`. If `N/A`, include a one-line reason. Never delete old tasks; mark them `N/A` instead.

Required “agent-proof” structure for new phases/tasks:
- **Objective:** 1–2 sentences describing the concrete outcome.
- **Reference (read first):** exact doc paths that explain the theory/contract (e.g. `runtime/docs/CTRR_TRANSMIGRATION.md`).
- **Constraints:** explicitly restate “no stop-the-world GC”, “no language-visible sharing primitives”, and any phase-specific invariants.
- **Subtasks (P0/P1/P2…):** each subtask must include:
  - **Label:** short, unique, grep-able (e.g. `T38-hot-tag-inline-dispatch`).
  - **Where:** exact file paths to modify.
  - **Why:** the architectural reason; what breaks or is slow today.
  - **What to change:** numbered steps (what to add/remove/change).
  - **Implementation details:** include pseudocode in C/Lisp and name the key structs/functions/macros to touch.
  - **Verification plan:** at least one concrete test case (source + expected behavior) and the exact command(s) to run.
  - **Performance plan (if perf-related):** baseline numbers + a measurable target (e.g. “`bench` 10k list ns/op improves ≥ 20%”).

**A task is INCOMPLETE if it lacks:**
1.  **Reference:** A link to the specific documentation file (e.g., `docs/BRANCH_LEVEL_REGION_NARROWING.md`) that explains the theory/design. **The developer is explicitly required to study this document before starting.**
2.  **Context/Why:** Explain the architectural goal. *Why* are we doing this? What problem does it solve?
3.  **Implementation Details:**
    *   **File Paths:** Exact files to modify.
    *   **Data Structures:** The exact C structs, Enums, or Lisp forms to be added. **Include code snippets.**
    *   **Logic Flow:** A step-by-step description of the algorithm or logic changes.
4.  **Verification Plan:** A concrete test case (source code + expected output) that proves success.

**BAD Example:**
```text
- [TODO] Label: T-fix-escape
  Objective: Fix escape analysis for branches.
  Where: analysis.c
  What: Update the escape analysis to handle if statements.
```

**GOOD Example:**
```text
- [TODO] Label: T1-analysis-scoped-escape
  Objective: Implement hierarchical, branch-level escape analysis to support "Region Narrowing".
  Reference: docs/BRANCH_LEVEL_REGION_NARROWING.md (Study Section 3 "Allocation Matrix" carefully)
  Where: csrc/analysis/analysis.h, csrc/analysis/analysis.c
  Why:
    Currently, escape analysis is function-global. We need to know if a variable escapes *its specific branch* to enable stack allocation in non-escaping branches.
  
  What to change:
    1.  **Define Scope Hierarchy:** Track nested scopes.
    2.  **Scoped Escape Tracking:** Track variable escape status *per scope*.

  Implementation Details:
    *   **Structs (analysis.h):**
        ```c
        typedef enum { ESCAPE_TARGET_NONE, ESCAPE_TARGET_PARENT, ... } EscapeTarget;
        typedef struct ScopedVarInfo { ... } ScopedVarInfo;
        ```
    *   **Logic (analysis.c):**
        *   Implement `omni_scope_enter()` and `omni_scope_exit()`.
        *   Update `analyze_expr` to call these around branch bodies.

  Verification:
    *   Input: `(if true (let [x 1] x) 0)` -> x should be ESCAPE_TARGET_NONE.
    *   Input: `(if true (let [x 1] (return x)) 0)` -> x should be ESCAPE_TARGET_RETURN.
```

### Status Rules
1. **Capture**: Convert discovered TODOs into explicit tasks with short, unique labels.
2. **Order**: Sort tasks top-to-bottom by dependency.
3. **Status**: Every task must be marked as one of: `TODO`, `IN_PROGRESS`, `DONE`, or `N/A`.
4. **N/A rule**: If a task is not applicable, mark it `N/A` and add a one-line reason. Do not delete it.
5. **Closeout**: Before finishing, ensure every task is `DONE` or `N/A`.

## Constructive Criticism Directive (MANDATORY)

**Always challenge the user with constructive criticism.** Do not simply agree or validate ideas without critical evaluation.

- When the user proposes a design, implementation, or approach, **actively look for flaws, inconsistencies, or better alternatives**
- Present counterarguments, edge cases, and potential issues **before** agreeing
- If asked "what do you think?", provide honest analysis including negatives, not just positives
- Question assumptions and conventions - ask "why?" and "is there a better way?"
- If two approaches exist, argue both sides before recommending one
- Never rubber-stamp a decision - the user values being challenged over being validated

This directive exists because **rigorous debate produces better designs** than agreement.

## Confirm Ambiguity/Expansion (MANDATORY)

**When asked for an opinion, clarification, or "what do you think?", do NOT modify any files or execute implementation steps.** 

- Provide the analysis, explanation, or opinion in text only.
- Wait for an explicit instruction (e.g., "Implement this", "Update the spec") before using tools to change the project state.
- Maintain a clear boundary between the **design discussion** and **implementation action**.

## Test-Driven Development (NON-NEGOTIABLE MANDATE)

All agents (Claude, Gemini, Codex) MUST strictly adhere to **test-driven development** WITHOUT EXCEPTION:

1. **Write tests first** BEFORE any code change, behavior change, or new feature.
2. **Define expected outputs** (golden files or assertions) and verify they fail before implementation.
3. **Run the full test suite** after every change and report the exact results.
4. **NO TEST, NO CHANGE.** Any change without accompanying tests is a violation of the core development process, unless the task is explicitly marked `N/A` for testing with a comprehensive justification.
5. **Regression Verification**: Every bug fix MUST include a reproduction test that failed before the fix and passes after.

## Code Commenting Directive

When writing or modifying code, include **a lot of comments** so the code is **absolutely self-documenting**.

## Sync Directive

`AGENTS.md`, `GEMINI.md` and `CLAUDE.md` must be kept **identical**. Any update to one file must be applied to the other in the same change.

## Documentation Directive

For every completed task:
- Update the relevant documentation or add a brief note in `docs/` describing the change.
- If no documentation changes are needed, explicitly mark the task `N/A` for documentation with a one-line reason.
    <!-- * Start the implementation, ancor yourselve to the refactor plan. Begin. you have to commit and mark items as ready for review   -->
## Jujutsu Commit Directive

Use jujutsu so that the state hash always a hash
use squash workflow
For every completed task:
- Create a dedicated jujutsu squash with a clear, imperative message (e.g., `Add effect handler core`).

## Test Policy (Directive)

**NEVER simplify tests to make them pass. Fix the underlying code instead.**

- Tests define the contract. If a test fails, the **implementation is wrong**, not the test.
- Tests should only change if the test itself is **incorrect** (wrong expected value, flawed logic).
- When encountering test failures:
  1. **Diagnose** the root cause in the implementation.
  2. **Fix** the implementation to satisfy the test.
  3. **Never** weaken, skip, or simplify tests to avoid fixing bugs.
- If a test is genuinely wrong, document **why** it was wrong before changing it.
- Treat test failures as **bugs to fix**, not obstacles to remove.

## Key Files

- `docs/ADVANCED_REGION_ALGORITHMS.md` - Advanced transmigration/tethering plans
- `tests.sh` - Regression tests (14 tests)
- `examples/demo.omni` - Example programs

## References

- `docs/CTRR.md` - CTRR contract (project spec)
- `runtime/docs/CTRR_TRANSMIGRATION.md` - transmigration contract (runtime spec)
- *ASAP (paper term): As Static As Possible memory management* (Proust, 2017)
- *Collapsing Towers of Interpreters* (Amin & Rompf, POPL 2018)
- *Better Static Memory Management* (Aiken et al., PLDI 1995)
- *Region-Based Memory Management* (Tofte & Talpin, 1997)
