 # Project Rules
# Agent Operational Guidelines

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
- **NOT Garbage Collection:** You are prohibited from implementing stop-the-world GC, mark/sweep, or runtime cyclic collection.
- **Statically Scheduled:** All memory deallocation decisions are made at compile-time.
- **Explicit Operations:** Your generated C code must emit `region_create`, `region_exit`, `transmigrate`, and `region_tether_start/end` based on static analysis.
- **Cleanup Phase:** Variables captured by closures/lambdas MUST NOT be freed in the parent scope; they must escape via transmigration.
- **Scanners:** Treat `scan_List()` and similar functions as **traversal utilities** for debugging and verification, never as a collector.

## 📋 Task & TODO Management (Agent-Proofing)
- **Append-Only Phases:** Add new phases to `TODO.md` as `## Phase NN` at the end of the file. Never renumber existing phases.
- **Zero-Context Clarity:** Every task must be implementable by a developer with no prior knowledge of the codebase.
- **Granular Requirements:** Every task must include:
    - **Reference:** Exact doc path (e.g., `runtime/docs/CTRR_TRANSMIGRATION.md`).
    - **Context:** The "Why" behind the architectural change.
    - **Implementation Details:** Snippets of C/Lisp, specific file paths, and logic flows.
    - **Verification:** A specific command and expected output.
- **Status Hygiene:** Use `TODO`, `IN_PROGRESS`, `DONE`, or `N/A`. Provide a reason for `N/A`.

## 🧪 Development & Testing Rigor
- **Test-Driven Development (TDD):** **NO TEST, NO CHANGE.** Write failing tests first. Define golden files/assertions before implementation.
- **Test Integrity:** Never simplify a test to make it pass. If a test fails, the implementation is broken.
- **Commit Workflow:** Use `jujutsu` (`jj`) with a **squash workflow**. Every completed task requires a dedicated squash with an imperative commit message.
- **Self-Documenting Code:** Over-comment logic, especially surrounding transmigration and escape boundaries, to ensure absolute clarity.

## ⚖️ Critical Interaction & Design
- **Constructive Criticism:** Do not rubber-stamp user ideas. Actively seek flaws, edge cases, and architectural inconsistencies. Present counterarguments BEFORE agreeing.
- **Ambiguity Boundary:** If asked for an opinion or clarification, **TEXT ONLY.** Do not use tools or modify code until an explicit "Implement" command is received.
- **File Sync:** `AGENTS.md`, `GEMINI.md`, and `CLAUDE.md` must be kept 100% identical. Update all simultaneously.

## 📂 Key Reference Mapping
| Component | Relevant Specification |
| :--- | :--- |
| **Memory Model** | `docs/CTRR.md` & `runtime/docs/CTRR_TRANSMIGRATION.md` |
| **Lisp Syntax** | `SYNTAX.md` |
| **Advanced Logic** | `docs/ADVANCED_REGION_ALGORITHMS.md` |
| **Validation** | `tests.sh` (Current: 14 regression tests) |
