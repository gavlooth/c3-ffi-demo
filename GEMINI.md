# OmniLisp C Scratch - ASAP Memory Management

## CRITICAL: ASAP is NOT Garbage Collection

**ASAP (As Static As Possible)** is a **compile-time static memory management** strategy.
It does NOT use runtime garbage collection.
### Core Principle

The compiler analyzes the program and **statically inserts `free()` calls** at the optimal
points during code generation. All deallocation decisions are made at compile time.
There is already an implementation in ~/code/omnilisp_c_scratch.

### Target: C99 + POSIX

The goal is to emit **ANSI C99 + POSIX** code:
- **C99** for the core language (no C11 features like `<stdatomic.h>`)
- **POSIX pthreads** for thread synchronization (`pthread_mutex_t`, `pthread_rwlock_t`)
- Compile with: `gcc -std=c99 -pthread` or `clang -std=c99 -pthread`

You can use other algorithms along ASAP as long as they don't do "stop the world". So mark/sweep and traditional garbage collection is out of the question
as well as "cyclic collection" algorithms that stop the world 
```
WRONG: Runtime GC that scans heap and collects garbage
RIGHT: Compiler injects free_obj(x) at compile time based on static analysis
```

### What ASAP Does

1. **CLEAN Phase** (compile-time)
   - Analyzes variable lifetimes statically
   - Injects `free_obj()` calls at scope exit (or earlier based on liveness)
   - Variables captured by closures are NOT freed (ownership transfers to closure)

2. **Liveness Analysis** (compile-time)
   - Tracks last use of each variable
   - Can free earlier than scope exit if variable is dead

3. **Escape Analysis** (compile-time)
   - `ESCAPE_NONE`: Value stays local → can stack-allocate
   - `ESCAPE_ARG`: Escapes via function argument → heap-allocate
   - `ESCAPE_GLOBAL`: Escapes to return/closure → heap-allocate, careful with freeing

4. **Capture Tracking** (compile-time)
   - Identifies variables captured by lambdas/closures
   - These variables must NOT be freed in parent scope

### What Scanners Are For

The `scan_List()` function is a **traversal utility**, NOT a garbage collector:
- Debugging (checking what's reachable)
- Manual reference counting updates
- Runtime verification in debug builds
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

### Required Task Granularity & Detail
Each task must include **all** of the following:
- **Label**: short unique ID (e.g., `T1-src-spans`).
- **Objective**: 1 sentence describing the end state.
- **Where**: file paths and (if known) functions or modules to touch.
- **What to change**: concrete actions (add struct X, extend enum Y, emit call Z).
- **How to verify**: exact command or manual step (test, sample input, expected output).
- **Acceptance**: 1–3 bullet points that define “done”.

Break work into **small, reviewable tasks**:
- Prefer tasks that touch **1–3 files**.
- Prefer tasks that can be completed in **≤ 1 day** by a new contributor.
- Split tasks if they combine unrelated concerns (e.g., “parser spans” vs “diagnostic rendering”).

### Task Template (Required)
```
- [STATUS] Label: T#-short-name
  Objective: <one sentence end state>
  Where: <paths + key functions/modules>
  What to change:
    - [ ] <implementation step 1>
    - [ ] <implementation step 2>
  (Optional) Implementation Checklist:
    - <concrete action 1>
    - <concrete action 2>
  How to verify: <command or manual step + expected outcome>
  Acceptance:
    - <criterion 1>
    - <criterion 2>
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

- *ASAP: As Static As Possible memory management* (Proust, 2017)
- *Collapsing Towers of Interpreters* (Amin & Rompf, POPL 2018)
- *Better Static Memory Management* (Aiken et al., PLDI 1995)
- *Region-Based Memory Management* (Tofte & Talpin, 1997)
