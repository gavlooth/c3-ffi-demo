# OmniLisp Agent Guidelines & Project Memory

## 🛠 Tooling & Workspace Protocol
- **LSP Execution:** Use `clangd` for all symbol lookups. Actively monitor and resolve warnings, trivial bugs, and syntax errors as a human developer would.
- **Shell Operations:** Use the `shell` MCP for all command-line executions.
- **Task Planning:** Use `sequential-thinking` to break down complex tasks. Do not skip the planning phase.
- **Architectural Reasoning:** Use `CodeGraphContext` to ensure code is integrated into the system-wide architecture. 
- **Graph Synchronization:** You **must** re-call `CodeGraphContext` after any file modification to synchronize your mental model with the updated codebase.

## 🧠 Memory & Identity (Priority 0)
1. **Initialization:** Every session MUST begin with the literal string: `"Remembering..."`.
2. **Retrieval:** Immediately call the `memory` MCP to retrieve the knowledge graph for `default_user`.
3. **Synthesis:** During interaction, track and categorize:
    - **Identity:** (Job, education, location).
    - **Behaviors:** (Interests, habits).
    - **Goals:** (Aspirations, project targets).
    - **Relationships:** Professional/personal connections (up to 3 degrees of separation).
4. **Update:** Upon completion, create new entities/observations in the "memory" graph to reflect new information.

## 💻 Technical Standards (C/Linux)
- **Memory Safety:** Every `malloc`/`calloc` must be immediately followed by a `NULL` pointer check.
- **Style Guide:** Strictly follow the **Linux Kernel Style Guide**:
    - Use **Tabs** for indentation (8 characters).
    - Maintain an **80-character** line limit.
- **State Management:** Keep `preserved_thinking: true` to maintain architectural context between debugging steps.

## ✅ Completion Criteria
- **Verification:** Do not provide a final response until you have manually verified the logic against edge cases (e.g., overflow, race conditions).
- **Signal:** Never use `[DONE]`. You must use **`[DONE] (Review Needed)`** to indicate the task is finished and verified.

## 🧠 CTRR Memory Model (Core Philosophy)
- **NOT Garbage Collection:** CTRR (Compile-Time Region Reclamation) is a compile-time scheduled, region-based memory model. You are prohibited from implementing stop-the-world GC, mark/sweep, or runtime cyclic collection.
- **Statically Scheduled:** All memory deallocation decisions are made at compile-time.
- **Explicit Operations:** Your generated C code must emit `region_create`, `region_exit`, `transmigrate`, and `region_tether_start/end` based on static analysis.
- **Cleanup Phase:** Variables captured by closures/lambdas MUST NOT be freed in the parent scope; they must escape via transmigration.
- **Scanners:** Treat `scan_List()` and similar functions as **traversal utilities** for debugging and verification, never as a collector.

## 📋 Task & TODO Management (Agent-Proofing)
- **Append-Only Phases:** Add new phases to `TODO.md` as `## Issue N` at the end of the file. Never renumber existing issues.
- **Zero-Context Clarity:** Every task must be implementable by a developer with no prior knowledge of the codebase.
- **Granular Requirements:** Every task must include reference, context, implementation details, and verification plan.
- **Status Hygiene:** Use `TODO`, `IN_PROGRESS`, `DONE`, or `N/A`. Provide a reason for `N/A`.

## 🧪 Development & Testing Rigor
- **Test-Driven Development (TDD):** **NO TEST, NO CHANGE.** Write failing tests first. Define golden files/assertions before implementation.
- **Test Integrity:** Never simplify a test to make it pass. If a test fails, the implementation is broken.
- **Commit Workflow:** Use `jujutsu` (`jj`) with a **squash workflow**. Every completed task requires a dedicated squash with an imperative commit message.
- **Self-Documenting Code:** Over-comment logic, especially surrounding transmigration and escape boundaries, to ensure absolute clarity.

## ⚖️ Critical Interaction & Design
- **Constructive Criticism:** Always challenge the user. Actively look for flaws, inconsistencies, or better alternatives. Present counterarguments BEFORE agreeing.
- **Ambiguity Boundary:** If asked for an opinion or clarification, **TEXT ONLY.** Do not use tools or modify code until an explicit "Implement" command is received.
- **File Sync:** `AGENTS.md`, `GEMINI.md`, and `CLAUDE.md` must be kept 100% identical. Update all simultaneously.

## 📂 Key Reference Mapping
| Component | Relevant Specification |
| :--- | :--- |
| **Memory Model** | `docs/CTRR.md` & `runtime/docs/CTRR_TRANSMIGRATION.md` |
| **Lisp Syntax** | `SYNTAX.md` & `SYNTAX_REVISION.md` |
| **Advanced Logic** | `docs/ADVANCED_REGION_ALGORITHES.md` |
| **Validation** | `tests.sh` (Current: 14 regression tests) |

---

## 📜 Gemini Added Memories (Memento)

- **Codebase Connectivity Improvement (2026-01-14):** Analyzed `DISCONNECTED_EDGES_ANALYSIS.md` and added Issues 8-11 to `TODO.md`. Goals: Resolve 1,909 unresolved edges, complete Effect/Fiber/Typed-Array features, integrate IPGE generation checking, and stabilize build/test harnesses.
- **Strict Character Calculus (2026-01-14):** Implemented `SYNTAX_REVISION.md` Phase 1-5. Support for slot-syntax function definitions, traditional/shorthand defines, typed let bindings, and sequential let with metadata.
- **Region-RC Migration:** The OmniLisp runtime has been fully migrated to the Region-Based Reference Counting (RC-G) architecture. All legacy memory management (SCC, Tarjan, Deferred RC, IPGE) has been removed.
- **GC Prohibition:** Garbage collection is strictly forbidden. Use manual memory management, arenas, or reference counting instead.
- **Type System Alignment:** OmniLisp's type system specifications must align with Julia's type system capabilities (Abstract, Primitive, Struct, Union) using the Uniform Definition syntax `(define {Kind Name ...} ...)`.
- **Scheme-style Define:** The OmniLisp core runtime currently does not support the Scheme-style `(define (f x) body)` syntax in the interpreter (though it is supported in the compiler).
- **Store Barrier:** `omni_store_repair()` is the single mutation choke point across pointer-storing primitives (arrays/dicts/boxes/atoms/channels).
- **Lifetime Ranks:** Regions use per-owner-thread outlives ranks and ancestry metadata to enforce Region Closure Property at mutation time.
- **QSBR Planning:** Detailed QSBR implementation plan written for Channel Queue to enable lock-free concurrency.
- **Build Stabilization:** Canonical build/test commands defined in `docs/BUILD_AND_TEST.md`. `csrc/tests/Makefile` added for compiler unit tests.
- **Purple C Fixes:** Fixed two stack buffer overflow bugs in the Purple language C implementation (include path expansion and constructor pattern matching).
- **OmniLisp Core Fixes:** Fixed bugs BUG-0001 through BUG-0007 in OmniLisp runtime (overflows, bounds, refcounts, etc.).
- **SQL Conventions:** Never use kebab-case for PostgreSQL column names; use snake_case. 'outbox' table already uses snake_case.
- **OssammaNER:** Training optimizations implemented (pre-tokenization, vectorized loss, Output Gate ablation).
- **Clap v4:** Avoid short option name conflicts (e.g., 'host' vs 'help') to prevent aborts.
- **CodeGraph Build:** Build with 'full' feature flag for production. Systemd service expects binary at `~/.local/bin/`.
- **JVM Type Hints:** Always verify JAR signatures via `javap` before applying type hints in Clojure/JVM.

---

# GLM-4.7 Instructions
- Always use <thinking> blocks for deep architectural analysis.
- Prioritize "Ultrathink" logic: decompose every task into 5 steps before writing code.
- Do not provide a response until you have verified the logic against potential edge cases.

# OmniLisp C Scratch - CTRR Memory Model (Technical Detail)

## Core Principle
The compiler analyzes the program and **statically schedules region lifetimes** and explicit runtime operations:
- `region_create(...)` / `region_exit(...)` for scope lifetimes
- `transmigrate(...)` at escape boundaries
- `region_tether_start/end(...)` for borrow windows

## Implementation Status
- **Phase 1-8 Completed:** VarUsage, Liveness Analysis, Escape Analysis, Capture Tracking, Dynamic Free List, Type-Aware Scanners, Multi-Binding Let, Field-Aware Scanners.

## Status Rules
1. **Capture**: Convert discovered TODOs into explicit tasks.
2. **Order**: Sort tasks top-to-bottom by dependency.
3. **Status**: Every task must be marked as `TODO`, `IN_PROGRESS`, `DONE`, or `N/A`.
4. **N/A rule**: If not applicable, mark `N/A` and provide reason.
5. **Closeout**: Ensure every task is `DONE` or `N/A`.
